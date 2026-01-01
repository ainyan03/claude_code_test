// グローバル変数
let processor;
let canvas;
let ctx;
let layers = [];
let canvasWidth = 800;
let canvasHeight = 600;

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

        // レイヤー情報を保存
        const layer = {
            id: layerId,
            name: imageData.name,
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

    // ノードエディタを初期化
    initializeNodeEditor(layerDiv, layer);

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

    // ノードエディタを更新
    updateNodeEditor(layerDiv, layer);
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
// ノードエディタ
// ========================================

function initializeNodeEditor(layerDiv, layer) {
    const toggleBtn = layerDiv.querySelector('.node-editor-toggle');
    const container = layerDiv.querySelector('.node-editor-canvas-container');

    // トグル機能
    toggleBtn.addEventListener('click', () => {
        container.classList.toggle('hidden');
        toggleBtn.classList.toggle('collapsed');
    });

    // 初期状態は表示
    updateNodeEditor(layerDiv, layer);
}

function updateNodeEditor(layerDiv, layer) {
    const svg = layerDiv.querySelector('.node-editor-canvas');
    if (!svg) return;

    // SVGをクリア
    svg.innerHTML = '';

    if (!layer.filters || layer.filters.length === 0) {
        return;
    }

    // 接続線を描画
    drawConnections(svg, layer);

    // ノードを描画
    layer.filters.forEach((filter, index) => {
        drawNode(svg, layer, filter, index);
    });
}

function drawConnections(svg, layer) {
    if (!layer.filters || layer.filters.length < 2) return;

    const ns = 'http://www.w3.org/2000/svg';

    for (let i = 0; i < layer.filters.length - 1; i++) {
        const filter1 = layer.filters[i];
        const filter2 = layer.filters[i + 1];

        // ノードの右端から次のノードの左端へ接続
        const x1 = filter1.posX + 120; // ノード幅100 + padding
        const y1 = filter1.posY + 20;  // ノード高さの中央
        const x2 = filter2.posX;
        const y2 = filter2.posY + 20;

        // ベジェ曲線でパスを描画
        const path = document.createElementNS(ns, 'path');
        const midX = (x1 + x2) / 2;
        const d = `M ${x1} ${y1} C ${midX} ${y1}, ${midX} ${y2}, ${x2} ${y2}`;
        path.setAttribute('d', d);
        path.setAttribute('class', 'node-connection');
        svg.appendChild(path);
    }
}

function drawNode(svg, layer, filter, filterIndex) {
    const ns = 'http://www.w3.org/2000/svg';

    // foreignObjectを使用してHTMLノードを埋め込む
    const foreignObject = document.createElementNS(ns, 'foreignObject');
    foreignObject.setAttribute('x', filter.posX);
    foreignObject.setAttribute('y', filter.posY);
    foreignObject.setAttribute('width', 120);
    foreignObject.setAttribute('height', 60);

    const nodeBox = document.createElement('div');
    nodeBox.className = 'node-box';
    nodeBox.dataset.layerId = layer.id;
    nodeBox.dataset.filterIndex = filterIndex;

    const header = document.createElement('div');
    header.className = 'node-box-header';

    const title = document.createElement('span');
    title.className = 'node-box-title';
    title.textContent = getFilterDisplayName(filter.type);

    const idBadge = document.createElement('span');
    idBadge.className = 'node-box-id';
    idBadge.textContent = `#${filter.nodeId}`;

    header.appendChild(title);
    header.appendChild(idBadge);

    const content = document.createElement('div');
    content.className = 'node-box-content';
    content.textContent = `(${filter.posX.toFixed(0)}, ${filter.posY.toFixed(0)})`;

    nodeBox.appendChild(header);
    nodeBox.appendChild(content);

    foreignObject.appendChild(nodeBox);
    svg.appendChild(foreignObject);

    // ドラッグ機能を追加
    setupNodeDrag(nodeBox, foreignObject, layer, filterIndex);
}

function setupNodeDrag(nodeBox, foreignObject, layer, filterIndex) {
    let isDragging = false;
    let startX, startY;
    let initialX, initialY;

    nodeBox.addEventListener('mousedown', (e) => {
        isDragging = true;
        nodeBox.classList.add('dragging');

        const rect = foreignObject.getBoundingClientRect();
        const svgRect = foreignObject.ownerSVGElement.getBoundingClientRect();

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

        // 座標表示を更新
        const content = nodeBox.querySelector('.node-box-content');
        content.textContent = `(${newX.toFixed(0)}, ${newY.toFixed(0)})`;

        // レイヤー情報を更新
        layer.filters[filterIndex].posX = newX;
        layer.filters[filterIndex].posY = newY;

        // C++側に同期
        processor.setFilterNodePosition(layer.id, filterIndex, newX, newY);

        // 接続線を再描画
        const layerDiv = document.querySelector(`.layer-item[data-layer-id="${layer.id}"]`);
        if (layerDiv) {
            updateNodeEditor(layerDiv, layer);
        }
    });

    document.addEventListener('mouseup', () => {
        if (isDragging) {
            isDragging = false;
            nodeBox.classList.remove('dragging');
        }
    });
}
