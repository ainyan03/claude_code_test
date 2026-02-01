// fleximg M5Stack Basic Example
// 複数Source合成・アフィン変換デモ
// 4/8/16個のソースを異なるピクセルフォーマットで合成

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

// ========================================
// 定数定義
// ========================================

static constexpr int MAX_SOURCES = 32;
static constexpr int IMAGE_SIZE = 32;

// ピクセルフォーマット配列
static const PixelFormatID FORMATS[4] = {
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::RGBA8_Straight
};

// フォーマット名（将来のUI表示用）
// static const char* FORMAT_NAMES[4] = {
//     "RGB332", "RGB565_LE", "RGB888", "RGBA8"
// };

// 模様タイプ
enum class PatternType {
    Checker = 0,
    VerticalStripe,
    HorizontalStripe,
    Gradient
};

// ベース色（RGB888形式、模様ごとに異なる色相）
static const uint32_t PATTERN_COLORS[4][2] = {
    { 0xFF4040, 0x802020 },  // チェッカー: 赤系
    { 0x40FF40, 0x208020 },  // 縦ストライプ: 緑系
    { 0x4040FF, 0x202080 },  // 横ストライプ: 青系
    { 0xFFFF40, 0x808020 }   // グラデーション: 黄系
};

// ========================================
// 画像生成関数
// ========================================

// RGB888 → 各フォーマットへの変換
static uint8_t toRGB332(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint8_t>((r & 0xE0) | ((g & 0xE0) >> 3) | ((b & 0xC0) >> 6));
}

static uint16_t toRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 模様生成（座標から色とアルファを決定）
static void getPatternColor(PatternType pattern, int x, int y, int width, int height,
                            uint32_t color1, uint32_t color2,
                            uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    bool useColor1 = false;
    a = 255;  // デフォルトは不透明

    switch (pattern) {
        case PatternType::Checker:
            // 8x8 チェッカーパターン
            useColor1 = ((x / 8) + (y / 8)) % 2 == 0;
            break;

        case PatternType::VerticalStripe:
            // 4ピクセル幅の縦ストライプ
            useColor1 = (x / 4) % 2 == 0;
            break;

        case PatternType::HorizontalStripe:
            // 4ピクセル幅の横ストライプ
            useColor1 = (y / 4) % 2 == 0;
            break;

        case PatternType::Gradient:
            // カラフルな虹色グラデーション（HSV的なアプローチ）
            {
                float fx = static_cast<float>(x) / static_cast<float>(width);
                float fy = static_cast<float>(y) / static_cast<float>(height);
                // 色相を位置から計算（0〜1を赤→緑→青→赤で一周）
                float hue = fmodf(fx + fy, 1.0f);
                // HSV to RGB（S=1, V=1）
                float h6 = hue * 6.0f;
                int sector = static_cast<int>(h6);
                float f = h6 - static_cast<float>(sector);
                float q = 1.0f - f;
                switch (sector % 6) {
                    case 0: r = 255; g = static_cast<uint8_t>(f * 255); b = 0; break;
                    case 1: r = static_cast<uint8_t>(q * 255); g = 255; b = 0; break;
                    case 2: r = 0; g = 255; b = static_cast<uint8_t>(f * 255); break;
                    case 3: r = 0; g = static_cast<uint8_t>(q * 255); b = 255; break;
                    case 4: r = static_cast<uint8_t>(f * 255); g = 0; b = 255; break;
                    default: r = 255; g = 0; b = static_cast<uint8_t>(q * 255); break;
                }
            }
            return;  // グラデーションは早期リターン
    }

    // パターン: 2色から選択
    uint32_t color = useColor1 ? color1 : color2;
    r = static_cast<uint8_t>((color >> 16) & 0xFF);
    g = static_cast<uint8_t>((color >> 8) & 0xFF);
    b = static_cast<uint8_t>(color & 0xFF);

    // アルファ値: 中心からの距離に応じて変化（RGBA8用）
    float cx = static_cast<float>(x) - static_cast<float>(width) / 2.0f;
    float cy = static_cast<float>(y) - static_cast<float>(height) / 2.0f;
    float dist = sqrtf(cx * cx + cy * cy);
    float maxDist = sqrtf(static_cast<float>(width * width + height * height)) / 2.0f;
    // 中心で192、端で64（より透過的に）
    a = static_cast<uint8_t>(192 - (dist / maxDist) * 128);
}

