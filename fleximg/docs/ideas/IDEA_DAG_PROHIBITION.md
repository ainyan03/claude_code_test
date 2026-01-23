# DAG構造禁止の方針

## 決定日
2026-01-13

## 概要
fleximg のノードグラフは**木構造（Tree）に制限**し、DAG（有向非巡回グラフ）を禁止する。

## 背景

### 問題
DAG構造（同一ノードの出力を複数の下流ノードが共有）を許可すると:
- 共有ノードが複数回評価される（重複計算）
- キャッシュ機構を導入すると複雑性とメモリ使用量が増加
- 評価順序の予測が困難になる

### 検討した選択肢

| 選択肢 | 参考システム | 評価 |
|--------|--------------|------|
| DAG + キャッシュ | GEGL | 複雑性が高い |
| DAG + 再計算許容 | Halide | メモリ効率良いが予測困難 |
| **木構造に制限** | Skia | シンプル、予測可能 |

### 決定理由
1. **組込み用途**: メモリ・計算リソースの予測可能性が重要
2. **回避策が存在**: SourceNode 複製で実質的な共有が可能
3. **設計の単純化**: キャッシュ機構が不要
4. **将来拡張への影響なし**: 動画処理（映像/音声分離）はマルチポートで対応可能

## 仕様

### 制約
- 1つの出力ポートは最大1つの入力ポートにのみ接続可能
- 1つの入力ポートは最大1つの出力ポートからのみ接続可能（従来通り）

### 許可される構造
```
SourceNode → AffineNode → RendererNode → SinkNode  (線形)

SourceNode1 → AffineNode ─┐
                          ├→ CompositeNode → ...   (複数入力)
SourceNode2 → Brightness ─┘

SourceNode → Renderer → DistributorNode ─┬→ Sink1
                                         └→ Sink2  (複数出力)
```

### 禁止される構造
```
              ┌→ Brightness ─┐
SourceNode → AffineNode      ├→ CompositeNode  (DAG: AffineNodeを共有)
              └→ Grayscale ──┘
```

### 回避方法
```cpp
// NG: DAG構造
SourceNode src(view, ox, oy);
src >> affine;
affine >> brightness >> composite;
affine >> grayscale;  // 同一affineに2つ目の下流 → エラー
grayscale.connectTo(composite, 1);

// OK: SourceNode複製
SourceNode src1(view, ox, oy);  // 同じ ViewPort を参照
SourceNode src2(view, ox, oy);  // コピーではなく参照なのでコスト低
AffineNode affine1, affine2;    // 別インスタンス

src1 >> affine1 >> brightness >> composite;
src2 >> affine2 >> grayscale;
grayscale.connectTo(composite, 1);
```

## 実装

### 検出方法
`Port::connect()` で接続時に検証:

```cpp
bool Port::connect(Port& other) {
    // 既存チェック...

    // DAG禁止: この出力ポートが既に接続済みならエラー
    if (this->connected != nullptr) {
        return false;
    }

    // 接続処理...
}
```

### 影響範囲
- `port.h`: connect() メソッドに検証追加
- 既存の正常なパイプラインに影響なし

## 将来拡張との関係

### 動画処理への拡張
DAG禁止は動画処理（映像/音声分離・合成）への拡張を妨げない。

映像/音声分離は「マルチポート出力」であり、DAGとは異なる:
- Demuxer: 1入力・2出力（映像ポート、音声ポート）
- 各出力は別々の下流に接続される（共有ではない）

必要な拡張:
- データ型の汎用化（ImageBuffer → MediaBuffer）
- 型付きポート（映像/音声の不正接続検出）
- 同期機構（タイムスタンプ管理）

## 参考資料

- 類似システム比較: GEGL, Halide, libvips, Skia
