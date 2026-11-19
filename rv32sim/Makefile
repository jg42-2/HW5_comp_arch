# Compilación rápida sin CMake:  make        -> build/rv32sim
#                                 make tests  -> ensambla y prueba los programas
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
SRC      := src/main.cpp src/cpu.cpp src/disasm.cpp src/loader.cpp
HDR      := $(wildcard src/*.hpp)

build/rv32sim: $(SRC) $(HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

tests: build/rv32sim
	./tests/build_tests.sh
	./tests/run_tests.sh

clean:
	rm -rf build

.PHONY: tests clean
