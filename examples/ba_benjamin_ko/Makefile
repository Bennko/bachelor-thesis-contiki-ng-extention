CONTIKI_PROJECT = parent child network
all: $(CONTIKI_PROJECT)

PLATFORMS_EXCLUDE = sky z1 native

PROJECT_SOURCEFILES += sf-simple.c network_interference_cells.c advanced_cell_alloc.c
CONTIKI=../..

MAKE_WITH_SECURITY ?= 0 # force Security from command line

MAKE_MAC = MAKE_MAC_TSCH

include $(CONTIKI)/Makefile.dir-variables
MODULES += $(CONTIKI_NG_MAC_DIR)/tsch/sixtop

ifeq ($(MAKE_WITH_SECURITY),1)
CFLAGS += -DWITH_SECURITY=1
endif

MODULES += os/services/shell

include $(CONTIKI)/Makefile.include