// 指定フォーマット・模様で画像を生成
static ImageBuffer createPatternImage(int width, int height,
                                       PixelFormatID format,
                                       PatternType pattern,
                                       uint32_t color1, uint32_t color2) {
    ImageBuffer img(width, height, format);

    for (int y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(img.pixelAt(0, y));

        for (int x = 0; x < width; ++x) {
            uint8_t r, g, b, a;
            getPatternColor(pattern, x, y, width, height, color1, color2, r, g, b, a);

            if (format == PixelFormatIDs::RGB332) {
                row[x] = toRGB332(r, g, b);
            } else if (format == PixelFormatIDs::RGB565_LE) {
                uint16_t* row16 = reinterpret_cast<uint16_t*>(row);
                row16[x] = toRGB565(r, g, b);
            } else if (format == PixelFormatIDs::RGB888) {
                row[x * 3 + 0] = r;
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = b;
            } else {  // RGBA8_Straight（アルファ値を使用）
                row[x * 4 + 0] = r;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = b;
                row[x * 4 + 3] = a;
            }
        }
    }

    return img;
}

// ========================================
// モード定義
// ========================================

enum class DemoMode {
    SingleDirect = 0,  // 単一Source → Renderer → Sink（CompositeNodeなし）
    SingleComposite,   // 単一Source → CompositeNode(1入力) → Renderer → Sink
    Four,              // 4個（各フォーマット1つ）
    Eight,             // 8個（各フォーマット2模様）
    Sixteen,           // 16個（全組み合わせ）
    ThirtyTwoAlpha,    // 32個（RGBA透過のみ、性能限界テスト）
    MODE_COUNT
};

enum class SpeedLevel {
    Slow = 0,
    Normal,
    Fast,
    LEVEL_COUNT
};

static const float SPEED_MULTIPLIERS[] = { 0.3f, 1.0f, 2.5f };
static const char* SPEED_NAMES[] = { "Slow", "Normal", "Fast" };
static const char* MODE_NAMES[] = { "1 Direct", "1 Composite", "4 Sources", "8 Sources", "16 Sources", "32 Alpha" };
static const int MODE_SOURCE_COUNTS[] = { 1, 1, 4, 8, 16, 32 };

// ========================================
// グローバル変数
// ========================================

static DemoMode currentMode = DemoMode::Four;
static SpeedLevel speedLevel = SpeedLevel::Normal;
static bool reverseDirection = false;

// 画像バッファ（16枚: 4フォーマット × 4模様）
static ImageBuffer srcImages[MAX_SOURCES];

// アニメーション
static float rotationAngle = 0.0f;
static float individualAngles[MAX_SOURCES];

// ノード
static SourceNode sources[MAX_SOURCES];
static AffineNode affines[MAX_SOURCES];
static CompositeNode composite(MAX_SOURCES);
static RendererNode renderer;
static LcdSinkNode lcdSink;

// PoolAllocator用のメモリプール
static constexpr size_t POOL_BLOCK_SIZE = 512;
static constexpr size_t POOL_BLOCK_COUNT = 32;
static uint8_t poolMemory[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];

static fleximg::core::memory::PoolAllocator internalPool;
static fleximg::core::memory::PoolAllocatorAdapter* poolAdapter = nullptr;

// 画面サイズ
static int16_t drawW = 320;
static int16_t drawH = 200;
static int16_t drawX = 0;
static int16_t drawY = 0;

// UI更新フラグ
static bool needsUIUpdate = true;

// ========================================
// 配置計算
// ========================================

