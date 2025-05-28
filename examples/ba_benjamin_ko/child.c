#include "contiki.h"
#include "sys/node-id.h"
#include "sys/log.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/routing/routing.h"
#include "random.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include <stdint.h>
#include <inttypes.h>
#include "sf-simple.h"
#include "project-conf.h"
#include "net/ipv6/uip-debug.h"
#include "net/mac/tsch/sixtop/sixp.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"
#include "net/mac/tsch/tsch-queue.h"
#include "net/mac/tsch/tsch-slot-operation.h"
#include "net/linkaddr.h"
#include "math.h"
#include "dev/leds.h"
#include "button-hal.h"
#include "advanced_cell_alloc.h"


#define DEBUG DEBUG_PRINT
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

//currently sending 10 packets per slotframe with one slotframe being 1.01 seconds
#define SLOTFRAME_LENGTH ((CLOCK_SECOND / 100) * 101)
#define TARGET_CELLS_PER_SLOTFRAME 30
#define STARTUP_TIME (CLOCK_SECOND * 30)


#define MAX_NUM_CELLS 100
#define HOUSEKEEPINGCOLLISION_PERIOD (CLOCK_SECOND * 60)
#define VARIANCE_FACTOR 0.1
#define SFSIMPLE 

static struct simple_udp_connection udp_conn;
static uint32_t udp_rx_count = 0;
static clock_time_t start_time;
static int added_num_of_tx_cells = 0;
static int sixp_add_finished = 0;

PROCESS(udp_client_process, "UDP server");
PROCESS(sixp_add_cells_process, "sixp message processor");
PROCESS(sixp_relocate_cells_process, "sixp message processor");
AUTOSTART_PROCESSES(&sixp_add_cells_process, &sixp_relocate_cells_process, &udp_client_process);

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  LOG_INFO("Received response '%.*s' from ", datalen, (char *) data);
  LOG_INFO_6ADDR(sender_addr);
  LOG_INFO_("\n");
  udp_rx_count++;
}

PROCESS_THREAD(udp_client_process, ev, data){
  static struct etimer periodic_timer;
  uip_ipaddr_t dest_ipaddr;
  static uint32_t tx_count;


  PROCESS_BEGIN();
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

  etimer_set(&periodic_timer, CLOCK_SECOND * 5);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));

    if(NETSTACK_ROUTING.node_is_reachable() && NETSTACK_ROUTING.get_root_ipaddr(&dest_ipaddr)) {
      /* Send to DAG root */
      // LOG_INFO("Sending request %"PRIu32" to ", tx_count);
      // LOG_INFO_6ADDR(&dest_ipaddr);
      // LOG_INFO_("\n");
      linkaddr_packet_t packet;
      packet.addr = linkaddr_node_addr;
      packet.code = CHILD_IDENTIFIER;
      simple_udp_sendto(&udp_conn, &packet, sizeof(packet), &dest_ipaddr);
      tx_count++;
    }

    if(added_num_of_tx_cells != 0){
      clock_time_t base_period = CLOCK_SECOND / (added_num_of_tx_cells);
      int16_t variance = (int16_t)((base_period * VARIANCE_FACTOR * 2 * random_rand() / RANDOM_RAND_MAX) - (base_period * VARIANCE_FACTOR));
      etimer_set(&periodic_timer, base_period + variance);
    }else{
      etimer_set(&periodic_timer, CLOCK_SECOND * 5);
    }
  }
  PROCESS_END();
}


