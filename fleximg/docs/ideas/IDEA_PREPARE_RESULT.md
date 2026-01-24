# PrepareResult - Prepare統一改修計画

## 概要

pushPrepare/pullPrepare両方の戻り値をPrepareResultに統一し、双方向の情報収集を可能にする。
**末端ノード（SinkNode/SourceNode）が累積行列を受け取りAABB計算を担当**する設計。

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

### 問題4: エラーハンドリングの統一

現状、pushPrepare/pullPrepareはboolを返すが、PipelineStatusで統一することで
より詳細なエラー情報を伝播できる。

## 設計

### 設計方針: 末端でAABB計算

**AffineNodeが行列を伝播し、末端がAABB計算する理由:**

1. **精度**: 複数のAffineNodeがある場合、各段階でAABB計算すると誤差が累積。末端で一度だけ計算するほうが正確
2. **責務の明確化**: AffineNodeは行列の合成・伝播のみ、末端ノードがAABB計算を担当
3. **既存の仕組みとの整合性**: PrepareRequestには既にアフィン行列伝播の仕組みがある（pullPrepare側のaffineMatrix、pushPrepare側のpushAffineMatrix）
4. **分岐対応**: DistributorNodeで分岐しても、各Sinkが個別にAABB計算し、DistributorNodeが和集合を返す

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

    // === AABBバウンディングボックス（処理すべき範囲） ===
    int16_t width = 0;
    int16_t height = 0;
    Point origin;

    // === フォーマット情報 ===
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;

    // 便利メソッド
    bool ok() const { return status == PipelineStatus::Success; }
};
```

### Prepareメソッドの変更

```cpp
// 現状
virtual bool pushPrepare(const PrepareRequest& request) final;
virtual bool pullPrepare(const PrepareRequest& request) final;

// 変更後
virtual PrepareResult pushPrepare(const PrepareRequest& request) final;
virtual PrepareResult pullPrepare(const PrepareRequest& request) final;
```

## 双方向の情報収集

### pushPrepare: 下流情報収集（Renderer → Sink）

```
pushPrepare (Renderer → Sink)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━▶
PrepareRequest（pushAffineMatrixを累積）

Renderer ──▶ Affine ──▶ Distributor ──▶ Sink1
                              │
                              └──▶ Sink2

◀━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PrepareResult (Sink → Renderer)
  末端が累積行列でAABB計算、中間ノードはパススルー
```

| ノード | PrepareRequest処理 | PrepareResult応答 |
|--------|-------------------|------------------|
| AffineNode | pushAffineMatrixに自身の行列を合成 | パススルー |
| DistributorNode | 各分岐先へ伝播 | 各分岐先AABBの和集合 |
| SinkNode | 累積行列を受け取る | 累積行列と自身のサイズからAABB計算 |
| RendererNode | 問い合わせ発行 | 受け取ったAABBでvirtualScreen設定 |

### pullPrepare: 上流情報収集（Renderer → Source）

```
pullPrepare (Renderer → Source)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━▶
PrepareRequest（affineMatrixを累積）

Renderer ──▶ Affine ──▶ Composite ──▶ Source1
                              │
                              └──▶ Source2

◀━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
PrepareResult (Source → Renderer)
  末端が累積行列でAABB計算、中間ノードはパススルー
