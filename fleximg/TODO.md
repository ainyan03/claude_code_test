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
