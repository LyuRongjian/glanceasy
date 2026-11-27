// Copyright 2026 The Glanceasy Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Minimal EGL 1.4 adapter for headless benchmarking.  It exposes Glanceasy
// through the small EGL surface/context API that glmark2's null backend uses:
// a surfaceless display, pbuffer or window surfaces, and ES 2.0/3.0 contexts.
// There is no native window system integration here; that is deliberate.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>

#include <cstdlib>
#include <cstring>

#include "glanceasy/glanceasy.h"

namespace {

constexpr EGLint kMaxDimension = 4096;
constexpr EGLint kDefaultWidth = 1920;
constexpr EGLint kDefaultHeight = 1080;

struct Display {
  int unused;
};

struct Config {
  EGLint id;
};

struct Surface {
  EGLint width;
  EGLint height;
};

struct Context {
  GlanceasyContext* context;
  EGLint client_version;
};

Display g_display;
Config g_config = {1};
EGLint g_error = EGL_SUCCESS;
Context* g_current_context = nullptr;
Surface* g_current_surface = nullptr;
void* g_gles_library = nullptr;

EGLint TakeError() {
  const EGLint error = g_error;
  g_error = EGL_SUCCESS;
  return error;
}

void SetError(EGLint error) {
  if (g_error == EGL_SUCCESS)
    g_error = error;
}

bool IsDisplay(EGLDisplay display) {
  return display == &g_display;
}

Surface* AsSurface(EGLSurface surface) {
  return static_cast<Surface*>(surface);
}

Context* AsContext(EGLContext context) {
  return static_cast<Context*>(context);
}

EGLint ConfigAttrib(EGLint attribute) {
  switch (attribute) {
    case EGL_CONFIG_ID:
      return g_config.id;
    case EGL_CONFIG_CAVEAT:
      return EGL_NONE;
    case EGL_CONFORMANT:
      return EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT;
    case EGL_COLOR_BUFFER_TYPE:
      return EGL_RGB_BUFFER;
    case EGL_BUFFER_SIZE:
      return 32;
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE:
      return 8;
    case EGL_LUMINANCE_SIZE:
    case EGL_ALPHA_MASK_SIZE:
      return 0;
    case EGL_DEPTH_SIZE:
      return 24;
    case EGL_STENCIL_SIZE:
      return 8;
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:
      return EGL_FALSE;
    case EGL_LEVEL:
    case EGL_NATIVE_VISUAL_TYPE:
    case EGL_NATIVE_VISUAL_ID:
    case EGL_SAMPLE_BUFFERS:
    case EGL_SAMPLES:
      return 0;
    case EGL_MAX_PBUFFER_WIDTH:
    case EGL_MAX_PBUFFER_HEIGHT:
      return kMaxDimension;
    case EGL_MAX_PBUFFER_PIXELS:
      return kMaxDimension * kMaxDimension;
    case EGL_MIN_SWAP_INTERVAL:
      return 0;
    case EGL_MAX_SWAP_INTERVAL:
      return 1;
    case EGL_NATIVE_RENDERABLE:
      return EGL_FALSE;
    case EGL_SURFACE_TYPE:
      return EGL_PBUFFER_BIT | EGL_WINDOW_BIT;
    case EGL_RENDERABLE_TYPE:
      return EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT;
    case EGL_TRANSPARENT_TYPE:
      return EGL_NONE;
    case EGL_TRANSPARENT_RED_VALUE:
    case EGL_TRANSPARENT_GREEN_VALUE:
    case EGL_TRANSPARENT_BLUE_VALUE:
      return 0;
    default:
      return -1;
  }
}

bool MatchesConfig(const EGLint* attributes) {
  for (const EGLint* it = attributes; it && *it != EGL_NONE; it += 2) {
    const EGLint key = it[0];
    const EGLint value = it[1];
    switch (key) {
      case EGL_SURFACE_TYPE:
        if ((value & (EGL_PBUFFER_BIT | EGL_WINDOW_BIT)) == 0)
          return false;
        break;
      case EGL_RENDERABLE_TYPE:
        if ((value & (EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT)) == 0)
          return false;
        break;
      case EGL_RED_SIZE:
      case EGL_GREEN_SIZE:
      case EGL_BLUE_SIZE:
      case EGL_ALPHA_SIZE:
        if (value > 8)
          return false;
        break;
      case EGL_DEPTH_SIZE:
        if (value > 24)
          return false;
        break;
      case EGL_STENCIL_SIZE:
        if (value > 8)
          return false;
        break;
      case EGL_SAMPLES:
        if (value > 0)
          return false;
        break;
      default:
        break;
    }
  }
  return true;
}

EGLint ReadSurfaceDimension(const EGLint* attributes, EGLint key,
                           EGLint fallback) {
  for (const EGLint* it = attributes; it && *it != EGL_NONE; it += 2) {
    if (it[0] == key)
      return it[1];
  }
  return fallback;
}

}  // namespace

