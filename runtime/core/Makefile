# SPDX-License-Identifier: MIT
# The core's own tests (tests/core_tests.cpp) against the core's sources that
# need no JUCE, on Linux: `make test`, from tests/test_core.py or a product's
# build (./build.sh in rhythmer-sample and rhythmer-move run it in their build
# images). Products build the core through core.mk, never through this file.
CORE := .
include core.mk
CXX ?= g++
CXXFLAGS := -std=c++17 -O2 -pthread -Wall -Wextra -Werror -MMD -MP
JUCE_SOURCES := $(shell grep -l juce $(CORE_SOURCES))
TEST_SOURCES := $(filter-out $(JUCE_SOURCES),$(CORE_SOURCES)) tests/core_tests.cpp
TEST_OBJECTS := $(patsubst %.cpp,build/obj/%.o,$(TEST_SOURCES))

.PHONY: test
test: build/core-tests
	build/core-tests
build/core-tests: $(TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@
build/obj/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CORE_CPPFLAGS) $(CXXFLAGS) -c $< -o $@
-include $(TEST_OBJECTS:.o=.d)

# Rhythmer's GPL musical adaptation uses the same pinned, portable bridge as
# the product. Keep this dependency explicit in the modified core's own tests.
FM1_GEN_ROOT ?= ../fm1-gen
FM1_GEN_OBJ := build/obj/fm1-gen.o
build/core-tests: $(FM1_GEN_OBJ)
$(FM1_GEN_OBJ): $(shell find $(FM1_GEN_ROOT) -type f)
	@mkdir -p $(@D)
	$(CC) -O1 -w -ffunction-sections -fdata-sections -DFM1_HOST_TEST=1 -I$(FM1_GEN_ROOT)/generated -I$(FM1_GEN_ROOT)/firmware/src -I$(FM1_GEN_ROOT)/tests -c $(FM1_GEN_ROOT)/bridge.c -o $@
