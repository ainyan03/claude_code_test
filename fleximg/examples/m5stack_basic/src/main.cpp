// fleximg M5Stack Basic Example
// 複数Source合成・アフィン変換デモ

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

// stb 方式: FLEXIMG_IMPLEMENTATION 定義済みなのでヘッダから実装が有効化される
#include "fleximg/core/memory/pool_allocator.h"
#include "fleximg/image/pixel_format.h"
#include "fleximg/operations/filters.h"

// カスタムSinkNode
#include "lcd_sink_node.h"

#include <cmath>

using namespace fleximg;

// 色付きグラデーション画像を作成
// baseR, baseG, baseB: ベース色（0-255）
static ImageBuffer createColorGradientImage(int width, int height,
                                            uint8_t baseR, uint8_t baseG, uint8_t baseB) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        float fy = static_cast<float>(y) / static_cast<float>(height);
        for (int x = 0; x < width; ++x) {
            float fx = static_cast<float>(x) / static_cast<float>(width);
            // ベース色に対角グラデーションを適用
            float grad = (fx + fy) * 0.5f;
            row[x * 4 + 0] = static_cast<uint8_t>(baseR * (0.5f + 0.5f * grad));
            row[x * 4 + 1] = static_cast<uint8_t>(baseG * (0.5f + 0.5f * grad));
            row[x * 4 + 2] = static_cast<uint8_t>(baseB * (0.5f + 0.5f * grad));
            row[x * 4 + 3] = 255;
        }
    }

    return img;
}

// チェッカーボード付き画像を作成（視認性向上）
static ImageBuffer createCheckerImage(int width, int height,
                                      uint8_t r1, uint8_t g1, uint8_t b1,
                                      uint8_t r2, uint8_t g2, uint8_t b2) {
    ImageBuffer img(width, height, PixelFormatIDs::RGBA8_Straight);
    const int cellSize = 8;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));
        for (int x = 0; x < width; ++x) {
            bool isEven = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            if (isEven) {
                row[x * 4 + 0] = r1;
                row[x * 4 + 1] = g1;
                row[x * 4 + 2] = b1;
            } else {
                row[x * 4 + 0] = r2;
                row[x * 4 + 1] = g2;
                row[x * 4 + 2] = b2;
            }
            row[x * 4 + 3] = 255;
        }
    }

    return img;
}

// モード定義
enum class DemoMode {
    SingleRotate = 0,   // 単体回転（従来互換）
    FourStatic,         // 4Source静止
    CompositeRotate,    // Composite全体回転
    IndividualAndComposite,  // 個別+全体回転
    MODE_COUNT
};

// 速度レベル
enum class SpeedLevel {
    Slow = 0,
    Normal,
    Fast,
    LEVEL_COUNT
};

static const float SPEED_MULTIPLIERS[] = { 0.3f, 1.0f, 2.5f };
static const char* SPEED_NAMES[] = { "Slow", "Normal", "Fast" };
static const char* MODE_NAMES[] = {
    "Single Rotate",
    "4-Source Static",
    "Composite Rotate",
    "Individual+Composite"
};

// グローバル変数
static DemoMode currentMode = DemoMode::SingleRotate;
static SpeedLevel speedLevel = SpeedLevel::Normal;
static bool reverseDirection = false;

// 画像バッファ
static ImageBuffer srcImage;      // モード0用（従来）
static ImageBuffer srcImages[4];  // モード1-3用（4色）

