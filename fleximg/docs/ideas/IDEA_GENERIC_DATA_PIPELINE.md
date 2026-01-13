# flexpipeline 設計文書

## 作成日
2026-01-13

## プロジェクト概要

| 項目 | 内容 |
|------|------|
| プロジェクト名 | `flexpipeline` |
| 配置場所 | リポジトリルート `/flexpipeline/` |
| 名前空間 | `flexpl` |
| fleximg との関係 | **完全独立**、fleximg の全機能再現が目標 |
| ビルド | **WebAssembly (Emscripten) 必須** |

## 概要

汎用データパイプライン基盤を新規プロジェクトとして構築する。

対応予定のデータ型:
- 画像処理（fleximg 相当の機能）
- 音声処理（将来拡張）
- シリアル通信（UART, SPI, I2C）（将来拡張）
- センサーデータ処理（将来拡張）

## 設計原則

### 1. ゼロコスト抽象化

**汎用化によって、現行の画像処理パフォーマンスを低下させない**

- 画像のみを扱う場合、汎用化前と同等のパフォーマンスを維持
- 使用しない機能（音声、シリアル等）による追加コストをゼロに
- 抽象化レイヤーはコンパイル時に解決し、実行時オーバーヘッドを最小化

### 2. 組込み環境への配慮

| 項目 | 方針 |
|------|------|
| **メモリ使用量** | 構造体サイズの肥大化を避ける。不要なメタデータ領域を持たない |
| **動的メモリ確保** | 最小限に抑える。可能な限り静的確保やスタック使用 |
| **分岐コスト** | ホットパス（内側ループ）での型判定分岐を避ける |
| **インライン化** | 頻繁に呼ばれる小さな関数は inline / constexpr |
| **仮想関数** | 必要最小限に。内側ループでの仮想呼び出しを避ける |

### 2.1 構造体のメンバー配置

**原則: サイズの大きい型から順に並べる**

これによりパディングを最小化し、メモリ効率を向上させる。

```cpp
// 悪い例（パディングが多い）
struct Bad {
    uint8_t a;      // 1 + 7 padding
    uint64_t b;     // 8
    uint8_t c;      // 1 + 3 padding
    uint32_t d;     // 4
};  // 合計 24 バイト

// 良い例（大きい順）
struct Good {
    uint64_t b;     // 8
    uint32_t d;     // 4
    uint8_t a;      // 1
    uint8_t c;      // 1 + 2 padding
};  // 合計 16 バイト
```

配置ルール:
1. サイズの大きい型から順に並べる
2. 同サイズの場合はアライメント要件の厳しいものを先に
3. 論理的なグループ化よりメモリ効率を優先

### 2.2 メタデータ構造体の制約

**原則: メタデータ構造体内にポインタ型を含めない**

- 固定幅整数型（int16_t, uint32_t 等）のみを使用
- 環境（32bit/64bit）によるサイズ変動を防ぐ

```cpp
// ImageMeta の例（ポインタなし）
struct ImageMeta {
    int32_t stride;      // 固定幅整数
    int32_t originX;
    int32_t originY;
    int16_t width;
    int16_t height;
    uint8_t formatID;
    // void* や char* は禁止
};
```

### 3. 段階的な複雑性

```
シンプルなユースケース → シンプルなコード
複雑なユースケース   → 必要に応じて機能を追加
```

- 画像処理のみなら、従来通りの簡潔なAPIを使用可能
- 音声やシリアル通信が必要な場合のみ、追加の型情報を扱う

### 4. 移行容易性

- 移行期間中はエイリアス/変換関数で互換性を維持
- 最終的に RenderResult / ImageBuffer は廃止し ImagePipelineData に統一
- ViewPort は便利関数として残す

## 設計思想

### 核心的な抽象化

**「純粋なデータはポインタとバイト長のみ。それ以外はすべてメタデータ」**

```
現行の RenderResult:
┌─────────────────────────────────────────┐
│ ImageBuffer                             │
│   ├─ void* data      ← 純粋なデータ     │
│   ├─ size_t capacity ← データ長         │
│   ├─ width, height   ← メタデータ       │
│   ├─ stride          ← メタデータ       │
│   └─ formatID        ← メタデータ       │
│ Point origin         ← メタデータ       │
└─────────────────────────────────────────┘

汎用化後:
┌─────────────────────────────────────────┐
│ DataChunk                               │
│   ├─ void* data      ← 純粋なデータ     │
│   └─ size_t size     ← データ長         │
│ TypedMetadata        ← 型に応じて解釈   │
└─────────────────────────────────────────┘
```