```

| ノード | PrepareRequest処理 | PrepareResult応答 |
|--------|-------------------|------------------|
| AffineNode | affineMatrixに自身の行列を合成 | パススルー |
| CompositeNode | 各入力元へ伝播 | 各入力元AABBの和集合 |
| SourceNode | 累積行列を受け取る | 累積行列と自身のサイズからAABB計算 |
| RendererNode | 問い合わせ発行 | 上流の情報を把握（将来: 最適化に活用） |

## 各ノードの役割（統合）

| ノード | pushPrepare | pullPrepare |
|--------|-------------|-------------|
| RendererNode | 問い合わせ発行 | 問い合わせ発行 |
| SinkNode | 累積行列でAABB計算 | - |
| SourceNode | - | 累積行列でAABB計算 |
| AffineNode | 行列合成＋パススルー | 行列合成＋パススルー |
| DistributorNode | 出力先の和集合 | パススルー |
| CompositeNode | パススルー | 入力元の和集合 |
| FilterNode等 | パススルー | パススルー |

## 実装例

### pushPrepare側

#### AffineNode (pushPrepare)
```cpp
PrepareResult AffineNode::onPushPrepare(const PrepareRequest& request) {
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return {PipelineStatus::NoDownstream};
    }

    // 行列を累積して下流へ伝播
    PrepareRequest newRequest = request;
    if (newRequest.hasPushAffine) {
        newRequest.pushAffineMatrix = matrix_ * newRequest.pushAffineMatrix;
    } else {
        newRequest.pushAffineMatrix = matrix_;
        newRequest.hasPushAffine = true;
    }

    PrepareResult result = downstream->pushPrepare(newRequest);

    // 下流情報を保持（process時に使用）
    downstreamInfo_ = result;
    return result;  // パススルー
}
```

#### SinkNode (pushPrepare)
```cpp
PrepareResult SinkNode::onPushPrepare(const PrepareRequest& request) {
    PrepareResult result;
    result.status = PipelineStatus::Success;
    result.preferredFormat = target_.formatID;

    if (request.hasPushAffine) {
        // 累積行列から必要な入力範囲を計算
        // 自身のサイズ(target_)に逆行列を適用
        AABB inputBounds = calcInverseAffineAABB(
            {target_.width, target_.height, {originX_, originY_}},
            request.pushAffineMatrix
        );
        result.width = inputBounds.width;
        result.height = inputBounds.height;
        result.origin = inputBounds.origin;
    } else {
        // アフィンなし: 自身のサイズをそのまま返す
        result.width = target_.width;
        result.height = target_.height;
        result.origin = {originX_, originY_};
    }

    return result;
}
```

#### DistributorNode (pushPrepare)
```cpp
PrepareResult DistributorNode::onPushPrepare(const PrepareRequest& request) {
    PrepareResult merged;
    merged.status = PipelineStatus::NoDownstream;

    for (int i = 0; i < outputCount(); ++i) {
        Node* downstream = downstreamNode(i);
        if (!downstream) continue;

        PrepareResult result = downstream->pushPrepare(request);
        if (!result.ok()) continue;

        downstreamInfos_[i] = result;

        if (merged.status != PipelineStatus::Success) {
            merged = result;
        } else {
            merged = mergeAABB(merged, result);
        }
    }

    return merged;
}
```

### pullPrepare側

#### AffineNode (pullPrepare)
```cpp
PrepareResult AffineNode::onPullPrepare(const PrepareRequest& request) {
    Node* upstream = upstreamNode(0);
    if (!upstream) {
        return {PipelineStatus::NoUpstream};
    }

    // 行列を累積して上流へ伝播（既存の仕組み）
    PrepareRequest newRequest = request;
    if (newRequest.hasAffine) {
        newRequest.affineMatrix = matrix_ * newRequest.affineMatrix;
    } else {
        newRequest.affineMatrix = matrix_;
        newRequest.hasAffine = true;
    }

    PrepareResult result = upstream->pullPrepare(newRequest);

    // 上流情報を保持
    upstreamInfo_ = result;
    return result;  // パススルー
}
```

#### SourceNode (pullPrepare)
```cpp
PrepareResult SourceNode::onPullPrepare(const PrepareRequest& request) {
    PrepareResult result;
    result.status = PipelineStatus::Success;
    result.preferredFormat = source_.formatID;

    if (request.hasAffine) {
        // 累積行列から出力範囲を計算
        // 自身のサイズ(source_)に順行列を適用
        AABB outputBounds = calcAffineAABB(
            {source_.width, source_.height, {0, 0}},
            request.affineMatrix
        );
        result.width = outputBounds.width;
        result.height = outputBounds.height;
        result.origin = outputBounds.origin;

        // 逆行列を事前計算（process時のDDA用）
        invMatrix_ = inverseFixed16(request.affineMatrix);
        hasAffine_ = true;
    } else {
        // アフィンなし: 自身のサイズをそのまま返す
        result.width = source_.width;
        result.height = source_.height;
        result.origin = {0, 0};
        hasAffine_ = false;
    }

    return result;
}
```

#### CompositeNode (pullPrepare)
```cpp
PrepareResult CompositeNode::onPullPrepare(const PrepareRequest& request) {
    PrepareResult merged;
    merged.status = PipelineStatus::NoUpstream;

    for (int i = 0; i < inputCount(); ++i) {
        Node* upstream = upstreamNode(i);
        if (!upstream) continue;

        PrepareResult result = upstream->pullPrepare(request);
        if (!result.ok()) continue;

        upstreamInfos_[i] = result;

        if (merged.status != PipelineStatus::Success) {
            merged = result;
        } else {
            merged = mergeAABB(merged, result);
        }
    }

    return merged;
}
```

### RendererNode
```cpp
PipelineStatus RendererNode::execPrepare() {
    // === Step 1: 下流情報収集 ===
    Node* downstream = downstreamNode(0);
    if (!downstream) {
        return PipelineStatus::NoDownstream;
    }

    PrepareRequest pushRequest;
    pushRequest.hasPushAffine = false;
    pushRequest.allocator = pipelineAllocator_;

    PrepareResult downstreamInfo = downstream->pushPrepare(pushRequest);
    if (!downstreamInfo.ok()) {
        return downstreamInfo.status;
    }

    // virtualScreenを自動設定
    virtualWidth_ = downstreamInfo.width;
    virtualHeight_ = downstreamInfo.height;

    // === Step 2: 上流情報収集 ===
    Node* upstream = upstreamNode(0);
    if (!upstream) {
        return PipelineStatus::NoUpstream;
    }

    PrepareRequest pullRequest;
    pullRequest.width = virtualWidth_;
    pullRequest.height = virtualHeight_;
    pullRequest.hasAffine = false;
    pullRequest.allocator = pipelineAllocator_;

    PrepareResult upstreamInfo = upstream->pullPrepare(pullRequest);
    if (!upstreamInfo.ok()) {
        return upstreamInfo.status;
    }

    // 上流情報を活用（将来: フォーマット決定等）

    return PipelineStatus::Success;
}
```

## 具体例

### 例1: Renderer → Affine(2x拡大) → Sink(320x240)

```
pushPrepare:
1. Renderer → Affine: PrepareRequest{hasPushAffine=false}
2. Affine → Sink: PrepareRequest{pushAffineMatrix=2x, hasPushAffine=true}
3. Sink: 320x240に逆行列(0.5x)を適用 → 必要な入力範囲 = 160x120
4. Sink → Affine: PrepareResult{width=160, height=120}
5. Affine → Renderer: PrepareResult{width=160, height=120}（パススルー）
6. Renderer: virtualScreenを160x120に設定
```

### 例2: Renderer → Affine(2x) → Distributor → Sink1(320x240), Sink2(640x480)

```
pushPrepare:
1. Renderer → Affine → Distributor: PrepareRequest{pushAffineMatrix=2x}
2. Distributor → Sink1: PrepareRequest{pushAffineMatrix=2x}
   Sink1: 320x240 × 逆行列 → 160x120
