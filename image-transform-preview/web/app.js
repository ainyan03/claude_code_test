// グローバル変数
let graphEvaluator;  // ノードグラフ評価エンジン（C++側）
let canvas;
let ctx;
let uploadedImages = [];  // 画像ライブラリ
let nextImageId = 1;
let canvasWidth = 800;
let canvasHeight = 600;
let canvasOrigin = { x: 400, y: 300 };  // キャンバス原点（ピクセル座標）
let previewScale = 1;  // 表示倍率（1〜5）

// グローバルノードグラフ
let globalNodes = [];  // すべてのノード（画像、フィルタ、合成、出力）を管理
let globalConnections = [];  // ノード間の接続
let nextGlobalNodeId = 1;
let nextCompositeId = 1;
let nextIndependentFilterId = 1;
let nextImageNodeId = 1;
let nodeGraphSvg = null;

// ドラッグ接続用の状態
let isDraggingConnection = false;
let dragConnectionFrom = null;
let dragConnectionPath = null;

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
    }

    function closeSidebar() {
        sidebar.classList.remove('open');
        toggle.classList.remove('open');
        overlay.classList.remove('visible');
        document.body.classList.remove('sidebar-open');
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

    // 初期状態でサイドバーを開く
    openSidebar();
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
        console.log('NodeGraphEvaluator initialized');
    } else {
        console.error('WebAssembly module not loaded!', typeof WasmModule);
        alert('エラー: WebAssemblyモジュールの読み込みに失敗しました。ページを再読み込みしてください。');
        return;
    }

    // イベントリスナー設定
    setupEventListeners();

    // グローバルノードグラフ初期化
    initializeNodeGraph();

    // 初期プレビュー
    updatePreviewFromGraph();

    console.log('App initialized successfully');
}

function displayVersionInfo() {
    const versionEl = document.getElementById('version-info');
    if (versionEl && typeof BUILD_INFO !== 'undefined') {
        versionEl.textContent = `Build: ${BUILD_INFO.buildDate} | Commit: ${BUILD_INFO.gitCommit} | ${BUILD_INFO.backend}`;
        console.log('Build Info:', BUILD_INFO);
    }
}

function setupEventListeners() {
    // サイドバー開閉
    setupSidebar();

    // スプリッターによるリサイズ
    setupSplitter();

    // 画像追加ボタン（サイドバー内）
    document.getElementById('sidebar-add-image-btn').addEventListener('click', () => {
        document.getElementById('image-input').click();
    });

    // 画像選択
    document.getElementById('image-input').addEventListener('change', handleImageUpload);

    // 出力設定適用ボタン（サイドバー内）
    document.getElementById('sidebar-apply-settings').addEventListener('click', applyOutputSettings);

    // キャンバス原点選択（9点グリッド、サイドバー内）
    // 初期選択は中央（0.5, 0.5）
    setupOriginGrid('sidebar-origin-grid', { x: 0.5, y: 0.5 }, (normalizedOrigin) => {
        // 9点ボタン押下時：正規化座標からピクセル座標を計算して入力欄に反映
        const w = parseInt(document.getElementById('sidebar-canvas-width').value) || 800;
        const h = parseInt(document.getElementById('sidebar-canvas-height').value) || 600;
        const pixelX = Math.round(normalizedOrigin.x * w);
        const pixelY = Math.round(normalizedOrigin.y * h);
        document.getElementById('sidebar-origin-x').value = pixelX;
        document.getElementById('sidebar-origin-y').value = pixelY;
        // サイズと原点を同時に適用（未適用のサイズ変更も反映）
        applyOutputSettings();
    });

    // 原点座標入力欄の初期値を設定
    document.getElementById('sidebar-origin-x').value = canvasOrigin.x;
    document.getElementById('sidebar-origin-y').value = canvasOrigin.y;

    // 表示倍率スライダー
    const scaleSlider = document.getElementById('sidebar-preview-scale');
    const scaleValue = document.getElementById('sidebar-preview-scale-value');
    if (scaleSlider && scaleValue) {
        scaleSlider.addEventListener('input', (e) => {
            previewScale = parseFloat(e.target.value);
            scaleValue.textContent = previewScale + 'x';
            updateCanvasDisplayScale();
            centerPreviewScroll();
        });
    }

    // ダウンロードボタン
    document.getElementById('download-btn').addEventListener('click', downloadComposedImage);

    // ノード追加プルダウンメニュー
    document.getElementById('add-node-select').addEventListener('change', (e) => {
        const value = e.target.value;
        if (!value) return;

        // ノードタイプに応じて追加
        if (value === 'affine') {
            addAffineNode();
        } else if (value === 'composite') {
            addCompositeNode();
        } else if (value === 'grayscale' || value === 'brightness' || value === 'blur' || value === 'alpha') {
            addIndependentFilterNode(value);
        }

        // セレクトをリセット
        e.target.value = '';
    });
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
    const imageId = nextImageId++;

    const image = {
        id: imageId,
        name: imageData.name || `Image ${imageId}`,
        imageData: imageData,
        width: imageData.width,
        height: imageData.height
    };

    uploadedImages.push(image);

    // C++側の画像ライブラリに登録
    graphEvaluator.registerImage(imageId, imageData.data, imageData.width, imageData.height);

    // UIを更新
    renderImageLibrary();
}

