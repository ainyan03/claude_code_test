# fleximg Test Suite Makefile
#
# Usage:
#   make           全テストをビルド
#   make test      全テストをビルドして実行
#   make clean     ビルド成果物を削除
#   make <name>    特定のテストをビルド (例: make viewport_test)

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../src
SRC_DIR = ../src/fleximg
OPS_DIR = ../src/fleximg/operations

# ソースファイル
CORE_SRCS = $(SRC_DIR)/viewport.cpp $(SRC_DIR)/pixel_format_registry.cpp
OPS_SRCS = $(OPS_DIR)/blend.cpp $(OPS_DIR)/filters.cpp

# テストバイナリ
TESTS = viewport_test integration_test calc_valid_range_test trapezoid_fit_test box_blur_test tile_origin_test affine_push_pull_test

.PHONY: all test clean

all: $(TESTS)

test: $(TESTS)
	@echo "========================================="
	@echo "Running all tests..."
	@echo "========================================="
	@for t in $(TESTS); do \
		echo ""; \
		echo "--- Running $$t ---"; \
		./$$t || exit 1; \
	done
	@echo ""
	@echo "========================================="
	@echo "All tests passed!"
	@echo "========================================="

# viewport_test: ViewPort/ImageBuffer 単体テスト
viewport_test: viewport_test.cpp $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# integration_test: パイプライン統合テスト
integration_test: integration_test.cpp $(CORE_SRCS) $(OPS_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# calc_valid_range_test: DDA範囲計算テスト（ヘッダーのみ）
calc_valid_range_test: calc_valid_range_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# trapezoid_fit_test: AABB分割台形フィットテスト（独立）
trapezoid_fit_test: trapezoid_fit_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# box_blur_test: ボックスブラーテスト
box_blur_test: box_blur_test.cpp $(CORE_SRCS) $(OPS_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# tile_origin_test: タイル分割時のorigin端数処理テスト
tile_origin_test: tile_origin_test.cpp $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# affine_push_pull_test: アフィン変換のプル/プッシュモード整合性テスト
affine_push_pull_test: affine_push_pull_test.cpp $(CORE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
