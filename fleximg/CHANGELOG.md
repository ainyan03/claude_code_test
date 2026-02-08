# Changelog

All notable changes to fleximg will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

> **Note**: 詳細な変更履歴は [docs/CHANGELOG_v2_detailed.md](docs/CHANGELOG_v2_detailed.md) を参照してください。

---

## [Unreleased]

### Changed

- **WebUI: C++同期型定義を `cpp-sync-types.js` に分離**
  - `NODE_TYPES`, `PIXEL_FORMATS`, `DEFAULT_PIXEL_FORMAT` 等のC++側と手動同期が必要な定義を `app.js` から `demo/web/cpp-sync-types.js` に分離
  - `buildFormatOptions()`, `NodeTypeHelper` も同ファイルに移動
  - C++側コメント・ドキュメントの参照先を更新

### Fixed

- **WebUI: `PIXEL_FORMATS` の `bpp` を bytesPerPixel から bitsPerPixel に修正**
  - 値をバイト単位からビット単位に変換（例: `4` → `32`, `0.5` → `4`）
  - UI表示を `(4B)` → `(32bit)` に変更

### Added

- **WebUI: Grayscale1/2/4 フォーマット選択 + optgroup 分類**
  - フォーマット選択ドロップダウンに Grayscale1/2/4 MSB/LSB の6フォーマットを追加
  - `<optgroup>` によるカテゴリ分類（RGB / Grayscale / Alpha / Index）でUI整理
  - `buildFormatOptions()` ヘルパー関数で SourceNode / SinkNode の select 構築を共通化

- **Grayscale bit-packed フォーマット** (Grayscale1/2/4 MSB/LSB)
  - 1/2/4ビットのグレースケールフォーマットを6種追加
  - MSBFirst（上位ビット優先）とLSBFirst（下位ビット優先）の両方に対応
  - `isIndexed = false`, `maxPaletteSize = 0`（Index系とは独立したフォーマット）
  - DDA転写・バイリニア補間をフルサポート（copyRowDDA_Bit / copyQuadDDA_Bit 共有）
  - `grayscale8.h` → `grayscale.h` にリネームし、Grayscale8 + GrayscaleN を統合
  - `bit_packed_detail` ヘルパー関数群を `grayscale.h` に移動（Index/Grayscale共用）
  - IndexN の `toStraight` を `grayscaleN_toStraight` に直接設定（コード共有）
  - IndexN の `fromStraight` を `grayscaleN_fromStraight` への委譲ラッパーに変更

### Changed

- **コーディングスタイル違反の包括的修正**
  - 固定小数点Q16.16メンバを `int32_t` → `int_fixed` に統一（AffinePrecomputed, SinkNode, SourceNode）
  - 関数引数を `int` → `int_fast16_t` に修正（viewport, image_buffer, pixel_format, render_types 等14ファイル）
  - 構造体メンバを `int` → `int16_t` に修正（RendererNode, BlurNode, MatteNode, PerfMetrics 等）
  - ローカル変数・ループカウンタの型を終端変数に一致させる修正（auto + static_castパターン）
  - 配列インデックスの型を `uint_fast8_t` / `size_t` に整理（EntryPool, RenderContext, FormatMetrics 等）
  - PerfMetrics の count/allocCount メンバを `uint32_t` に統一

- **関数ポインタ型の `int` 引数を `size_t` / `int_fast16_t` に修正**
  - `ConvertFunc` 系（ピクセル変換関数）: `int pixelCount` → `size_t pixelCount`
  - `CopyRowDDA_Func` / `CopyQuadDDA_Func`（DDA転送関数）: `int count` → `int_fast16_t count`
  - `LineFilterFunc`（ラインフィルタ関数）: `int count` → `int_fast16_t count`
  - 対象12ファイルの実装・ローカル変数も型を統一

- **bit-packed unpackロジック集約 + Index8処理共通化**
  - パレットLUT処理を `applyPaletteLUT` 共通関数として切り出し、Index8/IndexN で共有

- **Index8のパレットなしフォールバックをGrayscale8に統合**
  - `index8_toStraight` を削除し、Index8 Descriptorの `toStraight` に `grayscale8_toStraight` を直接設定
  - `index8_fromStraight` は `grayscale8_fromStraight` への委譲ラッパーに変更（将来のパレットマッピング拡張に備える）
  - `grayscale8_fromStraight` を4ピクセルループ展開版に最適化
  - `indexN_toStraight` の委譲先を `grayscale8_toStraight` に変更
  - バイナリ上の重複コード解消、関数ポインタの共有による効率化
  - `indexN_expandIndex` を末尾詰め方式に変更（チャンクバッファ不要、in-place展開）
  - `indexN_toStraight` を末尾詰め + `index8_toStraight` 委譲に変更
  - `copyRowDDA_Bit` に ConstY 高速パス追加（バルクunpack + DDAサンプリング）

