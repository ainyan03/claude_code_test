// fleximg M5Stack Matte Composite Example
// マット合成（アルファマスク合成）デモ

#include <M5Unified.h>

// fleximg (stb-style: define FLEXIMG_IMPLEMENTATION before including headers)
#define FLEXIMG_NAMESPACE fleximg
#define FLEXIMG_IMPLEMENTATION
#include "fleximg/core/common.h"
#include "fleximg/core/types.h"
#include "fleximg/core/memory/platform.h"
#include "fleximg/image/viewport.h"
#include "fleximg/image/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/matte_node.h"
#include "fleximg/nodes/renderer_node.h"

// stb 方式: FLEXIMG_IMPLEMENTATION 定義済みなのでヘッダから実装が有効化される
#include "fleximg/core/memory/pool_allocator.h"
#include "fleximg/image/pixel_format.h"
#include "fleximg/operations/filters.h"

// DefaultAllocatorの統計を追跡するラッパー
class TrackedDefaultAllocator : public fleximg::core::memory::IAllocator {
public:
    struct Stats {
        size_t allocCount = 0;
        size_t deallocCount = 0;
        size_t lastAllocSize = 0;

        void reset() {
            allocCount = deallocCount = 0;
            lastAllocSize = 0;
        }
    };

    void* allocate(size_t bytes, size_t alignment = 16) override {
        stats_.allocCount++;
        stats_.lastAllocSize = bytes;
        return fleximg::core::memory::DefaultAllocator::instance().allocate(bytes, alignment);
    }

    void deallocate(void* ptr) override {
        if (ptr) stats_.deallocCount++;
        fleximg::core::memory::DefaultAllocator::instance().deallocate(ptr);
    }

    const char* name() const override { return "TrackedDefaultAllocator"; }

    const Stats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }

    static TrackedDefaultAllocator& instance() {
        static TrackedDefaultAllocator s_instance;
        return s_instance;
    }

private:
    Stats stats_;
};

// カスタムSinkNode
#include "lcd_sink_node.h"

#include <cmath>

using namespace fleximg;

// チェッカーボード画像を作成（前景用）
static ImageBuffer createForegroundImage(int width, int height) {
    // 元画像はTrackedDefaultAllocatorで統計追跡
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight, InitPolicy::Uninitialized,
                    &TrackedDefaultAllocator::instance());

    const int cellSize = 16;  // チェッカーボードのセルサイズ

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            // チェッカーボードパターン（赤/黄）
            bool isEven = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            if (isEven) {
                // 赤
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 50;
                row[x * 4 + 2] = 50;
            } else {
                // 黄
                row[x * 4 + 0] = 255;
                row[x * 4 + 1] = 220;
                row[x * 4 + 2] = 50;
            }
            row[x * 4 + 3] = 255;  // A
        }
    }

    return img;
}

// 縦ストライプ + 横グラデーション画像を作成（背景用）
static ImageBuffer createBackgroundImage(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight, InitPolicy::Uninitialized,
                    &TrackedDefaultAllocator::instance());

    const int stripeWidth = 12;  // ストライプの幅

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        // 横グラデーション係数（左:暗い → 右:明るい）
        float gradientFactor = static_cast<float>(y) / static_cast<float>(height);

        for (int x = 0; x < width; ++x) {
            // 縦ストライプパターン
            bool isStripe = (x / stripeWidth) % 2 == 0;

            if (isStripe) {
                // 青系（グラデーション適用）
                row[x * 4 + 0] = static_cast<uint8_t>(30 + 50 * gradientFactor);
                row[x * 4 + 1] = static_cast<uint8_t>(80 + 100 * gradientFactor);
                row[x * 4 + 2] = static_cast<uint8_t>(180 + 75 * gradientFactor);
            } else {
                // シアン系（グラデーション適用）
                row[x * 4 + 0] = static_cast<uint8_t>(30 + 70 * gradientFactor);
                row[x * 4 + 1] = static_cast<uint8_t>(150 + 105 * gradientFactor);
                row[x * 4 + 2] = static_cast<uint8_t>(180 + 75 * gradientFactor);
            }
            row[x * 4 + 3] = 255;  // A
        }
    }

    return img;
}