## 詳細設計

### 1. DataChunk - 純粋なデータ

```cpp
// 最小限のデータコンテナ
struct DataChunk {
    // メンバー順序: サイズ大きい順（設計原則2.1）
    void* data = nullptr;
    size_t size = 0;
    ImageAllocator* allocator = nullptr;  // null = 参照モード

    // 所有権判定（allocator の有無で判定）
    bool ownsMemory() const { return allocator != nullptr; }

    // 有効性チェック
    bool isValid() const { return data != nullptr && size > 0; }

    // 解放
    void release() {
        if (data && allocator) {
            allocator->deallocate(data);
            data = nullptr;
            allocator = nullptr;
        }
    }

    // ムーブのみ許可（コピー禁止）
    DataChunk(DataChunk&&) = default;
    DataChunk& operator=(DataChunk&&) = default;
    DataChunk(const DataChunk&) = delete;
    DataChunk& operator=(const DataChunk&) = delete;
};
```

### 2. DataType - 型識別

```cpp
// データ型の識別子
enum class DataType : uint8_t {
    Raw = 0,        // 生バイト列（型なし）
    Image = 1,      // 画像データ
    Audio = 2,      // 音声データ
    Serial = 3,     // シリアル通信データ
    Custom = 255    // ユーザー定義
};
```

### 3. メタデータ構造

```cpp
// 画像メタデータ
struct ImageMeta {
    int16_t width = 0;
    int16_t height = 0;
    int32_t stride = 0;
    uint8_t formatID = 0;
    int32_t originX = 0;    // Q24.8 固定小数点
    int32_t originY = 0;    // Q24.8 固定小数点
};

// 音声メタデータ
struct AudioMeta {
    uint32_t sampleRate = 0;    // Hz
    uint16_t channels = 0;       // チャンネル数
    uint16_t bitDepth = 0;       // ビット深度
    uint64_t timestamp_us = 0;   // マイクロ秒
    uint32_t sampleCount = 0;    // サンプル数
};

// シリアル通信メタデータ
struct SerialMeta {
    uint32_t baudRate = 0;
    uint8_t dataBits = 8;
    uint8_t parity = 0;      // 0=None, 1=Odd, 2=Even
    uint8_t stopBits = 1;
    uint8_t deviceId = 0;    // SPIのCS, I2Cのアドレス等
};

// 生データメタデータ（最小限）
struct RawMeta {
    uint64_t timestamp_us = 0;
    uint32_t sequenceNumber = 0;
};
```

### 4. PipelineHeader - 共通ヘッダ（コンポジション用）

```cpp
// 共通ヘッダ: すべての型別データ構造体の先頭メンバとして持つ
struct PipelineHeader {
    DataChunk chunk;
    DataType type = DataType::Raw;

    bool isValid() const { return chunk.isValid(); }
};

// standard-layout を保証（先頭メンバへのキャストが安全）
static_assert(std::is_standard_layout_v<PipelineHeader>);
```

### 5. 型別データ構造体

各データ型専用の構造体。先頭に必ず `PipelineHeader header` を持つ。

```cpp
// 画像データ
struct ImagePipelineData {
    PipelineHeader header;  // 必ず先頭
    ImageMeta meta;

    // 便利関数
    bool isValid() const { return header.isValid(); }

    // ムーブのみ
    ImagePipelineData(ImagePipelineData&&) = default;
    ImagePipelineData& operator=(ImagePipelineData&&) = default;
    ImagePipelineData(const ImagePipelineData&) = delete;
    ImagePipelineData& operator=(const ImagePipelineData&) = delete;
};

// 音声データ
struct AudioPipelineData {
    PipelineHeader header;  // 必ず先頭
    AudioMeta meta;

    bool isValid() const { return header.isValid(); }

    AudioPipelineData(AudioPipelineData&&) = default;
    AudioPipelineData& operator=(AudioPipelineData&&) = default;
    AudioPipelineData(const AudioPipelineData&) = delete;
    AudioPipelineData& operator=(const AudioPipelineData&) = delete;
};

// シリアル通信データ
struct SerialPipelineData {
    PipelineHeader header;  // 必ず先頭
    SerialMeta meta;

    bool isValid() const { return header.isValid(); }

    SerialPipelineData(SerialPipelineData&&) = default;
    SerialPipelineData& operator=(SerialPipelineData&&) = default;
    SerialPipelineData(const SerialPipelineData&) = delete;
    SerialPipelineData& operator=(const SerialPipelineData&) = delete;
};

// standard-layout 検証（先頭が header であることを保証）
static_assert(std::is_standard_layout_v<ImagePipelineData>);
static_assert(std::is_standard_layout_v<AudioPipelineData>);
static_assert(std::is_standard_layout_v<SerialPipelineData>);
static_assert(offsetof(ImagePipelineData, header) == 0);
static_assert(offsetof(AudioPipelineData, header) == 0);
static_assert(offsetof(SerialPipelineData, header) == 0);
```

