#define SFSIMPLE

#include "contiki.h"
#include "sys/node-id.h"
#include "sys/log.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/routing/routing.h"
#include "net/ipv6/uip-debug.h"
#include "net/linkaddr.h"
#include "project-conf.h"
#include "net/ipv6/simple-udp.h"
#include "os/sys/process.h"
#include "dev/leds.h"
#include "network_interference_cells.h"

#include "random.h"
#include "sf-simple.h"
#include "arch/cpu/cc2538/dev/sys-ctrl.h"

#define DEBUG DEBUG_PRINT
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define NUM_CELLS_TO_INTERFERE 40

static struct simple_udp_connection udp_conn;
// static sf_simple_cell_t cell_list[NUM_CELLS_TO_INTERFERE];


PROCESS(udp_packages_process, "udp packages periodically sent");
PROCESS(sixp_slot_init, "sets up sixp slots for broadcast");
AUTOSTART_PROCESSES(&udp_packages_process, &sixp_slot_init);

static uint8_t start_interfering = 0;

static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  if(sender_addr != NULL){
    start_interfering++;
  }
}

PROCESS_THREAD(udp_packages_process, ev, data){
  uip_ipaddr_t dest_ipaddr;
  uip_ipaddr_t parent_addr;
  static struct etimer udp_et;
  static uint32_t tx_count;
  static struct tsch_neighbor *n = NULL;

  PROCESS_BEGIN();
  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL, UDP_SERVER_PORT, udp_rx_callback);

  /*Wait for nod to be reachable and parent address has been found */
  while(!NETSTACK_ROUTING.node_is_reachable() || !NETSTACK_ROUTING.get_root_ipaddr(&parent_addr)){
    etimer_set(&udp_et, CLOCK_SECOND * 3);
    PROCESS_YIELD_UNTIL(etimer_expired(&udp_et));
    LOG_INFO("Node is reachable1: %d\n", NETSTACK_ROUTING.node_is_reachable());
  }

  /* Send UDP message to set up auto cells */
  linkaddr_packet_t packet;
  packet.addr = linkaddr_node_addr;
  packet.code = NETWORK_IDENTIFIER;
  simple_udp_sendto(&udp_conn, &packet, sizeof(packet), &parent_addr);

  etimer_set(&udp_et, CLOCK_SECOND * 5);
  PROCESS_YIELD_UNTIL(etimer_expired(&udp_et));

  simple_udp_sendto(&udp_conn, &packet, sizeof(packet), &parent_addr);

  etimer_set(&udp_et, CLOCK_SECOND * 10);
  PROCESS_YIELD_UNTIL(etimer_expired(&udp_et));
  
  /* Set up auto cells */
  n = tsch_queue_get_time_source();
  add_mock_auto_cell(tsch_queue_get_nbr_address(n), LINK_OPTION_TX, 20, 2);

  etimer_set(&udp_et, CLOCK_SECOND *5);


  leds_on(LEDS_GREEN);

  while(1) {
    PROCESS_YIELD_UNTIL(etimer_expired(&udp_et));
    LOG_INFO("Node is reachable: %d with interference %u\n", NETSTACK_ROUTING.node_is_reachable(), start_interfering);

    if(NETSTACK_ROUTING.node_is_reachable()){
      uip_create_linklocal_allnodes_mcast(&dest_ipaddr);

      /* Print statistics every 10th TX */
      if(tx_count % 10 == 0) {
        LOG_INFO("Tx/Rx/MissedTx: %" PRIu32 "\n", tx_count);
      }
      
      /* Sending to broadcast address */
      LOG_INFO("Sending request %"PRIu32" to ", tx_count);
      LOG_INFO_6ADDR(&dest_ipaddr);
      LOG_INFO_("\n");
      linkaddr_packet_t udp_packet;
      packet.addr = linkaddr_node_addr;
      packet.code = NETWORK_IDENTIFIER;
      simple_udp_sendto(&udp_conn, &udp_packet, sizeof(udp_packet), &dest_ipaddr);
      tx_count++;
    } else {
      LOG_INFO("Not reachable yet\n");
    }

    if(start_interfering == 0){
      etimer_set(&udp_et, CLOCK_SECOND);
    }else{
      if(start_interfering == 1){
        start_interfering++;      
        leds_off(LEDS_GREEN);
        leds_on(LEDS_ALL);
      }
      etimer_set(&udp_et, CLOCK_SECOND / (NUM_CELLS_TO_INTERFERE * 1.1));
    }
  }
  PROCESS_END();
}