- **`getBytesPerPixel()` 関数を廃止・直接アクセス化**
  - `getBytesPerPixel(fmt)` → `fmt->bytesPerPixel` に全面置換
  - `ViewPort::bytesPerPixel()` / `ImageBuffer::bytesPerPixel()` の戻り値型を `uint8_t` に統一
  - BytesPerPixel関連メンバを `uint8_t` に統一（符号変換警告を解消）

- **PixelFormatDescriptor の整理・最適化**
  - `ChannelType` enum、`ChannelDescriptor` struct、`channels[4]` 配列を削除（32バイト削減）
  - 関連メソッド（`getChannel`, `getChannelIndex`, `hasChannelType`, `getChannelByType`）を削除
  - メンバ並び順をアライメント効率順に再配置（ポインタ→4byte→2byte→1byte）
  - `viewport.h` の `canUseSingleChannelBilinear()` を `hasAlpha` で代替

- **PixelAuxInfo メンバ配置最適化**
  - メンバ並び順をアライメント効率順に再配置（40+→28バイト）

- **FormatConverter::Context メンバ配置最適化・型整理**
  - メンバ並び順をアライメント効率順に再配置（72→56バイト、約22%削減）
  - BytesPerPixel系メンバを `uint8_t` に統一

- **BytesPerPixel変数名の明確化**
  - `srcBpp`, `dstBpp`, `paletteBpp` を `srcBytesPerPixel`, `dstBytesPerPixel`, `paletteBytesPerPixel` にリネーム
  - `Bpp` 略語がbit/byteどちらか不明瞭だった問題を解消
  - 対象: FormatConverter::Context メンバおよび関連ローカル変数（6ファイル）

- **ViewPort Coordinate Offset Support**（Plan B実装）
  - ViewPortに `x, y` 座標フィールドを追加（bit-packed format対応の根本的解決）
  - `data` ポインタは常にバッファ全体を指し、`(x,y)` で論理的な開始位置を表現
  - `subView()` は座標オフセットを累積（dataポインタを進めない）
  - 全フォーマット（bit-packed含む）で統一的に動作
  - 将来的なクロップ最適化、ウィンドウ管理の基盤

### Fixed

- **ViewPort x,y オフセットの適用漏れ修正**
  - `SourceNode::pullProcessWithAffine()`: アフィンパスでのDDAパラメータ計算時にオフセット加算
  - `ImageBuffer::toFormat()`: フォーマット変換時の行アドレス計算でオフセット考慮
  - `ImageBuffer::copyFrom()`: ディープコピー時の送信元・送信先アドレス計算でオフセット考慮
  - `ImageBuffer::blendFrom()`: ブレンド合成時の行開始アドレス計算でオフセット考慮
  - 非アフィンパスでの「先頭行が縦に引き伸ばされる」バグを修正

- **MatteNode マスク画像の横方向位置ずれ修正**
  - `InputView::from()` が ViewPort の x,y オフセットを無視していたため、`scanMaskZeroRanges` + `cropView` 後のマスクデータアクセス位置がずれていた
  - `ptr` 設定時に `vp.y * vp.stride + vp.x * vp.bytesPerPixel()` を加算し、ビュー位置をポインタに織り込むよう修正

- **MatteNode 背景コピーの ViewPort オフセット適用漏れ修正**
  - bgの非アフィン状態で先頭行の内容が全ラインに適用されるバグを修正
  - 背景コピーループの行アドレス計算に `bgViewPort.x`, `bgViewPort.y` オフセットを加算

- **WebUI: ビットパック形式のストライド不整合による画像歪みを修正**
  - `bindings.cpp` でビットパック形式（Gray1/2/4, Index1/2/4）の変換が全ピクセルを1Dストリームとして処理していたため、`width % pixelsPerUnit != 0` の場合に行境界でパディングされず画像が歪んでいた
  - `calcStride()` / `convertFormatRowByRow()` ヘルパー関数を追加し、全変換箇所（7箇所）を行単位変換に修正
  - `ImageStore::store()` / `allocate()` のバッファサイズ計算も `stride * height` に修正