### 6. PipelineData - 汎用コンテナ（union版）

型が実行時に決まる場面で使用。型が確定している場合は型別構造体を推奨。

```cpp
// 汎用データコンテナ（型が不明な受け渡し用）
struct PipelineData {
    PipelineHeader header;
    union {
        ImageMeta image;
        AudioMeta audio;
        SerialMeta serial;
        RawMeta raw;
    } meta;

    // 便利関数
    bool isValid() const { return header.isValid(); }
    DataType type() const { return header.type; }

    bool isImage() const { return header.type == DataType::Image; }
    bool isAudio() const { return header.type == DataType::Audio; }
    bool isSerial() const { return header.type == DataType::Serial; }

    // 型安全なメタデータアクセス
    const ImageMeta* asImage() const {
        return isImage() ? &meta.image : nullptr;
    }
    const AudioMeta* asAudio() const {
        return isAudio() ? &meta.audio : nullptr;
    }

    // ムーブのみ
    PipelineData(PipelineData&&) = default;
    PipelineData& operator=(PipelineData&&) = default;
    PipelineData(const PipelineData&) = delete;
    PipelineData& operator=(const PipelineData&) = delete;
};

static_assert(std::is_standard_layout_v<PipelineData>);
static_assert(offsetof(PipelineData, header) == 0);
```

### 設計ルール

1. **先頭メンバ**: すべてのXxxPipelineData構造体は `PipelineHeader header` を先頭に持つ
2. **standard-layout**: `static_assert` で検証し、先頭へのキャストを安全に
3. **使い分け**:
   - 型が確定 → 型別構造体（ImagePipelineData等）を使用
   - 型が不明 → PipelineData（union版）を使用
4. **相互変換**: 先頭が共通なので `reinterpret_cast<PipelineHeader*>` で共通処理可能

### 7. ViewPort との関係

ViewPort は**便利関数として残す**。ImagePipelineData から ViewPort を生成して使用。

```cpp
// ViewPort は既存のまま維持（参照ビュー構造体）
struct ViewPort {
    void* data;
    PixelFormatID formatID;
    int32_t stride;
    int16_t width;
    int16_t height;

    // サブビュー作成、ピクセルアクセス等の機能
    ViewPort subView(int x, int y, int w, int h) const;
    void* pixelAt(int x, int y);
};

// ImagePipelineData から ViewPort を生成
inline ViewPort toViewPort(const ImagePipelineData& data) {
    return ViewPort{
        data.header.chunk.data,
        data.meta.formatID,
        data.meta.stride,
        data.meta.width,
        data.meta.height
    };
}

// ViewPort + allocator から ImagePipelineData を生成
inline ImagePipelineData fromViewPort(ViewPort view, ImageAllocator* allocator = nullptr) {
    ImagePipelineData result;
    result.header.chunk.data = view.data;
    result.header.chunk.size = static_cast<size_t>(view.height) *
                               static_cast<size_t>(std::abs(view.stride));
    result.header.chunk.allocator = allocator;
    result.header.type = DataType::Image;
    result.meta.width = view.width;
    result.meta.height = view.height;
    result.meta.stride = view.stride;
    result.meta.formatID = view.formatID;
    return result;
}
```

**役割分担**:
- **ImagePipelineData**: パイプラインでのデータ受け渡し（所有権管理含む）
- **ViewPort**: 画像処理の実装内部で使用（サブビュー、ピクセルアクセス）

## ノード設計

### 設計方針

- **基底ノード**: `process()` を定義しない（共通機能のみ）
- **特化ノード**: 型別の `process()` を定義（ImagePipelineNode 等）
- **ポートごとの型**: 各ポートが `DataType` を持ち、接続時にチェック

### Port（型情報付き）

