CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pedantic
LDFLAGS  ?= -pthread

BUILD := build
LIB_SRCS := src/blotto.cpp src/opponents.cpp src/optimize.cpp src/simulate.cpp src/report.cpp src/plots.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=$(BUILD)/%.o)
HEADERS  := $(wildcard src/*.hpp)

.PHONY: all test run clean

all: $(BUILD)/blotto $(BUILD)/test_blotto

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/blotto: $(LIB_OBJS) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/test_blotto: tests/test_blotto.cpp $(LIB_OBJS) $(HEADERS)
	$(CXX) $(CXXFLAGS) tests/test_blotto.cpp $(LIB_OBJS) -o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_blotto
	./$(BUILD)/test_blotto

run: $(BUILD)/blotto
	./$(BUILD)/blotto --samples 50000 --seed 42 --scenario all --output outputs/

clean:
	rm -rf $(BUILD)