PROCESS_THREAD(sixp_add_cells_process, ev, data)
{
  static struct etimer et;
  static struct tsch_neighbor *n = NULL;

  PROCESS_BEGIN();
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  leds_on(LEDS_RED);
  NETSTACK_MAC.on();
  sixtop_add_sf(&sf_simple_driver);
  init_advanced_cell_alloc();

  /* wait and try get the address for the parent till it is available */
  do{
    etimer_set(&et, CLOCK_SECOND * 2);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
    n = tsch_queue_get_time_source();
    LOG_INFO("Trying to get neighbour address.\n");
  }while(n == NULL || !NETSTACK_ROUTING.node_is_reachable());

  /* Add a auto cell for the parent to communicate to the child */
  add_mock_auto_cell(tsch_queue_get_nbr_address(n), LINK_OPTION_RX, 5, 2);
  add_mock_auto_cell(tsch_queue_get_nbr_address(n), LINK_OPTION_RX, 50, 3);
  add_mock_auto_cell(tsch_queue_get_nbr_address(n), LINK_OPTION_RX, 80, 1);
  
  /* Wait for UDP message to be sent for Parent to set up RX*/
  etimer_set(&et, CLOCK_SECOND * 10);
  PROCESS_YIELD_UNTIL(etimer_expired(&et));
  add_mock_auto_cell(tsch_queue_get_nbr_address(n), LINK_OPTION_TX, 90, 3);
  added_num_of_tx_cells++;
  LOG_INFO("Added auto cells\n");

  /* Before starting experiment the queue is cleared */
  tsch_queue_free_packets_to(tsch_queue_get_nbr_address(n));

  start_time = clock_time();
  LOG_INFO("Python: Start time %lus\n", start_time / CLOCK_SECOND);
  while(added_num_of_tx_cells < TARGET_CELLS_PER_SLOTFRAME) {
    n = tsch_queue_get_time_source();
    /* Estimate time it would take for cells to elapse to MAX_NUM_CELLS */
    etimer_set(&et, SLOTFRAME_LENGTH * (ceil(MAX_NUM_CELLS / added_num_of_tx_cells)));
    PROCESS_YIELD_UNTIL(etimer_expired(&et));

    /* Wait for the last 6P transaction to be finished */
    while(sixp_trans_find(tsch_queue_get_nbr_address(n)) != NULL){
      etimer_set(&et, CLOCK_SECOND / 50);
      PROCESS_YIELD_UNTIL(etimer_expired(&et));
      // LOG_INFO("Add cells: Waiting for sixp_trans to be finished\n");
    }

    /* Update candidate list for relocation*/
    uint8_t cand_cells_interfered = 1;
    while(cand_cells_interfered){
      etimer_set(&et, CLOCK_SECOND / 50);
      PROCESS_YIELD_UNTIL(etimer_expired(&et));
      cand_cells_interfered = update_cand_cell_list();
      printf("SENSING THE CELLS\n");
    }

    tsch_queue_free_packets_to(tsch_queue_get_nbr_address(n));
    sf_simple_add_links(tsch_queue_get_nbr_address(n), 1);
    LOG_INFO("Added the %u cell at %lus\n", added_num_of_tx_cells + 1, clock_time()/CLOCK_SECOND);
    added_num_of_tx_cells++;
  }
  leds_on(LEDS_GREEN);
  sixp_add_finished = 1;
  PROCESS_END();
}

