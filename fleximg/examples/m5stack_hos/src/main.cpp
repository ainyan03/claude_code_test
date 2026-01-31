// fleximg M5Stack HOS Example
// パトレイバー HOS起動画面風アニメーション
// M5Canvasで生成したスプライトを回転しながら出現させる
// Index8フォーマット + パレットによる効率的なフェード処理

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
#include "fleximg/nodes/composite_node.h"
#include "fleximg/nodes/renderer_node.h"

#include "fleximg/core/memory/pool_allocator.h"
#include "fleximg/image/pixel_format.h"

#include "lcd_sink_node.h"

#include <cmath>
#include <algorithm>

using namespace fleximg;

// ========================================
// 定数定義
// ========================================

static constexpr int SPRITE_COUNT = 4;
static constexpr int ANIM_FRAMES = 80;      // アニメーションフレーム数
static constexpr int FADE_FRAMES = 128;     // フェードフレーム数

// スプライトサイズ
static constexpr int SP1_W = 193, SP1_H = 199;
static constexpr int SP2_W = 71,  SP2_H = 75;
static constexpr int SP3_W = 59,  SP3_H = 59;
static constexpr int SP4_W = 103, SP4_H = 105;

// パレット定義（RGBA8形式: 16色 × 4バイト = 64バイト）
// 0: 透明, 1-4: 背景色（赤系グラデーション）, 13: 黒, 14: グレー, 15: 明るいグレー
static uint8_t paletteData[16 * 4];

// ========================================
// グローバル変数
// ========================================

// M5Canvas（スプライト生成用）
static M5Canvas canvas1(nullptr);
static M5Canvas canvas2(nullptr);
static M5Canvas canvas3(nullptr);
static M5Canvas canvas4(nullptr);

// fleximg用画像バッファ（Index8フォーマット）
static ImageBuffer spriteImages[SPRITE_COUNT];

// アニメーション状態
static int animFrame = 0;
static bool animComplete = false;

// ノード
static SourceNode sources[SPRITE_COUNT];
static AffineNode affines[SPRITE_COUNT];
static CompositeNode composite(SPRITE_COUNT);
static RendererNode renderer;
static LcdSinkNode lcdSink;

// PoolAllocator用のメモリプール
static constexpr size_t POOL_BLOCK_SIZE = 1024;
static constexpr size_t POOL_BLOCK_COUNT = 48;
static uint8_t poolMemory[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];

static fleximg::core::memory::PoolAllocator internalPool;
static fleximg::core::memory::PoolAllocatorAdapter* poolAdapter = nullptr;

// 画面サイズ
static int16_t drawW = 320;
static int16_t drawH = 240;
static int16_t drawX = 0;
static int16_t drawY = 0;

// ========================================
// パレット操作（RGBA8形式）
// ========================================

// パレットエントリ設定（RGBA順）
static void setPaletteEntry(int idx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    paletteData[idx * 4 + 0] = r;
    paletteData[idx * 4 + 1] = g;
    paletteData[idx * 4 + 2] = b;
    paletteData[idx * 4 + 3] = a;
}

// パレット初期化
static void initPalette() {
    // 全エントリを透明で初期化
    std::memset(paletteData, 0, sizeof(paletteData));

    // 赤系グラデーション（背景色）
    setPaletteEntry(1, 0xCF, 0x00, 0x00, 0x80);  // 暗い赤
    setPaletteEntry(2, 0xEF, 0x3F, 0x5F, 0x80);  // 中間赤
    setPaletteEntry(3, 0xFF, 0x5F, 0x8F, 0x80);  // 明るい赤
    setPaletteEntry(4, 0xDF, 0x1F, 0x2F, 0x80);  // 赤

    // 線・枠用
    setPaletteEntry(13, 0x00, 0x00, 0x00, 0xFF);  // 黒
    setPaletteEntry(14, 0x50, 0x50, 0x50, 0xFF);  // グレー（線）
    setPaletteEntry(15, 0xBF, 0xBF, 0xBF, 0xFF);  // 明るいグレー（角飾り）
}

