// グローバル変数
let processor;
let canvas;
let ctx;
let layers = [];
let canvasWidth = 800;
let canvasHeight = 600;

// WebAssemblyモジュールが読み込まれた時の初期化
// Emscriptenが生成するModuleオブジェクトに設定を追加
var Module = Module || {};
Module.onRuntimeInitialized = function() {
    console.log('WebAssembly loaded successfully');
    initializeApp();
};

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
    if (typeof Module !== 'undefined' && Module.ImageProcessor) {
        processor = new Module.ImageProcessor(canvasWidth, canvasHeight);
        console.log('Using WebAssembly backend');
    } else {
        console.error('WebAssembly module not loaded!');
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
