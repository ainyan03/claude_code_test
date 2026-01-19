# fleximg コーディングスタイルガイド

## 概要

fleximg は組み込み環境への移植を見据えて設計されています。
このドキュメントでは、型選択やコーディング規約について定義します。

---

## 型選択方針

### 基本原則

1. **サイズが明確な型を優先** - 組み込み環境での移植性を確保
2. **演算性能が必要な場面では最速型を活用** - `int_fast*_t` 系
3. **明示キャストは警告が出る箇所のみ** - 過度なキャストは可読性を損なう

### 用途別の型選択

| 用途 | 推奨型 | 備考 |
|------|--------|------|
| ピクセルデータ | `uint8_t`, `uint16_t` | メモリレイアウトが固定される |
| 構造体メンバ（座標等） | `int16_t`, `int32_t` | サイズが明確、メモリ効率重視 |
| 関数引数（座標等） | `int_fast16_t`, `int_fast32_t` | 32bitマイコンでのビット切り詰め回避 |
| ローカル変数（演算用） | `int_fast16_t`, `int_fast32_t` | 演算速度優先の場面で使用 |
| ループカウンタ | `int`, `size_t` | 終端変数と型を一致させる |
| 固定小数点 | `int32_t` | `int_fixed8`, `int_fixed16` として定義済み |
| 配列サイズ/インデックス | `size_t` | 標準ライブラリとの互換性 |
| バッファサイズ | `size_t` | `memcpy` 等の引数に適合 |

### ループカウンタと配列インデックス

1. **ループカウンタと終端変数の型は一致させる**
   ```cpp
   // 良い例：型が一致
   for (int y = 0; y < height; ++y)           // height が int
   for (size_t i = 0; i < vec.size(); ++i)    // size() が size_t

   // 避けるべき例：型不一致
   for (int i = 0; i < vec.size(); ++i)       // 警告の原因
   ```

2. **配列インデックスに使用する際、型が異なる場合は明示的キャスト**
   ```cpp
   // 範囲チェック済みの値を配列インデックスに使用
   buffer[static_cast<size_t>(x)] = value;
   ```

3. **符号あり演算が必要な場面では符号あり型を許容**
   - 逆順ループ（`for (int i = n-1; i >= 0; --i)`）
   - 差分計算（`end - start`）
   - 負の座標を扱う処理

### 最速型 (`int_fast*_t`) の使用

`int_fast8_t`, `int_fast16_t` 等の最速型を使用する際は、必要に応じてサイズ選択の根拠をコメントで説明する。

```cpp
// 良い例：根拠が明確
// 画像幅以下のカウント値（int16_t範囲で十分）
auto count = static_cast<uint_fast16_t>(xEnd - xStart + 1);

// bpp は最大8（64bit RGBA）
int_fast8_t getBytesPerPixel(PixelFormatID id);

// 悪い例：なぜこの型か不明
int_fast32_t value = someCalculation();  // 根拠なし
```

**ローカル変数での `auto` 活用**: キャスト時に `auto` で受けると型情報が保持され、意図が明確になる。

**関数引数での最速型**: 座標や寸法を関数引数で受ける場合は `int_fast16_t` 等を使用する。
ESP32等の32bitマイコンでは16bit型の引数受け渡しでビット切り詰め命令が追加されるため、
最速型を使用することでパフォーマンスが向上する。構造体メンバへの格納時にキャストする。

```cpp
// 関数引数は最速型、メンバ格納時にキャスト
ViewPort(void* d, PixelFormatID fmt, int32_t str,
         int_fast16_t w, int_fast16_t h)
    : data(d), formatID(fmt), stride(str)
    , width(static_cast<int16_t>(w))
    , height(static_cast<int16_t>(h)) {}
```

**プラットフォーム依存性に関する注意**:
`int_fast*_t` の効果はプラットフォームにより異なります。
- ESP32等の32bitマイコン: 16bit引数のビット切り詰め回避で効果あり
- WASM/x64: 通常 `int32_t` と同等、効果は限定的

### 固定小数点型

```cpp
// types.h で定義
using int_fixed = int_fixed16;  // Q16.16 形式（統一型、座標・行列共通）
using int_fixed16 = int32_t;    // Q16.16 形式
using int_fixed8 = int32_t;     // Q24.8 形式 [DEPRECATED]
```

**規約**: 新規コードでは `int_fixed`（Q16.16）を使用すること。
`int_fixed8`（Q24.8）は後方互換性のため残されていますが、非推奨です。
生の `int32_t` で固定小数点値を扱わない。型エイリアスにより意図が明確になり、
将来の型変更にも対応しやすくなる。

```cpp
// 良い例（Q16.16 統一型）
int_fixed posX = to_fixed(100);
int_fixed scale = float_to_fixed(1.5f);

// 悪い例（意図が不明確）
int32_t posX = 100 << 16;
```

### 例

```cpp
// 構造体メンバはサイズ明確な型
struct RenderRequest {
    int16_t width = 0;
    int16_t height = 0;
    Point origin;
};

// ローカル変数で演算する場合は最速型も選択肢
void processPixels(const ViewPort& src) {
    int_fast32_t sum = 0;
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            // ...
        }
    }
}

// memcpy等は size_t にキャスト（警告回避）
std::memcpy(dst, src, static_cast<size_t>(width * bpp));
```