// 画像ライブラリUIを描画
function renderImageLibrary() {
    const libraryContainer = document.getElementById('sidebar-images-library');
    libraryContainer.innerHTML = '';

    const template = document.getElementById('image-item-template');
    uploadedImages.forEach(image => {
        const item = template.content.cloneNode(true);

        // サムネイル設定
        const thumbnail = item.querySelector('.image-thumbnail img');
        thumbnail.src = createThumbnailDataURL(image.imageData);

        // 画像名設定
        item.querySelector('.image-name').textContent = image.name;

        // ノード追加ボタン
        item.querySelector('.add-image-node-btn').addEventListener('click', () => {
            addImageNodeFromLibrary(image.id);
        });

        // 削除ボタン
        item.querySelector('.delete-image-btn').addEventListener('click', () => {
            deleteImageFromLibrary(image.id);
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
function addImageNodeFromLibrary(imageId) {
    const image = uploadedImages.find(img => img.id === imageId);
    if (!image) return;

    // 全ての画像ノードの数をカウント（より良い配置のため）
    const existingImageNodes = globalNodes.filter(n => n.type === 'image').length;

    // 画像ノードは縦方向に並べる（中央エリア基準）
    const spacing = 120; // ノード間の間隔
    const startX = 500;  // 1600幅キャンバスの中央寄り左側
    const startY = 450;  // 1200高さキャンバスの中央付近

    const imageNode = {
        id: `image-node-${nextImageNodeId++}`,
        type: 'image',
        imageId: imageId,
        title: image.name,
        posX: startX,
        posY: startY + existingImageNodes * spacing,  // 縦方向に並べる
        // 元画像の原点（正規化座標 0.0〜1.0）
        originX: 0.5,
        originY: 0.5
    };

    globalNodes.push(imageNode);
    renderNodeGraph();
}

// 画像ライブラリから画像を削除
function deleteImageFromLibrary(imageId) {
    // この画像を使用しているノードがあるか確認
    const usingNodes = globalNodes.filter(n => n.type === 'image' && n.imageId === imageId);
    if (usingNodes.length > 0) {
        if (!confirm(`この画像は${usingNodes.length}個のノードで使用されています。削除してもよろしいですか？`)) {
            return;
        }
        // ノードも削除
        globalNodes = globalNodes.filter(n => !(n.type === 'image' && n.imageId === imageId));
    }

    // 画像を削除
    uploadedImages = uploadedImages.filter(img => img.id !== imageId);

    renderImageLibrary();
    renderNodeGraph();
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
                    resolve({
                        data: imageData.data,
                        width: img.width,
                        height: img.height,
                        name: file.name
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

// 出力設定を適用（サイドバーから）
function applyOutputSettings() {
    const width = parseInt(document.getElementById('sidebar-canvas-width').value);
    const height = parseInt(document.getElementById('sidebar-canvas-height').value);

    // NaN チェックを追加（空文字やパース失敗時）
    if (isNaN(width) || isNaN(height) || width < 100 || width > 2000 || height < 100 || height > 2000) {
        alert('キャンバスサイズは100〜2000の範囲で指定してください');
        return;
    }

    // 原点座標を取得（入力欄から）
    const originX = parseInt(document.getElementById('sidebar-origin-x').value) || 0;
    const originY = parseInt(document.getElementById('sidebar-origin-y').value) || 0;

    // 原点をピクセル座標で保存
    canvasOrigin = {
        x: Math.max(0, Math.min(originX, width)),
        y: Math.max(0, Math.min(originY, height))
    };
    console.log('Output settings applied:', width, 'x', height, 'origin:', canvasOrigin);

    canvasWidth = width;
    canvasHeight = height;
    canvas.width = width;
    canvas.height = height;

    // 現在の倍率で表示サイズを更新
    updateCanvasDisplayScale();

    graphEvaluator.setCanvasSize(width, height);
    graphEvaluator.setDstOrigin(canvasOrigin.x, canvasOrigin.y);
    updatePreviewFromGraph();

    // キャンバスサイズ変更後にスクロール位置を再調整
    centerPreviewScroll();
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
    const toggleBtn = document.getElementById('node-graph-toggle');
    const container = document.querySelector('.node-graph-canvas-container');

    // トグル機能
    toggleBtn.addEventListener('click', () => {
        container.classList.toggle('hidden');
        toggleBtn.classList.toggle('collapsed');
    });

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

    // 初期スクロール位置を中央に設定
    centerNodeGraphScroll();

    // コンテナのリサイズを監視してスクロール位置を追従
    if (container) {
        const resizeObserver = new ResizeObserver(() => {
            centerNodeGraphScroll();
        });
        resizeObserver.observe(container);
    }

    // ウィンドウリサイズ時もスクロール位置を更新
    window.addEventListener('resize', () => {
        centerNodeGraphScroll();
    });

    // 出力画像エリアのスクロール位置を初期化
    initializePreviewScroll();
}

// 出力画像エリアのスクロール監視を初期化
function initializePreviewScroll() {
    const container = document.querySelector('.canvas-container');
    if (!container) return;

    // 初期スクロール位置を中央に設定
    centerPreviewScroll();

    // コンテナのリサイズを監視してスクロール位置を追従
    const resizeObserver = new ResizeObserver(() => {
        centerPreviewScroll();
    });
    resizeObserver.observe(container);

    // ウィンドウリサイズ時もスクロール位置を更新
    window.addEventListener('resize', () => {
        centerPreviewScroll();
    });

    console.log('Preview scroll initialized');
}

// ノードグラフのスクロール位置を中央に設定
function centerNodeGraphScroll() {
    const container = document.querySelector('.node-graph-canvas-container');
    if (!container || !nodeGraphSvg) return;

    // SVGのサイズを取得
    const svgWidth = nodeGraphSvg.width.baseVal.value || 1600;
    const svgHeight = nodeGraphSvg.height.baseVal.value || 1200;

    // コンテナの表示サイズを取得
    const containerWidth = container.clientWidth;
    const containerHeight = container.clientHeight;

    // 中央にスクロール
    const scrollX = Math.max(0, (svgWidth - containerWidth) / 2);
    const scrollY = Math.max(0, (svgHeight - containerHeight) / 2);

    container.scrollLeft = scrollX;
    container.scrollTop = scrollY;

    console.log(`Node graph scroll centered: (${scrollX}, ${scrollY})`);
}

// 出力画像エリアのスクロール位置を中央に設定
function centerPreviewScroll() {
    const container = document.querySelector('.canvas-container');
    const previewCanvas = document.getElementById('preview-canvas');
    if (!container || !previewCanvas) return;

    // キャンバスの表示サイズを取得（現在の倍率でスケールされたサイズ）
    const canvasDisplayWidth = previewCanvas.offsetWidth || (canvasWidth * previewScale);
    const canvasDisplayHeight = previewCanvas.offsetHeight || (canvasHeight * previewScale);

    // コンテナの表示サイズを取得
    const containerWidth = container.clientWidth;
    const containerHeight = container.clientHeight;

    // 中央にスクロール
    const scrollX = Math.max(0, (canvasDisplayWidth - containerWidth) / 2);
    const scrollY = Math.max(0, (canvasDisplayHeight - containerHeight) / 2);

    container.scrollLeft = scrollX;
    container.scrollTop = scrollY;

    console.log(`Preview scroll centered: (${scrollX}, ${scrollY})`);
}

function renderNodeGraph() {
    if (!nodeGraphSvg) return;

    // ドラッグ中の接続線を明示的にクリーンアップ
    if (isDraggingConnection) {
        stopDraggingConnection();
    }

    // SVGをクリア
    nodeGraphSvg.innerHTML = '';

    // 出力ノードが存在しない場合は追加
    if (!globalNodes.find(n => n.type === 'output')) {
        globalNodes.push({
            id: 'output',
            type: 'output',
            title: '出力',
            posX: 1000,  // 1600幅キャンバスの中央寄り右側
            posY: 550   // 1200高さキャンバスの中央付近
        });
    }

    // 接続線を描画（レイヤーと手動接続の両方）
    drawAllConnections();

    // ノードを描画
    globalNodes.forEach(node => {
        drawGlobalNode(node);
    });
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
    const minOffset = 80;

    // オフセットは以下の最大値を使用：
    // - 最小オフセット（常に確保）
    // - 水平距離の半分（通常のS字用）
    // - 垂直距離の比率（ループの膨らみ用）
    const offset = Math.max(minOffset, dx / 2, dy * 0.3);

    const cp1x = fromPos.x + offset;  // 常に右へ
    const cp2x = toPos.x - offset;    // 常に左から

    return `M ${fromPos.x} ${fromPos.y} C ${cp1x} ${fromPos.y}, ${cp2x} ${toPos.y}, ${toPos.x} ${toPos.y}`;
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

// ノードの高さを動的に計算
function getNodeHeight(node) {
    if (node.type === 'affine') {
        return 200; // アフィン変換ノード: 両モード共通で200px
    } else if (node.type === 'composite') {
        const inputCount = node.inputs ? node.inputs.length : 2;
        const baseHeight = 60; // ヘッダー + パディング
        const sliderHeight = 22; // 各アルファスライダー
        const buttonHeight = 30; // ボタンコンテナ
        return baseHeight + (inputCount * sliderHeight) + buttonHeight;
    } else if (node.type === 'image') {
        return 100; // 画像ノード: タイトル + アルファスライダー
    } else if (node.type === 'filter' && node.independent) {
        return 100; // 独立フィルタ: タイトル + パラメータスライダー
    } else {
        return 80; // デフォルト（出力ノード等）
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

    // 画像ノードの場合、サムネイルと原点セレクタを表示
    if (node.type === 'image' && node.imageId !== undefined) {
        const image = uploadedImages.find(img => img.id === node.imageId);
        if (image && image.imageData) {
            const contentRow = document.createElement('div');
            contentRow.style.cssText = 'display: flex; align-items: center; gap: 8px; padding: 4px;';

            // サムネイル
            const img = document.createElement('img');
            img.src = createThumbnailDataURL(image.imageData);
            img.style.cssText = 'width: 50px; height: 38px; object-fit: cover; border-radius: 3px;';
            contentRow.appendChild(img);

            // 原点セレクタ（9点グリッド）
            const originGrid = document.createElement('div');
            originGrid.className = 'node-origin-grid';
            originGrid.dataset.nodeId = node.id;

            const originValues = [
                { x: 0, y: 0 }, { x: 0.5, y: 0 }, { x: 1, y: 0 },
                { x: 0, y: 0.5 }, { x: 0.5, y: 0.5 }, { x: 1, y: 0.5 },
                { x: 0, y: 1 }, { x: 0.5, y: 1 }, { x: 1, y: 1 }
            ];

            originValues.forEach(({ x, y }) => {
                const btn = document.createElement('button');
                btn.className = 'node-origin-point';
                btn.dataset.x = x;
                btn.dataset.y = y;
                if (node.originX === x && node.originY === y) {
                    btn.classList.add('selected');
                }
                btn.addEventListener('click', (e) => {
                    e.stopPropagation();
                    // 選択状態を更新
                    originGrid.querySelectorAll('.node-origin-point').forEach(b => b.classList.remove('selected'));
                    btn.classList.add('selected');
                    // ノードの原点を更新
                    node.originX = x;
                    node.originY = y;
                    throttledUpdatePreview();
                });
                originGrid.appendChild(btn);
            });

            contentRow.appendChild(originGrid);
            nodeBox.appendChild(contentRow);
        }
    }

    // 独立フィルタノードの場合、パラメータスライダーを追加
    if (node.type === 'filter' && node.independent) {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';

        if (node.filterType === 'brightness') {
            controls.innerHTML = `
                <label style="font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;">
                    <span style="min-width: 40px;">明るさ:</span>
                    <input type="range" class="filter-param-slider" min="-1" max="1" step="0.01" value="${node.param || 0}" style="width: 60px;">
                    <span class="param-display">${(node.param || 0).toFixed(2)}</span>
                </label>
            `;
        } else if (node.filterType === 'blur') {
            controls.innerHTML = `
                <label style="font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;">
                    <span style="min-width: 40px;">半径:</span>
                    <input type="range" class="filter-param-slider" min="1" max="10" step="1" value="${node.param || 3}" style="width: 60px;">
                    <span class="param-display">${Math.round(node.param || 3)}px</span>
                </label>
            `;
        } else if (node.filterType === 'alpha') {
            controls.innerHTML = `
                <label style="font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;">
                    <span style="min-width: 40px;">α:</span>
                    <input type="range" class="filter-param-slider" min="0" max="1" step="0.01" value="${node.param || 1.0}" style="width: 60px;">
                    <span class="param-display">${Math.round((node.param || 1.0) * 100)}%</span>
                </label>
            `;
        }

        const slider = controls.querySelector('.filter-param-slider');
        const display = controls.querySelector('.param-display');

        if (slider && display) {
            slider.addEventListener('input', (e) => {
                const value = parseFloat(e.target.value);
                node.param = value;

                if (node.filterType === 'brightness') {
                    display.textContent = value.toFixed(2);
                } else if (node.filterType === 'blur') {
                    display.textContent = Math.round(value) + 'px';
                } else if (node.filterType === 'alpha') {
                    display.textContent = Math.round(value * 100) + '%';
                }

                throttledUpdatePreview();
            });
        }

        nodeBox.appendChild(controls);
    }

    // 合成ノードの場合、動的なアルファスライダーと編集・追加ボタンを追加
    if (node.type === 'composite') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';

        // 各入力のアルファスライダーを動的に生成
        if (node.inputs && node.inputs.length > 0) {
            node.inputs.forEach((input, index) => {
                const label = document.createElement('label');
                label.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';

                // XSS対策: DOM APIを使用してHTML構築
                const span = document.createElement('span');
                span.style.minWidth = '40px';
                span.textContent = `α${index + 1}:`;

                const slider = document.createElement('input');
                slider.type = 'range';
                slider.className = 'alpha-slider';
                slider.dataset.inputId = input.id;
                slider.min = '0';
                slider.max = '1';
                slider.step = '0.01';
                slider.value = String(input.alpha);
                slider.style.width = '60px';

                slider.addEventListener('input', (e) => {
                    input.alpha = parseFloat(e.target.value);
                    throttledUpdatePreview();
                });

                label.appendChild(span);
                label.appendChild(slider);
                controls.appendChild(label);
            });
        }

        // ボタンコンテナ
        const btnContainer = document.createElement('div');
        btnContainer.style.cssText = 'display: flex; gap: 2px; margin-top: 4px;';

        // 入力追加ボタン
        const addInputBtn = document.createElement('button');
        addInputBtn.textContent = '+ 入力';
        addInputBtn.style.cssText = 'font-size: 9px; padding: 2px 4px; flex: 1;';
        addInputBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            addCompositeInput(node);
        });

        btnContainer.appendChild(addInputBtn);
        controls.appendChild(btnContainer);

        nodeBox.appendChild(controls);
    }

    // アフィン変換ノードの場合、パラメータまたは行列のスライダーを追加
    if (node.type === 'affine') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';

        // モード切り替えトグル
        const modeToggle = document.createElement('div');
        modeToggle.style.cssText = 'font-size: 9px; margin-bottom: 4px; display: flex; gap: 2px;';

        const paramBtn = document.createElement('button');
        paramBtn.textContent = 'パラメータ';
        paramBtn.style.cssText = `font-size: 9px; padding: 2px 4px; flex: 1; ${node.matrixMode ? '' : 'background: #4CAF50; color: white;'}`;

        const matrixBtn = document.createElement('button');
        matrixBtn.textContent = '行列';
        matrixBtn.style.cssText = `font-size: 9px; padding: 2px 4px; flex: 1; ${node.matrixMode ? 'background: #4CAF50; color: white;' : ''}`;

        paramBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            node.matrixMode = false;
            renderNodeGraph();
            throttledUpdatePreview();
        });

        matrixBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            node.matrixMode = true;
            renderNodeGraph();
            throttledUpdatePreview();
        });

        modeToggle.appendChild(paramBtn);
        modeToggle.appendChild(matrixBtn);
        controls.appendChild(modeToggle);

        // パラメータモード
        if (!node.matrixMode) {
            // 平行移動X
            const txLabel = document.createElement('label');
            txLabel.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
            txLabel.innerHTML = `<span style="min-width: 40px;">X:</span><input type="range" class="affine-tx-slider" min="-500" max="500" step="1" value="${node.translateX || 0}" style="width: 60px;"> <span class="tx-display">${Math.round(node.translateX || 0)}</span>`;
            const txSlider = txLabel.querySelector('.affine-tx-slider');
            const txDisplay = txLabel.querySelector('.tx-display');
            txSlider.addEventListener('input', (e) => {
                node.translateX = parseFloat(e.target.value);
                txDisplay.textContent = Math.round(node.translateX);
                // 行列モードに同期
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0,
                    node.translateY || 0,
                    node.rotation || 0,
                    node.scaleX || 1,
                    node.scaleY || 1
                );
                throttledUpdatePreview();
            });
            controls.appendChild(txLabel);

            // 平行移動Y
            const tyLabel = document.createElement('label');
            tyLabel.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
            tyLabel.innerHTML = `<span style="min-width: 40px;">Y:</span><input type="range" class="affine-ty-slider" min="-500" max="500" step="1" value="${node.translateY || 0}" style="width: 60px;"> <span class="ty-display">${Math.round(node.translateY || 0)}</span>`;
            const tySlider = tyLabel.querySelector('.affine-ty-slider');
            const tyDisplay = tyLabel.querySelector('.ty-display');
            tySlider.addEventListener('input', (e) => {
                node.translateY = parseFloat(e.target.value);
                tyDisplay.textContent = Math.round(node.translateY);
                // 行列モードに同期
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0,
                    node.translateY || 0,
                    node.rotation || 0,
                    node.scaleX || 1,
                    node.scaleY || 1
                );
                throttledUpdatePreview();
            });
            controls.appendChild(tyLabel);

            // 回転
            const rotLabel = document.createElement('label');
            rotLabel.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
            rotLabel.innerHTML = `<span style="min-width: 40px;">回転:</span><input type="range" class="affine-rot-slider" min="-180" max="180" step="0.1" value="${node.rotation || 0}" style="width: 60px;"> <span class="rot-display">${(node.rotation || 0).toFixed(1)}°</span>`;
            const rotSlider = rotLabel.querySelector('.affine-rot-slider');
            const rotDisplay = rotLabel.querySelector('.rot-display');
            rotSlider.addEventListener('input', (e) => {
                node.rotation = parseFloat(e.target.value);
                rotDisplay.textContent = node.rotation.toFixed(1) + '°';
                // 行列モードに同期
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0,
                    node.translateY || 0,
                    node.rotation || 0,
                    node.scaleX || 1,
                    node.scaleY || 1
                );
                throttledUpdatePreview();
            });
            controls.appendChild(rotLabel);

            // スケールX
            const sxLabel = document.createElement('label');
            sxLabel.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
            sxLabel.innerHTML = `<span style="min-width: 40px;">SX:</span><input type="range" class="affine-sx-slider" min="0.1" max="3" step="0.1" value="${node.scaleX !== undefined ? node.scaleX : 1}" style="width: 60px;"> <span class="sx-display">${(node.scaleX !== undefined ? node.scaleX : 1).toFixed(1)}</span>`;
            const sxSlider = sxLabel.querySelector('.affine-sx-slider');
            const sxDisplay = sxLabel.querySelector('.sx-display');
            sxSlider.addEventListener('input', (e) => {
                node.scaleX = parseFloat(e.target.value);
                sxDisplay.textContent = node.scaleX.toFixed(1);
                // 行列モードに同期
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0,
                    node.translateY || 0,
                    node.rotation || 0,
                    node.scaleX || 1,
                    node.scaleY || 1
                );
                throttledUpdatePreview();
            });
            controls.appendChild(sxLabel);

            // スケールY
            const syLabel = document.createElement('label');
            syLabel.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
            syLabel.innerHTML = `<span style="min-width: 40px;">SY:</span><input type="range" class="affine-sy-slider" min="0.1" max="3" step="0.1" value="${node.scaleY !== undefined ? node.scaleY : 1}" style="width: 60px;"> <span class="sy-display">${(node.scaleY !== undefined ? node.scaleY : 1).toFixed(1)}</span>`;
            const sySlider = syLabel.querySelector('.affine-sy-slider');
            const syDisplay = syLabel.querySelector('.sy-display');
            sySlider.addEventListener('input', (e) => {
                node.scaleY = parseFloat(e.target.value);
                syDisplay.textContent = node.scaleY.toFixed(1);
                // 行列モードに同期
                node.matrix = calculateMatrixFromParams(
                    node.translateX || 0,
                    node.translateY || 0,
                    node.rotation || 0,
                    node.scaleX || 1,
                    node.scaleY || 1
                );
                throttledUpdatePreview();
            });
            controls.appendChild(syLabel);
        }
        // 行列モード
        else {
            // 行列要素 a, b, c, d, tx, ty
            const matrixParams = [
                { name: 'a', label: 'a', min: -3, max: 3, step: 0.1, default: 1 },
                { name: 'b', label: 'b', min: -3, max: 3, step: 0.1, default: 0 },
                { name: 'c', label: 'c', min: -3, max: 3, step: 0.1, default: 0 },
                { name: 'd', label: 'd', min: -3, max: 3, step: 0.1, default: 1 },
                { name: 'tx', label: 'tx', min: -500, max: 500, step: 1, default: 0 },
                { name: 'ty', label: 'ty', min: -500, max: 500, step: 1, default: 0 }
            ];

            matrixParams.forEach(param => {
                const value = node.matrix && node.matrix[param.name] !== undefined ? node.matrix[param.name] : param.default;
                const label = document.createElement('label');
                label.style.cssText = 'font-size: 10px; display: flex; align-items: center; gap: 4px; margin: 2px 0;';
                label.innerHTML = `<span style="min-width: 40px;">${param.label}:</span><input type="range" class="affine-matrix-slider" data-param="${param.name}" min="${param.min}" max="${param.max}" step="${param.step}" value="${value}" style="width: 60px;"> <span class="matrix-display">${value.toFixed(param.step >= 1 ? 0 : 2)}</span>`;

                const slider = label.querySelector('.affine-matrix-slider');
                const display = label.querySelector('.matrix-display');

                slider.addEventListener('input', (e) => {
                    if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
                    const val = parseFloat(e.target.value);
                    node.matrix[param.name] = val;
                    display.textContent = val.toFixed(param.step >= 1 ? 0 : 2);
                    throttledUpdatePreview();
                });

                controls.appendChild(label);
            });
        }

        nodeBox.appendChild(controls);
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
        circle.setAttribute('fill', '#4a5568');
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
        circle.setAttribute('fill', '#48bb78');
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

        case 'affine':
            // アフィン変換ノード: 入力1つ、出力1つ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            ports.outputs.push({ id: 'out', label: '出力', type: 'image' });
            break;

        case 'output':
            // 出力ノード: 入力のみ
            ports.inputs.push({ id: 'in', label: '入力', type: 'image' });
            break;
    }

    return ports;
}

