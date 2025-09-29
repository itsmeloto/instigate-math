# Simple Makefile for Atelier terminal
run-offload:
	__NV_PRIME_RENDER_OFFLOAD=1 \
	__GLX_VENDOR_LIBRARY_NAME=nvidia \
	./atelier

CXX := g++
CXXFLAGS := -std=c++17 -O2 $(shell sdl2-config --cflags) $(shell pkg-config --cflags SDL2_ttf 2>/dev/null)
LDFLAGS := $(shell sdl2-config --libs) -lSDL2_image $(shell pkg-config --libs SDL2_ttf 2>/dev/null)

BIN := atelier
SRC := main.cpp

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
