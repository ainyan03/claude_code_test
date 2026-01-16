// グローバル変数
let graphEvaluator;  // ノードグラフ評価エンジン（C++側）
let canvas;
let ctx;

// コンテンツライブラリ（入力画像・出力バッファ統合管理）
let contentLibrary = [];
let nextContentId = 1;
let nextCppImageId = 1;  // C++側に渡す数値ID
let focusedContentId = null;

// コンテンツタイプ定義
const CONTENT_TYPES = {
    image: { icon: '🖼️', label: '画像', buttonLabel: '+Source' },
    output: { icon: '📤', label: '出力', buttonLabel: '+Sink' }
};

let canvasWidth = 800;
let canvasHeight = 600;
let canvasOrigin = { x: 400, y: 300 };  // キャンバス原点（ピクセル座標）
let previewScale = 1;  // 表示倍率（1〜5）
let isResetting = false;  // リセット中フラグ（beforeunloadで保存をスキップ）

// スキャンライン処理設定（Rendererノードで管理、グローバルはフォールバック用）
// ※ v2.30.0以降、パイプライン上のリクエストは必ずheight=1（スキャンライン）
let tileWidth = 0;       // 0 = 横方向は分割なし
let tileHeight = 0;      // 内部的に常に1として扱われる（互換性のため変数は維持）
let debugCheckerboard = false;

// ========================================
// ノードタイプ定義（一元管理）
// C++側の NodeType enum と同期を維持すること
// ========================================
const NODE_TYPES = {
    // システム系（パイプライン制御）
    renderer:    { index: 0, name: 'Renderer',    nameJa: 'レンダラー',   category: 'system',    showEfficiency: false },
    source:      { index: 1, name: 'Source',      nameJa: 'ソース',       category: 'source',    showEfficiency: false },
    sink:        { index: 2, name: 'Sink',        nameJa: 'シンク',       category: 'system',    showEfficiency: false },
    distributor: { index: 3, name: 'Distributor', nameJa: '分配',         category: 'system',    showEfficiency: false },
    // 構造系（変換・合成）
    affine:      { index: 4, name: 'Affine',      nameJa: 'アフィン',     category: 'structure', showEfficiency: true },
    composite:   { index: 5, name: 'Composite',   nameJa: '合成',         category: 'structure', showEfficiency: false },
    // フィルタ系
    brightness:  { index: 6, name: 'Brightness',  nameJa: '明るさ',       category: 'filter',    showEfficiency: true },
    grayscale:   { index: 7, name: 'Grayscale',   nameJa: 'グレースケール', category: 'filter',  showEfficiency: true },
    boxBlur:     { index: 8, name: 'BoxBlur',     nameJa: 'ぼかし',       category: 'filter',    showEfficiency: true },
    alpha:       { index: 9, name: 'Alpha',       nameJa: '透明度',       category: 'filter',    showEfficiency: true },
    horizontalBlur: { index: 10, name: 'HBlur',   nameJa: '水平ぼかし',   category: 'filter',    showEfficiency: true },
    verticalBlur:   { index: 11, name: 'VBlur',   nameJa: '垂直ぼかし',   category: 'filter',    showEfficiency: true },
    // 特殊ソース系
    ninepatch:   { index: 12, name: 'NinePatch',  nameJa: '9パッチ',      category: 'source',    showEfficiency: false },
};

// ========================================
// ピクセルフォーマット定義
// C++側の BuiltinFormats と同期を維持すること
// formatName: C++側の PixelFormatDescriptor::name と一致させる
// ========================================
const PIXEL_FORMATS = [
    { formatName: 'RGBA8_Straight',        displayName: 'RGBA8888',   bpp: 4, description: 'Standard (default)' },
    { formatName: 'RGB888',                displayName: 'RGB888',     bpp: 3, description: 'RGB order' },
    { formatName: 'BGR888',                displayName: 'BGR888',     bpp: 3, description: 'BGR order' },
    { formatName: 'RGB565_LE',             displayName: 'RGB565_LE',  bpp: 2, description: 'Little Endian' },
    { formatName: 'RGB565_BE',             displayName: 'RGB565_BE',  bpp: 2, description: 'Big Endian' },
    { formatName: 'RGB332',                displayName: 'RGB332',     bpp: 1, description: '8-bit color' },
];

// デフォルトピクセルフォーマット
const DEFAULT_PIXEL_FORMAT = 'RGBA8_Straight';

// ヘルパー関数
const NodeTypeHelper = {
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

// スクロールマネージャーのインスタンス
let previewScrollManager = null;
let nodeGraphScrollManager = null;

// ========================================
// スクロールマネージャー（共通処理）
// コンテナのスクロール位置を比率で管理し、リサイズ時に維持する
// ========================================
function createScrollManager(containerSelector, options = {}) {
    const container = document.querySelector(containerSelector);
    if (!container) return null;

    const initialX = options.initialX ?? 0.5;
    const initialY = options.initialY ?? 0.5;
    let ratio = { x: initialX, y: initialY };

    // 現在のスクロール位置から比率を保存
    function saveRatio() {
        const scrollableWidth = container.scrollWidth - container.clientWidth;
        const scrollableHeight = container.scrollHeight - container.clientHeight;

        if (scrollableWidth > 0) {
            ratio.x = container.scrollLeft / scrollableWidth;
        }
        if (scrollableHeight > 0) {
            ratio.y = container.scrollTop / scrollableHeight;
        }
    }

    // 保存された比率に基づいてスクロール位置を適用
    function applyRatio() {
        const scrollableWidth = container.scrollWidth - container.clientWidth;
        const scrollableHeight = container.scrollHeight - container.clientHeight;

        if (scrollableWidth > 0) {
            container.scrollLeft = scrollableWidth * ratio.x;
        }
        if (scrollableHeight > 0) {
            container.scrollTop = scrollableHeight * ratio.y;
        }
    }

    // 比率を直接設定
    function setRatio(x, y) {
        ratio.x = x;
        ratio.y = y;
        applyRatio();
    }

    // 現在の比率を取得
    function getRatio() {
        return { ...ratio };
    }

    // イベントリスナーを設定
    container.addEventListener('scroll', saveRatio);

    const resizeObserver = new ResizeObserver(() => {
        applyRatio();
    });
    resizeObserver.observe(container);

    window.addEventListener('resize', () => {
        applyRatio();
    });

    // 初期スクロール位置を適用
    applyRatio();

    return { saveRatio, applyRatio, setRatio, getRatio, container };
}

// ========================================
// フィルタ定義（一元管理）
// 新規フィルタ追加時はここに定義を追加するだけでOK
// ========================================
const FILTER_DEFINITIONS = {
    grayscale: {
        id: 'grayscale',
        name: 'グレースケール',
        category: 'color',
        params: []  // パラメータなし
    },
    brightness: {
        id: 'brightness',
        name: '明るさ',
        category: 'color',
        params: [
            {
                name: 'brightness',
                label: '明るさ',
                min: -1,
                max: 1,
                step: 0.01,
                default: 0,
                format: v => v.toFixed(2)
            }
        ]
    },
    blur: {
        id: 'blur',
        name: 'ぼかし',
        category: 'blur',
        params: [
            {
                name: 'radius',
                label: '半径',
                min: 0,
                max: 20,
                step: 1,
                default: 3,
                format: v => `${Math.round(v)}px`
            }
        ]
    },
    alpha: {
        id: 'alpha',
        name: 'アルファ',
        category: 'other',
        params: [
            {
                name: 'alpha',
                label: 'α',
                min: 0,
                max: 1,
                step: 0.01,
                default: 1,
                format: v => `${Math.round(v * 100)}%`
            }
        ]
    },
    horizontalBlur: {
        id: 'horizontalBlur',
        name: '水平ぼかし',
        category: 'blur',
        params: [
            {
                name: 'radius',
                label: '半径',
                min: 0,
                max: 20,
                step: 1,
                default: 3,
                format: v => `${Math.round(v)}px`
            }
        ]
    },
    verticalBlur: {
        id: 'verticalBlur',
        name: '垂直ぼかし',
        category: 'blur',
        params: [
            {
                name: 'radius',
                label: '半径',
                min: 0,
                max: 20,
                step: 1,
                default: 3,
                format: v => `${Math.round(v)}px`
            }
        ]
    }
    // 新規フィルタはここに追加
    // contrast: { id: 'contrast', name: 'コントラスト', category: 'color', params: [...] }
};

// グローバルノードグラフ
let globalNodes = [];  // すべてのノード（画像、フィルタ、合成、出力）を管理
let globalConnections = [];  // ノード間の接続
let nextGlobalNodeId = 1;
let nextCompositeId = 1;
let nextDistributorId = 1;
let nextIndependentFilterId = 1;
let nextImageNodeId = 1;
let nodeGraphSvg = null;

// ドラッグ接続用の状態
let isDraggingConnection = false;
let dragConnectionFrom = null;
let dragConnectionPath = null;

// ========================================
// ノード配置ヘルパー関数
// ========================================

// ノードグラフの表示範囲の中央座標を取得
function getVisibleNodeGraphCenter() {
    const container = document.querySelector('.node-graph-canvas-container');
    if (!container) {
        return { x: 800, y: 600 };
    }
    return {
        x: container.scrollLeft + container.clientWidth / 2,
        y: container.scrollTop + container.clientHeight / 2
    };
}

// ランダムオフセットを生成（±range の範囲）
function randomOffset(range = 16) {
    return Math.round((Math.random() - 0.5) * range * 2);
}

// ノードをアニメーション付きで移動
// node: 移動するノード
// targetX, targetY: 目標位置
// duration: アニメーション時間（ms）
function animateNodeMove(node, targetX, targetY, duration = 300) {
    const startX = node.posX;
    const startY = node.posY;
    const startTime = performance.now();

    function update(currentTime) {
        const elapsed = currentTime - startTime;
        const progress = Math.min(elapsed / duration, 1);

        // ease-out: 最初速く、終わりに減速
        const eased = 1 - Math.pow(1 - progress, 3);

        node.posX = Math.round(startX + (targetX - startX) * eased);
        node.posY = Math.round(startY + (targetY - startY) * eased);

        // SVG内のforeignObjectを直接更新（renderNodeGraphを呼ばずに）
        const foreignObject = nodeGraphSvg?.querySelector(
            `foreignObject:has(.node-box[data-node-id="${node.id}"])`
        );
        if (foreignObject) {
            foreignObject.setAttribute('x', node.posX);
            foreignObject.setAttribute('y', node.posY);
            updateNodePortsPosition(node);
            updateConnectionsForNode(node.id);
        }

        if (progress < 1) {
            requestAnimationFrame(update);
        }
    }

    requestAnimationFrame(update);
}

// 新規ノード追加時に既存ノードを押し出す
// newX, newY: 新規ノードの位置
// newWidth, newHeight: 新規ノードのサイズ
function pushExistingNodes(newX, newY, newWidth = 160, newHeight = 70) {
    const margin = 5; // 押し出し判定マージン（小さめ）
    const newCenterX = newX + newWidth / 2;
    const newCenterY = newY + newHeight / 2;

    for (const node of globalNodes) {
        const otherWidth = 160;
        const otherHeight = getNodeHeight(node);

        // 矩形の重なりチェック
        const overlapX = (newX < node.posX + otherWidth + margin) && (newX + newWidth + margin > node.posX);
        const overlapY = (newY < node.posY + otherHeight + margin) && (newY + newHeight + margin > node.posY);

        if (overlapX && overlapY) {
            // 既存ノードを新規ノードから離れる方向に押し出す
            const otherCenterX = node.posX + otherWidth / 2;
            const otherCenterY = node.posY + otherHeight / 2;

            let dx = otherCenterX - newCenterX;
            let dy = otherCenterY - newCenterY;

            // 方向が0の場合はデフォルトで下方向に押し出す
            if (dx === 0 && dy === 0) {
                dy = 1;
            }

            // 正規化して少しだけ押し出し（重なりOKなので控えめ）
            const len = Math.sqrt(dx * dx + dy * dy);
            const pushAmount = 50; // 押し出し量（控えめ）
            const targetX = Math.max(0, Math.round(node.posX + (dx / len) * pushAmount));
            const targetY = Math.max(0, Math.round(node.posY + (dy / len) * pushAmount));

            // アニメーション付きで移動（renderNodeGraph後に実行されるようにsetTimeout）
            const nodeRef = node;
            setTimeout(() => animateNodeMove(nodeRef, targetX, targetY), 50);
        }
    }
}

// requestAnimationFrame用のフラグ
let updatePreviewScheduled = false;

// 初期化フラグ（イベントリスナーの重複登録防止）
let appInitialized = false;

// rAFベースのスロットル付きプレビュー更新（より高速）
function throttledUpdatePreview() {
    if (updatePreviewScheduled) {
        return; // 既に更新がスケジュールされている
    }

    updatePreviewScheduled = true;
    requestAnimationFrame(() => {
        updatePreviewFromGraph();
        updatePreviewScheduled = false;
    });
}

// サイドバー開閉ロジック
function setupSidebar() {
    const sidebar = document.getElementById('sidebar');
    const toggle = document.getElementById('sidebar-toggle');
    const overlay = document.getElementById('sidebar-overlay');

    function openSidebar() {
        sidebar.classList.add('open');
        toggle.classList.add('open');
        overlay.classList.add('visible');
        document.body.classList.add('sidebar-open');
        localStorage.setItem('sidebarOpen', 'true');
    }

    function closeSidebar() {
        sidebar.classList.remove('open');
        toggle.classList.remove('open');
        overlay.classList.remove('visible');
        document.body.classList.remove('sidebar-open');
        localStorage.setItem('sidebarOpen', 'false');
    }

    function toggleSidebar() {
        if (sidebar.classList.contains('open')) {
            closeSidebar();
        } else {
            openSidebar();
        }
    }

    // トグルボタンクリック
    toggle.addEventListener('click', toggleSidebar);

    // オーバーレイクリックで閉じる（モバイル時）
    overlay.addEventListener('click', closeSidebar);

    // ESCキーで閉じる
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && sidebar.classList.contains('open')) {
            closeSidebar();
        }
    });

    // 初期状態を復元（デフォルトは開く）
    const savedSidebarState = localStorage.getItem('sidebarOpen');
    if (savedSidebarState === 'false') {
        closeSidebar();
    } else {
        openSidebar();
    }

    // アコーディオンのセットアップ
    setupSidebarAccordion();
}

// サイドバーアコーディオン切り替えロジック
function setupSidebarAccordion() {
    const accordionItems = document.querySelectorAll('.sidebar-accordion-item');

    // アコーディオンを開く関数
    function openAccordion(item) {
        // 他のアイテムを閉じる
        accordionItems.forEach(otherItem => {
            if (otherItem !== item) {
                otherItem.classList.remove('active');
                const icon = otherItem.querySelector('.sidebar-accordion-icon');
                if (icon) icon.textContent = '▶';
            }
        });

        // 指定されたアイテムを開く
        item.classList.add('active');
        const icon = item.querySelector('.sidebar-accordion-icon');
        if (icon) icon.textContent = '▼';

        // 状態を保存
        const accordionId = item.dataset.accordion;
        if (accordionId) {
            localStorage.setItem('sidebarAccordion', accordionId);
        }
    }

    accordionItems.forEach(item => {
        const header = item.querySelector('.sidebar-accordion-header');

        header.addEventListener('click', () => {
            // 既にアクティブなら何もしない（常に1つは開いている）
            if (item.classList.contains('active')) {
                return;
            }
            openAccordion(item);
        });
    });

    // 初期状態を復元
    const savedAccordion = localStorage.getItem('sidebarAccordion');
    if (savedAccordion) {
        const targetItem = document.querySelector(`.sidebar-accordion-item[data-accordion="${savedAccordion}"]`);
        if (targetItem) {
            openAccordion(targetItem);
        }
    }
}

// スプリッターによるリサイズ処理
// 比率ベースで管理し、画面回転時も追従する
let splitterRatio = null;  // ノードグラフセクションの比率（0〜1）、nullは未操作状態

function setupSplitter() {
    const splitter = document.getElementById('splitter');
    const nodeGraphSection = document.querySelector('.node-graph-section');
    const mainContent = document.querySelector('.main-content');
    const container = document.querySelector('.container');
    const header = document.querySelector('header');

    let isDragging = false;
    let startY = 0;
    let startRatio = 0;

    // 利用可能な高さを計算（ヘッダーとスプリッター分を除く）
    function getAvailableHeight() {
        const containerHeight = container.offsetHeight;
        const headerHeight = header ? header.offsetHeight : 0;
        const splitterHeight = splitter.offsetHeight;
        return containerHeight - headerHeight - splitterHeight;
    }

    // 比率に基づいてflex値を適用
    function applyRatio(ratio) {
        if (ratio === null) return;  // 未操作時は何もしない（CSSのflex: 1が有効）

        const availableHeight = getAvailableHeight();
        const minHeight = 150;

        // 最小高さを確保した比率に補正
        const minRatio = minHeight / availableHeight;
        const maxRatio = 1 - minRatio;
        const clampedRatio = Math.max(minRatio, Math.min(maxRatio, ratio));

        const nodeGraphHeight = Math.round(availableHeight * clampedRatio);
        const mainContentHeight = availableHeight - nodeGraphHeight;

        nodeGraphSection.style.flex = `0 0 ${nodeGraphHeight}px`;
        mainContent.style.flex = `0 0 ${mainContentHeight}px`;
    }

    function onMouseDown(e) {
        isDragging = true;
        startY = e.clientY || e.touches?.[0]?.clientY;

        // 現在の比率を計算
        const availableHeight = getAvailableHeight();
        startRatio = nodeGraphSection.offsetHeight / availableHeight;

        splitter.classList.add('dragging');
        document.body.style.cursor = 'row-resize';
        document.body.style.userSelect = 'none';
        e.preventDefault();
    }

    function onMouseMove(e) {
        if (!isDragging) return;

        const clientY = e.clientY || e.touches?.[0]?.clientY;
        const deltaY = clientY - startY;
        const availableHeight = getAvailableHeight();

        // ピクセル差分を比率差分に変換
        const deltaRatio = deltaY / availableHeight;
        splitterRatio = startRatio + deltaRatio;

        // 比率を適用
        applyRatio(splitterRatio);
    }

    function onMouseUp() {
        if (!isDragging) return;
        isDragging = false;
        splitter.classList.remove('dragging');
        document.body.style.cursor = '';
        document.body.style.userSelect = '';
    }

    // マウスイベント
    splitter.addEventListener('mousedown', onMouseDown);
    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onMouseUp);

    // タッチイベント（モバイル対応）
    splitter.addEventListener('touchstart', onMouseDown, { passive: false });
    document.addEventListener('touchmove', onMouseMove, { passive: false });
    document.addEventListener('touchend', onMouseUp);

    // 画面リサイズ・回転時に比率を再適用
    function onResize() {
        if (splitterRatio !== null) {
            applyRatio(splitterRatio);
        }
    }

    window.addEventListener('resize', onResize);
    window.addEventListener('orientationchange', () => {
        // orientationchange後にレイアウトが確定するまで少し待つ
        setTimeout(onResize, 100);
    });
}

// WebAssemblyモジュールを初期化
// MODULARIZE=1を使用しているため、Moduleは関数としてエクスポートされる
if (typeof Module === 'function') {
    console.log('Initializing WebAssembly module...');
    Module({
        onRuntimeInitialized: function() {
            console.log('WebAssembly loaded successfully');
            // thisはModuleインスタンス
            window.WasmModule = this;
            initializeApp();
        }
    });
} else {
    console.error('Module function not found');
}

// WebAssembly読み込みタイムアウト（10秒）
setTimeout(() => {
    if (!graphEvaluator) {
        console.error('WebAssembly loading timeout');
        const loadingEl = document.getElementById('loading');
        if (loadingEl) {
            loadingEl.innerHTML = '<div class="spinner"></div><p style="color: #ff6b6b;">WebAssemblyの読み込みに失敗しました。<br>ページを再読み込みしてください。</p>';
        }
    }
}, 10000);

function initializeApp() {
    // 初期化フラグで重複実行を防止
    if (appInitialized) {
        console.log('App already initialized');
        return;
    }
    appInitialized = true;

    console.log('Initializing app...');

    // ページ離脱時に状態を保存（リロード直前の状態を確実に保存）
    // ただしリセット中は保存をスキップ
    window.addEventListener('beforeunload', () => {
        if (!isResetting) {
            saveStateToLocalStorage();
        }
    });

    // バージョン情報を表示
    displayVersionInfo();

    // ローディング非表示
    const loadingEl = document.getElementById('loading');
    if (loadingEl) {
        loadingEl.classList.add('hidden');
    }

    // キャンバス初期化
    canvas = document.getElementById('preview-canvas');
    ctx = canvas.getContext('2d');
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    // 補間なしで表示（ピクセル確認用）
    updateCanvasDisplayScale();
    canvas.style.imageRendering = 'pixelated';

    // NodeGraphEvaluator初期化（WebAssemblyモジュール）
    if (typeof WasmModule !== 'undefined' && WasmModule.NodeGraphEvaluator) {
        graphEvaluator = new WasmModule.NodeGraphEvaluator(canvasWidth, canvasHeight);
        graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);
        console.log('NodeGraphEvaluator initialized');
    } else {
        console.error('WebAssembly module not loaded!', typeof WasmModule);
        alert('エラー: WebAssemblyモジュールの読み込みに失敗しました。ページを再読み込みしてください。');
        return;
    }

    // デバッグセクションを動的生成
    initDebugDetailsSection();

    // イベントリスナー設定
    setupEventListeners();

    // グローバルノードグラフ初期化
    initializeNodeGraph();

    // 状態復元を試みる（URLパラメータ優先、次にLocalStorage）
    tryRestoreState();
}

// 状態復元を試みる
async function tryRestoreState() {
    // URLパラメータから状態を取得
    let state = getStateFromURL();
    let stateSource = 'URL';

    // URLパラメータがなければLocalStorageから取得
    if (!state) {
        state = loadStateFromLocalStorage();
        stateSource = 'LocalStorage';
    }

    if (state) {
        console.log(`Restoring state from ${stateSource}...`);
        const restored = await restoreAppState(state);
        if (restored) {
            console.log('App initialized with restored state');
            return;
        }
    }

    // 状態が復元できなかった場合はデフォルト初期化
    console.log('No saved state found, initializing with defaults');
    initDefaultState();  // generateTestPatterns()を内部で呼び出し
    updatePreviewFromGraph();
    console.log('App initialized successfully');
}

