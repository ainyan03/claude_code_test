// ========================================
// UI共通ヘルパー関数
// ========================================

// アフィン変換パラメータから行列を計算
// 注: 中心補正はC++側(node_graph.cpp)でsrcOriginを基準に行うため、
//     ここでは基本行列のみを計算する
export function calculateMatrixFromParams(translateX, translateY, rotation, scaleX, scaleY) {
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

// ノードグラフ用X,Yスライダーを作成（配置位置用、translateX/translateYを使用）
// options: { node, property, label, min, max, step, container, updatePreview }
// property: 'translateX' または 'translateY'
export function createNodeGraphPositionSlider(options) {
    const { node, property, label, min = -500, max = 500, step = 0.1, container, updatePreview } = options;

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
    const currentValue = node[property] ?? 0;
    slider.value = String(currentValue);
    slider.style.cssText = 'flex: 1; min-width: 50px;';

    const display = document.createElement('span');
    display.style.cssText = 'min-width: 35px; text-align: right; font-size: 9px;';
    display.textContent = currentValue.toFixed(1);

    slider.addEventListener('input', (e) => {
        const value = parseFloat(e.target.value);
        node[property] = value;
        display.textContent = value.toFixed(1);
        // 行列のtx/tyも更新（a,b,c,dは保持）
        if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
        if (property === 'translateX') {
            node.matrix.tx = value;
        } else if (property === 'translateY') {
            node.matrix.ty = value;
        }
        if (updatePreview) updatePreview();
    });

    row.appendChild(labelSpan);
    row.appendChild(slider);
    row.appendChild(display);
    container.appendChild(row);

    return { row, slider, display };
}

// 詳細ダイアログ用スライダー行を作成
// options: { label, min, max, step, value, onChange, unit }
export function createDetailSliderRow(options) {
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

// タブコンテナを作成する共通関数
// options: { tabs: [{ id, label, buildContent(container) }], defaultTab, container }
export function createTabContainer(options) {
    const { tabs, defaultTab, container, node } = options;

    const wrapper = document.createElement('div');
    wrapper.className = 'tab-container';

    // タブヘッダー
    const tabHeader = document.createElement('div');
    tabHeader.className = 'tab-header';
    tabHeader.style.cssText = 'display: flex; gap: 4px; margin-bottom: 8px;';

    // タブコンテンツ
    const tabContent = document.createElement('div');
    tabContent.className = 'tab-content';

    // 選択状態を保持するためのキー（nodeに保存）
    const tabStateKey = '_selectedTab';
    const currentTab = node?.[tabStateKey] || defaultTab || tabs[0]?.id;

    // タブボタン生成（パラメータ/行列ボタンと同じスタイル）
    const selectedStyle = 'flex: 1; padding: 6px 12px; font-size: 11px; border: 1px solid #4CAF50; border-radius: 3px; cursor: pointer; outline: none; background: #4CAF50; color: white;';
    const unselectedStyle = 'flex: 1; padding: 6px 12px; font-size: 11px; border: 1px solid #555; border-radius: 3px; cursor: pointer; outline: none; background: #333; color: #ccc;';

    tabs.forEach(tab => {
        const btn = document.createElement('button');
        btn.className = 'tab-button';
        btn.textContent = tab.label;
        btn.dataset.tabId = tab.id;
        btn.style.cssText = (tab.id === currentTab) ? selectedStyle : unselectedStyle;

        btn.addEventListener('click', () => {
            // 全タブのスタイルをリセット
            tabHeader.querySelectorAll('.tab-button').forEach(b => {
                b.style.cssText = unselectedStyle;
            });
            // 選択タブのスタイル
            btn.style.cssText = selectedStyle;

            // コンテンツ切り替え
            tabContent.innerHTML = '';
            tab.buildContent(tabContent);

            // 選択状態を保存
            if (node) {
                node[tabStateKey] = tab.id;
            }
        });

        tabHeader.appendChild(btn);
    });

    wrapper.appendChild(tabHeader);
    wrapper.appendChild(tabContent);

    // 初期タブのコンテンツを構築
    const initialTab = tabs.find(t => t.id === currentTab) || tabs[0];
    if (initialTab) {
        initialTab.buildContent(tabContent);
    }

    container.appendChild(wrapper);

    return { wrapper, tabHeader, tabContent };
}

// 9点セレクタ + ピクセル座標入力のセクションを作成（汎用）
// options: { node, container, onChange, width, height, sizeLabel }
// - width/height: 基準となるサイズ（SourceNodeは画像サイズ、Renderer/Sinkは仮想スクリーン/出力サイズ）
// - sizeLabel: サイズ表示のラベル（省略可）
export function createPivotSection(options) {
    const { node, container, onChange, width, height, sizeLabel } = options;

    // 初期値：未設定の場合は中央
    if (node.pivotX === undefined) node.pivotX = width / 2;
    if (node.pivotY === undefined) node.pivotY = height / 2;

    const section = document.createElement('div');
    section.className = 'node-detail-section';

    const label = document.createElement('div');
    label.className = 'node-detail-label';
    label.textContent = 'pivot';
    section.appendChild(label);

    // 横並びコンテナ（9点セレクタ左、XY入力右）
    const row = document.createElement('div');
    row.style.cssText = 'display: flex; align-items: flex-start; gap: 12px;';

    // 9点セレクタ（クイック選択）
    const pivotGrid = document.createElement('div');
    pivotGrid.className = 'node-origin-grid';
    pivotGrid.style.cssText = 'width: 54px; height: 54px; flex-shrink: 0;';

    // 9点の正規化座標（0, 0.5, 1）
    const pivotRatios = [
        { rx: 0, ry: 0 }, { rx: 0.5, ry: 0 }, { rx: 1, ry: 0 },
        { rx: 0, ry: 0.5 }, { rx: 0.5, ry: 0.5 }, { rx: 1, ry: 0.5 },
        { rx: 0, ry: 1 }, { rx: 0.5, ry: 1 }, { rx: 1, ry: 1 }
    ];

    let inputX, inputY;

    const updateInputs = (x, y) => {
        if (inputX) inputX.value = x;
        if (inputY) inputY.value = y;
    };

    // 現在のサイズを取得（RendererNodeは virtualWidth/Height を持つ）
    const getCurrentSize = () => {
        const w = node.virtualWidth ?? width;
        const h = node.virtualHeight ?? height;
        return { w, h };
    };

    // 現在のpivotが9点のいずれかに一致するか判定
    const updateGridSelection = (px, py) => {
        const { w, h } = getCurrentSize();
        pivotGrid.querySelectorAll('.node-origin-point').forEach(btn => {
            const rx = parseFloat(btn.dataset.rx);
            const ry = parseFloat(btn.dataset.ry);
            const expectedX = rx * w;
            const expectedY = ry * h;
            // 0.5の誤差を許容
            const match = Math.abs(px - expectedX) < 0.5 && Math.abs(py - expectedY) < 0.5;
            btn.classList.toggle('selected', match);
        });
    };

    pivotRatios.forEach(({ rx, ry }) => {
        const btn = document.createElement('button');
        btn.className = 'node-origin-point';
        btn.dataset.rx = String(rx);
        btn.dataset.ry = String(ry);
        // 初期選択状態
        const expectedX = rx * width;
        const expectedY = ry * height;
        if (Math.abs(node.pivotX - expectedX) < 0.5 && Math.abs(node.pivotY - expectedY) < 0.5) {
            btn.classList.add('selected');
        }
        btn.addEventListener('click', () => {
            // 現在のサイズを取得（変更後の値を使用）
            const { w, h } = getCurrentSize();
            node.pivotX = rx * w;
            node.pivotY = ry * h;
            updateGridSelection(node.pivotX, node.pivotY);
            updateInputs(node.pivotX, node.pivotY);
            if (onChange) onChange();
        });
        pivotGrid.appendChild(btn);
    });
    row.appendChild(pivotGrid);

    // XY入力エリア
    const inputArea = document.createElement('div');
    inputArea.style.cssText = 'display: flex; flex-direction: column; gap: 4px; flex: 1;';

    // X ピクセル座標入力
    const xRow = document.createElement('div');
    xRow.style.cssText = 'display: flex; align-items: center; gap: 4px;';
    const xLabel = document.createElement('label');
    xLabel.textContent = 'X:';
    xLabel.style.cssText = 'min-width: 16px; font-size: 11px;';
    inputX = document.createElement('input');
    inputX.type = 'number';
    inputX.step = '0.5';
    inputX.value = node.pivotX;
    inputX.style.cssText = 'width: 60px; font-size: 11px;';

    inputX.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value) || 0;
        node.pivotX = val;
        updateGridSelection(val, node.pivotY);
        if (onChange) onChange();
    });

    xRow.appendChild(xLabel);
    xRow.appendChild(inputX);
    inputArea.appendChild(xRow);

    // Y ピクセル座標入力
    const yRow = document.createElement('div');
    yRow.style.cssText = 'display: flex; align-items: center; gap: 4px;';
    const yLabel = document.createElement('label');
    yLabel.textContent = 'Y:';
    yLabel.style.cssText = 'min-width: 16px; font-size: 11px;';
    inputY = document.createElement('input');
    inputY.type = 'number';
    inputY.step = '0.5';
    inputY.value = node.pivotY;
    inputY.style.cssText = 'width: 60px; font-size: 11px;';

    inputY.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value) || 0;
        node.pivotY = val;
        updateGridSelection(node.pivotX, val);
        if (onChange) onChange();
    });

    yRow.appendChild(yLabel);
    yRow.appendChild(inputY);
    inputArea.appendChild(yRow);

    // サイズ参考表示
    if (sizeLabel !== false) {
        const sizeInfo = document.createElement('div');
        sizeInfo.style.cssText = 'font-size: 9px; color: #666; margin-top: 2px;';
        sizeInfo.textContent = sizeLabel || `${width} × ${height}`;
        inputArea.appendChild(sizeInfo);
    }

    row.appendChild(inputArea);
    section.appendChild(row);

    container.appendChild(section);
    return { section, updateInputs, updateGridSelection };
}

