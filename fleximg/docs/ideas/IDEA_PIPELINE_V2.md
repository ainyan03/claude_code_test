# パイプライン V2 設計案

## 作成日
2026-01-13

## 概要

fleximg のパイプライン構造を再設計する。FlexLib の Executor ポーリング方式をベースに、Pipe 中心の実行モデルを導入する。

### 方針
- **既存 fleximg を段階的に更新**（新規プロジェクトではない）
- **Executor ポーリング方式**をベース
- **Pipe 中心の実行モデル**を新たに導入

---

## 現行設計との比較

| 項目 | fleximg（現行） | FlexLib | V2（本設計） |
|------|----------------|---------|-------------|
| 実行制御 | RendererNode が発火点 | Executor → Node | Executor → Pipe |
| 状態管理 | なし | Node が状態保持 | Pipe が状態保持 |
| 接続 | Port で管理 | Pipe 1:1 接続 | Pipe 1:1 接続 |

---

## 設計目標

1. **Pipe 中心の実行モデル**: Executor は Pipe に対して execute() を呼び出す
2. **スタック安全**: 再帰呼び出しを避け、キュー駆動で処理
3. **対称構造**: データ要求と提供を対称的に扱う
4. **組込み環境対応**: virtual/テンプレートを最小限に、動的確保を避ける
5. **シンプルな状態遷移**: 3状態のみ（Idle / Request / Supply）

---

## Pipe 中心の実行モデル

### 基本概念

```
[従来: Node 中心]
Executor → Node.execute()
           Node が状態を持つ

[V2: Pipe 中心]
Executor → Pipe.execute()
           Pipe が状態を持ち自律動作
           Node は受動的な処理ユニット
```

### 処理フロー

```
1. 発火点 Pipe に Request 状態を設定

2. Executor が Pipe.execute() を呼ぶ
   └─ Pipe: 「自分は Request 状態だ」
      └─ 上流 Source の on_request() を呼ぶ

3. Source の応答
   ├─ データあり → Pipe.supply(data) → Supply 状態へ
   └─ 上流が必要 → 上流 Pipe.request() → その Pipe が Request 状態へ

4. Executor は状態変化した Pipe を検出して実行を継続
```

---

## Pipe の状態遷移

### 3状態モデル

```cpp
enum class PipeState : uint8_t {
    Idle,       // 待機中
    Request,    // データ要求（上流への要求待ち）
    Supply,     // データ提供（下流への提供待ち）
};
```

### 状態遷移図

```
      request()              supply(data)
Idle ─────────→ Request    Idle ─────────→ Supply
                  │                          │
                  │ execute()                │ execute()
                  │ (上流に要求)              │ (下流に提供)
                  ↓                          ↓
                Idle                        Idle
```

- execute() 呼び出し後は即座に Idle に戻る
- 「処理済み」フラグは不要

---

## データの対称構造

Pipe を流れるデータは上流向けと下流向けで対称的な構造を持つ。

```
上流向け（Request）           下流向け（Supply）
─────────────────           ─────────────────
「この領域が欲しい」          「このデータをどうぞ」
    RequestInfo        ←→       SupplyData
```

### Release フローの廃止

- 所有権移動（ムーブセマンティクス）によりメモリ解放は自動
- 明示的な Release 状態は不要
- Request / Supply のみの対称構造がシンプル

---

## 再帰呼び出しの禁止

### 原則

- Pipe は execute() 以外の関数からは**状態変更のみ**
- 状態変更時に Executor に通知（再帰呼び出しなし）
- 実際の処理（Source/Sink への呼び出し）は execute() 内でのみ

### フロー例

```
[ステップ1] 発火
SinkNode: pipe->request(info)
  └─ Pipe: state_ = Request
     └─ executor_->mark_pending(id_)  // 通知のみ、再帰なし

[ステップ2] Executor ループ
while (pending_mask_) {
    int id = get_next_pending();
    clear_pending(id);
    pipes_[id]->execute();
}

[ステップ3] Pipe.execute()
PipeState s = state_;
state_ = Idle;  // 即座に Idle へ
switch (s) {
    case Request: source_->on_request(this); break;
    case Supply:  sink_->on_supply(this);    break;
}

[ステップ4] Source/Sink の処理
void Source::on_request(Pipe* downstream) {
    if (has_data()) {
        downstream->supply(data);  // state=Supply, mark_pending
    } else {
        upstream_pipe_->request(info);  // state=Request, mark_pending
    }
    // ここで return、再帰なし
}
```

