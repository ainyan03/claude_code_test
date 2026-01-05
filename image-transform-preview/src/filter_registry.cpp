#include "filter_registry.h"

namespace ImageTransform {

// シングルトンインスタンス取得
FilterRegistry& FilterRegistry::getInstance() {
    static FilterRegistry instance;
    return instance;
}

// コンストラクタ: 標準フィルタを自動登録
FilterRegistry::FilterRegistry() {
    registerBuiltinFilters();
}

// フィルタ登録
void FilterRegistry::registerFilter(const FilterDef& def) {
    filters_[def.id] = def;
}

// フィルタ定義取得
const FilterDef* FilterRegistry::getFilterDef(const std::string& id) const {
    auto it = filters_.find(id);
    return (it != filters_.end()) ? &it->second : nullptr;
}

// 登録済みフィルタIDリスト取得
std::vector<std::string> FilterRegistry::getFilterIds() const {
    std::vector<std::string> ids;
    for (const auto& pair : filters_) {
        ids.push_back(pair.first);
    }
    return ids;
}

// フィルタ生成
std::unique_ptr<ImageFilter> FilterRegistry::createFilter(
    const std::string& id,
    const std::vector<float>& params
) const {
    auto def = getFilterDef(id);
    if (!def || !def->create) {
        return nullptr;
    }
    return def->create(params);
}

// ========================================================================
// 標準フィルタ登録
// 新規フィルタ追加時はここに登録処理を追加
// ========================================================================
void FilterRegistry::registerBuiltinFilters() {
    // ヘルパー: パラメータ取得（範囲外の場合はデフォルト値を返す）
    auto getParam = [](const std::vector<float>& params, size_t index, float defaultVal) -> float {
        return index < params.size() ? params[index] : defaultVal;
    };

    // 明るさフィルタ
    registerFilter({
        "brightness",  // id
        "明るさ",      // name
        {{ "brightness", 0.0f, -1.0f, 1.0f, 0.01f }},  // params
        [getParam](const std::vector<float>& params) -> std::unique_ptr<ImageFilter> {
            BrightnessFilterParams p(getParam(params, 0, 0.0f));
            return std::make_unique<BrightnessFilter>(p);
        }
    });

    // グレースケールフィルタ
    registerFilter({
        "grayscale",      // id
        "グレースケール",  // name
        {},                // params (なし)
        [](const std::vector<float>&) -> std::unique_ptr<ImageFilter> {
            GrayscaleFilterParams p;
            return std::make_unique<GrayscaleFilter>(p);
        }
    });

    // ボックスブラーフィルタ
    registerFilter({
        "blur",    // id
        "ぼかし",  // name
        {{ "radius", 3.0f, 1.0f, 20.0f, 1.0f }},  // params
        [getParam](const std::vector<float>& params) -> std::unique_ptr<ImageFilter> {
            BoxBlurFilterParams p(static_cast<int>(getParam(params, 0, 3.0f)));
            return std::make_unique<BoxBlurFilter>(p);
        }
    });

    // アルファフィルタ
    registerFilter({
        "alpha",     // id
        "アルファ",  // name
        {{ "alpha", 1.0f, 0.0f, 1.0f, 0.01f }},  // params
        [getParam](const std::vector<float>& params) -> std::unique_ptr<ImageFilter> {
            AlphaFilterParams p(getParam(params, 0, 1.0f));
            return std::make_unique<AlphaFilter>(p);
        }
    });
}

} // namespace ImageTransform
