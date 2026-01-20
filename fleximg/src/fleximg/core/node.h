#ifndef FLEXIMG_NODE_H
#define FLEXIMG_NODE_H

#include <vector>
#include <cassert>
#include "common.h"
#include "port.h"
#include "perf_metrics.h"
#include "../image/render_types.h"
#include "../image/image_buffer.h"

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// PrepareState - ノードの準備状態（循環参照検出用）
// ========================================================================

enum class PrepareState {
    Idle,       // 未処理（初期状態）
    Preparing,  // 処理中（この状態で再訪問 = 循環参照）
    Prepared,   // 処理完了（この状態で再訪問 = DAG共有、スキップ可）
    CycleError  // 循環参照エラー（processをスキップ）
};

// ========================================================================
// Node - ノード基底クラス
// ========================================================================
//
// パイプラインを構成するノードの基底クラスです。
// - 入力/出力ポートを持つ
// - 接続APIを提供（詳細API、簡易API、演算子）
// - プル型/プッシュ型の両インターフェースをサポート
//
// Template Methodパターン:
// - pullPrepare/pushPrepare/pullProcess/pushProcess/pullFinalize/pushFinalizeは
//   finalメソッドとして共通処理を実行し、派生クラス用のonXxxフックを呼び出す
// - 派生クラスはonXxxメソッドをオーバーライドしてカスタム処理を実装
// - 共通処理（状態管理、allocator保持等）の実装漏れを防止
//
// API:
// - pullProcess(): 上流から画像を取得して処理（プル型）
// - pushProcess(): 下流へ画像を渡す（プッシュ型）
// - process(): 共通処理（派生クラスで実装）
//

class Node {
public:
    virtual ~Node() = default;

    // ========================================
    // ポートアクセス（詳細API）
    // ========================================

    Port* inputPort(int index = 0) {
        return (index >= 0 && index < static_cast<int>(inputs_.size()))
            ? &inputs_[static_cast<size_t>(index)] : nullptr;
    }

    Port* outputPort(int index = 0) {
        return (index >= 0 && index < static_cast<int>(outputs_.size()))
            ? &outputs_[static_cast<size_t>(index)] : nullptr;
    }

    int inputPortCount() const { return static_cast<int>(inputs_.size()); }
    int outputPortCount() const { return static_cast<int>(outputs_.size()); }

    // ========================================
    // 接続API（簡易API）
    // ========================================

    // このノードの出力をtargetの入力に接続
    bool connectTo(Node& target, int targetInputIndex = 0, int outputIndex = 0) {
        Port* out = outputPort(outputIndex);
        Port* in = target.inputPort(targetInputIndex);
        return (out && in) ? out->connect(*in) : false;
    }

    // sourceの出力をこのノードの入力に接続
    bool connectFrom(Node& source, int sourceOutputIndex = 0, int inputIndex = 0) {
        return source.connectTo(*this, inputIndex, sourceOutputIndex);
    }

    // ========================================
    // 演算子（チェーン接続用）
    // ========================================

    // src >> affine >> sink のような記述を可能にする
    Node& operator>>(Node& downstream) {
        connectTo(downstream);
        return downstream;
    }

    Node& operator<<(Node& upstream) {
        connectFrom(upstream);
        return *this;
    }

    // ========================================
    // 新API: 共通処理（派生クラスで実装）
    // ========================================

    // 入力画像から出力画像を生成
    virtual RenderResult process(RenderResult&& input,
                                 const RenderRequest& request) {
        (void)request;
        return std::move(input);  // デフォルトはパススルー
    }

    // 準備処理（スクリーン情報を受け取る）
    virtual void prepare(const RenderRequest& screenInfo) {
        (void)screenInfo;
    }

    // 終了処理
    virtual void finalize() {
        // デフォルトは何もしない
    }

    // ========================================
    // プル型インターフェース（上流側）- Template Method
    // ========================================

