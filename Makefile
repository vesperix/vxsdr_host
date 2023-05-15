
CXX = g++

CXX_DEFINES =
CXX_BASEFLAGS = -Wall -pedantic -std=c++17 $(CXX_EXTRA_FLAGS)
CXX_OPTFLAGS = -O3
CXX_DBGFLAGS = -g -Og -fsanitize=address
CXX_LDFLAGS = -lpthread -lboost_program_options -lboost_system -lspdlog -lvxsdr

ifdef VXSDR_DEBUG
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_DBGFLAGS) $(CXX_DEFINES)
else
	CXX_FLAGS = $(CXX_BASEFLAGS) $(CXX_OPTFLAGS) $(CXX_DEFINES)
endif

COMMON_INCLUDES = host_radio_options.hpp utility.hpp logging.hpp
COMMON_OBJ = host_radio_options.o utility.o logging.o

PROGS = vxsdr_rx vxsdr_test vxsdr_test_rx vxsdr_test_tx vxsdr_test_commands vxsdr_test_atmosense_rx

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

vxsdr_test_atmosense_rx: vxsdr_test_atmosense_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_commands: vxsdr_test_commands.cpp $(COMMON_INCLUDES) $(COMMON_OBJ)
	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

clean:
	rm -f $(PROGS) *.o *~

.PHONY: clean