```cpp
class Port {
    DataType dataType_ = DataType::Raw;  // このポートが扱うデータ型
    Node* owner_ = nullptr;
    int index_ = 0;
    Port* connected_ = nullptr;

public:
    DataType dataType() const { return dataType_; }
    void setDataType(DataType t) { dataType_ = t; }

    // 接続時にポート同士の型をチェック
    bool connect(Port& other) {
        // Raw は任意の型を受け入れる
        if (dataType_ != other.dataType_ &&
            dataType_ != DataType::Raw &&
            other.dataType_ != DataType::Raw) {
            return false;  // 型不一致
        }
        // DAG禁止: 既に接続済みならエラー
        if (connected_ != nullptr) {
            return false;
        }
        // 接続処理...
        connected_ = &other;
        other.connected_ = this;
        return true;
    }
};
```

### 基底ノード

```cpp
// 基底ノード（process シグネチャなし、共通機能のみ）
class PipelineNode {
public:
    virtual ~PipelineNode() = default;

    // ポートアクセス
    Port* inputPort(int index = 0);
    Port* outputPort(int index = 0);
    int inputPortCount() const;
    int outputPortCount() const;

    // 接続API
    bool connectTo(PipelineNode& target, int targetInput = 0, int outputIndex = 0);
    PipelineNode& operator>>(PipelineNode& downstream);

protected:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;

    void initPorts(int inputCount, int outputCount);
};
```

### 特化ノード基底

各データ型専用のノード基底。`process()` シグネチャを型別に定義。

```cpp
// 画像処理ノード（入出力とも Image）
class ImagePipelineNode : public PipelineNode {
public:
    ImagePipelineNode() {
        initPorts(1, 1);
        inputs_[0].setDataType(DataType::Image);
        outputs_[0].setDataType(DataType::Image);
    }

    // 画像専用 process（派生クラスで実装）
    virtual ImagePipelineData process(ImagePipelineData&& input,
                                       const RenderRequest& request) = 0;

    // プル型処理
    virtual ImagePipelineData pullProcess(const RenderRequest& request);

    // プッシュ型処理
    virtual void pushProcess(ImagePipelineData&& input,
                             const RenderRequest& request);
};

// 音声処理ノード（入出力とも Audio）
class AudioPipelineNode : public PipelineNode {
public:
    AudioPipelineNode() {
        initPorts(1, 1);
        inputs_[0].setDataType(DataType::Audio);
        outputs_[0].setDataType(DataType::Audio);
    }

    virtual AudioPipelineData process(AudioPipelineData&& input) = 0;
};

// シリアル通信ノード（入出力とも Serial）
class SerialPipelineNode : public PipelineNode {
public:
    SerialPipelineNode() {
        initPorts(1, 1);
        inputs_[0].setDataType(DataType::Serial);
        outputs_[0].setDataType(DataType::Serial);
    }

    virtual SerialPipelineData process(SerialPipelineData&& input) = 0;
};
```

### 型変換ノード（特殊ケース）

入力と出力で型が異なるノード。ポートごとに適切な型を設定。

```cpp
// デマルチプレクサ（入力: Serial → 出力: Image + Audio）
class DemuxerNode : public PipelineNode {
public:
    DemuxerNode() {
        initPorts(1, 2);
        inputs_[0].setDataType(DataType::Serial);
        outputs_[0].setDataType(DataType::Image);  // 映像出力
        outputs_[1].setDataType(DataType::Audio);  // 音声出力
    }

    // 特殊な process（複数出力）
    void process(SerialPipelineData&& input) {
        // デコード処理...
        ImagePipelineData video = decodeVideo(input);
        AudioPipelineData audio = decodeAudio(input);

        // 各出力ポートの下流へ push
        pushToOutput(0, std::move(video));
        pushToOutput(1, std::move(audio));
    }

private:
    ImagePipelineData decodeVideo(const SerialPipelineData& input);
    AudioPipelineData decodeAudio(const SerialPipelineData& input);

    template<typename T>
    void pushToOutput(int portIndex, T&& data);
};

// エンコーダ（入力: Image → 出力: Serial）
class ImageEncoderNode : public PipelineNode {
public:
    ImageEncoderNode() {
        initPorts(1, 1);
        inputs_[0].setDataType(DataType::Image);
        outputs_[0].setDataType(DataType::Serial);
    }

    SerialPipelineData process(ImagePipelineData&& input) {
        // JPEG/PNG 等にエンコード
        return encode(input);
    }
};
```

### ノード種別まとめ

