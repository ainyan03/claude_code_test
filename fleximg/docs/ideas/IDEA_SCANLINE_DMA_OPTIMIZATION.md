# スキャンライン・DMA最適化戦略

## 作成日
2026-01-14

## 概要

LovyanGFX/M5GFXの開発経験から得た知見を基に、fleximgの最適化戦略を整理する。
「制約を設けることで最適化を実現する」アプローチを採用する。

---

## 背景: LovyanGFXの経験

### LovyanGFXの戦略
- 「フォーマット変換とアフィン変換を同時に行うテンプレート関数」を作成
- この関数ポインタを出力先（LCD）に渡す
- 出力先が関数を呼び、目的のアドレスに直接書き込む

### 利点
- **ゼロコピー**: 出力先に直接書き込み可能
- **DMA対応**: 条件が合えばDMA転送可能
- **高速**: ループ内の条件分岐を排除

### 課題（新ライブラリで解決したい）
- **組み合わせ爆発**: 入力フォーマット × 出力フォーマット × 変換種別 = O(N×M×K)
- ピクセルフォーマット数が増加中で管理困難
- ユーザー独自のピクセルフォーマットに対応できない
- 変換種別がアフィン以外に拡張しにくい

---

## fleximg/FlexLibでの設計方針

### 基本コンセプト
- 処理を行程（ノード）単位で分離
- 組み合わせは O(N+M) で済む
- ユーザー拡張が容易

### 課題
- 中間バッファの多発
- 出力先への直接書き込みができない
- DMA活用が困難

---

## 解決策: 制約による最適化

### 採用する制約

#### 1. スキャンライン制約
- 出力は基本的に上から下へのスキャンライン順
- タイル戦略は諦める（または限定的に使用）

#### 2. ランダムアクセス処理はソース直結のみ
- アフィン変換などランダムアクセスが必要な処理は、ソース画像ノード直下でのみ実行
- パイプライン途中での中間バッファへのランダムアクセスは禁止

#### 3. パラメータ伝播方式
- パイプライン途中に配置されたアフィンノードは、パラメータを上流に伝えるだけ
- 実際の処理はソース直結で一括実行
- 複数のアフィン変換は事前に行列合成して1回で処理

### 実現する挙動

**ユーザーから見たパイプライン:**
```
Source → Convert → Affine → ColorAdjust → Output
（自由に組み合わせ可能、制約を意識しない）
```

**内部での実際の処理:**
```
Source → Affine実行 → Convert → ColorAdjust → Output
         ↑パラメータが上流に伝播され、ここで一括実行
```

---

## 2段階Prepare方式

### 概要
prepare段階を2パスに分け、双方向の情報収集を行う。

### pushPrepare（下流へ）
最も下流のノードが要求する情報を上流に伝播する。

```
LCD → Affine → Convert → Source
「RGB565で、0x1000に書いて」
```

伝播する情報:
- 要求ピクセルフォーマット
- 「ここに書いて欲しい」出力バッファアドレス
- 要求領域

### pullPrepare（上流へ）
各ノードの能力と状況を下流に伝播する。

```
Source → Convert → Affine → LCD
「RGB332持ってる、アフィン適用可能、直接書き込みは条件次第」
```

伝播する情報:
- 提供可能なピクセルフォーマット
- アフィンパラメータ（伝播）
- 直接参照可能か
- DMA可能バッファか

### 具体例

#### 例1: Source(RGB565) → LCD(RGB565)
```
pushPrepare: LCD「RGB565、0x1000に書いて」→ Source
pullPrepare: Source「RGB565、0x2000にある」→ LCD
結果: フォーマット同一、DMA可能
```

#### 例2: Source(RGB565) → Affine → LCD(RGB565)
```
pushPrepare: LCD「RGB565、0x1000に書いて」→ Affine → Source
pullPrepare: Source「RGB565、アフィン適用可能」→ Affine（パラメータ渡す）→ LCD
結果: Sourceが0x1000に直接アフィン出力
```

