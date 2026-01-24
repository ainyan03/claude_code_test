# MatteNode / AlphaSplitNode

> **ステータス**: MatteNode ✅ 実装完了（2026-01-19）
>
> AlphaSplitNodeは将来検討。

## 概要

アルファマスクを使った画像合成と、チャンネル分離を行う専用ノード。

- **MatteNode**: 元画像1 + 元画像2 + アルファマスク → 合成画像（3入力→1出力）
- **AlphaSplitNode**: RGBA画像 → RGB画像 + Alpha画像（1入力→2出力）※将来検討

---

## MatteNode（3入力→1出力）

### 処理内容

```
Output = Image1 × Alpha + Image2 × (1 - Alpha)
```

映像業界で「マット合成」と呼ばれる標準的な画像処理。After Effectsの「Track Matte」やNukeの「Merge (matte)」に相当。

### インターフェース

```cpp
class MatteNode : public Node {
public:
    MatteNode();

    // 入力ポート:
    // - ポート0: 元画像1（前景、マスク白部分に表示）
    // - ポート1: 元画像2（背景、マスク黒部分に表示）
    // - ポート2: アルファマスク（Alpha8推奨）

    // 出力:
    // - Image1 × Alpha + Image2 × (1 - Alpha)

    // Node interface
    const char* name() const override { return "MatteNode"; }
    RenderResponse pullProcess(const RenderRequest& request) override;
};
```

### 入力の扱い

| 入力 | 役割 | 未接続・範囲外時 |
|------|------|-----------------|
| ポート0（元画像1） | 前景画像 | 透明の黒 (0,0,0,0) |
| ポート1（元画像2） | 背景画像 | 透明の黒 (0,0,0,0) |
| ポート2（アルファマスク） | 合成比率 | alpha=0（全面背景） |

**統一ルール**: 範囲外・未接続はすべて「透明の黒」として処理。特別な分岐ロジック不要。

### アルファマスクのフォーマット

- **推奨**: `Alpha8` フォーマット（1チャンネル8bit）
- **他フォーマット**: PixelFormat変換機構でアルファ抽出して対応
  - RGBA → Aチャンネル抽出
  - RGB → ルミナンス変換（将来検討）

### ユースケース

1. **マスク合成ワークフロー**
   - 写真素材（RGB）と別途作成したマスク画像を使って透過画像を生成
   - デザインツールで作成した素材の合成

2. **2画像のブレンド**
   ```
   元画像1: 晴れの風景
   元画像2: 雨の風景
   アルファマスク: グラデーション
   → 滑らかに遷移する合成画像
   ```

3. **部分的な画像差し替え**
   ```
   元画像1: 新しい顔
   元画像2: 元の写真
   アルファマスク: 顔領域のマスク
   → 顔だけ差し替えた画像
   ```

4. **元画像2を省略したマスク適用**
   ```
   元画像1: 任意の画像
   元画像2: （未接続 = 透明）
   アルファマスク: マスク画像
   → 元画像1にマスクを適用した透過画像
   ```

### 処理フロー（pull型）

```
1. 3つの入力の要求領域（intersection）を計算
2. 各入力からピクセルデータを取得
   - 元画像1: RGBA8_Straight に変換
   - 元画像2: RGBA8_Straight に変換
   - アルファマスク: Alpha8 に変換
3. ピクセル単位で合成
   for each pixel:
     alpha = mask[i]
     out.r = img1.r * alpha + img2.r * (255 - alpha)
     out.g = img1.g * alpha + img2.g * (255 - alpha)
     out.b = img1.b * alpha + img2.b * (255 - alpha)
     out.a = img1.a * alpha + img2.a * (255 - alpha)
     （各成分を255で除算）
4. 結果を出力
```

### NodeType定義

`perf_metrics.h` に追加:

```cpp
namespace NodeType {
    // ... 既存 ...
    constexpr int Matte = 13;
    constexpr int Count = 14;
}
```

### 実装順序

1. NodeType定義追加（perf_metrics.h）
2. MatteNodeクラス作成（matte_node.h）
3. pull型パイプライン実装
4. ユニットテスト作成
5. WebUIバインディング追加
6. WebUI コンポーネント作成

---

## AlphaSplitNode（1入力→2出力）※将来検討

RGBA画像をRGB画像とAlpha画像に分離するノード。

### ユースケース

- RGBとAlphaを独立したパイプラインで処理してから再結合
- アルファチャンネルだけにブラーやエッジ処理を適用

### インターフェース（案）

```cpp
class AlphaSplitNode : public Node {
public:
    // 入力ポート:
    // - ポート0: RGBA入力

    // 出力ポート:
    // - ポート0: RGB出力（アルファ=255の不透明画像）
    // - ポート1: Alpha出力（Alpha8またはグレースケール）
};
```

### 備考

- DistributorNodeを参考に複数出力を実装
- MatteNodeの需要・動作確認後に検討

---

## 関連

- **CompositeNode**: 複数入力ノードの実装参考
- **DistributorNode**: 複数出力ノードの実装参考
- **AlphaNode**: 既存のアルファスケール調整ノード
- **Alpha8フォーマット**: pixel_format.h で定義済み
