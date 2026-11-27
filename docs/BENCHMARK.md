# glmark2 benchmark

This page records how the optional EGL 1.4 adapter is used to run the generic
`glmark2-es2-null` benchmark against Glanceasy, and the numbers obtained on the
development machine. The adapter is a benchmark harness, not a platform
window-system layer: it implements the small surfaceless/pbuffer subset that
glmark2 needs and maps an EGL context to a Glanceasy context.

## Test setup

- glmark2 2023.01, `null-glesv2` flavor, built with Meson.
- `--off-screen=3`, 800x600 by default; the null flavor requires off-screen
  rendering because it has no native window.
- Frame end falls back to `glFinish` because Glanceasy does not implement
  `GL_ARB_sync`/`EGL_KHR_fence_sync`.
- The EGL config exposed by the adapter is RGBA8 + depth24 + stencil8, with
  `EGL_PBUFFER_BIT | EGL_WINDOW_BIT`.

## Building

Build Glanceasy with the adapter and the shared GLES entry points:

```sh
cmake -S . -B build-egl -DCMAKE_BUILD_TYPE=Release -DGLANCEASY_BUILD_EGL=ON
cmake --build build-egl -j
```

This produces `libglanceasy.so`, `libEGL.so` and the `libGLESv2.so` symlink in
`build-egl`.

Build glmark2 against the normal host image libraries:

```sh
meson setup build-glmark2 glmark2 --buildtype=release -Dflavors=null-glesv2
meson compile -C build-glmark2 -j
```

For the ARM32 cross build, the Luckfox uClibc sysroot has no libpng/libjpeg
development files. The test build enabled glmark2's in-tree `src/libpng`,
`src/libjpeg-turbo` and `src/zlib` targets instead of the host dependency lookup.
The GCC 8.3 toolchain also needs `-lstdc++fs` on the link line because
`std::filesystem` is not folded into `libstdc++` yet:

```ini
[built-in options]
cpp_link_args = ['-lstdc++fs']
```

## Running

Native x86_64:

```sh
export LD_LIBRARY_PATH=$PWD/build-egl
build-glmark2/src/glmark2-es2-null \
  --data-path glmark2/data --off-screen=3 --size=320x240 -b :duration=1
```

ARM32 under qemu-user:

```sh
export QEMU_LD_PREFIX=/path/to/luckfox/sysroot
export LD_LIBRARY_PATH=$PWD/build-egl-arm
qemu-arm-static -cpu cortex-a7 build-glmark2-arm/src/glmark2-es2-null \
  --data-path glmark2/data --off-screen=3 --size=320x240 -b :duration=1
```

`bench/run_glmark2.sh` wraps both forms and takes the paths from environment
variables.

## Results

The scores below are glmark2's integer average FPS. The full default scene list
was used with `duration=1`; this is a short smoke benchmark, not a tuned
performance run.

| Target | Resolution | glmark2 Score | Notes |
| --- | --- | ---: | --- |
| x86_64 host | 320x240 | 14 | Full default list |
| x86_64 host | 800x600 | 2 | Full default list |
| ARM32 Cortex-A7, qemu-user | 320x240 | 0 | Full default list; average FPS < 1 |
| ARM32 Cortex-A7, qemu-user | 800x600 | 0 | `build` scene alone: 1 FPS, 1.44 s/frame |

Selected per-scene numbers at 320x240 (x86_64 / ARM32 qemu-user):

| Scene | x86_64 FPS | ARM32 FPS | ARM32 frame time |
| --- | ---: | ---: | ---: |
| `build`, no VBO | 21 | 2 | 725 ms |
| `texture`, nearest | 38 | 2 | 670 ms |
| `shading`, phong | 3 | 1 | 7.11 s |
| `pulsar` | 111 | 3 | 364 ms |
| `conditionals`, steps=0 | 25 | 2 | 824 ms |
| `function`, low | 3 | 1 | 8.16 s |
| `terrain` | 1 | 1 | 387 s |

## RV1106G3 (native Cortex-A7) results

