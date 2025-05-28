#include "contiki-lib.h"
#include "lib/assert.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/mac/tsch/sixtop/sixp.h"
#include "net/mac/tsch/sixtop/sixp-nbr.h"
#include "net/mac/tsch/sixtop/sixp-pkt.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"
#include "./project-conf.h"
#include "sys/log.h"
#include "advanced_cell_alloc.h"

#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
#define SFSIMPLE

#include "sf-simple.h"
#include "network_interference_cells.h"

#define DEBUG DEBUG_PRINT
#include "net/net-debug.h"

static const uint16_t slotframe_handle = 0;
static uint8_t res_storage[4 + SF_SIMPLE_MAX_LINKS * 4];
static uint8_t req_storage[4 + SF_SIMPLE_MAX_LINKS * 4];
static sf_simple_cell_t current_cell_to_be_relocated;
const uint8_t *cell_listdsdf;
static void read_cell(const uint8_t *buf, sf_simple_cell_t *cell);
static void print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len);
static void remove_links_to_schedule(const uint8_t *cell_list,
                                     uint16_t cell_list_len);
static void add_response_sent_callback(void *arg, uint16_t arg_len,
                                       const linkaddr_t *dest_addr,
                                       sixp_output_status_t status);
static void delete_response_sent_callback(void *arg, uint16_t arg_len,
                                          const linkaddr_t *dest_addr,
                                          sixp_output_status_t status);
static void relocate_response_sent_callback(void *arg, uint16_t arg_len,
                                            const linkaddr_t *dest_addr,
                                            sixp_output_status_t status);
static void add_req_input(const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void delete_req_input(const uint8_t *body, uint16_t body_len,
                             const linkaddr_t *peer_addr);
static void relocate_req_input(const uint8_t *body, uint16_t body_len,
                              const linkaddr_t *peer_addr);
static void input(sixp_pkt_type_t type, sixp_pkt_code_t code,
                  const uint8_t *body, uint16_t body_len,
                  const linkaddr_t *src_addr);
static void request_input(sixp_pkt_cmd_t cmd,
                          const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void response_input(sixp_pkt_rc_t rc,
                           const uint8_t *body, uint16_t body_len,
                           const linkaddr_t *peer_addr);
static void sixp_add_request_sent_callback(void *arg, uint16_t arg_len, const linkaddr_t *dest_addr, sixp_output_status_t status);
static void sixp_relocate_request_sent_callback(void *arg, uint16_t arg_len, const linkaddr_t *dest_addr, sixp_output_status_t status);
static void timeout(sixp_pkt_cmd_t cmd, const linkaddr_t *peer_addr);



static void
read_cell(const uint8_t *buf, sf_simple_cell_t *cell)
{
  cell->timeslot_offset = buf[0] + (buf[1] << 8);
  cell->channel_offset = buf[2] + (buf[3] << 8);
}

static void
print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len)
{
  uint16_t i;
  sf_simple_cell_t cell;

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    PRINTF("%u ", cell.timeslot_offset);
  }
}

void add_mock_auto_cell(const linkaddr_t *peer_addr, uint8_t link_option, const uint8_t timeslot, const uint8_t channelOffset){
  sf_simple_cell_t cellToReserve[1];
  cellToReserve->timeslot_offset = timeslot;
  cellToReserve->channel_offset = channelOffset;
  add_links_to_schedule(peer_addr, link_option, (const uint8_t *)cellToReserve, sizeof(sf_simple_cell_t));
}

void
add_links_to_schedule(const linkaddr_t *peer_addr, uint8_t link_option,
                      const uint8_t *cell_list, uint16_t cell_list_len)
{
  /* add only the first valid cell */
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    return;
  }

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    PRINTF("sf-simple: Schedule link %d as %s with node ",
           cell.timeslot_offset,
           link_option == LINK_OPTION_RX ? "RX" : "TX");
    PRINTLLADDR((uip_lladdr_t *)peer_addr);
    PRINTF("\n");
    tsch_schedule_add_link(slotframe,
                           link_option, LINK_TYPE_NORMAL, peer_addr,
                           cell.timeslot_offset, cell.channel_offset, 1);
    break;
  }
  return;
}