---

## Executor の設計

### ビットマスク方式

RTOS なし環境でも動作するよう、動的確保を避けた設計。

```cpp
class Executor {
    static constexpr int MAX_PIPES = 32;
    uint32_t pending_mask_ = 0;
    Pipe* pipes_[MAX_PIPES] = {};

public:
    void register_pipe(Pipe* pipe, int id) {
        pipes_[id] = pipe;
        pipe->set_executor(this, id);
    }

    void mark_pending(int id) {
        pending_mask_ |= (1u << id);
    }

    void run_until_idle() {
        while (pending_mask_) {
            int id = __builtin_ctz(pending_mask_);  // 最下位ビット
            pending_mask_ &= ~(1u << id);           // 先にクリア
            pipes_[id]->execute();                   // execute内で再度mark可能
        }
    }
};
```

### Pipe との連携

```cpp
struct Pipe {
    Executor* executor_ = nullptr;
    int id_ = -1;

    void set_executor(Executor* exec, int id) {
        executor_ = exec;
        id_ = id;
    }

    void notify_state_change() {
        if (executor_) {
            executor_->mark_pending(id_);
        }
    }
};
```

---

## Pipe の構造

### 基本構造

```cpp
struct Pipe {
    PipeState state_ = PipeState::Idle;
    Executor* executor_ = nullptr;
    int id_ = -1;

    ISource* source_ = nullptr;  // 上流ノード
    ISink* sink_ = nullptr;      // 下流ノード

    // 上流向けデータ（要求内容）
    // 下流向けデータ（提供内容）
    // → 型の扱いは未決定

    void request(/* 要求データ */);
    void supply(/* 提供データ */);
    void execute();
};
```

### データ型の扱い（検討中）

virtual やテンプレートを避けつつ、異なるデータ型を扱う方法を検討中。

#### 案: DataHeader + キャスト方式

```cpp
enum class DataType : uint8_t {
    Raw = 0,
    Image,
    Audio,
};

// 共通ヘッダ（先頭に配置）
struct DataHeader {
    DataType type;
};

// 画像用リクエスト
struct ImageRequest {
    DataHeader header;  // 必ず先頭
    int16_t x, y, width, height;
};

// 画像用データ
struct ImageData {
    DataHeader header;  // 必ず先頭
    void* pixels;
    int32_t stride;
    int16_t width, height;
    uint8_t format;
};

// Pipe（型に依存しない）
struct Pipe {
    DataHeader* request_;  // 上流向けデータ
    DataHeader* supply_;   // 下流向けデータ
    // ...
};
```

使用側でキャスト:
```cpp
void ImageSource::on_request(Pipe* pipe) {
    auto* req = static_cast<ImageRequest*>(pipe->request_);
    // ...
}
```

---

## 未決定事項

### 1. データの寿命管理

request() / supply() に渡すデータは誰が所有するか？

- **案A**: Pipe がコピーを保持
- **案B**: 呼び出し側が保持し続ける（Pipe はポインタのみ）
- **案C**: 所有権をムーブで移動

### 2. 型の扱い

virtual やテンプレートを避けつつ型安全性を確保する方法。

- DataHeader + キャスト方式の詳細設計
- standard-layout の保証方法
- 型チェックのタイミング（コンパイル時 vs 実行時）

### 3. Node のインターフェース

Source / Sink / Processor のインターフェース設計。

```cpp
class ISource {
public:
    virtual void on_request(Pipe* pipe) = 0;
};

class ISink {
public:
    virtual void on_supply(Pipe* pipe) = 0;
};
```

### 4. 複数入出力ノード

- Compositor（複数入力 → 1出力）
- Distributor（1入力 → 複数出力）

これらのノードが複数の Pipe をどう管理するか。

---

## 関連ドキュメント

- [IDEA_PIPE_EMBEDDED_DESIGN.md](IDEA_PIPE_EMBEDDED_DESIGN.md) - Pipe 埋め込み設計（参考）

---

## 更新履歴

- 2026-01-13: 初版作成（検討中）
