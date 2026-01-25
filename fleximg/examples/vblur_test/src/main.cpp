// VerticalBlurNode 座標検証テスト
// radius=0 と radius=1 の比較（完全なパイプライン）
//
// ビルド: pio run -e vblur_test_native && .pio/build/vblur_test_native/program

#include <cstdio>
#include <cstdint>
#include <cstring>

#define FLEXIMG_NAMESPACE fleximg
#define FLEXIMG_IMPLEMENTATION
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/image/viewport.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/vertical_blur_node.h"
#include "fleximg/nodes/renderer_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/image/pixel_format.h"

using namespace fleximg;

// 画面サイズ
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 240;

// 左端ピクセルの位置を探す
int findLeftEdgeX(const ImageBuffer& buffer, int y) {
    if (y < 0 || y >= buffer.height()) return -1;

    const uint8_t* row = static_cast<const uint8_t*>(buffer.data()) + y * buffer.width() * 4;
    for (int x = 0; x < buffer.width(); ++x) {
        if (row[x * 4 + 3] > 0) {  // アルファ > 0
            return x;
        }
    }
    return -1;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    fprintf(stderr, "\n=== VerticalBlurNode Coordinate Test (Full Pipeline) ===\n\n");
    fprintf(stderr, "INT_FIXED_SHIFT = %d\n", INT_FIXED_SHIFT);
    fflush(stderr);

    // テスト画像作成（左端赤、右端青）
    ImageBuffer testImg(100, 100, PixelFormatIDs::RGBA8_Straight);
    for (int y = 0; y < 100; ++y) {
        uint8_t* row = static_cast<uint8_t*>(testImg.pixelAt(0, y));
        for (int x = 0; x < 100; ++x) {
            uint8_t* p = row + x * 4;
            if (x == 0) {
                p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255;  // 赤
            } else if (x == 99) {
                p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255;  // 青
            } else {
                p[0] = 0; p[1] = 255; p[2] = 0; p[3] = 255;  // 緑
            }
        }
    }
    fprintf(stderr, "Test image created: %dx%d\n", testImg.width(), testImg.height());
    fflush(stderr);

    // 出力バッファ
    ImageBuffer outputBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, PixelFormatIDs::RGBA8_Straight);

    // ノード構築
    SourceNode source;
    source.setSource(testImg.view());
    // pivotは画像左上（デフォルト）

    VerticalBlurNode vblur;

    // RendererNodeの原点を画面左上に設定
    RendererNode renderer;
    renderer.setVirtualScreen(SCREEN_WIDTH, SCREEN_HEIGHT, to_fixed(0), to_fixed(0));

    // SinkNodeも原点を左上に設定
    SinkNode sink;
    ViewPort outputView = outputBuffer.view();
    sink.setTarget(outputView);
    sink.setOrigin(to_fixed(0), to_fixed(0));

    // パイプライン接続
    source >> vblur >> renderer >> sink;
    fprintf(stderr, "Pipeline: Source -> VerticalBlur -> Renderer -> Sink\n");
    fprintf(stderr, "Origin: (0, 0) - top-left\n\n");
    fflush(stderr);

    // まずシンプルなテスト：画像を(0, 0)に配置して描画
    fprintf(stderr, "=== Basic test: source at (0, 0), radius=0 ===\n");
    fflush(stderr);

    std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
    source.setPosition(0.0f, 0.0f);
    vblur.setRadius(0);
    PrepareStatus status = renderer.exec();
    fprintf(stderr, "exec() status: %d\n", static_cast<int>(status));

    // 各行の左端位置を確認
    for (int y = 0; y < 10; ++y) {
        int leftEdge = findLeftEdgeX(outputBuffer, y);
        fprintf(stderr, "  Y=%d: leftEdgeX=%d\n", y, leftEdge);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    // radius=1 でも同様
    fprintf(stderr, "=== Basic test: source at (0, 0), radius=1 ===\n");
    fflush(stderr);

    std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
    source.setPosition(0.0f, 0.0f);
    vblur.setRadius(1);
    status = renderer.exec();
    fprintf(stderr, "exec() status: %d\n", static_cast<int>(status));

    for (int y = 0; y < 10; ++y) {
        int leftEdge = findLeftEdgeX(outputBuffer, y);
        fprintf(stderr, "  Y=%d: leftEdgeX=%d\n", y, leftEdge);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    // 画像を少しずらして配置
    fprintf(stderr, "=== Test: source at (50, 50), radius=0 vs radius=1 ===\n\n");
    fflush(stderr);

    for (int radius = 0; radius <= 1; ++radius) {
        std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
        source.setPosition(50.0f, 50.0f);
        vblur.setRadius(radius);
        renderer.exec();

        int leftEdge = findLeftEdgeX(outputBuffer, 100);  // Y=100は画像内(50+50)
        fprintf(stderr, "radius=%d: leftEdgeX at Y=100 is %d\n", radius, leftEdge);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    // 詳細デバッグ: sourceX=10.55でのorigin値を確認
    fprintf(stderr, "=== Debug: sourceX=10.55, comparing radius=0 vs radius=1 ===\n");
    fflush(stderr);

    float sourceX = 10.55f;
    source.setPosition(sourceX, 10.0f);

    // radius=0の場合のorigin確認
    {
        std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
        vblur.setRadius(0);

        // 手動でprepare/pullProcessを呼んで値を確認
        RenderRequest req;
        req.width = SCREEN_WIDTH;
        req.height = 1;
        req.origin.x = to_fixed(0);
        req.origin.y = to_fixed(60);

        // SourceNodeの結果を直接取得
        RenderResponse srcResult = source.pullProcess(req);
        int_fixed srcOriginX = srcResult.origin.x;
        fprintf(stderr, "radius=0: SourceNode origin.x = %d (%.4f)\n",
                srcOriginX, fixed_to_float(srcOriginX));

        // 実際のレンダリング
        renderer.exec();
        int leftEdge = findLeftEdgeX(outputBuffer, 60);
        fprintf(stderr, "radius=0: leftEdgeX = %d\n", leftEdge);
    }
    fprintf(stderr, "\n");

    // radius=1の場合のorigin確認
    {
        std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
        vblur.setRadius(1);

        // 手動でprepare/pullProcessを呼んで値を確認
        RenderRequest req;
        req.width = SCREEN_WIDTH;
        req.height = 1;
        req.origin.x = to_fixed(0);
        req.origin.y = to_fixed(60);

        // VerticalBlurNodeの結果を取得
        RenderResponse vblurResult = vblur.pullProcess(req);
        int_fixed vblurOriginX = vblurResult.origin.x;
        fprintf(stderr, "radius=1: VerticalBlurNode origin.x = %d (%.4f)\n",
                vblurOriginX, fixed_to_float(vblurOriginX));

        // 実際のレンダリング
        renderer.exec();
        int leftEdge = findLeftEdgeX(outputBuffer, 60);
        fprintf(stderr, "radius=1: leftEdgeX = %d\n", leftEdge);
    }
    fprintf(stderr, "\n");
    fflush(stderr);

    // sourceX を変化させて比較（0.1刻み）
    fprintf(stderr, "=== sourceX変化テスト（0.1刻み） ===\n\n");
    fprintf(stderr, "sourceX    radius  leftEdgeX(Y=60)  status\n");
    fprintf(stderr, "-------    ------  ---------------  ------\n");
    fflush(stderr);

    int lastLeftEdge_r0 = -1;
    int mismatchCount = 0;

    for (float sourceX = 9.0f; sourceX <= 12.05f; sourceX += 0.1f) {
        for (int radius = 0; radius <= 1; ++radius) {
            std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
            source.setPosition(sourceX, 10.0f);  // Y=10に配置
            vblur.setRadius(radius);
            renderer.exec();

            // Y=60で確認（画像内: sourceY=10, 画像高さ100なので Y=10〜109が画像範囲）
            int leftEdgeX = findLeftEdgeX(outputBuffer, 60);

            fprintf(stderr, "%7.2f    %6d  %15d", sourceX, radius, leftEdgeX);

            if (radius == 1 && lastLeftEdge_r0 >= 0) {
                if (leftEdgeX != lastLeftEdge_r0) {
                    fprintf(stderr, "  MISMATCH! (r0=%d, diff=%d)", lastLeftEdge_r0, leftEdgeX - lastLeftEdge_r0);
                    mismatchCount++;
                }
            }
            if (radius == 0) {
                lastLeftEdge_r0 = leftEdgeX;
            }

            fprintf(stderr, "\n");
        }
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    fprintf(stderr, "\n=== 結果サマリ ===\n");
    fprintf(stderr, "MISMATCHの数: %d\n", mismatchCount);
    fflush(stderr);

    // さらに細かいテスト（問題が発生しやすい境界付近）
    fprintf(stderr, "\n=== 詳細テスト（10.0〜11.0、0.05刻み） ===\n\n");
    fprintf(stderr, "sourceX    radius  leftEdgeX  status\n");
    fprintf(stderr, "-------    ------  ---------  ------\n");
    fflush(stderr);

    lastLeftEdge_r0 = -1;

    for (float sourceX = 10.0f; sourceX <= 11.05f; sourceX += 0.05f) {
        for (int radius = 0; radius <= 1; ++radius) {
            std::memset(outputBuffer.data(), 0, outputBuffer.totalBytes());
            source.setPosition(sourceX, 10.0f);
            vblur.setRadius(radius);
            renderer.exec();

            int leftEdgeX = findLeftEdgeX(outputBuffer, 60);

            fprintf(stderr, "%7.2f    %6d  %9d", sourceX, radius, leftEdgeX);

            if (radius == 1 && lastLeftEdge_r0 >= 0) {
                if (leftEdgeX != lastLeftEdge_r0) {
                    fprintf(stderr, "  MISMATCH! (r0=%d)", lastLeftEdge_r0);
                }
            }
            if (radius == 0) {
                lastLeftEdge_r0 = leftEdgeX;
            }

            fprintf(stderr, "\n");
        }
        fflush(stderr);
    }

    fprintf(stderr, "Test completed.\n");
    return 0;
}