PROCESS_THREAD(sixp_slot_init, ev, data){
  static struct etimer sixp_et;
  // uint16_t random_slot = 0;
  // uint16_t random_channel = 0;
  // static uint8_t cell_list_len = 0, slot_check = 1;


  PROCESS_BEGIN();
  NETSTACK_MAC.on();
  LOG_INFO("Netstack is on\n");
  sixtop_add_sf(&sf_simple_driver);
  LOG_INFO("scheduling function added\n");

  // do {
  //   /* Randomly select a slot offset within TSCH_SCHEDULE_CONF_DEFAULT_LENGTH */
  //   random_slot = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_CONF_DEFAULT_LENGTH;
  //   random_channel = ((random_rand() & 0xFF)) % NUMBER_OF_CHANNELS;
  //   LOG_INFO("chose random slot and channel\n");

  //   //prevent it from occupying the same space as the minimal cell
  //   if(random_slot == 0 || random_slot == 5 || random_slot == 50 || random_slot == 20 || random_slot == 90 ||random_slot == 80){
  //     random_slot++;
  //   }

  //   /* Check list for already existing cell in the same timeslot */
  //   for(int i = 0; i < cell_list_len; i++) {
  //     if(cell_list[i].timeslot_offset != random_slot) {
  //       /* Random selection resulted in a free slot */
  //       if(i == cell_list_len - 1) { /* Checked till last index of link list */
  //         slot_check = 1;
  //         LOG_INFO("Cell has no collision with list\n");
  //         break;
  //       }
  //     } else {
  //       /* Slot already present in CandidateLinkList */
  //       slot_check++;
  //       break;
  //     }
  //   }

  //   /* Random selection resulted in a free slot, add it to linklist */
  //   if(slot_check == 1) {
  //     cell_list[cell_list_len].timeslot_offset = random_slot;
  //     cell_list[cell_list_len].channel_offset = random_channel;
  //     LOG_INFO("Added cells to interfereList\n");

  //     cell_list_len++;
  //     slot_check++;
  //   } else if(slot_check > TSCH_SCHEDULE_DEFAULT_LENGTH) {
  //     LOG_INFO("sf-simple:! Number of trials for free slot exceeded...\n");
  //     PROCESS_EXIT();
  //   }
  // } while(cell_list_len < NUM_CELLS_TO_INTERFERE);


  /* Wait for node to be reachable */
  while(!NETSTACK_ROUTING.node_is_reachable()){
    etimer_set(&sixp_et, CLOCK_SECOND * 3);
    PROCESS_YIELD_UNTIL(etimer_expired(&sixp_et));
  }


  etimer_set(&sixp_et, CLOCK_SECOND * 5);

  /* Add each randomly generated cell to cell list */
  while(1){
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&sixp_et));
    leds_on(LEDS_RED);
    LOG_INFO("Waiting for connection to be set up to add cells with list length: %u\n", INTERFERED_CELLS);

    if(NETSTACK_ROUTING.node_is_reachable() ){
      static int j = 0;
      for(j=0; j < INTERFERED_CELLS; j++){
        LOG_INFO("Adding link with slotoffset %u and channeloffset %u\n", network_interfere_cells[j].timeslot_offset, network_interfere_cells[j].channel_offset);
        add_links_to_schedule(&tsch_broadcast_address, LINK_OPTION_TX | LINK_OPTION_SHARED , (const uint8_t *)&network_interfere_cells[j], sizeof(sf_simple_cell_t));
        etimer_set(&sixp_et, CLOCK_SECOND / 50);
        PROCESS_YIELD_UNTIL(etimer_expired(&sixp_et));
      }
      break;
    }

    etimer_set(&sixp_et, CLOCK_SECOND);
  }
  LOG_INFO("Setting up 6P cells done\n");

  leds_off(LEDS_RED);

  PROCESS_END();
}