// デフォルト状態を初期化
function initDefaultState() {
    // コンテンツライブラリ: デフォルト出力バッファを2つ用意
    // cppImageId: 1, 2 は出力バッファ用、3以降は入力画像用
    contentLibrary = [
        {
            id: 'out-1',
            type: 'output',
            name: 'LCD 320x240',
            width: 320,
            height: 240,
            cppImageId: 1,
            imageData: null
        },
        {
            id: 'out-2',
            type: 'output',
            name: 'LCD 960x540',
            width: 960,
            height: 540,
            cppImageId: 2,
            imageData: null
        }
    ];
    nextContentId = 3;
    nextCppImageId = 3;  // 1,2は出力バッファ用
    focusedContentId = 'out-1';  // 最初の出力にフォーカス

    // ノード: Rendererのみ（Sinkノードはなし）
    // ノードグラフ中央（viewBox: 1600x1200）にRendererを配置
    globalNodes = [
        {
            id: 'renderer',
            type: 'renderer',
            title: 'Renderer',
            posX: 700,
            posY: 550,
            virtualWidth: 1920,
            virtualHeight: 1080,
            originX: 960,
            originY: 540,
            tileWidth: 0,      // 横方向は分割なし（height は内部的に常に1）
            tileHeight: 0
        }
    ];

    // 接続: なし（Sinkがないため）
    globalConnections = [];

    // RendererノードのパラメータをC++側に同期
    const rendererNode = globalNodes[0];
    canvasWidth = rendererNode.virtualWidth;
    canvasHeight = rendererNode.virtualHeight;
    canvasOrigin.x = rendererNode.originX;
    canvasOrigin.y = rendererNode.originY;
    tileWidth = rendererNode.tileWidth;
    tileHeight = rendererNode.tileHeight;

    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    graphEvaluator.setCanvasSize(canvasWidth, canvasHeight);
    graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);
    graphEvaluator.setTileSize(tileWidth, tileHeight);
    updateCanvasDisplayScale();

    // UI更新
    renderContentLibrary();
    renderNodeGraph();

    // 初期テストパターン画像を生成
    console.log('initDefaultState: calling generateTestPatterns...');
    generateTestPatterns();
    console.log('initDefaultState: contentLibrary after generateTestPatterns:', contentLibrary.length, 'items');
}

function displayVersionInfo() {
    const versionEl = document.getElementById('version-info');
    if (versionEl && typeof BUILD_INFO !== 'undefined') {
        versionEl.textContent = `Build: ${BUILD_INFO.buildDate} | Commit: ${BUILD_INFO.gitCommit}`;
        console.log('Build Info:', BUILD_INFO);
    }
}

function setupEventListeners() {
    // サイドバー開閉
    setupSidebar();

    // デバッグステータスバーのクリックイベント
    setupDebugStatusBarClick();

    // スプリッターによるリサイズ
    setupSplitter();

    // 画像追加ボタン（サイドバー内）
    document.getElementById('sidebar-add-image-btn').addEventListener('click', () => {
        document.getElementById('image-input').click();
    });

    // 出力追加ボタン（サイドバー内）
    const addOutputBtn = document.getElementById('sidebar-add-output-btn');
    if (addOutputBtn) {
        addOutputBtn.addEventListener('click', () => {
            showAddOutputDialog();
        });
    }

    // 画像選択
    document.getElementById('image-input').addEventListener('change', handleImageUpload);

    // 表示倍率スライダー
    const scaleSlider = document.getElementById('sidebar-preview-scale');
    const scaleValue = document.getElementById('sidebar-preview-scale-value');
    if (scaleSlider && scaleValue) {
        scaleSlider.addEventListener('input', (e) => {
            previewScale = parseFloat(e.target.value);
            scaleValue.textContent = previewScale + 'x';
            updateCanvasDisplayScale();
            if (previewScrollManager) previewScrollManager.applyRatio();
        });
    }

    // 状態管理ボタン
    const copyStateUrlBtn = document.getElementById('copy-state-url-btn');
    if (copyStateUrlBtn) {
        copyStateUrlBtn.addEventListener('click', () => {
            setStateToURL();
            // URLをクリップボードにコピー
            navigator.clipboard.writeText(window.location.href).then(() => {
                alert('URLをクリップボードにコピーしました');
            }).catch(err => {
                console.error('Failed to copy URL:', err);
                prompt('以下のURLをコピーしてください:', window.location.href);
            });
        });
    }

    const resetStateBtn = document.getElementById('reset-state-btn');
    if (resetStateBtn) {
        resetStateBtn.addEventListener('click', () => {
            if (confirm('保存された状態をクリアして初期状態にリセットしますか？')) {
                isResetting = true;  // beforeunloadで保存をスキップ
                clearSavedState();
                clearStateFromURL();
                window.location.reload();
            }
        });
    }

    // ダウンロードボタン
    document.getElementById('download-btn').addEventListener('click', downloadComposedImage);

    // ノード追加ドロップダウンメニューを初期化
    initNodeAddDropdown();
}

// カテゴリ表示名のマッピング
const CATEGORY_LABELS = {
    transform: '変換',
    composite: '合成',
    special: '特殊ソース',
    color: 'フィルタ - 色調',
    blur: 'フィルタ - ぼかし',
    other: 'フィルタ - その他'
};

// カテゴリの表示順序
const CATEGORY_ORDER = ['transform', 'composite', 'special', 'color', 'blur', 'other'];

// ノード追加ドロップダウンの初期化
function initNodeAddDropdown() {
    const dropdown = document.getElementById('node-add-dropdown');
    const btn = document.getElementById('node-add-btn');
    const menu = document.getElementById('node-add-menu');

    if (!dropdown || !btn || !menu) return;

    // メニュー内容を構築
    buildNodeAddMenu(menu);

    // メニュー位置を計算して表示
    function showMenu() {
        const rect = btn.getBoundingClientRect();
        const menuHeight = 400; // max-height
        const viewportHeight = window.innerHeight;

        // 下に十分なスペースがあるか確認
        const spaceBelow = viewportHeight - rect.bottom - 10;
        const spaceAbove = rect.top - 10;

        menu.style.left = '';
        menu.style.right = '';

        if (spaceBelow >= menuHeight || spaceBelow >= spaceAbove) {
            // 下に表示
            menu.style.top = `${rect.bottom + 4}px`;
            menu.style.maxHeight = `${Math.min(menuHeight, spaceBelow)}px`;
        } else {
            // 上に表示
            menu.style.top = `${rect.top - Math.min(menuHeight, spaceAbove) - 4}px`;
            menu.style.maxHeight = `${Math.min(menuHeight, spaceAbove)}px`;
        }

        // 右端に揃える
        menu.style.right = `${window.innerWidth - rect.right}px`;

        menu.classList.add('visible');
        dropdown.classList.add('open');
    }

    function hideMenu() {
        menu.classList.remove('visible');
        dropdown.classList.remove('open');
    }

    // ボタンクリックでメニュー開閉
    btn.addEventListener('click', (e) => {
        e.stopPropagation();
        if (menu.classList.contains('visible')) {
            hideMenu();
        } else {
            showMenu();
        }
    });

    // メニュー外クリックで閉じる
    document.addEventListener('click', (e) => {
        if (!dropdown.contains(e.target) && !menu.contains(e.target)) {
            hideMenu();
        }
    });

    // スクロールやリサイズで閉じる（メニュー内のスクロールは除外）
    window.addEventListener('scroll', (e) => {
        if (!menu.contains(e.target)) {
            hideMenu();
        }
    }, true);
    window.addEventListener('resize', hideMenu);

    // メニューアイテムクリック
    menu.addEventListener('click', (e) => {
        const item = e.target.closest('.node-add-item');
        if (!item) return;

        const nodeType = item.dataset.type;
        handleNodeAdd(nodeType);
        hideMenu();
    });
}

// メニュー内容の構築
function buildNodeAddMenu(menu) {
    menu.innerHTML = '';

    // カテゴリごとにグループ化
    const categories = {
        transform: [{ id: 'affine', name: 'アフィン変換', icon: '🔄' }],
        composite: [
            { id: 'composite', name: '合成', icon: '📑' },
            { id: 'distributor', name: '分配', icon: '📤' }
        ],
        special: []
    };

    // FILTER_DEFINITIONSからフィルタを追加
    Object.values(FILTER_DEFINITIONS).forEach(def => {
        const cat = def.category || 'other';
        if (!categories[cat]) {
            categories[cat] = [];
        }
        categories[cat].push({
            id: def.id,
            name: def.name,
            icon: getCategoryIcon(cat)
        });
    });

    // カテゴリ順にメニュー構築
    CATEGORY_ORDER.forEach(catKey => {
        const items = categories[catKey];
        if (!items || items.length === 0) return;

        const categoryDiv = document.createElement('div');
        categoryDiv.className = 'node-add-category';

        const header = document.createElement('div');
        header.className = 'node-add-category-header';
        header.textContent = CATEGORY_LABELS[catKey] || catKey;
        categoryDiv.appendChild(header);

        items.forEach(item => {
            const itemDiv = document.createElement('div');
            itemDiv.className = 'node-add-item';
            itemDiv.dataset.type = item.id;
            itemDiv.innerHTML = `<span class="item-icon">${item.icon}</span>${item.name}`;
            categoryDiv.appendChild(itemDiv);
        });

        menu.appendChild(categoryDiv);
    });
}

// カテゴリに応じたアイコンを返す
function getCategoryIcon(category) {
    switch (category) {
        case 'color': return '🎨';
        case 'blur': return '💨';
        default: return '⚡';
    }
}

// ノード追加処理
function handleNodeAdd(nodeType) {
    if (nodeType === 'affine') {
        addAffineNode();
    } else if (nodeType === 'composite') {
        addCompositeNode();
    } else if (nodeType === 'distributor') {
        addDistributorNode();
    } else if (nodeType === 'ninepatch') {
        addNinePatchNode();
    } else if (FILTER_DEFINITIONS[nodeType]) {
        addIndependentFilterNode(nodeType);
    }
}

async function handleImageUpload(event) {
    const files = event.target.files;

    for (let file of files) {
        if (!file.type.startsWith('image/')) {
            console.log('Skipping non-image file:', file.name);
            continue;
        }

        try {
            const imageData = await loadImage(file);
            addImageToLibrary(imageData);
        } catch (error) {
            console.error('Failed to load image:', error);
            const errorMsg = error.message || error.toString();
            alert('画像の読み込みに失敗しました\nファイル名: ' + file.name + '\nエラー: ' + errorMsg);
        }
    }

    // inputをリセット（同じファイルを再度選択可能にする）
    event.target.value = '';
}

// 画像ライブラリに画像を追加
function addImageToLibrary(imageData) {
    const content = addImageContent(
        imageData.name || `Image ${nextContentId}`,
        imageData.width,
        imageData.height,
        imageData,
        imageData.isNinePatch || false
    );

    // C++側の入力ライブラリに登録（数値IDを使用）
    graphEvaluator.storeImage(content.cppImageId, imageData.data, imageData.width, imageData.height);

    // UIを更新
    renderContentLibrary();
}

// ========================================
// コンテンツライブラリ管理
// ========================================

// 画像コンテンツを追加
function addImageContent(name, width, height, imageData, isNinePatch = false) {
    const content = {
        id: `img-${nextContentId++}`,
        type: 'image',
        name: name,
        width: width,
        height: height,
        imageData: imageData,
        cppImageId: nextCppImageId++,  // C++側に渡す数値ID
        isNinePatch: isNinePatch       // 9patchフラグ
    };
    contentLibrary.push(content);
    return content;
}

// 出力バッファコンテンツを追加
function addOutputContent(name, width, height) {
    const content = {
        id: `out-${nextContentId++}`,
        type: 'output',
        name: name,
        width: width,
        height: height,
        cppImageId: nextCppImageId++,  // C++側の出力バッファID
        imageData: null
    };
    contentLibrary.push(content);
    setFocusedContent(content.id);
    return content;
}

// フォーカス管理
function setFocusedContent(contentId) {
    // 切り替え前のスクロール比率を保存
    const scrollRatio = previewScrollManager ? previewScrollManager.getRatio() : null;

    focusedContentId = contentId;
    renderContentLibrary();
    updateFocusedPreview();

    // 切り替え後にスクロール比率を復元
    if (scrollRatio && previewScrollManager) {
        // キャンバスサイズ変更後に比率を適用するため少し遅延
        requestAnimationFrame(() => {
            previewScrollManager.setRatio(scrollRatio.x, scrollRatio.y);
        });
    }
}

function getFocusedContent() {
    return contentLibrary.find(c => c.id === focusedContentId);
}

// フォーカス中のコンテンツをプレビュー表示
function updateFocusedPreview() {
    const focused = getFocusedContent();
    if (!focused) {
        ctx.clearRect(0, 0, canvas.width, canvas.height);
        return;
    }

    if (focused.type === 'image') {
        // 入力画像をそのまま表示
        displayContentImage(focused);
    } else if (focused.type === 'output') {
        // 出力バッファを表示（レンダリング結果）
        if (focused.imageData) {
            displayContentImage(focused);
        } else {
            // まだレンダリングされていない場合はクリア
            ctx.clearRect(0, 0, canvas.width, canvas.height);
        }
    }
}

// コンテンツの画像をキャンバスに表示
function displayContentImage(content) {
    console.log('displayContentImage: content.id=', content?.id, 'imageData=', content?.imageData ? 'exists' : 'null', 'data length=', content?.imageData?.data?.length);
    if (!content || !content.imageData) return;

    // キャンバスサイズを調整
    canvas.width = content.width;
    canvas.height = content.height;
    canvasWidth = content.width;
    canvasHeight = content.height;

    // ImageDataを作成して描画
    const imageData = new ImageData(
        new Uint8ClampedArray(content.imageData.data),
        content.width,
        content.height
    );
    // 最初の数ピクセルのRGBA値を確認
    console.log('displayContentImage: first pixels RGBA:',
        imageData.data[0], imageData.data[1], imageData.data[2], imageData.data[3],
        '|', imageData.data[4], imageData.data[5], imageData.data[6], imageData.data[7]);
    console.log('displayContentImage: drawing', content.width, 'x', content.height, 'canvas:', canvas.width, 'x', canvas.height);
    ctx.putImageData(imageData, 0, 0);
    updateCanvasDisplayScale();
}

// フォーカス更新（削除後）
function updateFocusAfterDelete(deletedContentId) {
    if (focusedContentId === deletedContentId) {
        const remaining = contentLibrary.find(c => c.type === 'output');
        focusedContentId = remaining ? remaining.id : null;
    }
}

// 出力バッファを削除（コンテンツライブラリから）
function deleteOutputContent(contentId) {
    const content = contentLibrary.find(c => c.id === contentId);
    if (!content || content.type !== 'output') return;

    // 確認ダイアログ
    const sinkNode = getSinkForContent(contentId);
    let message = `「${content.name}」を削除しますか？`;
    if (sinkNode) {
        message += '\n対応するSinkノードも削除されます。';
    }
    if (!confirm(message)) return;

    // 対応するSinkノードも削除
    if (sinkNode) {
        globalNodes = globalNodes.filter(n => n.id !== sinkNode.id);
        globalConnections = globalConnections.filter(
            c => c.fromNodeId !== sinkNode.id && c.toNodeId !== sinkNode.id
        );
    }

    // コンテンツライブラリから削除
    contentLibrary = contentLibrary.filter(c => c.id !== contentId);
    updateFocusAfterDelete(contentId);

    renderNodeGraph();
    renderContentLibrary();
    updateFocusedPreview();
    scheduleAutoSave();
}

// ========================================
// UI共通ヘルパー関数
// ========================================

// ノードグラフ用X,Yスライダーを作成（配置位置用）
// options: { node, property, label, min, max, step, container }
function createNodeGraphPositionSlider(options) {
    const { node, property, label, min = -500, max = 500, step = 0.1, container } = options;

    const row = document.createElement('label');
    row.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin-bottom: 2px;';

    const labelSpan = document.createElement('span');
    labelSpan.textContent = label + ':';
    labelSpan.style.cssText = 'min-width: 18px;';

    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = String(min);
    slider.max = String(max);
    slider.step = String(step);
    const currentValue = node.position?.[property] ?? 0;
    slider.value = String(currentValue);
    slider.style.cssText = 'flex: 1; min-width: 50px;';

    const display = document.createElement('span');
    display.style.cssText = 'min-width: 35px; text-align: right; font-size: 9px;';
    display.textContent = currentValue.toFixed(1);

    slider.addEventListener('input', (e) => {
        if (!node.position) node.position = { x: 0, y: 0 };
        const value = parseFloat(e.target.value);
        node.position[property] = value;
        display.textContent = value.toFixed(1);
        throttledUpdatePreview();
    });

    row.appendChild(labelSpan);
    row.appendChild(slider);
    row.appendChild(display);
    container.appendChild(row);

    return { row, slider, display };
}

// 詳細ダイアログ用スライダー行を作成
// options: { label, min, max, step, value, onChange, unit }
function createDetailSliderRow(options) {
    const { label, min, max, step, value, onChange, unit = '', width = '100%' } = options;

    const row = document.createElement('div');
    row.className = 'node-detail-row';
    row.style.cssText = 'display: flex; align-items: center; gap: 8px;';

    const labelEl = document.createElement('label');
    labelEl.textContent = label;
    labelEl.style.cssText = 'min-width: 24px;';

    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = String(min);
    slider.max = String(max);
    slider.step = String(step);
    slider.value = String(value);
    slider.style.cssText = 'flex: 1; min-width: 80px;';

    const display = document.createElement('span');
    display.style.cssText = 'min-width: 50px; text-align: right; font-size: 11px;';
    display.textContent = value.toFixed(step < 1 ? (step < 0.1 ? 2 : 1) : 0) + unit;

    slider.addEventListener('input', (e) => {
        const newValue = parseFloat(e.target.value);
        const decimals = step < 1 ? (step < 0.1 ? 2 : 1) : 0;
        display.textContent = newValue.toFixed(decimals) + unit;
        if (onChange) onChange(newValue);
    });

    row.appendChild(labelEl);
    row.appendChild(slider);
    row.appendChild(display);

    return { row, slider, display };
}

// 9点セレクタ + 正規化スライダーのセクションを作成
function createOriginSection(options) {
    const { node, container, onChange } = options;

    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = '原点';
    section.appendChild(label);

    // 9点セレクタ
    const originGrid = document.createElement('div');
    originGrid.className = 'node-origin-grid';
    originGrid.style.cssText = 'width: 60px; height: 60px; margin: 0 auto 8px;';

    const originValues = [
        { x: 0, y: 0 }, { x: 0.5, y: 0 }, { x: 1, y: 0 },
        { x: 0, y: 0.5 }, { x: 0.5, y: 0.5 }, { x: 1, y: 0.5 },
        { x: 0, y: 1 }, { x: 0.5, y: 1 }, { x: 1, y: 1 }
    ];

    let sliderX, sliderY, displayX, displayY;

    const updateSliders = (x, y) => {
        if (sliderX) { sliderX.value = String(x); displayX.textContent = x.toFixed(2); }
        if (sliderY) { sliderY.value = String(y); displayY.textContent = y.toFixed(2); }
    };

    const updateGridSelection = (x, y) => {
        originGrid.querySelectorAll('.node-origin-point').forEach(btn => {
            const bx = parseFloat(btn.dataset.x);
            const by = parseFloat(btn.dataset.y);
            btn.classList.toggle('selected', bx === x && by === y);
        });
    };

    originValues.forEach(({ x, y }) => {
        const btn = document.createElement('button');
        btn.className = 'node-origin-point';
        btn.dataset.x = String(x);
        btn.dataset.y = String(y);
        if (node.originX === x && node.originY === y) {
            btn.classList.add('selected');
        }
        btn.addEventListener('click', () => {
            node.originX = x;
            node.originY = y;
            updateGridSelection(x, y);
            updateSliders(x, y);
            if (onChange) onChange();
        });
        originGrid.appendChild(btn);
    });
    section.appendChild(originGrid);

    // X スライダー
    const xRow = document.createElement('div');
    xRow.style.cssText = 'display: flex; align-items: center; gap: 8px; margin-bottom: 4px;';
    const xLabel = document.createElement('label');
    xLabel.textContent = 'X:';
    xLabel.style.cssText = 'min-width: 20px;';
    sliderX = document.createElement('input');
    sliderX.type = 'range';
    sliderX.min = '0';
    sliderX.max = '1';
    sliderX.step = '0.01';
    sliderX.value = String(node.originX ?? 0.5);
    sliderX.style.cssText = 'flex: 1;';
    displayX = document.createElement('span');
    displayX.style.cssText = 'min-width: 36px; text-align: right; font-size: 11px;';
    displayX.textContent = (node.originX ?? 0.5).toFixed(2);

    sliderX.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        node.originX = val;
        displayX.textContent = val.toFixed(2);
        updateGridSelection(val, node.originY);
        if (onChange) onChange();
    });

    xRow.appendChild(xLabel);
    xRow.appendChild(sliderX);
    xRow.appendChild(displayX);
    section.appendChild(xRow);

    // Y スライダー
    const yRow = document.createElement('div');
    yRow.style.cssText = 'display: flex; align-items: center; gap: 8px;';
    const yLabel = document.createElement('label');
    yLabel.textContent = 'Y:';
    yLabel.style.cssText = 'min-width: 20px;';
    sliderY = document.createElement('input');
    sliderY.type = 'range';
    sliderY.min = '0';
    sliderY.max = '1';
    sliderY.step = '0.01';
    sliderY.value = String(node.originY ?? 0.5);
    sliderY.style.cssText = 'flex: 1;';
    displayY = document.createElement('span');
    displayY.style.cssText = 'min-width: 36px; text-align: right; font-size: 11px;';
    displayY.textContent = (node.originY ?? 0.5).toFixed(2);

    sliderY.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        node.originY = val;
        displayY.textContent = val.toFixed(2);
        updateGridSelection(node.originX, val);
        if (onChange) onChange();
    });

    yRow.appendChild(yLabel);
    yRow.appendChild(sliderY);
    yRow.appendChild(displayY);
    section.appendChild(yRow);

    container.appendChild(section);
    return section;
}

// 配置位置セクションを作成（詳細ダイアログ用）
function createPositionSection(options) {
    const { node, container, onChange } = options;

    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = '配置位置';
    section.appendChild(label);

    // X スライダー
    const xResult = createDetailSliderRow({
        label: 'X',
        min: -500,
        max: 500,
        step: 0.1,
        value: node.position?.x ?? 0,
        onChange: (val) => {
            if (!node.position) node.position = { x: 0, y: 0 };
            node.position.x = val;
            if (onChange) onChange();
        }
    });
    section.appendChild(xResult.row);

    // Y スライダー
    const yResult = createDetailSliderRow({
        label: 'Y',
        min: -500,
        max: 500,
        step: 0.1,
        value: node.position?.y ?? 0,
        onChange: (val) => {
            if (!node.position) node.position = { x: 0, y: 0 };
            node.position.y = val;
            if (onChange) onChange();
        }
    });
    section.appendChild(yResult.row);

    container.appendChild(section);
    return section;
}