- **bit-packed format の pixelOffsetInByte サポート**
  - CompositeNode経由でbit-packed（Index1/2/4）データを処理する際のチャンク境界でのオフセットずれを修正
  - `PixelAuxInfo::pixelOffsetInByte` フィールド追加（1バイト内でのピクセル位置 0 - PixelsPerByte-1）
  - `FormatConverter::Context::pixelOffsetInByte` フィールド追加
  - `ImageBuffer::blendFrom()`: bit-packedフォーマットで正確なバイト/ピクセルオフセット計算を実装
  - `unpackIndexBits()`: pixelOffsetInByte パラメータ追加、バイト内のピクセル位置から正確に読み取り
  - ViewPort.x の端数成分（subView使用時）を正しく反映
  - チャンク境界（64ピクセル単位）をまたぐ変換でも正確な位置からデータを読み取り

### Improved

- **blendFrom() のリファクタリング**
  - 変数名改善: `dstBpp`/`srcBpp` → `dstPixelBytes`/`srcPixelBits`（bits/bytesの混同防止）
  - `src.view()` の値コピー(×11回) → `src.viewRef()` のconst参照に変更
  - `getBytesPerPixel()` ヘルパ → `formatID->` メンバ直接参照に変更
  - srcオフセット計算をビット単位で一括化（`x * pixelBits` → `>>3` でバイト、`&7` でビット端数）
  - bit-packed/通常フォーマットの条件分岐を完全に除去
  - `pixelOffsetInByte` を `(totalBits & 7) >> (pixelBits >> 1)` のビットマスク・シフトで算出
  - 二重 `>>3` 切り捨てによるバイトオフセット誤差の潜在的問題を解消

### Added

- **ViewPort オフセットテスト**（`viewport_offset_test.cpp`）
  - 全ピクセルフォーマットでのsubView座標オフセット動作を検証
  - 4つのオフセットパターン（x=0/3/8/9）でバイト境界またぎを包括的にテスト
  - bit-packed形式（Index1/2/4 MSB/LSB）で特に重要
  - subView連鎖での累積オフセット検証
  - 12テストケース、59アサーション追加（162テスト総計）

- **DDA Function Consolidation and Naming Unification**
  - DDA（Digital Differential Analyzer）関連の関数を機能別に集約
  - 新規ファイル `pixel_format/dda.h` を作成し、全てのDDA関数を統合
  - テンプレート関数名を統一:
    - `copyRowDDA_bpp` → `copyRowDDA_Byte`（バイト単位: 1/2/3/4バイト）
    - `copyQuadDDA_bpp` → `copyQuadDDA_Byte`（バイト単位）
    - `indexN_copyRowDDA` → `copyRowDDA_Bit`（ビット単位: 1/2/4ビット）
    - `indexN_copyQuadDDA` → `copyQuadDDA_Bit`（ビット単位）
  - ラッパー関数名も統一: `copyRowDDA_1Byte` ～ `_4Byte`
  - 命名の対称性を実現: `_Byte`（バイト単位） vs `_Bit`（ビット単位）
  - pixel_format.hの肥大化を解消（約440行削減）

- **Index Format Consolidation**
  - `index8.h` と `bit_packed_index.h` を `index.h` に統合
  - Index1/2/4/8 全てのインデックスフォーマットを単一ファイルで管理
  - bit_packed_detail名前空間も同ファイル内に配置
  - ファイル構成の簡素化と保守性の向上

---

## [2.64.0] - 2026-02-06

### Added

- **Bit-packed Index Formats** (Index1/2/4 MSB/LSB)
  - 1/2/4ビットのパレットインデックスフォーマットを追加
  - MSBFirst（上位ビット優先）とLSBFirst（下位ビット優先）の両方に対応
  - アフィン変換（DDA）とバイリニア補間をフルサポート

### Changed

- **DDA Implementation Refactoring**
  - bit-packed形式のDDA実装をLovyanGFXスタイルのピクセル単位アクセスに書き換え
  - バッファベースからダイレクトビットアクセスに変更し、境界処理を改善

- **BPP/bpp Naming Unification**
  - 曖昧な "bpp" 表記を明示的な "bytesPerPixel" に統一
  - `PixelFormatDescriptor` に `bytesPerPixel` フィールドを追加
  - `(bitsPerPixel + 7) / 8` の分散した計算ロジックを一元化
  - テンプレートパラメータ、変数名、コメント、ドキュメントを全面的に更新

### Fixed