static void
remove_links_to_schedule(const uint8_t *cell_list, uint16_t cell_list_len)
{
  /* remove all the cells */

  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    return;
  }

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    LOG_INFO("sf-simple: Remove cell\n");
    tsch_schedule_remove_link_by_offsets(slotframe,
                                         cell.timeslot_offset,
                                         cell.channel_offset);
  }
}

static void add_response_sent_callback(void *arg, uint16_t arg_len,
                           const linkaddr_t *dest_addr,
                           sixp_output_status_t status)
{
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    LOG_INFO("sf-simple: 6P add response successfully sent");
    add_links_to_schedule(dest_addr, LINK_OPTION_RX,
                          cell_list, cell_list_len);
  }
}

static void
delete_response_sent_callback(void *arg, uint16_t arg_len,
                              const linkaddr_t *dest_addr,
                              sixp_output_status_t status)
{
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    remove_links_to_schedule(cell_list, cell_list_len);
  }
}

static void relocate_response_sent_callback(void *arg, uint16_t arg_len,
                           const linkaddr_t *dest_addr,
                           sixp_output_status_t status)
{
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cand_cell_list;
  uint16_t cand_cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
      sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                           (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                           &cand_cell_list, &cand_cell_list_len,
                           body, body_len) == 0 &&
      (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    LOG_INFO("sf-simple: Adding link with timeslot: ");
    print_cell_list(cand_cell_list, cand_cell_list_len);
    LOG_INFO("\n");
    add_links_to_schedule(dest_addr, LINK_OPTION_RX, cand_cell_list, cand_cell_list_len);
    LOG_INFO("sf-simple: removing cell with timeslot: %u\n", current_cell_to_be_relocated.timeslot_offset);
    remove_links_to_schedule((const uint8_t *) &current_cell_to_be_relocated, sizeof(current_cell_to_be_relocated));
    
  }
}

static void add_req_input(const uint8_t *body, uint16_t body_len, const linkaddr_t *peer_addr)
{
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int feasible_link;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    LOG_INFO("sf-simple: Parse error on add request\n");
    return;
  }

  LOG_INFO("sf-simple: Received a 6P Add Request for %d links from node \n",
         num_cells);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  LOG_INFO(" with LinkList : ");
  print_cell_list(cell_list, cell_list_len);
  LOG_INFO("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  if(num_cells > 0 && cell_list_len > 0) {
    memset(res_storage, 0, sizeof(res_storage));
    res_len = 0;

    /* checking availability for requested slots */
    for(i = 0, feasible_link = 0;
        i < cell_list_len && feasible_link < num_cells;
        i += sizeof(cell)) {
      read_cell(&cell_list[i], &cell);
      // check whether there is a cell in the timeslot since a node can only have one cell per timeslot
      if(tsch_schedule_get_link_by_timeslot(slotframe,
                                           cell.timeslot_offset) == NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               feasible_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
        feasible_link++;
      }
    }

    if(feasible_link == num_cells) {
      /* Links are feasible. Create Link Response packet */
      PRINTF("sf-simple: Send a 6P Response to node ");
      PRINTLLADDR((uip_lladdr_t *)peer_addr);
      PRINTF("\n");
      
      sixp_output(SIXP_PKT_TYPE_RESPONSE,
                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                  SF_SIMPLE_SFID,
                  res_storage, res_len, peer_addr,
                  add_response_sent_callback, res_storage, res_len);
    }
    else{
      PRINTF("no available cells matching cell list in 6P ADD request");
    }
  }
}

static void delete_req_input(const uint8_t *body, uint16_t body_len,
                 const linkaddr_t *peer_addr)
{
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;
  int removed_link;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    PRINTF("sf-simple: Parse error on delete request\n");
    return;
  }

  PRINTF("sf-simple: Received a 6P Delete Request for %d links from node ",
         num_cells);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF(" with LinkList : ");
  print_cell_list(cell_list, cell_list_len);
  PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  memset(res_storage, 0, sizeof(res_storage));
  res_len = 0;

  if(num_cells > 0 && cell_list_len > 0) {
    /* ensure before delete */
    for(i = 0, removed_link = 0; i < cell_list_len; i += sizeof(cell)) {
      read_cell(&cell_list[i], &cell);
      if(tsch_schedule_get_link_by_offsets(slotframe,
                                           cell.timeslot_offset,
                                           cell.channel_offset) != NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               removed_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
      }
    }
  }

  /* Links are feasible. Create Link Response packet */
  PRINTF("sf-simple: Send a 6P Response to node ");
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF("\n");
  sixp_output(SIXP_PKT_TYPE_RESPONSE,
              (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
              SF_SIMPLE_SFID,
              res_storage, res_len, peer_addr,
              delete_response_sent_callback, res_storage, res_len);
}

