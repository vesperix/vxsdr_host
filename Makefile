CXX = g++

DEFINES = -DBOOST_LOG_DYN_LINK

BASEFLAGS = -Wall -pedantic -std=c++14
OPTFLAGS = -O3
DBGFLAGS = -g -Og

#DEBUG = 1

ifdef DEBUG
	CXXFLAGS = $(BASEFLAGS) $(DBGFLAGS) $(DEFINES)
else
	CXXFLAGS = $(BASEFLAGS) $(OPTFLAGS) $(DEFINES)
endif

LDFLAGS = -lpthread -lboost_log -lboost_program_options
PROGS = host_tx host_rx

all: $(PROGS) Makefile

.PHONY: all

host_tx: host_tx.cpp udp_tx.hpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

host_rx: host_rx.cpp udp_rx.hpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~

.PHONY: clean