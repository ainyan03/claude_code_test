// ========================================
// C++ 同期型定義
// ========================================
// このファイルは C++ 側の定義と手動同期が必要です。
// 変更時は必ず対応する C++ ヘッダも確認してください。
//
// 同期対象:
//   NODE_TYPES          ← src/fleximg/core/perf_metrics.h  NodeType
//   PIXEL_FORMATS       ← src/fleximg/image/pixel_format.h BuiltinFormats
//   DEFAULT_PIXEL_FORMAT← src/fleximg/image/pixel_format.h PixelFormatIDs
// ========================================

// ========================================
// ノードタイプ定義
// C++側: src/fleximg/core/perf_metrics.h の NodeType namespace
// ========================================
export const NODE_TYPES = {
    // システム系（パイプライン制御）
    renderer:    { index: 0, name: 'Renderer',    nameJa: 'レンダラー',   category: 'system',    showEfficiency: false },
    source:      { index: 1, name: 'Source',      nameJa: 'ソース',       category: 'source',    showEfficiency: false },
    sink:        { index: 2, name: 'Sink',        nameJa: 'シンク',       category: 'system',    showEfficiency: false },
    distributor: { index: 3, name: 'Distributor', nameJa: '分配',         category: 'system',    showEfficiency: false },
    // 構造系（変換・合成）
    affine:      { index: 4, name: 'Affine',      nameJa: 'アフィン',     category: 'structure', showEfficiency: true },
    composite:   { index: 5, name: 'Composite',   nameJa: '合成',         category: 'structure', showEfficiency: false },
    matte:       { index: 13, name: 'Matte',      nameJa: 'マット合成',   category: 'structure', showEfficiency: false },
    // フィルタ系
    brightness:  { index: 6, name: 'Brightness',  nameJa: '明るさ',       category: 'filter',    showEfficiency: true },
    grayscale:   { index: 7, name: 'Grayscale',   nameJa: 'グレースケール', category: 'filter',  showEfficiency: true },
    alpha:       { index: 9, name: 'Alpha',       nameJa: '透明度',       category: 'filter',    showEfficiency: true },
    horizontalBlur: { index: 10, name: 'HBlur',   nameJa: '水平ぼかし',   category: 'filter',    showEfficiency: true },
    verticalBlur:   { index: 11, name: 'VBlur',   nameJa: '垂直ぼかし',   category: 'filter',    showEfficiency: true },
    // 特殊ソース系
    ninepatch:   { index: 12, name: 'NinePatch',  nameJa: '9パッチ',      category: 'source',    showEfficiency: false },
};