// 4個: 四隅配置
static void calcOffsets4(float offsets[][2]) {
    const float distX = 70.0f;
    const float distY = 45.0f;
    offsets[0][0] = -distX; offsets[0][1] = -distY;  // 左上
    offsets[1][0] =  distX; offsets[1][1] = -distY;  // 右上
    offsets[2][0] = -distX; offsets[2][1] =  distY;  // 左下
    offsets[3][0] =  distX; offsets[3][1] =  distY;  // 右下
}

// 8個: 円周配置（画面を広く使用）
static void calcOffsets8(float offsets[][2]) {
    const float radiusX = 100.0f;
    const float radiusY = 65.0f;
    for (int i = 0; i < 8; ++i) {
        float angle = static_cast<float>(i) * static_cast<float>(M_PI) / 4.0f - static_cast<float>(M_PI) / 2.0f;
        offsets[i][0] = radiusX * cosf(angle);
        offsets[i][1] = radiusY * sinf(angle);
    }
}

// 16個: 4x4グリッド配置（画面を広く使用）
static void calcOffsets16(float offsets[][2]) {
    const float spacingX = 52.0f;
    const float spacingY = 38.0f;
    const float startX = -spacingX * 1.5f;
    const float startY = -spacingY * 1.5f;

    for (int i = 0; i < 16; ++i) {
        int col = i % 4;
        int row = i / 4;
        offsets[i][0] = startX + static_cast<float>(col) * spacingX;
        offsets[i][1] = startY + static_cast<float>(row) * spacingY;
    }
}

// 32個: 二重円周配置（内周16個＋外周16個）
static void calcOffsets32(float offsets[][2]) {
    const float outerRadiusX = 110.0f;
    const float outerRadiusY = 75.0f;
    const float innerRadiusX = 55.0f;
    const float innerRadiusY = 38.0f;

    // 外周16個
    for (int i = 0; i < 16; ++i) {
        float angle = static_cast<float>(i) * static_cast<float>(M_PI) / 8.0f - static_cast<float>(M_PI) / 2.0f;
        offsets[i][0] = outerRadiusX * cosf(angle);
        offsets[i][1] = outerRadiusY * sinf(angle);
    }
    // 内周16個（少しオフセットして重なりを促進）
    for (int i = 0; i < 16; ++i) {
        float angle = static_cast<float>(i) * static_cast<float>(M_PI) / 8.0f - static_cast<float>(M_PI) / 2.0f + static_cast<float>(M_PI) / 16.0f;
        offsets[16 + i][0] = innerRadiusX * cosf(angle);
        offsets[16 + i][1] = innerRadiusY * sinf(angle);
    }
}

// 現在のモードに応じたオフセット配列
static float currentOffsets[MAX_SOURCES][2];

static void updateOffsets() {
    switch (currentMode) {
        case DemoMode::SingleDirect:
        case DemoMode::SingleComposite:
            // 単一ソース: 中央配置
            currentOffsets[0][0] = 0.0f;
            currentOffsets[0][1] = 0.0f;
            break;
        case DemoMode::Four:
            calcOffsets4(currentOffsets);
            break;
        case DemoMode::Eight:
            calcOffsets8(currentOffsets);
            break;
        case DemoMode::Sixteen:
            calcOffsets16(currentOffsets);
            break;
        case DemoMode::ThirtyTwoAlpha:
            calcOffsets32(currentOffsets);
            break;
        default:
            break;
    }
}

// ========================================
// パイプライン構築
// ========================================

