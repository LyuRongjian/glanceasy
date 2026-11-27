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
qemu-arm-static build-glmark2-arm/src/glmark2-es2-null \
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
| `build`, no VBO | 21 | 2 | 714 ms |
| `texture`, nearest | 38 | 2 | 592 ms |
| `shading`, phong | 3 | 1 | 6.65 s |
| `pulsar` | 111 | 4 | 329 ms |
| `conditionals`, steps=0 | 25 | 2 | 826 ms |
| `function`, low | 3 | 1 | 7.94 s |
| `terrain` | 1 | 1 | 384 s |

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
