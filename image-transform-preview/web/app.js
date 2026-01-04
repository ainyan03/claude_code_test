// グローバル変数
let graphEvaluator;  // ノードグラフ評価エンジン（C++側）
let canvas;
let ctx;
let uploadedImages = [];  // 画像ライブラリ
let nextImageId = 1;
let canvasWidth = 800;
let canvasHeight = 600;
let canvasOrigin = { x: 400, y: 300 };  // キャンバス原点（ピクセル座標）

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

    // 合成ノード編集パネル初期化
    initializeCompositeEditPanel();

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
    // 画像追加ボタン
    document.getElementById('add-image-btn').addEventListener('click', () => {
        document.getElementById('image-input').click();
    });

    // 画像選択
    document.getElementById('image-input').addEventListener('change', handleImageUpload);

    // キャンバスサイズ変更
    document.getElementById('resize-canvas').addEventListener('click', resizeCanvas);

    // キャンバス原点選択（9点グリッド）
    // 初期選択は中央（0.5, 0.5）
    setupOriginGrid('canvas-origin-grid', { x: 0.5, y: 0.5 }, (normalizedOrigin) => {
        // 9点ボタン押下時：正規化座標からピクセル座標を計算して入力欄に反映
        const w = parseInt(document.getElementById('canvas-width').value) || 800;
        const h = parseInt(document.getElementById('canvas-height').value) || 600;
        document.getElementById('origin-x').value = Math.round(normalizedOrigin.x * w);
        document.getElementById('origin-y').value = Math.round(normalizedOrigin.y * h);
        // 注: canvasOrigin はサイズ変更ボタン押下時に更新される
    });

    // 原点座標入力欄の初期値を設定
    document.getElementById('origin-x').value = canvasOrigin.x;
    document.getElementById('origin-y').value = canvasOrigin.y;

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
    const libraryContainer = document.getElementById('images-library');
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
        posY: startY + existingImageNodes * spacing  // 縦方向に並べる
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