| ノード種別 | 入力型 | 出力型 | process シグネチャ |
|-----------|--------|--------|-------------------|
| ImagePipelineNode | Image | Image | `ImagePipelineData(ImagePipelineData&&)` |
| AudioPipelineNode | Audio | Audio | `AudioPipelineData(AudioPipelineData&&)` |
| SerialPipelineNode | Serial | Serial | `SerialPipelineData(SerialPipelineData&&)` |
| DemuxerNode | Serial | Image, Audio | 特殊（複数出力、void） |
| ImageEncoderNode | Image | Serial | `SerialPipelineData(ImagePipelineData&&)` |

## 用途別実装例

### 画像処理（型別構造体を使用）

```cpp
// 現行の ImageBuffer を ImagePipelineData に変換
ImagePipelineData fromImageBuffer(ImageBuffer&& buf, Point origin,
                                   ImageAllocator* allocator = nullptr) {
    ImagePipelineData result;
    result.header.chunk.data = buf.data();
    result.header.chunk.size = buf.totalBytes();
    result.header.chunk.allocator = buf.ownsMemory() ? allocator : nullptr;
    result.header.type = DataType::Image;

    result.meta.width = buf.width();
    result.meta.height = buf.height();
    result.meta.stride = buf.stride();
    result.meta.formatID = buf.formatID();
    result.meta.originX = origin.x;
    result.meta.originY = origin.y;

    return result;
}
```

### 音声処理（型別構造体を使用）

```cpp
class AudioSourceNode : public AudioPipelineNode {
    AudioBuffer audioBuffer_;

public:
    AudioPipelineData process(AudioPipelineData&&) {
        AudioPipelineData result;
        result.header.chunk.data = audioBuffer_.data();
        result.header.chunk.size = audioBuffer_.size();
        result.header.chunk.allocator = nullptr;  // 内部バッファ参照
        result.header.type = DataType::Audio;

        result.meta.sampleRate = 44100;
        result.meta.channels = 2;
        result.meta.bitDepth = 16;
        result.meta.sampleCount = audioBuffer_.sampleCount();

        return result;
    }
};

class VolumeNode : public AudioPipelineNode {
    float gain_ = 1.0f;

public:
    AudioPipelineData process(AudioPipelineData&& input) {
        int16_t* samples = static_cast<int16_t*>(input.header.chunk.data);
        size_t count = input.meta.sampleCount * input.meta.channels;

        for (size_t i = 0; i < count; ++i) {
            samples[i] = static_cast<int16_t>(samples[i] * gain_);
        }

        return std::move(input);
    }
};
```

### UART送信（汎用PipelineDataを使用）

```cpp
class UartSinkNode : public SerialPipelineNode {
    int uartPort_;

public:
    explicit UartSinkNode(int port) : uartPort_(port) {}

    // Raw も受け入れ可能
    DataType inputType() const override { return DataType::Raw; }

    PipelineData process(PipelineData&& input) override {
        // プラットフォーム依存のUART送信
        uart_write(uartPort_, input.header.chunk.data, input.header.chunk.size);
        return {};  // Sink なので空を返す
    }
};
```

### SPI受信（型別構造体を使用）

```cpp
class SpiSourceNode : public SerialPipelineNode {
    int spiPort_;
    size_t readSize_;
    std::vector<uint8_t> buffer_;

public:
    SpiSourceNode(int port, size_t size)
        : spiPort_(port), readSize_(size), buffer_(size) {}

    SerialPipelineData process(SerialPipelineData&&) {
        // SPIから読み取り
        spi_read(spiPort_, buffer_.data(), readSize_);

        SerialPipelineData result;
        result.header.chunk.data = buffer_.data();
        result.header.chunk.size = readSize_;
        result.header.chunk.allocator = nullptr;  // 内部バッファ参照
        result.header.type = DataType::Serial;

        result.meta.deviceId = spiPort_;

        return result;
    }
};
```

## パイプライン例

### 画像をUART送信

```cpp
ImageSourceNode src(imageView, ox, oy);
JpegEncoderNode encoder;      // Image → Raw (JPEG bytes)
UartSinkNode uart(UART1);     // Raw → UART

src >> encoder >> uart;
renderer.exec();
```

### センサー → フィルタ → ログ

```cpp
SpiSourceNode sensor(SPI1, 64);
MovingAverageNode filter(8);
FileLoggerNode logger("sensor.log");

sensor >> filter >> logger;
renderer.exec();
```

### 映像と音声の分離・合成

```cpp
VideoSourceNode video("input.mp4");
DemuxerNode demux;
VideoProcessNode vproc;
AudioProcessNode aproc;
MuxerNode mux;
VideoSinkNode sink("output.mp4");

video >> demux;
demux.videoOutput() >> vproc >> mux.videoInput();
demux.audioOutput() >> aproc >> mux.audioInput();
mux >> sink;

renderer.exec();
```