Measured on 2026-11-27 with the Luckfox RV1106G3 board: Cortex-A7 at 1.2 GHz,
`MemTotal` 185564 kB (256 MB part, ~181 MB visible after reserves), Linux
5.10.160 armv7l. glmark2 2023.01 `null-glesv2` was run off-screen with
`--off-screen=3`; the only variable between the "before" and "after" columns is
the `libglanceasy.so.0.1.0` binary in `LD_LIBRARY_PATH`.

Selected scenes at 320x240, `duration=2`:

| Scene | Before | After | Speed-up |
| --- | ---: | ---: | ---: |
| `build` | 903.3 ms/frame | 448.7 ms/frame | 2.01x |
| `texture`, nearest | 529.7 ms/frame | 250.7 ms/frame | 2.11x |
| `texture`, linear | 577.8 ms/frame | 275.1 ms/frame | 2.10x |
| `shading`, phong | 11716.9 ms/frame | 5293.7 ms/frame | 2.21x |
| `pulsar` | 525.5 ms/frame | 239.4 ms/frame | 2.19x |

At 800x600, `build` alone (`duration=2`): **1569.4 -> 804.5 ms/frame**
(1.95x). The full default list without `terrain` runs at score 1 after the
optimisation (`duration=1`); before it rounded down to 0.

A second pass added four targeted paths: fixed-point RGBA8 blending for
`GL_FUNC_ADD`, four-fragment batching in the triangle sink, NEON RGBA8 bilinear
sampling, and a four-lane NEON `pow`. The table below compares this pass against
the previous optimised library built from `f196950`, using the same glmark2
binary and the same board (320x240, `duration=2`, medians of three runs (two for 800x600)):

| Scene | f196950 | Second pass | Change |
| --- | ---: | ---: | ---: |
| `build` | 447.4 ms/frame | 440.0 ms/frame | 1.7% faster |
| `texture`, nearest | 250.5 ms/frame | 241.5 ms/frame | 3.7% faster |
| `texture`, linear | 252.6 ms/frame | 242.8 ms/frame | 4.0% faster |
| `shading`, phong | 5268.4 ms/frame | 5267.9 ms/frame | unchanged |
| `pulsar` | 239.8 ms/frame | 222.2 ms/frame | 7.9% faster |
| `desktop` | 3048.7 ms/frame | 2784.5 ms/frame | 9.5% faster |
| `build`, 800x600 | 804.4 ms/frame | 786.7 ms/frame | 2.3% faster |

The phong shader does not call a four-component `pow`, so it does not exercise
the new vector path; its frame time is unchanged. The `Pow4` helper itself
measures 2.21x faster than four scalar `Pow` calls on the board (200000
iterations, 72.9 ms versus 161.0 ms), including its scalar fallback checks. The
texture scenes exercise both batching and bilinear sampling; `desktop` and
`pulsar` are the blend-heavy scenes.

Implementation notes for the second pass:

- RGBA8 blending evaluates the common `GL_FUNC_ADD` source/destination factors
  from 8-bit bytes, with a nearest-integer divide by 255. Factors that need the
  blend colour, sRGB attachments, masked writes and subtract/min/max equations
  still take the float path. The byte path can differ from the float path by at
  most one LSB on an 8-bit attachment.
- The triangle rasteriser accumulates up to four `Fragment`s and calls
  `FragmentSink::ShadeBatch`. `Pipeline` implements the batch entry point so
  target validation and scissor setup are hoisted out of the per-fragment
  virtual call. MSAA, line and point paths still use the single-fragment
  interface.
- The RGBA8 bilinear path loads four packed texels, converts the four channels
  to floats in four lanes, interpolates horizontally with a reversed-lane
  subtraction, then blends the two rows. The generic sampler remains for other
  formats and wrap/mip combinations.
- `Pow4` computes `exp2(y * log2(x))` with four-lane NEON polynomials and falls
  back to scalar `Pow` when any lane has a non-positive or subnormal base, a
  non-finite exponent, or an exponent result above 127. A regression test covers
  a fractional negative exponent, where the integer exponent must be floored
  rather than truncated.

