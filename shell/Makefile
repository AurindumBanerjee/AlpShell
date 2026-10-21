# AlpShell build.
#
#   make            build build/alpshell
#   make asan       build build-asan/alpshell with AddressSanitizer + UBSan
#   make test       build and run all tests: unit, script (against bash), command-line, pty and fork-failure tests
#   make test-asan  the same, using the sanitizer build
#   make clean
#
# Sources are discovered with a wildcard, and -MMD/-MP make every object depend on the headers it
# includes, so editing a header rebuilds exactly the files that use it.

CXX      ?= g++
CXXSTD   := -std=c++17
WARNINGS := -Wall -Wextra
CPPFLAGS += -Iinclude -MMD -MP
CXXFLAGS ?= -g -O2
LDLIBS   += -lreadline

BUILD  ?= build
TARGET := $(BUILD)/alpshell

LIB_SRCS  := $(wildcard src/*.cpp)
LIB_OBJS  := $(LIB_SRCS:%.cpp=$(BUILD)/%.o)
MAIN_OBJ  := $(BUILD)/main.o
UNIT_OBJ  := $(BUILD)/tests/unit_tests.o
UNIT_BIN  := $(BUILD)/unit_tests
DEPS      := $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(UNIT_OBJ:.o=.d)

SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined

.PHONY: all asan test test-asan clean

all: $(TARGET)

$(TARGET): $(MAIN_OBJ) $(LIB_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXSTD) $(WARNINGS) $(CXXFLAGS) -c $< -o $@

$(UNIT_BIN): $(UNIT_OBJ) $(LIB_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS) $(LDLIBS)

asan:
	$(MAKE) BUILD=build-asan CXXFLAGS="-g -O1 $(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)" all

test: $(TARGET) $(UNIT_BIN)
	$(UNIT_BIN)
	python3 tests/run_script_tests.py --shell $(TARGET)
	python3 tests/cli_test.py --shell $(TARGET)
	python3 tests/pty_tests.py --shell $(TARGET)
	python3 tests/fork_failure_test.py --shell $(TARGET)

test-asan:
	$(MAKE) BUILD=build-asan CXXFLAGS="-g -O1 $(SAN_FLAGS)" LDFLAGS="$(SAN_FLAGS)" test

clean:
	rm -rf build build-asan

-include $(DEPS)
