<p align="center">
  <a href="README.md"><img src="docs/badge-en.svg" alt="English"></a>
  <a href="README.zh-CN.md"><img src="docs/badge-zh.svg" alt="简体中文"></a>
</p>

# ZCat-GLES（中文）

一个独立的、纯 CPU 的 OpenGL ES 实现，面向 ARM32 Cortex-A SoC。

ZCat-GLES 的每一次绘制都在软件中完成光栅化。它源自 WebGlance 浏览器原型中的
ZCat 软件画家，但 GLES 前端是完全重写的：不依赖 GPU 驱动、不依赖 EGL、不依赖
平台窗口系统，也没有任何 x86 SSE 专用代码。库对外提供 Khronos GLES 2.0 / 3.0
入口，自带 RGBA8 颜色缓冲、16 位深度缓冲和 8 位模板缓冲，渲染结果留在内存中，
由集成方自行上屏或上传。

代码与注释遵循 Chromium C++ 风格，所有注释均为英文。

参考目标芯片是 Rockchip RV1106（单核 Cortex-A7，带 NEON）。下图由 ARM32 构建
在 `qemu-arm-static -cpu cortex-a7` 下实际渲染得到：

![ZCat-GLES 渲染的彩色立方体](docs/screenshot.png)

## 可视化测试图库

`tests/visual` 包含十五个渲染用例。每个用例都通过公开 GLES API 自建着色器和
顶点数据，渲染一帧 640x480 画面，校验少量像素，并把画面写成 PPM 文件。下面的
图片全部是 ARM32 构建在 `qemu-arm-static -cpu cortex-a7` 下的真实输出；解释器
与 SLJIT 后端的输出相对宿主机构建均在 1 个 8 位 LSB 以内，这正是该套件提供的
回归保障。

这十五个用例构成完整的可视化集合，每个用例一张截图。API 层测试共 181 个，
其中不少只验证状态迁移、非法枚举和错误路径，根本不会触碰帧缓冲，因此这些
用例只用代码断言，不做截图。

| 用例 | 截图 | 覆盖内容 |
| --- | --- | --- |
| `01_gradient` | <img src="docs/visual/01_gradient.png" width="320"> | 全屏三角形，顶点色插值加片元着色器暗角 |
| `02_triangle` | <img src="docs/visual/02_triangle.png" width="320"> | 两个 client 数组构造平面三角形，逐顶点 RGB 与重心插值 |
| `03_indexed_quad` | <img src="docs/visual/03_indexed_quad.png" width="320"> | `glDrawElements` 加 16 位索引缓冲，两个三角形拼成四边形 |
| `04_depth_test` | <img src="docs/visual/04_depth_test.png" width="320"> | 近处红三角通过深度缓冲遮挡远处蓝三角 |
| `05_alpha_blend` | <img src="docs/visual/05_alpha_blend.png" width="320"> | 不透明蓝色底层上叠加半透明红色覆盖层，`GL_SRC_ALPHA` / `GL_ONE_MINUS_SRC_ALPHA` |
| `06_texture_nearest` | <img src="docs/visual/06_texture_nearest.png" width="320"> | 8x8 棋盘纹理以 `GL_NEAREST` 放大，保留方块状纹素边界 |
| `07_texture_linear` | <img src="docs/visual/07_texture_linear.png" width="320"> | 同一纹理改用 `GL_LINEAR`，呈现平滑的双线性过渡 |
| `08_mipmap` | <img src="docs/visual/08_mipmap.png" width="320"> | 256x256 棋盘，左侧 `textureLod` 取 LOD 0，右侧取 LOD 3 |
| `09_culling` | <img src="docs/visual/09_culling.png" width="320"> | 背面剔除：红色背面被丢弃，只剩绿色正面 |
| `10_scissor` | <img src="docs/visual/10_scissor.png" width="320"> | 裁剪矩形把蓝色限制在红色画面的中央区域 |
| `11_stencil` | <img src="docs/visual/11_stencil.png" width="320"> | 菱形模板遮罩把渐变限制在遮罩内部 |
| `12_instancing` | <img src="docs/visual/12_instancing.png" width="320"> | 一次 `glDrawArraysInstanced` 绘制九个四边形，逐实例偏移与颜色 |
| `13_framebuffer` | <img src="docs/visual/13_framebuffer.png" width="320"> | 离屏帧缓冲颜色纹理，再合成回屏幕 |
| `14_glsl_pattern` | <img src="docs/visual/14_glsl_pattern.png" width="320"> | 完全在片元着色器中计算的同心圆环 |
| `15_perspective_cube` | <img src="docs/visual/15_perspective_cube.png" width="320"> | 透视投影立方体，逐面颜色加深测试 |