// SourceNode用のpivotセクション（画像サイズ基準）
export function createOriginSection(options) {
    const { node, container, onChange, contentLibrary } = options;

    // コンテンツから画像サイズを取得
    const content = contentLibrary.find(c => c.id === node.contentId);
    const imgWidth = content ? content.width : 100;
    const imgHeight = content ? content.height : 100;

    return createPivotSection({
        node,
        container,
        onChange,
        width: imgWidth,
        height: imgHeight,
        sizeLabel: `画像: ${imgWidth} × ${imgHeight}`
    });
}

// アフィン変換コントロールセクションを作成（共通関数）
// options: {
//   node: ノードオブジェクト
//   container: 追加先コンテナ
//   onChange: 変更時コールバック
//   collapsed: true/false - 初期状態で折りたたむか（default: true）
//   showTranslation: true/false - 移動パラメータを表示するか（default: true）
//   label: セクションラベル（default: 'アフィン変換'）
// }
export function createAffineControlsSection(options) {
    const {
        node,
        container,
        onChange,
        collapsed = true,
        showTranslation = true,
        label = 'アフィン変換'
    } = options;

    // メインセクション
    const section = document.createElement('div');
    section.className = 'node-detail-section affine-controls-section';

    // 折りたたみヘッダー
    const header = document.createElement('div');
    header.className = 'affine-controls-header';
    header.style.cssText = 'display: flex; align-items: center; gap: 6px; cursor: pointer; padding: 4px 0; user-select: none;';

    const arrow = document.createElement('span');
    arrow.textContent = collapsed && !node.affineExpanded ? '▶' : '▼';
    arrow.style.cssText = 'font-size: 10px; transition: transform 0.2s;';

    const headerLabel = document.createElement('span');
    headerLabel.className = 'node-detail-label';
    headerLabel.textContent = label;
    headerLabel.style.cssText = 'margin: 0; flex: 1;';

    // 変換有無のインジケーター
    const indicator = document.createElement('span');
    indicator.style.cssText = 'font-size: 10px; color: #888;';
    updateAffineIndicator();

    function updateAffineIndicator() {
        const hasTransform = hasAffineTransform(node);
        indicator.textContent = hasTransform ? '●' : '';
        indicator.style.color = hasTransform ? '#4CAF50' : '#888';
    }

    header.appendChild(arrow);
    header.appendChild(headerLabel);
    header.appendChild(indicator);
    section.appendChild(header);

    // コンテンツ部分（折りたたみ可能）
    const content = document.createElement('div');
    content.className = 'affine-controls-content';
    content.style.cssText = 'padding-top: 8px;';

    // 初期状態
    if (collapsed && !node.affineExpanded) {
        content.style.display = 'none';
    }

    // 折りたたみトグル
    header.addEventListener('click', () => {
        const isExpanded = content.style.display !== 'none';
        content.style.display = isExpanded ? 'none' : 'block';
        arrow.textContent = isExpanded ? '▶' : '▼';
        node.affineExpanded = !isExpanded;
    });

    // モード切替ボタン（パラメータ/行列）
    const modeRow = document.createElement('div');
    modeRow.style.cssText = 'display: flex; gap: 4px; margin-bottom: 8px;';

    const paramBtn = document.createElement('button');
    paramBtn.textContent = 'パラメータ';
    paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; ${!node.matrixMode ? 'background: #4CAF50; color: white; border-color: #4CAF50;' : 'background: #333; color: #ccc;'}`;

    const matrixBtn = document.createElement('button');
    matrixBtn.textContent = '行列';
    matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; ${node.matrixMode ? 'background: #4CAF50; color: white; border-color: #4CAF50;' : 'background: #333; color: #ccc;'}`;

    modeRow.appendChild(paramBtn);
    modeRow.appendChild(matrixBtn);
    content.appendChild(modeRow);

    // パラメータコンテナ
    const paramsContainer = document.createElement('div');
    paramsContainer.className = 'affine-params-container';
    content.appendChild(paramsContainer);

    // パラメータモードUI構築
    function buildParamMode() {
        paramsContainer.innerHTML = '';

        const params = [];
        if (showTranslation) {
            params.push({ key: 'translateX', label: 'X移動', min: -500, max: 500, step: 0.1, default: 0, unit: '' });
            params.push({ key: 'translateY', label: 'Y移動', min: -500, max: 500, step: 0.1, default: 0, unit: '' });
        }
        params.push({ key: 'rotation', label: '回転', min: -180, max: 180, step: 0.1, default: 0, unit: '°' });
        params.push({ key: 'scaleX', label: 'X倍率', min: -5, max: 5, step: 0.01, default: 1, unit: '' });
        params.push({ key: 'scaleY', label: 'Y倍率', min: -5, max: 5, step: 0.01, default: 1, unit: '' });

        params.forEach(p => {
            const value = node[p.key] !== undefined ? node[p.key] : p.default;
            const result = createDetailSliderRow({
                label: p.label,
                min: p.min,
                max: p.max,
                step: p.step,
                value: value,
                unit: p.unit,
                onChange: (val) => {
                    node[p.key] = val;
                    // 行列を再計算
                    node.matrix = calculateMatrixFromParams(
                        node.translateX || 0,
                        node.translateY || 0,
                        node.rotation || 0,
                        node.scaleX !== undefined ? node.scaleX : 1,
                        node.scaleY !== undefined ? node.scaleY : 1
                    );
                    updateAffineIndicator();
                    if (onChange) onChange();
                }
            });
            paramsContainer.appendChild(result.row);
        });
    }

    // 行列モードUI構築
    function buildMatrixMode() {
        paramsContainer.innerHTML = '';

        const matrixParams = [
            { name: 'a', label: 'a', min: -3, max: 3, step: 0.01, default: 1 },
            { name: 'b', label: 'b', min: -3, max: 3, step: 0.01, default: 0 },
            { name: 'c', label: 'c', min: -3, max: 3, step: 0.01, default: 0 },
            { name: 'd', label: 'd', min: -3, max: 3, step: 0.01, default: 1 },
            { name: 'tx', label: 'tx', min: -500, max: 500, step: 0.1, default: 0 },
            { name: 'ty', label: 'ty', min: -500, max: 500, step: 0.1, default: 0 }
        ];

        matrixParams.forEach(p => {
            const value = node.matrix && node.matrix[p.name] !== undefined ? node.matrix[p.name] : p.default;
            const result = createDetailSliderRow({
                label: p.label,
                min: p.min,
                max: p.max,
                step: p.step,
                value: value,
                onChange: (val) => {
                    if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
                    node.matrix[p.name] = val;
                    updateAffineIndicator();
                    if (onChange) onChange();
                }
            });
            paramsContainer.appendChild(result.row);
        });
    }

    // モード切替ハンドラ
    paramBtn.addEventListener('click', () => {
        node.matrixMode = false;
        paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #4CAF50; border-radius: 3px; cursor: pointer; background: #4CAF50; color: white;`;
        matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; background: #333; color: #ccc;`;
        buildParamMode();
    });

    matrixBtn.addEventListener('click', () => {
        node.matrixMode = true;
        matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #4CAF50; border-radius: 3px; cursor: pointer; background: #4CAF50; color: white;`;
        paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; background: #333; color: #ccc;`;
        buildMatrixMode();
    });

    // 初期表示
    if (node.matrixMode) {
        buildMatrixMode();
    } else {
        buildParamMode();
    }

    section.appendChild(content);
    container.appendChild(section);
    return section;
}