## 実装フェーズ

**方針**: fleximg とは完全独立の新規プロジェクトとして構築

### Phase 0: プロジェクト準備
- flexpipeline ディレクトリ作成
- ビルド環境構築（Makefile, build.sh, Emscripten 対応）
- テスト環境構築（doctest）
- fleximg のベンチマーク作成（比較用基準値）

### Phase 1: 基盤実装 (flexpl::core)
- common.h（共通定義、マクロ）
- data_chunk.h
- pipeline_header.h
- port.h（型情報付き、DAG禁止チェック）
- pipeline_node.h（基底、process なし）

### Phase 2: 画像処理実装 (flexpl::image)
- image_meta.h
- image_pipeline_data.h
- viewport.h（toViewPort / fromViewPort）
- pixel_format.h（fleximg から移植）
- image_pipeline_node.h

### Phase 3: ノード実装
- source_node.h
- renderer_node.h
- sink_node.h
- affine_node.h（DDA スキャンライン移植）
- composite_node.h
- filter_node_base.h
- フィルタノード群（brightness, grayscale 等）

### Phase 4: WebUI・検証
- demo/web/ 構築
- fleximg との機能比較テスト
- パフォーマンス比較

## メモリレイアウト（64bit環境）

### PipelineHeader（共通ヘッダ）

```
PipelineHeader (32バイト):
┌────────────────────────────────────────┐
│ DataChunk (24バイト)                   │
│   void* data          [8]              │
│   size_t size         [8]              │
│   ImageAllocator* allocator [8]        │
├────────────────────────────────────────┤
│ DataType type         [1]              │
│ padding               [7]              │
└────────────────────────────────────────┘
```

### 型別構造体

```
ImagePipelineData (56バイト):
┌────────────────────────────────────────┐
│ PipelineHeader header [32]             │
├────────────────────────────────────────┤
│ ImageMeta meta        [20]             │
│   int16_t width       [2]              │
│   int16_t height      [2]              │
│   int32_t stride      [4]              │
│   uint8_t formatID    [1]              │
│   padding             [3]              │
│   int32_t originX     [4]              │
│   int32_t originY     [4]              │
│ padding               [4]              │
└────────────────────────────────────────┘

AudioPipelineData (56バイト):
┌────────────────────────────────────────┐
│ PipelineHeader header [32]             │
├────────────────────────────────────────┤
│ AudioMeta meta        [24]             │
│   uint64_t timestamp  [8]              │
│   uint32_t sampleRate [4]              │
│   uint32_t sampleCount[4]              │
│   uint16_t channels   [2]              │
│   uint16_t bitDepth   [2]              │
│   padding             [4]              │
└────────────────────────────────────────┘

SerialPipelineData (40バイト):
┌────────────────────────────────────────┐
│ PipelineHeader header [32]             │
├────────────────────────────────────────┤
│ SerialMeta meta       [8]              │
│   uint32_t baudRate   [4]              │
│   uint8_t dataBits    [1]              │
│   uint8_t parity      [1]              │
│   uint8_t stopBits    [1]              │
│   uint8_t deviceId    [1]              │
└────────────────────────────────────────┘
```

### PipelineData（汎用版、union）

```
PipelineData (56バイト):
┌────────────────────────────────────────┐
│ PipelineHeader header [32]             │
├────────────────────────────────────────┤
│ union meta            [24]             │
│   ImageMeta  [20]                      │
│   AudioMeta  [24]                      │
│   SerialMeta [8]                       │
│   RawMeta    [16]                      │
└────────────────────────────────────────┘
```

## 考慮事項

### パフォーマンス保証

#### コンパイル時の最適化

```cpp
// 画像専用ノードは DataType チェックをコンパイル時に解決
template<DataType T>
class TypedPipelineNode : public PipelineNode {
    // コンパイル時に型が確定 → 実行時分岐なし
    static constexpr DataType TYPE = T;
};

// 画像処理の内側ループでは型チェックしない
void AffineNode::applyAffine(...) {
    // ここに到達する時点で DataType::Image は保証済み
    // ループ内での meta.type チェックは不要
}
```

#### メモリレイアウトの最適化

```cpp
// NG: 毎回メタデータ全体をコピー
PipelineData process(PipelineData input);  // 値渡し

// OK: ムーブで所有権移転のみ
PipelineData process(PipelineData&& input);  // ムーブ
```