The main wins in the first pass were not new approximate maths, but removing
work from the per-fragment path:

- `Value` inline storage was reduced from 64 to 16 floats. A scalar temporary
  was 600+ bytes and was copied for nearly every GLSL operation; a mat4 fits in
  the new 16-float inline block, while arrays and structures use the existing
  aggregate storage.
- `Type::components()` is now inline in the header instead of an out-of-line
  call made once per component loop.
- All `GLANCEASY_DEBUG_*` switches are cached at their call sites; previously
  `getenv()` was executed in per-pixel branches (8.5% of the phong callgrind
  profile).
- Disabled vertex attributes are copied from a per-draw default block, and only
  enabled attributes are fetched per vertex.
- Triangles wholly inside the frustum skip polygon clipping and duplicate
  removal.
- The rasteriser shades eight-pixel horizontal runs without per-pixel edge
  tests when both run endpoints are strictly interior.
- RGBA8 nearest/linear sampling has a direct path that skips the generic
  format/mip-level dispatcher.
- Depth compare, when a depth buffer and no `gl_FragDepth` are present, compares
  the incoming and stored 16-bit values directly.
- Four-lane add/sub use NEON on ARMv7 (`vaddq_f32`/`vsubq_f32`); four-lane
  sin/cos, reciprocal square root and the polynomial `exp2`/`log2`/`pow`
  approximations were already present.

These are smoke measurements, not a tuned benchmark: one board, two-second
scenes, and thermal state can move the numbers by a few percent.

## Interpretation and caveats

- qemu-user interprets every ARM instruction; it is useful for functional
  validation, not for estimating RV1106 frame rates. A native Cortex-A7 run is
  required before drawing performance conclusions.
- glmark2 reports an integer score. A scene that does not complete a full
  second's worth of frames can contribute less than 1 FPS, so the aggregate
  score for the emulated ARM build rounds down to zero even though individual
  scenes run at 1-4 FPS.
- `terrain` is pathological under qemu-user in this configuration: it prints
  `your 131072x1 screen size is bogus. expect trouble` and takes about 6.4
  minutes for one frame. Exclude it when comparing interactive workloads.
- `buffer`'s `update-method=map` variants are skipped because Glanceasy does not
  advertise `GL_OES_mapbuffer`.
- `shadow` and `refract` are skipped because the adapter does not advertise a
  depth-texture extension; their depth-only FBO setup would be incomplete.

## Third pass: more aggressive Cortex-A7 work

The third pass focused on the per-fragment JIT/interpreter overhead rather than
on more approximate maths. Measurements below are from the RV1106G3 board
(320x240, `duration=1`, off-screen, `LD_LIBRARY_PATH=/tmp/glmark2/lib`). The
reference column is the previous published library at `fd0d482`; the new
column is the current working tree. The scene list is the same non-`terrain`
set used above.

| Scene | `fd0d482` | current | Speed-up |
| --- | ---: | ---: | ---: |
| `build` | 454.4 ms/frame | 179.4 ms/frame | 2.53x |
| `texture` | 243.0 ms/frame | 162.8 ms/frame | 1.49x |
| `shading` | 855.1 ms/frame | 331.5 ms/frame | 2.58x |
| `bump` | 420.0 ms/frame | 106.9 ms/frame | 3.93x |
| `effect2d` | 1343.5 ms/frame | 877.8 ms/frame | 1.53x |
| `pulsar` | 146.7 ms/frame | 107.0 ms/frame | 1.37x |
| `desktop` | 2812.2 ms/frame | 1227.7 ms/frame | 2.29x |
| `ideas` | 558.1 ms/frame | 486.4 ms/frame | 1.15x |
| `jellyfish` | 1434.5 ms/frame | 1153.5 ms/frame | 1.24x |
| `conditionals` | 490.7 ms/frame | 347.6 ms/frame | 1.41x |
| `function` | 3189.6 ms/frame | 356.4 ms/frame | 8.95x |
| `loop` | 2878.8 ms/frame | 355.4 ms/frame | 8.10x |

