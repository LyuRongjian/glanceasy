// Copyright 2026 The ZCat-GLES Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZGL_SRC_ZGL_CONTEXT_H_
#define ZGL_SRC_ZGL_CONTEXT_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <utility>
#include <algorithm>
#include <vector>

#include "GLES3/gl32.h"
#include "src/glsl/glsl_program.h"
#include "src/zgl_common.h"
#include "zgl/zgl.h"

namespace zgl {

// Returns the number of color channels carried by an internal format.
int FormatColorChannels(GLenum format);

// A rectangular block of pixels used by textures, renderbuffers and the
// default framebuffer.  Colour is always stored as RGBA8; depth and stencil
// are kept in their own planes so that a depth-only surface costs nothing
// extra.
struct Surface {
  int width = 0;
  int height = 0;
  int depth = 0;  // Slice count for 3D textures, otherwise 1.
  int samples = 1;
  std::vector<uint8_t> rgba;
  std::vector<uint16_t> depth16;
  std::vector<uint8_t> stencil8;

  bool has_color() const { return !rgba.empty(); }
  bool has_depth() const { return !depth16.empty(); }
  bool has_stencil() const { return !stencil8.empty(); }

  void AllocateColor(int w, int h);
  void AllocateDepth(int w, int h);
  void AllocateStencil(int w, int h);
  void Clear();
};

// The drawable a draw call renders into.  Pointers refer to storage owned by
// the default framebuffer, a texture or a renderbuffer.
struct RenderTarget {
  int width = 0;
  int height = 0;
  // Storage for every colour attachment of the current framebuffer.
  uint8_t* colors[kMaxColorAttachments] = {};
  // Integer colour attachments store four 32-bit words per texel.  The
  // |colors| entry points at the same storage for compatibility with generic
  // code paths, while |integer_colors| and the flags below describe its type.
  uint32_t* integer_colors[kMaxColorAttachments] = {};
  uint32_t* resolved_integer_colors[kMaxColorAttachments] = {};
  bool color_is_integer[kMaxColorAttachments] = {};
  bool color_is_unsigned[kMaxColorAttachments] = {};
  bool color_has_alpha[kMaxColorAttachments] = {true, true, true, true};
  bool color_is_srgb[kMaxColorAttachments] = {};
  int color_channels[kMaxColorAttachments] = {4, 4, 4, 4};
  int color_bytes[kMaxColorAttachments] = {4, 4, 4, 4};
  // draw_buffers[i] selects the attachment that fragment output i writes.
  GLenum draw_buffers[kMaxColorAttachments] = {
      GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
      GL_COLOR_ATTACHMENT3};
  int draw_buffer_count = 1;
  // The colour buffer selected for reads, or draw buffer 0 for draws.
  uint8_t* color = nullptr;
  // Type of the currently selected read colour buffer.
  bool read_integer = false;
  bool read_unsigned = false;
  bool read_has_alpha = true;
  int read_channels = 4;
  GLenum read_format = 0;
  int read_color_bytes = 4;
  uint16_t* depth = nullptr;
  uint8_t* stencil = nullptr;

  // Multisample storage.  |samples| is 1 for ordinary surfaces; when it is
  // greater, the colour/depth/stencil pointers above address |samples| planes
  // of width * height texels each.
  int samples = 1;
  size_t sample_texels = 0;
  // Resolved planes written at the end of a draw so that reads and texture
  // sampling see a normal single-sample image.
  uint32_t* resolved_colors[kMaxColorAttachments] = {};
  uint16_t* resolved_depth = nullptr;
  uint8_t* resolved_stencil = nullptr;

