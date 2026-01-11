# プッシュ型アフィン変換

> **実装済み**: v2.19.0 で `AffineNode::pushProcess()` として実装

## 概要

JPEG デコーダー等の MCU 単位プッシュ処理に対応するため、AffineNode をプッシュ型でも動作可能にする。

## ユースケース

```
[JPEG Decoder] --push(MCU@position)--> [Affine] --push(transformed)--> [Sink]
```

- JPEG デコーダーが MCU (8x8 or 16x16) 単位でデコード
- 各 MCU を位置情報付きで下流へプッシュ
- AffineNode が変換して Sink へ渡す

## 設計方針

### prepare() / process() 分離

```cpp
void prepare(const RenderRequest& screenInfo) override {
    screenInfo_ = screenInfo;
    invMatrix_ = computeInverse(matrix_);      // プル用
    fwdMatrix_ = computeForwardFixed(matrix_); // プッシュ用
}

RenderResult process(RenderResult&& input, const RenderRequest& request) override {
    // 純粋に変換処理のみ（プル/プッシュ共通）
}
```

### プッシュ処理フロー

```cpp
void pushProcess(RenderResult&& input, const RenderRequest& inputInfo) override {
    // 1. 入力位置から出力位置を計算（順変換）
    RenderRequest outputReq = computeOutputRequest(input, inputInfo);

    // 2. process() で変換
    RenderResult output = process(std::move(input), outputReq);

    // 3. 下流へプッシュ
    downstreamNode(0)->pushProcess(std::move(output), outputReq);
}
```

## 課題

### 回転時の MCU 境界問題

```
入力 MCU (8x8)       回転後 AABB
┌────┐               ◇
│    │              ╱ ╲
└────┘             ◇   ◇
                    ╲ ╱
                     ◇
```

- 回転により AABB が拡大
- 隣接 MCU の出力領域が重複する可能性
- Sink 側で重複領域の合成（アルファブレンド）が必要

### 解決案

1. **Sink でのブレンド合成**: 重複領域をアルファブレンドで合成
2. **MCU 境界の事前計算**: prepare() で全 MCU の出力領域を計算し、重複を検出
3. **タイル単位での再構成**: 出力タイル単位で必要な MCU を収集して変換

## 実装ステップ

1. [x] AffineNode で prepare() を使用（逆行列の事前計算）
2. [x] 順変換用の固定小数点行列を追加 (`fwdMatrix_`)
3. [x] pushProcess() の基本実装（入力4隅を順変換してAABB計算）
4. [ ] Sink でのブレンド合成対応（将来課題）
5. [ ] JPEG デコーダーとの統合テスト（将来課題）

## 関連

- [IDEA_IMAGE_DECODER_NODE.md](IDEA_IMAGE_DECODER_NODE.md): JPEG デコーダーノードの構想