// アニメーション
static float rotationAngle = 0.0f;
static float individualAngles[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

// ノード（モード0用）
static SourceNode source;
static AffineNode affine;

// ノード（モード1-3用）
static SourceNode sources[4];
static AffineNode affines[4];
static CompositeNode composite(4);  // 4入力

// 共通ノード
static RendererNode renderer;
static LcdSinkNode lcdSink;

// 画面サイズ
static int16_t drawW = 320;
static int16_t drawH = 200;
static int16_t drawX = 0;
static int16_t drawY = 0;

// UI更新フラグ
static bool needsUIUpdate = true;

// パイプラインを現在のモードに応じて再構築
static void rebuildPipeline() {
    // 全ノードの接続をクリア
    source.disconnectAll();
    affine.disconnectAll();
    for (int i = 0; i < 4; ++i) {
        sources[i].disconnectAll();
        affines[i].disconnectAll();
    }
    composite.disconnectAll();
    renderer.disconnectAll();
    lcdSink.disconnectAll();

    if (currentMode == DemoMode::SingleRotate) {
        // モード0: 単体回転
        source >> affine >> renderer >> lcdSink;
    } else {
        // モード1-3: 4Source合成
        for (int i = 0; i < 4; ++i) {
            sources[i] >> affines[i];
            affines[i].connectTo(composite, i);
        }
        composite >> renderer >> lcdSink;
    }
}

// 配置オフセット（四隅に配置）
static const float OFFSETS[4][2] = {
    { -50.0f, -40.0f },  // 左上
    {  50.0f, -40.0f },  // 右上
    { -50.0f,  40.0f },  // 左下
    {  50.0f,  40.0f }   // 右下
};

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    // 画面サイズ取得
    int16_t screenW = static_cast<int16_t>(M5.Display.width());
    int16_t screenH = static_cast<int16_t>(M5.Display.height());

    // 描画領域（画面中央に配置、上部にUI用スペース確保）
    drawW = 320;
    drawH = 200;
    drawX = (screenW - drawW) / 2;
    drawY = 40;  // 上部40pxはUI用

    // モード0用画像（従来互換）
    srcImage = createColorGradientImage(80, 80, 255, 180, 100);

    // モード1-3用画像（4色チェッカーボード）
    srcImages[0] = createCheckerImage(40, 40, 255, 80, 80, 200, 50, 50);    // 赤系
    srcImages[1] = createCheckerImage(40, 40, 80, 255, 80, 50, 200, 50);    // 緑系
    srcImages[2] = createCheckerImage(40, 40, 80, 80, 255, 50, 50, 200);    // 青系
    srcImages[3] = createCheckerImage(40, 40, 255, 255, 80, 200, 200, 50);  // 黄系

    // モード0用ノード設定
    source.setSource(srcImage.view());
    source.setOrigin(
        float_to_fixed(srcImage.width() / 2.0f),
        float_to_fixed(srcImage.height() / 2.0f)
    );

    // モード1-3用ノード設定
    for (int i = 0; i < 4; ++i) {
        sources[i].setSource(srcImages[i].view());
        sources[i].setOrigin(
            float_to_fixed(srcImages[i].width() / 2.0f),
            float_to_fixed(srcImages[i].height() / 2.0f)
        );
        // 初期配置（拡大して四隅に配置）
        affines[i].setScale(2.0f, 2.0f);
        affines[i].setTranslation(OFFSETS[i][0], OFFSETS[i][1]);
    }

    // レンダラー設定
    renderer.setVirtualScreen(drawW, drawH);

    // LCD出力設定
    lcdSink.setTarget(&M5.Display, drawX, drawY, drawW, drawH);
    lcdSink.setOrigin(
        float_to_fixed(drawW / 2.0f),
        float_to_fixed(drawH / 2.0f)
    );

    // 初期パイプライン構築
    rebuildPipeline();

    M5.Display.startWrite();
}

// UI描画
static void drawUI() {
    // 上部UI領域クリア
    M5.Display.fillRect(0, 0, M5.Display.width(), 38, TFT_BLACK);

    M5.Display.setCursor(0, 0);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.printf("Mode: %s", MODE_NAMES[static_cast<int>(currentMode)]);

    M5.Display.setCursor(0, 12);
    M5.Display.printf("Speed: %s  Dir: %s",
                      SPEED_NAMES[static_cast<int>(speedLevel)],
                      reverseDirection ? "REV" : "FWD");

    M5.Display.setCursor(0, 24);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.print("A:Mode B:Speed C:Dir");

    needsUIUpdate = false;
}