#### 例3: Source(RGB332) → Convert → Affine → LCD(RGB565)
```
pushPrepare: LCD「RGB565、0x1000に書いて」→ Affine → Convert → Source
pullPrepare: Source「RGB332」→ Convert「変換必要」→ Affine「パラメータ伝播」→ LCD
結果: 変換必要なので中間バッファ経由、直接書き込み不可
```

---

## DMA転送の実現

### DMA可能条件
- フォーマットが同一（変換不要）
- 連続メモリ領域
- 変換処理なし（またはパラメータ伝播後に変換なしになる）

### 追加仕様
- ストライド≠出力幅の場合でも、DMAディスクリプタチェーンでY方向に複数DMAを連続指示可能
- DMA発行後、CPUは転送完了を待たずに次の処理へ進める

### 実現方針

**prepare段階でDMA可否を判定:**
```
// pushPrepare + pullPrepare 完了後
if (フォーマット同一 && 連続メモリ && 変換なし) {
    実行計画 = DMAパス;
}
```

**process段階:**
```
LCD.process()
  → ソースからポインタ取得
  → DMA発行して即リターン
```

### バッファ保護

**結論: ユーザー責任でOK**

理由:
- DMAが使える = 変換なし = 中間バッファなし = ソースバッファを直接使用
- ソースバッファ = ユーザー提供のメモリ
- LovyanGFXと同様、ユーザー責任で転送中バッファを破壊しないよう管理

---

## JPEGなどストリーミングソースの扱い

- JPEGはMCU（8x8/16x16ブロック）単位でデコードされる
- 必要なMCU群をデコード → その範囲でアフィン変換 → 次のMCU群...
- 全域保持が必要な場合は「バッファノード」を新設してソースとして振る舞わせる

---

## 必要な仕組み（実装項目）

1. **pushPrepare/pullPrepareプロトコル**
   - 下流→上流、上流→下流の情報伝播経路
   - 各ノードが伝播・収集する情報の定義

2. **ノードの能力宣言**
   - 「ランダムアクセス処理を受け入れ可能」（ソース系ノード）
   - 「スキャンライン処理のみ」（変換系ノード）
   - 「DMA可能バッファを提供可能」

3. **パラメータ伝播機構**
   - アフィンパラメータを上流に渡す仕組み
   - 複数アフィンの行列合成

4. **バッファノード**（必要時）
   - ストリーミングソースを全域保持してソースとして振る舞う

5. **出力先情報の伝播**
   - 下流側から「ここに書いてほしい」というバッファ情報を上流に渡す

---

## 実装計画

### Phase 1: アフィン伝播の最適化（✅ 実装完了 v2.30.0）

> **実装完了**: v2.30.0 で SourceNode へのアフィン伝播と、スキャンライン必須仕様を実装。
>
> 主な成果:
> - PrepareRequest 構造体でアフィン行列を上流に伝播
> - SourceNode が逆行列を事前計算し、pullProcess でDDA実行
> - スキャンライン必須制約により height=1 固定、DDA最適化が可能に
> - 有効ピクセル範囲のみのバッファを返却（範囲外の0データを下流に送らない）

RendererNodeより上流側のみの変更で、アフィン最適化を実現する。

**対象範囲:**
```
Source → Affine → RendererNode → Sink
  ↑_______↑
  この範囲のみ変更
```

**変更内容:**

1. **PrepareRequest構造体（最小版）**

```cpp
struct PrepareRequest {
    // 従来のスクリーン情報
    int16_t width;
    int16_t height;
    Point origin;

    // アフィン伝播用
    AffineMatrix affineMatrix;
    bool hasAffine = false;
};
```

2. **AffineNode.pullPrepare**

```cpp
bool pullPrepare(PrepareRequest& req) {
    if (req.hasAffine) {
        req.affineMatrix = matrix_ * req.affineMatrix;
    } else {
        req.affineMatrix = matrix_;
        req.hasAffine = true;
    }
    return upstream->pullPrepare(req);
}
```

3. **SourceNode.pullPrepare**

```cpp
bool pullPrepare(PrepareRequest& req) {
    if (req.hasAffine) {
        invMatrix_ = inverseFixed16(req.affineMatrix);
        txFixed8_ = float_to_fixed8(req.affineMatrix.tx);
        tyFixed8_ = float_to_fixed8(req.affineMatrix.ty);
        hasAffine_ = true;
    } else {
        hasAffine_ = false;
    }
    return true;
}
```

