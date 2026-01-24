# PrepareResult - 下流情報収集フレームワーク

## 概要

pushPrepareの戻り値を拡張し、下流ノードから上流（Renderer）へ情報を伝播する仕組みを導入する。
サイズ情報、アフィン変換、フォーマット希望などを統一的に扱う。

## 動機

### 問題1: 仮想スクリーンサイズの手動設定

現状、RendererNodeは仮想スクリーンサイズを手動設定する必要がある。

```cpp
renderer.setVirtualScreen(1920, 1080);
sink.setTarget(output320x240);  // 実際の出力は320x240
```

RendererNode(1920x1080)で処理を行うが、SinkNode(320x240)への出力時に大半のピクセルが無駄になる。

### 問題2: アフィン変換を考慮したサイズ計算

```
Renderer → Affine(2倍拡大) → Sink(320x240)
```

この場合、Rendererは160x120分だけ処理すればよいが、現状はその情報を自動取得できない。

### 問題3: フォーマット交渉との統合

IDEA_FORMAT_NEGOTIATION.mdで提案されている「下流からのフォーマット要求収集」と同じパターン。
統一的なフレームワークとして設計すべき。

## 設計

### PipelineStatus（実装済み）

```cpp
enum class PipelineStatus : int {
    Success = 0,
    CycleDetected = 1,
    NoUpstream = 2,
    NoDownstream = 3,
    InvalidConfig = 4,  // 将来追加
};
```

### PrepareResult（新規）

```cpp
struct PrepareResult {
    PipelineStatus status = PipelineStatus::Success;

    // === 末端情報（下流から収集） ===
    int16_t width = 0;
    int16_t height = 0;
    Point origin;

    // アフィン情報（末端からの累積）
    AffineMatrix affine;
    bool hasAffine = false;

    // フォーマット情報
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;

    // 便利メソッド
    bool ok() const { return status == PipelineStatus::Success; }
};
```

### pushPrepareの変更

```cpp
// 現状
virtual bool pushPrepare(const PrepareRequest& request) final;

// 変更後
virtual PrepareResult pushPrepare(const PrepareRequest& request) final;
```

## 各ノードの役割

| ノード | 要求の伝播 | 応答の変換 | 情報保持 |
|--------|-----------|-----------|---------|
| SinkNode | 末端（伝播なし） | 自身のサイズ・フォーマットを返す | - |
| AffineNode | そのまま下流へ | アフィンを累積して返す | `downstreamInfo_` |
| DistributorNode | 全出力先へ | 和集合を計算して返す | `downstreamInfos_[]` |
| RendererNode | 問い合わせ発行 | 応答を受けてサイズ決定 | `virtualWidth_`等 |

## 処理フロー

```
pushPrepare (Renderer → 末端)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━▶

Renderer ──▶ Affine ──▶ Distributor ──▶ Sink1
                              │
                              └──▶ Sink2

◀━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PrepareResult (末端 → Renderer)
```

### 詳細フロー

1. **RendererNode**: PrepareRequestを作成、下流にpushPrepare
2. **AffineNode**: 下流にそのまま伝播、応答のアフィンに自身を累積
3. **DistributorNode**: 全出力先に伝播、応答の和集合を計算
4. **SinkNode**: 自身のtargetサイズ・フォーマットを返す
5. **RendererNode**: 応答を受け取り、virtualScreenを自動設定

## 実装例

### SinkNode

```cpp
PrepareResult SinkNode::onPushPrepare(const PrepareRequest& request) {
    PrepareResult result;
    result.status = PipelineStatus::Success;
    result.width = target_.width;
    result.height = target_.height;
    result.origin = {originX_, originY_};
    result.preferredFormat = target_.formatID;
    // アフィンなし（末端）
    return result;
}
```

### AffineNode

```cpp
PrepareResult AffineNode::onPushPrepare(const PrepareRequest& request) {
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return {PipelineStatus::NoDownstream};
    }

    PrepareResult result = downstream->pushPrepare(request);
    if (!result.ok()) return result;

    // 自身のアフィンを累積
    if (result.hasAffine) {
        result.affine = result.affine * matrix_;
    } else {
        result.affine = matrix_;
        result.hasAffine = true;
    }

    // 自身で保持（後のprocess時に使用）
    downstreamInfo_ = result;

    return result;
}
```

### RendererNode

```cpp
PipelineStatus RendererNode::execPrepare() {
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return PipelineStatus::NoDownstream;
    }

    // 下流に問い合わせ
    PrepareRequest request;
    PrepareResult downstream_info = downstream->pushPrepare(request);

    if (!downstream_info.ok()) {
        return downstream_info.status;
    }

    // サイズ決定
    if (downstream_info.hasAffine) {
        // アフィン変換を考慮したサイズ計算
        auto bounds = calcRequiredBounds(downstream_info);
        virtualWidth_ = bounds.width;
        virtualHeight_ = bounds.height;
    } else {
        virtualWidth_ = downstream_info.width;
        virtualHeight_ = downstream_info.height;
    }

    // 以降、通常のpullPrepare処理...
}
```

## 関連機能

### 行単位の範囲打診（将来拡張）

CompositeNodeでの合成時、各入力の有効幅が行ごとに異なる場合のバッファ最適化。

```cpp
// pullProcess前に有効範囲を問い合わせ
RangeInfo pullQueryRange(const RenderRequest& request);

struct RangeInfo {
    int16_t startX;
    int16_t endX;
    bool valid;
};
```

PrepareResultとは別フェーズ（process時）だが、同様の「情報収集」パターン。

### フォーマット交渉との統合

PrepareResultにフォーマット情報を含めることで、IDEA_FORMAT_NEGOTIATION.mdの機能と統合。

- `preferredFormat`: 末端が希望するフォーマット
- 将来: `acceptableFormats[]` で複数候補対応

## 実装フェーズ

### Phase 1: 基盤整備
- [x] ExecResult → PipelineStatus リネーム
- [ ] PrepareResult構造体の定義
- [ ] pushPrepareの戻り値変更

### Phase 2: 末端ノード対応
- [ ] SinkNode: PrepareResult返却
- [ ] DistributorNode: 和集合計算

### Phase 3: 中間ノード対応
- [ ] AffineNode: アフィン累積
- [ ] 他のノード: パススルー実装

### Phase 4: RendererNode対応
- [ ] 下流情報の受け取り
- [ ] virtualScreen自動設定
- [ ] setVirtualScreen()をオプション化

### Phase 5: フォーマット交渉
- [ ] preferredFormatの活用
- [ ] 上流へのフォーマット決定伝播

## 考慮事項

- **後方互換性**: 既存のexec()呼び出しは動作を維持
- **オプショナル**: setVirtualScreen()は明示的設定として残す（上限指定等）
- **パフォーマンス**: prepare時の情報収集はexec()あたり1回のみ
