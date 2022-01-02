
CXX = clang++-10

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

#LIB_DEFINES = -DVXSDR_SEQ_ERR_DIAG -DVXSDR_RESPONSE_TIME_DIAG #-DVXSDR_TX_BUFFER_DIAG
#LIB_INCLUDES = vxsdr_header.h vxsdr.hpp
#LIB_OBJ = vxsdr.o vxsdr_device_cmds.o vxsdr_radio_cmds.o vxsdr_udp.o
#LIB_CXXFLAGS = -fPIC $(LIB_DEFINES)

#AR = ar
#AR_FLAGS = rcs

PROGS = vxsdr_rx vxsdr_test vxsdr_test_rx vxsdr_test_commands vxsdr_test_atmosense_rx

#LIBS = libvxsdr.so libvxsdr.a

DOCS = docs/html/index.html

all: $(PROGS) Makefile

docs: $(DOCS)

.PHONY: all docs

.cpp.o:
	$(CXX) $(CXX_FLAGS) -c -o $@ $<

vxsdr_test: vxsdr_test.cpp $(COMMON_INCLUDES) $(COMMON_OBJ) #libvxsdr.a
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_rx: vxsdr_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ) #libvxsdr.a
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_rx: vxsdr_test_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ) #libvxsdr.a
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_atmosense_rx: vxsdr_test_atmosense_rx.cpp $(COMMON_INCLUDES) $(COMMON_OBJ) #libvxsdr.a
	$(CXX) $(CXX_FLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

vxsdr_test_commands: vxsdr_test_commands.cpp $(COMMON_INCLUDES) $(COMMON_OBJ) #libvxsdr.a
	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -o $@ $< $(COMMON_OBJ) $(CXX_LDFLAGS)

#compile_checks.o: compile_checks.cpp vxsdr_header.h
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -c -o $@ $<

#vxsdr.o: vxsdr.cpp vxsdr.hpp vxsdr_header.h
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -c -o $@ $<

#vxsdr_device_cmds.o: vxsdr_device_cmds.cpp vxsdr.hpp vxsdr_header.h
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -c -o $@ $<

#vxsdr_radio_cmds.o: vxsdr_radio_cmds.cpp vxsdr.hpp vxsdr_header.h
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -c -o $@ $<

#vxsdr_udp.o: vxsdr_udp.cpp vxsdr.hpp vxsdr_header.h
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -c -o $@ $<

#libvxsdr.so: $(LIB_OBJ)
#	$(CXX) $(CXX_FLAGS) $(LIB_CXXFLAGS) -o $@ -Wl,-soname,libvxsdr.so.1 -fPIC -shared $(LIB_OBJ)

#libvxsdr.a: $(LIB_OBJ)
#	$(AR) $(AR_FLAGS) $@ $^

docs/html/index.html: Doxyfile vxsdr.hpp
	doxygen Doxyfile

clean:
	rm -f $(PROGS) $(DOCS) *.o *~

.PHONY: clean