#### 分岐の削減

```cpp
// NG: ホットパスでの型判定
for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
        if (meta.type == DataType::Image) {  // 毎ピクセル判定 ← 遅い
            // ...
        }
    }
}

// OK: ループ外で事前判定、関数ポインタで分岐
auto processFunc = selectProcessor(meta.type);  // 1回だけ
for (int y = 0; y < height; ++y) {
    processFunc(row);  // 分岐なし
}
```

#### ベンチマーク基準

汎用化後も以下を維持すること:
- 画像アフィン変換: 現行比 100% 以上の性能
- タイル処理スループット: 現行比 100% 以上
- メモリ使用量: 現行比 110% 以内（メタデータ拡張分のみ許容）

### 型安全性
- コンパイル時チェックは限定的
- 実行時に DataType で検証
- デバッグビルドでアサーション強化
- リリースビルドでは検証をスキップ可能（FLEXIMG_SKIP_TYPE_CHECK）

### メモリ管理
- DataChunk は所有権フラグで管理
- 参照モード（ownsMemory=false）で効率化
- カスタムアロケータ対応は将来検討

### 既存互換性
- RenderResult を互換レイヤーとして維持
- 既存の画像処理コードは変更不要

### 組込み環境での追加考慮

| 環境 | 考慮事項 |
|------|----------|
| **RAM制約** | TypedMetadata の union サイズを最小化（現行36バイト） |
| **ROM制約** | 未使用のノードタイプはリンクされないよう分離 |
| **RTOS** | スレッドセーフは呼び出し側の責任（内部ロックなし） |
| **DMA** | DataChunk のアライメントを保証（4/8バイト境界） |

## 懸念事項と対策

### 懸念1: 所有権管理の複雑化 【深刻度: 高】

**問題**:
```cpp
struct DataChunk {
    void* data;
    size_t size;
    bool ownsMemory;  // これだけでは不十分
};
```

- 現行の ImageBuffer は `allocator_` で解放先を管理
- DataChunk に変換した時点で、誰が・どのアロケータで解放するかが曖昧
- 参照モード → 所有モードへの変換時にバグが混入しやすい

**対策案**:
```cpp
struct DataChunk {
    void* data = nullptr;
    size_t size = 0;
    ImageAllocator* allocator = nullptr;  // null = 参照モード（解放しない）
};
```

**検討状況**: 解決済み

**決定**: 案B（アロケータを保持）を採用

```cpp
struct DataChunk {
    void* data = nullptr;
    size_t size = 0;
    ImageAllocator* allocator = nullptr;  // null = 参照モード

    bool ownsMemory() const { return allocator != nullptr; }

    void release() {
        if (data && allocator) {
            allocator->deallocate(data);
            data = nullptr;
            allocator = nullptr;
        }
    }
};
```

理由:
- 現行 ImageBuffer と同じモデルで整合性が保てる
- カスタムアロケータ（DMAバッファ等）に対応可能
- 「null = 参照モード」のルールがそのまま使える

---

### 懸念2: 互換レイヤーの二重管理 【深刻度: 高】

**問題**:
```cpp
struct RenderResult {
    PipelineData data_;  // 内部で PipelineData を持つ
    ImageBuffer& buffer() { /* ??? */ }  // 何を返す？
};
```

- ImageBuffer と DataChunk が別物で、`buffer()` が何を返すか曖昧
- 同じデータへの2つのビュー（ImageBuffer / DataChunk）が存在
- 整合性維持が困難、バグの温床

**対策案**:
- A案: 互換レイヤーではなく完全置換
- B案: ImageBuffer 自体を DataChunk + ImageMeta の薄いラッパーに再設計
- C案: 移行期間を設けず、一括で新設計に移行

**検討状況**: 解決済み

**決定**: ImageBuffer / RenderResult を廃止し、ImagePipelineData に統一

最終形（コンポジション方式）:
```cpp
struct PipelineHeader {
    DataChunk chunk;
    DataType type = DataType::Raw;
};

struct ImagePipelineData {
    PipelineHeader header;
    ImageMeta meta;
};
```

- ImageBuffer, RenderResult は最終的に廃止
- 移行期間中はエイリアス/変換関数を使用可
- ViewPort は便利関数として残す（toViewPort / fromViewPort）

---

### 懸念3: パフォーマンス検証手段の不足 【深刻度: 中、優先度: 高】

**問題**:
- 「現行比100%以上」を保証するベンチマークが存在しない
- パフォーマンスリグレッションを検出できない