// 接続を追加
function addConnection(fromNodeId, fromPortId, toNodeId, toPortId) {
    // 既存の接続をチェック（同じポートへの接続は1つのみ）
    const existingIndex = globalConnections.findIndex(
        conn => conn.toNodeId === toNodeId && conn.toPortId === toPortId
    );

    if (existingIndex >= 0) {
        // 既存の接続を置き換え
        globalConnections[existingIndex] = {
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
    // 既存の合成ノードの数を数えて位置をずらす
    const existingCompositeCount = globalNodes.filter(
        n => n.type === 'composite'
    ).length;

    const compositeNode = {
        id: `composite-${nextCompositeId++}`,
        type: 'composite',
        title: '合成',
        posX: 750,  // 1600幅キャンバスの中央エリア
        posY: 400 + existingCompositeCount * 150,  // 中央付近から配置
        // 動的な入力配列（デフォルトで2つの入力）
        inputs: [
            { id: 'in1', alpha: 1.0 },
            { id: 'in2', alpha: 1.0 }
        ]
    };

    globalNodes.push(compositeNode);
    renderNodeGraph();
}

// アフィン変換ノードを追加
function addAffineNode() {
    // 既存のアフィンノードの数を数えて位置をずらす
    const existingAffineCount = globalNodes.filter(
        n => n.type === 'affine'
    ).length;

    const affineNode = {
        id: `affine-${Date.now()}`,
        type: 'affine',
        title: 'アフィン変換',
        posX: 750,  // 1600幅キャンバスの中央エリア
        posY: 400 + existingAffineCount * 150,  // 中央付近から配置
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
}

// 合成ノードに入力を追加
function addCompositeInput(node) {
    if (!node.inputs) {
        node.inputs = [];
    }

    const newIndex = node.inputs.length + 1;
    node.inputs.push({
        id: `in${newIndex}`,
        alpha: 1.0
    });

    // ノードグラフを再描画
    renderNodeGraph();
}

// 独立フィルタノードを追加（レイヤーに属さない）
function addIndependentFilterNode(filterType) {
    // デフォルトパラメータ
    let defaultParam = 0.0;
    if (filterType === 'brightness') {
        defaultParam = 0.0;  // -1.0 ~ 1.0
    } else if (filterType === 'blur') {
        defaultParam = 3.0;  // radius
    } else if (filterType === 'alpha') {
        defaultParam = 1.0;  // 0.0 ~ 1.0
    }

    // 既存の独立フィルタノードの数を数えて位置をずらす
    const existingFilterCount = globalNodes.filter(
        n => n.type === 'filter' && n.independent
    ).length;

    const filterNode = {
        id: `independent-filter-${nextIndependentFilterId++}`,
        type: 'filter',
        independent: true,  // 独立フィルタノードであることを示すフラグ
        filterType: filterType,
        param: defaultParam,
        title: getFilterDisplayName(filterType),
        posX: 700,  // 1600幅キャンバスの中央エリア（画像ノードと出力の中間）
        posY: 400 + existingFilterCount * 120  // 中央付近から配置
    };

    globalNodes.push(filterNode);
    renderNodeGraph();
}

// フィルタ表示名を取得
function getFilterDisplayName(filterType) {
    const names = {
        'grayscale': 'グレースケール',
        'brightness': '明るさ',
        'blur': 'ぼかし',
        'alpha': 'アルファ'
    };
    return names[filterType] || filterType;
}


// グラフベースのプレビュー更新（C++側で完結）
function updatePreviewFromGraph() {
    const perfStart = performance.now();

    const outputNode = globalNodes.find(n => n.type === 'output');
    if (!outputNode) {
        // 出力ノードがない場合はキャンバスをクリア
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
        return;
    }

    // 出力ノードへの入力接続を取得
    const inputConn = globalConnections.find(
        c => c.toNodeId === outputNode.id && c.toPortId === 'in'
    );

    if (!inputConn) {
        // 接続がない場合はキャンバスをクリア
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
        return;
    }

    // C++側にノードグラフ構造を渡す（1回のWASM呼び出しで完結）
    const evalStart = performance.now();

    // アフィンノードのパラメータを行列に統一してからC++に渡す
    const nodesForCpp = globalNodes.map(node => {
        if (node.type === 'affine' && !node.matrixMode) {
            // パラメータモードの場合、JS側で行列に変換
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
        return node;
    });

    graphEvaluator.setNodes(nodesForCpp);
    graphEvaluator.setConnections(globalConnections);

    // C++側でノードグラフ全体を評価
    const resultImage = graphEvaluator.evaluateGraph();

    const evalTime = performance.now() - evalStart;

    if (resultImage && resultImage.data) {
        // キャンバスに描画
        const drawStart = performance.now();
        const imageData = new ImageData(
            resultImage.data,
            resultImage.width,
            resultImage.height
        );

        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
        ctx.putImageData(imageData, 0, 0);
        const drawTime = performance.now() - drawStart;

        // C++側の詳細計測結果を取得
        const metrics = graphEvaluator.getPerfMetrics();
        const totalTime = performance.now() - perfStart;

        // 詳細ログ出力
        const details = [];
        if (metrics.filterCount > 0) {
            details.push(`Filter: ${metrics.filterTime.toFixed(2)}ms (x${metrics.filterCount})`);
        }
        if (metrics.affineCount > 0) {
            details.push(`Affine: ${metrics.affineTime.toFixed(2)}ms (x${metrics.affineCount})`);
        }
        if (metrics.compositeCount > 0) {
            details.push(`Composite: ${metrics.compositeTime.toFixed(2)}ms (x${metrics.compositeCount})`);
        }
        if (metrics.convertCount > 0) {
            details.push(`Convert: ${metrics.convertTime.toFixed(2)}ms (x${metrics.convertCount})`);
        }
        details.push(`Output: ${metrics.outputTime.toFixed(2)}ms`);

        console.log(`[Perf] Total: ${totalTime.toFixed(1)}ms | WASM: ${evalTime.toFixed(1)}ms (${details.join(', ')}) | Draw: ${drawTime.toFixed(1)}ms`);
    } else {
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
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

// ノードを削除
function deleteNode(node) {
    // outputノードは削除不可
    if (node.type === 'output') {
        alert('出力ノードは削除できません');
        return;
    }

    // 確認ダイアログ
    if (!confirm(`ノード「${node.title}」を削除しますか？`)) {
        return;
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
    throttledUpdatePreview();
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
