# fleximg - Native Build Makefile
# This Makefile builds CLI tools and test runners for native environments.
# For WebAssembly demo build, use build.sh instead.
#
# Usage:
#   make all                    リリースビルド
#   make all FLEXIMG_DEBUG=1    デバッグビルド（性能計測有効）
#   make test FLEXIMG_DEBUG=1   デバッグビルドでテスト実行

CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -g
INCLUDES := -Isrc -Ithird_party

# デバッグフラグ（FLEXIMG_DEBUG=1 で有効化）
ifdef FLEXIMG_DEBUG
CXXFLAGS += -DFLEXIMG_DEBUG
endif

# Google Test (via Homebrew)
GTEST_CFLAGS := $(shell pkg-config --cflags gtest gtest_main 2>/dev/null || echo "-I/opt/homebrew/include")
GTEST_LIBS := $(shell pkg-config --libs gtest gtest_main 2>/dev/null || echo "-L/opt/homebrew/lib -lgtest -lgtest_main -pthread")

# Source files (core library, excluding WASM bindings)
CORE_SRCS := src/fleximg/pixel_format_registry.cpp \
             src/fleximg/viewport.cpp \
             src/fleximg/image_buffer.cpp \
             src/fleximg/operators.cpp \
             src/fleximg/node_graph.cpp \
             src/fleximg/evaluation_node.cpp

CORE_OBJS := $(CORE_SRCS:.cpp=.o)

# Output directories
BUILD_DIR := build
TEST_DIR := test

# Targets
.PHONY: all clean test cli help

all: cli test_runner

help:
	@echo "Usage:"
	@echo "  make all         - Build CLI tool and test runner"
	@echo "  make cli         - Build CLI tool only"
	@echo "  make test_runner - Build test runner only"
	@echo "  make test        - Build and run tests"
	@echo "  make clean       - Remove build artifacts"

# ==============================================================================
# CLI Tool
# ==============================================================================

cli: $(BUILD_DIR)/imgproc

$(BUILD_DIR)/imgproc: cli/main.cpp cli/stb_impl.cpp $(CORE_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# ==============================================================================
# Test Runner
# ==============================================================================

test_runner: $(BUILD_DIR)/test_runner

TEST_SRCS := $(TEST_DIR)/viewport_test.cpp \
             $(TEST_DIR)/affine_mapping_test.cpp

$(BUILD_DIR)/test_runner: $(TEST_SRCS) $(CORE_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(GTEST_CFLAGS) -o $@ $^ $(GTEST_LIBS)

test: $(BUILD_DIR)/test_runner
	./$(BUILD_DIR)/test_runner

# ==============================================================================
# Core Library Objects
# ==============================================================================

src/fleximg/%.o: src/fleximg/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# ==============================================================================
# Directories
# ==============================================================================

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ==============================================================================
# Clean
# ==============================================================================

clean:
	rm -rf $(BUILD_DIR)
	rm -f src/fleximg/*.o
