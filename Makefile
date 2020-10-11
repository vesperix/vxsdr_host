CXX=g++

DEFINES=

CXXFLAGS=-Wall -pedantic -O3 -march=native -std=c++14 $(DEFINES)

LDFLAGS = -lboost_program_options -lboost_date_time -lpthread

PROGS := host_tx host_rx

all: $(PROGS) Makefile

host_tx: host_tx.cpp udp_server.hpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

host_rx: host_rx.cpp udp_server.hpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~
