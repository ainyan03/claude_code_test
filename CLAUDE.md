# Claude Code プロジェクトガイドライン

## プロジェクト概要

**fleximg**: ノードベースの画像処理パイプラインライブラリ
- C++17（一部C++14互換）、組み込み環境対応
- WebAssembly版デモあり（GitHub Pages公開中）
- M5Stack等のマイコン向けサンプルあり

### 主要ディレクトリ

```
fleximg/
├── src/fleximg/           # コアライブラリ
│   ├── nodes/             # ノード実装（Source, Composite, Filter等）
│   ├── image/             # ImageBuffer, PixelFormat, ViewPort
│   ├── operations/        # blend, filters, transform
│   └── core/memory/       # アロケータ、プール管理
├── examples/              # サンプルコード
│   ├── bench/             # ベンチマーク（native/M5Stack両対応）
│   ├── m5stack_basic/     # M5Stack基本サンプル
│   └── m5stack_matte/     # マット合成サンプル
├── demo/                  # WebAssemblyデモ
├── test/                  # doctestベースのテスト
└── docs/                  # 設計ドキュメント
```

### 参照すべきドキュメント

- `fleximg/docs/README.md` - ドキュメント一覧
- `fleximg/docs/ARCHITECTURE.md` - 全体アーキテクチャ
- `fleximg/TODO.md` - 課題・アイデア管理
- `fleximg/CHANGELOG.md` - 変更履歴

## ブランチ運用規則

### 命名規則
- 作業用ブランチは `claude/fleximg` で始める名前を使用する
- 形式: `claude/fleximg-<セッションID>` 

### ワークフロー
1. mainブランチから新しい作業用ブランチを作成
2. 作業量が多い場合は `.work/plans/` に作業計画書を作成する
3. 作業のフェーズ単位で、作業計画書の更新とコミットを行う
4. 作業完了後、必要に応じてドキュメントを更新
5. 作業フェーズ単位のコミットを適切な粒度にまとめてからPRを作成する
6. PRのマージ作業は、ユーザー側が確認して実施する
7. マージが確認されたら、作業用ブランチと作業計画書を削除する

### 作業用フォルダ構成
`.work/` は `.gitignore` に含まれるローカル専用フォルダ

```
.work/
├── plans/    # 作業計画書（Claude用、セッション継続時に参照）
└── reports/  # 分析レポート等（ユーザー用）
```

### コンテキスト継続
セッション継続時（`/compact` 後や新規セッション開始時）、`.work/plans/` 内のドキュメントを確認すること

## ビルド手順

### WebAssembly（Emscripten）

```bash
cd fleximg
source /path/to/emsdk/emsdk_env.sh  # Emscripten環境を読み込み
./build.sh                          # リリースビルド（8bit Straight）
./build.sh --debug                  # デバッグビルド
./build.sh --premul                 # 16bit Premulモード
```

### PlatformIO

```bash
cd fleximg
pio run -e bench_native             # ネイティブベンチマーク
pio run -e basic_m5stack_core2 -t upload  # M5Stack書き込み
```

### テスト

```bash
cd fleximg/test
make all_tests && ./all_tests       # 全テスト実行
```

※ローカル環境固有の設定（emsdkパスなど）は `CLAUDE.local.md` に記載

## コード品質

- 変更前に必ずビルドが通ることを確認
- TODO.md で課題を管理
- コミットメッセージは変更内容を明確に記載

## コーディング規約

詳細は `fleximg/docs/CODING_STYLE.md` を参照。以下は特に重要なポイント:

- **警告オプション**: `-Wall -Wextra -Wpedantic` でクリーンビルドを維持
- **memcpy/memset**: サイズ引数は `static_cast<size_t>(...)` でキャスト
- **ループカウンタ**: 終端変数と型を一致させる
- **配列インデックス**: 型が異なる場合は明示的キャスト
