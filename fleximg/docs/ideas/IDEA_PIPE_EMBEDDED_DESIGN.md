# Pipe 埋め込み設計案

## 概要

fleximg のパイプライン接続構造を再設計し、FlexLib で検討した設計思想を反映する。

**目標**:
- メモリ効率の最大化（動的確保なし）
- 型安全性（コンパイル時に型確定）
- 組み込み環境への最適化
- ユーザー管理負担の最小化

---

## 現状（fleximg）

### Port 構造

```cpp
struct Port {
    Node* owner = nullptr;
    Port* connected = nullptr;
    int index = 0;
};

class Node {
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;
};
```

**課題**:
- Port に状態管理がない（Node 側で管理）
- DataType による型区別がない
- vector による動的確保

---

## 新設計: Pipe 埋め込み方式

### 基本方針

1. **Pipe 実体は上流ノード（Source）がメンバとして所有**
2. **下流ノード（Sink）はポインタで参照**
3. **派生型ごとに適切な Pipe 型を埋め込み**
4. **virtual を最小限に（DataType で型識別）**

### 構造図

```
FilterNode (上流)           SinkNode (下流)
┌─────────────────┐        ┌─────────────────┐
│ ImagePipe output_│───────→│ Pipe* input_    │
│   [実体]         │        │   [参照]        │
└─────────────────┘        └─────────────────┘
```

---

## 実装詳細

### DataType 列挙型

```cpp
enum class DataType : uint8_t {
    Raw = 0,
    Image,
    Audio,
    Stream,
    Spi,
    I2c,
};
```

### Pipe 基底構造体

```cpp
struct Pipe {
    DataType type = DataType::Raw;  // 先頭配置（型識別用）

    enum class State : uint8_t {
        Idle,      // 待機中
        Request,   // データ要求中
        Complete,  // データ準備完了
        Release,   // 解放要求中
        Failed     // エラー
    };
    State state = State::Idle;

    Node* owner = nullptr;          // この Pipe を所有するノード
    Node* connected = nullptr;      // 接続先ノード
    Pipe** connectedSlot = nullptr; // 接続先の Pipe* スロット

    // 接続状態
    bool isConnected() const { return connected != nullptr; }

    // 接続（下流の Pipe* スロットに自分を設定）
    bool connect(Pipe*& targetSlot, Node* targetNode) {
        if (connected != nullptr) return false;
        if (targetSlot != nullptr) return false;

        connected = targetNode;
        connectedSlot = &targetSlot;
        targetSlot = this;
        return true;
    }

    // 切断
    void disconnect() {
        if (connectedSlot) {
            *connectedSlot = nullptr;
            connectedSlot = nullptr;
        }
        connected = nullptr;
        state = State::Idle;
    }

    // 所有者消滅時（デストラクタから呼ばれる）
    void onOwnerDestroyed() {
        disconnect();
    }

    // 状態遷移
    void request() { state = State::Request; }
    void complete() { state = State::Complete; }
    void release() { state = State::Release; }
    void idle() { state = State::Idle; }
    void fail() { state = State::Failed; }
};
```

### 派生 Pipe（画像用）

```cpp
struct ImagePipe : Pipe {
    ImagePipe() { type = DataType::Image; }

    PixelFormat format = nullptr;
    int16_t width = 0;
    int16_t height = 0;
};
```

### Node 基底クラス

```cpp
class Node {
public:
    virtual ~Node() = default;

    // 派生型が実装
    virtual int inputCount() const { return 0; }
    virtual int outputCount() const { return 0; }
    virtual Pipe* inputPipe(int index) { (void)index; return nullptr; }
    virtual Pipe* outputPipe(int index) { (void)index; return nullptr; }

    // 接続 API
    bool connectTo(Node& target, int targetInput = 0, int outputIdx = 0) {
        Pipe* out = outputPipe(outputIdx);
        Pipe** inSlot = target.inputSlot(targetInput);
        if (!out || !inSlot) return false;
        return out->connect(*inSlot, &target);
    }

    // チェーン接続演算子
    Node& operator>>(Node& downstream) {
        connectTo(downstream);
        return downstream;
    }

protected:
    virtual Pipe** inputSlot(int index) { (void)index; return nullptr; }
};
```

### 派生ノード例: 単一入出力フィルタ

```cpp
class FilterNode : public Node {
protected:
    ImagePipe output_;
    Pipe* input_ = nullptr;

public:
    FilterNode() {
        output_.owner = this;
    }

    ~FilterNode() override {
        output_.onOwnerDestroyed();
    }

    int inputCount() const override { return 1; }
    int outputCount() const override { return 1; }

    Pipe* inputPipe(int index) override {
        return (index == 0) ? input_ : nullptr;
    }
    Pipe* outputPipe(int index) override {
        return (index == 0) ? &output_ : nullptr;
    }

protected:
    Pipe** inputSlot(int index) override {
        return (index == 0) ? &input_ : nullptr;
    }
};
```