**対策案**:
- 汎用化実装**前に**ベンチマークを整備
- CI でパフォーマンス計測を自動化
- 主要処理（アフィン変換、ブレンド、フィルタ）の基準値を記録

**検討状況**: 方針決定

**決定**:
- 汎用化作業**前に**ベンチマークプログラムを作成
- 基準値を記録し、移行後に比較可能にする
- 詳細な計測対象・実装は別途決定

**TODO**: ベンチマークプログラムの作成（内容は後日決定）

---

### 懸念4: union の将来拡張 【深刻度: 中】

**問題**:
```cpp
union {
    ImageMeta image;    // 20バイト
    AudioMeta audio;    // 20バイト
    SerialMeta serial;  // 8バイト
    RawMeta raw;        // 12バイト
};  // 最大 20バイト → 新型追加で肥大化
```

- 新しいデータ型を追加すると union サイズが増加
- 全メタデータが最大サイズを消費

**対策案**:
- A案: メタデータを外部ポインタで持つオプション
- B案: Core（必須）と Extended（オプション）に分離
- C案: 最大サイズを固定し、超える場合は外部参照

**検討状況**: 解決済み

**決定**:
- 現時点では固定union（案A）で進める
- メタデータ構造体にはポインタを含めない（設計原則2.2）
- 構造体メンバーはサイズの大きい順に配置（設計原則2.1）
- 拡張が必要な場合は `extendedMeta` ポインタを使用

---

### 懸念5: 32bit 環境でのメモリレイアウト 【深刻度: 低】

**問題**:
```
64bit: void* = 8バイト, size_t = 8バイト
32bit: void* = 4バイト, size_t = 4バイト
```

- 文書のメモリレイアウト図が64bit前提
- 組込み（32bitマイコン）では異なるサイズ

**対策案**:
- 両環境でのレイアウトを文書化
- `static_assert(sizeof(DataChunk) == EXPECTED_SIZE)` で検証
- 固定幅型（uint32_t等）の使用を検討

**検討状況**: 解決済み

懸念4の対策（メタデータにポインタを含めない）により解決。
DataChunk のポインタサイズが環境依存なのは自明であり、問題なし。

---

### 懸念6: スコープクリープのリスク 【深刻度: 中】

**問題**:
- 汎用パイプラインを作ると、音声・通信・センサー等を追加したくなる
- fleximg の本来の目的（軽量な画像処理ライブラリ）から離れる

**対策案**:
- 画像処理を最優先、他は「拡張可能だが実装しない」スタンス
- 汎用パイプライン基盤を別ライブラリ/別名前空間として分離
- 明確なスコープ定義を文書化

**検討状況**: 解決済み

**決定**: fleximg とは完全独立の新規プロジェクト `flexpipeline` として構築

```
flexpipeline/
├── src/
│   └── flexpl/
│       ├── core/              # 汎用パイプライン基盤
│       │   ├── data_chunk.h
│       │   ├── pipeline_header.h
│       │   ├── port.h
│       │   └── pipeline_node.h
│       │
│       └── image/             # 画像処理
│           ├── image_meta.h
│           ├── image_pipeline_data.h
│           ├── image_pipeline_node.h
│           └── nodes/
```

名前空間:
```cpp
namespace flexpl {
    namespace core { /* 汎用基盤 */ }
    namespace image { /* 画像処理 */ }
}
```

責務:
- **flexpl::core**: データフロー基盤（DataChunk, PipelineHeader, Port, PipelineNode）
- **flexpl::image**: 画像処理（ImageMeta, ImagePipelineNode, 画像ノード群）
- 音声・通信等は将来拡張として `flexpl::audio`, `flexpl::serial` を追加可能

---

### 懸念7: テスト戦略 【深刻度: 中】

**問題**:
- 新規プロジェクトのためテストを一から構築する必要がある
- fleximg と同等の品質を保証する必要がある

**対策案**:
- doctest によるユニットテスト環境を構築
- fleximg のテストケースを参考に同等のカバレッジを確保
- fleximg との出力比較テストで機能の同等性を検証

**検討状況**: 解決済み

**決定**: doctest + fleximg 比較テストで対応

1. **Phase 0**: fleximg のベンチマーク・テストデータを準備
2. **実装中**: 各ノード実装後に fleximg と同一入力での出力比較
3. **完成後**: flexpipeline 単独のテストスイートとして維持

---

## 関連文書
- [IDEA_DAG_PROHIBITION.md](IDEA_DAG_PROHIBITION.md) - DAG禁止方針