At 800x600, `build` moved from 786.7 ms/frame (`fd0d482`) to 410.9 ms/frame.
`desktop` is still dominated by its 121-tap two-dimensional Gaussian shadow
blur and remains around 1.2 s/frame at 320x240; it is the clearest remaining
outlier. `terrain` is still excluded.

Implementation notes:

- Fragment interpolation only computes `dFdx`/`dFdy` values when the fragment
  shader actually uses derivatives or implicit-LOD texture lookups. `phong`,
  for example, no longer interpolates derivative vectors it never reads.
- The JIT builtin call path reuses a thread-local `Value` array instead of
  allocating one vector per builtin call, then inlines `normalize`, `dot`,
  `length`, `reflect`, scalar `pow`, `min` and `max` helpers that read the
  shader frame slots directly.
- JIT loops are now emitted with SLJIT labels, including `break` and
  `continue`.
- Integer loop counters and uniform integers are lowered as floats when the
  shader only uses exact small-integer add/subtract/multiply, comparisons and
  increment/decrement. Bitwise, modulo and integer division still force the
  interpreter.
- User functions that are called from the shader are inlined into the JIT
  frame: parameters and locals are allocated as JIT slots, arguments are copied
  into the parameter slots, and `return` jumps to the inline end label.
- A fragment shader of the form `vec4 t = texture2D(s, v); gl_FragColor = t;`
  bypasses the interpreter/JIT entirely and samples the cached RGBA8 image
  directly.
- Texture bindings are cached only for the sampler units the program actually
  uses. Units outside that set fall back to a one-off lookup, so literal texture
  units remain correct.
- The common `GL_SRC_ALPHA` / `GL_ONE_MINUS_SRC_ALPHA` blend case has a
  direct byte path in `StoreFragmentColor`.

Validation on the board with the current library: `glanceasy_unittests` still
reports `183 tests, 79 failed checks` and `glanceasy_visual_tests` still reports
`15 visual cases, 1 failed` (`02_triangle`). Both match the pre-existing
baseline.

## Sixth pass: 40x30 reaches 30 fps for the raster-heavy glmark2 scenes

The fourth and fifth passes added conservative pre-culling and a narrower
texture-parameter copy path.  The sixth pass targeted the two remaining costs
at the internal 40x30 resolution: generic JIT builtin calls and the per-triangle
setup of very small triangles.

Measured on the RV1106G3 board, off-screen, `duration=1`, 40x30:

| Scene | Frame time | FPS |
| --- | ---: | ---: |
| `build` | 9.3 ms | 108 |
| `texture` | 3.3 ms | 308 |
| `shading` | 18.9 ms | 54 |
| `bump` | 7.5 ms | 134 |
| `effect2d` | 16.0 ms | 63 |
| `pulsar` | 2.7 ms | 365 |
| `conditionals` | 30.2 ms | 34 |
| `function` | 30.5 ms | 33 |
| `loop` | 30.9 ms | 33 |
| `desktop` (exact blur) | 22.5 ms | 45 |

At 60x45 the same scenes are `build` 11.8 ms, `texture` 6.8 ms, `shading`
22.6 ms, `bump` 10.3 ms, `effect2d` 33.6 ms, `conditionals` 35.0 ms,
`function` 35.2 ms, `loop` 35.9 ms and `desktop` 46.5 ms.  At 320x240 the
corresponding frame times are `build` 17.4 ms, `shading` 33.7 ms,
`conditionals` 225 ms, `function` 225 ms, `loop` 227 ms and `desktop`
1212 ms.  `jellyfish` remains vertex-bound (13.2k vertices with two `sin`
calls, four mat4 multiplies and several normalisations per vertex) and is
around 6 fps at 40x30 even after the fast builtins.

Implementation notes:

- `fract` is now a direct JIT helper that uses integer truncation plus a
  correction instead of the Cortex-A7 `floorf` library path.  `sin`, `cos`,
  `abs` and `distance` also have direct helpers, reusing
  `fastmath::Sin/Cos/Sqrt`.  This cut `conditionals` from about 66 ms to
  36 ms at 40x30.