### 座標値と配列インデックス

画像処理では座標値（`int16_t`, `int32_t`, `int_fixed`）を配列インデックスとして使用することが多い。
座標は負になり得る（基準点相対座標）ため、以下のルールに従う。

1. **座標値は符号あり型を維持**
   - 負の座標（基準点より左/上）を扱う必要があるため
   - 計算途中での符号反転を正しく検出するため

2. **アクセサ関数でキャストを吸収**
   ```cpp
   // ViewPort::pixelAt() 等の内部でキャストを行う
   void* pixelAt(int x, int y) {
       // 範囲チェック後、内部で size_t にキャスト
       return static_cast<uint8_t*>(data) +
              static_cast<size_t>(y) * stride +
              static_cast<size_t>(x) * bpp;
   }
   ```

3. **直接アクセス時は明示的キャスト**
   ```cpp
   // 範囲チェック済みの座標を直接使用する場合
   row[static_cast<size_t>(x * 4 + 0)] = r;

   // または一時変数に格納
   size_t offset = static_cast<size_t>(x * 4);
   row[offset + 0] = r;
   ```

---

## コンパイルオプション

### 必須警告オプション

```bash
-Wall -Wextra -Wpedantic
-Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wdouble-promotion
-Wformat=2 -Wnull-dereference -Wunused
```

これらのオプションでクリーンビルドを維持すること。

### 各オプションの説明

| オプション | 説明 |
|-----------|------|
| `-Wall -Wextra -Wpedantic` | 基本的な警告セット |
| `-Wconversion` | 暗黙の型変換（精度喪失の可能性） |
| `-Wsign-conversion` | 符号付き↔符号なし変換 |
| `-Wshadow` | 変数シャドウイング |
| `-Wcast-qual` | const修飾の削除キャスト |
| `-Wdouble-promotion` | float→double暗黙昇格 |
| `-Wformat=2` | printf系フォーマット文字列の厳密チェック |
| `-Wnull-dereference` | NULLポインタ参照の検出 |
| `-Wunused` | 未使用変数・関数の検出 |

### 警告対応の基本方針

1. **新規コードは本ガイドラインに従い、警告が出ないように記述**
2. **型変換は明示的キャストで行う**
   - `int` → `size_t`: `static_cast<size_t>(value)`
   - `int64_t` → `float`: 両辺を `static_cast<float>()` でキャスト
   - `int` → `int16_t`: `static_cast<int16_t>(value)`
3. **memcpy/memsetのサイズ引数は `size_t` にキャスト**
   ```cpp
   std::memcpy(dst, src, static_cast<size_t>(width) * 4);
   ```

---

## 境界チェックパターン

### 符号なしキャストによる範囲チェック

座標値が「負または幅以上」かを判定する際、符号なし型へのキャストで比較を1回にまとめる。

```cpp
// 従来の方法（2回の比較）
if (x < 0 || x >= width) { /* 範囲外 */ }

// 推奨パターン（1回の比較）
if (static_cast<uint_fast32_t>(x) >= static_cast<uint_fast32_t>(width)) {
    /* 範囲外 */
}
```

**原理**: 負の値を符号なし型にキャストすると大きな正の値になるため、
`width` 以上という単一の条件で「負または幅以上」を判定できる。

**使用する型**:
- `uint_fast16_t` - 座標値が `int16_t` 範囲の場合
- `uint_fast32_t` - 座標値が `int32_t` 範囲の場合

**注意**: このパターンは `width` が非負であることを前提とする。

---

## ヘッダ構成

| ヘッダ | 責務 |
|--------|------|
| `types.h` | 固定小数点型、Point構造体、Matrix2x2、変換関数 |
| `common.h` | 名前空間、バージョン、AffineMatrix、行列変換関数 |
| `viewport.h` | ViewPort構造体、画像バッファ操作 |
| `pixel_format.h` | PixelFormatID、フォーマット定義 |

---

## 将来予定

### 型エイリアスによる検証モード

`int_fast*_t` を直接使用せず、fleximg 固有の型エイリアスを導入し、
ビルドオプションで厳密な型と最速型を切り替え可能にする構想。

```cpp
// types.h（構想）
#ifdef FLEXIMG_STRICT_TYPES  // テスト・検証用
using flex_int16 = int16_t;
using flex_int32 = int32_t;
#else  // リリース・パフォーマンス重視
using flex_int16 = int_fast16_t;
using flex_int32 = int_fast32_t;
#endif
```

**目的**:
- テスト時に厳密な型（`int16_t`）でビルドし、オーバーフローを検出
- リリース時は最速型でパフォーマンスを確保
- 環境依存の桁不足バグを早期発見

**状態**: 検討中（影響範囲が大きいため、詳細は後日決定）

---

## 変更履歴

- 2026-01-19: 追加警告オプション（-Wconversion等）を必須化、全警告を解消
- 2026-01-19: ループカウンタ/配列インデックス、座標値、警告オプションのルールを明確化
- 2026-01-12: 初版作成
