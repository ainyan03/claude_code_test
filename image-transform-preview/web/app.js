// グローバル変数
let processor;
let graphEvaluator;  // ノードグラフ評価エンジン（C++側）
let canvas;
let ctx;
let layers = [];  // 後方互換性のため一時保持
let uploadedImages = [];  // 画像ライブラリ
let nextImageId = 1;
let canvasWidth = 800;
let canvasHeight = 600;

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
    if (!processor) {
        console.error('WebAssembly loading timeout');
        const loadingEl = document.getElementById('loading');
        if (loadingEl) {
            loadingEl.innerHTML = '<div class="spinner"></div><p style="color: #ff6b6b;">WebAssemblyの読み込みに失敗しました。<br>ページを再読み込みしてください。</p>';
        }
    }
}, 10000);

function initializeApp() {
    if (processor) {
        console.log('App already initialized');
        return; // 既に初期化済み
    }

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

    // ImageProcessor初期化（WebAssemblyのみ）
    if (typeof WasmModule !== 'undefined' && WasmModule.ImageProcessor) {
        processor = new WasmModule.ImageProcessor(canvasWidth, canvasHeight);
        console.log('Using WebAssembly backend');
    } else {
        console.error('WebAssembly module not loaded!', typeof WasmModule);
        alert('エラー: WebAssemblyモジュールの読み込みに失敗しました。ページを再読み込みしてください。');
        return;
    }

    // NodeGraphEvaluator初期化（C++側でグラフ評価）
    if (typeof WasmModule !== 'undefined' && WasmModule.NodeGraphEvaluator) {
        graphEvaluator = new WasmModule.NodeGraphEvaluator(canvasWidth, canvasHeight);
        console.log('NodeGraphEvaluator initialized');
    } else {
        console.error('NodeGraphEvaluator not found in WebAssembly module');
        alert('エラー: NodeGraphEvaluatorの読み込みに失敗しました。');
        return;
    }

    // イベントリスナー設定
    setupEventListeners();

    // グローバルノードグラフ初期化
    initializeNodeGraph();

    // 合成ノード編集パネル初期化
    initializeCompositeEditPanel();

    // 初期プレビュー
    updatePreview();

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

    // ダウンロードボタン
    document.getElementById('download-btn').addEventListener('click', downloadComposedImage);

    // 合成ノード追加ボタン
    document.getElementById('add-composite-btn').addEventListener('click', addCompositeNode);

    // 独立フィルタノード追加ボタン
    document.getElementById('add-filter-node-btn').addEventListener('click', () => {
        // セレクトボックスからフィルタタイプを取得
        const filterTypeSelect = document.getElementById('filter-type-select');
        const filterType = filterTypeSelect.value;
        if (filterType) {
            addIndependentFilterNode(filterType);
        }
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

    // C++側に画像を登録（後方互換性）
    graphEvaluator.setLayerImage(imageId, imageData.data, imageData.width, imageData.height);

    // UIを更新
    renderImageLibrary();
}

// 画像ライブラリUIを描画
function renderImageLibrary() {
    const libraryContainer = document.getElementById('images-library');
    libraryContainer.innerHTML = '';

    uploadedImages.forEach(image => {
        const template = document.getElementById('image-item-template');
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

    // 既存の同じ画像IDのノード数をカウント
    const existingCount = globalNodes.filter(n => n.type === 'image' && n.imageId === imageId).length;

    const imageNode = {
        id: `image-node-${nextImageNodeId++}`,
        type: 'image',
        imageId: imageId,
        alpha: 1.0,  // 画像ノードのアルファ値
        title: image.name,
        posX: 50,
        posY: 50 + existingCount * 120
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

function addLayer(imageData) {
    console.log('Adding layer:', imageData.name, imageData.width + 'x' + imageData.height);

    try {
        // プロセッサにレイヤー追加
        const layerId = processor.addLayer(imageData.data, imageData.width, imageData.height);
        console.log('Layer added with ID:', layerId);

        // レイヤー情報を保存（元の画像データも保持）
        const layer = {
            id: layerId,
            name: imageData.name,
            imageData: {  // ノードグラフ用に元画像データを保持
                data: new Uint8ClampedArray(imageData.data),
                width: imageData.width,
                height: imageData.height
            },
            params: {
                translateX: 0,
                translateY: 0,
                rotation: 0,
                scaleX: 1.0,
                scaleY: 1.0,
                alpha: 1.0
            },
            visible: true
        };
        layers.push(layer);

        // C++側のNodeGraphEvaluatorにも画像を登録
        graphEvaluator.setLayerImage(layerId, imageData.data, imageData.width, imageData.height);
        console.log('Layer image registered to NodeGraphEvaluator');

        // UIにレイヤー追加
        createLayerUI(layer);

        // グローバルノードグラフを更新
        renderNodeGraph();

        // プレビュー更新
        updatePreviewFromGraph();

        console.log('Layer successfully added and preview updated');
    } catch (error) {
        console.error('Error in addLayer:', error);
        throw error;
    }
}

function createLayerUI(layer) {
    const template = document.getElementById('layer-template');
    const layerElement = template.content.cloneNode(true);
    const layerDiv = layerElement.querySelector('.layer-item');
    layerDiv.dataset.layerId = layer.id;

    // レイヤー名設定
    layerDiv.querySelector('.layer-title').textContent = layer.name;

    // 可視性チェックボックス
    const visibilityCheckbox = layerDiv.querySelector('.layer-visibility');
    visibilityCheckbox.addEventListener('change', (e) => {
        layer.visible = e.target.checked;
        processor.setLayerVisibility(layer.id, layer.visible);
        updatePreviewFromGraph();
    });

    // パラメータスライダー
    setupSlider(layerDiv, layer, 'translateX', -canvasWidth/2, canvasWidth/2, (val) => val);
    setupSlider(layerDiv, layer, 'translateY', -canvasHeight/2, canvasHeight/2, (val) => val);
    setupSlider(layerDiv, layer, 'rotation', 0, 360, (val) => val + '°', (val) => val * Math.PI / 180);
    setupSlider(layerDiv, layer, 'scaleX', 0.1, 3.0, (val) => val.toFixed(2));
    setupSlider(layerDiv, layer, 'scaleY', 0.1, 3.0, (val) => val.toFixed(2));
    setupSlider(layerDiv, layer, 'alpha', 0, 1, (val) => Math.round(val * 100) + '%');

    // レイヤー順序変更ボタン
    layerDiv.querySelector('.layer-up').addEventListener('click', () => moveLayerUp(layer.id));
    layerDiv.querySelector('.layer-down').addEventListener('click', () => moveLayerDown(layer.id));

    // 削除ボタン
    layerDiv.querySelector('.layer-delete').addEventListener('click', () => deleteLayer(layer.id));

    // レイヤーリストに追加
    document.getElementById('layers-list').appendChild(layerDiv);
}

function setupSlider(layerDiv, layer, paramName, min, max, displayFn, transformFn) {
    // セレクター用に小文字に変換（HTMLのclass名が小文字のため）
    const selectorName = paramName.toLowerCase();
    const slider = layerDiv.querySelector(`.param-${selectorName}`);
    const valueSpan = slider.parentElement.querySelector('.param-value');

    slider.min = min;
    slider.max = max;
    slider.value = layer.params[paramName];
    const initialValue = parseFloat(slider.value);
    valueSpan.textContent = displayFn ? displayFn(initialValue) : initialValue;

    slider.addEventListener('input', (e) => {
        const value = parseFloat(e.target.value);
        layer.params[paramName] = value;
        valueSpan.textContent = displayFn ? displayFn(value) : value;

        // C++側にパラメータ更新（従来の方法用）
        const p = layer.params;
        const rotation = transformFn && paramName === 'rotation' ? transformFn(value) : p.rotation * Math.PI / 180;

        processor.setLayerTransform(
            layer.id,
            p.translateX,
            p.translateY,
            rotation,
            p.scaleX,
            p.scaleY,
            p.alpha
        );

        // スロットル付き更新でパフォーマンス向上
        throttledUpdatePreview();
    });
}

function moveLayerUp(layerId) {
    const index = layers.findIndex(l => l.id === layerId);
    if (index > 0) {
        // 配列内で入れ替え
        [layers[index], layers[index - 1]] = [layers[index - 1], layers[index]];

        // C++側で移動
        processor.moveLayer(index, index - 1);

        // UI更新
        refreshLayersList();
        updatePreviewFromGraph();
    }
}

function moveLayerDown(layerId) {
    const index = layers.findIndex(l => l.id === layerId);
    if (index >= 0 && index < layers.length - 1) {
        // 配列内で入れ替え
        [layers[index], layers[index + 1]] = [layers[index + 1], layers[index]];

        // C++側で移動
        processor.moveLayer(index, index + 1);

        // UI更新
        refreshLayersList();
        updatePreviewFromGraph();
    }
}

function deleteLayer(layerId) {
    const index = layers.findIndex(l => l.id === layerId);
    if (index >= 0) {
        // C++側から削除
        processor.removeLayer(layerId);

        // 配列から削除
        layers.splice(index, 1);

        // 残りのレイヤーのIDを更新（削除後にIDがずれるため）
        for (let i = index; i < layers.length; i++) {
            layers[i].id = i;
        }

        // UI更新
        refreshLayersList();
        updatePreviewFromGraph();
    }
}

function refreshLayersList() {
    const layersList = document.getElementById('layers-list');
    layersList.innerHTML = '';

    layers.forEach(layer => {
        createLayerUI(layer);
    });
}

function updatePreview() {
    if (!processor) return;

    try {
        // C++で画像合成
        const result = processor.compose();

        // キャンバスに描画
        const imageData = new ImageData(
            new Uint8ClampedArray(result.data),
            result.width,
            result.height
        );
        ctx.putImageData(imageData, 0, 0);
    } catch (error) {
        console.error('Failed to compose image:', error);
    }
}

function resizeCanvas() {
    const width = parseInt(document.getElementById('canvas-width').value);
    const height = parseInt(document.getElementById('canvas-height').value);

    if (width < 100 || width > 2000 || height < 100 || height > 2000) {
        alert('キャンバスサイズは100〜2000の範囲で指定してください');
        return;
    }

    canvasWidth = width;
    canvasHeight = height;
    canvas.width = width;
    canvas.height = height;

    processor.setCanvasSize(width, height);
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
}

function renderNodeGraph() {
    if (!nodeGraphSvg) return;

    // ドラッグ中の接続線を明示的にクリーンアップ
    if (isDraggingConnection) {
        stopDraggingConnection();
    }

    // SVGをクリア
    nodeGraphSvg.innerHTML = '';

    // 既存のノード位置を保持しながら、レイヤーベースのノードを更新
    syncNodesFromLayers();

    // 出力ノードが存在しない場合は追加
    if (!globalNodes.find(n => n.type === 'output')) {
        globalNodes.push({
            id: 'output',
            type: 'output',
            title: '出力',
            posX: 500,  // スマートフォン対応：右端にはみ出ないよう調整
            posY: 200
        });
    }

    // 接続線を描画（レイヤーと手動接続の両方）
    drawAllConnections();

    // ノードを描画
    globalNodes.forEach(node => {
        drawGlobalNode(node);
    });
}

// レイヤーからノードを同期（既存のノードは位置を保持）
function syncNodesFromLayers() {
    const existingNodes = new Map(globalNodes.map(n => [n.id, n]));
    const newNodes = [];

    // 各レイヤーから画像ノードとフィルタノードを作成
    layers.forEach((layer, layerIndex) => {
        // 画像ノード
        const imageId = `image-${layer.id}`;
        const existing = existingNodes.get(imageId);

        const imageNode = {
            id: imageId,
            type: 'image',
            layerId: layer.id,
            // paramsをコピーし、rotationを度→ラジアンに変換
            params: {
                translateX: layer.params.translateX,
                translateY: layer.params.translateY,
                rotation: layer.params.rotation * Math.PI / 180,  // 度→ラジアン
                scaleX: layer.params.scaleX,
                scaleY: layer.params.scaleY,
                alpha: layer.params.alpha
            },
            title: layer.name,
            posX: existing ? existing.posX : 50,
            posY: existing ? existing.posY : 50 + layerIndex * 150
        };
        newNodes.push(imageNode);

        // フィルタノード
        if (layer.filters) {
            layer.filters.forEach((filter, filterIndex) => {
                const filterId = `filter-${layer.id}-${filterIndex}`;
                const existing = existingNodes.get(filterId);

                const filterNode = {
                    id: filterId,
                    type: 'filter',
                    layerId: layer.id,
                    filterIndex: filterIndex,
                    filterType: filter.type,
                    title: getFilterDisplayName(filter.type),
                    nodeId: filter.nodeId,
                    posX: existing ? existing.posX : filter.posX + 250,
                    posY: existing ? existing.posY : filter.posY + layerIndex * 150
                };
                newNodes.push(filterNode);
            });
        }
    });

    // 合成ノード、出力ノード、独立フィルタノードは保持
    globalNodes.forEach(node => {
        if (node.type === 'composite' || node.type === 'output' ||
            (node.type === 'filter' && node.independent)) {
            newNodes.push(node);
        }
    });

    globalNodes = newNodes;
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

function getPortPosition(node, portId, portType) {
    const nodeWidth = 160;
    const nodeHeight = 80;
    const portRadius = 6;

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
    const nodeHeight = 80;

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

    // 画像ノードの場合、アルファスライダーを追加
    if (node.type === 'image' && node.imageId !== undefined) {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';

        const label = document.createElement('label');
        label.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
        label.innerHTML = `α: <input type="range" class="image-alpha-slider" min="0" max="1" step="0.01" value="${node.alpha || 1.0}" style="width: 80px;"> <span class="alpha-display">${Math.round((node.alpha || 1.0) * 100)}%</span>`;

        const slider = label.querySelector('.image-alpha-slider');
        const display = label.querySelector('.alpha-display');

        slider.addEventListener('input', (e) => {
            const value = parseFloat(e.target.value);
            node.alpha = value;
            display.textContent = Math.round(value * 100) + '%';
            throttledUpdatePreview();
        });

        controls.appendChild(label);
        nodeBox.appendChild(controls);
    }

    // 独立フィルタノードの場合、パラメータスライダーを追加
    if (node.type === 'filter' && node.independent) {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';

        if (node.filterType === 'brightness') {
            controls.innerHTML = `
                <label style="font-size: 10px; display: block; margin: 2px 0;">
                    明るさ: <input type="range" class="filter-param-slider" min="-1" max="1" step="0.01" value="${node.param || 0}" style="width: 70px;">
                    <span class="param-display">${(node.param || 0).toFixed(2)}</span>
                </label>
            `;
        } else if (node.filterType === 'blur') {
            controls.innerHTML = `
                <label style="font-size: 10px; display: block; margin: 2px 0;">
                    半径: <input type="range" class="filter-param-slider" min="1" max="10" step="1" value="${node.param || 3}" style="width: 70px;">
                    <span class="param-display">${Math.round(node.param || 3)}px</span>
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
                label.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
                label.innerHTML = `α${index + 1}: <input type="range" class="alpha-slider" data-input-id="${input.id}" min="0" max="1" step="0.01" value="${input.alpha}" style="width: 60px;">`;

                const slider = label.querySelector('.alpha-slider');
                slider.addEventListener('input', (e) => {
                    input.alpha = parseFloat(e.target.value);
                    throttledUpdatePreview();
                });

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
            txLabel.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
            txLabel.innerHTML = `X: <input type="range" class="affine-tx-slider" min="-500" max="500" step="1" value="${node.translateX || 0}" style="width: 60px;"> <span class="tx-display">${Math.round(node.translateX || 0)}</span>`;
            const txSlider = txLabel.querySelector('.affine-tx-slider');
            const txDisplay = txLabel.querySelector('.tx-display');
            txSlider.addEventListener('input', (e) => {
                node.translateX = parseFloat(e.target.value);
                txDisplay.textContent = Math.round(node.translateX);
                throttledUpdatePreview();
            });
            controls.appendChild(txLabel);

            // 平行移動Y
            const tyLabel = document.createElement('label');
            tyLabel.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
            tyLabel.innerHTML = `Y: <input type="range" class="affine-ty-slider" min="-500" max="500" step="1" value="${node.translateY || 0}" style="width: 60px;"> <span class="ty-display">${Math.round(node.translateY || 0)}</span>`;
            const tySlider = tyLabel.querySelector('.affine-ty-slider');
            const tyDisplay = tyLabel.querySelector('.ty-display');
            tySlider.addEventListener('input', (e) => {
                node.translateY = parseFloat(e.target.value);
                tyDisplay.textContent = Math.round(node.translateY);
                throttledUpdatePreview();
            });
            controls.appendChild(tyLabel);

            // 回転
            const rotLabel = document.createElement('label');
            rotLabel.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
            rotLabel.innerHTML = `回転: <input type="range" class="affine-rot-slider" min="-180" max="180" step="1" value="${node.rotation || 0}" style="width: 60px;"> <span class="rot-display">${Math.round(node.rotation || 0)}°</span>`;
            const rotSlider = rotLabel.querySelector('.affine-rot-slider');
            const rotDisplay = rotLabel.querySelector('.rot-display');
            rotSlider.addEventListener('input', (e) => {
                node.rotation = parseFloat(e.target.value);
                rotDisplay.textContent = Math.round(node.rotation) + '°';
                throttledUpdatePreview();
            });
            controls.appendChild(rotLabel);

            // スケールX
            const sxLabel = document.createElement('label');
            sxLabel.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
            sxLabel.innerHTML = `SX: <input type="range" class="affine-sx-slider" min="0.1" max="3" step="0.1" value="${node.scaleX !== undefined ? node.scaleX : 1}" style="width: 60px;"> <span class="sx-display">${(node.scaleX !== undefined ? node.scaleX : 1).toFixed(1)}</span>`;
            const sxSlider = sxLabel.querySelector('.affine-sx-slider');
            const sxDisplay = sxLabel.querySelector('.sx-display');
            sxSlider.addEventListener('input', (e) => {
                node.scaleX = parseFloat(e.target.value);
                sxDisplay.textContent = node.scaleX.toFixed(1);
                throttledUpdatePreview();
            });
            controls.appendChild(sxLabel);

            // スケールY
            const syLabel = document.createElement('label');
            syLabel.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
            syLabel.innerHTML = `SY: <input type="range" class="affine-sy-slider" min="0.1" max="3" step="0.1" value="${node.scaleY !== undefined ? node.scaleY : 1}" style="width: 60px;"> <span class="sy-display">${(node.scaleY !== undefined ? node.scaleY : 1).toFixed(1)}</span>`;
            const sySlider = syLabel.querySelector('.affine-sy-slider');
            const syDisplay = syLabel.querySelector('.sy-display');
            sySlider.addEventListener('input', (e) => {
                node.scaleY = parseFloat(e.target.value);
                syDisplay.textContent = node.scaleY.toFixed(1);
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
                label.style.cssText = 'font-size: 10px; display: block; margin: 2px 0;';
                label.innerHTML = `${param.label}: <input type="range" class="affine-matrix-slider" data-param="${param.name}" min="${param.min}" max="${param.max}" step="${param.step}" value="${value}" style="width: 60px;"> <span class="matrix-display">${value.toFixed(param.step >= 1 ? 0 : 1)}</span>`;

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
    drawNodePorts(node, nodeWidth, nodeHeight);

    // ドラッグ機能
    setupGlobalNodeDrag(nodeBox, foreignObject, node);
}

function drawNodePorts(node, nodeWidth, nodeHeight) {
    const ns = 'http://www.w3.org/2000/svg';
    const ports = getNodePorts(node);
    const portRadius = 6;

    // 入力ポート
    ports.inputs.forEach((port, index) => {
        const portCount = ports.inputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

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

        // ポートのドロップターゲット（マウス）
        circle.addEventListener('mouseup', (e) => {
            if (isDraggingConnection && dragConnectionFrom) {
                const fromNode = dragConnectionFrom.nodeId;
                const fromPort = dragConnectionFrom.portId;
                addConnection(fromNode, fromPort, node.id, port.id);
                stopDraggingConnection();
            }
        });

        // ポートのドロップターゲット（タッチ）
        circle.addEventListener('touchend', (e) => {
            if (isDraggingConnection && dragConnectionFrom) {
                const fromNode = dragConnectionFrom.nodeId;
                const fromPort = dragConnectionFrom.portId;
                addConnection(fromNode, fromPort, node.id, port.id);
                stopDraggingConnection();
                e.preventDefault();
            }
        });

        nodeGraphSvg.appendChild(circle);
    });

    // 出力ポート
    ports.outputs.forEach((port, index) => {
        const portCount = ports.outputs.length;
        const spacing = nodeHeight / (portCount + 1);
        const y = node.posY + spacing * (index + 1);

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

        // ポートからドラッグ開始（マウス）
        circle.addEventListener('mousedown', (e) => {
            e.stopPropagation();
            startDraggingConnection(node.id, port.id, e.clientX, e.clientY);
        });

        // ポートからドラッグ開始（タッチ）
        circle.addEventListener('touchstart', (e) => {
            e.stopPropagation();
            if (e.touches && e.touches[0]) {
                startDraggingConnection(node.id, port.id, e.touches[0].clientX, e.touches[0].clientY);
                e.preventDefault();
            }
        }, { passive: false });

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
            // ドラッグ終了時にグラフ全体を再描画して整合性を保つ
            renderNodeGraph();
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

    // マウスとタッチの両方のイベントを登録
    nodeBox.addEventListener('mousedown', handleStart);
    nodeBox.addEventListener('touchstart', handleStart, { passive: false });
}

// ノードのポート位置を更新
function updateNodePortsPosition(node) {
    const nodeWidth = 160;
    const nodeHeight = 80;
    const ports = getNodePorts(node);

    // 入力ポートを更新
    ports.inputs.forEach((port, index) => {
        const portElement = nodeGraphSvg.querySelector(
            `circle.node-port-input[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (portElement) {
            const portCount = ports.inputs.length;
            const spacing = nodeHeight / (portCount + 1);
            const y = node.posY + spacing * (index + 1);
            portElement.setAttribute('cx', node.posX);
            portElement.setAttribute('cy', y);
        }
    });

    // 出力ポートを更新
    ports.outputs.forEach((port, index) => {
        const portElement = nodeGraphSvg.querySelector(
            `circle.node-port-output[data-node-id="${node.id}"][data-port-id="${port.id}"]`
        );
        if (portElement) {
            const portCount = ports.outputs.length;
            const spacing = nodeHeight / (portCount + 1);
            const y = node.posY + spacing * (index + 1);
            portElement.setAttribute('cx', node.posX + nodeWidth);
            portElement.setAttribute('cy', y);
        }
    });
}

// 特定ノードに関連する接続線を更新
function updateConnectionsForNode(nodeId) {
    // 接続線のみを再描画（効率的な更新）
    const paths = nodeGraphSvg.querySelectorAll('path.node-connection');
    paths.forEach(path => path.remove());
    drawAllConnections();
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
        posX: 300,
        posY: 50 + existingCompositeCount * 150,  // 既存のノードと重ならないように配置
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
        posX: 200,
        posY: 50 + existingFilterCount * 120  // 既存のノードと重ならないように配置
    };

    globalNodes.push(filterNode);
    renderNodeGraph();
}

// フィルタ表示名を取得
function getFilterDisplayName(filterType) {
    const names = {
        'grayscale': 'グレースケール',
        'brightness': '明るさ',
        'blur': 'ぼかし'
    };
    return names[filterType] || filterType;
}

// ノード間のグラフを処理して画像を生成（16bit Premultiplied Alpha版）
function evaluateNodeGraph(nodeId, visited = new Set()) {
    // 循環参照チェック
    if (visited.has(nodeId)) {
        console.warn('Circular reference detected at node:', nodeId);
        return null;
    }
    visited.add(nodeId);

    const node = globalNodes.find(n => n.id === nodeId);
    if (!node) return null;

    // ノードタイプごとに処理
    switch (node.type) {
        case 'image': {
            // 画像ノード: レイヤーから画像データを取得して16bit premultipliedに変換
            const nodeStart = performance.now();
            const layer = layers.find(l => l.id === node.layerId);
            if (!layer || !layer.imageData) return null;

            // 8bit → 16bit premultiplied変換
            const convertStart = performance.now();
            let result = processor.toPremultiplied(layer.imageData);
            console.log(`[Node:${node.id}] Convert to 16bit: ${(performance.now() - convertStart).toFixed(1)}ms`);

            // アフィン変換が必要かチェック
            const p = layer.params;
            const needsTransform = p && (
                p.translateX !== 0 || p.translateY !== 0 || p.rotation !== 0 ||
                p.scaleX !== 1.0 || p.scaleY !== 1.0 || p.alpha !== 1.0
            );

            if (needsTransform) {
                // 行列ベースアフィン変換（16bit版、固定小数点演算）
                const transformStart = performance.now();
                const centerX = canvasWidth / 2.0;
                const centerY = canvasHeight / 2.0;

                const matrix = processor.createAffineMatrix(
                    p.translateX, p.translateY, p.rotation,
                    p.scaleX, p.scaleY, centerX, centerY
                );

                result = processor.applyTransformToImage16(
                    result, matrix.a, matrix.b, matrix.c, matrix.d,
                    matrix.tx, matrix.ty, p.alpha
                );
                console.log(`[Node:${node.id}] Transform: ${(performance.now() - transformStart).toFixed(1)}ms`);
            }

            console.log(`[Node:${node.id}] Image total: ${(performance.now() - nodeStart).toFixed(1)}ms`);
            return result;
        }

        case 'filter': {
            // フィルタノード: 入力画像にフィルタを適用（16bit版）
            const nodeStart = performance.now();
            const inputConn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in'
            );

            if (!inputConn) return null;

            const inputImage = evaluateNodeGraph(inputConn.fromNodeId, visited);
            if (!inputImage) return null;

            // フィルタ適用
            let filterType, filterParam;

            if (node.independent) {
                // 独立フィルタノード
                filterType = node.filterType;
                filterParam = node.param;
            } else {
                // レイヤー付帯フィルタノード
                const layer = layers.find(l => l.id === node.layerId);
                if (!layer || !layer.filters || !layer.filters[node.filterIndex]) return inputImage;

                const filter = layer.filters[node.filterIndex];
                filterType = filter.type;
                filterParam = filter.param;
            }

            const filterStart = performance.now();
            const result = processor.applyFilterToImage16(inputImage, filterType, filterParam);
            console.log(`[Node:${node.id}] Filter(${filterType}): ${(performance.now() - filterStart).toFixed(1)}ms`);
            return result;
        }

        case 'composite': {
            // 合成ノード: 複数入力を合成（16bit premultiplied版、超高速）
            const nodeStart = performance.now();
            const input1Conn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in1'
            );
            const input2Conn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in2'
            );

            const images = [];

            if (input1Conn) {
                const img1 = evaluateNodeGraph(input1Conn.fromNodeId, visited);
                if (img1) {
                    // alpha1をプリマルチプライド形式で適用
                    const alpha1 = node.alpha1 || 1.0;
                    if (alpha1 !== 1.0) {
                        const alphaU16 = Math.floor(alpha1 * 65535);
                        const scaledImg = {
                            data: new Uint16Array(img1.data.length),
                            width: img1.width,
                            height: img1.height
                        };
                        for (let i = 0; i < img1.data.length; i++) {
                            scaledImg.data[i] = (img1.data[i] * alphaU16) >> 16;
                        }
                        images.push(scaledImg);
                    } else {
                        images.push(img1);
                    }
                }
            }

            if (input2Conn) {
                const img2 = evaluateNodeGraph(input2Conn.fromNodeId, visited);
                if (img2) {
                    const alpha2 = node.alpha2 || 1.0;
                    if (alpha2 !== 1.0) {
                        const alphaU16 = Math.floor(alpha2 * 65535);
                        const scaledImg = {
                            data: new Uint16Array(img2.data.length),
                            width: img2.width,
                            height: img2.height
                        };
                        for (let i = 0; i < img2.data.length; i++) {
                            scaledImg.data[i] = (img2.data[i] * alphaU16) >> 16;
                        }
                        images.push(scaledImg);
                    } else {
                        images.push(img2);
                    }
                }
            }

            if (images.length === 0) return null;

            let result;
            if (images.length === 1) {
                result = images[0];
            } else {
                // 16bit premultiplied合成（除算なし、超高速）
                const mergeStart = performance.now();
                result = processor.mergeImages16(images);
                console.log(`[Node:${node.id}] Merge: ${(performance.now() - mergeStart).toFixed(1)}ms`);
            }

            // 合成ノードのアフィン変換が必要かチェック
            if (node.affineParams) {
                const p = node.affineParams;
                const needsTransform = (
                    p.translateX !== 0 || p.translateY !== 0 || p.rotation !== 0 ||
                    p.scaleX !== 1.0 || p.scaleY !== 1.0 || p.alpha !== 1.0
                );

                if (needsTransform) {
                    const transformStart = performance.now();
                    const centerX = canvasWidth / 2.0;
                    const centerY = canvasHeight / 2.0;

                    const matrix = processor.createAffineMatrix(
                        p.translateX, p.translateY, p.rotation,
                        p.scaleX, p.scaleY, centerX, centerY
                    );

                    result = processor.applyTransformToImage16(
                        result, matrix.a, matrix.b, matrix.c, matrix.d,
                        matrix.tx, matrix.ty, p.alpha
                    );
                    console.log(`[Node:${node.id}] Transform: ${(performance.now() - transformStart).toFixed(1)}ms`);
                }
            }

            console.log(`[Node:${node.id}] Composite total: ${(performance.now() - nodeStart).toFixed(1)}ms`);
            return result;
        }

        default:
            return null;
    }
}

// グラフベースのプレビュー更新（C++側で完結）
function updatePreviewFromGraph() {
    const perfStart = performance.now();

    const outputNode = globalNodes.find(n => n.type === 'output');
    if (!outputNode) {
        // 出力ノードがない場合は従来の方法で合成
        updatePreview();
        return;
    }

    // レイヤーの変更を反映（重要！）
    syncNodesFromLayers();

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
