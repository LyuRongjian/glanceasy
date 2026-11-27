// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Public API of Glanceasy, a CPU rasterising OpenGL ES implementation.
//
// The library exports the OpenGL ES 2.0 / 3.0 entry points declared in
// <GLES3/gl32.h>, and adds the handful of context-lifetime calls below,
// which a window system would normally provide.  The implementation keeps
// every pixel on the CPU: there is no driver, no GPU, and no window
// system dependency.
//
// Typical use:
//
//   glanceasyCreateContext(640, 480);        // makes the context current
//   ... glClear / glDraw* calls ...
//   const uint8_t* rgba = glanceasyGetPixels(&w, &h);
//   glanceasyDestroyContext();
//
// All GL calls operate on the current context of the calling thread.  A
// context is not thread safe: use one context per thread, or serialise
// access externally.

#ifndef GLANCEASY_H_
#define GLANCEASY_H_

#include <stdint.h>

#include "GLES3/gl32.h"

#ifndef GLANCEASY_EXPORT
#if defined(_WIN32) && defined(GLANCEASY_SHARED)
#define GLANCEASY_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define GLANCEASY_EXPORT __attribute__((visibility("default")))
#else
#define GLANCEASY_EXPORT
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Opaque context handle.  Each handle owns a complete GL state machine and
// its own colour, depth and stencil buffers.
typedef struct GlanceasyContext GlanceasyContext;

// Creates the default context, destroys any previous default context, and
// makes the new one current.  Returns 1 on success, 0 when |width| or
// |height| is not positive.
GLANCEASY_EXPORT int glanceasyCreateContext(int width, int height);

// Destroys the default context if it exists.  Safe to call when no context
// was ever created.
GLANCEASY_EXPORT void glanceasyDestroyContext(void);

// Returns 1 when a context exists and is current.
GLANCEASY_EXPORT int glanceasyContextReady(void);

// Multi-context API.  A new handle is created but not made current;
// call glanceasyMakeCurrent() to bind it.  Passing NULL to glanceasyMakeCurrent
// unbinds the current context without destroying it.
GLANCEASY_EXPORT GlanceasyContext* glanceasyCreateContextHandle(int width, int height);
GLANCEASY_EXPORT void glanceasyDestroyContextHandle(GlanceasyContext* context);
GLANCEASY_EXPORT void glanceasyMakeCurrent(GlanceasyContext* context);
GLANCEASY_EXPORT GlanceasyContext* glanceasyGetCurrentContext(void);

// Resizes the default framebuffer of |context|.  Returns 1 on success.
// Object names and GL state survive a resize; the colour and depth buffers
// are reallocated and cleared to transparent black.  A framebuffer object
// that is bound at the time is left untouched.
GLANCEASY_EXPORT int glanceasyResizeContext(GlanceasyContext* context, int width, int height);

// Dimensions of the default framebuffer of the current context.
GLANCEASY_EXPORT int glanceasyGetWidth(void);
GLANCEASY_EXPORT int glanceasyGetHeight(void);

// Returns the default framebuffer of the current context as tightly packed
// RGBA/UNORM bytes, top row first, or NULL when no context is current.  The
// pointer stays valid until the next resize or glanceasyDestroyContext().
GLANCEASY_EXPORT const uint8_t* glanceasyGetPixels(int* width, int* height);

// Same framebuffer, native 32-bit packing 0xAABBGGRR (RGBA bytes in memory
// order on little endian targets), which is what a blit wants.
GLANCEASY_EXPORT const uint32_t* glanceasyGetPixels32(int* width, int* height);

// Per-context counters.  Cheap enough to read once per frame.
typedef struct GlanceasyStats {
  uint64_t draw_calls;
  uint64_t vertices_submitted;
  uint64_t fragments_shaded;
  uint64_t fragments_written;
  uint64_t triangles_rasterised;
} GlanceasyStats;

GLANCEASY_EXPORT void glanceasyGetStats(GlanceasyStats* stats);
GLANCEASY_EXPORT void glanceasyResetStats(void);

// Native shader compiler counters.  These are process-wide because a program
// can outlive the context that linked it.  A fallback means the stage uses a
// construct outside the JIT fast path and the GLSL evaluator ran instead.
typedef struct GlanceasyJitStats {
  uint64_t compile_attempts;
  uint64_t stages_compiled;
  uint64_t fallback_stages;
  uint64_t dispatches;
} GlanceasyJitStats;

GLANCEASY_EXPORT void glanceasyGetJitStats(GlanceasyJitStats* stats);
GLANCEASY_EXPORT void glanceasyResetJitStats(void);

// Number of contexts alive in the process, useful for leak checks.
GLANCEASY_EXPORT int glanceasyLiveContextCount(void);

// One line describing the pixel format and the last shader error, or an
// empty string.  Intended for logs, not for applications.
GLANCEASY_EXPORT const char* glanceasyGetDiagnostics(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // GLANCEASY_H_