function resizeCanvas() {
    const width = parseInt(document.getElementById('canvas-width').value);
    const height = parseInt(document.getElementById('canvas-height').value);

    // NaN チェックを追加（空文字やパース失敗時）
    if (isNaN(width) || isNaN(height) || width < 100 || width > 2000 || height < 100 || height > 2000) {
        alert('キャンバスサイズは100〜2000の範囲で指定してください');
        return;
    }

    // 原点座標を取得（入力欄から）
    const originX = parseInt(document.getElementById('origin-x').value) || 0;
    const originY = parseInt(document.getElementById('origin-y').value) || 0;

    // 原点をピクセル座標で保存（将来のバックエンド連携用）
    canvasOrigin = {
        x: Math.max(0, Math.min(originX, width)),
        y: Math.max(0, Math.min(originY, height))
    };
    console.log('Canvas resized:', width, 'x', height, 'origin:', canvasOrigin);

    canvasWidth = width;
    canvasHeight = height;
    canvas.width = width;
    canvas.height = height;

    graphEvaluator.setCanvasSize(width, height);  // graphEvaluatorのサイズも更新
    // TODO: 原点座標もバックエンドに渡す（設計決定後に実装）
    updatePreviewFromGraph();
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

function drawConnectionBetweenPorts(fromNode, fromPortId, toNode, toPortId) {
    const ns = 'http://www.w3.org/2000/svg';

    // ポート位置を計算
    const fromPos = getPortPosition(fromNode, fromPortId, 'output');
    const toPos = getPortPosition(toNode, toPortId, 'input');

    const path = document.createElementNS(ns, 'path');
    const midX = (fromPos.x + toPos.x) / 2;
    const d = `M ${fromPos.x} ${fromPos.y} C ${midX} ${fromPos.y}, ${midX} ${toPos.y}, ${toPos.x} ${toPos.y}`;

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
function calculateMatrixFromParams(translateX, translateY, rotation, scaleX, scaleY) {
    // キャンバス中心を基準点とする
    const centerX = canvasWidth / 2.0;
    const centerY = canvasHeight / 2.0;

    // 回転を度からラジアンに変換
    const rad = rotation * Math.PI / 180.0;
    const cos = Math.cos(rad);
    const sin = Math.sin(rad);

    // 行列要素を計算
    // 変換の順序：中心に移動 → スケール → 回転 → 元に戻す → 平行移動
    const a = cos * scaleX;
    const b = -sin * scaleY;
    const c = sin * scaleX;
    const d = cos * scaleY;
    const tx = -centerX * a - centerY * c + centerX + translateX;
    const ty = -centerX * b - centerY * d + centerY + translateY;

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

    // 画像ノードの場合、サムネイルを表示
    if (node.type === 'image' && node.imageId !== undefined) {
        const image = uploadedImages.find(img => img.id === node.imageId);
        if (image && image.imageData) {
            const thumbnail = document.createElement('div');
            thumbnail.className = 'node-box-thumbnail';
            thumbnail.style.cssText = 'padding: 4px; text-align: center;';

            const img = document.createElement('img');
            img.src = createThumbnailDataURL(image.imageData);
            img.style.cssText = 'max-width: 80px; max-height: 60px; border-radius: 4px;';

            thumbnail.appendChild(img);
            nodeBox.appendChild(thumbnail);
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

        // 詳細編集ボタン
        const editBtn = document.createElement('button');
        editBtn.textContent = '⚙️ 詳細';
        editBtn.style.cssText = 'font-size: 9px; padding: 2px 4px; flex: 1;';
        editBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            openCompositeEditPanel(node);
        });

        btnContainer.appendChild(addInputBtn);
        btnContainer.appendChild(editBtn);
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
            rotLabel.innerHTML = `<span style="min-width: 40px;">回転:</span><input type="range" class="affine-rot-slider" min="-180" max="180" step="1" value="${node.rotation || 0}" style="width: 60px;"> <span class="rot-display">${Math.round(node.rotation || 0)}°</span>`;
            const rotSlider = rotLabel.querySelector('.affine-rot-slider');
            const rotDisplay = rotLabel.querySelector('.rot-display');
            rotSlider.addEventListener('input', (e) => {
                node.rotation = parseFloat(e.target.value);
                rotDisplay.textContent = Math.round(node.rotation) + '°';
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
                label.innerHTML = `<span style="min-width: 40px;">${param.label}:</span><input type="range" class="affine-matrix-slider" data-param="${param.name}" min="${param.min}" max="${param.max}" step="${param.step}" value="${value}" style="width: 60px;"> <span class="matrix-display">${value.toFixed(param.step >= 1 ? 0 : 1)}</span>`;

                const slider = label.querySelector('.affine-matrix-slider');
                const display = label.querySelector('.matrix-display');

                slider.addEventListener('input', (e) => {
                    if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
                    const val = parseFloat(e.target.value);
                    node.matrix[param.name] = val;
                    display.textContent = val.toFixed(param.step >= 1 ? 0 : 1);
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
                    const midX = (fromPos.x + toPos.x) / 2;
                    const d = `M ${fromPos.x} ${fromPos.y} C ${midX} ${fromPos.y}, ${midX} ${toPos.y}, ${toPos.x} ${toPos.y}`;
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
        ],
        affineParams: {
            translateX: 0,
            translateY: 0,
            rotation: 0,
            scaleX: 1.0,
            scaleY: 1.0,
            alpha: 1.0
        }
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

    graphEvaluator.setNodes(globalNodes);
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

        const totalTime = performance.now() - perfStart;
        console.log(`[Perf] Total: ${totalTime.toFixed(1)}ms (Eval: ${evalTime.toFixed(1)}ms, Draw: ${drawTime.toFixed(1)}ms)`);
    } else {
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
    }
}

// ========================================
// 合成ノード編集パネル
// ========================================

let currentEditingComposite = null;

function initializeCompositeEditPanel() {
    const closeBtn = document.getElementById('close-composite-panel');
    if (closeBtn) {
        closeBtn.addEventListener('click', closeCompositeEditPanel);
    }

    // 各パラメータスライダーのイベントリスナー
    const translateX = document.getElementById('composite-translatex');
    const translateY = document.getElementById('composite-translatey');
    const rotation = document.getElementById('composite-rotation');
    const scaleX = document.getElementById('composite-scalex');
    const scaleY = document.getElementById('composite-scaley');
    const alpha = document.getElementById('composite-alpha');

    if (translateX) {
        translateX.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.translateX = value;
            document.getElementById('composite-translatex-value').textContent = value.toFixed(0);
            throttledUpdatePreview();
        });
    }

    if (translateY) {
        translateY.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.translateY = value;
            document.getElementById('composite-translatey-value').textContent = value.toFixed(0);
            throttledUpdatePreview();
        });
    }

    if (rotation) {
        rotation.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.rotation = value;  // 度数法で保存
            document.getElementById('composite-rotation-value').textContent = value.toFixed(0) + '°';
            throttledUpdatePreview();
        });
    }

    if (scaleX) {
        scaleX.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.scaleX = value;
            document.getElementById('composite-scalex-value').textContent = value.toFixed(2);
            throttledUpdatePreview();
        });
    }

    if (scaleY) {
        scaleY.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.scaleY = value;
            document.getElementById('composite-scaley-value').textContent = value.toFixed(2);
            throttledUpdatePreview();
        });
    }

    if (alpha) {
        alpha.addEventListener('input', (e) => {
            if (!currentEditingComposite) return;
            const value = parseFloat(e.target.value);
            currentEditingComposite.affineParams.alpha = value;
            document.getElementById('composite-alpha-value').textContent = value.toFixed(2);
            throttledUpdatePreview();
        });
    }
}

function openCompositeEditPanel(node) {
    currentEditingComposite = node;
    const panel = document.getElementById('composite-edit-panel');

    if (!panel) return;

    // パネルを表示
    panel.classList.remove('hidden');

    // 現在の値をスライダーに反映
    const params = node.affineParams;
    if (params) {
        document.getElementById('composite-translatex').value = params.translateX;
        document.getElementById('composite-translatex-value').textContent = params.translateX.toFixed(0);

        document.getElementById('composite-translatey').value = params.translateY;
        document.getElementById('composite-translatey-value').textContent = params.translateY.toFixed(0);

        document.getElementById('composite-rotation').value = params.rotation;
        document.getElementById('composite-rotation-value').textContent = params.rotation.toFixed(0) + '°';

        document.getElementById('composite-scalex').value = params.scaleX;
        document.getElementById('composite-scalex-value').textContent = params.scaleX.toFixed(2);

        document.getElementById('composite-scaley').value = params.scaleY;
        document.getElementById('composite-scaley-value').textContent = params.scaleY.toFixed(2);

        document.getElementById('composite-alpha').value = params.alpha;
        document.getElementById('composite-alpha-value').textContent = params.alpha.toFixed(2);
    }
}

function closeCompositeEditPanel() {
    const panel = document.getElementById('composite-edit-panel');
    if (panel) {
        panel.classList.add('hidden');
    }
    currentEditingComposite = null;
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
