#ifndef FLEXIMG_DISTRIBUTOR_NODE_H
#define FLEXIMG_DISTRIBUTOR_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// DistributorNode - 分配ノード
// ========================================================================
//
// 1つの入力画像を複数の出力に分配します。
// - 入力: 1ポート
// - 出力: コンストラクタで指定（デフォルト1）
//
// CompositeNode（N入力・1出力）と対称的な構造です。
//
// メモリ管理:
// - 下流には参照モードImageBuffer（ownsMemory()==false）を渡す
// - 下流ノードが変更を加えたい場合はコピーを作成する
// - ImageLibrary → SourceNode の関係と対称的
//
// 使用例:
//   DistributorNode distributor(2);  // 2出力
//   renderer >> distributor;
//   distributor.connectTo(sink1, 0, 0);  // 出力0 → sink1
//   distributor.connectTo(sink2, 0, 1);  // 出力1 → sink2
//

class DistributorNode : public Node {
public:
    explicit DistributorNode(int outputCount = 1) {
        initPorts(1, outputCount);  // 入力1、出力N
    }

    // ========================================
    // 出力管理（CompositeNode::setInputCount と対称）
    // ========================================

    // 出力数を変更（既存接続は維持）
    void setOutputCount(int count) {
        if (count < 1) count = 1;
        outputs_.resize(count);
        for (int i = 0; i < count; ++i) {
            if (outputs_[i].owner == nullptr) {
                outputs_[i] = Port(this, i);
            }
        }
    }

    int outputCount() const {
        return static_cast<int>(outputs_.size());
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "DistributorNode"; }
    int nodeTypeForMetrics() const override { return NodeType::Distributor; }

    // ========================================
    // プッシュ型インターフェース
    // ========================================

    // 下流へ準備を伝播（全出力へ）
    bool pushPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 準備処理
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);

        // 全下流へ伝播
        int numOutputs = outputCount();
        for (int i = 0; i < numOutputs; ++i) {
            Node* downstream = downstreamNode(i);
            if (downstream) {
                if (!downstream->pushPrepare(request)) {
                    pushPrepareState_ = PrepareState::CycleError;
                    return false;
                }
            }
        }

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // 下流へ終了を伝播（全出力へ）
    void pushFinalize() override {
        if (pushPrepareState_ == PrepareState::Idle) {
            return;
        }
        pushPrepareState_ = PrepareState::Idle;

        // 全下流へ伝播
        int numOutputs = outputCount();
        for (int i = 0; i < numOutputs; ++i) {
            Node* downstream = downstreamNode(i);
            if (downstream) {
                downstream->pushFinalize();
            }
        }
        finalize();
    }

    // プッシュ処理: 全出力に参照モードで配信
    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        // 循環エラー状態ならスキップ
        if (pushPrepareState_ != PrepareState::Prepared) {
            return;
        }

        // プッシュ型単一入力: 無効なら処理終了
        if (!input.isValid()) {
            return;
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto distStart = std::chrono::high_resolution_clock::now();
#endif

        int numOutputs = outputCount();
        int validOutputs = 0;

        // 接続されている出力を数える
        for (int i = 0; i < numOutputs; ++i) {
            if (downstreamNode(i)) {
                ++validOutputs;
            }
        }

        if (validOutputs == 0) {
            return;
        }

        // 各出力に参照モードImageBufferを配信
        int processed = 0;
        for (int i = 0; i < numOutputs; ++i) {
            Node* downstream = downstreamNode(i);
            if (!downstream) continue;

            ++processed;

            // 参照モードImageBufferを作成（メモリ解放しない）
            // 最後の出力には元のバッファをmoveで渡す（効率化）
            if (processed < validOutputs) {
                // 参照モード: ViewPortから新しいImageBufferを作成
                RenderResult ref(ImageBuffer(input.buffer.view()), input.origin);
                downstream->pushProcess(std::move(ref), request);
            } else {
                // 最後: 元のバッファをそのまま渡す
                downstream->pushProcess(std::move(input), request);
            }
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto distEnd = std::chrono::high_resolution_clock::now();
        auto& mDist = PerfMetrics::instance().nodes[NodeType::Distributor];
        mDist.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            distEnd - distStart).count();
        mDist.count += validOutputs;
#endif
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_DISTRIBUTOR_NODE_H