// アフィン変換が設定されているか判定
export function hasAffineTransform(node) {
    // パラメータモードのチェック
    if (node.rotation && node.rotation !== 0) return true;
    if (node.scaleX !== undefined && node.scaleX !== 1) return true;
    if (node.scaleY !== undefined && node.scaleY !== 1) return true;
    if (node.translateX && node.translateX !== 0) return true;
    if (node.translateY && node.translateY !== 0) return true;

    // 行列モードのチェック
    if (node.matrix) {
        const m = node.matrix;
        if (m.a !== 1 || m.b !== 0 || m.c !== 0 || m.d !== 1 || m.tx !== 0 || m.ty !== 0) {
            return true;
        }
    }

    return false;
}

// ノードのアフィン変換行列を取得（matrixModeに応じて適切な値を返す）
// matrixMode: true → node.matrixのa,b,c,dを使用（tx/tyはtranslateX/Yから）
// matrixMode: false → パラメータから行列を計算
export function getAffineMatrix(node) {
    const tx = node.translateX || 0;
    const ty = node.translateY || 0;

    if (node.matrixMode && node.matrix) {
        // 行列モード: node.matrix の a,b,c,d を使用、tx/ty は translateX/Y から
        return {
            a: node.matrix.a ?? 1,
            b: node.matrix.b ?? 0,
            c: node.matrix.c ?? 0,
            d: node.matrix.d ?? 1,
            tx: tx,
            ty: ty
        };
    } else {
        // パラメータモード: パラメータから行列を計算
        return calculateMatrixFromParams(
            tx,
            ty,
            node.rotation || 0,
            node.scaleX !== undefined ? node.scaleX : 1,
            node.scaleY !== undefined ? node.scaleY : 1
        );
    }
}

