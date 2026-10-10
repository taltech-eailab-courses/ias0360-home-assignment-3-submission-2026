# Host (PC) test of src/position_estimator.cpp against the float64 Python reference:
#   make -C test/host test        (golden data: `make golden`, needs numpy + scipy)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -I../../src -DGOLDEN_DIR=\"$(CURDIR)/golden\"

test: build/test_estimator
	build/test_estimator

build/test_estimator: test_estimator.cpp ../../src/position_estimator.cpp ../../src/position_estimator.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) test_estimator.cpp ../../src/position_estimator.cpp -o $@

golden:
	cd ../../tools/air_writing && python3 make_golden.py

.PHONY: test golden
