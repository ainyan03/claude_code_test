# ガウシアンブラー アップグレード計画

> **ステータス**: ✅ 実装完了（2026-01-18、v2.34.0）
>
> オプション1（複数パスボックスブラー）を採用し、HorizontalBlurNodeとVerticalBlurNodeに`passes`パラメータを追加しました。passes=3でガウシアン近似を実現。
>
> 関連: [CHANGELOG.md](../../CHANGELOG.md#2340---2026-01-18)

---

## パラメータ上限（2026-01-19更新）

| パラメータ | 上限 | 備考 |
|-----------|------|------|
| radius | 127 | 実用上十分、メモリ消費も許容範囲 |
| passes | 3 | ガウシアン近似に十分 |

**パイプライン方式による制約緩和**:
- 各パスが独立してブラー処理を行うため、passes数によるradius制限は不要
- 32bit演算の制約は単一パス分のみ考慮すればよい

---

## メモリ消費量（VerticalBlurNode）

垂直ブラーはパイプライン方式のスキャンラインキャッシュを使用します。

**計算式**: `passes × width × (radius × 8 + 20)` bytes
- 各ステージ: 行キャッシュ `(radius×2+1) × width × 4` + 列合計 `width × 16`

| radius | passes | width=640 | width=2048 |
|--------|--------|-----------|------------|
| 50 | 3 | 492 KB | 1.5 MB |
| 100 | 3 | 983 KB | 3.1 MB |
| 127 | 3 | 1.2 MB | 4.0 MB |

**組み込み環境（ESP32等）での目安**:
- QVGA (320幅): radius=50, passes=3 で約250KB
- VGA (640幅): radius=50, passes=3 で約500KB

水平ブラーは1行完結処理のため、メモリ消費は少ない（width×4 bytes程度）。

**パイプライン方式の利点**:
- 「3パス×1ノード」と「1パス×3ノード直列」が同等の結果を得られる
- 各パスで独立した境界処理が行われる

---

## 現状分析

### 現在の実装
- **HorizontalBlurNode / VerticalBlurNode**: 分離型ボックスブラー（パイプライン方式）
- ~~**BoxBlurNode**: 統合型ボックスブラー~~ → 廃止（2026-01-19）
- **アルゴリズム**: スライディングウィンドウ方式、O(width×height)でradius非依存
- **演算**: 32bit整数演算（uint32_t）、α加重平均
- **カーネルサイズ**: 2 * radius + 1

### 過去の試行
- **Stack Blur**: アルゴリズムに不備があったためBox Blurに戻した（2026-01-17）
- **BoxBlurNode**: HorizontalBlur + VerticalBlur で代替可能なため廃止（2026-01-19）

### 要件
- ガウシアンブラーまたは近い結果
- radius: 0-127（パイプライン方式により制約緩和）
- 32bit整数演算のみ使用
- スタックブラーは避ける

---

## 提案オプション

### オプション1: 複数パスボックスブラー（推奨）★★★★★

**原理**: 中心極限定理により、ボックスブラーを複数回適用するとガウシアン分布に収束

**実装**:
```
3パス適用: BoxBlur(r) → BoxBlur(r) → BoxBlur(r)
結果: ガウシアンに非常に近い滑らかなブラー
```

**メリット**:
- ✅ 既存のボックスブラーコードをそのまま利用
- ✅ 追加の計算コストは3倍だが、依然としてO(width×height)
- ✅ 64bit演算や複雑な除算不要
- ✅ 視覚的にガウシアンと見分けがつかないレベル
- ✅ 実装が簡単で安全

**デメリット**:
- ⚠️ 3倍の処理時間
- ⚠️ 完全なガウシアンではない（ただし実用上問題なし）

**radius変換式**:
```
ガウシアンのradius rを実現するためのボックスブラーradius:
r_box = floor(sqrt((12 * r^2) / n + 1) / 4)
n = パス数（3を推奨）

例:
- ガウシアンr=5 → ボックス3パス r=3.16 ≈ 3
- ガウシアンr=10 → ボックス3パス r=6.33 ≈ 6
- ガウシアンr=32 → ボックス3パス r=20.26 ≈ 20
```

---

### オプション2: テントブラー（三角分布）★★★★☆

**原理**: ボックスブラーを2回適用すると三角（テント）分布になる

**実装**:
```
2パス適用: BoxBlur(r) → BoxBlur(r)
結果: ガウシアンよりは劣るが、ボックスよりは滑らか
```

**メリット**:
- ✅ オプション1より高速（2パス）
- ✅ ボックスよりは滑らか
- ✅ 実装簡単

**デメリット**:
- ⚠️ ガウシアンほど滑らかではない
- ⚠️ 視覚的にやや角張った結果

---

### オプション3: 整数ガウシアンカーネル★★★☆☆

**原理**: ガウシアンカーネルを整数係数で近似し、シフト演算で正規化

**実装例（radius=3の場合）**:
```cpp
// 1D ガウシアンカーネル (σ≈1.0, radius=3)
// 係数合計 = 256 (2^8) でシフトによる正規化
int16_t kernel[7] = {6, 44, 117, 162, 117, 44, 6};

// 畳み込み
for (int i = 0; i < width; i++) {
    int32_t sum = 0;
    for (int k = -3; k <= 3; k++) {
        sum += pixel[i+k] * kernel[k+3];
    }
    result[i] = sum >> 8;  // 256で除算（シフト演算）
}
```

**メリット**:
- ✅ 真のガウシアン分布
- ✅ 16bit係数 + 32bit累積で十分
- ✅ シフト演算で正規化（除算不要）

**デメリット**:
- ⚠️ radius増加で計算量がO(width×height×radius)に
- ⚠️ radius=32では現実的でない（カーネルサイズ65）
- ⚠️ radiusごとにカーネル係数の事前計算が必要
- ⚠️ スライディングウィンドウほど効率的でない

**適用範囲**: radius ≤ 5程度に限定すれば実用的

---

### オプション4: 可変radiusガウシアン（整数最適化版）★★☆☆☆

**原理**: 整数ガウシアンカーネルを動的生成

**実装**:
```cpp
void generateGaussianKernel(int radius, float sigma, int16_t* kernel, int* sum) {
    *sum = 0;
    for (int x = -radius; x <= radius; x++) {
        float value = exp(-(x*x) / (2*sigma*sigma));
        kernel[x + radius] = (int16_t)(value * 256);
        *sum += kernel[x + radius];
    }
    // 正規化調整（合計が256になるよう微調整）
}
```

**メリット**:
- ✅ 任意のradiusに対応
- ✅ 真のガウシアン分布

**デメリット**:
- ⚠️ 浮動小数点演算（exp）が必要（準備段階のみ）
- ⚠️ radius大で計算量増
- ⚠️ 組み込み環境で浮動小数点が使えない場合は不可

---

## 推奨案: ハイブリッドアプローチ

**戦略**: radiusに応じて最適な手法を使い分ける

### 実装方針

```cpp
enum class BlurMode {
    Box,           // 従来のボックスブラー
    Gaussian,      // ガウシアンブラー（内部で最適手法を選択）
};

class GaussianBlurNode {
    // radius小（≤5）: 整数カーネルガウシアン
    // radius中-大（6-32）: 3パスボックスブラー近似
    void applyGaussianBlur(...) {
        if (radius_ <= 5) {
            applyKernelGaussian();
        } else {
            applyMultiPassBox(3);  // 3パスボックス
        }
    }
};
```

### メリット
- ✅ radius小: 真のガウシアンで高品質
- ✅ radius大: 効率的な近似で実用性
- ✅ すべてのradius範囲で64bit不要
- ✅ 除算はシフト演算で代替

---

## 最もシンプルな推奨実装: 3パスボックスブラー

**理由**:
1. 既存コードの再利用で実装コスト最小
2. 視覚的にガウシアンと区別困難
3. すべてのradius範囲で一貫した動作
4. デバッグ済みのボックスブラーを使うため安全

**実装手順**:
1. 既存の `HorizontalBlurNode` / `VerticalBlurNode` を3回適用
2. または新しい `GaussianBlurNode` を作成し、内部で3回呼び出し
3. radiusの調整式を適用

**コード例**:
```cpp
class GaussianBlurNode : public Node {
    // r_gaussian = 10 の場合
    // r_box = floor(sqrt((12 * 100) / 3 + 1) / 4) = 6
    int calculateBoxRadius(int gaussianRadius) {
        int r_sq = gaussianRadius * gaussianRadius;
        int temp = (12 * r_sq) / 3 + 1;  // n=3パス
        return (int)(sqrt(temp) / 4);
    }

    void process() {
        int boxRadius = calculateBoxRadius(radius_);
        HorizontalBlurNode h1, h2, h3;
        VerticalBlurNode v1, v2, v3;
        h1.setRadius(boxRadius);
        v1.setRadius(boxRadius);
        // ... 3回適用
    }
};
```

---

## 性能見積もり

### 3パスボックスブラー
- **計算量**: O(3 × width × height) = O(width × height)
- **メモリ**: 中間バッファ 2-3枚分
- **処理時間**: 単一ボックスの約3倍

### 整数ガウシアン（radius=5）
- **計算量**: O(width × height × 11) ≈ O(11 × width × height)
- **メモリ**: 中間バッファ 1-2枚分
- **処理時間**: radius小なら許容範囲

---

## 次のステップ

### Phase 1: プロトタイプ実装
1. 3パスボックスブラーの検証コード作成
2. テスト画像で視覚的品質確認
3. 性能測定

### Phase 2: 本実装
1. `GaussianBlurNode` クラス作成
2. 水平・垂直分離版の実装
3. テスト追加

### Phase 3: 最適化（オプション）
1. radius小の場合の整数カーネル実装
2. ハイブリッド切り替えロジック追加

---

## 判断基準

| 項目 | 3パスボックス | 整数カーネル | ハイブリッド |
|------|---------------|--------------|--------------|
| 実装難易度 | ★☆☆☆☆ | ★★★☆☆ | ★★★★☆ |
| 品質（小radius） | ★★★★☆ | ★★★★★ | ★★★★★ |
| 品質（大radius） | ★★★★★ | ★☆☆☆☆ | ★★★★★ |
| 性能（小radius） | ★★★☆☆ | ★★★★☆ | ★★★★☆ |
| 性能（大radius） | ★★★★★ | ★☆☆☆☆ | ★★★★★ |
| 保守性 | ★★★★★ | ★★★☆☆ | ★★★☆☆ |

**総合評価**: まず **3パスボックスブラー** で実装し、必要に応じてハイブリッド化

---

## 参考資料

- [Box Blur to Gaussian Approximation](http://www.peterkovesi.com/papers/FastGaussianSmoothing.pdf)
- [Efficient Gaussian Blur](http://blog.ivank.net/fastest-gaussian-blur.html)
- 中心極限定理: n回の一様分布の畳み込み → ガウシアン分布

