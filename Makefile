
CXX = clang++-10

CXX_DEFINES = -DVXSDR_SEQ_ERR_DIAG
CXX_BASEFLAGS = -Wall -pedantic -std=c++17 $(CXX_EXTRA_FLAGS)
CXX_OPTFLAGS = -O3
CXX_DBGFLAGS = -g -Og -fsanitize=address
CXX_LDFLAGS = -lpthread -lboost_program_options -lboost_system -lspdlog

ifdef VXSDR_DEBUG
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_DBGFLAGS) $(CXX_DEFINES)
else
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_OPTFLAGS) $(CXX_DEFINES)
endif

COMMON_INCLUDES = packet_proc.hpp vxsdr_header.h host_radio_options.hpp utility.hpp logging.hpp
COMMON_SRC = packet_proc.cpp utility.cpp logging.cpp

LIB_SRC = vxsdr.cpp vxsdr_udp.cpp
LIB_LDFLAGS = -lpthread

PROGS = vxsdr_test vxsdr_test_rx test_rx test_tx test_rx_seq_only

LIBS = libvxsdr.so

all: $(PROGS) $(LIBS) Makefile

.PHONY: all

test_rx: test_rx.cpp test_tx_rx.hpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -DBOOST_BIND_GLOBAL_PLACEHOLDERS -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

test_rx_seq_only: test_rx_seq_only.cpp test_tx_rx.hpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -DBOOST_BIND_GLOBAL_PLACEHOLDERS -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

test_tx: test_tx.cpp test_tx_rx.hpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -DBOOST_BIND_GLOBAL_PLACEHOLDERS -o $@ $< $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_test: vxsdr_test.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS) -L. -lvxsdr

vxsdr_test_rx: vxsdr_test_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS) -L. -lvxsdr

libvxsdr.so: vxsdr.hpp $(LIB_SRC) $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ -fPIC -shared $(LIB_SRC) $(COMMON_SRC) $(LIB_LDFLAGS)

clean:
	rm -f $(PROGS) $(LIBS) *.o *~

.PHONY: clean