// ========================================
// ヘルパー関数
// ========================================
function getSinkForContent(contentId) {
    return globalNodes.find(n => n.type === 'sink' && n.contentId === contentId);
}

function getImageNodesForContent(contentId) {
    return globalNodes.filter(n => n.type === 'image' && n.contentId === contentId);
}

// コンテンツからコンテンツを取得（画像/出力）
function getContentById(contentId) {
    return contentLibrary.find(c => c.id === contentId);
}

// 出力バッファからSinkノードを追加
function addSinkNodeFromLibrary(contentId) {
    const content = contentLibrary.find(c => c.id === contentId);
    if (!content || content.type !== 'output') return;

    // 既にこのcontentに紐付くSinkがあればスキップ
    const existingSink = getSinkForContent(contentId);
    if (existingSink) {
        console.log('Sink already exists for this output');
        return;
    }

    // 表示範囲の中央付近に配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 140;
    const nodeHeight = 70;
    const posX = center.x + 200 + randomOffset();  // 右寄り
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const sinkNode = {
        id: `sink-${Date.now()}`,
        type: 'sink',
        title: content.name,
        contentId: contentId,
        posX: posX,
        posY: posY,
        originX: Math.round(content.width / 2),   // 仮想スクリーン上の基準座標（中央）
        originY: Math.round(content.height / 2),
        outputFormat: DEFAULT_PIXEL_FORMAT
    };

    globalNodes.push(sinkNode);

    // Renderer下流が未接続なら自動接続
    autoConnectToRenderer(sinkNode);

    renderNodeGraph();
    throttledUpdatePreview();  // 接続変更時の描画更新
    setFocusedContent(contentId);
    scheduleAutoSave();
}

// Renderer下流が未接続なら自動接続（Sink追加時）
function autoConnectToRenderer(sinkNode) {
    const rendererNode = globalNodes.find(n => n.type === 'renderer');
    if (!rendererNode) return;

    // Rendererの出力ポートから出ている接続があるか確認
    const hasDownstreamConnection = globalConnections.some(
        c => c.fromNodeId === rendererNode.id && c.fromPortId === 'out'
    );

    if (!hasDownstreamConnection) {
        // 自動接続: Renderer.out → Sink.in
        globalConnections.push({
            fromNodeId: rendererNode.id,
            fromPortId: 'out',
            toNodeId: sinkNode.id,
            toPortId: 'in'
        });
    }
}

// Renderer上流が未接続なら自動接続（Source追加時）
function autoConnectFromSource(imageNode) {
    const rendererNode = globalNodes.find(n => n.type === 'renderer');
    if (!rendererNode) return;

    // Rendererの入力ポートに入っている接続があるか確認
    const hasUpstreamConnection = globalConnections.some(
        c => c.toNodeId === rendererNode.id && c.toPortId === 'in'
    );

    if (!hasUpstreamConnection) {
        // 自動接続: Image.out → Renderer.in
        globalConnections.push({
            fromNodeId: imageNode.id,
            fromPortId: 'out',
            toNodeId: rendererNode.id,
            toPortId: 'in'
        });
    }
}

// 出力追加ダイアログを表示
function showAddOutputDialog() {
    const width = prompt('出力幅 (px):', '320');
    if (!width) return;

    const height = prompt('出力高さ (px):', '240');
    if (!height) return;

    const w = parseInt(width, 10);
    const h = parseInt(height, 10);
    if (isNaN(w) || isNaN(h) || w <= 0 || h <= 0) {
        alert('有効なサイズを入力してください');
        return;
    }

    const outputCount = contentLibrary.filter(c => c.type === 'output').length;
    const content = addOutputContent(
        `LCD #${outputCount + 1}`,
        w,
        h
    );

    // 対応するSinkノードも自動生成
    addSinkNodeFromLibrary(content.id);

    scheduleAutoSave();
}

// デバッグ用テストパターン画像を生成
function generateTestPatterns() {
    const patterns = [];

    // パターン1: チェッカーパターン（128x96、4:3比率）
    {
        const width = 128;
        const height = 96;
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = width;
        tempCanvas.height = height;
        const tempCtx = tempCanvas.getContext('2d');

        const cellSize = 16;
        for (let y = 0; y < height; y += cellSize) {
            for (let x = 0; x < width; x += cellSize) {
                const isWhite = ((x / cellSize) + (y / cellSize)) % 2 === 0;
                tempCtx.fillStyle = isWhite ? '#ffffff' : '#4a90d9';
                tempCtx.fillRect(x, y, cellSize, cellSize);
            }
        }
        // 中心マーク
        tempCtx.fillStyle = '#ff0000';
        tempCtx.beginPath();
        tempCtx.arc(width / 2, height / 2, 4, 0, Math.PI * 2);
        tempCtx.fill();

        const imageData = tempCtx.getImageData(0, 0, width, height);
        patterns.push({
            name: 'Checker',
            data: new Uint8ClampedArray(imageData.data),
            width: width,
            height: height
        });
    }

    // パターン2: 同心円ターゲット（128x128、正方形）
    {
        const size = 128;
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = size;
        tempCanvas.height = size;
        const tempCtx = tempCanvas.getContext('2d');

        // 背景
        tempCtx.fillStyle = '#ffffff';
        tempCtx.fillRect(0, 0, size, size);

        // 同心円
        const cx = size / 2;
        const cy = size / 2;
        const colors = ['#ff6b6b', '#ffffff', '#4ecdc4', '#ffffff', '#45b7d1', '#ffffff', '#96ceb4'];
        for (let i = colors.length - 1; i >= 0; i--) {
            const radius = (i + 1) * (size / 2 / colors.length);
            tempCtx.fillStyle = colors[i];
            tempCtx.beginPath();
            tempCtx.arc(cx, cy, radius, 0, Math.PI * 2);
            tempCtx.fill();
        }
        // 中心点
        tempCtx.fillStyle = '#000000';
        tempCtx.beginPath();
        tempCtx.arc(cx, cy, 3, 0, Math.PI * 2);
        tempCtx.fill();

        const imageData = tempCtx.getImageData(0, 0, size, size);
        patterns.push({
            name: 'Target',
            data: new Uint8ClampedArray(imageData.data),
            width: size,
            height: size
        });
    }

    // パターン3: グリッド＋十字線（128x128、正方形）
    {
        const size = 128;
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = size;
        tempCanvas.height = size;
        const tempCtx = tempCanvas.getContext('2d');

        // 背景（半透明グレー）
        tempCtx.fillStyle = 'rgba(200, 200, 200, 0.5)';
        tempCtx.fillRect(0, 0, size, size);

        // グリッド線
        tempCtx.strokeStyle = '#666666';
        tempCtx.lineWidth = 1;
        const gridStep = 16;
        for (let i = 0; i <= size; i += gridStep) {
            tempCtx.beginPath();
            tempCtx.moveTo(i, 0);
            tempCtx.lineTo(i, size);
            tempCtx.stroke();
            tempCtx.beginPath();
            tempCtx.moveTo(0, i);
            tempCtx.lineTo(size, i);
            tempCtx.stroke();
        }

        // 中心十字線（太め）
        tempCtx.strokeStyle = '#ff0000';
        tempCtx.lineWidth = 2;
        tempCtx.beginPath();
        tempCtx.moveTo(size / 2, 0);
        tempCtx.lineTo(size / 2, size);
        tempCtx.stroke();
        tempCtx.beginPath();
        tempCtx.moveTo(0, size / 2);
        tempCtx.lineTo(size, size / 2);
        tempCtx.stroke();

        // 対角線
        tempCtx.strokeStyle = '#0066cc';
        tempCtx.lineWidth = 1;
        tempCtx.beginPath();
        tempCtx.moveTo(0, 0);
        tempCtx.lineTo(size, size);
        tempCtx.stroke();
        tempCtx.beginPath();
        tempCtx.moveTo(size, 0);
        tempCtx.lineTo(0, size);
        tempCtx.stroke();

        const imageData = tempCtx.getImageData(0, 0, size, size);
        patterns.push({
            name: 'Grid',
            data: new Uint8ClampedArray(imageData.data),
            width: size,
            height: size
        });
    }

    // パターン4: クロスヘア（101x63、奇数×奇数、精度検証用）
    // 中心ピクセルを基準に180度点対称
    {
        const width = 101;
        const height = 63;
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = width;
        tempCanvas.height = height;
        const tempCtx = tempCanvas.getContext('2d');

        // 背景（薄い青）
        tempCtx.fillStyle = '#e8f4fc';
        tempCtx.fillRect(0, 0, width, height);

        // 中心ピクセル（奇数サイズなので中央の1ピクセル: 50, 31）
        const centerPixelX = Math.floor(width / 2);   // 50
        const centerPixelY = Math.floor(height / 2);  // 31

        // 外枠（青色、fillRectで描画し、全周で同じ1ピクセル幅を保証）
        tempCtx.fillStyle = '#0066cc';
        tempCtx.fillRect(0, 0, width, 1);           // 上辺
        tempCtx.fillRect(0, height - 1, width, 1);  // 下辺
        tempCtx.fillRect(0, 0, 1, height);          // 左辺
        tempCtx.fillRect(width - 1, 0, 1, height);  // 右辺

        // グリッド線（中心ピクセルから±10px間隔で対称に配置）
        tempCtx.fillStyle = '#cccccc';
        // 縦線: 中心から ±10, ±20, ±30, ±40, ±50
        for (let offset = 10; offset <= 50; offset += 10) {
            const xLeft = centerPixelX - offset;
            const xRight = centerPixelX + offset;
            if (xLeft >= 0) {
                tempCtx.fillRect(xLeft, 0, 1, height);
            }
            if (xRight < width) {
                tempCtx.fillRect(xRight, 0, 1, height);
            }
        }
        // 横線: 中心から ±10, ±20, ±30
        for (let offset = 10; offset <= 30; offset += 10) {
            const yTop = centerPixelY - offset;
            const yBottom = centerPixelY + offset;
            if (yTop >= 0) {
                tempCtx.fillRect(0, yTop, width, 1);
            }
            if (yBottom < height) {
                tempCtx.fillRect(0, yBottom, width, 1);
            }
        }

        // 中心十字線（1ピクセル幅、赤）
        tempCtx.fillStyle = '#ff0000';
        tempCtx.fillRect(centerPixelX, 0, 1, height);  // 垂直線
        tempCtx.fillRect(0, centerPixelY, width, 1);   // 水平線

        // 中心マーカー（3x3、赤塗り）
        tempCtx.fillRect(centerPixelX - 1, centerPixelY - 1, 3, 3);

        const imageData = tempCtx.getImageData(0, 0, width, height);
        patterns.push({
            name: 'CrossHair',
            data: new Uint8ClampedArray(imageData.data),
            width: width,
            height: height
        });
    }

    // パターン5: 小チェッカー（70x35、偶数×奇数、5x5セル）
    {
        const width = 70;
        const height = 35;
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = width;
        tempCanvas.height = height;
        const tempCtx = tempCanvas.getContext('2d');

        const cellSize = 5;
        for (let y = 0; y < height; y += cellSize) {
            for (let x = 0; x < width; x += cellSize) {
                const isWhite = ((x / cellSize) + (y / cellSize)) % 2 === 0;
                tempCtx.fillStyle = isWhite ? '#ffffff' : '#4a90d9';
                tempCtx.fillRect(x, y, cellSize, cellSize);
            }
        }

        // 中心マーク
        tempCtx.fillStyle = '#ff0000';
        tempCtx.beginPath();
        tempCtx.arc(width / 2, height / 2, 3, 0, Math.PI * 2);
        tempCtx.fill();

        const imageData = tempCtx.getImageData(0, 0, width, height);
        patterns.push({
            name: 'SmallCheck',
            data: new Uint8ClampedArray(imageData.data),
            width: width,
            height: height
        });
    }

    // パターン6: 9patch テスト画像（八角形 + タイルパターン背景）
    // 外周1pxはメタデータ、内部48x48がコンテンツ
    {
        const totalSize = 50;  // メタデータ含む
        const contentSize = 48;  // コンテンツサイズ
        const cornerSize = 16;  // 角の固定サイズ
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = totalSize;
        tempCanvas.height = totalSize;
        const tempCtx = tempCanvas.getContext('2d');

        // 背景を透明に
        tempCtx.clearRect(0, 0, totalSize, totalSize);

        // コンテンツ領域（1,1から48x48）にタイルパターン背景
        const tileSize = 8;
        for (let y = 1; y < totalSize - 1; y++) {
            for (let x = 1; x < totalSize - 1; x++) {
                const tx = Math.floor((x - 1) / tileSize);
                const ty = Math.floor((y - 1) / tileSize);
                const isTile1 = (tx + ty) % 2 === 0;
                // 半透明（alpha=0.5）でオーバーラップ効果を視覚的に確認
                tempCtx.fillStyle = isTile1 ? 'rgba(224, 232, 240, 0.5)' : 'rgba(200, 216, 232, 0.5)';
                tempCtx.fillRect(x, y, 1, 1);
            }
        }

        // 八角形を描画（角を斜めカット）
        // コンテンツ領域は (1,1) から (48,48) の 48x48 ピクセル
        // Canvas座標では左上角を指すので、右端/下端は +1 する
        const contentLeft = 1;
        const contentTop = 1;
        const contentRight = totalSize - 1;  // 49 (ピクセル48の右端)
        const contentBottom = totalSize - 1; // 49 (ピクセル48の下端)
        const cutSize = cornerSize / 2;  // 角のカットサイズ
        tempCtx.fillStyle = 'rgba(74, 144, 217, 0.5)';  // 青（半透明）
        tempCtx.beginPath();
        // 上辺の左側から時計回りに（コンテンツ領域内に収める）
        tempCtx.moveTo(contentLeft + cutSize, contentTop);              // 上辺左
        tempCtx.lineTo(contentRight - cutSize, contentTop);             // 上辺右
        tempCtx.lineTo(contentRight, contentTop + cutSize);             // 右上角
        tempCtx.lineTo(contentRight, contentBottom - cutSize);          // 右辺下
        tempCtx.lineTo(contentRight - cutSize, contentBottom);          // 右下角
        tempCtx.lineTo(contentLeft + cutSize, contentBottom);           // 下辺左
        tempCtx.lineTo(contentLeft, contentBottom - cutSize);           // 左下角
        tempCtx.lineTo(contentLeft, contentTop + cutSize);              // 左辺上
        tempCtx.closePath();
        tempCtx.fill();
        // 枠線は省略（stroke がメタデータ領域にはみ出すため）

        // 固定部と伸縮部の境界線を描画（目視確認用）
        // 境界位置: 左=16, 右=33, 上=16, 下=33（キャンバス座標）
        const boundaryLeft = cornerSize;      // 16（左固定部の右端）
        const boundaryRight = totalSize - 1 - cornerSize;  // 33（右固定部の左端）
        const boundaryTop = cornerSize;       // 16（上固定部の下端）
        const boundaryBottom = totalSize - 1 - cornerSize; // 33（下固定部の上端）
        tempCtx.fillStyle = 'rgba(255, 0, 0, 0.8)';  // 赤（目立つ色）
        // 縦の境界線（左）
        for (let y = 1; y < totalSize - 1; y++) {
            tempCtx.fillRect(boundaryLeft, y, 1, 1);
        }
        // 縦の境界線（右）
        for (let y = 1; y < totalSize - 1; y++) {
            tempCtx.fillRect(boundaryRight, y, 1, 1);
        }
        // 横の境界線（上）
        for (let x = 1; x < totalSize - 1; x++) {
            tempCtx.fillRect(x, boundaryTop, 1, 1);
        }
        // 横の境界線（下）
        for (let x = 1; x < totalSize - 1; x++) {
            tempCtx.fillRect(x, boundaryBottom, 1, 1);
        }

        // メタデータ境界線（外周1px）に伸縮領域を示す黒ピクセルを配置
        // 上辺：中央16ピクセル（x=17〜32）が伸縮領域
        // 左辺：中央16ピクセル（y=17〜32）が伸縮領域
        const stretchStart = 1 + cornerSize;  // 17
        const stretchEnd = totalSize - 1 - cornerSize;  // 33
        tempCtx.fillStyle = 'rgba(0, 0, 0, 1)';  // 黒（不透明）
        // 上辺
        for (let x = stretchStart; x < stretchEnd; x++) {
            tempCtx.fillRect(x, 0, 1, 1);
        }
        // 左辺
        for (let y = stretchStart; y < stretchEnd; y++) {
            tempCtx.fillRect(0, y, 1, 1);
        }

        // 各伸縮部にX字状の斜線を描画（バイリニア補間の動作確認用）
        // 伸縮部の座標:
        // [1] 上辺: x=17-32, y=1-16   (col=1, row=0)
        // [3] 左辺: x=1-16, y=17-32   (col=0, row=1)
        // [4] 中央: x=17-32, y=17-32  (col=1, row=1)
        // [5] 右辺: x=33-48, y=17-32  (col=2, row=1)
        // [7] 下辺: x=17-32, y=33-48  (col=1, row=2)
        tempCtx.fillStyle = 'rgba(0, 128, 0, 0.8)';  // 緑
        const stretchParts = [
            { x1: stretchStart, y1: 1, x2: stretchEnd - 1, y2: boundaryTop },               // [1] 上辺
            { x1: 1, y1: stretchStart, x2: boundaryLeft, y2: stretchEnd - 1 },              // [3] 左辺
            { x1: stretchStart, y1: stretchStart, x2: stretchEnd - 1, y2: stretchEnd - 1 }, // [4] 中央
            { x1: boundaryRight, y1: stretchStart, x2: totalSize - 2, y2: stretchEnd - 1 }, // [5] 右辺
            { x1: stretchStart, y1: boundaryBottom, x2: stretchEnd - 1, y2: totalSize - 2 } // [7] 下辺
        ];
        stretchParts.forEach(part => {
            const w = part.x2 - part.x1 + 1;
            const h = part.y2 - part.y1 + 1;
            // 左上→右下の斜線
            for (let i = 0; i < Math.min(w, h); i++) {
                tempCtx.fillRect(part.x1 + i, part.y1 + i, 1, 1);
            }
            // 右上→左下の斜線
            for (let i = 0; i < Math.min(w, h); i++) {
                tempCtx.fillRect(part.x2 - i, part.y1 + i, 1, 1);
            }
        });

        const imageData = tempCtx.getImageData(0, 0, totalSize, totalSize);
        patterns.push({
            name: '9patch-Octagon',
            data: new Uint8ClampedArray(imageData.data),
            width: totalSize,
            height: totalSize,
            isNinePatch: true  // 9patchフラグ
        });
    }

    // パターン7: 9patch ファンタジー装飾枠（セリフ枠用）
    // 外周1pxはメタデータ、内部62x62がコンテンツ
    {
        const totalSize = 64;  // メタデータ含む
        const contentSize = 62;  // コンテンツサイズ
        const cornerSize = 20;  // 角の固定サイズ（装飾含む）
        const borderWidth = 4;  // 枠の太さ
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = totalSize;
        tempCanvas.height = totalSize;
        const tempCtx = tempCanvas.getContext('2d');

        // 背景を透明に
        tempCtx.clearRect(0, 0, totalSize, totalSize);

        // コンテンツ領域の範囲
        const contentLeft = 1;
        const contentTop = 1;
        const contentRight = totalSize - 1;
        const contentBottom = totalSize - 1;

        // ========================================
        // 外枠（金属的な銀色グラデーション風）
        // ========================================
        // 外側の枠（濃いグレー）
        tempCtx.fillStyle = '#505860';
        tempCtx.fillRect(contentLeft, contentTop, contentSize, contentSize);

        // 内側を削って枠にする（角丸風に角を残す）
        const innerLeft = contentLeft + borderWidth;
        const innerTop = contentTop + borderWidth;
        const innerSize = contentSize - borderWidth * 2;

        // 内側の背景（ダークブラウン木目調）
        // グラデーション効果を出すため複数色で塗り分け
        const woodColors = ['#2d1810', '#3d2415', '#2d1810'];
        const bandHeight = Math.floor(innerSize / woodColors.length);
        for (let i = 0; i < woodColors.length; i++) {
            tempCtx.fillStyle = woodColors[i];
            const y = innerTop + i * bandHeight;
            const h = (i === woodColors.length - 1) ? (innerSize - i * bandHeight) : bandHeight;
            tempCtx.fillRect(innerLeft, y, innerSize, h);
        }

        // 木目のテクスチャライン（微細な横線）
        tempCtx.fillStyle = 'rgba(60, 40, 25, 0.3)';
        for (let y = innerTop + 2; y < innerTop + innerSize; y += 4) {
            tempCtx.fillRect(innerLeft, y, innerSize, 1);
        }

        // ========================================
        // 枠の立体感（ハイライトとシャドウ）
        // ========================================
        // 上辺ハイライト
        tempCtx.fillStyle = '#a0a8b0';
        tempCtx.fillRect(contentLeft, contentTop, contentSize, 1);
        tempCtx.fillStyle = '#808890';
        tempCtx.fillRect(contentLeft, contentTop + 1, contentSize, 1);

        // 左辺ハイライト
        tempCtx.fillStyle = '#909098';
        tempCtx.fillRect(contentLeft, contentTop, 1, contentSize);
        tempCtx.fillStyle = '#707880';
        tempCtx.fillRect(contentLeft + 1, contentTop, 1, contentSize);

        // 下辺シャドウ
        tempCtx.fillStyle = '#303840';
        tempCtx.fillRect(contentLeft, contentBottom - 1, contentSize, 1);
        tempCtx.fillStyle = '#404850';
        tempCtx.fillRect(contentLeft, contentBottom - 2, contentSize, 1);

        // 右辺シャドウ
        tempCtx.fillStyle = '#384048';
        tempCtx.fillRect(contentRight - 1, contentTop, 1, contentSize);
        tempCtx.fillStyle = '#485058';
        tempCtx.fillRect(contentRight - 2, contentTop, 1, contentSize);

        // ========================================
        // 内側の枠線（金色アクセント）
        // ========================================
        tempCtx.fillStyle = '#c9a227';  // ゴールド
        // 上
        tempCtx.fillRect(innerLeft, innerTop, innerSize, 1);
        // 下
        tempCtx.fillRect(innerLeft, innerTop + innerSize - 1, innerSize, 1);
        // 左
        tempCtx.fillRect(innerLeft, innerTop, 1, innerSize);
        // 右
        tempCtx.fillRect(innerLeft + innerSize - 1, innerTop, 1, innerSize);

        // ========================================
        // 四隅の装飾（ダイヤモンド型）
        // ========================================
        const decorSize = 5;  // 装飾のサイズ
        const decorOffset = 2;  // 枠からのオフセット
        const corners = [
            { x: contentLeft + decorOffset + decorSize, y: contentTop + decorOffset + decorSize },      // 左上
            { x: contentRight - decorOffset - decorSize, y: contentTop + decorOffset + decorSize },     // 右上
            { x: contentLeft + decorOffset + decorSize, y: contentBottom - decorOffset - decorSize },   // 左下
            { x: contentRight - decorOffset - decorSize, y: contentBottom - decorOffset - decorSize }   // 右下
        ];

        corners.forEach(corner => {
            // ダイヤモンド型（菱形）を描画
            tempCtx.fillStyle = '#ffd700';  // 明るいゴールド
            tempCtx.beginPath();
            tempCtx.moveTo(corner.x, corner.y - decorSize + 1);  // 上
            tempCtx.lineTo(corner.x + decorSize - 1, corner.y);  // 右
            tempCtx.lineTo(corner.x, corner.y + decorSize - 1);  // 下
            tempCtx.lineTo(corner.x - decorSize + 1, corner.y);  // 左
            tempCtx.closePath();
            tempCtx.fill();

            // 中心にハイライト
            tempCtx.fillStyle = '#ffffff';
            tempCtx.fillRect(corner.x, corner.y, 1, 1);
        });

        // ========================================
        // メタデータ境界線（外周1px）
        // ========================================
        const stretchStart = 1 + cornerSize;  // 21
        const stretchEnd = totalSize - 1 - cornerSize;  // 43
        tempCtx.fillStyle = 'rgba(0, 0, 0, 1)';  // 黒（不透明）
        // 上辺
        for (let x = stretchStart; x < stretchEnd; x++) {
            tempCtx.fillRect(x, 0, 1, 1);
        }
        // 左辺
        for (let y = stretchStart; y < stretchEnd; y++) {
            tempCtx.fillRect(0, y, 1, 1);
        }

        const imageData = tempCtx.getImageData(0, 0, totalSize, totalSize);
        patterns.push({
            name: '9patch-Fantasy',
            data: new Uint8ClampedArray(imageData.data),
            width: totalSize,
            height: totalSize,
            isNinePatch: true
        });
    }

    // 画像ライブラリに追加
    patterns.forEach(pattern => {
        addImageToLibrary(pattern);
    });

    console.log(`Generated ${patterns.length} test patterns`);
}

