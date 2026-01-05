#ifndef FILTER_REGISTRY_H
#define FILTER_REGISTRY_H

#include "filters.h"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <memory>

namespace ImageTransform {

// フィルタパラメータ定義
struct FilterParamDef {
    std::string name;    // パラメータ名
    float defaultValue;  // デフォルト値
    float minValue;      // 最小値
    float maxValue;      // 最大値
    float step;          // ステップ

    FilterParamDef(const std::string& n = "", float def = 0.0f,
                   float min = 0.0f, float max = 1.0f, float s = 0.01f)
        : name(n), defaultValue(def), minValue(min), maxValue(max), step(s) {}
};

// フィルタ定義
struct FilterDef {
    std::string id;                           // 識別子
    std::string name;                         // 表示名
    std::vector<FilterParamDef> params;       // パラメータ定義
    // ファクトリ関数: パラメータ配列からフィルタインスタンスを生成
    std::function<std::unique_ptr<ImageFilter>(const std::vector<float>&)> create;
};

// ========================================================================
// フィルタレジストリ（シングルトン）
// フィルタ定義を一元管理し、文字列IDからフィルタを生成
// ========================================================================
class FilterRegistry {
public:
    static FilterRegistry& getInstance();

    // フィルタ登録
    void registerFilter(const FilterDef& def);

    // フィルタ定義取得
    const FilterDef* getFilterDef(const std::string& id) const;

    // 登録済みフィルタIDリスト取得
    std::vector<std::string> getFilterIds() const;

    // フィルタ生成
    std::unique_ptr<ImageFilter> createFilter(
        const std::string& id,
        const std::vector<float>& params
    ) const;

private:
    FilterRegistry();
    std::map<std::string, FilterDef> filters_;

    // 標準フィルタを登録
    void registerBuiltinFilters();
};

} // namespace ImageTransform

#endif // FILTER_REGISTRY_H