4. **SourceNode.pullProcess**
   - hasAffine_がtrueならDDA実行
   - falseなら従来通りサブビュー参照

5. **AffineNode.pullProcess**
   - 入力をそのまま返す（パススルー）

**期待される効果:**
- AffineNodeが約977行→50行程度に削減
- 中間バッファ削減（Affineでのバッファ確保が不要に）
- 複数Affineの行列合成による効率化

**影響を受けないもの:**
- Sink/LCD側のノード（変更なし）
- pushPrepare/pushProcess（変更なし）

---

### Phase 1 実装ステップ

#### Step 1: PrepareRequest構造体の追加
- ファイル: `core/render_types.h`
- 内容: 最小版の構造体定義

#### Step 2: Node基底クラスの変更
- ファイル: `core/node.h`
- 内容: pullPrepare(PrepareRequest&) オーバーロード追加

```cpp
// 新版（追加）
virtual bool pullPrepare(PrepareRequest& request) {
    Node* upstream = upstreamNode(0);
    return upstream ? upstream->pullPrepare(request) : true;
}
```

#### Step 3: RendererNodeの変更
- ファイル: `nodes/renderer_node.h`
- 内容: execPrepareで新旧両方を呼ぶ

```cpp
PrepareStatus execPrepare() {
    RenderRequest screenInfo = createScreenRequest();

    // 旧版（既存ノード用）
    upstream->pullPrepare(screenInfo);

    // 新版（PrepareRequest対応ノード用）
    PrepareRequest prepReq;
    prepReq.width = screenInfo.width;
    prepReq.height = screenInfo.height;
    prepReq.origin = screenInfo.origin;
    prepReq.hasAffine = false;
    upstream->pullPrepare(prepReq);

    downstream->pushPrepare(screenInfo);
    return PrepareStatus::Prepared;
}
```

#### Step 4: SourceNodeのアフィン対応
- ファイル: `nodes/source_node.h`
- 内容:
  - DDA関連コード（applyAffine, copyRowDDA）をAffineNodeから移動
  - pullPrepare(PrepareRequest&) オーバーライド
  - pullProcess でアフィン実行分岐

#### Step 5: AffineNodeの簡素化
- ファイル: `nodes/affine_node.h`
- 内容:
  - pullPrepare(PrepareRequest&) で行列合成・伝播
  - pullProcess はパススルー
  - 不要コード削除（AABB分割、DDA実装、台形フィットなど）

#### Step 6: テスト・動作確認
- 既存テストが通ることを確認
- 新旧両方のパスで動作確認

#### Step 7: 旧版の削除（後日）
- execPrepareから旧pullPrepare呼び出しを削除
- 必要に応じて旧pullPrepareメソッド自体を削除

---

### Phase 2: 下流要求の伝播とDMA対応（将来）

Phase 1完了後、必要に応じて実装。

**検討事項:**
- 発火点（RendererNode）の汎用化
- 下流からの要求情報（フォーマット、出力バッファ）の上流伝播
- DMA可否判定とDMAパス実行
- 3フェーズprepare or 戻り値による情報返却

**影響範囲が大きいため、Phase 1の成果を確認してから着手**

---

## 関連ドキュメント

- [IDEA_PIPELINE_V2.md](IDEA_PIPELINE_V2.md) - パイプラインV2設計（Executor/Pipe中心）
- [IDEA_PIPE_EMBEDDED_DESIGN.md](IDEA_PIPE_EMBEDDED_DESIGN.md) - Pipe埋め込み設計
- [IDEA_FORMAT_NEGOTIATION.md](IDEA_FORMAT_NEGOTIATION.md) - フォーマット交渉

---

## 更新履歴

- 2026-01-14: 初版作成
- 2026-01-14: Phase 1/Phase 2の実装計画を追加
- 2026-01-14: Phase 1の詳細実装ステップを追加
- 2026-01-14: Phase 1 実装完了（v2.30.0）、スキャンライン必須仕様を決定
