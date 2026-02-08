// デバッグ用テストパターン画像を生成
export function generateTestPatterns({
    addImageToLibrary,
    addNativeImageToLibrary,
    paletteLibrary,
    PRESET_PALETTES,
    graphEvaluator,
    allocCppImageId,
    allocPaletteId
}) {
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

    // パターン6: 星型マスク（128x128、Index8として直接生成）
    // 輝度グラデーションでインデックス値を直接計算
    // ※ Star画像はIndex8ネイティブ形式で登録するため、patternsには追加しない
    // ※ 後で addNativeImageToLibrary で登録する
    let starIndexData = null;
    const starSize = 128;
    {
        const size = starSize;
        const indexData = new Uint8Array(size * size);

        const cx = size / 2;
        const cy = size / 2;
        const outerRadius = size * 0.45;  // 外側の頂点
        const innerRadius = size * 0.18;  // 内側の頂点
        const points = 5;
        const blurRadius = 8;  // ぼかし半径

        // 星型の境界距離を計算する関数
        function getStarRadius(angle, outerR, innerR, numPoints) {
            // 角度を正規化（0〜2π）
            const a = ((angle % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI);
            // 星の頂点間の角度
            const segmentAngle = Math.PI / numPoints;
            // 現在の角度がどのセグメントにあるか
            const segment = Math.floor(a / segmentAngle);
            // セグメント内の相対角度（0〜1）
            const t = (a - segment * segmentAngle) / segmentAngle;

            // 偶数セグメントは外→内、奇数セグメントは内→外
            if (segment % 2 === 0) {
                return outerR * (1 - t) + innerR * t;
            } else {
                return innerR * (1 - t) + outerR * t;
            }
        }

        for (let y = 0; y < size; y++) {
            for (let x = 0; x < size; x++) {
                const dx = x - cx;
                const dy = y - cy;
                const dist = Math.sqrt(dx * dx + dy * dy);
                const angle = Math.atan2(dy, dx) + Math.PI / 2;  // 上向きを0度に

                // 星の境界距離
                const starRadius = getStarRadius(angle, outerRadius, innerRadius, points);

                // 距離に基づいてインデックス値を計算（0-255）
                let index;
                if (dist > starRadius + blurRadius) {
                    // 完全に外側: 背景（インデックス0）
                    index = 0;
                } else if (dist < starRadius * 0.92) {
                    // 星の内部: 最大値（インデックス255）
                    index = 255;
                } else if (dist < starRadius) {
                    // 内側グラデーション（92%〜100%）
                    const t = (dist - starRadius * 0.92) / (starRadius * 0.08);
                    index = Math.floor(255 - t * 51);  // 255 → 204
                } else {
                    // 外側ぼかし（100%〜100%+blurRadius）
                    const t = (dist - starRadius) / blurRadius;
                    index = Math.floor(204 * (1 - t));  // 204 → 0
                }

                indexData[y * size + x] = Math.max(0, Math.min(255, index));
            }
        }

        starIndexData = indexData;
    }

    // パターン7: 9patch テスト画像（八角形 + タイルパターン背景）
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

    // パターン8: 9patch ファンタジー装飾枠（セリフ枠用）
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

    // Star画像用にGrayscale256パレットを登録（まだパレットがない場合）
    let grayscalePaletteId = null;
    if (paletteLibrary.length === 0) {
        const preset = PRESET_PALETTES[0];  // Grayscale 256
        if (preset) {
            const { data, count } = preset.generate();
            const cppImageId = allocCppImageId();
            const palId = allocPaletteId();

            // C++側にRGBA8画像として格納
            if (graphEvaluator) {
                graphEvaluator.storeImageWithFormat(cppImageId, data, count, 1, 'RGBA8_Straight');
            }

            paletteLibrary.push({
                id: palId,
                name: preset.name,
                colorCount: count,
                rgba8Data: data,
                cppImageId: cppImageId
            });

            grayscalePaletteId = palId;
        }
    } else {
        // 既存のパレットから最初のものを使用
        grayscalePaletteId = paletteLibrary[0].id;
    }

    // Star画像をIndex8として登録
    if (starIndexData && grayscalePaletteId) {
        addNativeImageToLibrary('Star', starIndexData, starSize, starSize, 'Index8', grayscalePaletteId);
    }

    console.log(`Generated ${patterns.length + 1} test patterns (including Star as Index8)`);
}