// パレット更新（フェード用）
static void updatePalette(int fadeFrame) {
    int fade = fadeFrame * 4;

    // 背景色の透過値（0x80 = 半透明）
    constexpr uint8_t bgAlpha = 0x80;


    // 赤系グラデーションをフェード（半透明を維持）
    auto fade1 = static_cast<uint8_t>(std::max(0, std::min(207, 237 - fade)));
    setPaletteEntry(1, fade1, 0, 0, (fade1 * bgAlpha) >> 8);

    auto fade2 = static_cast<uint8_t>(std::max(0, std::min(239, 307 - fade)));
    setPaletteEntry(2,
        fade2,
        static_cast<uint8_t>(std::max(0, 63 - fade)),
        static_cast<uint8_t>(std::max(0, 95 - fade)),
        (fade2 * bgAlpha) >> 8);

    auto fade3 = static_cast<uint8_t>(std::max(0, std::min(255, 352 - fade)));
    setPaletteEntry(3,
        fade3,
        static_cast<uint8_t>(std::max(0, 95 - fade)),
        static_cast<uint8_t>(std::max(0, 143 - fade)),
        (fade3 * bgAlpha) >> 8);

    auto fade4 = static_cast<uint8_t>(std::max(0, std::min(223, 272 - fade)));
    setPaletteEntry(4,
        fade4,
        static_cast<uint8_t>(std::max(0, 31 - fade)),
        static_cast<uint8_t>(std::max(0, 47 - fade)),
        (fade4 * bgAlpha) >> 8);

    // 線の色も赤に変化（不透明を維持）
    setPaletteEntry(13,
        static_cast<uint8_t>(std::min(255, fade >> 1)),
        0, 0, 0xFF);
}

// 全スプライトにパレットを適用
static void applyPaletteToSprites() {
    PaletteData palette(paletteData, PixelFormatIDs::RGBA8_Straight, 16);
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        sources[i].setSource(spriteImages[i].view(), palette);
    }
}

// ========================================
// スプライト生成（M5Canvas使用）
// ========================================

static void createSprite1(M5Canvas& sp) {
    sp.setColorDepth(8);
    sp.createSprite(SP1_W, SP1_H);
    sp.clear(uint8_t(1));  // 背景色1

    // 外枠
    sp.setColor(uint8_t(14));
    sp.drawRect(0, 0, SP1_W, SP1_H);

    // 四隅の角飾り
    sp.setColor(uint8_t(15));
    sp.fillRect(0, 0, 16, 18);
    sp.fillRect(SP1_W - 16, 0, 16, 18);
    sp.fillRect(SP1_W - 16, SP1_H - 18, 16, 18);
    sp.fillRect(0, SP1_H - 18, 16, 18);

    // 内部を背景色で塗る
    sp.setColor(uint8_t(1));
    sp.fillRect(2, 2, SP1_W - 4, SP1_H - 4);

    // 十字線
    sp.setColor(uint8_t(14));
    sp.drawFastHLine(0, SP1_H / 2, SP1_W);
    sp.drawFastVLine(SP1_W / 2, 0, SP1_H);
}

static void createSprite2(M5Canvas& sp) {
    sp.setColorDepth(8);
    sp.createSprite(SP2_W, SP2_H);
    sp.clear(uint8_t(2));  // 背景色2

    // 外枠
    sp.setColor(uint8_t(14));
    sp.drawRect(0, 0, SP2_W, SP2_H);

    // 中央縦線
    sp.drawFastVLine(SP2_W / 2, 0, SP2_H);

    // 左右の縦帯
    sp.setColor(uint8_t(13));
    sp.fillRect(0, 0, 3, SP2_H);
    sp.fillRect(SP2_W - 3, 0, 3, SP2_H);

    // 中央横帯
    sp.fillRect(0, SP2_H / 2 - 1, SP2_W, 3);
}

static void createSprite3(M5Canvas& sp) {
    sp.setColorDepth(8);
    sp.createSprite(SP3_W, SP3_H);
    sp.clear(uint8_t(3));  // 背景色3

    // 外枠
    sp.setColor(uint8_t(14));
    sp.drawRect(0, 0, SP3_W, SP3_H);

    // 十字線
    sp.drawFastHLine(0, SP3_H / 2, SP3_W);
    sp.drawFastVLine(SP3_W / 2, 0, SP3_H);

    // 円
    int cx = SP3_W / 2;
    int cy = SP3_H / 2;
    sp.setColor(uint8_t(13));
    sp.fillCircle(cx, cy, 29);
    sp.setColor(uint8_t(3));
    sp.fillCircle(cx, cy, 26);

    // 斜め線
    sp.setColor(uint8_t(14));
    sp.drawLine(12, 10, cx, cy - 2);
}

