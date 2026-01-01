// グローバル変数
let processor;
let canvas;
let ctx;
let layers = [];
let canvasWidth = 800;
let canvasHeight = 600;

// グローバルノードグラフ
let globalNodes = [];  // すべてのノード（画像、フィルタ、合成、出力）を管理
let globalConnections = [];  // ノード間の接続
let nextGlobalNodeId = 1;
let nextCompositeId = 1;
let nodeGraphSvg = null;

// ドラッグ接続用の状態
let isDraggingConnection = false;
let dragConnectionFrom = null;
let dragConnectionPath = null;

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

    // イベントリスナー設定
    setupEventListeners();

    // グローバルノードグラフ初期化
    initializeNodeGraph();

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
            addLayer(imageData);
        } catch (error) {
            console.error('Failed to load image:', error);
            const errorMsg = error.message || error.toString();
            alert('画像の読み込みに失敗しました\nファイル名: ' + file.name + '\nエラー: ' + errorMsg);
        }
    }

    // inputをリセット（同じファイルを再度選択可能にする）
    event.target.value = '';
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

        // UIにレイヤー追加
        createLayerUI(layer);

        // グローバルノードグラフを更新
        renderNodeGraph();

        // プレビュー更新
        updatePreview();

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
        updatePreview();
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

    // フィルタ追加ボタン
    const filterAddButtons = layerDiv.querySelectorAll('.filter-add-btn');
    filterAddButtons.forEach(btn => {
        btn.addEventListener('click', () => {
            const filterType = btn.dataset.filter;
            addFilterToLayer(layer, filterType);
        });
    });

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

        // C++側にパラメータ更新
        const p = layer.params;
        const rotation = transformFn && paramName === 'rotation' ? transformFn(value) : p.rotation * Math.PI / 180;

        // デバッグログ
        console.log(`[${paramName}] Layer ${layer.id}: tx=${p.translateX}, ty=${p.translateY}, rot=${rotation}, sx=${p.scaleX}, sy=${p.scaleY}, alpha=${p.alpha}`);

        processor.setLayerTransform(
            layer.id,
            p.translateX,
            p.translateY,
            rotation,
            p.scaleX,
            p.scaleY,
            p.alpha
        );

        updatePreview();
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
        updatePreview();
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
        updatePreview();
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
        updatePreview();
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
    updatePreview();
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
// フィルタ管理
// ========================================

function addFilterToLayer(layer, filterType) {
    // デフォルトパラメータ
    let defaultParam = 0.0;
    if (filterType === 'brightness') {
        defaultParam = 0.0;  // -1.0 ~ 1.0
    } else if (filterType === 'blur') {
        defaultParam = 3.0;  // radius
    }

    // C++側にフィルタ追加
    processor.addFilter(layer.id, filterType, defaultParam);

    // レイヤーにフィルタ情報を追加
    if (!layer.filters) {
        layer.filters = [];
    }
    const filterIndex = layer.filters.length;

    // C++から割り当てられたノード情報を取得
    const nodeId = processor.getFilterNodeId(layer.id, filterIndex);
    const nodePosX = processor.getFilterNodePosX(layer.id, filterIndex);
    const nodePosY = processor.getFilterNodePosY(layer.id, filterIndex);

    layer.filters.push({
        type: filterType,
        param: defaultParam,
        nodeId: nodeId,
        posX: nodePosX,
        posY: nodePosY
    });

    // UIを更新
    updateLayerFiltersUI(layer);

    // グローバルノードグラフを更新
    renderNodeGraph();

    // プレビュー更新
    updatePreview();
}

function updateLayerFiltersUI(layer) {
    const layerDiv = document.querySelector(`.layer-item[data-layer-id="${layer.id}"]`);
    if (!layerDiv) return;

    const filtersList = layerDiv.querySelector('.filters-list');
    filtersList.innerHTML = '';

    if (!layer.filters) return;

    layer.filters.forEach((filter, index) => {
        const filterItem = document.createElement('div');
        filterItem.className = 'filter-item';

        const header = document.createElement('div');
        header.className = 'filter-item-header';

        const filterName = document.createElement('span');
        filterName.className = 'filter-name';
        filterName.textContent = getFilterDisplayName(filter.type);

        // ノードIDバッジを追加
        if (filter.nodeId) {
            const nodeBadge = document.createElement('span');
            nodeBadge.className = 'node-id-badge';
            nodeBadge.textContent = `#${filter.nodeId}`;
            nodeBadge.title = `Node ID: ${filter.nodeId}, Position: (${filter.posX.toFixed(0)}, ${filter.posY.toFixed(0)})`;
            filterName.appendChild(nodeBadge);
        }

        const removeBtn = document.createElement('button');
        removeBtn.className = 'filter-remove-btn';
        removeBtn.textContent = '×';
        removeBtn.addEventListener('click', () => removeFilterFromLayer(layer, index));

        header.appendChild(filterName);
        header.appendChild(removeBtn);
        filterItem.appendChild(header);

        // パラメータ付きフィルタの場合、スライダーを追加
        if (filter.type === 'brightness') {
            const paramDiv = createFilterParamSlider(layer, index, 'brightness', -1.0, 1.0,
                (val) => (val >= 0 ? '+' : '') + val.toFixed(2),
                filter.param);
            filterItem.appendChild(paramDiv);
        } else if (filter.type === 'blur') {
            const paramDiv = createFilterParamSlider(layer, index, 'blur', 1, 10,
                (val) => Math.round(val) + 'px',
                filter.param);
            filterItem.appendChild(paramDiv);
        }

        filtersList.appendChild(filterItem);
    });
}