// コンテンツライブラリUIを描画
function renderContentLibrary() {
    const libraryContainer = document.getElementById('sidebar-images-library');
    if (!libraryContainer) return;
    libraryContainer.innerHTML = '';

    const template = document.getElementById('content-item-template');

    contentLibrary.forEach(content => {
        const item = template.content.cloneNode(true);
        const itemDiv = item.querySelector('.content-item');

        // フォーカス状態を反映
        if (content.id === focusedContentId) {
            itemDiv.classList.add('focused');
        }

        // サムネイル設定
        const thumbnailContainer = item.querySelector('.content-thumbnail');
        const thumbnailImg = thumbnailContainer.querySelector('img');

        if (content.type === 'image') {
            // 画像コンテンツ: サムネイルを表示
            thumbnailImg.src = createThumbnailDataURL(content.imageData);
        } else if (content.type === 'output') {
            // 出力コンテンツ: レンダリング結果があればサムネイル、なければアイコン
            if (content.imageData) {
                thumbnailImg.src = createThumbnailDataURL(content.imageData);
            } else {
                thumbnailImg.remove();
                const placeholder = document.createElement('span');
                placeholder.className = 'placeholder-icon';
                placeholder.textContent = '📤';
                thumbnailContainer.appendChild(placeholder);
            }
        }

        // 名前設定
        item.querySelector('.content-name').textContent = content.name;

        // 解像度設定
        item.querySelector('.content-size').textContent = `${content.width}x${content.height}`;

        // ボタンの設定
        const addBtn = item.querySelector('.add-node-btn');
        const deleteBtn = item.querySelector('.delete-btn');

        if (content.type === 'image') {
            if (content.isNinePatch) {
                // 9patch画像: 紫系
                addBtn.textContent = '+9P';
                addBtn.classList.add('ninepatch');
                addBtn.title = '9patchノードを追加';
                addBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    addNinePatchNode(content.id);
                });
            } else {
                // 通常画像（Source側）: オレンジ
                addBtn.textContent = '+Src';
                addBtn.classList.add('source');
                addBtn.title = '画像ソースノードを追加';
                addBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    addImageNodeFromLibrary(content.id);
                });
            }

            // 削除ボタン
            deleteBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                deleteImageFromLibrary(content.id);
            });
        } else if (content.type === 'output') {
            // 出力（Sink側）: 青
            addBtn.classList.add('sink');
            addBtn.title = '出力シンクノードを追加';
            const existingSink = getSinkForContent(content.id);
            if (existingSink) {
                addBtn.textContent = '✓Snk';
                addBtn.disabled = true;
            } else {
                addBtn.textContent = '+Snk';
                addBtn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    addSinkNodeFromLibrary(content.id);
                });
            }

            // 削除ボタン
            deleteBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                deleteOutputContent(content.id);
            });
        }

        // フォーカス切り替え（アイテムクリック）
        itemDiv.addEventListener('click', (e) => {
            if (!e.target.closest('.add-node-btn') &&
                !e.target.closest('.delete-btn')) {
                setFocusedContent(content.id);
            }
        });

        libraryContainer.appendChild(item);
    });
}


// サムネイル用のData URLを作成
function createThumbnailDataURL(imageData) {
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = imageData.width;
    tempCanvas.height = imageData.height;
    const tempCtx = tempCanvas.getContext('2d');
    tempCtx.putImageData(new ImageData(imageData.data, imageData.width, imageData.height), 0, 0);
    return tempCanvas.toDataURL();
}

// 画像ライブラリから画像ノードを追加
function addImageNodeFromLibrary(contentId) {
    const content = contentLibrary.find(c => c.id === contentId);
    if (!content || content.type !== 'image') return;

    // 表示範囲の中央付近に固定配置（画像ノードは左寄り）+ ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 70;
    const posX = center.x - 230 + randomOffset(); // 中央より左寄り
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const imageNode = {
        id: `image-node-${nextImageNodeId++}`,
        type: 'image',
        contentId: contentId,  // contentLibraryのID
        title: content.name,
        posX: posX,
        posY: posY,
        // 元画像の原点（正規化座標 0.0〜1.0）
        originX: 0.5,
        originY: 0.5
    };

    globalNodes.push(imageNode);

    // Renderer上流が未接続なら自動接続
    autoConnectFromSource(imageNode);

    renderNodeGraph();
    throttledUpdatePreview();  // 接続変更時の描画更新
    scheduleAutoSave();
}

// 画像ライブラリから画像を削除
function deleteImageFromLibrary(contentId) {
    const content = contentLibrary.find(c => c.id === contentId);
    if (!content || content.type !== 'image') return;

    // この画像を使用しているノードがあるか確認
    const usingNodes = getImageNodesForContent(contentId);
    if (usingNodes.length > 0) {
        if (!confirm(`この画像は${usingNodes.length}個のノードで使用されています。削除してもよろしいですか？`)) {
            return;
        }
        // ノードも削除
        globalNodes = globalNodes.filter(n => !(n.type === 'image' && n.contentId === contentId));
        // 接続も削除
        const nodeIds = usingNodes.map(n => n.id);
        globalConnections = globalConnections.filter(
            c => !nodeIds.includes(c.fromNodeId) && !nodeIds.includes(c.toNodeId)
        );
    }

    // 画像を削除
    contentLibrary = contentLibrary.filter(c => c.id !== contentId);

    renderContentLibrary();
    renderNodeGraph();
    scheduleAutoSave();
}

function loadImage(file) {
    return new Promise((resolve, reject) => {
        console.log('Loading image:', file.name, 'Type:', file.type, 'Size:', file.size);

        const reader = new FileReader();

        reader.onload = (e) => {
            console.log('FileReader loaded successfully');
            const img = new Image();

            img.onload = () => {
                console.log('Image loaded:', img.width + 'x' + img.height);
                try {
                    // 画像をRGBAデータに変換
                    const tempCanvas = document.createElement('canvas');
                    tempCanvas.width = img.width;
                    tempCanvas.height = img.height;
                    const tempCtx = tempCanvas.getContext('2d');
                    tempCtx.drawImage(img, 0, 0);
                    const imageData = tempCtx.getImageData(0, 0, img.width, img.height);

                    console.log('ImageData created:', imageData.data.length, 'bytes');
                    // .9.png で終わるファイル名は9patch画像として認識
                    const isNinePatch = file.name.toLowerCase().endsWith('.9.png');
                    if (isNinePatch) {
                        console.log('Detected as 9patch image:', file.name);
                    }
                    resolve({
                        data: imageData.data,
                        width: img.width,
                        height: img.height,
                        name: file.name,
                        isNinePatch: isNinePatch
                    });
                } catch (error) {
                    console.error('Error processing image:', error);
                    reject(new Error('画像処理エラー: ' + error.message));
                }
            };

            img.onerror = (error) => {
                console.error('Image load error:', error);
                reject(new Error('画像の読み込みエラー'));
            };

            img.src = e.target.result;
        };

        reader.onerror = (error) => {
            console.error('FileReader error:', error);
            reject(new Error('ファイル読み込みエラー'));
        };

        reader.readAsDataURL(file);
    });
}

// キャンバスの表示スケールを更新
function updateCanvasDisplayScale() {
    if (!canvas) return;
    canvas.style.width = (canvasWidth * previewScale) + 'px';
    canvas.style.height = (canvasHeight * previewScale) + 'px';
}

// キャンバスサイズを変更（グローバル変数とC++側も更新）
function resizeCanvas(width, height) {
    canvasWidth = width;
    canvasHeight = height;
    canvas.width = width;
    canvas.height = height;
    updateCanvasDisplayScale();
    if (graphEvaluator) {
        graphEvaluator.setCanvasSize(width, height);
    }
}

// スキャンライン処理設定を適用
function applyTileSettings() {
    if (!graphEvaluator) return;

    // グローバル変数から設定を取得
    graphEvaluator.setTileSize(tileWidth, tileHeight);
    graphEvaluator.setDebugCheckerboard(debugCheckerboard);
}


function downloadComposedImage() {
    canvas.toBlob((blob) => {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'composed_image_' + Date.now() + '.png';
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }, 'image/png');
}

// ========================================
// グローバルノードグラフ
// ========================================

function initializeNodeGraph() {
    nodeGraphSvg = document.getElementById('node-graph-canvas');

    // 接続ドラッグのマウス移動（タッチ対応）
    document.addEventListener('mousemove', (e) => {
        if (isDraggingConnection) {
            updateDragConnectionPath(e.clientX, e.clientY);
        }
    });

    // 接続ドラッグのタッチ移動
    document.addEventListener('touchmove', (e) => {
        if (isDraggingConnection && e.touches && e.touches[0]) {
            updateDragConnectionPath(e.touches[0].clientX, e.touches[0].clientY);
            e.preventDefault();
        }
    }, { passive: false });

    // 接続ドラッグのキャンセル（空白エリアでリリース）
    nodeGraphSvg.addEventListener('mouseup', (e) => {
        if (isDraggingConnection && e.target === nodeGraphSvg) {
            stopDraggingConnection();
        }
    });

    // 接続ドラッグのキャンセル（タッチ）
    nodeGraphSvg.addEventListener('touchend', (e) => {
        if (isDraggingConnection && e.target === nodeGraphSvg) {
            stopDraggingConnection();
        }
    });

    console.log('Node graph initialized');

    // スクロールマネージャーを初期化（比率ベースのスクロール位置管理）
    nodeGraphScrollManager = createScrollManager('.node-graph-canvas-container', { initialX: 0.5, initialY: 0.5 });
    previewScrollManager = createScrollManager('.canvas-container', { initialX: 0.5, initialY: 0.5 });
}

function renderNodeGraph() {
    if (!nodeGraphSvg) return;

    // ドラッグ中の接続線を明示的にクリーンアップ
    if (isDraggingConnection) {
        stopDraggingConnection();
    }

    // SVGをクリア
    nodeGraphSvg.innerHTML = '';

    // Renderer ノードが存在しない場合は追加（ノードグラフ中央に配置）
    if (!globalNodes.find(n => n.type === 'renderer')) {
        globalNodes.push({
            id: 'renderer',
            type: 'renderer',
            title: 'Renderer',
            virtualWidth: canvasWidth,
            virtualHeight: canvasHeight,
            originX: canvasOrigin.x,
            originY: canvasOrigin.y,
            posX: 700,
            posY: 550
        });
    }

    // ノードを描画（背面）
    globalNodes.forEach(node => {
        drawGlobalNode(node);
    });

    // 接続線を描画（前面）
    drawAllConnections();
}

function drawAllConnections() {
    // グローバル接続を描画
    globalConnections.forEach(conn => {
        const fromNode = globalNodes.find(n => n.id === conn.fromNodeId);
        const toNode = globalNodes.find(n => n.id === conn.toNodeId);

        if (fromNode && toNode) {
            drawConnectionBetweenPorts(fromNode, conn.fromPortId, toNode, conn.toPortId);
        }
    });
}

// 接続線のSVGパス文字列を計算
// 出力ポート（右側）からは必ず右向きに出る
// 入力ポート（左側）には必ず左向きから入る
// 条件分岐なしの連続的な計算で、ノード位置に関わらず滑らかな曲線を生成
function calculateConnectionPath(fromPos, toPos) {
    const dx = toPos.x - fromPos.x;
    const dy = Math.abs(toPos.y - fromPos.y);

    // 接続点間の距離
    const distance = Math.sqrt(dx * dx + dy * dy);

    // 距離に応じたスケール係数（距離200以上で1.0、近いほど小さく）
    const distanceScale = Math.min(1, 0.2 + distance / 250);

    // 縦並び度合い（0〜1）：dxが小さくdyが大きいほど1に近づく
    const verticalness = dy > 0 ? Math.min(1, Math.max(0, (dy - dx) / dy)) : 0;

    // オフセット：縦並び度合いに応じて連続的に変化（距離に応じて縮小）
    const baseOffset = 80 * distanceScale;
    const verticalOffset = Math.max(100, dy * 0.4) * distanceScale;
    const offset = Math.max(baseOffset, dx / 2, dy * 0.3) * (1 - verticalness) + verticalOffset * verticalness;

    // オフセットを距離の半分までに制限
    const limitedOffset = Math.min(offset, distance / 2);

    // 制御点Y：縦並び度合いに応じて相手側に寄せる
    const blendY = verticalness * 0.9;
    const cp1y = fromPos.y + (toPos.y - fromPos.y) * blendY;
    const cp2y = toPos.y + (fromPos.y - toPos.y) * blendY;

    const cp1x = fromPos.x + limitedOffset;  // 常に右へ
    const cp2x = toPos.x - limitedOffset;    // 常に左から

    return `M ${fromPos.x} ${fromPos.y} C ${cp1x} ${cp1y}, ${cp2x} ${cp2y}, ${toPos.x} ${toPos.y}`;
}

function drawConnectionBetweenPorts(fromNode, fromPortId, toNode, toPortId) {
    const ns = 'http://www.w3.org/2000/svg';

    // ポート位置を計算
    const fromPos = getPortPosition(fromNode, fromPortId, 'output');
    const toPos = getPortPosition(toNode, toPortId, 'input');

    const path = document.createElementNS(ns, 'path');
    const d = calculateConnectionPath(fromPos, toPos);

    // 一意のIDを設定（リアルタイム更新用）
    const pathId = `conn-${fromNode.id}-${fromPortId}-${toNode.id}-${toPortId}`;
    path.setAttribute('id', pathId);
    path.setAttribute('d', d);
    path.setAttribute('class', 'node-connection');
    path.setAttribute('stroke', '#667eea');
    path.setAttribute('stroke-width', '2');
    path.setAttribute('fill', 'none');

    // 接続を右クリックで削除
    path.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        removeConnection(fromNode.id, fromPortId, toNode.id, toPortId);
    });

    nodeGraphSvg.appendChild(path);
}

// アフィン変換パラメータから行列を計算
// 注: 中心補正はC++側(node_graph.cpp)でsrcOriginを基準に行うため、
//     ここでは基本行列のみを計算する
function calculateMatrixFromParams(translateX, translateY, rotation, scaleX, scaleY) {
    // 回転を度からラジアンに変換
    const rad = rotation * Math.PI / 180.0;
    const cos = Math.cos(rad);
    const sin = Math.sin(rad);

    // 基本行列要素を計算（原点(0,0)基準）
    // C++側で srcOrigin を中心とした変換 T(origin) × M × T(-origin) を適用
    const a = cos * scaleX;
    const b = -sin * scaleY;
    const c = sin * scaleX;
    const d = cos * scaleY;
    const tx = translateX;
    const ty = translateY;

    return { a, b, c, d, tx, ty };
}

// ノードの高さを動的に計算（コンパクト表示）
function getNodeHeight(node) {
    if (node.type === 'image') {
        return 120; // 画像ノード: サムネイル + X,Yスライダー
    } else if (node.type === 'composite') {
        // 合成ノード: 入力数に応じて可変高さ（ポート間隔を最低15px確保）
        const inputCount = node.inputs ? node.inputs.length : 2;
        const minPortSpacing = 15;
        const minHeight = 60;
        return Math.max(minHeight, (inputCount + 1) * minPortSpacing);
    } else if (node.type === 'distributor') {
        // 分配ノード: 出力数に応じて可変高さ
        const outputCount = node.outputs ? node.outputs.length : 2;
        const minPortSpacing = 15;
        const minHeight = 60;
        return Math.max(minHeight, (outputCount + 1) * minPortSpacing);
    } else if (node.type === 'affine') {
        return 70; // アフィン: 主要パラメータ1つ
    } else if (node.type === 'ninepatch') {
        return 120; // 9patch: サムネイル + X,Yスライダー
    } else if (node.type === 'filter' && node.independent) {
        return 70; // フィルタ: 主要パラメータ1つ
    } else if (node.type === 'renderer') {
        return 80; // Renderer: 仮想スクリーン情報
    } else if (node.type === 'sink') {
        return 110; // Sink: サムネイル + 出力情報
    } else {
        return 50; // デフォルト
    }
}

function getPortPosition(node, portId, portType) {
    const nodeWidth = 160;
    const nodeHeight = getNodeHeight(node); // 動的に計算

    const ports = getNodePorts(node);
    const portList = portType === 'input' ? ports.inputs : ports.outputs;
    const portIndex = portList.findIndex(p => p.id === portId);

    if (portIndex === -1) {
        // デフォルト位置
        return {
            x: node.posX + (portType === 'input' ? 0 : nodeWidth),
            y: node.posY + nodeHeight / 2
        };
    }

    const portCount = portList.length;
    const spacing = nodeHeight / (portCount + 1);
    const y = node.posY + spacing * (portIndex + 1);

    return {
        x: node.posX + (portType === 'input' ? 0 : nodeWidth),
        y: y
    };
}