- **Bilinear Interpolation Edge Fade**
  - バイリニア補間時のエッジフェード機能を修正
  - 境界外ピクセルは隣接する有効なピクセルの値を使用するように改善
  - edgeFlagsの生成ロジックを座標ベースに修正

---

## [2.63.x] - 2026-02-05～2026-02-06

### ハイライト

v2.63系では、パフォーマンス最適化とAPI整理を中心に28回のリリースを実施しました。

#### 主な新機能

- **Alpha8/Grayscale8 単一チャンネル バイリニア補間** (v2.63.28)
  - 1チャンネルフォーマットの補間処理を最適化（メモリ4倍削減、演算量約1/4削減）

- **カラーキー透過機能** (v2.63.27)
  - アルファチャンネルを持たないフォーマットで特定色を透明として扱える機能
  - `SourceNode::setColorKey()` / `clearColorKey()` API追加

- **EdgeFadeFlags** (v2.63.15)
  - バイリニア補間の辺ごとのフェードアウト制御
  - NinePatch内部エッジでのフェードアウト無効化に使用

#### 主な改善・最適化

- **ImageBuffer の簡素化** (v2.63.26)
  - validSegments 複数セグメント追跡を廃止（265行削減）
  - ゼロ初期化 + 常にブレンド方式に変更し、パフォーマンス向上

- **CompositeNode 合成処理の最適化** (v2.63.25)
  - 事前集計方式に変更、ベンチマークで約27%の性能改善（N=32時）

- **SourceNode::getDataRange() の正確化** (v2.63.24)
  - DDAベースの厳密計算でNinePatchの余白バッファを削減

- **ImageBuffer 座標系の統一** (v2.63.22)
  - `startX_` (int16_t) → `origin_` (Point, Q16.16固定小数点) に拡張
  - M5Stackでの性能退行を解消

- **バイリニア補間のマルチフォーマット対応** (v2.63.16)
  - Index8を含む全フォーマットでバイリニア補間が利用可能に

#### API変更

- **ImageBufferSet**: オフセットパラメータ削除、origin統一 (v2.63.23)
- **Node::getDataRangeBounds()**: AABB由来の最大範囲上限を返すメソッド追加 (v2.63.24)
- **ImageBuffer::blendFrom()**: under合成メソッドの簡素化 (v2.63.25)

#### バグ修正

- RenderResponseプール管理の再設計（DOUBLE RELEASE修正） (v2.63.6)
- NinePatch角パッチの描画位置修正 (v2.63.21)
- SourceNode AABB拡張（バイリニア補間時のフェードアウト領域反映） (v2.63.20)

詳細は [docs/CHANGELOG_v2_detailed.md の v2.63.x セクション](docs/CHANGELOG_v2_detailed.md#2630---2026-02-05) を参照してください。

---

## [2.62.x] - 2026-02-01

### ハイライト

v2.62系では、パイプラインリソース管理の一元化とレスポンス参照パターンへの移行を実施しました。

#### 主な変更

- **RenderContext導入** (v2.62.0)
  - パイプライン動的リソース管理の一元化
  - PrepareRequest.allocator/entryPool を context に統合
  - 将来拡張に対応（PerfMetrics, TextureCache等）

- **RenderResponse参照ベースパターン** (v2.62.3)
  - 値渡しから参照渡しへ移行、ムーブコスト削減
  - RenderContextがRenderResponseプール（MAX_RESPONSES=64）を管理

- **ImageBufferSet::transferFrom()**: バッチ転送API追加 (v2.62.2)
  - 上流結果の統合を効率化

#### バグ修正

- ImageBufferEntryPool::release(): エントリ返却時にバッファをリセット (v2.62.4)

詳細は [docs/CHANGELOG_v2_detailed.md の v2.62.x セクション](docs/CHANGELOG_v2_detailed.md#2620---2026-02-01) を参照してください。

---

## [2.61.0 以前] - 2026-01-09～2026-01-31

v2.0.0 から v2.61.0 までの詳細な変更履歴は [docs/CHANGELOG_v2_detailed.md](docs/CHANGELOG_v2_detailed.md) を参照してください。

### 主なマイルストーン

- **v2.56.0**: インデックスカラー対応（Index8, Grayscale8, Alpha8）
- **v2.50.0**: NinePatchSourceNode追加
- **v2.40.0**: ガウシアンブラー実装
- **v2.30.0**: メモリプール最適化
- **v2.20.0**: レンダリングパイプライン刷新
- **v2.0.0**: メジャーバージョンリリース
