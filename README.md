# ZCat-GLES

A standalone, CPU-only implementation of OpenGL ES for ARM32 Cortex-A SoCs.

ZCat-GLES rasterises every draw call in software. It is derived from the ZCat
software painter that shipped inside the WebGlance browser prototype, but the
GLES front end has been rewritten from scratch: no GPU driver, no EGL, no
platform window system, and no x86-specific code. The library exposes the
Khronos GLES 2.0 and 3.0 entry points, keeps its own RGBA8 colour buffer, a
16-bit depth buffer and an 8-bit stencil buffer, and renders into memory that
the embedder can blit or upload wherever it likes.

The code and comments follow the Chromium C++ style, and all comments are in
English.

## Status

| Area | State |
| --- | --- |
| GLES 2.0 API | Complete: buffers, vertex arrays, textures, framebuffers, shaders, state, draws |
| GLES 3.0 API | Complete for the interpreted subset: VAOs, instancing, primitive restart, queries, fences, sampler objects, multiple render targets, blit, invalidate |
| GLSL | Interpreter for ES 1.00 / 3.00 shaders, see [docs/GLSL_SUBSET.md](docs/GLSL_SUBSET.md) |
| Transform feedback, compute, SSBO, image load/store | Accepted stubs; report `GL_INVALID_OPERATION` where output would change |
| Tessellation / geometry shaders | Not part of GLES; entry points are stubs |
| Platform integration | None by design; the embedder owns the surface |

## Building on a desktop host

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/zgl_unittests
```

The host build is the development configuration and is also used for the
AddressSanitizer and coverage runs:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
./build-asan/zgl_unittests
```

On Windows the repository is built from WSL; the Windows shell is only used to
launch `wsl.exe`.

## Building for ARM32 with the Android NDK

`tools/build_android.sh` cross-compiles the library and the unit test binary for
`armeabi-v7a` with NEON enabled and a static C++ runtime:

```sh
ANDROID_NDK_ROOT=$HOME/android-ndk-r27c tools/build_android.sh
```

The result is a statically linked `ELF 32-bit ARM EABI5` executable. The script
uses `-mfpu=neon -mfloat-abi=softfp`, keeps the ARMv7-A baseline, and does not
rely on any x86 SSE code.

## Running the ARM32 binary under QEMU

Static bionic executables need a 32-byte aligned TLS segment; the NDK linker
does not always provide one, and bionic aborts at start-up under qemu-user.
`tools/run_qemu_arm.sh` patches the `PT_TLS` program header and then runs the
binary:

```sh
tools/run_qemu_arm.sh build-arm/zgl_unittests
```

`QEMU_ARM_STATIC` selects the emulator (default `~/qemu-local/usr/bin/qemu-arm-static`).
A system-mode QEMU installation can also boot a full ARM image, but the
user-mode emulator is what the test suite is verified against.

## Testing and coverage

The unit tests are registered through a small in-tree harness
(`tests/zgl_test.h`) and cover the rasteriser, texture sampling, the GLSL
interpreter, every implemented GL entry point, the error paths of the state
machine and the compatibility stubs.

```sh
tools/coverage.sh build-cov
```

The script configures a gcov build, runs the tests, and prints per-file line
coverage into `build-cov/coverage/summary.txt`. The current tree executes 148
tests with zero failed checks; line coverage is reported as 75.1% of 8590
instrumented lines. The uncovered remainder is dominated by defensive error
branches, unsupported-format rejection paths and the GPU-only stubs, which are
listed in the coverage summary rather than hidden.

## Directory layout

```
include/GLES2, include/GLES3   Khronos headers (unmodified)
include/zgl/zgl.h              Public C API of the software renderer
src/                           Context, rasteriser, texture pipeline, draw pipeline
src/glsl/                      Lexer, parser and interpreter for the GLSL subset
src/zgl_api_*.cpp              GLES entry points
tests/                         Unit tests and the test harness
tools/                         NDK build, QEMU runner, TLS patch, coverage
docs/                          GLSL subset and porting notes
```

## Using the library

```c
#include "zgl/zgl.h"

zglCreateContext(320, 240);
/* Call the GLES entry points as usual.  Include GLES3/gl32.h first. */
zglDestroyContext();
```

`include/zgl/zgl.h` also exposes `zglGetPixels`, `zglGetPixels32`,
`zglGetStats` and `zglGetDiagnostics` for embedders that need the rendered
image, draw statistics or the last shader log.

## Known limitations

- Fragment shaders may write one colour output; `gl_FragData` and multiple
  render targets are supported, but per-output `out` variables beyond the first
  are ignored.
- Arrays, structures, uniform blocks, `out`/`inout` parameters and recursive
  functions are outside the interpreted GLSL subset.
- Texture images are stored top-down; `v = 0` addresses the first stored row.
- `dFdx`, `dFdy` and `fwidth` return zero, and implicit LOD selection is not
  computed; `textureLod` selects mip levels explicitly.
- Line and point primitives that land exactly on the bottom or right viewport
  edge clip asymmetrically because NDC `y = -1` maps to `height`.
- `glGetTexImage` is not part of GLES and is not provided.

## License

BSD-3-Clause. See [LICENSE](LICENSE).