  bool valid() const { return color != nullptr && width > 0 && height > 0; }
  bool multisampled() const { return samples > 1; }
};

// ---------------------------------------------------------------------------
// Objects.  All object tables hand out positive names and recycle them, as
// the GL specification requires.

class Buffer {
 public:
  std::vector<uint8_t> data;
  GLenum usage = GL_STATIC_DRAW;
  GLenum target = 0;
  bool mapped = false;
  GLenum access_flags = 0;
  size_t map_offset = 0;
  size_t map_length = 0;
};

struct VertexAttribute {
  bool enabled = false;
  int size = 4;
  GLenum type = GL_FLOAT;
  bool normalized = false;
  bool integer = false;
  int stride = 0;
  int offset = 0;
  GLuint buffer = 0;
  const uint8_t* client_pointer = nullptr;
  int divisor = 0;
};

struct VertexArray {
  VertexAttribute attributes[kMaxVertexAttribs];
  GLuint element_buffer = 0;
};

struct TextureImage {
  int width = 0;
  int height = 0;
  int depth = 1;
  int samples = 1;
  bool integer_signed = false;
  // 0xAABBGGRR per texel, one 32-bit word per pixel.  A multisample image
  // keeps the resolved plane here and the sample planes in |sample_rgba|.
  std::vector<uint32_t> rgba;
  std::vector<uint16_t> depth16;
  std::vector<uint8_t> stencil8;
  std::vector<uint32_t> sample_rgba;
  std::vector<uint16_t> sample_depth16;
  std::vector<uint8_t> sample_stencil8;
  // Floating point colour storage (RGBA, four floats per texel).  Non-empty
  // only for float internal formats.
  std::vector<float> float_rgba;
  // Integer colour storage, four 32-bit words per texel.
  std::vector<uint32_t> integer_rgba;
  std::vector<uint32_t> sample_integer_rgba;
};

class Texture {
 public:
  // Zero until the texture is first bound: a name may be bound to any target
  // only while its target is still unspecified.
  GLenum target = 0;
  GLint base_level = 0;
  GLint max_level = 1000;
  GLenum min_filter = GL_NEAREST_MIPMAP_LINEAR;
  GLenum mag_filter = GL_LINEAR;
  GLenum wrap_s = GL_REPEAT;
  GLenum wrap_t = GL_REPEAT;
  GLenum wrap_r = GL_REPEAT;
  GLenum compare_mode = GL_NONE;
  GLenum compare_func = GL_LEQUAL;
  float min_lod = -1000.0f;
  float max_lod = 1000.0f;
  GLenum swizzle_r = GL_RED;
  GLenum swizzle_g = GL_GREEN;
  GLenum swizzle_b = GL_BLUE;
  GLenum swizzle_a = GL_ALPHA;
  GLenum internal_format = GL_RGBA;
  int width = 0;
  int height = 0;
  int depth = 0;
  int levels = 0;
  int samples = 0;  // Zero for ordinary textures, otherwise the sample count.
  bool fixed_sample_locations = true;
  bool immutable = false;
  // Rendering into a texture stores rows top-down, while GL texture
  // coordinates address the first uploaded row as v = 0.  Attachments flip
  // the sampled coordinate until new image data is uploaded.
  bool render_target_flip_y = false;
  std::vector<TextureImage> images;  // level * faces + face

  int faces() const { return target == GL_TEXTURE_CUBE_MAP ? 6 : 1; }
  const TextureImage* image(int level, int face) const;
  TextureImage* mutable_image(int level, int face);
  void Resize(int levels, int faces);
  bool IsMipmapComplete() const;
  bool IsComplete() const;
};

class Renderbuffer {
 public:
  int width = 0;
  int height = 0;
  int samples = 0;
  GLenum format = GL_RGBA4;
  TextureImage image;
};

struct FramebufferAttachment {
  GLuint texture = 0;
  GLint level = 0;
  GLint layer = 0;
  GLenum cube_face = 0;
  GLuint renderbuffer = 0;

  bool bound() const { return texture != 0 || renderbuffer != 0; }
};

class Framebuffer {
 public:
  FramebufferAttachment colors[kMaxColorAttachments];
  FramebufferAttachment depth;
  FramebufferAttachment stencil;
  GLenum draw_buffers[kMaxColorAttachments] = {
      GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
      GL_COLOR_ATTACHMENT3};
  GLenum read_buffer = GL_COLOR_ATTACHMENT0;
  int draw_buffer_count = 1;
};

class ShaderObject {
 public:
  GLuint name = 0;
  GLenum type = 0;
  std::string source;
  std::unique_ptr<glsl::ShaderAst> ast;
  bool compiled = false;
  bool delete_status = false;
  std::string log;
};

class ProgramObject {
 public:
  std::vector<GLuint> shaders;
  std::unique_ptr<glsl::Program> program;
  bool linked = false;
  bool delete_status = false;
  std::string log;
  std::vector<std::pair<std::string, int>> bindings;
  std::vector<std::pair<std::string, int>> frag_data_bindings;
  std::vector<std::string> transform_feedback_varyings;
  GLenum transform_feedback_mode = GL_INTERLEAVED_ATTRIBS;
  bool program_binary_retrievable_hint = false;
};

class Query {
 public:
  GLenum target = 0;
  GLuint result = 0;
  bool active = false;
  uint32_t start_fragments = 0;
  uint32_t start_primitives = 0;
};

class Sampler {
 public:
  GLenum min_filter = GL_NEAREST_MIPMAP_LINEAR;
  GLenum mag_filter = GL_LINEAR;
  GLenum wrap_s = GL_REPEAT;
  GLenum wrap_t = GL_REPEAT;
  GLenum wrap_r = GL_REPEAT;
  GLenum compare_mode = GL_NONE;
  GLenum compare_func = GL_LEQUAL;
  float min_lod = -1000.0f;
  float max_lod = 1000.0f;
};

class SyncObject {
 public:
  bool signaled = true;
};

// Object table indexed by name; name 0 is reserved and never allocated.
template <typename T>
class ObjectTable {
 public:
  ObjectTable() { objects_.emplace_back(nullptr); }

