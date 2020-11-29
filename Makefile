CC = gcc

CC_DEFINES =
CC_BASEFLAGS = -Wall -pedantic -std=gnu11
CC_OPTFLAGS = -O3
CC_DBGFLAGS = -g -Og
CC_LDFLAGS = -lpthread -lm

CXX = g++

CXX_DEFINES = -DBOOST_LOG_DYN_LINK
CXX_BASEFLAGS = -Wall -pedantic -std=c++14
CXX_OPTFLAGS = -O3
CXX_DBGFLAGS = -g -Og
CXX_LDFLAGS = -lpthread -lboost_log -lboost_program_options

#DEBUG = 1

ifdef DEBUG
	CC_FLAGS = $(CC_BASEFLAGS) $(CC_DBGFLAGS) $(CC_DEFINES)
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_DBGFLAGS) $(CXX_DEFINES)
else
	CC_FLAGS = $(CC_BASEFLAGS) $(CC_OPTFLAGS) $(CC_DEFINES)
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_OPTFLAGS) $(CXX_DEFINES)
endif

PROGS = host_tx host_rx host_tx_sync host_rx_sync host_tx_throttle host_tester minimal_tx minimal_rx

all: $(PROGS) Makefile

.PHONY: all

host_tx: host_tx.cpp udp_tx.hpp
	$(CXX) $(CXX_FLAGS) -o $@ $< $(CXX_LDFLAGS)

host_tx_sync: host_tx.cpp udp_tx_sync.hpp
	$(CXX) $(CXX_FLAGS) -DSYNC -o $@ $< $(CXX_LDFLAGS)

host_tx_throttle: host_tx_throttle.cpp udp_tx_rx.hpp
	$(CXX) $(CXX_FLAGS) -o $@ $< $(CXX_LDFLAGS)

host_tester: host_tester.cpp udp_tx_rx_testing.hpp
	$(CXX) $(CXX_FLAGS) -o $@ $< $(CXX_LDFLAGS)

host_rx: host_rx.cpp udp_rx.hpp
	$(CXX) $(CXX_FLAGS) -o $@ $< $(CXX_LDFLAGS)

host_rx_sync: host_rx.cpp udp_rx_sync.hpp
	$(CXX) $(CXX_FLAGS) -DSYNC -o $@ $< $(CXX_LDFLAGS)

minimal_tx: minimal_tx.c
	$(CC) $(CC_FLAGS) -o $@ $< $(CC_LDFLAGS)

minimal_rx: minimal_rx.c
	$(CC) $(CC_FLAGS) -o $@ $< $(CC_LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~

.PHONY: clean