function createFilterParamSlider(layer, filterIndex, filterType, min, max, displayFn, initialValue) {
    const paramDiv = document.createElement('div');
    paramDiv.className = 'filter-param';

    const label = document.createElement('label');
    const slider = document.createElement('input');
    slider.type = 'range';
    slider.min = min;
    slider.max = max;
    slider.step = (max - min) > 10 ? 1 : 0.01;
    slider.value = initialValue;

    const valueSpan = document.createElement('span');
    valueSpan.className = 'param-value';
    valueSpan.textContent = displayFn(initialValue);

    slider.addEventListener('input', (e) => {
        const value = parseFloat(e.target.value);
        layer.filters[filterIndex].param = value;
        valueSpan.textContent = displayFn(value);

        // C++側のフィルタを更新（削除して再追加）
        rebuildLayerFilters(layer);
        updatePreview();
    });

    label.appendChild(slider);
    label.appendChild(valueSpan);
    paramDiv.appendChild(label);

    return paramDiv;
}

function removeFilterFromLayer(layer, filterIndex) {
    // 配列から削除
    layer.filters.splice(filterIndex, 1);

    // C++側のフィルタを再構築
    rebuildLayerFilters(layer);

    // UI更新
    updateLayerFiltersUI(layer);

    // グローバルノードグラフを更新
    renderNodeGraph();

    // プレビュー更新
    updatePreview();
}

function rebuildLayerFilters(layer) {
    // C++側のフィルタをクリアして再追加
    processor.clearFilters(layer.id);

    if (layer.filters) {
        layer.filters.forEach(filter => {
            processor.addFilter(layer.id, filter.type, filter.param);
        });
    }
}

function getFilterDisplayName(filterType) {
    const names = {
        'grayscale': 'グレースケール',
        'brightness': '明るさ',
        'blur': 'ぼかし'
    };
    return names[filterType] || filterType;
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

    // 接続ドラッグのマウス移動
    document.addEventListener('mousemove', (e) => {
        if (isDraggingConnection) {
            updateDragConnectionPath(e.clientX, e.clientY);
        }
    });

    // 接続ドラッグのキャンセル（空白エリアでリリース）
    nodeGraphSvg.addEventListener('mouseup', (e) => {
        if (isDraggingConnection && e.target === nodeGraphSvg) {
            stopDraggingConnection();
        }
    });

    console.log('Node graph initialized');
}