static void relocate_req_input(const uint8_t *body, uint16_t body_len, const linkaddr_t *peer_addr)
{
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int feasible_link;
  uint8_t num_cells;
  const uint8_t *rel_cell_list;
  uint16_t rel_cell_list_len;
  const uint8_t *cand_cell_list;
  uint16_t cand_cell_list_len;
  uint16_t res_len;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_rel_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
                            &rel_cell_list, &rel_cell_list_len,
                            body, body_len) != 0 ||
      sixp_pkt_get_cand_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
                            &cand_cell_list, &cand_cell_list_len,
                            body, body_len) != 0) {
    PRINTF("sf-simple: Parse error on relocate request\n");
    return;
  }

  PRINTF("sf-simple: Received a 6P Relocate Request for %d links from node ", num_cells);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF(" with cells to relocate : ");
  print_cell_list(rel_cell_list, rel_cell_list_len);
  PRINTF("\n and candidate list: ");
  print_cell_list(cand_cell_list, cand_cell_list_len);
  PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  /* Check for validity of request */
  if(num_cells > 0 && rel_cell_list_len > 0 && cand_cell_list_len > 0) {
    memset(res_storage, 0, sizeof(res_storage));
    res_len = 0;

    /* compiling list with cells to relocate the cells to */
    for(i = 0, feasible_link = 0;
        i < cand_cell_list_len && feasible_link < num_cells;
        i += sizeof(cell)) {
      read_cell(&cand_cell_list[i], &cell);
      // check whether there is a cell in the timeslot since a node can only have one cell per timeslot
      if(tsch_schedule_get_link_by_timeslot(slotframe, cell.timeslot_offset) == NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               feasible_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
        feasible_link++;
      }
    }

    if(feasible_link == num_cells) {
      /* Links are feasible. Create Link Response packet */
      read_cell(rel_cell_list, &current_cell_to_be_relocated);
      PRINTF("sf-simple: Send a 6P Response to node ");
      PRINTLLADDR((uip_lladdr_t *)peer_addr);
      PRINTF("\n");
      
      sixp_output(SIXP_PKT_TYPE_RESPONSE,
                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                  SF_SIMPLE_SFID,
                  res_storage, res_len, peer_addr,
                  relocate_response_sent_callback, res_storage, res_len);
    }
    else{
      PRINTF("no available cells matching cell list in 6P RELOCATE request");
    }
  }
}

static void input(sixp_pkt_type_t type, sixp_pkt_code_t code,
      const uint8_t *body, uint16_t body_len, const linkaddr_t *src_addr)
{
  assert(body != NULL && body != NULL);
  switch(type) {
    case SIXP_PKT_TYPE_REQUEST:
      request_input(code.cmd, body, body_len, src_addr);
      break;
    case SIXP_PKT_TYPE_RESPONSE:
      response_input(code.rc, body, body_len, src_addr);
      break;
    default:
      /* unsupported */
      break;
  }
}

static void request_input(sixp_pkt_cmd_t cmd, const uint8_t *body, uint16_t body_len, const linkaddr_t *peer_addr){
  assert(body != NULL && peer_addr != NULL);

  switch(cmd) {
    case SIXP_PKT_CMD_ADD:
      add_req_input(body, body_len, peer_addr);
      LOG_INFO("sf-simple: Add request recieved\n");
      break;
    case SIXP_PKT_CMD_DELETE:
      delete_req_input(body, body_len, peer_addr);
      break;
    case SIXP_PKT_CMD_RELOCATE:
      relocate_req_input(body, body_len, peer_addr);
    default:
      /* unsupported request */
      break;
  }
}


