
CXX = clang++-10

CXX_DEFINES = -DVXSDR_SEQ_ERR_DIAG -DVXSDR_TX_BUFFER_DIAG
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
COMMON_SRC = packet_proc.cpp utility.cpp logging.cpp compile_checks.cpp

LIB_SRC = vxsdr.cpp vxsdr_udp.cpp
LIB_LDFLAGS = -lpthread

PROGS = vxsdr_test vxsdr_test_rx vxsdr_test_commands vxsdr_test_atmosense_rx

LIBS = libvxsdr.so

DOCS = docs/html/index.html

all: $(PROGS) $(LIBS) Makefile

docs: $(DOCS)

.PHONY: all docs

vxsdr_test: vxsdr_test.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) #libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(LIB_SRC) $(CXX_LDFLAGS) #-L. -lvxsdr

vxsdr_test_rx: vxsdr_test_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) #libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(LIB_SRC) $(CXX_LDFLAGS) #-L. -lvxsdr

vxsdr_test_atmosense_rx: vxsdr_test_atmosense_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) #libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(LIB_SRC) $(CXX_LDFLAGS) #-L. -lvxsdr

vxsdr_test_commands: vxsdr_test_commands.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC) #libvxsdr.so
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(LIB_SRC) $(CXX_LDFLAGS) #-L. -lvxsdr

libvxsdr.so: vxsdr.hpp $(LIB_SRC) $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ -fPIC -shared $(LIB_SRC) $(COMMON_SRC) $(LIB_LDFLAGS)

docs/html/index.html: Doxyfile vxsdr.hpp
	doxygen Doxyfile

clean:
	rm -f $(PROGS) $(LIBS) $(DOCS) *.o *~

.PHONY: clean