// ========================================
// ピクセルフォーマット定義
// C++側: src/fleximg/image/pixel_format.h の BuiltinFormats
// formatName: C++側の PixelFormatDescriptor::name と一致させる
// ========================================
export const PIXEL_FORMATS = [
    // RGB
    { formatName: 'RGBA8_Straight', displayName: 'RGBA8888',     bpp: 32, description: 'Standard (default)', category: 'RGB' },
    { formatName: 'RGB888',         displayName: 'RGB888',       bpp: 24, description: 'RGB order',          category: 'RGB' },
    { formatName: 'BGR888',         displayName: 'BGR888',       bpp: 24, description: 'BGR order',          category: 'RGB' },
    { formatName: 'RGB565_LE',      displayName: 'RGB565_LE',    bpp: 16, description: 'Little Endian',      category: 'RGB' },
    { formatName: 'RGB565_BE',      displayName: 'RGB565_BE',    bpp: 16, description: 'Big Endian',         category: 'RGB' },
    { formatName: 'RGB332',         displayName: 'RGB332',       bpp: 8,  description: '8-bit color',        category: 'RGB' },
    // Grayscale
    { formatName: 'Grayscale8',     displayName: 'Gray8',        bpp: 8,  description: 'Grayscale 8bit',     category: 'Grayscale' },
    { formatName: 'Grayscale4_MSB', displayName: 'Gray4 (MSB)',  bpp: 4,  description: '4bit, 2px/byte',     category: 'Grayscale' },
    { formatName: 'Grayscale4_LSB', displayName: 'Gray4 (LSB)',  bpp: 4,  description: '4bit, 2px/byte',     category: 'Grayscale' },
    { formatName: 'Grayscale2_MSB', displayName: 'Gray2 (MSB)',  bpp: 2,  description: '2bit, 4px/byte',     category: 'Grayscale' },
    { formatName: 'Grayscale2_LSB', displayName: 'Gray2 (LSB)',  bpp: 2,  description: '2bit, 4px/byte',     category: 'Grayscale' },
    { formatName: 'Grayscale1_MSB', displayName: 'Gray1 (MSB)',  bpp: 1,  description: '1bit, 8px/byte',     category: 'Grayscale' },
    { formatName: 'Grayscale1_LSB', displayName: 'Gray1 (LSB)',  bpp: 1,  description: '1bit, 8px/byte',     category: 'Grayscale' },
    // Alpha
    { formatName: 'Alpha8',         displayName: 'Alpha8',       bpp: 8,  description: 'Alpha only (for matte)', category: 'Alpha' },
    // Index (Palette)
    { formatName: 'Index8',         displayName: 'Index8',       bpp: 8,  description: 'Palette (256色)',    category: 'Index', sinkDisabled: true },
    { formatName: 'Index4_MSB',     displayName: 'Index4 (MSB)', bpp: 4,  description: 'Palette (16色, 2px/byte)', category: 'Index', sinkDisabled: true },
    { formatName: 'Index4_LSB',     displayName: 'Index4 (LSB)', bpp: 4,  description: 'Palette (16色, 2px/byte)', category: 'Index', sinkDisabled: true },
    { formatName: 'Index2_MSB',     displayName: 'Index2 (MSB)', bpp: 2,  description: 'Palette (4色, 4px/byte)', category: 'Index', sinkDisabled: true },
    { formatName: 'Index2_LSB',     displayName: 'Index2 (LSB)', bpp: 2,  description: 'Palette (4色, 4px/byte)', category: 'Index', sinkDisabled: true },
    { formatName: 'Index1_MSB',     displayName: 'Index1 (MSB)', bpp: 1,  description: 'Palette (2色, 8px/byte)', category: 'Index', sinkDisabled: true },
    { formatName: 'Index1_LSB',     displayName: 'Index1 (LSB)', bpp: 1,  description: 'Palette (2色, 8px/byte)', category: 'Index', sinkDisabled: true },
];

// デフォルトピクセルフォーマット
export const DEFAULT_PIXEL_FORMAT = 'RGBA8_Straight';

// ========================================
// ヘルパー関数（上記データに依存）
// ========================================

// フォーマット選択 select 要素を optgroup 付きで構築するヘルパー
export function buildFormatOptions(selectElement, currentFormat, opts = {}) {
    const { disableSinkOnly = false } = opts;
    let currentGroup = null;
    let optgroup = null;
    PIXEL_FORMATS.forEach(fmt => {
        if (fmt.category !== currentGroup) {
            currentGroup = fmt.category;
            optgroup = document.createElement('optgroup');
            optgroup.label = currentGroup;
            selectElement.appendChild(optgroup);
        }
        const option = document.createElement('option');
        option.value = fmt.formatName;
        option.textContent = `${fmt.displayName} (${fmt.bpp}bit)`;
        option.title = fmt.description;
        if (disableSinkOnly && fmt.sinkDisabled) option.disabled = true;
        if (currentFormat === fmt.formatName) option.selected = true;
        optgroup.appendChild(option);
    });
}

// NodeType ヘルパー
export const NodeTypeHelper = {
    // カテゴリでフィルタ
    byCategory: (category) =>
        Object.entries(NODE_TYPES).filter(([_, v]) => v.category === category),

    // インデックスからキーを取得
    keyByIndex: (index) =>
        Object.entries(NODE_TYPES).find(([_, v]) => v.index === index)?.[0],

    // インデックスから定義を取得
    byIndex: (index) =>
        Object.values(NODE_TYPES).find(v => v.index === index),

    // 全ノードタイプ数
    count: () => Object.keys(NODE_TYPES).length,

    // 表示名の配列を取得（インデックス順）
    names: () =>
        Object.values(NODE_TYPES)
            .sort((a, b) => a.index - b.index)
            .map(v => v.name),
};