void loop() {
#if defined ( M5UNIFIED_PC_BUILD )
    // フレームレート制限（約60fps）
    lgfx::delay(16);
#endif
    M5.update();

    // ボタン処理
    if (M5.BtnA.wasPressed()) {
        int mode = static_cast<int>(currentMode);
        mode = (mode + 1) % static_cast<int>(DemoMode::MODE_COUNT);
        currentMode = static_cast<DemoMode>(mode);
        rebuildPipeline();
        needsUIUpdate = true;
    }

    if (M5.BtnB.wasPressed()) {
        int level = static_cast<int>(speedLevel);
        level = (level + 1) % static_cast<int>(SpeedLevel::LEVEL_COUNT);
        speedLevel = static_cast<SpeedLevel>(level);
        needsUIUpdate = true;
    }

    if (M5.BtnC.wasPressed()) {
        reverseDirection = !reverseDirection;
        needsUIUpdate = true;
    }

    // UI更新
    if (needsUIUpdate) {
        drawUI();
    }

    // 速度計算
    float speedMult = SPEED_MULTIPLIERS[static_cast<int>(speedLevel)];
    float direction = reverseDirection ? -1.0f : 1.0f;
    float deltaAngle = 0.05f * speedMult * direction;

    // 回転角度更新
    rotationAngle += deltaAngle;
    if (rotationAngle > 2.0f * static_cast<float>(M_PI)) {
        rotationAngle -= 2.0f * static_cast<float>(M_PI);
    } else if (rotationAngle < 0.0f) {
        rotationAngle += 2.0f * static_cast<float>(M_PI);
    }

    // モード別処理
    switch (currentMode) {
        case DemoMode::SingleRotate:
            // モード0: 単体回転
            affine.setRotation(rotationAngle);
            break;

        case DemoMode::FourStatic:
            // モード1: 4Source静止（配置のみ、回転なし）
            composite.setMatrix(AffineMatrix{});  // 単位行列
            for (int i = 0; i < 4; ++i) {
                affines[i].setRotationScale(0.0f, 2.0f, 2.0f);
                affines[i].setTranslation(OFFSETS[i][0], OFFSETS[i][1]);
            }
            break;

        case DemoMode::CompositeRotate:
            // モード2: Composite全体回転
            // 各Sourceは静止、Composite全体が回転
            for (int i = 0; i < 4; ++i) {
                affines[i].setRotationScale(0.0f, 2.0f, 2.0f);
                affines[i].setTranslation(OFFSETS[i][0], OFFSETS[i][1]);
            }
            // CompositeNodeのAffineCapabilityで全体を回転
            composite.setRotation(rotationAngle);
            break;

        case DemoMode::IndividualAndComposite:
            // モード3: 個別+全体回転
            // 各Sourceが個別に自転
            for (int i = 0; i < 4; ++i) {
                individualAngles[i] += deltaAngle * (1.0f + 0.3f * static_cast<float>(i));
                if (individualAngles[i] > 2.0f * static_cast<float>(M_PI)) {
                    individualAngles[i] -= 2.0f * static_cast<float>(M_PI);
                } else if (individualAngles[i] < 0.0f) {
                    individualAngles[i] += 2.0f * static_cast<float>(M_PI);
                }
                affines[i].setRotationScale(individualAngles[i], 2.0f, 2.0f);
                affines[i].setTranslation(OFFSETS[i][0], OFFSETS[i][1]);
            }
            // Composite全体も公転（逆方向、遅め）
            composite.setRotation(-rotationAngle * 0.5f);
            break;

        default:
            break;
    }

    // レンダリング実行
    renderer.exec();

    // FPS表示
    static unsigned long lastTime = 0;
    static int frameCount = 0;
    static float fps = 0.0f;

    frameCount++;
    unsigned long now = lgfx::millis();
    if (now - lastTime >= 1000) {
        fps = static_cast<float>(frameCount) * 1000.0f / static_cast<float>(now - lastTime);
        frameCount = 0;
        lastTime = now;

        // FPS表示更新
        int16_t dispH = static_cast<int16_t>(M5.Display.height());
        M5.Display.fillRect(0, dispH - 16, 80, 16, TFT_BLACK);
        M5.Display.setCursor(0, dispH - 16);
        M5.Display.setTextColor(TFT_GREEN);
        M5.Display.printf("FPS:%.1f", static_cast<double>(fps));
    }
}
