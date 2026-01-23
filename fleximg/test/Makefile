# fleximg Test Suite Makefile (doctest)
#
# Usage:
#   make                 全テストをビルド
#   make test            全テストをビルドして実行
#   make all_tests       統合テストバイナリをビルド
#   make <name>_test     個別テストをビルド (例: make viewport_test)
#   make clean           ビルド成果物を削除
#
# 統合バイナリの便利なオプション:
#   ./all_tests --list-test-cases     テストケース一覧
#   ./all_tests --test-case="*viewport*"  特定テストのみ実行
#   ./all_tests -s                    成功テストも表示
#
# ベンチマーク:
#   ベンチマークは examples/bench/ に統合されました
#   pio run -e native && .pio/build/native/program  (PC)
#   pio run -e m5stack_core2 -t upload              (M5Stack)

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic \
           -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wdouble-promotion \
           -Wformat=2 -Wnull-dereference -Wunused \
           -I../src -I../third_party/doctest

# ソースディレクトリ
SRC_DIR = ../src/fleximg
MEMORY_DIR = ../src/fleximg/core/memory
OPS_DIR = ../src/fleximg/operations

# ライブラリソース
# Note: fleximg.cpp は stb方式で全実装を含む（pixel_format.h等）
FLEXIMG_MAIN = $(SRC_DIR)/fleximg.cpp
ALL_LIB_SRCS = $(FLEXIMG_MAIN)

# テストソース（test_main.cpp以外）
TEST_SRCS = $(filter-out test_main.cpp, $(wildcard *_test.cpp))
TEST_BINS = $(TEST_SRCS:.cpp=)

# ベンチマークソース
BENCH_SRCS = $(wildcard *_bench.cpp)
BENCH_BINS = $(BENCH_SRCS:.cpp=)

.PHONY: all test bench clean list

# デフォルト: 全テストバイナリをビルド
all: $(TEST_BINS) all_tests

# ベンチマークをビルド
bench: $(BENCH_BINS)

# 全テスト実行
test: all
	@echo "========================================="
	@echo "Running all tests..."
	@echo "========================================="
	@./all_tests
	@echo ""
	@echo "========================================="
	@echo "All tests completed!"
	@echo "========================================="

# 統合テストバイナリ
all_tests: test_main.cpp $(TEST_SRCS) $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# 個別テストバイナリのパターンルール
%_test: %_test.cpp test_main.cpp $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# ベンチマークバイナリのパターンルール
%_bench: %_bench.cpp test_main.cpp $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) -O3 -o $@ $^

# テスト一覧表示
list:
	@echo "Available tests:"
	@for t in $(TEST_BINS); do echo "  $$t"; done
	@echo ""
	@echo "Run 'make <name>' to build individual test"
	@echo "Run 'make test' to build and run all tests"

# クリーンアップ
clean:
	rm -f $(TEST_BINS) $(BENCH_BINS) all_tests *.o
