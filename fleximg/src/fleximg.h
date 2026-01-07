/**
 * @file fleximg.h
 * @brief Main header for fleximg - Flexible Image Processing Engine
 *
 * Include this header to use all fleximg functionality.
 *
 * @example
 * #include <fleximg.h>
 *
 * using namespace fleximg;
 *
 * // Create a viewport and apply operators
 * ViewPort vp(100, 100, PixelFormatIDs::RGBA8_Straight);
 */

#ifndef FLEXIMG_H
#define FLEXIMG_H

// Common definitions
#include "fleximg/common.h"

// Basic types
#include "fleximg/image_types.h"
#include "fleximg/image_allocator.h"

// Pixel format system
#include "fleximg/pixel_format.h"
#include "fleximg/pixel_format_registry.h"

// Core image type
#include "fleximg/viewport.h"

// Operators
#include "fleximg/operators.h"

// Node graph (optional - for pipeline evaluation)
#include "fleximg/evaluation_node.h"
#include "fleximg/node_graph.h"

#endif // FLEXIMG_H
