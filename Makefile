
CXX = g++

CXX_DEFINES =
CXX_BASEFLAGS = -Wall -pedantic -std=c++17 $(CXX_EXTRA_FLAGS)
CXX_OPTFLAGS = -O3
CXX_DBGFLAGS = -g -Og -fsanitize=address
CXX_LDFLAGS = -lpthread -lboost_program_options -lboost_system -lvxsdr

# VXSDR_DEBUG=1

ifdef VXSDR_DEBUG
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_DBGFLAGS) $(CXX_DEFINES)
else
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_OPTFLAGS) $(CXX_DEFINES)
endif

COMMON_INCLUDES = host_radio_options.hpp utility.hpp logging.hpp
COMMON_OBJ = host_radio_options.o utility.o logging.o

PROGS = vxsdr_rx vxsdr_test vxsdr_test_rx vxsdr_test_tx vxsdr_test_tx_loop vxsdr_test_tx_rx vxsdr_test_simul

all: $(PROGS) Makefile

.PHONY: all

.cpp.o:
	$(CXX) $(CXX_FLAGS) -c -o $@ $<

vxsdr_test: vxsdr_test.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_rx: vxsdr_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_rx: vxsdr_test_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_tx: vxsdr_test_tx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_tx_bias: vxsdr_test_tx_bias.cpp inst_interface.hpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_tx_corr: vxsdr_test_tx_corr.cpp inst_interface.hpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_tx_corr: vxsdr_tx_corr.cpp inst_interface.hpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) neldermead.cpp $(CXX_LDFLAGS)

vxsdr_tx_corr_2: vxsdr_tx_corr_2.cpp inst_interface.hpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) neldermead.cpp $(CXX_LDFLAGS) -lfftw3f

vxsdr_test_tx_rx: vxsdr_test_tx_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
vxsdr_test_tx_loop: vxsdr_test_tx_loop.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_tx_rx: vxsdr_test_tx_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_simul: vxsdr_test_simul.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~

.PHONY: clean
