# blendUnder* ベンチマーク結果

## 概要

ピクセルフォーマット別の `blendUnderPremul` / `blendUnderStraight` 関数について、
**直接パス**と**間接パス**の性能比較を行った結果です。

### パス定義

| パス | 処理内容 |
|------|----------|
| 直接パス | `srcFormat->blendUnderPremul(dst, src, count)` |
| 間接パス | `srcFormat->toPremul(tmp, src, count)` + `RGBA16_Premultiplied->blendUnderPremul(dst, tmp, count)` |

直接パスは変換と合成を1関数で行い、間接パスは標準形式への変換後に合成を行います。

---

## 測定環境

### PC環境

- **OS**: macOS (Darwin)
- **CPU**: Apple Silicon
- **コンパイラ**: g++ (clang) -O3
- **ピクセル数**: 1024
- **反復回数**: 1000

### M5Stack Core2 (ESP32)

- **CPU**: ESP32 (Xtensa LX6 dual-core, 240MHz)
- **コンパイラ**: pio (Arduino framework) -Os
- **ピクセル数**: 4096
- **反復回数**: 1000

---

## blendUnderPremul 結果

### PC (x86-64 / Apple Silicon)

| Format | Direct(us) | Indirect(us) | Ratio |
|--------|------------|--------------|-------|
| RGB332 | 4.04 | 2.20 | 0.55x |
| RGB565_LE | 4.72 | 4.75 | 1.01x |
| RGB565_BE | 4.16 | 4.10 | 0.99x |
| RGB888 | 2.18 | 2.69 | 1.23x |
| BGR888 | 1.95 | 2.64 | 1.36x |
| RGBA8_Straight | 1.92 | 2.90 | 1.51x |

### M5Stack Core2 (ESP32)

| Format | Direct(us) | Indirect(us) | Ratio |
|--------|------------|--------------|-------|
| RGB332 | 1336 | 1782 | **1.33x** |
| RGB565_LE | 1302 | 1585 | **1.22x** |
| RGB565_BE | 1354 | 1688 | **1.25x** |
| RGB888 | 1165 | 1662 | **1.43x** |
| BGR888 | 1165 | 1662 | **1.43x** |
| RGBA8_Straight | 930 | 1361 | **1.46x** |

> **Ratio**: Indirect / Direct（1より大きい場合、直接パスが高速）

---

## blendUnderStraight 結果

### PC (x86-64 / Apple Silicon)

| Format | Direct(us) | Indirect(us) | Ratio |
|--------|------------|--------------|-------|
| RGBA8_Straight | 3.62 | 3.60 | 1.00x |

> 他のフォーマットは `blendUnderStraight` 未実装のためスキップ

---

## 考察

### プラットフォーム差異

| 観点 | PC | ESP32 |
|------|-----|-------|
| RGB332 | 間接パスが速い (0.55x) | 直接パスが速い (1.33x) |
| RGB565 | ほぼ同等 | 直接パスが22-25%速い |
| RGB888/BGR888 | 直接パスが23-36%速い | 直接パスが43%速い |
| RGBA8_Straight | 直接パスが51%速い | 直接パスが46%速い |

### 分析

1. **ESP32では全フォーマットで直接パスが有利**
   - メモリ帯域が制約となる組み込み環境では、中間バッファを使わない直接パスが効果的

2. **PCではRGB332/RGB565で直接パスのメリットが薄い**
   - x86-64の分岐予測・キャッシュ効率により、間接パスでも十分高速
   - RGB332の直接パスが遅いのは、ビットデコード処理の最適化余地がある可能性

3. **RGBA8_Straightは両環境で直接パスが優位**
   - 変換オーバーヘッドがないため、直接パスの利点が明確

### 結論

- **組み込み環境（ESP32等）**: 直接パス最適化は有効。全フォーマットで22-46%の改善
- **PC環境**: RGB888/RGBA8では直接パスが有効。RGB332/RGB565は最適化検討の余地あり

---

## ベンチマーク実行方法

### PC

```bash
cd fleximg/test
make blend_bench
./blend_bench
```

### M5Stack

```bash
cd fleximg/examples/m5stack_method_bench
pio run -e m5stack_core2 -t upload
# シリアルで 'c' コマンドを送信
```

---

## 関連ドキュメント

- [DESIGN_PIXEL_FORMAT.md](DESIGN_PIXEL_FORMAT.md) - ピクセルフォーマット設計
- [DESIGN_PERF_METRICS.md](DESIGN_PERF_METRICS.md) - パフォーマンス計測

---

*測定日: 2026-01-22*