extern "C" {

EGLint EGLAPIENTRY eglGetError(void) {
  return TakeError();
}

EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType) {
  return &g_display;
}

EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay display, EGLint* major,
                                    EGLint* minor) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  if (major)
    *major = 1;
  if (minor)
    *minor = 4;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay display) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  return EGL_TRUE;
}

const char* EGLAPIENTRY eglQueryString(EGLDisplay display, EGLint name) {
  if (display != EGL_NO_DISPLAY && !IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return nullptr;
  }
  switch (name) {
    case EGL_VENDOR:
      return "Glanceasy";
    case EGL_VERSION:
      return "1.4";
    case EGL_EXTENSIONS:
      return "EGL_EXT_platform_base EGL_MESA_platform_surfaceless "
             "EGL_KHR_create_context EGL_KHR_surfaceless_context";
    case EGL_CLIENT_APIS:
      return "OpenGL_ES";
    default:
      SetError(EGL_BAD_PARAMETER);
      return nullptr;
  }
}

EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay display, EGLConfig* configs,
                                    EGLint config_size, EGLint* num_config) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  if (num_config)
    *num_config = 1;
  if (configs && config_size > 0)
    configs[0] = &g_config;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay display,
                                      const EGLint* attrib_list,
                                      EGLConfig* configs, EGLint config_size,
                                      EGLint* num_config) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  const bool matches = MatchesConfig(attrib_list);
  if (num_config)
    *num_config = matches ? 1 : 0;
  if (matches && configs && config_size > 0)
    configs[0] = &g_config;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay display, EGLConfig config,
                                         EGLint attribute, EGLint* value) {
  if (!IsDisplay(display) || config != &g_config) {
    SetError(config != &g_config ? EGL_BAD_CONFIG : EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  const EGLint result = ConfigAttrib(attribute);
  if (result < 0) {
    SetError(EGL_BAD_ATTRIBUTE);
    return EGL_FALSE;
  }
  if (value)
    *value = result;
  return EGL_TRUE;
}

EGLSurface EGLAPIENTRY eglCreateWindowSurface(
    EGLDisplay display, EGLConfig config, EGLNativeWindowType,
    const EGLint* attrib_list) {
  if (!IsDisplay(display) || config != &g_config) {
    SetError(config != &g_config ? EGL_BAD_CONFIG : EGL_BAD_DISPLAY);
    return EGL_NO_SURFACE;
  }
  Surface* surface = new Surface();
  surface->width = ReadSurfaceDimension(attrib_list, EGL_WIDTH, 800);
  surface->height = ReadSurfaceDimension(attrib_list, EGL_HEIGHT, 600);
  return surface;
}

EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay display,
                                              EGLConfig config,
                                              const EGLint* attrib_list) {
  if (!IsDisplay(display) || config != &g_config) {
    SetError(config != &g_config ? EGL_BAD_CONFIG : EGL_BAD_DISPLAY);
    return EGL_NO_SURFACE;
  }
  Surface* surface = new Surface();
  surface->width = ReadSurfaceDimension(attrib_list, EGL_WIDTH, 800);
  surface->height = ReadSurfaceDimension(attrib_list, EGL_HEIGHT, 600);
  if (surface->width <= 0 || surface->height <= 0 ||
      surface->width > kMaxDimension || surface->height > kMaxDimension) {
    delete surface;
    SetError(EGL_BAD_ATTRIBUTE);
    return EGL_NO_SURFACE;
  }
  return surface;
}

EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay, EGLConfig,
                                              EGLNativePixmapType,
                                              const EGLint*) {
  SetError(EGL_BAD_NATIVE_PIXMAP);
  return EGL_NO_SURFACE;
}

EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay display,
                                        EGLSurface egl_surface) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  Surface* surface = AsSurface(egl_surface);
  if (!surface) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
  }
  if (g_current_surface == surface) {
    glanceasyMakeCurrent(nullptr);
    g_current_context = nullptr;
    g_current_surface = nullptr;
  }
  delete surface;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay display,
                                      EGLSurface egl_surface, EGLint attribute,
                                      EGLint* value) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  Surface* surface = AsSurface(egl_surface);
  if (!surface) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
  }
  switch (attribute) {
    case EGL_WIDTH:
      *value = surface->width;
      return EGL_TRUE;
    case EGL_HEIGHT:
      *value = surface->height;
      return EGL_TRUE;
    case EGL_SWAP_BEHAVIOR:
      *value = EGL_BUFFER_DESTROYED;
      return EGL_TRUE;
    default:
      SetError(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
  }
}

EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay display,
                                       EGLSurface egl_surface, EGLint,
                                       EGLint) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  if (!AsSurface(egl_surface)) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
  }
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay, EGLSurface, EGLint) {
  SetError(EGL_BAD_SURFACE);
  return EGL_FALSE;
}

EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay, EGLSurface, EGLint) {
  SetError(EGL_BAD_SURFACE);
  return EGL_FALSE;
}

EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay display, EGLint) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api) {
  if (api != EGL_OPENGL_ES_API) {
    SetError(EGL_BAD_PARAMETER);
    return EGL_FALSE;
  }
  return EGL_TRUE;
}

EGLenum EGLAPIENTRY eglQueryAPI(void) {
  return EGL_OPENGL_ES_API;
}

EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay display, EGLConfig config,
                                       EGLContext, const EGLint* attrib_list) {
  if (!IsDisplay(display) || config != &g_config) {
    SetError(config != &g_config ? EGL_BAD_CONFIG : EGL_BAD_DISPLAY);
    return EGL_NO_CONTEXT;
  }
  Context* context = new Context();
  context->client_version =
      ReadSurfaceDimension(attrib_list, EGL_CONTEXT_CLIENT_VERSION, 2);
  context->context = glanceasyCreateContextHandle(kDefaultWidth, kDefaultHeight);
  if (!context->context) {
    delete context;
    SetError(EGL_BAD_ALLOC);
    return EGL_NO_CONTEXT;
  }
  return context;
}

EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay display,
                                        EGLContext egl_context) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  Context* context = AsContext(egl_context);
  if (!context) {
    SetError(EGL_BAD_CONTEXT);
    return EGL_FALSE;
  }
  if (g_current_context == context) {
    glanceasyMakeCurrent(nullptr);
    g_current_context = nullptr;
    g_current_surface = nullptr;
  }
  glanceasyDestroyContextHandle(context->context);
  delete context;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay display, EGLSurface draw,
                                     EGLSurface read, EGLContext egl_context) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  if (draw != read) {
    SetError(EGL_BAD_MATCH);
    return EGL_FALSE;
  }
  Context* context = AsContext(egl_context);
  if (!context) {
    glanceasyMakeCurrent(nullptr);
    g_current_context = nullptr;
    g_current_surface = nullptr;
    return EGL_TRUE;
  }
  Surface* surface = AsSurface(draw);
  if (surface) {
    if (surface->width != kDefaultWidth || surface->height != kDefaultHeight) {
      if (!glanceasyResizeContext(context->context, surface->width,
                                 surface->height)) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
      }
    }
  }
  glanceasyMakeCurrent(context->context);
  g_current_context = context;
  g_current_surface = surface;
  return EGL_TRUE;
}

EGLContext EGLAPIENTRY eglGetCurrentContext(void) {
  return g_current_context;
}

EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint) {
  return g_current_surface;
}

EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void) {
  return g_current_context ? &g_display : EGL_NO_DISPLAY;
}

EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay display,
                                      EGLContext egl_context, EGLint attribute,
                                      EGLint* value) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  Context* context = AsContext(egl_context);
  if (!context) {
    SetError(EGL_BAD_CONTEXT);
    return EGL_FALSE;
  }
  if (attribute != EGL_CONTEXT_CLIENT_VERSION) {
    SetError(EGL_BAD_ATTRIBUTE);
    return EGL_FALSE;
  }
  *value = context->client_version;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglWaitGL(void) {
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglWaitClient(void) {
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglReleaseThread(void) {
  glanceasyMakeCurrent(nullptr);
  g_current_context = nullptr;
  g_current_surface = nullptr;
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay display,
                                     EGLSurface egl_surface) {
  if (!IsDisplay(display)) {
    SetError(EGL_BAD_DISPLAY);
    return EGL_FALSE;
  }
  if (!AsSurface(egl_surface)) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
  }
  return EGL_TRUE;
}

EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageKHR(EGLDisplay display,
                                                  EGLSurface surface, EGLint*,
                                                  EGLint) {
  return eglSwapBuffers(display, surface);
}

EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT(EGLenum, void*,
                                               const EGLint*) {
  return &g_display;
}

EGLDisplay EGLAPIENTRY eglGetPlatformDisplay(EGLenum, void*,
                                            const EGLAttrib*) {
  return &g_display;
}

EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurface(EGLDisplay display,
                                                      EGLConfig config,
                                                      void* native_window,
                                                      const EGLAttrib* attribs) {
  return eglCreateWindowSurface(
      display, config, reinterpret_cast<EGLNativeWindowType>(native_window),
      nullptr);
}

EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurface(EGLDisplay, EGLConfig,
                                                     void*, const EGLAttrib*) {
  SetError(EGL_BAD_NATIVE_PIXMAP);
  return EGL_NO_SURFACE;
}

__eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char* name) {
#define EGL_FUNCTION(fn) \
  if (std::strcmp(name, #fn) == 0) return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(fn)

  EGL_FUNCTION(eglGetError);
  EGL_FUNCTION(eglGetDisplay);
  EGL_FUNCTION(eglInitialize);
  EGL_FUNCTION(eglTerminate);
  EGL_FUNCTION(eglQueryString);
  EGL_FUNCTION(eglGetConfigs);
  EGL_FUNCTION(eglChooseConfig);
  EGL_FUNCTION(eglGetConfigAttrib);
  EGL_FUNCTION(eglCreateWindowSurface);
  EGL_FUNCTION(eglCreatePbufferSurface);
  EGL_FUNCTION(eglCreatePixmapSurface);
  EGL_FUNCTION(eglDestroySurface);
  EGL_FUNCTION(eglQuerySurface);
  EGL_FUNCTION(eglSurfaceAttrib);
  EGL_FUNCTION(eglBindTexImage);
  EGL_FUNCTION(eglReleaseTexImage);
  EGL_FUNCTION(eglSwapInterval);
  EGL_FUNCTION(eglBindAPI);
  EGL_FUNCTION(eglQueryAPI);
  EGL_FUNCTION(eglCreateContext);
  EGL_FUNCTION(eglDestroyContext);
  EGL_FUNCTION(eglMakeCurrent);
  EGL_FUNCTION(eglGetCurrentContext);
  EGL_FUNCTION(eglGetCurrentSurface);
  EGL_FUNCTION(eglGetCurrentDisplay);
  EGL_FUNCTION(eglQueryContext);
  EGL_FUNCTION(eglWaitGL);
  EGL_FUNCTION(eglWaitClient);
  EGL_FUNCTION(eglReleaseThread);
  EGL_FUNCTION(eglSwapBuffers);
  EGL_FUNCTION(eglGetPlatformDisplayEXT);
  EGL_FUNCTION(eglGetPlatformDisplay);

#undef EGL_FUNCTION

  if (name && std::strncmp(name, "egl", 3) == 0)
    return nullptr;

  void* symbol = dlsym(RTLD_DEFAULT, name);
  if (!symbol) {
    if (!g_gles_library)
      g_gles_library = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_GLOBAL);
    if (g_gles_library)
      symbol = dlsym(g_gles_library, name);
  }
  return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(symbol);
}

}  // extern "C"
