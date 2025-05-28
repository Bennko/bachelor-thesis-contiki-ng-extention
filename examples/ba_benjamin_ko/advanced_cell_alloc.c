#include "contiki-lib.h"
#include "lib/assert.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/tsch-types.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/mac/tsch/sixtop/sixp.h"
#include "net/mac/tsch/sixtop/sixp-nbr.h"
#include "net/mac/tsch/sixtop/sixp-pkt.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"
#include "./project-conf.h"
#include "sys/log.h"
#include "sf-simple.h"
#include "network_interference_cells.h"

#include "advanced_cell_alloc.h"


static sf_simple_cell_t candidate_cell_list[CAND_CELL_LIST_LEN];
static CircularBuffer blacklist;


void init_advanced_cell_alloc(){
    init_cand_cell_list(candidate_cell_list);
    init_buffer(&blacklist);
}

void init_cand_cell_list(sf_simple_cell_t *cell_list){
    uint8_t slot_check = 1, index = 0;
    uint16_t random_slot = 0;
    uint16_t random_channel = 0;
    do {
        /* Randomly select a slot offset within TSCH_SCHEDULE_CONF_DEFAULT_LENGTH */
        random_slot = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_CONF_DEFAULT_LENGTH;
        random_channel = ((random_rand() & 0xFF)) % NUMBER_OF_CHANNELS;
    
        //prevent it from occupying the same space as the minimal cell
        if(random_slot == 0){
          random_slot++;
        }
    
        /* To prevent repeated slots */
        for(int i = 0; i < index; i++) {
            if(cell_list[i].timeslot_offset != random_slot) {
                /* Random selection resulted in a free slot */
                if(i == index - 1) { /* Checked till last index of link list */
                slot_check = 1;
                break;
                }
            } else {
                /* Slot already present in CandidateLinkList */
                slot_check++;
                break;
            }
        }
    
        /* Random selection resulted in a free slot, add it to linklist */
        if(slot_check == 1) {
            cell_list[index].timeslot_offset = random_slot;
            cell_list[index].channel_offset = random_channel;

            index++;
            slot_check++;
        } else if(slot_check > TSCH_SCHEDULE_DEFAULT_LENGTH) {
            LOG_INFO("advanced-cell:! Number of trials for free slot exceeded...\n");
            return;
        }
    } while(index < CAND_CELL_LIST_LEN);
}

/* Get SF_SIMPLE_MAX_LINKS amount of cells for candidates of add request */
void get_candidate_add(sf_simple_cell_t *cell_list){
    for(int i = 0; i < SF_SIMPLE_MAX_LINKS; i++){
        cell_list[i].timeslot_offset = candidate_cell_list[i].timeslot_offset;
        cell_list[i].channel_offset = candidate_cell_list[i].channel_offset;
    }
    // for(int a=0; a<9; a++){
    //     printf("Candidate %u ist time %u and channel %u\n",a,candidate_cell_list[a].timeslot_offset, candidate_cell_list[a].channel_offset);
    // }
}

/* Delete a candidate cell and replace it immediately with a valid cell */
void replace_candidate_cell(uint16_t timeslot_offset){
    struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(0);
    for(int i = 0; i<CAND_CELL_LIST_LEN; i++){
        if(candidate_cell_list[i].timeslot_offset == timeslot_offset){
            uint8_t found_valid_slot = 0;
            /* Find free slots that dont exist in cand_cell_list, blacklist and schedule  */
            while(found_valid_slot != 2){
                found_valid_slot = 0;
                int8_t random_timeslot_offset = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_CONF_DEFAULT_LENGTH;
                uint8_t random_channel_offset = ((random_rand() & 0xFF)) % NUMBER_OF_CHANNELS;
                
                //prevent it from occupying the same space as the minimal cell
                if(random_timeslot_offset == 0){
                    random_timeslot_offset++;
                }
                if(tsch_schedule_get_link_by_timeslot(sf, random_timeslot_offset) != NULL) {
                    continue;  
                }
                for(int j = 0; j < CAND_CELL_LIST_LEN; j++) {
                    if(candidate_cell_list[j].timeslot_offset != random_timeslot_offset) {
                        /* Random selection resulted in a free slot */
                        if(j == CAND_CELL_LIST_LEN - 1) { /* Checked till last index of link list */
                            found_valid_slot++;
                            break;
                        }
                    } else {
                        /* Slot already present in CandidateLinkList */
                        break;
                    }
                }
                for(int a=0; a<blacklist.count; a++){ /* Check whether cell is in blacklist */
                    if(random_timeslot_offset == blacklist.buffer[blacklist.head + a].timeslot_offset
                        && random_channel_offset== blacklist.buffer[blacklist.head + a].channel_offset
                    ){
                        break;
                    }else if(a == blacklist.count -1){ /* Reached the end of the list */
                        candidate_cell_list[i].timeslot_offset = random_timeslot_offset;
                        candidate_cell_list[i].channel_offset = random_channel_offset;
                        found_valid_slot++;
                    }
                } 
            }
            return;
        }
    }
}

/* Check cand_cell_list with the cells that are interfered with, emulate the sensing being evaluated */
uint8_t update_cand_cell_list(){
    uint8_t ret = 0;
    for(int i=0; i<CAND_CELL_LIST_LEN; i++){ /* Check every candidate cell */
        for(int j=0; j<INTERFERED_CELLS; j++){ /* Check every interferred cell for given candidate cell*/
            if(candidate_cell_list[i].timeslot_offset == network_interfere_cells[j].timeslot_offset
                && candidate_cell_list[i].channel_offset == network_interfere_cells[j].channel_offset
            ){
                push(&blacklist, &(candidate_cell_list[i]));
                replace_candidate_cell(candidate_cell_list[i].timeslot_offset);
                replace_candidate_cell(candidate_cell_list[i + 1].timeslot_offset);
                ret = 1;
            }
        }
    }
    return ret;
}

// Initialize the blacklist
void init_buffer(CircularBuffer *cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

// Add an element to the buffer
void push(CircularBuffer *cb, sf_simple_cell_t *cell) {
    if (cb->count == BLACKLIST_MAX_SIZE) {
        cb->head = (cb->head + 1) % BLACKLIST_MAX_SIZE;
    } else {
        cb->count++;
    }
    
    cb->buffer[cb->tail].timeslot_offset = cell->timeslot_offset;
    cb->buffer[cb->tail].channel_offset = cell->channel_offset;
    cb->tail = (cb->tail + 1) % BLACKLIST_MAX_SIZE;
}
