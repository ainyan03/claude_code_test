# fleximg v1 (アーカイブ)

このディレクトリはfleximg v1の成果物をアーカイブとして保存しています。

## 注意

**このコードは開発終了しています。** 新規開発は `fleximg/` で行われています。

## 用途

- 新旧バージョンの比較
- v1 設計の参照
- ドキュメント作成時の参照

## ディレクトリ構造

```
fleximg_old/
├── src/
│   ├── fleximg/       # v1 ソースコード
│   └── fleximg.h      # v1 統合ヘッダ
├── docs/              # v1 設計ドキュメント
├── test/              # v1 テストコード
├── cli/               # v1 CLIツール
├── demo/
│   └── bindings.cpp   # v1 WASMバインディング
├── TODO.md            # v1 タスク一覧
├── CHANGELOG.md       # v1 変更履歴
└── Makefile           # v1 ネイティブビルド用
```

## 新 fleximg への移行

新しい fleximg では以下の改善が行われました:

- **Node/Port モデル**: 文字列IDからオブジェクト参照への変更
- **ViewPort の簡素化**: ImageBuffer + ViewPort の明確な責務分離
- **タイル効率改善**: より効率的なメモリ使用とタイル処理
- **コード量削減**: より簡潔で保守しやすい設計

新規開発は `fleximg/` を使用してください。