function renderNodeGraph() {
    if (!nodeGraphSvg) return;

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
            posX: 700,
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

    // 合成ノードと出力ノードは保持
    globalNodes.forEach(node => {
        if (node.type === 'composite' || node.type === 'output') {
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

    // 合成ノードの場合、アルファスライダーを追加
    if (node.type === 'composite') {
        const controls = document.createElement('div');
        controls.className = 'node-box-controls';
        controls.innerHTML = `
            <label style="font-size: 10px; display: block; margin: 2px 0;">
                α1: <input type="range" class="alpha1-slider" min="0" max="1" step="0.01" value="${node.alpha1 || 1.0}" style="width: 80px;">
            </label>
            <label style="font-size: 10px; display: block; margin: 2px 0;">
                α2: <input type="range" class="alpha2-slider" min="0" max="1" step="0.01" value="${node.alpha2 || 1.0}" style="width: 80px;">
            </label>
        `;

        const alpha1Slider = controls.querySelector('.alpha1-slider');
        const alpha2Slider = controls.querySelector('.alpha2-slider');

        alpha1Slider.addEventListener('input', (e) => {
            node.alpha1 = parseFloat(e.target.value);
            updatePreviewFromGraph();
        });

        alpha2Slider.addEventListener('input', (e) => {
            node.alpha2 = parseFloat(e.target.value);
            updatePreviewFromGraph();
        });

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

        // ポートのドロップターゲット
        circle.addEventListener('mouseup', (e) => {
            if (isDraggingConnection && dragConnectionFrom) {
                const fromNode = dragConnectionFrom.nodeId;
                const fromPort = dragConnectionFrom.portId;
                addConnection(fromNode, fromPort, node.id, port.id);
                stopDraggingConnection();
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

        // ポートからドラッグ開始
        circle.addEventListener('mousedown', (e) => {
            e.stopPropagation();
            startDraggingConnection(node.id, port.id, e.clientX, e.clientY);
        });

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

    nodeBox.addEventListener('mousedown', (e) => {
        isDragging = true;
        nodeBox.classList.add('dragging');

        startX = e.clientX;
        startY = e.clientY;
        initialX = parseFloat(foreignObject.getAttribute('x'));
        initialY = parseFloat(foreignObject.getAttribute('y'));

        e.preventDefault();
    });

    document.addEventListener('mousemove', (e) => {
        if (!isDragging) return;

        const dx = e.clientX - startX;
        const dy = e.clientY - startY;

        const newX = Math.max(0, initialX + dx);
        const newY = Math.max(0, initialY + dy);

        foreignObject.setAttribute('x', newX);
        foreignObject.setAttribute('y', newY);

        const content = nodeBox.querySelector('.node-box-content');
        content.textContent = `(${newX.toFixed(0)}, ${newY.toFixed(0)})`;

        node.posX = newX;
        node.posY = newY;

        // フィルタノードの場合、C++側に同期
        if (node.type === 'filter') {
            const layer = layers.find(l => l.id === node.layerId);
            if (layer && layer.filters[node.filterIndex]) {
                layer.filters[node.filterIndex].posX = newX - 200;  // オフセット調整
                layer.filters[node.filterIndex].posY = newY - layers.findIndex(l => l.id === node.layerId) * 150;
                processor.setFilterNodePosition(node.layerId, node.filterIndex,
                    layer.filters[node.filterIndex].posX,
                    layer.filters[node.filterIndex].posY);
            }
        }

        renderNodeGraph();
    });

    document.addEventListener('mouseup', () => {
        if (isDragging) {
            isDragging = false;
            nodeBox.classList.remove('dragging');
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
            // 合成ノード: 入力2つ、出力1つ
            ports.inputs.push({ id: 'in1', label: '入力1', type: 'image' });
            ports.inputs.push({ id: 'in2', label: '入力2', type: 'image' });
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
    const compositeNode = {
        id: `composite-${nextCompositeId++}`,
        type: 'composite',
        title: '合成',
        posX: 300,
        posY: 150,
        alpha1: 1.0,  // 入力1のアルファ値
        alpha2: 1.0   // 入力2のアルファ値
    };

    globalNodes.push(compositeNode);
    renderNodeGraph();
}

// ノード間のグラフを処理して画像を生成
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
            // 画像ノード: レイヤーから画像データを取得
            const layer = layers.find(l => l.id === node.layerId);
            if (!layer || !layer.imageData) return null;

            // 画像データを返す（変換なし）
            return {
                data: new Uint8ClampedArray(layer.imageData.data),
                width: layer.imageData.width,
                height: layer.imageData.height
            };
        }

        case 'filter': {
            // フィルタノード: 入力画像にフィルタを適用
            const inputConn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in'
            );

            if (!inputConn) return null;

            const inputImage = evaluateNodeGraph(inputConn.fromNodeId, visited);
            if (!inputImage) return null;

            // フィルタ適用
            const layer = layers.find(l => l.id === node.layerId);
            if (!layer || !layer.filters || !layer.filters[node.filterIndex]) return inputImage;

            const filter = layer.filters[node.filterIndex];
            return processor.applyFilterToImage(inputImage, filter.type, filter.param);
        }

        case 'composite': {
            // 合成ノード: 複数入力を合成
            const input1Conn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in1'
            );
            const input2Conn = globalConnections.find(
                c => c.toNodeId === nodeId && c.toPortId === 'in2'
            );

            const images = [];
            const alphas = [];

            if (input1Conn) {
                const img1 = evaluateNodeGraph(input1Conn.fromNodeId, visited);
                if (img1) {
                    images.push(img1);
                    alphas.push(node.alpha1 || 1.0);
                }
            }

            if (input2Conn) {
                const img2 = evaluateNodeGraph(input2Conn.fromNodeId, visited);
                if (img2) {
                    images.push(img2);
                    alphas.push(node.alpha2 || 1.0);
                }
            }

            if (images.length === 0) return null;
            if (images.length === 1) return images[0];

            // C++の mergeImages を使用
            return processor.mergeImages(images, alphas);
        }

        default:
            return null;
    }
}

// グラフベースのプレビュー更新
function updatePreviewFromGraph() {
    const outputNode = globalNodes.find(n => n.type === 'output');
    if (!outputNode) {
        // 出力ノードがない場合は従来の方法で合成
        updatePreview();
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

    // グラフを評価して最終画像を取得
    const resultImage = evaluateNodeGraph(inputConn.fromNodeId);

    if (resultImage && resultImage.data) {
        // キャンバスに描画
        const imageData = new ImageData(
            resultImage.data,
            resultImage.width,
            resultImage.height
        );

        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
        ctx.putImageData(imageData, 0, 0);
    } else {
        ctx.clearRect(0, 0, canvasWidth, canvasHeight);
    }
}
