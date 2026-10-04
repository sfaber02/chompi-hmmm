# Desktop build of the HMMM engine for listening tests (see render.cpp).
CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter

all: render boottest

render: render.cpp $(wildcard ../code/src/hmmm/*.h)
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

boottest: boottest.cpp $(wildcard ../code/src/hmmm/*.h)
	$(CXX) $(CXXFLAGS) -o $@ boottest.cpp

clean:
	rm -f render boottest
