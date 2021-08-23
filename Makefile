CC = clang

CC_DEFINES =
CC_BASEFLAGS = -Wall -pedantic -std=gnu11
CC_OPTFLAGS = -O3
CC_DBGFLAGS = -g -Og
CC_LDFLAGS = -lpthread -lm

CXX = clang++

CXX_DEFINES = -DBOOST_LOG_DYN_LINK -DBOOST_BIND_GLOBAL_PLACEHOLDERS
CXX_BASEFLAGS = -Wall -pedantic -std=c++14
CXX_OPTFLAGS = -O3
CXX_DBGFLAGS = -g -Og -fsanitize=address
CXX_LDFLAGS = -lpthread -lboost_log -lboost_program_options -lboost_system

DEBUG = 1

ifdef DEBUG
	CC_FLAGS = $(CC_BASEFLAGS) $(CC_DBGFLAGS) $(CC_DEFINES)
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_DBGFLAGS) $(CXX_DEFINES)
else
	CC_FLAGS = $(CC_BASEFLAGS) $(CC_OPTFLAGS) $(CC_DEFINES)
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_OPTFLAGS) $(CXX_DEFINES)
endif

COMMON_INCLUDES = packet_proc.cpp vxsdr_header.hpp host_radio_options.hpp utility.hpp
COMMON_SRC = host_radio_options.cpp packet_proc.cpp utility.cpp

PROGS = host_tx_throttle vxsdr_test test_rx test_tx

all: $(PROGS) Makefile

.PHONY: all

host_tx_throttle: host_tx_throttle.cpp udp_tx_rx.hpp $(COMMON_INCLUDES)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

test_rx: test_rx.cpp test_tx_rx.hpp $(COMMON_INCLUDES)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

test_tx: test_tx.cpp test_tx_rx.hpp $(COMMON_INCLUDES)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_test: vxsdr_test.cpp vxsdr.hpp $(COMMON_INCLUDES)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~

.PHONY: clean
