
CXX = clang++-10

CXX_DEFINES = -DVXSDR_SEQ_ERR_DIAG -DVXSDR_RESPONSE_TIME_DIAG #-DVXSDR_TX_BUFFER_DIAG
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
COMMON_SRC = packet_proc.cpp utility.cpp logging.cpp compile_checks.cpp vxsdr.cpp vxsdr_udp.cpp

LIB_INCLUDES = vxsdr_header.h vxsdr.hpp
LIB_SRC = vxsdr.cpp vxsdr_udp.cpp
LIB_LDFLAGS =

AR = ar
AR_FLAGS = rcs

PROGS = vxsdr_rx vxsdr_test vxsdr_test_rx vxsdr_test_commands vxsdr_test_atmosense_rx

LIBS = libvxsdr.so libvxsdr.a

DOCS = docs/html/index.html

all: $(PROGS) $(LIBS) Makefile

docs: $(DOCS)

.PHONY: all docs

.cpp.o:
	$(CXX) $(CXX_FLAGS) -c -o $@ $<

vxsdr_test: vxsdr_test.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_rx: vxsdr_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_test_rx: vxsdr_test_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_test_atmosense_rx: vxsdr_test_atmosense_rx.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS)

vxsdr_test_commands: vxsdr_test_commands.cpp host_radio_options.cpp $(COMMON_INCLUDES) $(COMMON_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ $< host_radio_options.cpp $(COMMON_SRC) $(CXX_LDFLAGS)

libvxsdr.so: $(LIB_INCLUDES) $(LIB_SRC)
	$(CXX) $(CXX_FLAGS) -o $@ -Wl,-soname,libvxsdr.so.1 -fPIC -shared $(LIB_SRC) $(LIB_LDFLAGS)

libvxsdr.a: vxsdr.o vxsdr_udp.o
	$(AR) $(AR_FLAGS) $@ $^

docs/html/index.html: Doxyfile vxsdr.hpp
	doxygen Doxyfile

clean:
	rm -f $(PROGS) $(LIBS) $(DOCS) *.o *~

.PHONY: clean
