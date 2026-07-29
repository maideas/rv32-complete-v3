# Makefile for the RV32 reference model and its test suite.
# All build artifacts go into build/ (see AGENTS.md).

BUILD   := build
CXX     := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -fsanitize=undefined -I.
HEADERS := $(wildcard *.hpp)

.PHONY: all test clean

all: $(BUILD)/test_riscv

$(BUILD)/test_riscv: test_riscv_model.cpp $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(BUILD)/test_riscv
	./$(BUILD)/test_riscv

clean:
	rm -rf $(BUILD)