static void createSprite4(M5Canvas& sp) {
    sp.setColorDepth(8);
    sp.createSprite(SP4_W, SP4_H);
    sp.clear(uint8_t(4));  // 背景色4

    // 外枠
    sp.setColor(uint8_t(14));
    sp.drawRect(0, 0, SP4_W, SP4_H);

    // 四隅の角飾り
    sp.setColor(uint8_t(15));
    sp.fillRect(0, 0, 16, 18);
    sp.fillRect(SP4_W - 16, 0, 16, 18);
    sp.fillRect(SP4_W - 16, SP4_H - 18, 16, 18);
    sp.fillRect(0, SP4_H - 18, 16, 18);

    // 内部
    sp.setColor(uint8_t(4));
    sp.fillRect(2, 2, SP4_W - 4, SP4_H - 4);

    // 分割線
    sp.setColor(uint8_t(14));
    sp.drawFastHLine(0, SP4_H / 2, SP4_W);
    sp.drawFastVLine(25, 0, SP4_H / 2);
    sp.drawFastVLine(77, SP4_H / 2, SP4_H / 2);

    // 左上の半円
    sp.setClipRect(0, 0, SP4_W / 2, SP4_H);
    sp.setColor(uint8_t(13));
    sp.fillCircle(SP4_W / 2, 26, 27);
    sp.setColor(uint8_t(4));
    sp.fillCircle(SP4_W / 2, 26, 24);

    // 右下の半円
    sp.setClipRect(SP4_W / 2, 0, SP4_W / 2, SP4_H);
    sp.setColor(uint8_t(13));
    sp.fillCircle(SP4_W / 2 - 1, SP4_H - 27, 27);
    sp.setColor(uint8_t(4));
    sp.fillCircle(SP4_W / 2 - 1, SP4_H - 27, 24);

    sp.clearClipRect();

    // 中央縦線
    sp.setColor(uint8_t(14));
    sp.drawFastVLine(SP4_W / 2, 0, SP4_H);

    // 斜め線
    sp.drawLine(34, 10, SP4_W / 2, 27);
}

// ========================================
// M5Canvas → fleximg ImageBuffer 変換（Index8）
// ========================================

static ImageBuffer canvasToIndex8Buffer(M5Canvas& canvas) {
    int w = canvas.width();
    int h = canvas.height();

    // Index8フォーマットで作成
    ImageBuffer img(w, h, PixelFormatIDs::Index8);

    // インデックス値を直接コピー
    for (int y = 0; y < h; ++y) {
        uint8_t* dst = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < w; ++x) {
            dst[x] = static_cast<uint8_t>(canvas.readPixelValue(x, y) & 0x0F);
        }
    }

    return img;
}

// ========================================
// パイプライン構築
// ========================================

static void buildPipeline() {
    // パレット初期化
    initPalette();

    // スプライト生成
    createSprite1(canvas1);
    createSprite2(canvas2);
    createSprite3(canvas3);
    createSprite4(canvas4);

    // Index8 ImageBufferに変換
    spriteImages[0] = canvasToIndex8Buffer(canvas1);
    spriteImages[1] = canvasToIndex8Buffer(canvas2);
    spriteImages[2] = canvasToIndex8Buffer(canvas3);
    spriteImages[3] = canvasToIndex8Buffer(canvas4);

    // CompositeNodeの入力数を設定
    composite.setInputCount(SPRITE_COUNT);

    // 各ソースを接続
    int pivotX[] = { SP1_W / 2, SP2_W / 2, SP3_W / 2, SP4_W / 2 };
    int pivotY[] = { SP1_H / 2, SP2_H / 2, SP3_H / 2, SP4_H / 2 };

    // パレットデータ作成（全スプライト共通）
    PaletteData palette(paletteData, PixelFormatIDs::RGBA8_Straight, 16);

    for (int i = 0; i < SPRITE_COUNT; ++i) {
        sources[i].setPivot(
            float_to_fixed(static_cast<float>(pivotX[i])),
            float_to_fixed(static_cast<float>(pivotY[i]))
        );

        // パレット情報付きでソース設定
        sources[i].setSource(spriteImages[i].view(), palette);

        sources[i] >> affines[i];
        // 小さいスプライトを前面に配置（ポート番号を逆順に）
        // SP4(最小)→ポート0(最前面), SP1(最大)→ポート3(最背面)
        affines[i].connectTo(composite, SPRITE_COUNT - 1 - i);

        // 初期状態（非表示）
        affines[i].setRotationScale(0.0f, 0.0f, 0.0f);
        affines[i].setTranslation(0.0f, 0.0f);
    }

    composite >> renderer >> lcdSink;
}

// ========================================
// アニメーション更新
// ========================================

