OBJS = 98ripper.o _fnmatch.o \
	fat.o fat8.o fatdo.o elfdos.o makodos.o iso.o \
	d88.o fdd.o fdi.o hdi.o nhd.o nfd.o dcp.o dim.o dip.o fim.o
BIN = 98ripper
CXXFLAGS = -O2 -Wall -std=c++17

ifeq ($(OS),Windows_NT)
	LDFLAGS = -liconv
	CXXFLAGS += -s -static
else
	LDFLAGS =
endif

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(OBJS) $(LDFLAGS)

98ripper.o fat.o fat8.o fatdo.o elfdos.o makodos.o iso.o: _fnmatch.h

$(OBJS): 98ripper.h

clean:
	$(RM) ${OBJS} ${BIN}
