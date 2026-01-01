// Pure JavaScript implementation of image transformation
// This is a fallback when WebAssembly is not available
// It implements the same functionality as the C++ version

class ImageTransformJS {
    constructor(canvasWidth, canvasHeight) {
        this.canvasWidth = canvasWidth;
        this.canvasHeight = canvasHeight;
        this.layers = [];
    }

    addLayer(imageData, width, height) {
        const layer = {
            data: new Uint8ClampedArray(imageData),
            width: width,
            height: height,
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
        this.layers.push(layer);
        return this.layers.length - 1;
    }

    removeLayer(layerId) {
        if (layerId >= 0 && layerId < this.layers.length) {
            this.layers.splice(layerId, 1);
        }
    }

    setLayerTransform(layerId, translateX, translateY, rotation, scaleX, scaleY, alpha) {
        if (layerId >= 0 && layerId < this.layers.length) {
            this.layers[layerId].params = {
                translateX, translateY, rotation, scaleX, scaleY, alpha
            };
        }
    }

    setLayerVisibility(layerId, visible) {
        if (layerId >= 0 && layerId < this.layers.length) {
            this.layers[layerId].visible = visible;
        }
    }

    moveLayer(fromIndex, toIndex) {
        if (fromIndex >= 0 && fromIndex < this.layers.length &&
            toIndex >= 0 && toIndex < this.layers.length) {
            const [layer] = this.layers.splice(fromIndex, 1);
            this.layers.splice(toIndex, 0, layer);
        }
    }

    setCanvasSize(width, height) {
        this.canvasWidth = width;
        this.canvasHeight = height;
    }

    getLayerCount() {
        return this.layers.length;
    }

    compose() {
        const result = new Uint8ClampedArray(this.canvasWidth * this.canvasHeight * 4);

        // Initialize with transparent
        result.fill(0);

        // Compose each visible layer
        for (const layer of this.layers) {
            if (!layer.visible) continue;

            const transformed = this._applyAffineTransform(layer);
            this._blendLayer(result, transformed, layer.params.alpha);
        }

        return {
            data: result,
            width: this.canvasWidth,
            height: this.canvasHeight
        };
    }

    _applyAffineTransform(layer) {
        const result = new Uint8ClampedArray(this.canvasWidth * this.canvasHeight * 4);
        const params = layer.params;

        const centerX = this.canvasWidth / 2.0;
        const centerY = this.canvasHeight / 2.0;

        const cosTheta = Math.cos(-params.rotation);
        const sinTheta = Math.sin(-params.rotation);

        for (let dstY = 0; dstY < this.canvasHeight; dstY++) {
            for (let dstX = 0; dstX < this.canvasWidth; dstX++) {
                // Canvas coordinates to relative
                let dx = dstX - centerX;
                let dy = dstY - centerY;

                // Apply translation (inverse)
                dx -= params.translateX;
                dy -= params.translateY;

                // Apply rotation (inverse)
                const rotX = dx * cosTheta - dy * sinTheta;
                const rotY = dx * sinTheta + dy * cosTheta;

                // Apply scale (inverse)
                let srcX, srcY;
                if (params.scaleX !== 0.0 && params.scaleY !== 0.0) {
                    srcX = rotX / params.scaleX + layer.width / 2.0;
                    srcY = rotY / params.scaleY + layer.height / 2.0;
                } else {
                    continue;
                }

                // Bilinear interpolation
                const dstIdx = (dstY * this.canvasWidth + dstX) * 4;
                const pixel = this._getTransformedPixel(layer, srcX, srcY);

                if (pixel) {
                    result[dstIdx] = pixel[0];
                    result[dstIdx + 1] = pixel[1];
                    result[dstIdx + 2] = pixel[2];
                    result[dstIdx + 3] = pixel[3];
                }
            }
        }

        return result;
    }

    _getTransformedPixel(layer, x, y) {
        if (x < 0 || y < 0 || x >= layer.width - 1 || y >= layer.height - 1) {
            return null;
        }

        const x0 = Math.floor(x);
        const y0 = Math.floor(y);
        const x1 = x0 + 1;
        const y1 = y0 + 1;

        const fx = x - x0;
        const fy = y - y0;

        const pixel = new Uint8Array(4);

        // Bilinear interpolation for each channel
        for (let c = 0; c < 4; c++) {
            const p00 = layer.data[(y0 * layer.width + x0) * 4 + c];
            const p10 = layer.data[(y0 * layer.width + x1) * 4 + c];
            const p01 = layer.data[(y1 * layer.width + x0) * 4 + c];
            const p11 = layer.data[(y1 * layer.width + x1) * 4 + c];

            const p0 = p00 * (1 - fx) + p10 * fx;
            const p1 = p01 * (1 - fx) + p11 * fx;
            const p = p0 * (1 - fy) + p1 * fy;

            pixel[c] = Math.max(0, Math.min(255, Math.round(p)));
        }

        return pixel;
    }

    _blendLayer(dst, src, layerAlpha) {
        for (let i = 0; i < dst.length; i += 4) {
            const srcAlpha = (src[i + 3] / 255.0) * layerAlpha;
            const dstAlpha = dst[i + 3] / 255.0;

            const outAlpha = srcAlpha + dstAlpha * (1.0 - srcAlpha);

            if (outAlpha > 0.0) {
                for (let c = 0; c < 3; c++) {
                    const srcC = src[i + c] / 255.0;
                    const dstC = dst[i + c] / 255.0;
                    const outC = (srcC * srcAlpha + dstC * dstAlpha * (1.0 - srcAlpha)) / outAlpha;
                    dst[i + c] = Math.round(outC * 255.0);
                }
                dst[i + 3] = Math.round(outAlpha * 255.0);
            }
        }
    }
}

// Export for use in app.js
if (typeof window !== 'undefined') {
    window.ImageTransformJS = ImageTransformJS;
}