static void response_input(sixp_pkt_rc_t rc,
               const uint8_t *body, uint16_t body_len,
               const linkaddr_t *peer_addr)
{
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  const uint8_t *cand_cell_list;
  uint16_t cand_cell_list_len;
  sixp_nbr_t *nbr;
  sixp_trans_t *trans;

  assert(body != NULL && peer_addr != NULL);

  if((nbr = sixp_nbr_find(peer_addr)) == NULL ||
     (trans = sixp_trans_find(peer_addr)) == NULL) {
    return;
  }

  if(rc == SIXP_PKT_RC_SUCCESS) {
    switch(sixp_trans_get_cmd(trans)) {
      case SIXP_PKT_CMD_ADD:
        LOG_INFO("sf-simple: Received a Add Response packet\n");
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          PRINTF("sf-simple: Parse error on add response\n");
          return;
        }
        PRINTF("sf-simple: Received a 6P Add Response with LinkList : ");
        print_cell_list(cell_list, cell_list_len);
        PRINTF("\n");
        add_links_to_schedule(peer_addr, LINK_OPTION_TX, cell_list, cell_list_len);
        
        sf_simple_cell_t added_cell;
        read_cell(cell_list, &added_cell);

        /* Remove cell from candidate cell list */
        replace_candidate_cell(added_cell.timeslot_offset);

        /* Log when which cell was added */
        LOG_INFO("Python: Added a cell (%u, %u) at %lus\n",added_cell.timeslot_offset, added_cell.channel_offset, clock_time()/CLOCK_SECOND);
        break;

      case SIXP_PKT_CMD_DELETE:
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          PRINTF("sf-simple: Parse error on add response\n");
          return;
        }
        PRINTF("sf-simple: Received a 6P Delete Response with LinkList : ");
        print_cell_list(cell_list, cell_list_len);
        PRINTF("\n");
        remove_links_to_schedule(cell_list, cell_list_len);
        break;

      case SIXP_PKT_CMD_RELOCATE:
        if (sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
           (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
           &cand_cell_list, &cand_cell_list_len,
           body, body_len) != 0) {
          PRINTF("sf-simple: Parse error on relocate response\n");
          return;
        }
        PRINTF("sf-simple: Received a 6P Relocate Response from:" );
        PRINTLLADDR((uip_lladdr_t *)peer_addr);
        PRINTF(" with cells to relocate : ");
        print_cell_list((const uint8_t *) &current_cell_to_be_relocated, sizeof(current_cell_to_be_relocated));
        PRINTF("\n and candidate list: ");
        print_cell_list(cand_cell_list, cand_cell_list_len);
        PRINTF("\n");

        replace_candidate_cell(((sf_simple_cell_t *) cand_cell_list)[0].timeslot_offset);

        add_links_to_schedule(peer_addr, LINK_OPTION_TX, cand_cell_list, cand_cell_list_len);
        remove_links_to_schedule((const uint8_t *) &current_cell_to_be_relocated, sizeof(current_cell_to_be_relocated));
        break;

      default:
        PRINTF("sf-simple: unsupported response\n");
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link addition
 */
int sf_simple_add_links(linkaddr_t *peer_addr, uint8_t num_links){
  // uint8_t i = 0;
  uint8_t index = 0;
  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  uint8_t req_len;
  sf_simple_cell_t cell_list[SF_SIMPLE_MAX_LINKS];

  /* Flag to prevent repeated slots */
  // uint8_t slot_check = 1;
  // uint16_t random_slot = 0;
  // uint16_t random_channel = 0;

  assert(peer_addr != NULL);
  assert(sf != NULL);

  get_candidate_add(cell_list);
  replace_candidate_cell(cell_list[0].timeslot_offset);
  replace_candidate_cell(cell_list[1].timeslot_offset);
  replace_candidate_cell(cell_list[2].timeslot_offset);
  index = 4;
  // printf("get candidate list\n");
  // for(int a=0; a<SF_SIMPLE_MAX_LINKS; a++){
  //   printf("CELL list  %u ist time %u and channel %u\n",a,cell_list[a].timeslot_offset, cell_list[a].channel_offset);
  // }

  // do {
  //   /* Randomly select a slot offset within TSCH_SCHEDULE_CONF_DEFAULT_LENGTH */
  //   random_slot = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_CONF_DEFAULT_LENGTH;
  //   random_channel = ((random_rand() & 0xFF)) % NUMBER_OF_CHANNELS;

  //   //prevent it from occupying the same space as the minimal cell
  //   if(random_slot == 0){
  //     random_slot++;
  //   }

  //   if(tsch_schedule_get_link_by_timeslot(sf, random_slot) == NULL) {

  //     /* To prevent repeated slots */
  //     for(i = 0; i < index; i++) {
  //       if(cell_list[i].timeslot_offset != random_slot) {
  //         /* Random selection resulted in a free slot */
  //         if(i == index - 1) { /* Checked till last index of link list */
  //           slot_check = 1;
  //           break;
  //         }
  //       } else {
  //         /* Slot already present in CandidateLinkList */
  //         slot_check++;
  //         break;
  //       }
  //     }

  //     /* Random selection resulted in a free slot, add it to linklist */
  //     if(slot_check == 1) {
  //       cell_list[index].timeslot_offset = random_slot;
  //       cell_list[index].channel_offset = random_channel;

  //       index++;
  //       slot_check++;
  //     } else if(slot_check > TSCH_SCHEDULE_DEFAULT_LENGTH) {
  //       PRINTF("sf-simple:! Number of trials for free slot exceeded...\n");
  //       return -1;
  //       break; /* exit while loop */
  //     }
  //   }
  // } while(index < SF_SIMPLE_MAX_LINKS);

  /* Create a Sixtop Add Request. Return 0 if Success */
  if(index == 0 ) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_cell_options(SIXP_PKT_TYPE_REQUEST,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                               SIXP_PKT_CELL_OPTION_TX,
                               req_storage,
                               sizeof(req_storage)) != 0 ||
     sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            num_links,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            (const uint8_t *)cell_list,
                            index * sizeof(sf_simple_cell_t), 0,
                            req_storage, sizeof(req_storage)) != 0) {
    PRINTF("sf-simple: Build error on add request\n");
    return -1;
  }

  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + index * sizeof(sf_simple_cell_t);
  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              sixp_add_request_sent_callback, NULL, 0);

  PRINTF("sf-simple: Send a 6P Add Request for %d links to node ",num_links);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF(" with LinkList : ");
  print_cell_list((const uint8_t *)cell_list, index * sizeof(sf_simple_cell_t));
  PRINTF("\n");

  return 0;
}

