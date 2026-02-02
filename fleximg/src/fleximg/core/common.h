/**
 * @file common.h
 * @brief Common definitions for fleximg library
 */

#ifndef FLEXIMG_COMMON_H
#define FLEXIMG_COMMON_H

// Namespace definition (types.h より前に必要)
#ifndef FLEXIMG_NAMESPACE
#define FLEXIMG_NAMESPACE fleximg
#endif

#include "types.h"

#include <cstdlib>  // std::abort

// ========================================================================
// Debug/Release assertion macros
// ========================================================================
//
// FLEXIMG_ASSERT(cond, msg): デバッグ時のみ有効（リリースでは無効化）
// FLEXIMG_REQUIRE(cond, msg): 常に有効（致命的エラー検出用）
//

#ifdef FLEXIMG_DEBUG
  #ifdef ARDUINO
    #define FLEXIMG_ASSERT(cond, msg) \
      do { if (!(cond)) { printf("ASSERT FAIL: %s\n", msg); fflush(stdout); vTaskDelay(1); std::abort(); } } while(0)
  #else
    #define FLEXIMG_ASSERT(cond, msg) \
      do { if (!(cond)) { printf("ASSERT FAIL: %s\n", msg); std::abort(); } } while(0)
  #endif
#else
  #define FLEXIMG_ASSERT(cond, msg) ((void)0)
#endif

#define FLEXIMG_REQUIRE(cond, msg) \
  do { if (!(cond)) { printf("REQUIRE FAIL: %s\n", msg); std::abort(); } } while(0)

// ========================================================================
// Deprecated attribute
// ========================================================================

#if __cplusplus >= 201402L
  #define FLEXIMG_DEPRECATED(msg) [[deprecated(msg)]]
#else
  #define FLEXIMG_DEPRECATED(msg)
#endif

// Version information
#define FLEXIMG_VERSION_MAJOR 2
#define FLEXIMG_VERSION_MINOR 0
#define FLEXIMG_VERSION_PATCH 0

#endif // FLEXIMG_COMMON_H
