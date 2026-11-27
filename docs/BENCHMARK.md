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

The main wins on this board were not new approximate maths, but removing work
from the per-fragment path:

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
