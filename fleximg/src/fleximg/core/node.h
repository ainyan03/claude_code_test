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
    // コピー/ムーブ操作
    // ========================================
    //
    // ノードのコピー/ムーブ時は既存の接続が切断されます。
    // 代入後は新しいノードとして再接続が必要です。
    //

    // デフォルトコンストラクタ
    Node() = default;

    // コピーコンストラクタ: ポート構造のみコピー、接続は引き継がない
    Node(const Node& other)
        : allocator_(nullptr)
    {
        prepareResponse_.status = PrepareStatus::Idle;
        initPorts(static_cast<int>(other.inputs_.size()),
                  static_cast<int>(other.outputs_.size()));
    }

    // ムーブコンストラクタ: ポート構造をムーブし、ownerを修正
    Node(Node&& other) noexcept
        : inputs_(std::move(other.inputs_))
        , outputs_(std::move(other.outputs_))
        , allocator_(nullptr)
    {
        prepareResponse_.status = PrepareStatus::Idle;
        // ownerポインタを自分に修正
        for (auto& port : inputs_) {
            port.owner = this;
        }
        for (auto& port : outputs_) {
            port.owner = this;
        }
    }

    // コピー代入演算子: 既存接続を切断し、ポート構造のみコピー
    Node& operator=(const Node& other) {
        if (this != &other) {
            disconnectAll();
            initPorts(static_cast<int>(other.inputs_.size()),
                      static_cast<int>(other.outputs_.size()));
            prepareResponse_.status = PrepareStatus::Idle;
            allocator_ = nullptr;
        }
        return *this;
    }

    // ムーブ代入演算子: 既存接続を切断し、ポート構造をムーブ後にowner修正
    Node& operator=(Node&& other) noexcept {
        if (this != &other) {
            disconnectAll();
            inputs_ = std::move(other.inputs_);
            outputs_ = std::move(other.outputs_);
            // ownerポインタを自分に修正
            for (auto& port : inputs_) {
                port.owner = this;
            }
            for (auto& port : outputs_) {
                port.owner = this;
            }
            prepareResponse_.status = PrepareStatus::Idle;
            allocator_ = nullptr;
        }
        return *this;
    }

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
    // 接続解除
    // ========================================

    // 全ての入力/出力ポートの接続を解除
    void disconnectAll() {
        for (auto& port : inputs_) {
            port.disconnect();
        }
        for (auto& port : outputs_) {
            port.disconnect();
        }
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
    virtual RenderResponse process(RenderResponse&& input,
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
    virtual RenderResponse pullProcess(const RenderRequest& request) final {
        // 共通処理: スキャンライン処理チェック
        assert(request.height == 1 && "Scanline processing requires height == 1");
        // 共通処理: 準備完了状態チェック
        if (prepareResponse_.status != PrepareStatus::Prepared) {
            return RenderResponse();
        }
        // 派生クラスのカスタム処理を呼び出し
        return onPullProcess(request);
    }

    // 上流へ準備を伝播（finalメソッド）
    // 派生クラスはonPullPrepare()をオーバーライド
    // 戻り値: PrepareResponse（status == Prepared で成功）
    virtual PrepareResponse pullPrepare(const PrepareRequest& request) final {
        // 共通処理: 状態チェック
        bool shouldContinue;
        if (!checkPrepareStatus(shouldContinue)) {
            PrepareResponse errorResult;
            errorResult.status = PrepareStatus::CycleError;
            return errorResult;
        }
        if (!shouldContinue) {
            return prepareResponse_;  // DAG共有ノード: キャッシュを返す
        }
        // 共通処理: アロケータを保持
        allocator_ = request.allocator;

        // 派生クラスのカスタム処理を呼び出し
        PrepareResponse result = onPullPrepare(request);

        // 共通処理: 状態更新・結果キャッシュ
        prepareResponse_ = result;
        return result;
    }

    // 上流へ終了を伝播（finalメソッド）
    // 派生クラスはonPullFinalize()をオーバーライド
    virtual void pullFinalize() final {
        // 共通処理: 循環防止
        if (prepareResponse_.status == PrepareStatus::Idle) {
            return;
        }
        // 共通処理: 状態リセット
        prepareResponse_.status = PrepareStatus::Idle;

        // 派生クラスのカスタム処理を呼び出し
        // 注: allocator_はonPullFinalize()で使用される可能性があるため、
        //     クリアはonPullFinalize()の後に行う
        onPullFinalize();

        // 共通処理: アロケータをクリア
        allocator_ = nullptr;
    }

    // ========================================
    // プッシュ型インターフェース（下流側）- Template Method
    // ========================================

    // 上流から画像を受け取って処理し、下流へ渡す（finalメソッド）
    // 派生クラスはonPushProcess()をオーバーライド
    virtual void pushProcess(RenderResponse&& input, const RenderRequest& request) final {
        // 共通処理: スキャンライン処理チェック
        assert(request.height == 1 && "Scanline processing requires height == 1");
        // 共通処理: 準備完了状態チェック
        if (prepareResponse_.status != PrepareStatus::Prepared) {
            return;
        }
        // 派生クラスのカスタム処理を呼び出し
        onPushProcess(std::move(input), request);
    }

    // 下流へ準備を伝播（finalメソッド）
    // 派生クラスはonPushPrepare()をオーバーライド
    // 戻り値: PrepareResponse（status == Prepared で成功）
    virtual PrepareResponse pushPrepare(const PrepareRequest& request) final {
        // 共通処理: 状態チェック
        bool shouldContinue;
        if (!checkPrepareStatus(shouldContinue)) {
            PrepareResponse errorResult;
            errorResult.status = PrepareStatus::CycleError;
            return errorResult;
        }
        if (!shouldContinue) {
            return prepareResponse_;  // DAG共有ノード: キャッシュを返す
        }
        // 共通処理: アロケータを保持
        allocator_ = request.allocator;

        // 派生クラスのカスタム処理を呼び出し
        PrepareResponse result = onPushPrepare(request);

        // 共通処理: 状態更新・結果キャッシュ
        prepareResponse_ = result;
        return result;
    }

    // 下流へ終了を伝播（finalメソッド）
    // 派生クラスはonPushFinalize()をオーバーライド
    virtual void pushFinalize() final {
        // 共通処理: 循環防止
        if (prepareResponse_.status == PrepareStatus::Idle) {
            return;
        }
        // 共通処理: 状態リセット
        prepareResponse_.status = PrepareStatus::Idle;

        // 派生クラスのカスタム処理を呼び出し
        // 注: allocator_はonPushFinalize()で使用される可能性があるため、
        //     クリアはonPushFinalize()の後に行う
        onPushFinalize();

        // 共通処理: アロケータをクリア
        allocator_ = nullptr;
    }

    // ノード名（デバッグ用）
    virtual const char* name() const { return "Node"; }

    // ========================================
    // メトリクス用ノードタイプ
    // ========================================

    // 派生クラスでオーバーライドしてNodeType::Xxxを返す
    virtual int nodeTypeForMetrics() const { return 0; }

    // ========================================
    // 範囲判定（最適化用）
    // ========================================

    // このノードがrequestに対して提供できるデータ範囲を取得
    // デフォルト: 上流があればパススルー、なければ空（データなし）
    // 派生クラス: 範囲を変更するノード（CompositeNode等）はオーバーライド
    virtual DataRange getDataRange(const RenderRequest& request) const {
        Node* upstream = upstreamNode(0);
        if (upstream) {
            return upstream->getDataRange(request);  // 上流パススルー
        }
        return DataRange{0, 0};  // 上流なしはデータなし
    }

    // prepare応答を取得（派生クラスでの判定用）
    const PrepareResponse& lastPrepareResponse() const { return prepareResponse_; }

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

    // 準備応答キャッシュ（状態 + AABB情報、DAG共有ノード用）
    // status フィールドで循環参照検出にも使用
    PrepareResponse prepareResponse_;

    // RendererNodeから伝播されるアロケータ（prepare時に保持、finalize時にクリア）
    core::memory::IAllocator* allocator_ = nullptr;

    // ========================================
    // Template Method フック（派生クラスでオーバーライド）
    // ========================================

    // pullPrepare()から呼ばれるフック
    // デフォルト: 上流ノードへ伝播し、prepare()を呼び出す
    virtual PrepareResponse onPullPrepare(const PrepareRequest& request) {
        // 上流へ伝播
        Node* upstream = upstreamNode(0);
        if (upstream) {
            PrepareResponse result = upstream->pullPrepare(request);
            if (!result.ok()) {
                return result;
            }
            // 準備処理（PrepareRequestからRenderRequest相当の情報を渡す）
            RenderRequest screenInfo;
            screenInfo.width = request.width;
            screenInfo.height = request.height;
            screenInfo.origin = request.origin;
            prepare(screenInfo);
            return result;  // 上流の結果をパススルー
        }
        // 上流なし: 自身の情報を返す（末端ノード）
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);
        PrepareResponse result;
        result.status = PrepareStatus::Prepared;
        result.width = request.width;
        result.height = request.height;
        result.origin = request.origin;
        return result;
    }

    // pushPrepare()から呼ばれるフック
    // デフォルト: prepare()を呼び出し、下流ノードへ伝播
    virtual PrepareResponse onPushPrepare(const PrepareRequest& request) {
        // 準備処理
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);
        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            PrepareResponse result = downstream->pushPrepare(request);
            return result;  // 下流の結果をパススルー
        }
        // 下流なし: 自身の情報を返す（末端ノード）
        PrepareResponse result;
        result.status = PrepareStatus::Prepared;
        result.width = request.width;
        result.height = request.height;
        result.origin = request.origin;
        return result;
    }

    // pullProcess()から呼ばれるフック
    // デフォルト: 上流からpullしてprocess()を呼び出す
    virtual RenderResponse onPullProcess(const RenderRequest& request) {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResponse();
        RenderResponse input = upstream->pullProcess(request);
        return process(std::move(input), request);
    }

    // pushProcess()から呼ばれるフック
    // デフォルト: process()を呼び出して下流へpush
    virtual void onPushProcess(RenderResponse&& input, const RenderRequest& request) {
        RenderResponse output = process(std::move(input), request);
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
    // prepareResponse_.status を参照・更新する
    bool checkPrepareStatus(bool& shouldContinue);

    // フォーマット変換ヘルパー（メトリクス記録付き）
    ImageBuffer convertFormat(ImageBuffer&& buffer, PixelFormatID target,
                              FormatConversion mode = FormatConversion::CopyIfNeeded);

    // 派生クラス用：ポート初期化
    void initPorts(int inputCount, int outputCount);
};

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
using core::Node;

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {
namespace core {

// ============================================================================
// Node - ヘルパーメソッド実装
// ============================================================================

// 循環参照チェック（pullPrepare/pushPrepare共通）
// prepareResponse_.status を参照・更新する
// 戻り値: true=成功, false=エラー
// shouldContinue: true=処理継続, false=スキップ（Prepared）またはエラー
bool Node::checkPrepareStatus(bool& shouldContinue) {
    PrepareStatus& status = prepareResponse_.status;
    if (status == PrepareStatus::Preparing) {
        status = PrepareStatus::CycleError;
        shouldContinue = false;
        return false;  // 循環参照検出
    }
    if (status == PrepareStatus::Prepared) {
        shouldContinue = false;
        return true;   // 成功（DAG共有、スキップ）
    }
    if (status == PrepareStatus::CycleError) {
        shouldContinue = false;
        return false;  // 既にエラー状態
    }
    status = PrepareStatus::Preparing;
    shouldContinue = true;
    return true;       // 成功（処理継続）
}

// フォーマット変換ヘルパー（メトリクス記録付き）
// 参照モードから所有モードに変わった場合、ノード別統計に記録
// allocator_を使用してバッファを確保する
ImageBuffer Node::convertFormat(ImageBuffer&& buffer, PixelFormatID target,
                                FormatConversion mode) {
    bool wasOwning = buffer.ownsMemory();

    // 参照モードの場合、ノードのallocator_を新バッファ用に渡す
    // 注: setAllocator()で参照バッファのallocator_を変更すると、
    //     デストラクタが非所有メモリを解放しようとするバグがあるため、
    //     toFormat()のallocパラメータで安全に渡す
    core::memory::IAllocator* newAlloc = wasOwning ? nullptr : allocator_;

    ImageBuffer result = std::move(buffer).toFormat(target, mode, newAlloc);

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
void Node::initPorts(int inputCount, int outputCount) {
    inputs_.resize(static_cast<size_t>(inputCount));
    outputs_.resize(static_cast<size_t>(outputCount));
    for (int i = 0; i < inputCount; ++i) {
        inputs_[static_cast<size_t>(i)] = Port(this, i);
    }
    for (int i = 0; i < outputCount; ++i) {
        outputs_[static_cast<size_t>(i)] = Port(this, i);
    }
}

} // namespace core
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_NODE_H