在宿主机上运行图库：

```sh
cmake --build build --target zgl_visual_tests
./build/zgl_visual_tests visual-out 640 480
```

第二个参数是输出目录，每个用例会在该目录写一个 PPM 文件。

## 状态

| 范围 | 状态 |
| --- | --- |
| GLES 2.0 API | 完整：缓冲、顶点数组、纹理、帧缓冲、着色器、状态、绘制 |
| GLES 3.0 API | 核心子集：VAO、instancing、primitive restart、query、fence、sampler object、blit、invalidate、uniform block、transform feedback、4x MSAA、ETC2/EAC 压缩纹理与浮点纹理采样 |
| Uniform block | `layout(std140)` 块，支持标量、向量、矩阵、数组与结构体成员，支持可选实例名、每程序 binding point 和带范围的 `GL_UNIFORM_BUFFER` 绑定 |
| Transform feedback | 命名的 feedback 对象、interleaved / separate varyings、`GL_TRANSFORM_FEEDBACK_BUFFER` 的 base / range 绑定、begin/end/pause/resume、primitive query 与 `glDrawTransformFeedback`。varying 仅支持浮点标量或向量；尚不支持 instanced feedback 绘制 |
| GLSL | 支持 ES 1.00 / 3.00 的解释执行与 SLJIT 分层编译；不支持的构造按 stage 回退，见 [docs/GLSL_SUBSET.md](docs/GLSL_SUBSET.md) |
| 着色器数学 | 超越函数使用 minimax 近似；ARMv7 上 `sin` / `cos` 每次并行计算四个 lane（NEON）。整数、深度、模板与混合路径保持精确 |
| 固定功能 SIMD | `dot` / `normalize`、颜色/深度/模板的 clear 填充、同尺寸 blit 的行拷贝在 ARMv7 上走四 lane NEON；部分颜色掩码仍走标量路径 |
| 多重渲染目标 | 最多四个颜色输出，可用 `layout(location = N)`、`gl_FragData[0..3]` 或顺序 `out` 变量；`glDrawBuffers` 决定每个输出写到哪个附件 |
| compute、SSBO、image load/store | 纯桩；会影响输出的调用返回 `GL_INVALID_OPERATION` |
| 细分 / 几何着色器 | 不属于 GLES；入口为桩 |
| 平台集成 | 有意不做；表面归集成方所有 |

## 桌面宿主机编译

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/zgl_unittests
```

宿主机构建是开发配置，也用于 AddressSanitizer 与覆盖率运行：

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan
./build-asan/zgl_unittests
```

ARM32 目标在 Linux 上使用 Luckfox SDK 工具链与 qemu-user 验证，不以 Windows
作为构建或测试环境。

## 使用 Luckfox SDK 编译 RV1106 目标

