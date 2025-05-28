#include "contiki.h"
#include "sys/node-id.h"
#include "sys/log.h"
#include "net/ipv6/uip-ds6-route.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "dev/leds.h"
#include "project-conf.h"
#include "net/ipv6/uip-ds6-nbr.h"
#include "net/ipv6/uip-ds6.h"
#include "net/nbr-table.h"
#include "net/linkaddr.h"

#include "sf-simple.h"
#include "tsch-const.h"
#include "net/ipv6/uip.h"


#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

#define WITH_SERVER_REPLY  0
#define UDP_CLIENT_PORT	8765
#define UDP_SERVER_PORT	5678

#define DEBUG DEBUG_PRINT
#define SFSIMPLE 
#include "net/ipv6/uip-debug.h"

static struct simple_udp_connection udp_conn;
static uint8_t set_auto_tx_network_cell = 0;
static uint8_t set_auto_tx_child_cell = 0;
static uip_ipaddr_t network_interferer_addr;

PROCESS(udp_server_process, "UDP server");
PROCESS(sixp_process, "sixp message processor");
AUTOSTART_PROCESSES(&sixp_process, &udp_server_process);

static void
udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  /* Only evaluate request if needed*/
  if(set_auto_tx_network_cell == 0 || set_auto_tx_child_cell == 0){
    const linkaddr_packet_t *packet = (const linkaddr_packet_t *) data;

    if (packet->code == NETWORK_IDENTIFIER && set_auto_tx_network_cell == 0) {
      LOG_INFO("Recieved multicast/broadcast UDP packet from:");
      LOG_INFO_6ADDR(sender_addr);
      LOG_INFO("\n");
      add_mock_auto_cell(&(packet->addr), LINK_OPTION_RX, 20, 2);
      set_auto_tx_network_cell = 1;
  
      /* Save network interferer adress for later */
      network_interferer_addr = *sender_addr;
      leds_on(LEDS_RED);
    }
    
    /* If not yet done, using the UDP payload carrying the neighbours address setting up a auto Tx cell with that neighbour */
    else if(packet->code == CHILD_IDENTIFIER && set_auto_tx_child_cell == 0){
      LOG_INFO("Recieved multicast/broadcast UDP packet from child and saved network interferer is:");
      LOG_INFO_6ADDR(&network_interferer_addr);
      LOG_INFO("\n");
      add_mock_auto_cell(&(packet->addr), LINK_OPTION_TX, 5, 2);
      add_mock_auto_cell(&(packet->addr), LINK_OPTION_TX, 50, 3);
      add_mock_auto_cell(&(packet->addr), LINK_OPTION_TX, 80, 1);
      add_mock_auto_cell(&(packet->addr), LINK_OPTION_RX, 90, 3);
      LOG_INFO("Set up auto Tx and Rx cells");
      set_auto_tx_child_cell = 1;
  
      /* Signal to network it can start interfering */
      simple_udp_sendto(&udp_conn, &linkaddr_node_addr, sizeof(linkaddr_node_addr), &network_interferer_addr);
      leds_on(LEDS_GREEN);
    }
  }
  

  /* LOGGING */
  // LOG_INFO("Received request with data %u length %u from ", packet->code, datalen );
  // LOG_INFO_6ADDR(sender_addr);
  // LOG_INFO_("\n");
}

PROCESS_THREAD(udp_server_process, ev, data)
{
  PROCESS_BEGIN();

  /* Initialize UDP connection */
  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL, UDP_CLIENT_PORT, udp_rx_callback);
  LOG_INFO("Registering UDP\n");

  PROCESS_END();
}

PROCESS_THREAD(sixp_process, ev, data)
{
  static int is_coordinator = 1;

  PROCESS_BEGIN();
  LOG_INFO("Setting up network\n");

  /* start tsch network and RPL routing */
  if(is_coordinator) {
    NETSTACK_ROUTING.root_start();
  }
  NETSTACK_MAC.on();
  sixtop_add_sf(&sf_simple_driver);

  PROCESS_END();
}