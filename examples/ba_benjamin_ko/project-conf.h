#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/*******************************************************/
/********************* Enable TSCH *********************/
/*******************************************************/

/* Needed for CC2538 platforms only */
/* For TSCH we have to use the more accurate crystal oscillator
 * by default the RC oscillator is activated */
#define SYS_CTRL_CONF_OSC32K_USE_XTAL 1

/* Needed for cc2420 platforms only */
/* Disable DCO calibration (uses timerB) */
#define DCOSYNCH_CONF_ENABLED 0
/* Enable SFD timestamps (uses timerB) */
#define CC2420_CONF_SFD_TIMESTAMPS 1

/* Enable Sixtop Implementation */
#define TSCH_CONF_WITH_SIXTOP 1

/*******************************************************/
/******************* Configure TSCH ********************/
/*******************************************************/

/* IEEE802.15.4 PANID */
#define IEEE802154_CONF_PANID 0xabcd

/* Do not start TSCH at init, wait for NETSTACK_MAC.on() */
#define TSCH_CONF_AUTOSTART 0

/* 6TiSCH schedule length */
#define TSCH_SCHEDULE_CONF_DEFAULT_LENGTH 101

// #define LOG_CONF_LEVEL_IPV6                        LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_RPL                         LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_6LOWPAN                     LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_TCPIP                       LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_MAC                         LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_FRAMER                      LOG_LEVEL_DBG
// #define LOG_CONF_LEVEL_6TOP                         LOG_LEVEL_DBG

/* Turn on when flashing parent */
// #define TSCH_CONF_MAX_EB_PERIOD (8 * CLOCK_SECOND)

#define TSCH_SCHEDULE_CONF_MAX_LINKS 90

#define QUEUEBUF_CONF_NUM 32

#define NETWORK_IDENTIFIER 2
#define CHILD_IDENTIFIER 1
#endif /* PROJECT_CONF_H_ */