#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define DEBUG DEBUG_PRINT
#include "net/net-debug.h"
#include "sf-simple.h"

#define CAND_CELL_LIST_LEN 9
#define BLACKLIST_MAX_SIZE 5
#define CAND_CELL_INTERFERENCE_THRESH 0.5

/*
base time 30s * je mehr allocationen desto schneller wollen wir updaten?
*/

typedef struct {
    sf_simple_cell_t buffer[BLACKLIST_MAX_SIZE];  // Fixed-size array
    int head;   // Points to the oldest element
    int tail;   // Points to the next empty slo
    int count;  // Number of elements in the buffer
} CircularBuffer;

void init_advanced_cell_alloc();

void init_cand_cell_list(sf_simple_cell_t *cell_list);
void get_candidate_add(sf_simple_cell_t *cell_list);
void replace_candidate_cell(uint16_t timeslot_offset);
uint8_t update_cand_cell_list();

void init_buffer(CircularBuffer *cb);
void push(CircularBuffer *cb, sf_simple_cell_t *cell);