// 点が三角形の内部にあるかチェック（符号付き面積法）
static bool pointInTriangle(float px, float py,
                            float x1, float y1, float x2, float y2, float x3, float y3) {
    auto sign = [](float p1x, float p1y, float p2x, float p2y, float p3x, float p3y) {
        return (p1x - p3x) * (p2y - p3y) - (p2x - p3x) * (p1y - p3y);
    };

    float d1 = sign(px, py, x1, y1, x2, y2);
    float d2 = sign(px, py, x2, y2, x3, y3);
    float d3 = sign(px, py, x3, y3, x1, y1);

    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

// 六芒星マスクを作成（くっきり）
static ImageBuffer createHexagramMask(int width, int height) {
    ImageBuffer img(width, height, PixelFormatIDs::Alpha8, InitPolicy::Uninitialized,
                    &TrackedDefaultAllocator::instance());

    float centerX = static_cast<float>(width) / 2.0f;
    float centerY = static_cast<float>(height) / 2.0f;
    float radius = std::min(centerX, centerY) * 0.85f;

    // 上向き三角形の頂点
    float t1_x1 = centerX;
    float t1_y1 = centerY - radius;
    float t1_x2 = centerX - radius * 0.866f;  // cos(30°) ≈ 0.866
    float t1_y2 = centerY + radius * 0.5f;
    float t1_x3 = centerX + radius * 0.866f;
    float t1_y3 = centerY + radius * 0.5f;

    // 下向き三角形の頂点
    float t2_x1 = centerX;
    float t2_y1 = centerY + radius;
    float t2_x2 = centerX - radius * 0.866f;
    float t2_y2 = centerY - radius * 0.5f;
    float t2_x3 = centerX + radius * 0.866f;
    float t2_y3 = centerY - radius * 0.5f;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            float px = static_cast<float>(x);
            float py = static_cast<float>(y);

            // どちらかの三角形の内部にあれば白
            bool inStar = pointInTriangle(px, py, t1_x1, t1_y1, t1_x2, t1_y2, t1_x3, t1_y3) ||
                          pointInTriangle(px, py, t2_x1, t2_y1, t2_x2, t2_y2, t2_x3, t2_y3);

            row[x] = inStar ? 255 : 0;
        }
    }

    return img;
}

// PoolAllocator用のメモリプール（内部RAM使用）
// fleximg内部で動的に確保されるバッファ用（MatteNode等の一時バッファ）
// 320幅のスキャンライン: 320 * 4 = 1280バイト
// 余裕を持って512B x 32ブロック = 16KB
static constexpr size_t POOL_BLOCK_SIZE = 512;  // 512 bytes per block
static constexpr size_t POOL_BLOCK_COUNT = 32;        // 32 blocks = 16KB
static uint8_t poolMemory[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];

// PoolAllocatorとアダプタのインスタンス
static fleximg::core::memory::PoolAllocator internalPool;
static fleximg::core::memory::PoolAllocatorAdapter* poolAdapter = nullptr;

// グローバル変数
static ImageBuffer foregroundImage;
static ImageBuffer backgroundImage;
static ImageBuffer maskImage;
static float rotationAngle = 0.0f;

static SourceNode foregroundSource;
static SourceNode backgroundSource;
static SourceNode maskSource;
static AffineNode affine;
static AffineNode bgAffine;
static AffineNode maskAffine;
static MatteNode matte;
static RendererNode renderer;
static LcdSinkNode lcdSink;

// アニメーション用
static float animationTime = 0.0f;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    M5.Display.setCursor(0, 0);
    M5.Display.println("fleximg Matte Demo");

    // 元画像を作成（DefaultAllocatorを使用）
    foregroundImage = createForegroundImage(30, 30);
    backgroundImage = createBackgroundImage(40, 40);
    maskImage = createHexagramMask(50, 50);

    // PoolAllocatorを初期化（fleximg内部バッファ用）
    internalPool.initialize(poolMemory, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
    static fleximg::core::memory::PoolAllocatorAdapter adapter(internalPool);
    poolAdapter = &adapter;

    M5.Display.printf("Pool: %zu x %zu bytes\n", POOL_BLOCK_COUNT, POOL_BLOCK_SIZE);
    M5.Display.println("Internal buffers use pool");

    // 画面サイズ取得
    int16_t screenW = static_cast<int16_t>(M5.Display.width());
    int16_t screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域（画面中央に配置）
    int16_t drawW = 320;
    int16_t drawH = 200;
    int16_t drawX = (screenW - drawW) / 2;
    int16_t drawY = (screenH - drawH) / 2;

    // fleximg パイプライン構築

    // 前景ソース（回転する画像）
    foregroundSource.setSource(foregroundImage.view());
    foregroundSource.setPivot(
        float_to_fixed(foregroundImage.width() / 2.0f),
        float_to_fixed(foregroundImage.height() / 2.0f)
    );

    // 背景ソース（静止画像）
    backgroundSource.setSource(backgroundImage.view());
    backgroundSource.setPivot(
        float_to_fixed(backgroundImage.width() / 2.0f),
        float_to_fixed(backgroundImage.height() / 2.0f)
    );

    // マスクソース
    maskSource.setSource(maskImage.view());
    maskSource.setPivot(
        float_to_fixed(maskImage.width() / 2.0f),
        float_to_fixed(maskImage.height() / 2.0f)
    );

    // レンダラー設定
    renderer.setVirtualScreen(drawW, drawH);
    renderer.setPivotCenter();
    renderer.setAllocator(poolAdapter);  // 内部バッファ用アロケータを設定

    // LCD出力設定
    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(
        float_to_fixed(drawW / 2.0f),
        float_to_fixed(drawH / 2.0f)
    );

    // パイプライン接続
    // 前景 → Affine → MatteNode入力0
    foregroundSource >> affine;
    affine.connectTo(matte, 0);

    // 背景 → BgAffine → MatteNode入力1
    backgroundSource >> bgAffine;
    bgAffine.connectTo(matte, 1);

    // マスク → MaskAffine → MatteNode入力2
    maskSource >> maskAffine;
    maskAffine.connectTo(matte, 2);

    // Matte → Renderer → LCD
    matte >> renderer >> lcdSink;

    // 初期化完了後、DefaultAllocatorのトラップを有効化
    // これ以降DefaultAllocatorが使われるとassertで停止する
#ifdef FLEXIMG_TRAP_DEFAULT_ALLOCATOR
    fleximg::core::memory::DefaultAllocator::trapEnabled() = true;
    M5.Display.println("DefaultAllocator trap ENABLED");
#endif

    M5.Display.startWrite();
}