### 派生ノード例: 複数入力（合成）

```cpp
class CompositeNode : public Node {
protected:
    ImagePipe output_;
    std::array<Pipe*, 4> inputs_ = {};

public:
    CompositeNode() {
        output_.owner = this;
    }

    ~CompositeNode() override {
        output_.onOwnerDestroyed();
    }

    int inputCount() const override { return 4; }
    int outputCount() const override { return 1; }

    Pipe* inputPipe(int index) override {
        return (index >= 0 && index < 4) ? inputs_[index] : nullptr;
    }
    Pipe* outputPipe(int index) override {
        return (index == 0) ? &output_ : nullptr;
    }

protected:
    Pipe** inputSlot(int index) override {
        return (index >= 0 && index < 4) ? &inputs_[index] : nullptr;
    }
};
```

### 派生ノード例: 複数出力（分配）

```cpp
class DistributorNode : public Node {
protected:
    std::array<ImagePipe, 4> outputs_;
    Pipe* input_ = nullptr;

public:
    DistributorNode() {
        for (auto& out : outputs_) {
            out.owner = this;
        }
    }

    ~DistributorNode() override {
        for (auto& out : outputs_) {
            out.onOwnerDestroyed();
        }
    }

    int inputCount() const override { return 1; }
    int outputCount() const override { return 4; }

    Pipe* inputPipe(int index) override {
        return (index == 0) ? input_ : nullptr;
    }
    Pipe* outputPipe(int index) override {
        return (index >= 0 && index < 4) ? &outputs_[index] : nullptr;
    }

protected:
    Pipe** inputSlot(int index) override {
        return (index == 0) ? &input_ : nullptr;
    }
};
```

---

## 使用例

```cpp
SourceNode src;
FilterNode filter;
SinkNode sink;

// チェーン接続
src >> filter >> sink;

// または明示的に
src.connectTo(filter, 0, 0);
filter.connectTo(sink, 0, 0);

// 状態アクセス
Pipe* pipe = src.outputPipe(0);
pipe->request();
// ... 処理 ...
pipe->complete();
```

---

## ダングリングポインタ対策

### 上流ノードが先に破棄された場合

```cpp
// FilterNode のデストラクタ
~FilterNode() {
    output_.onOwnerDestroyed();  // 下流の input_ を nullptr に
}

// Pipe::onOwnerDestroyed()
void onOwnerDestroyed() {
    if (connectedSlot) {
        *connectedSlot = nullptr;  // 下流の参照をクリア
    }
    connected = nullptr;
    connectedSlot = nullptr;
}
```

### 下流ノードが先に破棄された場合

上流の `output_.connected` はダングリングになるが、
下流ノードのデストラクタで通知する方式も可能:

```cpp
// SinkNode のデストラクタ
~SinkNode() {
    if (input_ && input_->connected == this) {
        input_->connected = nullptr;
        input_->connectedSlot = nullptr;
    }
}
```

---

## メモリレイアウト比較

### 従来方式（vector<Port>）

```
Node:
  vtable ptr         8 bytes
  vector<Port>      24 bytes (ptr + size + capacity)
  vector<Port>      24 bytes
  ─────────────────────────
  計: 56 bytes + ヒープ確保
```

### 新方式（埋め込み）

```
FilterNode:
  vtable ptr         8 bytes
  ImagePipe output_ ~40 bytes (埋め込み)
  Pipe* input_       8 bytes
  ─────────────────────────
  計: ~56 bytes（ヒープ確保なし）
```

---

## 設計判断の根拠

### virtual 廃止（Pipe）

- Pipe インスタンス数: 10-20 程度
- vtable ポインタ節約: 40-160 bytes
- RTTI なし環境との整合性
- DataType による型識別で十分

### 埋め込み方式

- 動的確保のオーバーヘッド回避
- コンパイル時型安全性
- メモリレイアウトの予測可能性

### 1:1 接続原則

- Pipe は常に 1 Source : 1 Sink
- 複数接続はノード側で複数 Pipe を持つことで対応
- メモリ管理の単純化

---

## 移行計画

1. **Phase 1**: Pipe 構造体と基本 API を実装
2. **Phase 2**: FilterNode 等の基本ノードを移行
3. **Phase 3**: CompositeNode / DistributorNode を移行
4. **Phase 4**: 既存テストを更新・動作確認

---

## 関連ドキュメント

- FlexLib/docs/pipeline_design.md - パイプライン設計
- FlexLib/docs/CODING_STYLE.md - コーディング規約

## 更新日

2026-01-13