`tools/build_luckfox_arm.sh` 使用
[Luckfox Pico RV1106 SDK](https://wiki.luckfox.com/zh/Luckfox-Pico-RV1106/SDK)
自带的交叉编译器。把 `LUCKFOX_TOOLCHAIN` 指向 SDK 工具链目录，或把
`LUCKFOX_SDK` 指向 SDK 根目录：

```sh
LUCKFOX_TOOLCHAIN=$HOME/luckfox-pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf \
  tools/build_luckfox_arm.sh build-rv1106
```

脚本使用 `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`，并静态链接 SDK
的 uClibc 与 libstdc++。

## 使用 Android NDK 交叉编译 ARM32

`tools/build_android.sh` 面向 `armeabi-v7a` 交叉编译库与测试程序，开启 NEON，
使用静态 C++ 运行库：

```sh
ANDROID_NDK_ROOT=$HOME/android-ndk-r27c tools/build_android.sh
```

产物是静态链接的 `ELF 32-bit ARM EABI5` 可执行文件。编译参数为
`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=softfp`，与 RV1106 的 Cortex-A7
核心匹配，同时仍适用于整个 ARMv7-A 系列，不依赖任何 x86 SSE 代码。

## 在 QEMU 下运行 ARM32 产物

`tools/run_qemu_arm.sh` 以 `qemu-arm-static -cpu cortex-a7` 运行任意 ARM32 ELF。
Luckfox 的 uClibc 产物无需修补；Android NDK 的 bionic 产物需要 32 字节对齐的
TLS 段，此时设置 `ZGL_PATCH_TLS=1`：

```sh
tools/run_qemu_arm.sh build-rv1106/zgl_unittests
tools/run_qemu_arm.sh build-rv1106/zgl_visual_tests visual-out 640 480
```

`QEMU_ARM_STATIC` 可指定模拟器路径；未设置时从 `PATH` 中查找
`qemu-arm-static`。系统模式的 QEMU 也可以启动完整 ARM 镜像，但本套件验证使用
的是用户模式模拟器。

## 渲染示例

`examples/render_cube.cpp` 使用公开 API 绘制一个带透视投影和深度测试的立方体，
渲染到离屏缓冲后写出二进制 PPM 文件：

```sh
cmake --build build --target zgl_cube_demo
./build/zgl_cube_demo cube.ppm 800 600
tools/run_qemu_arm.sh build-rv1106/zgl_cube_demo cube.ppm 800 600
```

`docs/screenshot.png` 由第二条命令生成；在开发机上 qemu-user 耗时约 8 秒，这不
代表 RV1106 实机性能。

## 测试与覆盖率

单元测试通过仓库内的小型注册式框架（`tests/zgl_test.h`）组织，覆盖光栅化、
纹理采样、GLSL 解释器、全部已实现的 GL 入口、状态机错误路径以及兼容桩。

```sh
tools/coverage.sh build-cov
```

脚本会配置 gcov 构建、运行测试，并把逐文件行覆盖率写入
`build-cov/coverage/summary.txt`。当前共 181 个 API 测试、0 个失败断言；
`tools/coverage.sh` 会给出宿主机构建的行覆盖率。未覆盖部分主要是防御性错误
分支、不支持的格式拒绝路径以及 GPU 专用桩，均如实列在覆盖率汇总中。

上面的十五个可视化用例是第二层验证：它们既校验像素，又把渲染结果作为产物保留，
因此光栅化回归既表现为断言失败，也表现为肉眼可见的画面变化。这些用例不计入
gcov 数字，覆盖率只统计通过 API 测试执行到的库代码。

## 目录结构

```
include/GLES2, GLES3, KHR     Khronos 原始头文件
include/zgl/zgl.h              软件渲染器的公共 C API
src/                           上下文、光栅化、纹理管线、绘制管线
src/zgl_fastmath.*             标量着色器数学近似
src/zgl_simd.*                 四 lane 点积/缩放与批量填充
src/glsl/                      GLSL 子集的词法、语法与解释执行
src/zgl_api_*.cpp              GLES 入口实现
examples/                      独立渲染示例
tests/                         单元测试与测试框架
tests/visual/                  可视化回归用例与 PPM 写出
tools/                         Luckfox/NDK 构建、QEMU 运行、TLS 修补、覆盖率
docs/                          GLSL 子集、移植说明与渲染截图
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

- 片元着色器最多写四个颜色输出，location 为 0 到 3；超过四个输出或两个输出
  占用同一 location 会链接失败。
- `out`/`inout` 参数、函数重载与递归不在解释器子集内；数组、结构体以及
  uniform block 内的结构体数组已支持，局部结构体定义尚不支持。
- 解释器中的结构体与数组元素使用独立的聚合值缓冲区；SLJIT 仅编译标量、向量与
  矩阵类型，遇到聚合类型或无符号整数会回退到解释器。
- MSAA 最多四个样本，适用于 renderbuffer 与纹理；默认帧缓冲仍为单采样，
  `gl_SampleMask` 输出与 `glMinSampleShading` 尚未实现。
- 压缩纹理为软件解码的 ETC2/EAC 块（兼容 ETC1 的 RGB、ETC2 RGBA 与 R11/RG11
  EAC）；ASTC、DXT 与厂商格式仍不支持。浮点纹理可采样，但不能作为颜色附件渲染。
- transform feedback 的 varying 仅支持浮点标量/向量；instanced feedback 绘制
  尚未实现。反馈开启但未丢弃光栅化时，顶点着色器会为捕获和光栅化各执行一次。
- 纹理图像按 top-down 存储，`v = 0` 对应第一条存储的行。
- `dFdx`、`dFdy`、`fwidth` 恒为 0，不计算隐式 LOD；`textureLod` 可显式选级。
- 恰好落在视口底边或右边的线与点会被非对称裁剪，因为 NDC 的 `y = -1` 映射到
  `height`。
- 不提供 `glGetTexImage`（它不属于 GLES）。

## 许可证

BSD-3-Clause，见 [LICENSE](LICENSE)。第三方代码：`deps/sljit` 下的 SLJIT，采用 Simplified BSD License。