// 使用する画像インデックスを取得
// SingleDirect/SingleComposite: グラデーションRGBA8（最も負荷が高いパターン）
// 4個モード: 各フォーマット1つ（模様0: チェッカー）
// 8個モード: 各フォーマット2模様（チェッカー + 縦ストライプ）
// 16個モード: 全16枚
static int getImageIndex(int sourceIndex) {
    switch (currentMode) {
        case DemoMode::SingleDirect:
        case DemoMode::SingleComposite:
            // グラデーションRGBA8を使用（pattern=3, format=3）
            return 3 * 4 + 3;  // index 15
        case DemoMode::Four:
            // フォーマット0-3、模様はチェッカー(0)
            return sourceIndex;  // 0, 1, 2, 3
        case DemoMode::Eight:
            // フォーマット0-3 × 模様0-1
            {
                int format = sourceIndex % 4;
                int pattern = sourceIndex / 4;
                return pattern * 4 + format;
            }
        case DemoMode::Sixteen:
            return sourceIndex;
        case DemoMode::ThirtyTwoAlpha:
            // RGBA8のみ使用（インデックス3, 7, 11, 15 = 各模様のRGBA8版）
            // 隣接するソースが異なる模様になるよう交互配置
            {
                int pattern = sourceIndex % 4;
                return pattern * 4 + 3;  // フォーマット3 = RGBA8_Straight
            }
        default:
            return sourceIndex;
    }
}

static void rebuildPipeline() {
    int sourceCount = MODE_SOURCE_COUNTS[static_cast<int>(currentMode)];

    // 全ノードの接続をクリア
    for (int i = 0; i < MAX_SOURCES; ++i) {
        sources[i].disconnectAll();
        affines[i].disconnectAll();
    }
    composite.disconnectAll();
    renderer.disconnectAll();
    lcdSink.disconnectAll();

    // オフセット更新
    updateOffsets();

    // SingleDirectモード: CompositeNodeを使わない
    if (currentMode == DemoMode::SingleDirect) {
        int imgIdx = getImageIndex(0);
        sources[0].setSource(srcImages[imgIdx].view());
        sources[0].setPivot(
            float_to_fixed(IMAGE_SIZE / 2.0f),
            float_to_fixed(IMAGE_SIZE / 2.0f)
        );

        // Source → Affine → Renderer → Sink（CompositeNodeなし）
        sources[0] >> affines[0] >> renderer >> lcdSink;

        float scale = 2.5f;  // 単一なので大きめに
        affines[0].setScale(scale, scale);
        affines[0].setTranslation(0.0f, 0.0f);
        return;
    }

    // CompositeNodeの入力数を設定
    composite.setInputCount(sourceCount);

    // 各ソースを接続
    for (int i = 0; i < sourceCount; ++i) {
        int imgIdx = getImageIndex(i);
        sources[i].setSource(srcImages[imgIdx].view());
        sources[i].setPivot(
            float_to_fixed(IMAGE_SIZE / 2.0f),
            float_to_fixed(IMAGE_SIZE / 2.0f)
        );

        sources[i] >> affines[i];
        affines[i].connectTo(composite, i);

        // 初期配置（モード切替時も現在の角度を維持）
        float scale = (currentMode == DemoMode::ThirtyTwoAlpha) ? 1.5f :
                      (currentMode == DemoMode::Sixteen) ? 1.3f :
                      (currentMode == DemoMode::SingleComposite) ? 2.5f : 1.8f;
        affines[i].setScale(scale, scale);
        affines[i].setTranslation(currentOffsets[i][0], currentOffsets[i][1]);
    }

    composite >> renderer >> lcdSink;

    // Composite の行列をリセット
    composite.setMatrix(AffineMatrix{});
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
    drawW = 320;
    drawH = 200;
    drawX = (screenW - drawW) / 2;
    drawY = 40;

    // PoolAllocator初期化
    internalPool.initialize(poolMemory, POOL_BLOCK_SIZE, POOL_BLOCK_COUNT, false);
    static fleximg::core::memory::PoolAllocatorAdapter adapter(internalPool);
    poolAdapter = &adapter;

    // 16枚の画像を生成（4フォーマット × 4模様）
    for (int pattern = 0; pattern < 4; ++pattern) {
        for (int format = 0; format < 4; ++format) {
            int idx = pattern * 4 + format;
            srcImages[idx] = createPatternImage(
                IMAGE_SIZE, IMAGE_SIZE,
                FORMATS[format],
                static_cast<PatternType>(pattern),
                PATTERN_COLORS[pattern][0],
                PATTERN_COLORS[pattern][1]
            );
        }
    }

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

    // 初期パイプライン構築
    rebuildPipeline();

    M5.Display.startWrite();
}

