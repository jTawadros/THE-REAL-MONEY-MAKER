# Heuristica — build with plain GNU make (no CMake needed).
#   make            -> ./build/heuristica (release)
#   make debug      -> ./build/heuristica (debug + sanitizers)
#   make test       -> builds & runs ./build/heuristica_tests
#   make clean

CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter
OPT      ?= -O2
CXXFLAGS ?=
CXXFLAGS += $(CXXSTD) $(WARN) $(OPT) -Isrc -MMD -MP
LDFLAGS  ?=

BUILD    := build
SRCS     := $(shell find src -name '*.cpp' ! -name 'main.cpp')
OBJS     := $(SRCS:%.cpp=$(BUILD)/%.o)
MAIN_OBJ := $(BUILD)/src/main.o
TEST_SRCS:= $(shell find tests -name '*.cpp' 2>/dev/null)
TEST_OBJS:= $(TEST_SRCS:%.cpp=$(BUILD)/%.o)

BIN      := $(BUILD)/heuristica
TEST_BIN := $(BUILD)/heuristica_tests

.PHONY: all debug test clean run headless

all: $(BIN)

$(BIN): $(OBJS) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(OBJS) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug:
	$(MAKE) OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address,undefined" BUILD=build-debug

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(BIN)
	./$(BIN)

headless: $(BIN)
	./$(BIN) --headless --days 120

clean:
	rm -rf build build-debug

-include $(OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(TEST_OBJS:.o=.d)
