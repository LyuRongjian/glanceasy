// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Public API of ZCat-GLES, a CPU rasterising OpenGL ES implementation.
//
// The library exports the OpenGL ES 2.0 / 3.0 entry points declared in
// <GLES3/gl32.h>, and adds the handful of context-lifetime calls below,
// which a window system would normally provide.  The implementation keeps
// every pixel on the CPU: there is no driver, no GPU, and no window
// system dependency.
//
// Typical use:
//
//   zglCreateContext(640, 480);        // makes the context current
//   ... glClear / glDraw* calls ...
//   const uint8_t* rgba = zglGetPixels(&w, &h);
//   zglDestroyContext();
//
// All GL calls operate on the current context of the calling thread.  A
// context is not thread safe: use one context per thread, or serialise
// access externally.

#ifndef ZGL_ZGL_H_
#define ZGL_ZGL_H_

#include <stdint.h>

#include "GLES3/gl32.h"

#ifndef ZGL_EXPORT
#if defined(_WIN32) && defined(ZGL_SHARED)
#define ZGL_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define ZGL_EXPORT __attribute__((visibility("default")))
#else
#define ZGL_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context handle.  Each handle owns a complete GL state machine and
// its own colour, depth and stencil buffers.
typedef struct ZglContext ZglContext;

// Creates the default context, destroys any previous default context, and
// makes the new one current.  Returns 1 on success, 0 when |width| or
// |height| is not positive.
ZGL_EXPORT int zglCreateContext(int width, int height);

// Destroys the default context if it exists.  Safe to call when no context
// was ever created.
ZGL_EXPORT void zglDestroyContext(void);

// Returns 1 when a context exists and is current.
ZGL_EXPORT int zglContextReady(void);

// Multi-context API.  A new handle is created but not made current;
// call zglMakeCurrent() to bind it.  Passing NULL to zglMakeCurrent
// unbinds the current context without destroying it.
ZGL_EXPORT ZglContext* zglCreateContextHandle(int width, int height);
ZGL_EXPORT void zglDestroyContextHandle(ZglContext* context);
ZGL_EXPORT void zglMakeCurrent(ZglContext* context);
ZGL_EXPORT ZglContext* zglGetCurrentContext(void);

// Resizes the default framebuffer of |context|.  Returns 1 on success.
// Object names and GL state survive a resize; the colour and depth buffers
// are reallocated and cleared to transparent black.  A framebuffer object
// that is bound at the time is left untouched.
ZGL_EXPORT int zglResizeContext(ZglContext* context, int width, int height);

// Dimensions of the default framebuffer of the current context.
ZGL_EXPORT int zglGetWidth(void);
ZGL_EXPORT int zglGetHeight(void);

// Returns the default framebuffer of the current context as tightly packed
// RGBA/UNORM bytes, top row first, or NULL when no context is current.  The
// pointer stays valid until the next resize or zglDestroyContext().
ZGL_EXPORT const uint8_t* zglGetPixels(int* width, int* height);

// Same framebuffer, native 32-bit packing 0xAABBGGRR (RGBA bytes in memory
// order on little endian targets), which is what a blit wants.
ZGL_EXPORT const uint32_t* zglGetPixels32(int* width, int* height);

// Per-context counters.  Cheap enough to read once per frame.
typedef struct ZglStats {
  uint64_t draw_calls;
  uint64_t vertices_submitted;
  uint64_t fragments_shaded;
  uint64_t fragments_written;
  uint64_t triangles_rasterised;
} ZglStats;

ZGL_EXPORT void zglGetStats(ZglStats* stats);
ZGL_EXPORT void zglResetStats(void);

// Number of contexts alive in the process, useful for leak checks.
ZGL_EXPORT int zglLiveContextCount(void);

// One line describing the pixel format and the last shader error, or an
// empty string.  Intended for logs, not for applications.
ZGL_EXPORT const char* zglGetDiagnostics(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // ZGL_ZGL_H_