3. Distributor → Sink2: PrepareRequest{pushAffineMatrix=2x}
   Sink2: 640x480 × 逆行列 → 320x240
4. Distributor: 和集合(160x120, 320x240) → 320x240
5. Affine → Renderer: PrepareResult{width=320, height=240}
6. Renderer: virtualScreenを320x240に設定
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
- [x] PrepareResult構造体の定義（render_types.h）
- [x] pushPrepare/pullPrepareの戻り値変更（node.h）

### Phase 2: 末端ノード対応
- [x] AABB計算ヘルパー関数（render_types.h）
  - `calcAffineAABB`: 順変換でAABB計算
  - `calcInverseAffineAABB`: 逆変換でAABB計算
- [x] SinkNode: 累積行列でAABB計算、PrepareResult返却
- [x] SourceNode: 累積行列でAABB計算、PrepareResult返却
- [x] DistributorNode: 出力先の和集合計算

### Phase 3: 中間ノード対応
- [x] AffineNode: 行列合成＋パススルー（既存実装で対応済み）
- [x] CompositeNode: 入力元の和集合計算
- [x] MatteNode: 入力元の和集合計算
- [x] VerticalBlurNode等のFilterNode: パススルー（既存実装で対応済み）

### Phase 4: RendererNode対応
- [x] 下流情報の受け取りとvirtualScreen自動設定
- [x] 上流情報の受け取り
- [x] setVirtualScreen()をオプション化（未設定時に自動設定）

### Phase 5: フォーマット交渉
- [x] PrepareRequestにpreferredFormatを追加
- [x] RendererNode: 下流から上流へフォーマット情報を伝播
- [x] SourceNode: preferredFormatを受け取り保存（将来の最適化基盤）

## 考慮事項

- **後方互換性**: 既存のexec()呼び出しは動作を維持
- **オプショナル**: setVirtualScreen()は明示的設定として残す（上限指定等）
- **パフォーマンス**: prepare時の情報収集はexec()あたり1回のみ
- **責務の明確化**: AffineNodeは行列伝播のみ、末端ノードがAABB計算
- **精度**: 複数アフィンは末端で一度だけ計算 → 誤差累積なし
- **対称性**: push/pull両方向で同じパターン → 理解しやすい設計
- **メンバ統合**: `prepareState_`/`prepareResult_`はpush/pull共通（ノードはどちらか一方でのみ使用されるため）
