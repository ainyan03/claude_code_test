# Changelog

## [2.1.0] - 2026-01-09

### 追加

- **パフォーマンス計測基盤**: デバッグビルド時にノード別の処理時間を計測
  - `./build.sh --debug` で有効化
  - ノードタイプ別の処理時間（μs）、呼び出し回数を記録
  - Filter/Transformノードのピクセル効率（wasteRatio）を計測
  - `Renderer::getPerfMetrics()` でC++から取得
  - `evaluator.getPerfMetrics()` でJSから取得

- **メモリ確保統計**: ノード別・グローバルのメモリ使用量を計測
  - `NodeMetrics.allocatedBytes`: ノード別確保バイト数
  - `NodeMetrics.allocCount`: ノード別確保回数
  - `NodeMetrics.maxAllocBytes/Width/Height`: 一回の最大確保サイズ
  - `PerfMetrics.totalAllocatedBytes`: 累計確保バイト数
  - `PerfMetrics.peakMemoryBytes`: ピークメモリ使用量
  - `PerfMetrics.maxAllocBytes/Width/Height`: グローバル最大確保サイズ

- **シングルトンパターン**: `PerfMetrics::instance()` でグローバルアクセス
  - ImageBuffer のコンストラクタ/デストラクタで自動記録
  - 正確なピークメモリ追跡が可能に

### 改善

- **ノード詳細ポップアップ**: UI操作性の向上
  - ヘッダー部分をドラッグしてウィンドウを移動可能に
  - パネル内ボタンクリック時にポップアップが閉じる問題を修正

- **サイドバー状態の保持**: ページ再読み込み時に前回の状態を復元
  - サイドバーの開閉状態を localStorage に保存
  - アコーディオンの展開状態を localStorage に保存

### ファイル追加

- `src/fleximg/perf_metrics.h`: メトリクス構造体定義（シングルトン）
- `docs/DESIGN_PERF_METRICS.md`: 設計ドキュメント

---

## [2.0.0] - 2026-01-09

C++コアライブラリを大幅に刷新しました。

### 主な変更点

- **Node/Port モデル**: ノード間の接続をオブジェクト参照で直接管理
- **Renderer クラス**: パイプライン実行を集中管理
- **ViewPort/ImageBuffer 分離**: メモリ所有と参照の明確な責務分離
- **タイルベースレンダリング**: メモリ効率の良いタイル分割処理

### 機能

- SourceNode: 画像データの提供
- SinkNode: 出力先の管理
- TransformNode: アフィン変換（平行移動、回転、スケール）
- FilterNode: フィルタ処理（明るさ、グレースケール、ぼかし、アルファ）
- CompositeNode: 複数入力の合成

### タイル分割

- 矩形タイル分割
- スキャンライン分割
- デバッグ用チェッカーボードスキップ

### ピクセルフォーマット

- RGBA8_Straight: 入出力用
- RGBA16_Premultiplied: 内部処理用

---

旧バージョン（1.x系）の変更履歴は `fleximg_old/CHANGELOG.md` を参照してください。