PROCESS_THREAD(sixp_relocate_cells_process, ev, data)
{
  static struct etimer et;
  static struct tsch_neighbor *n;
  static tsch_schedule_cell_stats cell_rel_list[MAX_ALLOCATE_CELLS];
  static uint8_t cell_rel_list_length = 0;
  static uint8_t cells_evaluated_static;
  static clock_time_t time_no_relocation[TARGET_CELLS_PER_SLOTFRAME] = { 0 };
  static uint8_t curr_smallest = 0;

  PROCESS_BEGIN();
  PROCESS_WAIT_EVENT_UNTIL(ev == button_hal_press_event);
  /* Wait the startup time */
  etimer_set(&et, STARTUP_TIME);
  PROCESS_YIELD_UNTIL(etimer_expired(&et));

  /* Wait for the same conditions as the 6P ADD process to have similar beginning time */
  while(!NETSTACK_ROUTING.node_is_reachable()){
    etimer_set(&et, CLOCK_SECOND * 3);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
  }

  while(1) {
    etimer_set(&et, HOUSEKEEPINGCOLLISION_PERIOD);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
    LOG_INFO("Relocation_process: Relocation process starting\n");

    /* Get time-source neighbor */
    n = tsch_queue_get_time_source();

    /* Update candidate list for relocation*/
    uint8_t cand_cells_interfered = 1;
    while(cand_cells_interfered){
      cand_cells_interfered = update_cand_cell_list();
      printf("SENSING THE CELLS");
    }

    /* Reset rel cell list */
    memset(cell_rel_list, 0, sizeof(tsch_schedule_cell_stats) * MAX_ALLOCATE_CELLS);
    cell_rel_list_length = 0;
    /*Getting relocation cell list and length */
    uint8_t cells_evaluated = tsch_stats_evaluate_cells_for_relocation(cell_rel_list, &cell_rel_list_length);
    /* Workaround to have the value as static */
    cells_evaluated_static = cells_evaluated;

    etimer_set(&et, CLOCK_SECOND / 50);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));

    curr_smallest = cells_evaluated_static;
    for(int a=0; a<cell_rel_list_length; a++){
      if(cell_rel_list[a].numberCellAllocated < curr_smallest){
        curr_smallest = cell_rel_list[a].numberCellAllocated;
      }
    }

    etimer_set(&et, CLOCK_SECOND / 50);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));

    clock_time_t network_stable_time = clock_time();
    for(int b=0; b<curr_smallest-1; b++){
      if(time_no_relocation[b] == 0){
        time_no_relocation[b] = network_stable_time /CLOCK_SECOND;
      }
    }

    /* Go through cell_rel_list and making 6P RELOCATION request for each*/
    static int i;
    for(i=0; i<cell_rel_list_length; i++){
      etimer_set(&et, CLOCK_SECOND);
      PROCESS_YIELD_UNTIL(etimer_expired(&et));
      /* Wait for the last 6P transaction to be finished */
      while(sixp_trans_find(tsch_queue_get_nbr_address(n)) != NULL){
        update_cand_cell_list();
        etimer_set(&et, CLOCK_SECOND / 50);
        PROCESS_YIELD_UNTIL(etimer_expired(&et));
        // LOG_INFO("Relocation_process: Waiting for trans to be free for: %u\n", cell_rel_list[i].slotOffset);
      }

      LOG_INFO("Relocation_process: Relocating cell with timeslot: %u channel: %u\n", cell_rel_list[i].slotOffset, cell_rel_list[i].channelOffset);
      sf_simple_cell_t cell_to_rel = {cell_rel_list[i].slotOffset, cell_rel_list[i].channelOffset};
      sf_simple_relocate_links(tsch_queue_get_nbr_address(n), 1, &cell_to_rel);
    }

    /* If all cells were evaluated and non are relocated the network is considered stable and the experiment ends */
    if(cells_evaluated_static >= (TARGET_CELLS_PER_SLOTFRAME - 5) && sixp_add_finished == 1 && cell_rel_list_length == 0){
      clock_time_t stop_time = clock_time();
      LOG_INFO("Python: All cells %u evaluated and no reloaction to be done anymore time %lus\n", cells_evaluated_static, stop_time / CLOCK_SECOND);
      print_cell_pdr_list();

      time_no_relocation[cells_evaluated_static-1] = stop_time /CLOCK_SECOND;
      printf("Python: relocation times ");
      for(int i=0; i<TARGET_CELLS_PER_SLOTFRAME; i++){
        printf("%ld, ", time_no_relocation[i]);
      }
      printf("\n");
      break; 
    }

    /* Wait for last relocation to be done before deleting cell from stats list to avoid them getting registered again */
    while(sixp_trans_find(tsch_queue_get_nbr_address(n)) != NULL){
      etimer_set(&et, CLOCK_SECOND / 50);
      PROCESS_YIELD_UNTIL(etimer_expired(&et));
    }
    etimer_set(&et, CLOCK_SECOND);
    PROCESS_YIELD_UNTIL(etimer_expired(&et));
    tsch_stats_delete_cells_pdr_list();

    LOG_INFO("Relocation_process: Relocated %u links with %u added cells and %u evaluated\n", cell_rel_list_length, added_num_of_tx_cells, cells_evaluated_static);
  }
  leds_on(LEDS_ALL);

  PROCESS_END();
}