// タブ内で使用するアフィンコントロール（折りたたみなし）
// container内にアフィン制御UIを構築する
export function buildAffineTabContent(node, container, onChange) {
    // === X/Y移動スライダー（常に表示、モード切替の外） ===
    const translateSection = document.createElement('div');
    translateSection.className = 'affine-translate-section';
    translateSection.style.cssText = 'margin-bottom: 12px; padding-bottom: 8px; border-bottom: 1px solid #444;';

    // X/Y移動更新用のヘルパー（matrixModeに応じて適切に行列を更新）
    function updateTranslation() {
        if (node.matrixMode && node.matrix) {
            // 行列モード: tx/tyのみ更新、a,b,c,dは保持
            node.matrix.tx = node.translateX || 0;
            node.matrix.ty = node.translateY || 0;
        } else {
            // パラメータモード: 全体を再計算
            node.matrix = calculateMatrixFromParams(
                node.translateX || 0,
                node.translateY || 0,
                node.rotation || 0,
                node.scaleX !== undefined ? node.scaleX : 1,
                node.scaleY !== undefined ? node.scaleY : 1
            );
        }
    }

    // X移動
    const txValue = node.translateX !== undefined ? node.translateX : 0;
    const txResult = createDetailSliderRow({
        label: 'X移動',
        min: -500,
        max: 500,
        step: 0.1,
        value: txValue,
        onChange: (val) => {
            node.translateX = val;
            updateTranslation();
            if (onChange) onChange();
        }
    });
    translateSection.appendChild(txResult.row);

    // Y移動
    const tyValue = node.translateY !== undefined ? node.translateY : 0;
    const tyResult = createDetailSliderRow({
        label: 'Y移動',
        min: -500,
        max: 500,
        step: 0.1,
        value: tyValue,
        onChange: (val) => {
            node.translateY = val;
            updateTranslation();
            if (onChange) onChange();
        }
    });
    translateSection.appendChild(tyResult.row);

    container.appendChild(translateSection);

    // === モード切替ボタン（パラメータ/行列） ===
    const modeRow = document.createElement('div');
    modeRow.style.cssText = 'display: flex; gap: 4px; margin-bottom: 8px;';

    const paramBtn = document.createElement('button');
    paramBtn.textContent = 'パラメータ';
    paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; outline: none; ${!node.matrixMode ? 'background: #4CAF50; color: white; border-color: #4CAF50;' : 'background: #333; color: #ccc;'}`;

    const matrixBtn = document.createElement('button');
    matrixBtn.textContent = '行列';
    matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; outline: none; ${node.matrixMode ? 'background: #4CAF50; color: white; border-color: #4CAF50;' : 'background: #333; color: #ccc;'}`;

    modeRow.appendChild(paramBtn);
    modeRow.appendChild(matrixBtn);
    container.appendChild(modeRow);

    // パラメータコンテナ
    const paramsContainer = document.createElement('div');
    paramsContainer.className = 'affine-params-container';
    container.appendChild(paramsContainer);

    // パラメータモードUI構築（回転・スケールのみ、X/Y移動は上部に分離）
    function buildParamMode() {
        paramsContainer.innerHTML = '';

        const params = [
            { key: 'rotation', label: '回転', min: -180, max: 180, step: 0.1, default: 0, unit: '°' },
            { key: 'scaleX', label: 'X倍率', min: -5, max: 5, step: 0.01, default: 1, unit: '' },
            { key: 'scaleY', label: 'Y倍率', min: -5, max: 5, step: 0.01, default: 1, unit: '' }
        ];

        params.forEach(p => {
            const value = node[p.key] !== undefined ? node[p.key] : p.default;
            const result = createDetailSliderRow({
                label: p.label,
                min: p.min,
                max: p.max,
                step: p.step,
                value: value,
                unit: p.unit,
                onChange: (val) => {
                    node[p.key] = val;
                    // 行列を再計算
                    node.matrix = calculateMatrixFromParams(
                        node.translateX || 0,
                        node.translateY || 0,
                        node.rotation || 0,
                        node.scaleX !== undefined ? node.scaleX : 1,
                        node.scaleY !== undefined ? node.scaleY : 1
                    );
                    if (onChange) onChange();
                }
            });
            paramsContainer.appendChild(result.row);
        });
    }

    // 行列モードUI構築（a,b,c,dのみ、tx/tyは上部に分離）
    function buildMatrixMode() {
        paramsContainer.innerHTML = '';

        const matrixParams = [
            { name: 'a', label: 'a', min: -3, max: 3, step: 0.01, default: 1 },
            { name: 'b', label: 'b', min: -3, max: 3, step: 0.01, default: 0 },
            { name: 'c', label: 'c', min: -3, max: 3, step: 0.01, default: 0 },
            { name: 'd', label: 'd', min: -3, max: 3, step: 0.01, default: 1 }
        ];

        matrixParams.forEach(p => {
            const value = node.matrix && node.matrix[p.name] !== undefined ? node.matrix[p.name] : p.default;
            const result = createDetailSliderRow({
                label: p.label,
                min: p.min,
                max: p.max,
                step: p.step,
                value: value,
                onChange: (val) => {
                    if (!node.matrix) node.matrix = { a: 1, b: 0, c: 0, d: 1, tx: 0, ty: 0 };
                    node.matrix[p.name] = val;
                    if (onChange) onChange();
                }
            });
            paramsContainer.appendChild(result.row);
        });
    }

    // モード切替ハンドラ
    paramBtn.addEventListener('click', () => {
        node.matrixMode = false;
        paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #4CAF50; border-radius: 3px; cursor: pointer; outline: none; background: #4CAF50; color: white;`;
        matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; outline: none; background: #333; color: #ccc;`;
        buildParamMode();
    });

    matrixBtn.addEventListener('click', () => {
        node.matrixMode = true;
        matrixBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #4CAF50; border-radius: 3px; cursor: pointer; outline: none; background: #4CAF50; color: white;`;
        paramBtn.style.cssText = `flex: 1; padding: 4px; font-size: 10px; border: 1px solid #555; border-radius: 3px; cursor: pointer; outline: none; background: #333; color: #ccc;`;
        buildMatrixMode();
    });

    // 初期表示
    if (node.matrixMode) {
        buildMatrixMode();
    } else {
        buildParamMode();
    }
}