function drawGlobalNode(node) {
    const ns = 'http://www.w3.org/2000/svg';
    const nodeWidth = 160;
    const nodeHeight = getNodeHeight(node); // 動的に計算

    // ノードボックス (foreignObject)
    const foreignObject = document.createElementNS(ns, 'foreignObject');
    foreignObject.setAttribute('x', node.posX);
    foreignObject.setAttribute('y', node.posY);
    foreignObject.setAttribute('width', nodeWidth);
    foreignObject.setAttribute('height', nodeHeight);

    const nodeBox = document.createElement('div');
    nodeBox.className = `node-box node-type-${node.type}`;
    nodeBox.dataset.nodeId = node.id;
    nodeBox.style.width = `${nodeWidth}px`;
    nodeBox.style.height = `${nodeHeight}px`;

    const header = document.createElement('div');
    header.className = 'node-box-header';

    const title = document.createElement('span');
    title.className = 'node-box-title';
    title.textContent = node.title;

    const idBadge = document.createElement('span');
    idBadge.className = 'node-box-id';
    idBadge.textContent = node.nodeId ? `#${node.nodeId}` : node.type;

    header.appendChild(title);
    header.appendChild(idBadge);
    nodeBox.appendChild(header);

    // 画像ノードの場合、サムネイル + X,Yスライダー表示
    if (node.type === 'image' && node.contentId !== undefined) {
        const content = contentLibrary.find(c => c.id === node.contentId);
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.style.cssText = 'padding: 4px;';

        // サムネイル行
        if (content && content.imageData) {
            const thumbRow = document.createElement('div');
            thumbRow.style.cssText = 'display: flex; align-items: center; gap: 6px; margin-bottom: 4px;';

            const img = document.createElement('img');
            img.src = createThumbnailDataURL(content.imageData);
            img.style.cssText = 'width: 32px; height: 32px; object-fit: cover; border-radius: 3px;';
            thumbRow.appendChild(img);

            // 原点表示（コンパクト）
            const originText = document.createElement('span');
            originText.style.cssText = 'font-size: 10px; color: #666;';
            const ox = node.originX ?? 0.5;
            const oy = node.originY ?? 0.5;
            const originNames = { '0,0': '左上', '0.5,0': '上', '1,0': '右上', '0,0.5': '左', '0.5,0.5': '中央', '1,0.5': '右', '0,1': '左下', '0.5,1': '下', '1,1': '右下' };
            originText.textContent = originNames[`${ox},${oy}`] || '中央';
            thumbRow.appendChild(originText);

            controls.appendChild(thumbRow);
        }

        // X スライダー
        createNodeGraphPositionSlider({
            node, property: 'x', label: 'X',
            min: -500, max: 500, step: 0.1,
            container: controls
        });

        // Y スライダー
        createNodeGraphPositionSlider({
            node, property: 'y', label: 'Y',
            min: -500, max: 500, step: 0.1,
            container: controls
        });

        nodeBox.appendChild(controls);
    }

    // 独立フィルタノードの場合、主要パラメータ1つのみ表示（コンパクト）
    if (node.type === 'filter' && node.independent) {
        const filterDef = FILTER_DEFINITIONS[node.filterType];
        if (filterDef && filterDef.params.length > 0) {
            const paramDef = filterDef.params[0]; // 最初のパラメータのみ
            const currentValue = node.param ?? paramDef.default;

            const controls = document.createElement('div');
            controls.className = 'node-box-controls';
            controls.style.cssText = 'padding: 4px;';

            const label = document.createElement('label');
            label.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px;';

            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = String(paramDef.min);
            slider.max = String(paramDef.max);
            slider.step = String(paramDef.step);
            slider.value = String(currentValue);
            slider.style.cssText = 'flex: 1; min-width: 60px;';

            const display = document.createElement('span');
            display.style.cssText = 'min-width: 30px; text-align: right;';
            display.textContent = paramDef.format ? paramDef.format(currentValue) : String(currentValue);

            slider.addEventListener('input', (e) => {
                const value = parseFloat(e.target.value);
                node.param = value;
                display.textContent = paramDef.format ? paramDef.format(value) : String(value);
                throttledUpdatePreview();
            });

            label.appendChild(slider);
            label.appendChild(display);
            controls.appendChild(label);
            nodeBox.appendChild(controls);
        }
    }

    // 合成ノードの場合、入力数のみ表示（コンパクト）
    if (node.type === 'composite') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.style.cssText = 'padding: 4px; font-size: 11px; color: #666;';

        const inputCount = node.inputs ? node.inputs.length : 0;
        controls.textContent = `${inputCount} 入力`;

        nodeBox.appendChild(controls);
    }

    // アフィン変換ノードの場合、回転パラメータのみ表示（コンパクト）
    if (node.type === 'affine') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.style.cssText = 'padding: 4px;';

        const label = document.createElement('label');
        label.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px;';

        const span = document.createElement('span');
        span.textContent = '回転:';
        span.style.cssText = 'min-width: 28px;';

        const slider = document.createElement('input');
        slider.type = 'range';
        slider.min = '-180';
        slider.max = '180';
        slider.step = '0.1';
        slider.value = String(node.rotation || 0);
        slider.style.cssText = 'flex: 1; min-width: 50px;';

        const display = document.createElement('span');
        display.style.cssText = 'min-width: 40px; text-align: right;';
        display.textContent = `${(node.rotation || 0).toFixed(1)}°`;

        slider.addEventListener('input', (e) => {
            node.rotation = parseFloat(e.target.value);
            display.textContent = `${node.rotation.toFixed(1)}°`;
            node.matrix = calculateMatrixFromParams(
                node.translateX || 0,
                node.translateY || 0,
                node.rotation || 0,
                node.scaleX || 1,
                node.scaleY || 1
            );
            throttledUpdatePreview();
        });

        label.appendChild(span);
        label.appendChild(slider);
        label.appendChild(display);
        controls.appendChild(label);
        nodeBox.appendChild(controls);
    }

    // 9patchノードの場合、サムネイル + X,Yスライダーを表示
    if (node.type === 'ninepatch') {
        const content = contentLibrary.find(c => c.id === node.contentId);
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.style.cssText = 'padding: 4px;';

        // サムネイル + サイズ情報
        if (content && content.imageData) {
            const thumbRow = document.createElement('div');
            thumbRow.style.cssText = 'display: flex; align-items: center; gap: 6px; margin-bottom: 4px;';
            const img = document.createElement('img');
            img.src = createThumbnailDataURL(content.imageData);
            img.style.cssText = 'width: 32px; height: 32px; object-fit: cover; border-radius: 3px;';
            thumbRow.appendChild(img);

            const sizeInfo = document.createElement('span');
            sizeInfo.style.cssText = 'font-size: 10px; color: #666;';
            sizeInfo.textContent = `${node.outputWidth}×${node.outputHeight}`;
            sizeInfo.id = `ninepatch-size-${node.id}`;
            thumbRow.appendChild(sizeInfo);
            controls.appendChild(thumbRow);
        }

        // X スライダー（配置位置）
        createNodeGraphPositionSlider({
            node, property: 'x', label: 'X',
            min: -500, max: 500, step: 0.1,
            container: controls
        });

        // Y スライダー（配置位置）
        createNodeGraphPositionSlider({
            node, property: 'y', label: 'Y',
            min: -500, max: 500, step: 0.1,
            container: controls
        });

        nodeBox.appendChild(controls);
    }

    // Rendererノードの場合、仮想スクリーン情報を表示
    if (node.type === 'renderer') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.style.cssText = 'padding: 4px; font-size: 10px; color: #666; line-height: 1.4;';

        const vw = node.virtualWidth ?? canvasWidth;
        const vh = node.virtualHeight ?? canvasHeight;
        const ox = node.originX ?? canvasOrigin.x;
        const oy = node.originY ?? canvasOrigin.y;

        controls.innerHTML = `${vw}×${vh}<br>原点: ${ox.toFixed(0)}, ${oy.toFixed(0)}`;
        nodeBox.appendChild(controls);
    }

    // Sinkノードの場合、サムネイルと出力サイズ情報を表示
    if (node.type === 'sink') {
        const contentRow = document.createElement('div');
        contentRow.style.cssText = 'display: flex; align-items: center; gap: 8px; padding: 4px;';

        // サムネイル用Canvas
        const thumbnailCanvas = document.createElement('canvas');
        thumbnailCanvas.id = `sink-thumbnail-${node.id}`;
        thumbnailCanvas.width = 60;
        thumbnailCanvas.height = 45;
        thumbnailCanvas.style.cssText = 'border: 1px solid #444; background: #222; border-radius: 3px;';
        contentRow.appendChild(thumbnailCanvas);

        // 出力情報（contentLibraryから取得）
        const infoDiv = document.createElement('div');
        infoDiv.style.cssText = 'font-size: 10px; color: #666; line-height: 1.4;';
        const sinkContent = contentLibrary.find(c => c.id === node.contentId);
        const ow = sinkContent?.width ?? 0;
        const oh = sinkContent?.height ?? 0;
        const formatDisplay = PIXEL_FORMATS.find(f => f.formatName === (node.outputFormat ?? DEFAULT_PIXEL_FORMAT))?.displayName ?? 'RGBA8';
        infoDiv.innerHTML = `${ow}×${oh}<br>${formatDisplay}`;
        contentRow.appendChild(infoDiv);

        nodeBox.appendChild(contentRow);
    }

    foreignObject.appendChild(nodeBox);
    nodeGraphSvg.appendChild(foreignObject);

    // ポートを描画
    drawNodePorts(node, nodeWidth);

    // ドラッグ機能
    setupGlobalNodeDrag(nodeBox, foreignObject, node);
}

function drawNodePorts(node, nodeWidth) {
    const ns = 'http://www.w3.org/2000/svg';
    const nodeHeight = getNodeHeight(node); // 動的に計算
    const ports = getNodePorts(node);
    const portRadius = 6;

    // 入力ポート
    ports.inputs.forEach((port, index) => {
        const portCount = ports.inputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

        // 透明な大きめの円（クリックエリア拡大用）
        const hitArea = document.createElementNS(ns, 'circle');
        hitArea.setAttribute('cx', node.posX);
        hitArea.setAttribute('cy', y);
        hitArea.setAttribute('r', portRadius * 2); // 2倍の半径
        hitArea.setAttribute('class', 'node-port-hitarea node-port-hitarea-input');
        hitArea.setAttribute('fill', 'transparent');
        hitArea.setAttribute('stroke', 'none');
        hitArea.dataset.nodeId = node.id;
        hitArea.dataset.portId = port.id;
        hitArea.dataset.portType = 'input';
        hitArea.style.cursor = 'pointer';

        // ポートのドロップターゲット（マウス）
        hitArea.addEventListener('mouseup', () => {
            if (isDraggingConnection && dragConnectionFrom) {
                const fromNode = dragConnectionFrom.nodeId;
                const fromPort = dragConnectionFrom.portId;
                addConnection(fromNode, fromPort, node.id, port.id);
                stopDraggingConnection();
            }
        });

        // ポートのドロップターゲット（タッチ）
        hitArea.addEventListener('touchend', (e) => {
            if (isDraggingConnection && dragConnectionFrom) {
                const fromNode = dragConnectionFrom.nodeId;
                const fromPort = dragConnectionFrom.portId;
                addConnection(fromNode, fromPort, node.id, port.id);
                stopDraggingConnection();
                e.preventDefault();
            }
        });

        nodeGraphSvg.appendChild(hitArea);

        // 視覚的な円（実際に見えるポート）
        const circle = document.createElementNS(ns, 'circle');
        circle.setAttribute('cx', node.posX);
        circle.setAttribute('cy', y);
        circle.setAttribute('r', portRadius);
        circle.setAttribute('class', 'node-port node-port-input');
        circle.setAttribute('fill', '#667eea');  // 青（受ける側/Sink）
        circle.setAttribute('stroke', '#e2e8f0');
        circle.setAttribute('stroke-width', '2');
        circle.dataset.nodeId = node.id;
        circle.dataset.portId = port.id;
        circle.dataset.portType = 'input';
        circle.style.pointerEvents = 'none'; // クリックイベントは透明円に任せる

        nodeGraphSvg.appendChild(circle);
    });

    // 出力ポート
    ports.outputs.forEach((port, index) => {
        const portCount = ports.outputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

        // 透明な大きめの円（クリックエリア拡大用）
        const hitArea = document.createElementNS(ns, 'circle');
        hitArea.setAttribute('cx', node.posX + nodeWidth);
        hitArea.setAttribute('cy', y);
        hitArea.setAttribute('r', portRadius * 2); // 2倍の半径
        hitArea.setAttribute('class', 'node-port-hitarea node-port-hitarea-output');
        hitArea.setAttribute('fill', 'transparent');
        hitArea.setAttribute('stroke', 'none');
        hitArea.dataset.nodeId = node.id;
        hitArea.dataset.portId = port.id;
        hitArea.dataset.portType = 'output';
        hitArea.style.cursor = 'pointer';

        // ポートからドラッグ開始（マウス）
        hitArea.addEventListener('mousedown', (e) => {
            e.stopPropagation();
            console.log('mousedown on port:', node.id, port.id, 'dataset:', hitArea.dataset.nodeId);  // デバッグ用
            startDraggingConnection(node.id, port.id, e.clientX, e.clientY);
        });

        // ポートからドラッグ開始（タッチ）
        hitArea.addEventListener('touchstart', (e) => {
            e.stopPropagation();
            if (e.touches && e.touches[0]) {
                startDraggingConnection(node.id, port.id, e.touches[0].clientX, e.touches[0].clientY);
                e.preventDefault();
            }
        }, { passive: false });

        nodeGraphSvg.appendChild(hitArea);

        // 視覚的な円（実際に見えるポート）
        const circle = document.createElementNS(ns, 'circle');
        circle.setAttribute('cx', node.posX + nodeWidth);
        circle.setAttribute('cy', y);
        circle.setAttribute('r', portRadius);
        circle.setAttribute('class', 'node-port node-port-output');
        circle.setAttribute('fill', '#ff9800');  // オレンジ（送る側/Source）
        circle.setAttribute('stroke', '#e2e8f0');
        circle.setAttribute('stroke-width', '2');
        circle.dataset.nodeId = node.id;
        circle.dataset.portId = port.id;
        circle.dataset.portType = 'output';
        circle.style.pointerEvents = 'none'; // クリックイベントは透明円に任せる

        nodeGraphSvg.appendChild(circle);
    });
}

function startDraggingConnection(nodeId, portId, mouseX, mouseY) {
    console.log('startDraggingConnection:', nodeId, portId);  // デバッグ用
    isDraggingConnection = true;
    dragConnectionFrom = { nodeId, portId };

    // ドラッグ中の接続線を作成
    const ns = 'http://www.w3.org/2000/svg';
    dragConnectionPath = document.createElementNS(ns, 'path');
    dragConnectionPath.setAttribute('class', 'node-connection-drag');
    dragConnectionPath.setAttribute('stroke', '#667eea');
    dragConnectionPath.setAttribute('stroke-width', '2');
    dragConnectionPath.setAttribute('stroke-dasharray', '5,5');
    dragConnectionPath.setAttribute('fill', 'none');
    nodeGraphSvg.appendChild(dragConnectionPath);

    updateDragConnectionPath(mouseX, mouseY);
}

function updateDragConnectionPath(mouseX, mouseY) {
    if (!dragConnectionPath || !dragConnectionFrom) return;

    const fromNode = globalNodes.find(n => n.id === dragConnectionFrom.nodeId);
    if (!fromNode) return;

    const fromPos = getPortPosition(fromNode, dragConnectionFrom.portId, 'output');

    // SVG座標に変換
    const svgRect = nodeGraphSvg.getBoundingClientRect();
    const toX = mouseX - svgRect.left;
    const toY = mouseY - svgRect.top;

    const midX = (fromPos.x + toX) / 2;
    const d = `M ${fromPos.x} ${fromPos.y} C ${midX} ${fromPos.y}, ${midX} ${toY}, ${toX} ${toY}`;
    dragConnectionPath.setAttribute('d', d);
}

function stopDraggingConnection() {
    isDraggingConnection = false;
    dragConnectionFrom = null;

    if (dragConnectionPath) {
        dragConnectionPath.remove();
        dragConnectionPath = null;
    }
}

function setupGlobalNodeDrag(nodeBox, foreignObject, node) {
    let isDragging = false;
    let startX, startY;
    let initialX, initialY;

    const handleMove = (e) => {
        if (!isDragging) return;

        // タッチとマウスの両方に対応
        const clientX = e.clientX || (e.touches && e.touches[0].clientX);
        const clientY = e.clientY || (e.touches && e.touches[0].clientY);

        const dx = clientX - startX;
        const dy = clientY - startY;

        const newX = Math.max(0, initialX + dx);
        const newY = Math.max(0, initialY + dy);

        foreignObject.setAttribute('x', newX);
        foreignObject.setAttribute('y', newY);

        node.posX = newX;
        node.posY = newY;

        // ポートの位置を更新
        updateNodePortsPosition(node);

        // 接続線を再描画
        updateConnectionsForNode(node.id);

        e.preventDefault();
    };

    const handleEnd = () => {
        if (isDragging) {
            isDragging = false;
            nodeBox.classList.remove('dragging');
            // リスナーを削除
            document.removeEventListener('mousemove', handleMove);
            document.removeEventListener('mouseup', handleEnd);
            document.removeEventListener('touchmove', handleMove);
            document.removeEventListener('touchend', handleEnd);
            // 再描画不要：ポートと接続線はhandleMove内で既に更新済み
        }
    };

    const handleStart = (e) => {
        // クリックがスライダーやボタンの場合はドラッグしない
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'BUTTON') {
            return;
        }

        isDragging = true;
        nodeBox.classList.add('dragging');

        // タッチとマウスの両方に対応
        const clientX = e.clientX || (e.touches && e.touches[0].clientX);
        const clientY = e.clientY || (e.touches && e.touches[0].clientY);

        startX = clientX;
        startY = clientY;
        initialX = parseFloat(foreignObject.getAttribute('x'));
        initialY = parseFloat(foreignObject.getAttribute('y'));

        // マウスとタッチの両方のリスナーを追加
        document.addEventListener('mousemove', handleMove);
        document.addEventListener('mouseup', handleEnd);
        document.addEventListener('touchmove', handleMove, { passive: false });
        document.addEventListener('touchend', handleEnd);

        e.preventDefault();
        e.stopPropagation();
    };

    // 右クリックメニュー
    nodeBox.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        e.stopPropagation();
        showContextMenu(e.clientX, e.clientY, node);
    });

    // ダブルクリックで詳細パネルを開く
    nodeBox.addEventListener('dblclick', (e) => {
        // スライダーやボタンの場合はスキップ
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'BUTTON') {
            return;
        }
        e.preventDefault();
        e.stopPropagation();
        showNodeDetailPanel(node);
    });

    // 長押し検出（スマートフォン用）
    let longPressTimer = null;
    nodeBox.addEventListener('touchstart', (e) => {
        // スライダーやボタンの場合はスキップ
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'BUTTON') {
            return;
        }

        longPressTimer = setTimeout(() => {
            if (!isDragging) {
                const touch = e.touches[0];
                showContextMenu(touch.clientX, touch.clientY, node);
            }
        }, 500); // 500msの長押し
    }, { passive: true });

    nodeBox.addEventListener('touchend', () => {
        if (longPressTimer) {
            clearTimeout(longPressTimer);
            longPressTimer = null;
        }
    });

    nodeBox.addEventListener('touchmove', () => {
        if (longPressTimer) {
            clearTimeout(longPressTimer);
            longPressTimer = null;
        }
    });

    // マウスとタッチの両方のイベントを登録
    nodeBox.addEventListener('mousedown', handleStart);
    nodeBox.addEventListener('touchstart', handleStart, { passive: false });
}

