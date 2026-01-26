# トラブルシューティング

fleximg を使用する際によくある問題と解決方法です。

---

## フィルタ関連

### Q: ブラーフィルタをRenderer下流に配置すると、タイル境界で縦線が入る

**原因**: ブラーフィルタ（およびカーネルベースフィルタ）は隣接ピクセルを参照するため、Renderer下流のPush処理では隣接タイルの情報を参照できません。

**解決方法**: ブラーフィルタを Renderer の **上流（Pull処理側）** に配置してください。

```
【NG】タイル境界で乱れる
SourceNode → Renderer → HBlur → VBlur → SinkNode
                          ↑ Push処理では隣接タイル参照不可

【OK】正常に動作
SourceNode → HBlur → VBlur → Renderer → SinkNode
               ↑ Pull処理では必要な範囲を要求可能
```

**技術的背景**: Pull処理では、ブラーノードが上流に対して `radius` 分拡大した範囲を要求できます。Push処理では、受け取ったデータのみで処理するため、タイル境界の情報が不足します。

---

### Q: フィルタを適用しても画像が変化しない

**考えられる原因**:

1. **パラメータがデフォルト値のまま**
   ```cpp
   BrightnessNode brightness;
   brightness.setAmount(0.0f);  // デフォルト値 = 変化なし
   ```

2. **ノードが接続されていない**
   ```cpp
   // 接続忘れ
   source >> affine >> renderer >> sink;
   // ↑ brightness が接続されていない
   ```

3. **radius = 0 のブラーフィルタ**
   ```cpp
   HorizontalBlurNode hblur;
   hblur.setRadius(0);  // スルー出力になる
   ```

---

## パイプライン関連

### Q: exec() を呼んでも何も描画されない

**チェックリスト**:

1. **SinkNode に有効な出力先が設定されているか**
   ```cpp
   SinkNode sink(outputView, pivotX, pivotY);
   // outputView が有効か確認
   ```

2. **RendererNode の仮想スクリーンが設定されているか**
   ```cpp
   renderer.setVirtualScreen(width, height);
   renderer.setPivotCenter();  // または renderer.setPivot(pivotX, pivotY);
   ```

3. **ノードが正しく接続されているか**
   ```cpp
   source >> affine >> renderer >> sink;
   // 全ノードが連結されているか確認
   ```

4. **SourceNode に画像が設定されているか**
   ```cpp
   SourceNode source(imageView);
   // imageView が有効か確認
   ```

---

### Q: 循環参照エラーが発生する

**原因**: ノードグラフに循環（ループ）が存在します。

```
【NG】循環参照
NodeA → NodeB → NodeC → NodeA
                         ↑ ループ
```

**解決方法**: グラフが DAG（有向非巡回グラフ）になるよう接続を見直してください。

---

## 座標関連

### Q: 画像が意図した位置に表示されない

**チェックポイント**:

1. **SourceNode の pivot 設定**
   ```cpp
   // 画像中央を pivot にする場合
   source.setPivot(to_fixed(imageWidth / 2), to_fixed(imageHeight / 2));
   ```

2. **SinkNode の pivot 設定**
   ```cpp
   // キャンバス中央を pivot にする場合
   SinkNode sink(outputView, canvasWidth / 2, canvasHeight / 2);
   // または sink.setPivotCenter();
   ```

3. **RendererNode の仮想スクリーン設定**
   ```cpp
   renderer.setVirtualScreen(width, height);
   renderer.setPivotCenter();  // または renderer.setPivot(pivotX, pivotY);
   ```

**pivot 一致ルール**: SourceNode と SinkNode の pivot がワールド原点 (0,0) で一致するように配置されます。詳細は [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) を参照。

---

### Q: アフィン変換後の画像が切れる

**原因**: 変換後の画像サイズが仮想スクリーンより大きくなっている可能性があります。

**解決方法**: 仮想スクリーンサイズを十分大きく設定してください。

```cpp
// 回転・拡大後のサイズを考慮
renderer.setVirtualScreen(
    originalWidth * 2,   // 余裕を持たせる
    originalHeight * 2
);
renderer.setPivotCenter();  // 中央を pivot に設定
```

---

## WebUI関連

### Q: ノードを追加できない

**考えられる原因**:

1. **右クリックメニューが表示されない**: キャンバス領域を右クリックしているか確認
2. **ノードが画面外に追加される**: スクロールして探すか、キャンバスをリセット

---

### Q: ワイヤーが接続できない

**チェックポイント**:

1. **出力ポートから入力ポートへ** ドラッグしているか（逆方向は不可）
2. **同じノードへの接続** は不可
3. **既に接続済みの入力ポート** には接続不可（先に切断が必要）

---

### Q: パラメータを変更しても反映されない

**考えられる原因**:

1. **Renderer ノードがない**: パイプラインを実行するにはRendererノードが必要
2. **ノードが接続されていない**: 孤立したノードは処理されない

**解決方法**: ブラウザの開発者ツール（F12）でコンソールを確認してください。

---

## ビルド関連

### Q: Emscripten ビルドが失敗する

**チェックリスト**:

1. **Emscripten 環境が有効か**
   ```bash
   source /path/to/emsdk/emsdk_env.sh
   which emcc  # パスが表示されるか確認
   ```

2. **必要なツールがインストールされているか**
   ```bash
   ./emsdk install latest
   ./emsdk activate latest
   ```

---

### Q: ネイティブテストが失敗する

**よくある原因**:

1. **C++17 対応コンパイラがない**
   ```bash
   g++ --version  # C++17 サポートを確認
   ```

2. **doctest.h がない**
   - `test/doctest.h` が存在するか確認

---

## 関連ドキュメント

- [QUICKSTART.md](QUICKSTART.md) - 基本的な使い方
- [CONCEPTS.md](CONCEPTS.md) - 基本概念の説明
- [ARCHITECTURE.md](ARCHITECTURE.md) - 技術詳細
- [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) - 座標系の詳細