// ========================================
// UI描画
// ========================================

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

// ========================================
// メインループ
// ========================================

void loop() {
#if defined(M5UNIFIED_PC_BUILD)
    lgfx::delay(16);
#endif
    M5.update();

    // ボタン処理
    if (M5.BtnA.wasSingleClicked()) {
        int mode = static_cast<int>(currentMode);
        mode = (mode + 1) % static_cast<int>(DemoMode::MODE_COUNT);
        currentMode = static_cast<DemoMode>(mode);
        rebuildPipeline();
        needsUIUpdate = true;
    }

    if (M5.BtnB.wasSingleClicked()) {
        int level = static_cast<int>(speedLevel);
        level = (level + 1) % static_cast<int>(SpeedLevel::LEVEL_COUNT);
        speedLevel = static_cast<SpeedLevel>(level);
        needsUIUpdate = true;
    }

    if (M5.BtnC.wasSingleClicked()) {
        reverseDirection = !reverseDirection;
        needsUIUpdate = true;
    }

    if (M5.BtnC.wasDoubleClicked()) {
        lcdSink.setDrawEnabled(!lcdSink.getDrawEnabled());
    }

    // UI更新
    if (needsUIUpdate) {
        drawUI();
    }

    // 速度計算
    float speedMult = SPEED_MULTIPLIERS[static_cast<int>(speedLevel)];
    float direction = reverseDirection ? -1.0f : 1.0f;
    float deltaAngle = 0.05f * speedMult * direction;

    // 回転角度更新（fmod で連続的に正規化）
    // composite が 0.5 倍速で回転するため、4π で正規化して周回を合わせる
    constexpr float FULL_CYCLE = 4.0f * static_cast<float>(M_PI);
    rotationAngle += deltaAngle;
    rotationAngle = fmodf(rotationAngle, FULL_CYCLE);
    if (rotationAngle < 0.0f) rotationAngle += FULL_CYCLE;

    // 各ソースの更新
    int sourceCount = MODE_SOURCE_COUNTS[static_cast<int>(currentMode)];
    float baseScale = (currentMode == DemoMode::SingleDirect ||
                       currentMode == DemoMode::SingleComposite) ? 2.5f :
                      (currentMode == DemoMode::ThirtyTwoAlpha) ? 1.5f :
                      (currentMode == DemoMode::Sixteen) ? 1.3f : 1.8f;

    constexpr float ONE_CYCLE = 2.0f * static_cast<float>(M_PI);
    for (int i = 0; i < sourceCount; ++i) {
        // 個別回転（各ソースが少しずつ異なる速度で自転）
        // 速度差を小さくして滑らかなアニメーションに
        individualAngles[i] += deltaAngle * (1.0f + 0.05f * static_cast<float>(i));
        individualAngles[i] = fmodf(individualAngles[i], ONE_CYCLE);
        if (individualAngles[i] < 0.0f) individualAngles[i] += ONE_CYCLE;

        float scale = baseScale;
        if (currentMode == DemoMode::ThirtyTwoAlpha) {
            // 32個モード: スケールを1.0〜2.0で周期的に変化
            // 各ソースで位相をずらして波打つ効果
            float phase = individualAngles[i] + static_cast<float>(i) * 0.3f;
            scale = 1.5f + 0.5f * sinf(phase);  // 1.0〜2.0
        }

        affines[i].setRotationScale(individualAngles[i], scale, scale);
        affines[i].setTranslation(currentOffsets[i][0], currentOffsets[i][1]);
    }

    // Composite全体も公転（SingleDirectモード以外）
    if (currentMode != DemoMode::SingleDirect) {
        composite.setRotation(-rotationAngle * 0.5f);
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
        M5.Display.fillRect(0, dispH - 16, 100, 16, TFT_BLACK);
        M5.Display.setCursor(0, dispH - 16);
        M5.Display.setTextColor(TFT_GREEN);
        M5.Display.printf("FPS:%.1f", static_cast<double>(fps));
    }
}