  GLuint Create() {
    if (!free_.empty()) {
      const GLuint name = free_.back();
      free_.pop_back();
      objects_[name].reset(new T());
      return name;
    }
    objects_.emplace_back(new T());
    return static_cast<GLuint>(objects_.size() - 1);
  }

  T* Get(GLuint name) {
    return name < objects_.size() ? objects_[name].get() : nullptr;
  }

  // Binding a name that was never generated creates the object, as required
  // for the glBind* entry points.
  T* GetOrCreate(GLuint name) {
    if (name == 0)
      return nullptr;
    if (name >= objects_.size())
      objects_.resize(static_cast<size_t>(name) + 1);
    if (!objects_[name]) {
      auto it = std::find(free_.begin(), free_.end(), name);
      if (it != free_.end())
        free_.erase(it);
      objects_[name].reset(new T());
    }
    return objects_[name].get();
  }
  const T* Get(GLuint name) const {
    return name < objects_.size() ? objects_[name].get() : nullptr;
  }

  bool Delete(GLuint name) {
    if (name == 0 || name >= objects_.size() || !objects_[name])
      return false;
    objects_[name].reset();
    free_.push_back(name);
    return true;
  }

  // Removes the object from the table without recycling its name.  Used for
  // shaders that stay alive because a program still references their AST;
  // the caller must eventually call Reclaim() with the same name.
  std::unique_ptr<T> Release(GLuint name) {
    if (name == 0 || name >= objects_.size() || !objects_[name])
      return nullptr;
    return std::move(objects_[name]);
  }

  void Reclaim(GLuint name) {
    if (name != 0 && name < objects_.size() && !objects_[name])
      free_.push_back(name);
  }

  void Clear() {
    objects_.clear();
    objects_.emplace_back(nullptr);
    free_.clear();
  }

  int size() const { return static_cast<int>(objects_.size()) - 1; }

 private:
  std::vector<std::unique_ptr<T>> objects_;
  std::vector<GLuint> free_;
};

// ---------------------------------------------------------------------------
// Fixed function state.

struct DepthState {
  bool test = false;
  GLenum func = GL_LESS;
  bool mask = true;
  float range_near = 0.0f;
  float range_far = 1.0f;
};

struct BlendState {
  bool enabled = false;
  GLenum src_rgb = GL_ONE;
  GLenum dst_rgb = GL_ZERO;
  GLenum src_alpha = GL_ONE;
  GLenum dst_alpha = GL_ZERO;
  GLenum equation_rgb = GL_FUNC_ADD;
  GLenum equation_alpha = GL_FUNC_ADD;
  float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct StencilFace {
  GLenum func = GL_ALWAYS;
  int reference = 0;
  GLenum op_fail = GL_KEEP;
  GLenum op_depth_fail = GL_KEEP;
  GLenum op_pass = GL_KEEP;
};

struct StencilState {
  bool test = false;
  int mask = 0xFF;
  int write_mask = 0xFF;
  StencilFace front;
  StencilFace back;
};

struct CullState {
  bool enabled = false;
  GLenum mode = GL_BACK;
  GLenum front_face = GL_CCW;
};

struct PixelStoreState {
  int pack_alignment = 4;
  int unpack_alignment = 4;
  int pack_row_length = 0;
  int unpack_row_length = 0;
  int pack_skip_rows = 0;
  int unpack_skip_rows = 0;
  int pack_skip_pixels = 0;
  int unpack_skip_pixels = 0;
  int unpack_image_height = 0;
  int unpack_skip_images = 0;
};

struct IndexedBufferBinding {
  GLuint buffer = 0;
  size_t offset = 0;
  size_t size = 0;
  bool whole_buffer = false;
};

class TransformFeedback {
 public:
  IndexedBufferBinding buffers[kMaxTransformFeedbackBuffers];
  size_t cursors[kMaxTransformFeedbackBuffers] = {};
  bool active = false;
  bool paused = false;
  GLenum primitive_mode = 0;
  int vertex_count = 0;
  uint32_t primitives_written = 0;
};

struct State {
  Rect viewport;
  float depth_range[2] = {0.0f, 1.0f};

  bool scissor_test = false;
  Rect scissor;

  float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float clear_depth = 1.0f;
  int clear_stencil = 0;

  bool color_mask[4] = {true, true, true, true};
  bool depth_mask = true;
  GLuint stencil_write_mask = 0xFFu;
  GLuint stencil_mask = 0xFFu;

  DepthState depth;
  StencilState stencil;
  BlendState blend;
  CullState cull;

