# fleximg TODO

## 実装予定

### ノード循環参照検出機能

**概要**: ノードグラフの循環参照を検出し、無限再帰によるスタックオーバーフローを防止する

**方式**: 2段階フラグ方式（Idle/Preparing/Prepared）

**実装内容**:
1. `Node`クラスに`PrepareState`列挙型と状態変数を追加
   ```cpp
   enum class PrepareState { Idle, Preparing, Prepared };
   PrepareState prepareState_ = PrepareState::Idle;
   ```

2. `pullPrepare`の修正
   - `Preparing`状態での再帰呼び出し → エラー（真の循環参照）
   - `Prepared`状態での呼び出し → スキップ（DAG共有ノード対応）
   - 処理開始時に`Preparing`、完了時に`Prepared`へ遷移

3. `pullFinalize`の修正
   - 処理完了後に`Idle`へリセット

4. `pushPrepare`/`pushFinalize`も同様に対応

**備考**:
- CompositeNodeのような複数入力ノードで、同じ上流ノードが共有されるケース（正当なDAG）に対応
- エラー時の通知方法は要検討（戻り値/例外/コールバック）

---

### 画像デコーダーノード（RendererNode派生）

**概要**: JPEG/PNG等の画像デコードをRendererNode派生型として実装し、フォーマット固有のブロック単位でタイル処理を行う

**設計方針**:
- RendererNodeは「タイル分割戦略の決定者」であり、デコーダー派生型がフォーマット固有の制約を課す
- 画像フォーマットの特性上、ブロック順序（ラスタースキャン等）の強制は妥当な制約

**実装内容（JpegDecoderNode例）**:

1. クラス構造
   ```cpp
   class JpegDecoderNode : public RendererNode {
   public:
       void setSource(const uint8_t* jpegData, size_t size);
   protected:
       void execPrepare() override;   // ヘッダ解析、MCUサイズ設定
       void execProcess() override;   // MCU順ループ
       void processTile(int tileX, int tileY) override;
   };
   ```

2. `execPrepare()`
   - JPEGヘッダ解析、画像サイズ・MCUサイズ取得
   - `setVirtualScreen(imageWidth, imageHeight)`
   - `setTileConfig(mcuWidth, mcuHeight)`

3. `execProcess()`
   - MCU行単位でデコード（libjpeg-turboのスキャンライン API活用）
   - MCU順（ラスタースキャン）でprocessTile()を呼び出し

4. `processTile()`
   - デコード済みMCUからRenderResult生成
   - 下流ノードへpush

**利点**:
- メモリ効率: 全画像を一度にデコードせず、必要な部分だけ処理
- パイプライン再利用: 既存のフィルター/シンクノードをそのまま活用
- 制約の明示性: クラス選択でMCU順処理が明示される

**検討事項**:
- MCU行単位のバッファ管理（1行分をキャッシュし、processTileで切り出す）
- 入力ポート数: 発火点兼データソースなので0ポート化も検討
- 将来拡張: PNG（スキャンライン単位）、WebP（ブロック単位）等のフォーマット対応
