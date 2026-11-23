# The interpreted GLSL subset

ZCat-GLES evaluates vertex and fragment shaders on the CPU. It is not a
compiler: the lexer and parser build an AST, and the evaluator walks that AST
for every vertex and every fragment. The subset is intentionally close to
GLSL ES 1.00 with the ES 3.00 additions that the state machine already accepts.

## Versions

`#version 100` and `#version 300 es` are recognised. The version controls which
storage qualifiers are accepted; the evaluator semantics are identical.

## Types

- Scalars: `float`, `int`, `bool`.
- Vectors: `vec2`-`vec4`, `ivec2`-`ivec4`, `bvec2`-`bvec4`.
- Matrices: `mat2`-`mat4`; square matrices only, stored column major.
- Samplers: `sampler2D`, `samplerCube`, `sampler3D`.
- `void` for function returns.

Integer and boolean values live in float registers. The interpreter truncates
on read; this is exact for the 24-bit integer range that shaders normally use
and avoids a second register file.

## Qualifiers and declarations

- Vertex stage: `attribute` (ES 1.00) and `in` (ES 3.00) inputs.
- Fragment stage: `varying` (ES 1.00) and `in` (ES 3.00) inputs.
- Vertex outputs: `varying` and `out`.
- Fragment outputs: `gl_FragColor`, `gl_FragData[0..3]`, or up to four `out vec4`
  variables with `layout(location = N)`; locations 0 to 3.
- `uniform` declarations of scalar, vector, matrix and sampler types.

Arrays, structures, interface blocks and uniform blocks are not part of the
grammar. `layout(location = ...)` is accepted on vertex inputs and fragment
outputs.

## Statements

`if`/`else`, `for`, `while`, `do`/`while`, `break`, `continue`, `return`,
`discard`, block scoping, and expression statements. User functions are
supported with by-value parameters; recursion is rejected at run time, the call
depth limit is 16 and the total step budget is 2^21 AST evaluations.

Operators: `+ - * /`, unary `-` and `!`, `++`/`--` as statements, relational and
equality operators, `&&`, `||`, `^^`, and the ternary operator. Assignment
supports `=`, `+=`, `-=`, `*=`, `/=` and swizzled targets.

## Built-in functions

Angle and exponential: `radians`, `degrees`, `sin`, `cos`, `tan`, `asin`,
`acos`, `atan` (one and two arguments), `pow`, `exp`, `log`, `exp2`, `log2`,
`sqrt`, `inversesqrt`.

These are evaluated with the approximations in `src/zgl_fastmath.*` rather than
libm. The polynomial fits keep the error well under one 8-bit output LSB for
the ranges exercised by the test and visual suites, and `sin` / `cos` process
four components at a time with NEON on ARMv7. The exact integer, depth,
stencil and blend paths never call the approximation layer.

Common: `abs`, `sign`, `floor`, `ceil`, `fract`, `mod`, `min`, `max`, `clamp`,
`mix`, `step`, `smoothstep`.

Geometric: `length`, `distance`, `dot`, `cross`, `normalize`, `faceforward`,
`reflect`, `refract`. Dot products and the scale used by `normalize` run four
components at a time on ARMv7; scalar tails cover vec2/vec3 and matrix
remainders.

Matrix: `matrixCompMult`, `transpose`, `determinant`, `inverse`.

Relational: `lessThan`, `lessThanEqual`, `greaterThan`, `greaterThanEqual`,
`equal`, `notEqual`, `any`, `all`, `not`.

Texture: `texture2D`, `texture2DProj`, `textureCube`, `texture3D`, `texture`,
`textureLod`.

Derivatives: `dFdx`, `dFdy`, `fwidth`. The CPU rasteriser shades one fragment at
a time and has no quad neighbourhood, so these always return zero.

## Conversions

The evaluator is deliberately permissive: assignment, constructor arguments and
function arguments convert implicitly between `float`, `int` and `bool`, and
between vector widths (widening fills the tail with `(0, 0, 0, 1)` as GLSL
does). Explicit constructors (`vec3(x)`, `mat2(...)`, `ivec2(...)`) and
swizzles (`v.xyz`, `v.rgba`, `v.stpq`) follow the usual GLSL rules, including
duplicate components on the left-hand side of an assignment.

## Uniform access

`Program::uniform_value(location)` returns the value slot for a location
reported by `glGetUniformLocation`. `glUniform*` writes through that slot.
Matrices are received in column-major order unless the `transpose` flag is set.

## Deliberate omissions

- Arrays, structures, and `const` arrays.
- `out` and `inout` parameters.
- Function overloading and recursion.
- Uniform blocks and shader storage blocks.
- Implicit LOD selection: `texture2D` samples level zero, `textureLod` selects a
  level explicitly. `dFdx`, `dFdy` and `fwidth` return zero.
- Integer bit operations, packing functions and precision qualifiers are
  parsed but have no effect.
