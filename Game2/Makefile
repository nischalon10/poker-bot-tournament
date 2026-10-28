CXX        ?= c++
CXXFLAGS   ?= -std=c++20 -O3 -Wall -Wextra -pedantic
LDFLAGS    ?= -pthread

# Rules, RNG and opponent generators are shared with Game 1 so both games use identical populations.
GAME1_SRC  := ../Game1/src
CPPFLAGS   += -Isrc -I$(GAME1_SRC)

BUILD      := build
GAME1_OBJS := $(BUILD)/game1_blotto.o $(BUILD)/game1_opponents.o
LIB_OBJS   := $(GAME1_OBJS) $(BUILD)/fourway.o $(BUILD)/field.o $(BUILD)/search.o $(BUILD)/experiment.o
HEADERS    := $(wildcard src/*.hpp) $(wildcard $(GAME1_SRC)/*.hpp)

.PHONY: all test run clean

all: $(BUILD)/fourway $(BUILD)/test_fourway

$(BUILD)/game1_%.o: $(GAME1_SRC)/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.cpp $(HEADERS) | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/fourway: $(LIB_OBJS) $(BUILD)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/test_fourway: tests/test_fourway.cpp $(LIB_OBJS) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_fourway.cpp $(LIB_OBJS) -o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_fourway
	./$(BUILD)/test_fourway

run: $(BUILD)/fourway
	./$(BUILD)/fourway --seed 42 --scenario all --output outputs/

clean:
	rm -rf $(BUILD)
