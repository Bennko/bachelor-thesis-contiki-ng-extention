#ifndef _SIXTOP_SF_SIMPLE_H_
#define _SIXTOP_SF_SIMPLE_H_

#include "net/linkaddr.h"


typedef struct {
    uint16_t timeslot_offset;
    uint16_t channel_offset;
} sf_simple_cell_t;

typedef struct {
  linkaddr_t addr;
  uint8_t code;
} linkaddr_packet_t;


void add_mock_auto_cell(const linkaddr_t *peer_addr, uint8_t link_option, const uint8_t timeslot, const uint8_t channelOffset);
void add_links_to_schedule(const linkaddr_t *peer_addr, uint8_t link_option, const uint8_t *cell_list, uint16_t cell_list_len);
int sf_simple_add_links(linkaddr_t *peer_addr, uint8_t num_links);
int sf_simple_remove_links(linkaddr_t *peer_addr);
int sf_simple_relocate_links(linkaddr_t *peer_addr, uint8_t num_links, sf_simple_cell_t *cell_to_relocate);

#define SF_SIMPLE_MAX_LINKS  4
#define SF_SIMPLE_SFID       0xf0
#define NUMBER_OF_CHANNELS 4

extern const sixtop_sf_t sf_simple_driver;

#endif /* !_SIXTOP_SF_SIMPLE_H_ */