static void sixp_add_request_sent_callback(void *arg, uint16_t arg_len, const linkaddr_t *dest_addr, sixp_output_status_t status){
  LOG_INFO("sf-simple: 6P ADD request sent with %s", (status==0)? "success":"failure or aborted");
  LOG_INFO("\n");
}

/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link deletion
 */
int
sf_simple_remove_links(linkaddr_t *peer_addr)
{
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;

  uint16_t req_len;
  sf_simple_cell_t cell;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++) {
    l = tsch_schedule_get_link_by_offsets(sf, i, 0);

    if(l) {
      /* Non-zero value indicates a scheduled link */
      if((linkaddr_cmp(&l->addr, peer_addr)) && (l->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        index++;
        break;   /* delete atmost one */
      }
    }
  }

  if(index == 0) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            1,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            (const uint8_t *)&cell, sizeof(cell),
                            0,
                            req_storage, sizeof(req_storage)) != 0) {
    PRINTF("sf-simple: Build error on add request\n");
    return -1;
  }
  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + sizeof(sf_simple_cell_t);

  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  PRINTF("sf-simple: Send a 6P Delete Request for %d links to node ",
         1);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF(" with LinkList : ");
  print_cell_list((const uint8_t *)&cell, sizeof(cell));
  PRINTF("\n");

  return 0;
}