    // 上流から画像を取得して処理（finalメソッド）
    // 派生クラスはonPullProcess()をオーバーライド
    virtual RenderResult pullProcess(const RenderRequest& request) final {
        // 共通処理: スキャンライン処理チェック
        assert(request.height == 1 && "Scanline processing requires height == 1");
        // 共通処理: 循環エラー状態チェック
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }
        // 派生クラスのカスタム処理を呼び出し
        return onPullProcess(request);
    }

    // 上流へ準備を伝播（finalメソッド）
    // 派生クラスはonPullPrepare()をオーバーライド
    // 戻り値: true = 成功、false = 循環参照検出
    virtual bool pullPrepare(const PrepareRequest& request) final {
        // 共通処理: 状態チェック
        bool shouldContinue;
        if (!checkPrepareState(pullPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }
        // 共通処理: アロケータを保持
        allocator_ = request.allocator;

        // 派生クラスのカスタム処理を呼び出し
        bool result = onPullPrepare(request);

        // 共通処理: 状態更新
        pullPrepareState_ = result ? PrepareState::Prepared : PrepareState::CycleError;
        return result;
    }

    // 上流へ終了を伝播（finalメソッド）
    // 派生クラスはonPullFinalize()をオーバーライド
    virtual void pullFinalize() final {
        // 共通処理: 循環防止
        if (pullPrepareState_ == PrepareState::Idle) {
            return;
        }
        // 共通処理: 状態リセット
        pullPrepareState_ = PrepareState::Idle;
        // 共通処理: アロケータをクリア
        allocator_ = nullptr;

        // 派生クラスのカスタム処理を呼び出し
        onPullFinalize();
    }

    // ========================================
    // プッシュ型インターフェース（下流側）- Template Method
    // ========================================

    // 上流から画像を受け取って処理し、下流へ渡す（finalメソッド）
    // 派生クラスはonPushProcess()をオーバーライド
    virtual void pushProcess(RenderResult&& input, const RenderRequest& request) final {
        // 共通処理: スキャンライン処理チェック
        assert(request.height == 1 && "Scanline processing requires height == 1");
        // 共通処理: 循環エラー状態チェック
        if (pushPrepareState_ != PrepareState::Prepared) {
            return;
        }
        // 派生クラスのカスタム処理を呼び出し
        onPushProcess(std::move(input), request);
    }

    // 下流へ準備を伝播（finalメソッド）
    // 派生クラスはonPushPrepare()をオーバーライド
    // 戻り値: true = 成功、false = 循環参照検出
    virtual bool pushPrepare(const PrepareRequest& request) final {
        // 共通処理: 状態チェック
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }
        // 共通処理: アロケータを保持
        allocator_ = request.allocator;

        // 派生クラスのカスタム処理を呼び出し
        bool result = onPushPrepare(request);

        // 共通処理: 状態更新
        pushPrepareState_ = result ? PrepareState::Prepared : PrepareState::CycleError;
        return result;
    }

    // 下流へ終了を伝播（finalメソッド）
    // 派生クラスはonPushFinalize()をオーバーライド
    virtual void pushFinalize() final {
        // 共通処理: 循環防止
        if (pushPrepareState_ == PrepareState::Idle) {
            return;
        }
        // 共通処理: 状態リセット
        pushPrepareState_ = PrepareState::Idle;
        // 共通処理: アロケータをクリア
        allocator_ = nullptr;

        // 派生クラスのカスタム処理を呼び出し
        onPushFinalize();
    }

    // ノード名（デバッグ用）
    virtual const char* name() const { return "Node"; }

    // ========================================
    // メトリクス用ノードタイプ
    // ========================================

    // 派生クラスでオーバーライドしてNodeType::Xxxを返す
    virtual int nodeTypeForMetrics() const { return 0; }

    // ========================================
    // ノードアクセス
    // ========================================

    // 上流ノードを取得（入力ポート経由）
    Node* upstreamNode(int inputIndex = 0) const {
        if (inputIndex < 0 || inputIndex >= static_cast<int>(inputs_.size())) {
            return nullptr;
        }
        return inputs_[static_cast<size_t>(inputIndex)].connectedNode();
    }

    // 下流ノードを取得（出力ポート経由）
    Node* downstreamNode(int outputIndex = 0) const {
        if (outputIndex < 0 || outputIndex >= static_cast<int>(outputs_.size())) {
            return nullptr;
        }
        return outputs_[static_cast<size_t>(outputIndex)].connectedNode();
    }

    // ========================================
    // アロケータアクセス
    // ========================================

    // prepare時に設定されたアロケータを取得
    // 設定されていない場合はnullptrを返す
    core::memory::IAllocator* allocator() const { return allocator_; }