---

# ZCat-GLES（中文）

一个独立的、纯 CPU 的 OpenGL ES 实现，面向 ARM32 Cortex-A SoC。

ZCat-GLES 的每一次绘制都在软件中完成光栅化。它源自 WebGlance 浏览器原型中的
ZCat 软件画家，但 GLES 前端是完全重写的：不依赖 GPU 驱动、不依赖 EGL、不依赖
平台窗口系统，也没有任何 x86 SSE 专用代码。库对外提供 Khronos GLES 2.0 / 3.0
入口，自带 RGBA8 颜色缓冲、16 位深度缓冲和 8 位模板缓冲，渲染结果留在内存中，
由集成方自行上屏或上传。

代码与注释遵循 Chromium C++ 风格，所有注释均为英文。

## 状态

| 范围 | 状态 |
| --- | --- |
| GLES 2.0 API | 完整：缓冲、顶点数组、纹理、帧缓冲、着色器、状态、绘制 |
| GLES 3.0 API | 解释器子集内完整：VAO、instancing、primitive restart、query、fence、sampler object、多重渲染目标、blit、invalidate |
| GLSL | 支持 ES 1.00 / 3.00 着色器解释执行，见 [docs/GLSL_SUBSET.md](docs/GLSL_SUBSET.md) |
| Transform feedback、compute、SSBO、image load/store | 纯桩；会影响输出的调用返回 `GL_INVALID_OPERATION` |
| 细分 / 几何着色器 | 不属于 GLES；入口为桩 |
| 平台集成 | 有意不做；表面归集成方所有 |

## 桌面宿主机编译

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/zgl_unittests
```

Windows 上请在 WSL 中执行上述命令，Windows 侧只用 `wsl.exe` 启动。

## 使用 Android NDK 交叉编译 ARM32

```sh
ANDROID_NDK_ROOT=$HOME/android-ndk-r27c tools/build_android.sh
```

脚本面向 `armeabi-v7a`，开启 NEON，使用静态 C++ 运行库，产物是
`ELF 32-bit ARM EABI5` 静态可执行文件；编译参数为
`-mfpu=neon -mfloat-abi=softfp`，保持 ARMv7-A 基线，不依赖任何 x86 SSE 代码。

## 在 QEMU 下运行 ARM32 产物

NDK 生成的静态 bionic 程序需要 32 字节对齐的 TLS 段，否则 bionic 在 qemu-user
下启动即中止。`tools/run_qemu_arm.sh` 会先修补 `PT_TLS` 程序头，再启动模拟器：

```sh
tools/run_qemu_arm.sh build-arm/zgl_unittests
```

`QEMU_ARM_STATIC` 可指定模拟器路径，默认
`~/qemu-local/usr/bin/qemu-arm-static`。

## 测试与覆盖率

单元测试通过仓库内的小型注册式框架（`tests/zgl_test.h`）组织，覆盖光栅化、
纹理采样、GLSL 解释器、全部已实现的 GL 入口、状态机错误路径以及兼容桩。

```sh
tools/coverage.sh build-cov
```

脚本会配置 gcov 构建、运行测试，并把逐文件行覆盖率写入
`build-cov/coverage/summary.txt`。当前共 148 个测试、0 个失败断言；行覆盖率
为 8590 行中的 75.1%。未覆盖部分主要是防御性错误分支、不支持的格式拒绝路径
以及 GPU 专用桩，均如实列在覆盖率汇总中。

## 目录结构

```
include/GLES2, include/GLES3   Khronos 原始头文件
include/zgl/zgl.h              软件渲染器的公共 C API
src/                           上下文、光栅化、纹理管线、绘制管线
src/glsl/                      GLSL 子集的词法、语法与解释执行
src/zgl_api_*.cpp              GLES 入口实现
tests/                         单元测试与测试框架
tools/                         NDK 构建、QEMU 运行、TLS 修补、覆盖率
docs/                          GLSL 子集与移植说明
```

## 使用方式

```c
#include "zgl/zgl.h"

zglCreateContext(320, 240);
/* 之后正常调用 GLES 入口，需先包含 GLES3/gl32.h。 */
zglDestroyContext();
```

`include/zgl/zgl.h` 还提供 `zglGetPixels`、`zglGetPixels32`、`zglGetStats` 与
`zglGetDiagnostics`，供集成方取回渲染结果、绘制统计或最近的着色器日志。

## 已知限制

- 片元着色器只使用第一个颜色输出；`gl_FragData` 与多重渲染目标可用，但除第一个
  之外的 `out` 变量会被忽略。
- 数组、结构体、uniform block、`out`/`inout` 参数、递归函数不在解释器子集内。
- 纹理图像按 top-down 存储，`v = 0` 对应第一条存储的行。
- `dFdx`、`dFdy`、`fwidth` 恒为 0，不计算隐式 LOD；`textureLod` 可显式选级。
- 恰好落在视口底边或右边的线与点会被非对称裁剪，因为 NDC 的 `y = -1` 映射到
  `height`。
- 不提供 `glGetTexImage`（它不属于 GLES）。

## 许可证

BSD-3-Clause，见 [LICENSE](LICENSE)。