int sf_simple_relocate_links(linkaddr_t *peer_addr, uint8_t num_links, sf_simple_cell_t *cell_to_relocate)
{
  // uint8_t i = 0;
  uint8_t index = 0;
  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  uint8_t req_len;
  sf_simple_cell_t cand_cell_list[SF_SIMPLE_MAX_LINKS];
  /* Flag to prevent repeated slots */
  // uint8_t slot_check = 1;
  // uint16_t random_slot = 0;
  // uint16_t random_channel = 0;
  current_cell_to_be_relocated = *cell_to_relocate;

  assert(peer_addr != NULL);
  assert (sf != NULL);

  get_candidate_add(cand_cell_list);
  index = 3;

  // do {
  //   /* Randomly select a slot offset within TSCH_SCHEDULE_CONF_DEFAULT_LENGTH */
  //   random_slot = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_CONF_DEFAULT_LENGTH;
  //   random_channel = ((random_rand() & 0xFF)) % NUMBER_OF_CHANNELS;

  //   //prevent it from occupying the same space as the minimal cell
  //   if(random_slot == 0){
  //     random_slot++;
  //   }

  //   if(tsch_schedule_get_link_by_timeslot(sf, random_slot) == NULL) {

  //     /* To prevent repeated slots */
  //     for(i = 0; i < index; i++) {
  //       if(cand_cell_list[i].timeslot_offset != random_slot) {
  //         /* Random selection resulted in a free slot */
  //         if(i == index - 1) { /* Checked till last index of link list */
  //           slot_check = 1;
  //           break;
  //         }
  //       } else {
  //         /* Slot already present in CandidateLinkList */
  //         slot_check++;
  //         break;
  //       }
  //     }

  //     /* Random selection resulted in a free slot, add it to linklist */
  //     if(slot_check == 1) {
  //       cand_cell_list[index].timeslot_offset = random_slot;
  //       cand_cell_list[index].channel_offset = random_channel;

  //       index++;
  //       slot_check++;
  //     } else if(slot_check > TSCH_SCHEDULE_DEFAULT_LENGTH) {
  //       PRINTF("sf-simple:! Number of trials for free slot exceeded...\n");
  //       return -1;
  //       break; /* exit while loop */
  //     }
  //   }
  // } while(index < SF_SIMPLE_MAX_LINKS - 1);

  /* Create a Sixtop Add Request. Return 0 if Success */
  if(index == 0 ) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_cell_options(SIXP_PKT_TYPE_REQUEST,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
                               SIXP_PKT_CELL_OPTION_TX,
                               req_storage,
                               sizeof(req_storage)) != 0){
    PRINTF("sf-simple: Build error on relocate request 1111\n");
  }
  if(     sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
    (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
    num_links,
    req_storage,
    sizeof(req_storage)) != 0){
    PRINTF("sf-simple: Build error on relocate request 2222\n");
  }
  if(   sixp_pkt_set_rel_cell_list(SIXP_PKT_TYPE_REQUEST,
    (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
    (const uint8_t *)cell_to_relocate,
    sizeof(sf_simple_cell_t), 0,
    req_storage, sizeof(req_storage)) != 0){
    PRINTF("sf-simple: Build error on relocate request 3333\n");
  }
  if(  sixp_pkt_set_cand_cell_list(SIXP_PKT_TYPE_REQUEST,
    (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
    (const uint8_t *)cand_cell_list,
    index * sizeof(sf_simple_cell_t), 0,
    req_storage, sizeof(req_storage)) != 0){
    PRINTF("sf-simple: Build error on relocate request 4444\n");
  }

  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = sizeof(sixp_pkt_metadata_t) + sizeof(sixp_pkt_cell_options_t) + sizeof(sixp_pkt_num_cells_t) + (SF_SIMPLE_MAX_LINKS * sizeof(sf_simple_cell_t));
  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_RELOCATE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              sixp_relocate_request_sent_callback, NULL, 0);

  PRINTF("sf-simple: Send a 6P Relocate Request for %d links to node ",num_links);
  PRINTLLADDR((uip_lladdr_t *)peer_addr);
  PRINTF(" with candidate List : ");
  print_cell_list((const uint8_t *)cand_cell_list, index * sizeof(sf_simple_cell_t));
  PRINTF("\n");

  return 0;
}

static void sixp_relocate_request_sent_callback(void *arg, uint16_t arg_len, const linkaddr_t *dest_addr, sixp_output_status_t status){
  LOG_INFO("sf-simple: 6P RELOCATE request sent with %s", (status==0)? "success":"failure or aborted");
  LOG_INFO("\n");
}

const sixtop_sf_t sf_simple_driver = {
  SF_SIMPLE_SFID,
  CLOCK_SECOND * 2,
  NULL,
  input,
  timeout,
  NULL
};


static void timeout(sixp_pkt_cmd_t cmd, const linkaddr_t *peer_addr){
  if(cmd == SIXP_PKT_CMD_ADD){
    // sf_simple_add_links((linkaddr_t *)peer_addr, 1);
    LOG_INFO("sf-simple: Timeout occoured\n");
  }
}