protected:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;

    // 循環参照検出用状態
    PrepareState pullPrepareState_ = PrepareState::Idle;
    PrepareState pushPrepareState_ = PrepareState::Idle;

    // RendererNodeから伝播されるアロケータ（prepare時に保持、finalize時にクリア）
    core::memory::IAllocator* allocator_ = nullptr;

    // ========================================
    // Template Method フック（派生クラスでオーバーライド）
    // ========================================

    // pullPrepare()から呼ばれるフック
    // デフォルト: 上流ノードへ伝播し、prepare()を呼び出す
    virtual bool onPullPrepare(const PrepareRequest& request) {
        // 上流へ伝播
        Node* upstream = upstreamNode(0);
        if (upstream) {
            if (!upstream->pullPrepare(request)) {
                return false;
            }
        }
        // 準備処理（PrepareRequestからRenderRequest相当の情報を渡す）
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);
        return true;
    }

    // pushPrepare()から呼ばれるフック
    // デフォルト: prepare()を呼び出し、下流ノードへ伝播
    virtual bool onPushPrepare(const PrepareRequest& request) {
        // 準備処理
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);
        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(request)) {
                return false;
            }
        }
        return true;
    }

    // pullProcess()から呼ばれるフック
    // デフォルト: 上流からpullしてprocess()を呼び出す
    virtual RenderResult onPullProcess(const RenderRequest& request) {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();
        RenderResult input = upstream->pullProcess(request);
        return process(std::move(input), request);
    }

    // pushProcess()から呼ばれるフック
    // デフォルト: process()を呼び出して下流へpush
    virtual void onPushProcess(RenderResult&& input, const RenderRequest& request) {
        RenderResult output = process(std::move(input), request);
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(output), request);
        }
    }

    // pullFinalize()から呼ばれるフック
    // デフォルト: finalize()を呼び出し、上流へ伝播
    virtual void onPullFinalize() {
        finalize();
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullFinalize();
        }
    }

    // pushFinalize()から呼ばれるフック
    // デフォルト: 下流へ伝播し、finalize()を呼び出す
    virtual void onPushFinalize() {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }
        finalize();
    }

    // ========================================
    // ヘルパーメソッド
    // ========================================

    // 循環参照チェック（pullPrepare/pushPrepare共通）
    // 戻り値: true=成功, false=エラー
    // shouldContinue: true=処理継続, false=スキップ（Prepared）またはエラー
    bool checkPrepareState(PrepareState& state, bool& shouldContinue) {
        if (state == PrepareState::Preparing) {
            state = PrepareState::CycleError;
            shouldContinue = false;
            return false;  // 循環参照検出
        }
        if (state == PrepareState::Prepared) {
            shouldContinue = false;
            return true;   // 成功（DAG共有、スキップ）
        }
        if (state == PrepareState::CycleError) {
            shouldContinue = false;
            return false;  // 既にエラー状態
        }
        state = PrepareState::Preparing;
        shouldContinue = true;
        return true;       // 成功（処理継続）
    }

    // フォーマット変換ヘルパー（メトリクス記録付き）
    // 参照モードから所有モードに変わった場合、ノード別統計に記録
    // allocator_を使用してバッファを確保する
    ImageBuffer convertFormat(ImageBuffer&& buffer, PixelFormatID target,
                              FormatConversion mode = FormatConversion::CopyIfNeeded) {
        bool wasOwning = buffer.ownsMemory();

        // 参照モードのバッファにノードのallocator_を設定してから変換
        // これにより、toFormat()内で新バッファ作成時にallocator_が使われる
        if (!wasOwning && allocator_) {
            buffer.setAllocator(allocator_);
        }

        ImageBuffer result = std::move(buffer).toFormat(target, mode);

        // 参照→所有モードへの変換時にメトリクス記録
        if (!wasOwning && result.ownsMemory()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().nodes[nodeTypeForMetrics()].recordAlloc(
                result.totalBytes(), result.width(), result.height());
#endif
        }
        return result;
    }

    // 派生クラス用：ポート初期化
    void initPorts(int inputCount, int outputCount) {
        inputs_.resize(static_cast<size_t>(inputCount));
        outputs_.resize(static_cast<size_t>(outputCount));
        for (int i = 0; i < inputCount; ++i) {
            inputs_[static_cast<size_t>(i)] = Port(this, i);
        }
        for (int i = 0; i < outputCount; ++i) {
            outputs_[static_cast<size_t>(i)] = Port(this, i);
        }
    }
};

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
using core::PrepareState;
using core::Node;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NODE_H