static void updateAnimation() {
    if (animComplete) return;

    int i = animFrame;

    // 各スプライトのパラメータ計算
    // sp1: フレーム20-50で出現
    float z1 = 1.0f, r1 = 0.0f;
    if (i <= 20) {
        z1 = 0.0f;
    } else if (i <= 50) {
        r1 = static_cast<float>(i - 50) * 6.0f;  // -180° → 0°
        z1 = static_cast<float>(i - 20) / 30.0f;
    }

    // sp2: フレーム30-60で出現
    float z2 = 1.0f, r2 = 0.0f;
    if (i <= 30) {
        z2 = 0.0f;
    } else if (i <= 60) {
        r2 = static_cast<float>(i - 60) * 6.0f;
        z2 = static_cast<float>(i - 30) / 30.0f;
    }

    // sp3: フレーム42-72で出現
    float z3 = 1.0f, r3 = 0.0f;
    if (i <= 42) {
        z3 = 0.0f;
    } else if (i <= 72) {
        r3 = static_cast<float>(i - 72) * 6.0f;
        z3 = static_cast<float>(i - 42) / 30.0f;
    }

    // sp4: フレーム50-75で出現
    float z4 = 1.0f, r4 = 0.0f;
    if (i <= 50) {
        z4 = 0.0f;
    } else if (i <= 75) {
        r4 = static_cast<float>(i - 75) * 7.0f;
        z4 = static_cast<float>(i - 50) / 25.0f;
    }

    // ラジアンに変換
    float deg2rad = static_cast<float>(M_PI) / 180.0f;

    affines[0].setRotationScale(r1 * deg2rad, z1, z1);
    affines[1].setRotationScale(r2 * deg2rad, z2, z2);
    affines[2].setRotationScale(r3 * deg2rad, z3, z3);
    affines[3].setRotationScale(r4 * deg2rad, z4, z4);

    animFrame++;

    if (animFrame > ANIM_FRAMES) {
        animComplete = true;
    }
}

// フェード処理
static int fadeFrame = 0;

static void updateFade() {
    if (!animComplete) return;
    if (fadeFrame >= FADE_FRAMES) return;

    // パレット更新のみ（画像再生成不要！）
    updatePalette(fadeFrame);
    applyPaletteToSprites();

    fadeFrame++;
}

// ========================================
// セットアップ
// ========================================

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    // 画面サイズ取得
    int16_t screenW = static_cast<int16_t>(M5.Display.width());
    int16_t screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域
    drawW = screenW;
    drawH = screenH-16;
    drawX = 0;
    drawY = 8;

    // PoolAllocator初期化
    internalPool.initialize(poolMemory, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
    static fleximg::core::memory::PoolAllocatorAdapter adapter(internalPool);
    poolAdapter = &adapter;

    // レンダラー設定
    renderer.setVirtualScreen(drawW, drawH);
    renderer.setPivotCenter();
    renderer.setAllocator(poolAdapter);

    // LCD出力設定
    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(
        float_to_fixed(drawW / 2.0f),
        float_to_fixed(drawH / 2.0f)
    );

    // パイプライン構築
    buildPipeline();

    // 中央に十字線（背景）
    M5.Display.drawFastHLine(0, screenH / 2, screenW, M5.Display.color888(80, 80, 80));

    M5.Display.startWrite();
}

// ========================================
// メインループ
// ========================================

void loop() {
#if defined(M5UNIFIED_PC_BUILD)
    lgfx::delay(16);
#endif
    M5.update();

    // ボタン処理（リセット）
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
        animFrame = 0;
        fadeFrame = 0;
        animComplete = false;
        initPalette();
        applyPaletteToSprites();
    }

    // アニメーション更新
    updateAnimation();
    updateFade();

    // レンダリング実行
    renderer.exec();

    // フレーム制御（約30fps）
    static unsigned long lastFrame = 0;
    unsigned long now = lgfx::millis();
    int dl = 33 - (now - lastFrame);
    if (dl > 0) {
        lgfx::delay(dl);
    }
    lastFrame = lgfx::millis();

    // FPS表示
    static int frameCount = 0;
    static float fps = 0.0f;
    static unsigned long fpsLastTime = 0;

    frameCount++;
    now = lgfx::millis();
    if (now - fpsLastTime >= 1000) {
        fps = static_cast<float>(frameCount) * 1000.0f / static_cast<float>(now - fpsLastTime);
        frameCount = 0;
        fpsLastTime = now;

        M5.Display.setCursor(0, 232);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.printf("FPS:%.1f F:%d/%d  ",
                          static_cast<double>(fps),
                          animFrame,
                          fadeFrame);
    }
}
