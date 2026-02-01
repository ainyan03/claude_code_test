/**
 * @file fleximg.cpp
 * @brief fleximg ライブラリ実装のコンパイル単位
 *
 * このファイルは fleximg ライブラリの唯一のコンパイル単位です。
 * FLEXIMG_IMPLEMENTATION を定義してから各ヘッダをincludeすることで、
 * 実装部がここでのみコンパイルされます。
 *
 * stb ライブラリ方式（Implementation Macro パターン）を採用しています。
 */

#define FLEXIMG_IMPLEMENTATION

// =============================================================================
// Core
// =============================================================================
#include "core/memory/platform.h"
#include "core/memory/pool_allocator.h"
#include "core/affine_capability.h"
#include "core/node.h"

// =============================================================================
// Image
// =============================================================================
#include "image/pixel_format.h"
#include "image/viewport.h"
#include "image/image_buffer_set.h"

// =============================================================================
// Operations
// =============================================================================
#include "operations/filters.h"

// =============================================================================
// Nodes
// =============================================================================
#include "nodes/vertical_blur_node.h"
#include "nodes/matte_node.h"
#include "nodes/source_node.h"
#include "nodes/ninepatch_source_node.h"
#include "nodes/composite_node.h"
#include "nodes/horizontal_blur_node.h"
#include "nodes/renderer_node.h"
#include "nodes/sink_node.h"
#include "nodes/filter_node_base.h"
#include "nodes/affine_node.h"
#include "nodes/distributor_node.h"
