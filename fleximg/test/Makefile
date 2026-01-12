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

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../src -I../third_party/doctest

# ソースディレクトリ
SRC_DIR = ../src/fleximg
OPS_DIR = ../src/fleximg/operations

# ライブラリソース
CORE_SRCS = $(SRC_DIR)/viewport.cpp $(SRC_DIR)/pixel_format_registry.cpp
OPS_SRCS = $(OPS_DIR)/blend.cpp $(OPS_DIR)/filters.cpp
ALL_LIB_SRCS = $(CORE_SRCS) $(OPS_SRCS)

# テストソース（test_main.cpp以外）
TEST_SRCS = $(filter-out test_main.cpp, $(wildcard *_test.cpp))
TEST_BINS = $(TEST_SRCS:.cpp=)

.PHONY: all test clean list

# デフォルト: 全テストバイナリをビルド
all: $(TEST_BINS) all_tests

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

# テスト一覧表示
list:
	@echo "Available tests:"
	@for t in $(TEST_BINS); do echo "  $$t"; done
	@echo ""
	@echo "Run 'make <name>' to build individual test"
	@echo "Run 'make test' to build and run all tests"

# クリーンアップ
clean:
	rm -f $(TEST_BINS) all_tests *.o