- `Pipeline::FetchAttributes` copies plain float attributes with `memcpy`
  and skips the integer payload for them.
- `Rasterizer::DrawTriangle` uses truncation-based floor/ceil for the
  bounding box and sub-pixel quantisation (no libm `floor`/`ceil` on Cortex-A7).
- Triangles whose bounding box is at most 24 pixels take a direct per-pixel
  path instead of the eight-pixel run machinery.  This is the change that
  moved `conditionals`, `function` and `loop` from 29-31 fps to 33-34 fps
  at 40x30.
- `GLANCEASY_APPROX_CULL_MVP=1` enables the experimental raw-attribute
  pre-cull for shaders that build a local `vec4` before multiplying by
  `ModelViewProjectionMatrix`/`MVP`/`uWorldViewProj`.  It is off by default
  because the shader may displace vertices; it is useful for experiments on
  `jellyfish`, where an area threshold of 1.0 raises the scene to about
  62 fps at 40x30 at the cost of losing most of the mesh.

Validation remains at the same baseline after these changes.



## Seventh pass: fragment fixed cost and exact desktop blur

This pass removed the remaining per-fragment setup in the JIT and added a
direct exact path for the separable blur used by glmark2 `desktop`.

Measured on the RV1106G3 board, off-screen, `duration=1`, 40x30:

| Scene | Frame time | FPS |
| --- | ---: | ---: |
| `build` | 9.34 ms | 108 |
| `texture` | 2.49 ms | 403 |
| `shading` | 19.32 ms | 52 |
| `bump` | 5.88 ms | 171 |
| `effect2d` | 5.47 ms | 183 |
| `pulsar` | 2.25 ms | 446 |
| `conditionals` | 24.65 ms | 41 |
| `function` | 25.13 ms | 40 |
| `loop` | 24.64 ms | 41 |
| `desktop` (exact blur) | 13.57 ms | 74 |
| `jellyfish` | 145.8 ms | 7 |

At 60x45: `build` 11.66 ms, `texture` 5.25 ms, `shading` 22.22 ms,
`bump` 8.22 ms, `effect2d` 11.02 ms, `pulsar` 4.11 ms, `conditionals`
27.65 ms, `function` 28.50 ms, `loop` 28.24 ms and `desktop` 28.01 ms
(36 fps).  Exact desktop therefore clears 30 fps at 40x30 and 60x45.

Implementation notes:

- `ShaderJit::Impl::EmitPrologue()` no longer clears every frame slot.
  `ShaderJit::Impl::Run()` performs one host `memset` of the local slot area
  instead.  This removes hundreds of zero stores per fragment for shaders
  with many temporary slots.
- `Evaluator::PrepareBuiltins()` writes built-in values directly into the
  global `Value` storage and skips built-ins that the shader does not
  reference.  This avoids constructing and destroying a `Value` (with its
  two vectors) for every built-in on every fragment.
- Fast JIT helpers now cover `fract`, `sin`, `cos`, `abs` and `distance`.
- Texture coordinate and LOD floor/ceil calls in the texture module use a
  truncating helper instead of the Cortex-A7 double-precision libm path,
  including ordinary 2D sampling, repeat/mirror wrap, mip level selection,
  array layer selection and 3D sampling.
- `Program` now recognises the generated `desktop` convolution shader:
  `result` is a weighted sum of `texture2D(Texture0, TextureCoord +
  vec2(dx*TextureStepX, dy*TextureStepY))` taps, followed by optional scale
  and `gl_FragColor = vec4(result.xyz, 1.0)`.  `Pipeline::ShadeFragment`
  samples those taps directly through the cached RGBA8 sampler instead of
  entering the JIT once per tap.  The emitted values were compared against
  the JIT result on the board and match within float rounding (at most
  `5.96e-8` per channel); the exact path is enabled only for this source
  shape and falls back to the JIT for anything else.

Validation after the cleanup: `glanceasy_unittests` `183 tests, 79 failed
checks`; `glanceasy_visual_tests` `15 visual cases, 1 failed` (`02_triangle`),
the same pre-existing baseline on both host and board.