  // Fixed function features that are accepted but have no effect on a CPU
  // rasteriser; they are kept for glGet queries and for state validation.
  bool dither = true;
  float line_width = 1.0f;
  bool sample_coverage = false;
  float sample_coverage_value = 1.0f;
  bool invert_coverage = false;
  bool sample_alpha_to_coverage = false;
  bool sample_alpha_to_one = false;
  bool polygon_offset_fill = false;
  float polygon_offset_factor = 0.0f;
  float polygon_offset_units = 0.0f;
  bool rasterizer_discard = false;
  GLenum generate_mipmap_hint = GL_DONT_CARE;
  GLenum fragment_shader_derivative_hint = GL_DONT_CARE;
  bool primitive_restart = false;
  bool primitive_restart_fixed_index = false;
  GLuint primitive_restart_index = 0;

  GLenum active_texture = GL_TEXTURE0;
  GLuint textures[kMaxCombinedTextureUnits] = {};
  GLuint samplers[kMaxCombinedTextureUnits] = {};
  float current_attribute[kMaxVertexAttribs][4] = {};
  GLint current_attribute_integer[kMaxVertexAttribs][4] = {};
  GLuint array_buffer = 0;
  GLuint element_buffer = 0;
  GLuint copy_read_buffer = 0;
  GLuint copy_write_buffer = 0;
  GLuint pixel_pack_buffer = 0;
  GLuint pixel_unpack_buffer = 0;
  GLuint uniform_buffer = 0;
  IndexedBufferBinding uniform_buffers[kMaxUniformBufferBindings];
  GLuint transform_feedback = 0;
  GLuint transform_feedback_buffer = 0;
  GLuint vertex_array = 0;
  GLuint program = 0;
  GLuint framebuffer = 0;
  GLuint read_framebuffer = 0;
  GLenum default_draw_buffer = GL_BACK;
  GLenum default_read_buffer = GL_BACK;
  GLuint renderbuffer = 0;

  PixelStoreState pixel_store;
};

// ---------------------------------------------------------------------------

class Context {
 public:
  Context(int width, int height);
  ~Context();

  bool Resize(int width, int height);

  // Resolves the currently bound draw framebuffer into pointer form.
  RenderTarget GetDrawTarget();
  RenderTarget GetReadTarget();

  // The colour buffer that zglGetPixels() exposes.
  const uint8_t* color_bytes() const { return surface.rgba.data(); }

  void SetError(GLenum error) {
    if (error == GL_NO_ERROR)
      return;
    if (error_ == GL_NO_ERROR)
      error_ = error;
  }
  GLenum TakeError() {
    const GLenum error = error_;
    error_ = GL_NO_ERROR;
    return error;
  }

  Surface surface;  // Default framebuffer storage.
  State state;
  // Vertex array object 0.  GL ES 3 requires one implicit default object.
  VertexArray default_vertex_array;

  ObjectTable<Buffer> buffers;
  ObjectTable<VertexArray> vertex_arrays;
  ObjectTable<Texture> textures;
  // GL ES texture name 0 is a real default texture object.  Keep one per
  // texture family so state setup calls on name 0 have a destination.
  Texture default_textures[4];
  ObjectTable<Renderbuffer> renderbuffers;
  ObjectTable<Framebuffer> framebuffers;
  ObjectTable<ShaderObject> shaders;
  ObjectTable<ProgramObject> programs;
  ObjectTable<Query> queries;
  ObjectTable<TransformFeedback> transform_feedbacks;
  TransformFeedback default_transform_feedback;
  ObjectTable<Sampler> samplers;
  ObjectTable<SyncObject> syncs;

  ZglStats stats = {};
  std::string diagnostics;

  // Shaders deleted while a program still references them.  The GL
  // specification keeps their objects alive until the last reference goes
  // away, and the linked program borrows the parsed AST.
  void CollectOrphanedShaders();
  // Resolves a shader name, including shaders that were deleted while still
  // attached to a program.
  ShaderObject* FindShader(GLuint name);
  std::vector<std::pair<GLuint, std::unique_ptr<ShaderObject>>>
      orphaned_shaders;

 private:
  GLenum error_ = GL_NO_ERROR;
};

// Resolves a framebuffer name into pointers.  Name 0 is the default
// framebuffer; a non-zero name is looked up in |context|.
RenderTarget ResolveFramebuffer(Context* context, GLuint name);

// The transform feedback object bound to the context, or the default object.
TransformFeedback* CurrentTransformFeedback(Context* context);

// The context bound to the calling thread, or nullptr.
Context* CurrentContext();
void SetCurrentContext(Context* context);
int LiveContextCount();

// Sets the sticky error of the current context.
void SetError(GLenum error);

}  // namespace zgl

#endif  // ZGL_SRC_ZGL_CONTEXT_H_