void loop() {
#if defined ( M5UNIFIED_PC_BUILD )
    // フレームレート制限（約60fps）
    lgfx::delay(16);
#endif
    M5.update();

    // ボタン処理: ダブルクリックで描画トグル
    if (M5.BtnC.wasDoubleClicked()) {
        lcdSink.setDrawEnabled(!lcdSink.getDrawEnabled());
    }

    // アニメーション時間を更新
    animationTime += 0.02f;
    if (animationTime > 2.0f * static_cast<float>(M_PI)) {
        animationTime -= 2.0f * static_cast<float>(M_PI);
    }

    // 前景の回転（速い）
    rotationAngle += 0.05f;
    if (rotationAngle > 2.0f * static_cast<float>(M_PI)) {
        rotationAngle -= 2.0f * static_cast<float>(M_PI);
    }
    affine.setRotationScale(rotationAngle, 6.0f, 6.0f);

    // 背景のアニメーション（3倍拡大 + 緩やかな回転）
    float bgRotation = animationTime * 1.5f;  // 回転
    float bgScale = 6.0f;
    bgAffine.setRotationScale(bgRotation, bgScale, bgScale);

    // マスクのアニメーション（回転・スケール・移動）
    float maskRotation = -animationTime * 0.5f;
    float maskScale = 5.0f + 4.9f * std::sinf(animationTime * 2.0f);
    float moveRadius = 30.0f;
    float offsetX = moveRadius * std::cosf(animationTime);
    float offsetY = moveRadius * std::sinf(animationTime);

    // 新しいセッターで簡潔に設定
    maskAffine.setRotationScale(maskRotation, maskScale, maskScale);
    maskAffine.setTranslation(offsetX, offsetY);

    // レンダリング実行
    renderer.exec();

    // FPS・アロケータ統計表示
    static unsigned long lastTime = 0;
    static int frameCount = 0;
    static float fps = 0.0f;

    frameCount++;
    unsigned long now = lgfx::millis();
    if (now - lastTime >= 1000) {
        fps = static_cast<float>(frameCount) * 1000.0f / static_cast<float>(now - lastTime);
        frameCount = 0;
        lastTime = now;

        // FPS・アロケータ統計表示更新
        int16_t dispH = static_cast<int16_t>(M5.Display.height());
        M5.Display.fillRect(0, dispH - 60, 250, 60, TFT_BLACK);
        M5.Display.setCursor(0, dispH - 60);
        M5.Display.printf("FPS:%.1f", static_cast<double>(fps));

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // 統計取得（デバッグビルド時のみ）
        const auto& poolStats = poolAdapter->stats();
        const auto& defaultStats = TrackedDefaultAllocator::instance().stats();
        M5.Display.setCursor(0, dispH - 45);
        M5.Display.printf("Pool  A:%zu F:%zu",
                          poolStats.poolHits, poolStats.poolDeallocs);
        M5.Display.setCursor(0, dispH - 30);
        M5.Display.printf("Deflt A:%zu F:%zu",
                          defaultStats.allocCount, defaultStats.deallocCount);
        M5.Display.setCursor(0, dispH - 15);
        M5.Display.printf("Miss:%zu Size:%zu",
                          poolStats.poolMisses, poolStats.lastAllocSize);
#endif
    }
}