// ノードのポート位置を更新
function updateNodePortsPosition(node) {
    const nodeWidth = 160;
    const nodeHeight = getNodeHeight(node); // 動的に計算
    const ports = getNodePorts(node);

    // 入力ポートを更新（透明なヒットエリア円と視覚的な円の両方）
    ports.inputs.forEach((port, index) => {
        const portCount = ports.inputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

        // 透明なヒットエリア円を更新
        const hitArea = nodeGraphSvg.querySelector(
            `circle.node-port-hitarea-input[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (hitArea) {
            hitArea.setAttribute('cx', node.posX);
            hitArea.setAttribute('cy', y);
        }

        // 視覚的な円を更新
        const portElement = nodeGraphSvg.querySelector(
            `circle.node-port-input[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (portElement) {
            portElement.setAttribute('cx', node.posX);
            portElement.setAttribute('cy', y);
        }
    });

    // 出力ポートを更新（透明なヒットエリア円と視覚的な円の両方）
    ports.outputs.forEach((port, index) => {
        const portCount = ports.outputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

        // 透明なヒットエリア円を更新
        const hitArea = nodeGraphSvg.querySelector(
            `circle.node-port-hitarea-output[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (hitArea) {
            hitArea.setAttribute('cx', node.posX + nodeWidth);
            hitArea.setAttribute('cy', y);
        }

        // 視覚的な円を更新
        const portElement = nodeGraphSvg.querySelector(
            `circle.node-port-output[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (portElement) {
            portElement.setAttribute('cx', node.posX + nodeWidth);
            portElement.setAttribute('cy', y);
        }
    });
}

// 特定ノードに関連する接続線を更新（リアルタイム更新版）
function updateConnectionsForNode(nodeId) {
    // 該当するノードの接続線のみをリアルタイムで更新
    globalConnections.forEach(conn => {
        if (conn.fromNodeId === nodeId || conn.toNodeId === nodeId) {
            const fromNode = globalNodes.find(n => n.id === conn.fromNodeId);
            const toNode = globalNodes.find(n => n.id === conn.toNodeId);

            if (fromNode && toNode) {
                // 接続線のパスを探す
                const pathId = `conn-${conn.fromNodeId}-${conn.fromPortId}-${conn.toNodeId}-${conn.toPortId}`;
                const path = document.getElementById(pathId);

                if (path) {
                    // パスのd属性だけを更新（削除・再作成しない）
                    const fromPos = getPortPosition(fromNode, conn.fromPortId, 'output');
                    const toPos = getPortPosition(toNode, conn.toPortId, 'input');
                    const d = calculateConnectionPath(fromPos, toPos);
                    path.setAttribute('d', d);
                }
            }
        }
    });
}

// ========================================
// ノードグラフ - 接続システム
// ========================================

// ノードのポート定義を取得
function getNodePorts(node) {
    const ports = { inputs: [], outputs: [] };

    switch (node.type) {
        case 'image':
            // 画像ノード: 出力のみ
            ports.outputs.push({ id: 'out', label: '画像', type: 'image' });
            break;

        case 'filter':
            // フィルタノード: 入力1つ、出力1つ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'composite':
            // 合成ノード: 動的な入力数、出力1つ
            if (node.inputs && node.inputs.length > 0) {
                node.inputs.forEach((input, index) => {
                    ports.inputs.push({
                        id: input.id,
                        label: `入力${index + 1}`,
                        type: 'image'
                    });
                });
            }
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'distributor':
            // 分配ノード: 入力1つ、動的な出力数
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            if (node.outputs && node.outputs.length > 0) {
                node.outputs.forEach((output, index) => {
                    ports.outputs.push({
                        id: output.id,
                        label: `出力${index + 1}`,
                        type: 'image'
                    });
                });
            }
            break;

        case 'affine':
            // アフィン変換ノード: 入力1つ、出力1つ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'ninepatch':
            // 9patchノード: 出力のみ（ソースノードと同様）
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'renderer':
            // Rendererノード: 入力1つ、出力1つ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'sink':
            // Sinkノード: 入力のみ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            break;
    }

    return ports;
}

// 接続を追加
function addConnection(fromNodeId, fromPortId, toNodeId, toPortId) {
    // 既存の接続をチェック（入力ポートへの接続は1つのみ）
    const existingToIndex = globalConnections.findIndex(
        conn => conn.toNodeId === toNodeId && conn.toPortId === toPortId
    );

    // 出力ポートからの接続も1つのみに制限
    const existingFromIndex = globalConnections.findIndex(
        conn => conn.fromNodeId === fromNodeId && conn.fromPortId === fromPortId
    );

    // 出力側に既存接続がある場合は削除
    if (existingFromIndex >= 0) {
        globalConnections.splice(existingFromIndex, 1);
    }

    // 入力側に既存接続がある場合のインデックスを再取得（削除でずれる可能性）
    const existingToIndexNew = globalConnections.findIndex(
        conn => conn.toNodeId === toNodeId && conn.toPortId === toPortId
    );

    if (existingToIndexNew >= 0) {
        // 既存の接続を置き換え
        globalConnections[existingToIndexNew] = {
            fromNodeId,
            fromPortId,
            toNodeId,
            toPortId
        };
    } else {
        globalConnections.push({
            fromNodeId,
            fromPortId,
            toNodeId,
            toPortId
        });
    }

    renderNodeGraph();
    updatePreviewFromGraph();
}

// 接続を削除
function removeConnection(fromNodeId, fromPortId, toNodeId, toPortId) {
    globalConnections = globalConnections.filter(
        conn => !(conn.fromNodeId === fromNodeId &&
                 conn.fromPortId === fromPortId &&
                 conn.toNodeId === toNodeId &&
                 conn.toPortId === toPortId)
    );

    renderNodeGraph();
    updatePreviewFromGraph();
}

// 合成ノードを追加
function addCompositeNode() {
    // 表示範囲の中央に固定配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 90;
    const posX = center.x - nodeWidth / 2 + randomOffset();
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const compositeNode = {
        id: `composite-${nextCompositeId++}`,
        type: 'composite',
        title: '合成',
        posX: posX,
        posY: posY,
        // 動的な入力配列（デフォルトで2つの入力）
        inputs: [
            { id: 'in1' },
            { id: 'in2' }
        ]
    };

    globalNodes.push(compositeNode);
    renderNodeGraph();
    scheduleAutoSave();
}

// アフィン変換ノードを追加
function addAffineNode() {
    // 表示範囲の中央に固定配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 90;
    const posX = center.x - nodeWidth / 2 + randomOffset();
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const affineNode = {
        id: `affine-${Date.now()}`,
        type: 'affine',
        title: 'アフィン変換',
        posX: posX,
        posY: posY,
        matrixMode: false,  // デフォルトはパラメータモード
        // パラメータモード用の初期値
        translateX: 0,
        translateY: 0,
        rotation: 0,
        scaleX: 1.0,
        scaleY: 1.0,
        // 行列モード用の初期値
        matrix: {
            a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0
        }
    };

    globalNodes.push(affineNode);
    renderNodeGraph();
    scheduleAutoSave();
}

// 合成ノードに入力を追加
function addCompositeInput(node) {
    if (!node.inputs) {
        node.inputs = [];
    }

    const newIndex = node.inputs.length + 1;
    node.inputs.push({
        id: `in${newIndex}`
    });

    // ノードグラフを再描画
    renderNodeGraph();
    scheduleAutoSave();
}

// 分配ノードを追加
function addDistributorNode() {
    // 表示範囲の中央に固定配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 90;
    const posX = center.x - nodeWidth / 2 + randomOffset();
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const distributorNode = {
        id: `distributor-${nextDistributorId++}`,
        type: 'distributor',
        title: '分配',
        posX: posX,
        posY: posY,
        // 動的な出力配列（デフォルトで2つの出力）
        outputs: [
            { id: 'out1' },
            { id: 'out2' }
        ]
    };

    globalNodes.push(distributorNode);
    renderNodeGraph();
    scheduleAutoSave();
}

// 分配ノードに出力を追加
function addDistributorOutput(node) {
    if (!node.outputs) {
        node.outputs = [];
    }

    const newIndex = node.outputs.length + 1;
    node.outputs.push({
        id: `out${newIndex}`
    });

    // ノードグラフを再描画
    renderNodeGraph();
    scheduleAutoSave();
}

// 9patchノードIDカウンタ
let nextNinePatchNodeId = 1;

// 9patchノードを追加
function addNinePatchNode(contentId = null) {
    // contentIdが指定されていない場合、最初の9patch画像を探す
    let content = null;
    if (contentId) {
        content = contentLibrary.find(c => c.id === contentId && c.isNinePatch);
    }
    if (!content) {
        // ライブラリから最初の9patch画像を探す
        content = contentLibrary.find(c => c.type === 'image' && c.isNinePatch);
    }
    if (!content) {
        alert('9patch画像がコンテンツライブラリにありません。\n9patch形式の画像をアップロードしてください。');
        return;
    }

    // 表示範囲の中央に固定配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 120;  // サムネイル + 幅/高さスライダー
    const posX = center.x - nodeWidth / 2 + randomOffset();
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    // 9patchの内部サイズ（メタデータの1px境界を除く）
    const contentWidth = content.width - 2;
    const contentHeight = content.height - 2;

    const ninepatchNode = {
        id: `ninepatch-${nextNinePatchNodeId++}`,
        type: 'ninepatch',
        contentId: content.id,
        title: content.name || '9patch',
        posX: posX,
        posY: posY,
        // 出力サイズ（デフォルトはコンテンツサイズ）
        outputWidth: contentWidth,
        outputHeight: contentHeight,
        // 原点（正規化座標 0.0〜1.0）
        originX: 0.5,
        originY: 0.5
    };

    globalNodes.push(ninepatchNode);
    renderNodeGraph();
    throttledUpdatePreview();
    scheduleAutoSave();
}

// 独立フィルタノードを追加（レイヤーに属さない）
function addIndependentFilterNode(filterType) {
    const filterDef = FILTER_DEFINITIONS[filterType];
    if (!filterDef) {
        console.warn(`Unknown filter type: ${filterType}`);
        return;
    }

    // デフォルトパラメータをFILTER_DEFINITIONSから取得
    // 複数パラメータ対応: params配列からデフォルト値を取得
    const defaultParam = filterDef.params.length > 0
        ? filterDef.params[0].default
        : 0.0;

    // 表示範囲の中央に固定配置 + ランダムオフセット
    const center = getVisibleNodeGraphCenter();
    const nodeWidth = 160;
    const nodeHeight = 70;
    const posX = center.x - nodeWidth / 2 + randomOffset();
    const posY = center.y - nodeHeight / 2 + randomOffset();

    // 既存ノードを押し出す
    pushExistingNodes(posX, posY, nodeWidth, nodeHeight);

    const filterNode = {
        id: `independent-filter-${nextIndependentFilterId++}`,
        type: 'filter',
        independent: true,  // 独立フィルタノードであることを示すフラグ
        filterType: filterType,
        param: defaultParam,
        title: getFilterDisplayName(filterType),
        posX: posX,
        posY: posY
    };

    globalNodes.push(filterNode);
    renderNodeGraph();
    scheduleAutoSave();
}

// フィルタ表示名を取得
function getFilterDisplayName(filterType) {
    const filterDef = FILTER_DEFINITIONS[filterType];
    return filterDef ? filterDef.name : filterType;
}


// グラフベースのプレビュー更新（C++側で完結）
function updatePreviewFromGraph() {
    const perfStart = performance.now();

    // フォーカス中のコンテンツを取得
    const focusedContent = contentLibrary.find(c => c.id === focusedContentId);

    // 全てのSinkノードを収集（複数Sink対応）
    const allSinkNodes = globalNodes.filter(n => {
        if (n.type !== 'sink') return false;
        // contentIdが設定されているSinkのみ対象
        const content = contentLibrary.find(c => c.id === n.contentId);
        return content && content.type === 'output';
    });

    if (allSinkNodes.length === 0) {
        // Sinkノードがない場合はフォーカス中のコンテンツを表示
        updateFocusedPreview();
        return;
    }

    // プレビュー表示用のSinkを決定
    // 1. フォーカスが出力バッファの場合、そのcontentIdを持つSinkを探す
    // 2. そうでなければ、最初のSinkを使う
    let displaySinkNode = null;
    if (focusedContent && focusedContent.type === 'output') {
        displaySinkNode = allSinkNodes.find(n => n.contentId === focusedContentId);
    }
    if (!displaySinkNode) {
        displaySinkNode = allSinkNodes[0];
    }

    // C++側にノードグラフ構造を渡す（1回のWASM呼び出しで完結）
    const evalStart = performance.now();

    // ノードデータをC++に渡す形式に変換
    const nodesForCpp = globalNodes.map(node => {
        // 画像ノード: 正規化originをピクセル座標に変換、contentIdをcppImageIdに変換
        if (node.type === 'image') {
            const content = contentLibrary.find(c => c.id === node.contentId);
            if (content) {
                const ox = node.originX ?? 0.5;
                const oy = node.originY ?? 0.5;
                return {
                    ...node,
                    imageId: content.cppImageId,  // C++側に渡す数値ID
                    // ピクセル座標に変換してC++に渡す
                    originX: ox * content.width,
                    originY: oy * content.height,
                    // 配置位置（オブジェクト形式で渡す）
                    position: {
                        x: node.position?.x ?? 0,
                        y: node.position?.y ?? 0
                    },
                    bilinear: node.bilinear || false  // バイリニア補間フラグ
                };
            }
        }
        // アフィンノード: パラメータを行列に統一
        if (node.type === 'affine' && !node.matrixMode) {
            const matrix = calculateMatrixFromParams(
                node.translateX || 0,
                node.translateY || 0,
                node.rotation || 0,
                node.scaleX !== undefined ? node.scaleX : 1,
                node.scaleY !== undefined ? node.scaleY : 1
            );
            return {
                ...node,
                matrixMode: true,  // C++には常に行列モードとして渡す
                matrix: matrix
            };
        }
        // Sinkノード: contentLibraryからサイズを取得
        if (node.type === 'sink') {
            const content = contentLibrary.find(c => c.id === node.contentId);
            if (content) {
                return {
                    ...node,
                    outputWidth: content.width,
                    outputHeight: content.height,
                    imageId: content.cppImageId  // 出力先の画像ID
                };
            }
        }
        // フィルタノード: paramをfilterParams配列に変換
        if (node.type === 'filter' && node.independent) {
            const filterDef = FILTER_DEFINITIONS[node.filterType];
            // 現在のパラメータを配列形式に変換
            // 将来的に複数パラメータ対応する際はnode.paramsを使用
            const filterParams = node.param !== undefined ? [node.param] : [];
            return {
                ...node,
                filterParams: filterParams
            };
        }
        // 9patchノード: contentIdをcppImageIdに変換
        if (node.type === 'ninepatch') {
            const content = contentLibrary.find(c => c.id === node.contentId);
            if (content) {
                const outW = node.outputWidth ?? (content.width - 2);
                const outH = node.outputHeight ?? (content.height - 2);
                // 正規化座標（0.0〜1.0）をピクセル座標に変換
                const ox = (node.originX ?? 0.5) * outW;
                const oy = (node.originY ?? 0.5) * outH;
                return {
                    ...node,
                    imageId: content.cppImageId,  // C++側に渡す数値ID
                    outputWidth: outW,
                    outputHeight: outH,
                    originX: ox,
                    originY: oy,
                    // 配置位置（オブジェクト形式で渡す）
                    position: {
                        x: node.position?.x ?? 0,
                        y: node.position?.y ?? 0
                    },
                    bilinear: node.bilinear || false  // バイリニア補間フラグ
                };
            }
        }
        return node;
    });

    graphEvaluator.setNodes(nodesForCpp);
    graphEvaluator.setConnections(globalConnections);

    // 全てのSinkの出力バッファを確保（複数Sink対応）
    for (const sinkNode of allSinkNodes) {
        const sinkContent = contentLibrary.find(c => c.id === sinkNode.contentId);
        if (sinkContent) {
            graphEvaluator.allocateImage(sinkContent.cppImageId, sinkContent.width, sinkContent.height);
        }
    }

    // C++側でノードグラフ全体を評価（全Sinkに出力される）
    // 戻り値: 0 = 成功、1 = 循環参照検出
    const execResult = graphEvaluator.evaluateGraph();
    if (execResult === 1) {
        console.error('Cycle detected in node graph');
        alert('エラー: ノードグラフに循環参照があります。接続を修正してください。');
        return;
    }

    const evalTime = performance.now() - evalStart;

    // 全てのSinkの結果をcontentLibraryに保存
    let hasValidResult = false;
    for (const sinkNode of allSinkNodes) {
        const sinkContent = contentLibrary.find(c => c.id === sinkNode.contentId);
        if (!sinkContent) continue;

        const resultData = graphEvaluator.getImage(sinkContent.cppImageId);
        if (resultData && resultData.length > 0) {
            sinkContent.imageData = {
                data: new Uint8ClampedArray(resultData),
                width: sinkContent.width,
                height: sinkContent.height
            };
            hasValidResult = true;
        }
    }

    console.log('updatePreviewFromGraph: processed', allSinkNodes.length, 'Sink nodes, hasValidResult=', hasValidResult);

    if (hasValidResult) {

        // フォーカス中のコンテンツをプレビュー表示
        const drawStart = performance.now();
        updateFocusedPreview();
        const drawTime = performance.now() - drawStart;

        // C++側の詳細計測結果を取得（時間はマイクロ秒）
        const metrics = graphEvaluator.getPerfMetrics();
        const totalTime = performance.now() - perfStart;

        // マイクロ秒→ミリ秒変換ヘルパー
        const usToMs = (us) => (us / 1000).toFixed(2);

        // 詳細ログ出力（NODE_TYPESを使用）
        const details = [];
        if (metrics.nodes) {
            for (let i = 0; i < metrics.nodes.length; i++) {
                const m = metrics.nodes[i];
                const typeDef = NodeTypeHelper.byIndex(i);
                if (m.count > 0 && typeDef) {
                    let entry = `${typeDef.name}: ${usToMs(m.time_us)}ms (x${m.count})`;
                    // ピクセル効率を表示（showEfficiencyフラグで制御）
                    if (typeDef.showEfficiency && m.requestedPixels > 0) {
                        const efficiency = ((1.0 - m.wasteRatio) * 100).toFixed(1);
                        entry += ` [eff:${efficiency}%]`;
                        // Affine ノードのみAABB分割効果の推定値を表示（改善倍率）
                        if (typeDef.index === NODE_TYPES.affine.index && m.splitEfficiencyEstimate > 0) {
                            const improvementFactor = (1.0 / m.splitEfficiencyEstimate).toFixed(1);
                            entry += ` (aabb:${improvementFactor}x)`;
                        }
                    }
                    details.push(entry);
                }
            }
        }

        console.log(`[Perf] Total: ${totalTime.toFixed(1)}ms | WASM: ${evalTime.toFixed(1)}ms (${details.join(', ')}) | Draw: ${drawTime.toFixed(1)}ms`);

        // デバッグステータスバーを更新
        updateDebugStatusBar(totalTime, evalTime, details);

        // サイドバーのデバッグ詳細セクションを更新
        updateDebugDetails(metrics);
    } else {
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
    }

    // Sinkノードのサムネイルを更新
    updateSinkThumbnails();

    // 状態を自動保存
    scheduleAutoSave();
}

// Sinkノードのサムネイルを更新
function updateSinkThumbnails() {
    if (!graphEvaluator) return;

    globalNodes.filter(n => n.type === 'sink').forEach(sinkNode => {
        const thumbnailCanvas = document.getElementById(`sink-thumbnail-${sinkNode.id}`);
        if (!thumbnailCanvas) return;

        // C++側からプレビューデータを取得
        const preview = graphEvaluator.getSinkPreview(sinkNode.id);
        if (!preview || !preview.data || preview.width === 0 || preview.height === 0) {
            // プレビューがない場合はクリア
            const thumbCtx = thumbnailCanvas.getContext('2d');
            thumbCtx.clearRect(0, 0, thumbnailCanvas.width, thumbnailCanvas.height);
            return;
        }

        // ImageDataを作成
        const imageData = new ImageData(
            new Uint8ClampedArray(preview.data),
            preview.width,
            preview.height
        );

        // 一時Canvasで描画
        const tempCanvas = document.createElement('canvas');
        tempCanvas.width = preview.width;
        tempCanvas.height = preview.height;
        tempCanvas.getContext('2d').putImageData(imageData, 0, 0);

        // サムネイルに縮小描画
        const thumbCtx = thumbnailCanvas.getContext('2d');
        thumbCtx.clearRect(0, 0, thumbnailCanvas.width, thumbnailCanvas.height);

        // アスペクト比を維持して中央に描画
        const scale = Math.min(
            thumbnailCanvas.width / preview.width,
            thumbnailCanvas.height / preview.height
        );
        const w = preview.width * scale;
        const h = preview.height * scale;
        const x = (thumbnailCanvas.width - w) / 2;
        const y = (thumbnailCanvas.height - h) / 2;

        thumbCtx.drawImage(tempCanvas, x, y, w, h);
    });
}

// デバッグステータスバーを更新
function updateDebugStatusBar(totalTime, wasmTime, details) {
    const totalEl = document.getElementById('debug-perf-total');
    const detailsEl = document.getElementById('debug-perf-details');

    if (totalEl) {
        totalEl.textContent = `Total: ${totalTime.toFixed(1)}ms | WASM: ${wasmTime.toFixed(1)}ms`;
    }
    if (detailsEl) {
        detailsEl.textContent = details.join(' | ');
    }
}

// デバッグセクションのHTML生成（NODE_TYPESから動的生成）
function initDebugDetailsSection() {
    const container = document.getElementById('debug-details');
    if (!container) return;

    // 表示名取得ヘルパー（日本語名優先）
    const getDisplayName = (def) => def.nameJa || def.name;

    // 処理時間セクション
    let timeHtml = `
        <div class="debug-section">
            <div class="debug-section-header">処理時間</div>
            <div class="debug-metrics" id="debug-metrics-time">`;

    // カテゴリ別にグループ化して表示
    const systemTypes = NodeTypeHelper.byCategory('system');
    const sourceTypes = NodeTypeHelper.byCategory('source');
    const structureTypes = NodeTypeHelper.byCategory('structure');
    const filterTypes = NodeTypeHelper.byCategory('filter');

    // システム系ノード（Distributor/Sink、Rendererは計測しない）
    for (const [key, def] of systemTypes) {
        if (key === 'renderer') continue;
        timeHtml += `
                <div class="debug-metric-row">
                    <span class="debug-metric-label">${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-time">--</span>
                </div>`;
    }

    // ソース系ノード（NinePatchは内部でSourceを使うため除外）
    for (const [key, def] of sourceTypes) {
        if (key === 'ninepatch') continue;
        timeHtml += `
                <div class="debug-metric-row">
                    <span class="debug-metric-label">${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-time">--</span>
                </div>`;
    }

    // 構造系ノード（Affineはアフィン伝播によりSourceで処理されるため除外）
    for (const [key, def] of structureTypes) {
        if (key === 'affine') continue;
        timeHtml += `
                <div class="debug-metric-row">
                    <span class="debug-metric-label">${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-time">--</span>
                </div>`;
    }

    // フィルタ系ノード（インデント表示）
    if (filterTypes.length > 0) {
        timeHtml += `
                <div class="debug-metric-row debug-metric-sub">
                    <span class="debug-metric-label">フィルタ:</span>
                    <span class="debug-metric-value"></span>
                </div>`;
        for (const [key, def] of filterTypes) {
            timeHtml += `
                <div class="debug-metric-row debug-metric-sub">
                    <span class="debug-metric-label">├ ${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-time">--</span>
                </div>`;
        }
    }

    // 合計
    timeHtml += `
                <div class="debug-metric-row debug-metric-total">
                    <span class="debug-metric-label">合計</span>
                    <span class="debug-metric-value" id="debug-total-time">--</span>
                </div>
            </div>
        </div>`;

    // メモリセクション
    let memHtml = `
        <div class="debug-section">
            <div class="debug-section-header">メモリ</div>
            <div class="debug-metrics">
                <div class="debug-metric-row">
                    <span class="debug-metric-label">累計確保</span>
                    <span class="debug-metric-value" id="debug-alloc-bytes">--</span>
                </div>
                <div class="debug-metric-row">
                    <span class="debug-metric-label">ピーク</span>
                    <span class="debug-metric-value" id="debug-peak-bytes">--</span>
                </div>
                <div class="debug-metric-row">
                    <span class="debug-metric-label">最大単一</span>
                    <span class="debug-metric-value" id="debug-max-alloc">--</span>
                </div>`;

    // 構造系ノードのメモリ（Affineは除外）
    for (const [key, def] of structureTypes) {
        if (key === 'affine') continue;
        memHtml += `
                <div class="debug-metric-row debug-metric-sub">
                    <span class="debug-metric-label">├ ${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-alloc">--</span>
                </div>
                <div class="debug-metric-row debug-metric-sub debug-metric-max">
                    <span class="debug-metric-label">│  └ max</span>
                    <span class="debug-metric-value" id="debug-${key}-max">--</span>
                </div>`;
    }

    // フィルタ系ノードのメモリ
    for (const [key, def] of filterTypes) {
        memHtml += `
                <div class="debug-metric-row debug-metric-sub">
                    <span class="debug-metric-label">├ ${getDisplayName(def)}</span>
                    <span class="debug-metric-value" id="debug-${key}-alloc">--</span>
                </div>
                <div class="debug-metric-row debug-metric-sub debug-metric-max">
                    <span class="debug-metric-label">│  └ max</span>
                    <span class="debug-metric-value" id="debug-${key}-max">--</span>
                </div>`;
    }

    memHtml += `
            </div>
        </div>`;

    container.innerHTML = timeHtml + memHtml;
}

// サイドバーのデバッグ詳細セクションを更新
function updateDebugDetails(metrics) {
    if (!metrics) return;

    const usToMs = (us) => (us / 1000).toFixed(2);
    const formatBytes = (bytes) => {
        if (bytes < 1024) return `${bytes} B`;
        if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
        return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
    };

    // ノードタイプ別の処理時間・メモリを更新
    if (metrics.nodes) {
        for (const [key, def] of Object.entries(NODE_TYPES)) {
            const m = metrics.nodes[def.index];
            if (!m) continue;

            // 処理時間
            const timeEl = document.getElementById(`debug-${key}-time`);
            if (timeEl) {
                if (m.count > 0) {
                    let text = `${usToMs(m.time_us)}ms (x${m.count})`;
                    if (def.showEfficiency && m.requestedPixels > 0) {
                        const efficiency = ((1.0 - m.wasteRatio) * 100).toFixed(1);
                        text += ` [${efficiency}%]`;
                        // Affine ノードのみAABB分割効果の推定値を表示（改善倍率）
                        if (key === 'affine' && m.splitEfficiencyEstimate > 0) {
                            const improvementFactor = (1.0 / m.splitEfficiencyEstimate).toFixed(1);
                            text += ` (aabb:${improvementFactor}x)`;
                        }
                    }
                    timeEl.textContent = text;
                } else {
                    timeEl.textContent = '--';
                }
            }

            // メモリ確保量
            const allocEl = document.getElementById(`debug-${key}-alloc`);
            if (allocEl) {
                if (m.allocCount > 0) {
                    allocEl.textContent = `${formatBytes(m.allocatedBytes)} (x${m.allocCount})`;
                } else {
                    allocEl.textContent = '--';
                }
            }

            // 最大単一確保
            const maxEl = document.getElementById(`debug-${key}-max`);
            if (maxEl) {
                if (m.maxAllocBytes > 0) {
                    maxEl.textContent = `${formatBytes(m.maxAllocBytes)} (${m.maxAllocWidth}x${m.maxAllocHeight})`;
                } else {
                    maxEl.textContent = '--';
                }
            }
        }
    }

    // 合計時間
    const totalEl = document.getElementById('debug-total-time');
    if (totalEl && metrics.totalTime !== undefined) {
        totalEl.textContent = `${usToMs(metrics.totalTime)}ms`;
    }

    // メモリ確保量（累計）
    const allocEl = document.getElementById('debug-alloc-bytes');
    if (allocEl && metrics.totalAllocBytes !== undefined) {
        allocEl.textContent = formatBytes(metrics.totalAllocBytes);
    }

    // ピークメモリ
    const peakEl = document.getElementById('debug-peak-bytes');
    if (peakEl && metrics.peakMemoryBytes !== undefined) {
        peakEl.textContent = formatBytes(metrics.peakMemoryBytes);
    }

    // 最大単一確保
    const maxAllocEl = document.getElementById('debug-max-alloc');
    if (maxAllocEl && metrics.maxAllocBytes !== undefined) {
        if (metrics.maxAllocBytes > 0) {
            maxAllocEl.textContent = `${formatBytes(metrics.maxAllocBytes)} (${metrics.maxAllocWidth}x${metrics.maxAllocHeight})`;
        } else {
            maxAllocEl.textContent = '--';
        }
    }
}

// ステータスバークリックでデバッグセクションを開く
function setupDebugStatusBarClick() {
    const statusBar = document.getElementById('debug-status-bar');
    if (statusBar) {
        statusBar.addEventListener('click', () => {
            // サイドバーを開く
            const sidebar = document.getElementById('sidebar');
            const toggle = document.getElementById('sidebar-toggle');
            const overlay = document.getElementById('sidebar-overlay');
            if (sidebar && !sidebar.classList.contains('open')) {
                sidebar.classList.add('open');
                toggle?.classList.add('open');
                overlay?.classList.add('visible');
                document.body.classList.add('sidebar-open');
            }

            // デバッグセクションを開く
            const debugItem = document.querySelector('.sidebar-accordion-item[data-accordion="debug"]');
            if (debugItem && !debugItem.classList.contains('active')) {
                // 他のアイテムを閉じる
                document.querySelectorAll('.sidebar-accordion-item').forEach(item => {
                    if (item !== debugItem) {
                        item.classList.remove('active');
                        const icon = item.querySelector('.sidebar-accordion-icon');
                        if (icon) icon.textContent = '▶';
                    }
                });
                // デバッグアイテムを開く
                debugItem.classList.add('active');
                const icon = debugItem.querySelector('.sidebar-accordion-icon');
                if (icon) icon.textContent = '▼';
            }
        });
    }
}

// ========================================
// ノードコンテキストメニュー & 削除機能
// ========================================

let contextMenuTargetNode = null;
const contextMenu = document.getElementById('node-context-menu');

// コンテキストメニューを表示
function showContextMenu(x, y, node) {
    contextMenuTargetNode = node;

    // 合成ノードの場合のみ「入力を追加」を表示
    const addInputMenu = document.getElementById('add-input-menu');
    if (node.type === 'composite') {
        addInputMenu.style.display = 'block';
    } else {
        addInputMenu.style.display = 'none';
    }

    // 分配ノードの場合のみ「出力を追加」を表示
    const addOutputMenu = document.getElementById('add-output-menu');
    if (addOutputMenu) {
        if (node.type === 'distributor') {
            addOutputMenu.style.display = 'block';
        } else {
            addOutputMenu.style.display = 'none';
        }
    }

    contextMenu.style.left = x + 'px';
    contextMenu.style.top = y + 'px';
    contextMenu.style.display = 'block';
}

// コンテキストメニューを非表示
function hideContextMenu() {
    contextMenu.style.display = 'none';
    contextMenuTargetNode = null;
}

// ドキュメント全体のクリックでメニューを閉じる
document.addEventListener('click', (e) => {
    if (!contextMenu.contains(e.target)) {
        hideContextMenu();
    }
});

// 削除メニューアイテムのクリック
document.getElementById('delete-node-menu').addEventListener('click', () => {
    if (contextMenuTargetNode) {
        deleteNode(contextMenuTargetNode);
        hideContextMenu();
    }
});

// 入力追加メニューアイテムのクリック
document.getElementById('add-input-menu').addEventListener('click', () => {
    if (contextMenuTargetNode && contextMenuTargetNode.type === 'composite') {
        addCompositeInput(contextMenuTargetNode);
        hideContextMenu();
    }
});

// 出力追加メニューアイテムのクリック
const addOutputMenuEl = document.getElementById('add-output-menu');
if (addOutputMenuEl) {
    addOutputMenuEl.addEventListener('click', () => {
        if (contextMenuTargetNode && contextMenuTargetNode.type === 'distributor') {
            addDistributorOutput(contextMenuTargetNode);
            hideContextMenu();
        }
    });
}

// 詳細メニューアイテムのクリック
document.getElementById('detail-node-menu').addEventListener('click', () => {
    if (contextMenuTargetNode) {
        showNodeDetailPanel(contextMenuTargetNode);
        hideContextMenu();
    }
});

// ========================================
// ノード詳細パネル
// ========================================

let detailPanelNode = null;
const detailPanel = document.getElementById('node-detail-panel');
const detailPanelContent = detailPanel.querySelector('.node-detail-content');
const detailPanelTitle = detailPanel.querySelector('.node-detail-title');
const detailPanelClose = detailPanel.querySelector('.node-detail-close');

// 詳細パネルを表示
function showNodeDetailPanel(node) {
    detailPanelNode = node;
    detailPanelTitle.textContent = node.title;
    detailPanelContent.innerHTML = '';

    // ノードタイプに応じたコンテンツを生成
    buildDetailPanelContent(node);

    // パネルを画面中央に表示
    detailPanel.style.left = '50%';
    detailPanel.style.top = '50%';
    detailPanel.style.transform = 'translate(-50%, -50%)';
    detailPanel.style.display = 'flex';
}

// 詳細パネルを閉じる
function hideNodeDetailPanel() {
    detailPanel.style.display = 'none';
    detailPanelNode = null;
}

// 閉じるボタン
detailPanelClose.addEventListener('click', (e) => {
    e.stopPropagation();
    hideNodeDetailPanel();
});

// パネル内クリックは外部に伝播させない（ボタンクリック時にパネルが閉じるのを防ぐ）
detailPanel.addEventListener('click', (e) => {
    e.stopPropagation();
});

// 外部クリックで閉じる
document.addEventListener('click', (e) => {
    if (detailPanel.style.display !== 'none' &&
        !detailPanel.contains(e.target) &&
        !contextMenu.contains(e.target)) {
        hideNodeDetailPanel();
    }
});

// ========================================
// 詳細パネルのドラッグ移動
// ========================================

const detailPanelHeader = detailPanel.querySelector('.node-detail-header');
let detailPanelDragging = false;
let detailPanelDragOffsetX = 0;
let detailPanelDragOffsetY = 0;

detailPanelHeader.addEventListener('mousedown', (e) => {
    // 閉じるボタン上でのドラッグ開始を防ぐ
    if (e.target === detailPanelClose) return;

    detailPanelDragging = true;

    // transform を解除して left/top で位置管理
    const rect = detailPanel.getBoundingClientRect();
    detailPanel.style.transform = 'none';
    detailPanel.style.left = rect.left + 'px';
    detailPanel.style.top = rect.top + 'px';

    detailPanelDragOffsetX = e.clientX - rect.left;
    detailPanelDragOffsetY = e.clientY - rect.top;

    e.preventDefault();
});

document.addEventListener('mousemove', (e) => {
    if (!detailPanelDragging) return;

    const newX = e.clientX - detailPanelDragOffsetX;
    const newY = e.clientY - detailPanelDragOffsetY;

    detailPanel.style.left = newX + 'px';
    detailPanel.style.top = newY + 'px';
});

document.addEventListener('mouseup', () => {
    detailPanelDragging = false;
});

// 詳細パネルのコンテンツを生成
function buildDetailPanelContent(node) {
    if (node.type === 'image') {
        buildImageDetailContent(node);
    } else if (node.type === 'filter' && node.independent) {
        buildFilterDetailContent(node);
    } else if (node.type === 'composite') {
        buildCompositeDetailContent(node);
    } else if (node.type === 'distributor') {
        buildDistributorDetailContent(node);
    } else if (node.type === 'affine') {
        buildAffineDetailContent(node);
    } else if (node.type === 'ninepatch') {
        buildNinePatchDetailContent(node);
    } else if (node.type === 'renderer') {
        buildRendererDetailContent(node);
    } else if (node.type === 'sink') {
        buildSinkDetailContent(node);
    }
}

// 画像ノードの詳細コンテンツ
function buildImageDetailContent(node) {
    const onUpdate = () => {
        renderNodeGraph();
        throttledUpdatePreview();
    };

    // 原点セクション（9点セレクタ + X,Yスライダー）
    createOriginSection({
        node,
        container: detailPanelContent,
        onChange: onUpdate
    });

    // 配置位置セクション（X,Yスライダー）
    createPositionSection({
        node,
        container: detailPanelContent,
        onChange: onUpdate
    });

    // ピクセルフォーマット選択セクション
    const formatSection = document.createElement('div');
    formatSection.className = 'node-detail-section';

    const formatLabel = document.createElement('div');
    formatLabel.className = 'node-detail-label';
    formatLabel.textContent = 'ピクセルフォーマット';
    formatSection.appendChild(formatLabel);

    const formatSelect = document.createElement('select');
    formatSelect.className = 'node-detail-select';
    formatSelect.style.cssText = 'width: 100%; padding: 4px; margin-top: 4px;';

    const currentFormat = node.pixelFormat ?? DEFAULT_PIXEL_FORMAT;
    PIXEL_FORMATS.forEach(fmt => {
        const option = document.createElement('option');
        option.value = fmt.formatName;
        option.textContent = `${fmt.displayName} (${fmt.bpp}B)`;
        option.title = fmt.description;
        if (currentFormat === fmt.formatName) option.selected = true;
        formatSelect.appendChild(option);
    });

    formatSelect.addEventListener('change', () => {
        const newFormat = formatSelect.value;
        onPixelFormatChange(node, newFormat);
    });

    formatSection.appendChild(formatSelect);
    detailPanelContent.appendChild(formatSection);

    // バイリニア補間チェックボックス
    const interpolationSection = document.createElement('div');
    interpolationSection.className = 'node-detail-section';

    const interpolationLabel = document.createElement('label');
    interpolationLabel.className = 'node-detail-checkbox-label';
    interpolationLabel.style.cssText = 'display: flex; align-items: center; gap: 8px; cursor: pointer;';

    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.checked = node.bilinear || false;
    checkbox.addEventListener('change', () => {
        node.bilinear = checkbox.checked;
        throttledUpdatePreview();
    });

    interpolationLabel.appendChild(checkbox);
    interpolationLabel.appendChild(document.createTextNode('バイリニア補間'));

    // 注釈
    const note = document.createElement('div');
    note.className = 'node-detail-note';
    note.style.cssText = 'font-size: 11px; color: #888; margin-top: 4px;';
    note.textContent = '※ RGBA8形式のみ対応。端1pxは描画されません。';

    interpolationSection.appendChild(interpolationLabel);
    interpolationSection.appendChild(note);
    detailPanelContent.appendChild(interpolationSection);
}

// ピクセルフォーマット変更時の処理
function onPixelFormatChange(node, formatId) {
    if (node.type !== 'image') return;

    node.pixelFormat = formatId;

    // 画像を再登録（バインディング層で変換）
    const content = contentLibrary.find(c => c.id === node.contentId);
    if (content && content.imageData) {
        graphEvaluator.storeImageWithFormat(
            content.cppImageId,
            content.imageData.data,
            content.imageData.width,
            content.imageData.height,
            formatId
        );
    }

    throttledUpdatePreview();
}

// フィルタノードの詳細コンテンツ
function buildFilterDetailContent(node) {
    const filterDef = FILTER_DEFINITIONS[node.filterType];
    if (!filterDef) return;

    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = 'パラメータ';
    section.appendChild(label);

    filterDef.params.forEach((paramDef, index) => {
        const currentValue = node.param ?? paramDef.default;

        const row = document.createElement('div');
        row.className = 'node-detail-row';

        const paramLabel = document.createElement('label');
        paramLabel.textContent = paramDef.label;

        const slider = document.createElement('input');
        slider.type = 'range';
        slider.min = String(paramDef.min);
        slider.max = String(paramDef.max);
        slider.step = String(paramDef.step);
        slider.value = String(currentValue);

        const display = document.createElement('span');
        display.className = 'value-display';
        display.textContent = paramDef.format ? paramDef.format(currentValue) : String(currentValue);

        slider.addEventListener('input', (e) => {
            const value = parseFloat(e.target.value);
            node.param = value;
            display.textContent = paramDef.format ? paramDef.format(value) : String(value);
            renderNodeGraph();
            throttledUpdatePreview();
        });

        row.appendChild(paramLabel);
        row.appendChild(slider);
        row.appendChild(display);
        section.appendChild(row);
    });

    detailPanelContent.appendChild(section);
}

// 合成ノードの詳細コンテンツ
function buildCompositeDetailContent(node) {
    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = `入力数: ${node.inputs ? node.inputs.length : 0}`;
    section.appendChild(label);

    // 入力追加ボタン
    const addBtn = document.createElement('button');
    addBtn.textContent = '+ 入力を追加';
    addBtn.style.cssText = 'width: 100%; margin-top: 8px; padding: 6px; font-size: 12px;';
    addBtn.addEventListener('click', () => {
        addCompositeInput(node);
        detailPanelContent.innerHTML = '';
        buildCompositeDetailContent(node);
    });
    section.appendChild(addBtn);

    // ヒントテキスト
    const hint = document.createElement('div');
    hint.style.cssText = 'margin-top: 12px; font-size: 11px; color: #888;';
    hint.textContent = '💡 アルファ調整はAlphaフィルタノードを使用してください';
    section.appendChild(hint);

    detailPanelContent.appendChild(section);
}

// 分配ノードの詳細コンテンツ
function buildDistributorDetailContent(node) {
    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = `出力数: ${node.outputs ? node.outputs.length : 0}`;
    section.appendChild(label);

    // 出力追加ボタン
    const addBtn = document.createElement('button');
    addBtn.textContent = '+ 出力を追加';
    addBtn.style.cssText = 'width: 100%; margin-top: 8px; padding: 6px; font-size: 12px;';
    addBtn.addEventListener('click', () => {
        addDistributorOutput(node);
        detailPanelContent.innerHTML = '';
        buildDistributorDetailContent(node);
    });
    section.appendChild(addBtn);

    // ヒントテキスト
    const hint = document.createElement('div');
    hint.style.cssText = 'margin-top: 12px; font-size: 11px; color: #888;';
    hint.textContent = '💡 1つの入力を複数の出力に分配します';
    section.appendChild(hint);

    detailPanelContent.appendChild(section);
}

// アフィンノードの詳細コンテンツ
function buildAffineDetailContent(node) {
    // モード切替
    const modeSection = document.createElement('div');
    modeSection.className = 'node-detail-section';

    const modeLabel = document.createElement('div');
    modeLabel.className = 'node-detail-label';
    modeLabel.textContent = 'モード';
    modeSection.appendChild(modeLabel);

    const modeRow = document.createElement('div');
    modeRow.style.cssText = 'display: flex; gap: 4px;';

    const paramBtn = document.createElement('button');
    paramBtn.textContent = 'パラメータ';
    paramBtn.style.cssText = `flex: 1; padding: 6px; font-size: 11px; ${!node.matrixMode ? 'background: #4CAF50; color: white;' : ''}`;
    paramBtn.addEventListener('click', () => {
        node.matrixMode = false;
        detailPanelContent.innerHTML = '';
        buildAffineDetailContent(node);
        renderNodeGraph();
        throttledUpdatePreview();
    });

    const matrixBtn = document.createElement('button');
    matrixBtn.textContent = '行列';
    matrixBtn.style.cssText = `flex: 1; padding: 6px; font-size: 11px; ${node.matrixMode ? 'background: #4CAF50; color: white;' : ''}`;
    matrixBtn.addEventListener('click', () => {
        node.matrixMode = true;
        detailPanelContent.innerHTML = '';
        buildAffineDetailContent(node);
        renderNodeGraph();
        throttledUpdatePreview();
    });

    modeRow.appendChild(paramBtn);
    modeRow.appendChild(matrixBtn);
    modeSection.appendChild(modeRow);
    detailPanelContent.appendChild(modeSection);

    // パラメータセクション
    const section = document.createElement('div');
    section.className = 'node-detail-section';

    if (!node.matrixMode) {
        // パラメータモード
        const params = [
            { key: 'translateX', label: 'X移動', min: -500, max: 500, step: 0.1, default: 0, format: v => v.toFixed(1) },
            { key: 'translateY', label: 'Y移動', min: -500, max: 500, step: 0.1, default: 0, format: v => v.toFixed(1) },
            { key: 'rotation', label: '回転', min: -180, max: 180, step: 0.1, default: 0, format: v => `${v.toFixed(1)}°` },
            { key: 'scaleX', label: 'X倍率', min: 0.1, max: 3, step: 0.01, default: 1, format: v => v.toFixed(2) },
            { key: 'scaleY', label: 'Y倍率', min: 0.1, max: 3, step: 0.01, default: 1, format: v => v.toFixed(2) }
        ];

        params.forEach(p => {
            const value = node[p.key] !== undefined ? node[p.key] : p.default;
            const row = document.createElement('div');
            row.className = 'node-detail-row';

            const paramLabel = document.createElement('label');
            paramLabel.textContent = p.label;

            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = String(p.min);
            slider.max = String(p.max);
            slider.step = String(p.step);
            slider.value = String(value);

            const display = document.createElement('span');
            display.className = 'value-display';
            display.textContent = p.format(value);

            slider.addEventListener('input', (e) => {
                node[p.key] = parseFloat(e.target.value);
                display.textContent = p.format(node[p.key]);
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0, node.translateY || 0,
                    node.rotation || 0, node.scaleX || 1, node.scaleY || 1
                );
                renderNodeGraph();
                throttledUpdatePreview();
            });

            row.appendChild(paramLabel);
            row.appendChild(slider);
            row.appendChild(display);
            section.appendChild(row);
        });
    } else {
        // 行列モード
        const matrixParams = [
            { name: 'a', min: -3, max: 3, step: 0.01, default: 1, decimals: 2 },
            { name: 'b', min: -3, max: 3, step: 0.01, default: 0, decimals: 2 },
            { name: 'c', min: -3, max: 3, step: 0.01, default: 0, decimals: 2 },
            { name: 'd', min: -3, max: 3, step: 0.01, default: 1, decimals: 2 },
            { name: 'tx', min: -500, max: 500, step: 0.1, default: 0, decimals: 1 },
            { name: 'ty', min: -500, max: 500, step: 0.1, default: 0, decimals: 1 }
        ];

        matrixParams.forEach(p => {
            const value = node.matrix && node.matrix[p.name] !== undefined ? node.matrix[p.name] : p.default;
            const row = document.createElement('div');
            row.className = 'node-detail-row';

            const paramLabel = document.createElement('label');
            paramLabel.textContent = p.name;

            const slider = document.createElement('input');
            slider.type = 'range';
            slider.min = String(p.min);
            slider.max = String(p.max);
            slider.step = String(p.step);
            slider.value = String(value);

            const display = document.createElement('span');
            display.className = 'value-display';
            display.textContent = value.toFixed(p.decimals);

            slider.addEventListener('input', (e) => {
                if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
                node.matrix[p.name] = parseFloat(e.target.value);
                display.textContent = node.matrix[p.name].toFixed(p.decimals);
                renderNodeGraph();
                throttledUpdatePreview();
            });

            row.appendChild(paramLabel);
            row.appendChild(slider);
            row.appendChild(display);
            section.appendChild(row);
        });
    }

    detailPanelContent.appendChild(section);
}

// Rendererノードの詳細コンテンツ
function buildRendererDetailContent(node) {
    // === 仮想スクリーンサイズ ===
    const sizeSection = document.createElement('div');
    sizeSection.className = 'node-detail-section';

    const sizeLabel = document.createElement('div');
    sizeLabel.className = 'node-detail-label';
    sizeLabel.textContent = '仮想スクリーン';
    sizeSection.appendChild(sizeLabel);

    // 幅
    const widthRow = document.createElement('div');
    widthRow.className = 'node-detail-row';
    const widthLabel = document.createElement('label');
    widthLabel.textContent = '幅';
    const widthInput = document.createElement('input');
    widthInput.type = 'number';
    widthInput.min = '100';
    widthInput.max = '4096';
    widthInput.value = node.virtualWidth ?? canvasWidth;
    widthInput.style.width = '80px';
    widthRow.appendChild(widthLabel);
    widthRow.appendChild(widthInput);
    sizeSection.appendChild(widthRow);

    // 高さ
    const heightRow = document.createElement('div');
    heightRow.className = 'node-detail-row';
    const heightLabel = document.createElement('label');
    heightLabel.textContent = '高さ';
    const heightInput = document.createElement('input');
    heightInput.type = 'number';
    heightInput.min = '100';
    heightInput.max = '4096';
    heightInput.value = node.virtualHeight ?? canvasHeight;
    heightInput.style.width = '80px';
    heightRow.appendChild(heightLabel);
    heightRow.appendChild(heightInput);
    sizeSection.appendChild(heightRow);

    // 原点X
    const originXRow = document.createElement('div');
    originXRow.className = 'node-detail-row';
    const originXLabel = document.createElement('label');
    originXLabel.textContent = '原点X';
    const originXInput = document.createElement('input');
    originXInput.type = 'number';
    originXInput.value = Math.round(node.originX ?? canvasOrigin.x);
    originXInput.style.width = '80px';
    originXRow.appendChild(originXLabel);
    originXRow.appendChild(originXInput);
    sizeSection.appendChild(originXRow);

    // 原点Y
    const originYRow = document.createElement('div');
    originYRow.className = 'node-detail-row';
    const originYLabel = document.createElement('label');
    originYLabel.textContent = '原点Y';
    const originYInput = document.createElement('input');
    originYInput.type = 'number';
    originYInput.value = Math.round(node.originY ?? canvasOrigin.y);
    originYInput.style.width = '80px';
    originYRow.appendChild(originYLabel);
    originYRow.appendChild(originYInput);
    sizeSection.appendChild(originYRow);

    // 適用ボタン
    const applyRow = document.createElement('div');
    applyRow.className = 'node-detail-row';
    applyRow.style.justifyContent = 'flex-end';
    const applyBtn = document.createElement('button');
    applyBtn.className = 'primary-btn';
    applyBtn.textContent = '適用';
    applyBtn.style.marginTop = '8px';
    applyBtn.addEventListener('click', () => {
        node.virtualWidth = parseInt(widthInput.value);
        node.virtualHeight = parseInt(heightInput.value);
        node.originX = parseFloat(originXInput.value);
        node.originY = parseFloat(originYInput.value);

        // グローバル変数も更新
        canvasWidth = node.virtualWidth;
        canvasHeight = node.virtualHeight;
        canvasOrigin.x = node.originX;
        canvasOrigin.y = node.originY;

        // SinkノードのサイズはcontentLibraryから取得するため、ここでは同期しない
        // 各Sinkは独自のcontentIdで出力バッファを参照する

        // キャンバスをリサイズ＆原点を更新
        resizeCanvas(node.virtualWidth, node.virtualHeight);
        if (graphEvaluator) {
            graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);
        }
        renderNodeGraph();
        throttledUpdatePreview();
    });
    applyRow.appendChild(applyBtn);
    sizeSection.appendChild(applyRow);

    detailPanelContent.appendChild(sizeSection);

    // === デバッグ設定 ===
    const tileSection = document.createElement('div');
    tileSection.className = 'node-detail-section';

    const tileLabel = document.createElement('div');
    tileLabel.className = 'node-detail-label';
    tileLabel.textContent = 'デバッグ';
    tileSection.appendChild(tileLabel);

    // 交互スキップ
    const debugRow = document.createElement('div');
    debugRow.className = 'node-detail-row';
    const debugLabel = document.createElement('label');
    debugLabel.className = 'sidebar-checkbox-label';
    debugLabel.style.fontSize = '11px';
    const debugCheckbox = document.createElement('input');
    debugCheckbox.type = 'checkbox';
    debugCheckbox.checked = node.debugCheckerboard ?? debugCheckerboard;
    debugCheckbox.addEventListener('change', () => {
        node.debugCheckerboard = debugCheckbox.checked;
        debugCheckerboard = node.debugCheckerboard;
        applyTileSettings();
        throttledUpdatePreview();
    });
    debugLabel.appendChild(debugCheckbox);
    debugLabel.appendChild(document.createTextNode(' 🐛 交互スキップ'));
    debugRow.appendChild(debugLabel);
    tileSection.appendChild(debugRow);

    detailPanelContent.appendChild(tileSection);
}

// Sinkノードの詳細コンテンツ
function buildSinkDetailContent(node) {
    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = '出力設定';
    section.appendChild(label);

    // 出力バッファ情報（contentLibraryから取得、読み取り専用）
    const content = contentLibrary.find(c => c.id === node.contentId);
    const outputWidth = content?.width ?? 0;
    const outputHeight = content?.height ?? 0;

    // サイズ表示（読み取り専用）
    const sizeRow = document.createElement('div');
    sizeRow.className = 'node-detail-row';
    const sizeLabel = document.createElement('label');
    sizeLabel.textContent = 'サイズ';
    const sizeValue = document.createElement('span');
    sizeValue.textContent = `${outputWidth} x ${outputHeight}`;
    sizeValue.style.color = '#888';
    sizeRow.appendChild(sizeLabel);
    sizeRow.appendChild(sizeValue);
    section.appendChild(sizeRow);

    // 原点X
    const originXRow = document.createElement('div');
    originXRow.className = 'node-detail-row';
    const originXLabel = document.createElement('label');
    originXLabel.textContent = '原点X';
    const originXInput = document.createElement('input');
    originXInput.type = 'number';
    originXInput.value = Math.round(node.originX ?? 0);
    originXInput.style.width = '80px';
    originXRow.appendChild(originXLabel);
    originXRow.appendChild(originXInput);
    section.appendChild(originXRow);

    // 原点Y
    const originYRow = document.createElement('div');
    originYRow.className = 'node-detail-row';
    const originYLabel = document.createElement('label');
    originYLabel.textContent = '原点Y';
    const originYInput = document.createElement('input');
    originYInput.type = 'number';
    originYInput.value = Math.round(node.originY ?? 0);
    originYInput.style.width = '80px';
    originYRow.appendChild(originYLabel);
    originYRow.appendChild(originYInput);
    section.appendChild(originYRow);

    // ピクセルフォーマット選択
    const formatRow = document.createElement('div');
    formatRow.className = 'node-detail-row';
    const formatLabel = document.createElement('label');
    formatLabel.textContent = 'フォーマット';
    const formatSelect = document.createElement('select');
    formatSelect.style.width = '120px';

    const currentFormat = node.outputFormat ?? DEFAULT_PIXEL_FORMAT;
    PIXEL_FORMATS.forEach(fmt => {
        const option = document.createElement('option');
        option.value = fmt.formatName;
        option.textContent = `${fmt.displayName} (${fmt.bpp}B)`;
        option.title = fmt.description;
        if (currentFormat === fmt.formatName) option.selected = true;
        formatSelect.appendChild(option);
    });

    formatRow.appendChild(formatLabel);
    formatRow.appendChild(formatSelect);
    section.appendChild(formatRow);

    // 適用ボタン
    const applyRow = document.createElement('div');
    applyRow.className = 'node-detail-row';
    applyRow.style.justifyContent = 'flex-end';
    const applyBtn = document.createElement('button');
    applyBtn.className = 'primary-btn';
    applyBtn.textContent = '適用';
    applyBtn.style.marginTop = '8px';
    applyBtn.addEventListener('click', () => {
        node.originX = parseFloat(originXInput.value);
        node.originY = parseFloat(originYInput.value);
        node.outputFormat = formatSelect.value;

        // Sink出力フォーマットをC++側に設定
        if (graphEvaluator) {
            graphEvaluator.setSinkFormat(node.id, node.outputFormat);
        }

        renderNodeGraph();
        throttledUpdatePreview();
        scheduleAutoSave();
    });
    applyRow.appendChild(applyBtn);
    section.appendChild(applyRow);

    detailPanelContent.appendChild(section);
}

// 9patchノードの詳細コンテンツ
function buildNinePatchDetailContent(node) {
    const content = contentLibrary.find(c => c.id === node.contentId);

    const onUpdate = () => {
        renderNodeGraph();
        throttledUpdatePreview();
    };

    // 原点セクション（9点セレクタ + X,Yスライダー）
    createOriginSection({
        node,
        container: detailPanelContent,
        onChange: onUpdate
    });

    // 配置位置セクション（X,Yスライダー）
    createPositionSection({
        node,
        container: detailPanelContent,
        onChange: onUpdate
    });

    // 出力サイズセクション
    const sizeSection = document.createElement('div');
    sizeSection.className = 'node-detail-section';

    const sizeLabel = document.createElement('div');
    sizeLabel.className = 'node-detail-label';
    sizeLabel.textContent = '出力サイズ';
    sizeSection.appendChild(sizeLabel);

    // 元画像サイズ（参考情報）
    if (content) {
        const srcSizeRow = document.createElement('div');
        srcSizeRow.className = 'node-detail-row';
        srcSizeRow.style.color = '#888';
        srcSizeRow.style.fontSize = '11px';
        srcSizeRow.textContent = `元画像: ${content.width - 2} x ${content.height - 2}`;
        sizeSection.appendChild(srcSizeRow);
    }

    // 幅スライダー
    const defaultWidth = node.outputWidth ?? (content ? content.width - 2 : 48);
    const widthResult = createDetailSliderRow({
        label: 'W',
        min: 1,
        max: 1000,
        step: 0.1,
        value: defaultWidth,
        onChange: (val) => {
            node.outputWidth = val;
            onUpdate();
        }
    });
    sizeSection.appendChild(widthResult.row);

    // 高さスライダー
    const defaultHeight = node.outputHeight ?? (content ? content.height - 2 : 48);
    const heightResult = createDetailSliderRow({
        label: 'H',
        min: 1,
        max: 1000,
        step: 0.1,
        value: defaultHeight,
        onChange: (val) => {
            node.outputHeight = val;
            onUpdate();
        }
    });
    sizeSection.appendChild(heightResult.row);

    detailPanelContent.appendChild(sizeSection);

    // バイリニア補間チェックボックス
    const interpolationSection = document.createElement('div');
    interpolationSection.className = 'node-detail-section';

    const interpolationLabel = document.createElement('label');
    interpolationLabel.className = 'node-detail-checkbox-label';
    interpolationLabel.style.cssText = 'display: flex; align-items: center; gap: 8px; cursor: pointer;';

    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.checked = node.bilinear || false;
    checkbox.addEventListener('change', () => {
        node.bilinear = checkbox.checked;
        throttledUpdatePreview();
    });

    interpolationLabel.appendChild(checkbox);
    interpolationLabel.appendChild(document.createTextNode('バイリニア補間'));

    // 注釈
    const note = document.createElement('div');
    note.className = 'node-detail-note';
    note.style.cssText = 'font-size: 11px; color: #888; margin-top: 4px;';
    note.textContent = '※ RGBA8形式のみ対応。端1pxは描画されません。';

    interpolationSection.appendChild(interpolationLabel);
    interpolationSection.appendChild(note);
    detailPanelContent.appendChild(interpolationSection);
}

// ノードを削除
function deleteNode(node) {
    // Rendererは削除不可
    if (node.type === 'renderer') {
        alert('Rendererノードは削除できません');
        return;
    }

    // Sinkノード: 確認ダイアログ + 出力バッファ連動削除
    if (node.type === 'sink') {
        const content = contentLibrary.find(c => c.id === node.contentId);
        if (content) {
            const confirmed = confirm(
                `「${content.name}」はコンテンツライブラリからも削除されます。\n削除してよろしいですか？`
            );
            if (!confirmed) return;

            // コンテンツライブラリから削除
            contentLibrary = contentLibrary.filter(c => c.id !== content.id);
            updateFocusAfterDelete(content.id);
        }
    } else {
        // その他のノード: 通常の確認ダイアログ
        if (!confirm(`ノード「${node.title}」を削除しますか？`)) {
            return;
        }
    }

    // ノードを削除
    const index = globalNodes.findIndex(n => n.id === node.id);
    if (index >= 0) {
        globalNodes.splice(index, 1);
    }

    // 関連する接続を削除
    globalConnections = globalConnections.filter(conn =>
        conn.fromNodeId !== node.id && conn.toNodeId !== node.id
    );

    // グラフを再描画
    renderNodeGraph();
    renderContentLibrary();
    throttledUpdatePreview();
    scheduleAutoSave();
}

// 原点選択グリッドのセットアップ
// gridId: グリッド要素のID
// initialOrigin: 初期値 {x, y}（0.0〜1.0）
// onChange: 変更時のコールバック (origin) => void
function setupOriginGrid(gridId, initialOrigin, onChange) {
    const grid = document.getElementById(gridId);
    if (!grid) return;

    const points = grid.querySelectorAll('.origin-point');

    // 初期選択状態を設定
    points.forEach(point => {
        const x = parseFloat(point.dataset.x);
        const y = parseFloat(point.dataset.y);
        if (x === initialOrigin.x && y === initialOrigin.y) {
            point.classList.add('selected');
        } else {
            point.classList.remove('selected');
        }
    });

    // クリックイベント
    points.forEach(point => {
        point.addEventListener('click', (e) => {
            e.preventDefault();
            e.stopPropagation();

            // 選択状態を更新
            points.forEach(p => p.classList.remove('selected'));
            point.classList.add('selected');

            // 原点値を取得してコールバック
            const origin = {
                x: parseFloat(point.dataset.x),
                y: parseFloat(point.dataset.y)
            };
            onChange(origin);
        });
    });
}

// ========================================
// 状態管理（LocalStorage / URLパラメータ）
// ========================================

const STATE_STORAGE_KEY = 'imageTransformPreviewState';
const STATE_VERSION = 2;  // コンテンツライブラリ対応

// アプリ状態をオブジェクトとして取得
function getAppState() {
    // Rendererノードからタイル設定を取得
    const rendererNode = globalNodes.find(n => n.type === 'renderer');
    const currentTileWidth = rendererNode?.tileWidth ?? tileWidth;
    const currentTileHeight = rendererNode?.tileHeight ?? tileHeight;
    const currentDebugCheckerboard = rendererNode?.debugCheckerboard ?? debugCheckerboard;

    // スクロール位置を保存（倍率変更後も比率で復元できるように）
    const scrollRatio = previewScrollManager ? previewScrollManager.getRatio() : { x: 0.5, y: 0.5 };

    return {
        version: STATE_VERSION,
        timestamp: Date.now(),
        canvas: {
            width: canvasWidth,
            height: canvasHeight,
            origin: canvasOrigin,
            scale: previewScale,
            scrollRatio: scrollRatio
        },
        tile: {
            width: currentTileWidth,
            height: currentTileHeight,
            debugCheckerboard: currentDebugCheckerboard
        },
        // コンテンツライブラリ（画像・出力バッファ統合）
        contentLibrary: contentLibrary.map(content => ({
            id: content.id,
            type: content.type,
            name: content.name,
            width: content.width,
            height: content.height,
            cppImageId: content.cppImageId,
            isNinePatch: content.isNinePatch || false,  // 9patchフラグを保存
            // 画像コンテンツのみimageDataを保存（出力バッファは再生成）
            dataURL: content.type === 'image' && content.imageData
                ? imageDataToDataURL(content.imageData)
                : null
        })),
        focusedContentId: focusedContentId,
        nodes: globalNodes.map(node => ({...node})),
        connections: globalConnections.map(conn => ({...conn})),
        nextIds: {
            contentId: nextContentId,
            cppImageId: nextCppImageId,
            globalNodeId: nextGlobalNodeId,
            compositeId: nextCompositeId,
            distributorId: nextDistributorId,
            independentFilterId: nextIndependentFilterId,
            imageNodeId: nextImageNodeId,
            ninePatchNodeId: nextNinePatchNodeId
        }
    };
}

// ImageDataまたは{data, width, height}形式のオブジェクトをDataURLに変換
function imageDataToDataURL(imageData) {
    if (!imageData) return null;
    const tempCanvas = document.createElement('canvas');
    tempCanvas.width = imageData.width;
    tempCanvas.height = imageData.height;
    const tempCtx = tempCanvas.getContext('2d');

    // ImageDataオブジェクトかどうかをチェック
    if (imageData instanceof ImageData) {
        tempCtx.putImageData(imageData, 0, 0);
    } else if (imageData.data) {
        // {data, width, height}形式のオブジェクト
        const imgData = new ImageData(
            new Uint8ClampedArray(imageData.data),
            imageData.width,
            imageData.height
        );
        tempCtx.putImageData(imgData, 0, 0);
    } else {
        return null;
    }
    return tempCanvas.toDataURL('image/png');
}

// DataURLをImageDataに変換（非同期）
function dataURLToImageData(dataURL, width, height) {
    return new Promise((resolve, reject) => {
        if (!dataURL) {
            reject(new Error('No dataURL provided'));
            return;
        }
        const img = new Image();
        img.onload = () => {
            const tempCanvas = document.createElement('canvas');
            tempCanvas.width = width;
            tempCanvas.height = height;
            const tempCtx = tempCanvas.getContext('2d');
            tempCtx.drawImage(img, 0, 0);
            resolve(tempCtx.getImageData(0, 0, width, height));
        };
        img.onerror = reject;
        img.src = dataURL;
    });
}

// LocalStorageに状態を保存
function saveStateToLocalStorage() {
    try {
        const state = getAppState();
        localStorage.setItem(STATE_STORAGE_KEY, JSON.stringify(state));
        console.log('State saved to LocalStorage');
    } catch (e) {
        console.warn('Failed to save state to LocalStorage:', e);
    }
}

// LocalStorageから状態を読み込み
function loadStateFromLocalStorage() {
    try {
        const stateJson = localStorage.getItem(STATE_STORAGE_KEY);
        if (!stateJson) return null;
        const state = JSON.parse(stateJson);
        if (state.version !== STATE_VERSION) {
            console.warn('State version mismatch, ignoring saved state');
            return null;
        }
        return state;
    } catch (e) {
        console.warn('Failed to load state from LocalStorage:', e);
        return null;
    }
}

// 状態をURLパラメータにエンコード（圧縮）
function encodeStateToURL() {
    const state = getAppState();
    // 画像データを除外した軽量版（URLが長くなりすぎるため）
    const lightState = {
        ...state,
        contentLibrary: state.contentLibrary.map(content => ({
            id: content.id,
            type: content.type,
            name: content.name,
            width: content.width,
            height: content.height,
            cppImageId: content.cppImageId
            // dataURLは除外
        }))
    };
    const json = JSON.stringify(lightState);
    const encoded = btoa(encodeURIComponent(json));
    return encoded;
}

// URLパラメータから状態をデコード
function decodeStateFromURL(encoded) {
    try {
        const json = decodeURIComponent(atob(encoded));
        return JSON.parse(json);
    } catch (e) {
        console.warn('Failed to decode state from URL:', e);
        return null;
    }
}

// URLに状態を設定
function setStateToURL() {
    const encoded = encodeStateToURL();
    const url = new URL(window.location.href);
    url.searchParams.set('state', encoded);
    window.history.replaceState({}, '', url.toString());
    console.log('State encoded to URL');
}

// URLから状態を取得
function getStateFromURL() {
    const url = new URL(window.location.href);
    const encoded = url.searchParams.get('state');
    if (!encoded) return null;
    return decodeStateFromURL(encoded);
}

// 状態を復元（非同期）
async function restoreAppState(state) {
    if (!state) return false;

    console.log('Restoring app state...');

    // キャンバス設定を復元
    canvasWidth = state.canvas.width;
    canvasHeight = state.canvas.height;
    canvasOrigin = state.canvas.origin;
    previewScale = state.canvas.scale;

    // キャンバスサイズを更新
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    updateCanvasDisplayScale();
    graphEvaluator.setCanvasSize(canvasWidth, canvasHeight);
    graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);

    // 表示倍率UIを更新
    const scaleSlider = document.getElementById('sidebar-preview-scale');
    const scaleValue = document.getElementById('sidebar-preview-scale-value');
    if (scaleSlider) {
        scaleSlider.value = previewScale;
    }
    if (scaleValue) {
        scaleValue.textContent = previewScale + 'x';
    }

    // スクロール位置を復元（表示サイズ更新後に比率で復元）
    if (state.canvas.scrollRatio && previewScrollManager) {
        previewScrollManager.setRatio(state.canvas.scrollRatio.x, state.canvas.scrollRatio.y);
    }

    // スキャンライン処理設定を復元（グローバル変数に直接設定）
    // ※ tileHeight は内部的に常に1として扱われる（互換性のため変数は維持）
    if (state.tile) {
        tileWidth = state.tile.width || 0;
        tileHeight = state.tile.height || 0;
        debugCheckerboard = state.tile.debugCheckerboard || false;
    }
    // C++側に設定を反映
    applyTileSettings();

    // 次のID値を復元
    nextContentId = state.nextIds.contentId || 1;
    nextCppImageId = state.nextIds.cppImageId || 1;
    nextGlobalNodeId = state.nextIds.globalNodeId;
    nextCompositeId = state.nextIds.compositeId;
    nextDistributorId = state.nextIds.distributorId || 1;
    nextIndependentFilterId = state.nextIds.independentFilterId;
    nextImageNodeId = state.nextIds.imageNodeId;
    nextNinePatchNodeId = state.nextIds.ninePatchNodeId || 1;

    // コンテンツライブラリを復元
    // URLパラメータからの復元時は画像データがないため、LocalStorageから補完を試みる
    const localState = loadStateFromLocalStorage();
    const localContents = localState ? localState.contentLibrary : [];

    contentLibrary = [];
    let missingImages = [];
    for (const contentState of state.contentLibrary) {
        const content = {
            id: contentState.id,
            type: contentState.type,
            name: contentState.name,
            width: contentState.width,
            height: contentState.height,
            cppImageId: contentState.cppImageId,
            isNinePatch: contentState.isNinePatch || false,  // 9patchフラグを復元
            imageData: null
        };

        // 画像コンテンツのみ画像データを復元
        if (contentState.type === 'image') {
            let dataURL = contentState.dataURL;

            // 画像データがない場合、LocalStorageから同じIDのコンテンツを探す
            if (!dataURL) {
                const localContent = localContents.find(lc => lc.id === contentState.id);
                if (localContent && localContent.dataURL) {
                    dataURL = localContent.dataURL;
                    console.log(`Image content ${contentState.id} (${contentState.name}) loaded from LocalStorage`);
                }
            }

            if (dataURL) {
                content.imageData = await dataURLToImageData(dataURL, contentState.width, contentState.height);
                // C++側に画像を登録（cppImageIdを使用）
                graphEvaluator.storeImage(
                    content.cppImageId,
                    content.imageData.data,
                    content.width,
                    content.height
                );
            } else {
                // 画像データが見つからない場合は警告
                missingImages.push(contentState.name);
                console.warn(`Image content ${contentState.id} (${contentState.name}) not found in LocalStorage`);
            }
        }
        // 出力バッファはimageDataはnull（レンダリング時に生成）

        contentLibrary.push(content);
    }

    if (missingImages.length > 0) {
        console.warn(`Missing images: ${missingImages.join(', ')}`);
    }

    // フォーカス状態を復元
    focusedContentId = state.focusedContentId;
    renderContentLibrary();

    // ノードとコネクションを復元
    globalNodes = state.nodes;
    globalConnections = state.connections;

    // Rendererノードの設定をC++側に同期
    const rendererNode = globalNodes.find(n => n.type === 'renderer');
    if (rendererNode) {
        // Rendererノードの値をグローバル変数と同期
        if (rendererNode.virtualWidth !== undefined) {
            canvasWidth = rendererNode.virtualWidth;
            canvasHeight = rendererNode.virtualHeight;
            canvas.width = canvasWidth;
            canvas.height = canvasHeight;
            graphEvaluator.setCanvasSize(canvasWidth, canvasHeight);
        }
        if (rendererNode.originX !== undefined) {
            canvasOrigin.x = rendererNode.originX;
            canvasOrigin.y = rendererNode.originY;
            graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);
        }
        // タイル設定も同期
        if (rendererNode.tileWidth !== undefined) {
            tileWidth = rendererNode.tileWidth;
            tileHeight = rendererNode.tileHeight;
            graphEvaluator.setTileSize(tileWidth, tileHeight);
        }
        if (rendererNode.debugCheckerboard !== undefined) {
            debugCheckerboard = rendererNode.debugCheckerboard;
            graphEvaluator.setDebugCheckerboard(debugCheckerboard);
        }
        updateCanvasDisplayScale();
    }

    // 画像ノードのピクセルフォーマットを適用
    // (画像はデフォルトのRGBA8で登録済みなので、異なるフォーマットの場合は再登録)
    globalNodes.forEach(node => {
        if (node.type === 'image' && node.pixelFormat && node.pixelFormat !== DEFAULT_PIXEL_FORMAT) {
            const content = contentLibrary.find(c => c.id === node.contentId);
            if (content && content.imageData) {
                graphEvaluator.storeImageWithFormat(
                    content.cppImageId,
                    content.imageData.data,
                    content.imageData.width,
                    content.imageData.height,
                    node.pixelFormat
                );
            }
        }
        // Sinkノードの出力フォーマットを適用
        if (node.type === 'sink' && node.outputFormat && node.outputFormat !== DEFAULT_PIXEL_FORMAT) {
            graphEvaluator.setSinkFormat(node.id, node.outputFormat);
        }
    });

    renderNodeGraph();

    // プレビュー更新
    updatePreviewFromGraph();

    console.log('App state restored successfully');
    return true;
}

// 自動保存の設定（状態変更時に保存）
let autoSaveTimeout = null;
function scheduleAutoSave() {
    if (autoSaveTimeout) {
        clearTimeout(autoSaveTimeout);
    }
    // 500ms後に保存（頻繁な更新を防ぐ）
    autoSaveTimeout = setTimeout(() => {
        saveStateToLocalStorage();
    }, 500);
}

// LocalStorageをクリア
function clearSavedState() {
    localStorage.removeItem(STATE_STORAGE_KEY);
    console.log('Saved state cleared');
}

// URLから状態パラメータを除去
function clearStateFromURL() {
    const url = new URL(window.location.href);
    url.searchParams.delete('state');
    window.history.replaceState({}, '', url.toString());
}
