# The interpreted GLSL subset

Glanceasy evaluates vertex and fragment shaders on the CPU. It is not a
compiler: the lexer and parser build an AST, and the evaluator walks that AST
for every vertex and every fragment. The subset is intentionally close to
GLSL ES 1.00 with the ES 3.00 additions that the state machine already accepts.

## Versions

`#version 100` and `#version 300 es` are recognised. The version controls which
storage qualifiers are accepted; the evaluator semantics are identical.

## Types

- Scalars: `float`, `int`, `uint`, `bool`.
- Vectors: `vec2`-`vec4`, `ivec2`-`ivec4`, `uvec2`-`uvec4`, `bvec2`-`bvec4`.
- Matrices: `mat2`-`mat4` plus the non-square `matCxR` forms, stored column
  major.
- Samplers: `sampler2D`, `samplerCube`, `sampler3D`.
- Arrays and structures, including arrays of structures, with `std140` layout
  metadata for uniform blocks.
- `void` for function returns.

Integer and boolean values live in float registers. The interpreter truncates
on read; this is exact for the 24-bit integer range that shaders normally use
and avoids a second register file.  Unsigned integer literals use the `u`
suffix and integer bit operations are evaluated with unsigned semantics.

## Qualifiers and declarations

- Vertex stage: `attribute` (ES 1.00) and `in` (ES 3.00) inputs.
- Fragment stage: `varying` (ES 1.00) and `in` (ES 3.00) inputs.
- Vertex outputs: `varying` and `out`.
- Fragment outputs: `gl_FragColor`, `gl_FragData[0..3]`, or up to four `out vec4`
  variables with `layout(location = N)`; locations 0 to 3.
- `uniform` declarations of scalar, vector, matrix, sampler, array and
  structure types.
- Global and local array declarations, global structure definitions and
  structure instance declarations.  Constructor syntax covers arrays
  (`Type[N](...)`) and structures.
- Array indexing, structure field selection and `array.length()` are
  available in expressions.

`layout(location = ...)` is accepted on vertex inputs and fragment outputs.

## Uniform blocks

A `layout(std140) uniform` block may contain scalar, vector, matrix, array and
structure members.  Arrays use a 16-byte aligned stride and structures use
recursive member offsets.  Block members cannot be samplers. Both spellings
are accepted:

```
layout(std140) uniform Tint { vec4 color; };             // color is global
layout(std140) uniform Tint { vec4 color; } tint;         // tint.color
```

Members are assigned std140 offsets: scalars are 4-byte aligned, `vec2` 8-byte,
`vec3`, `vec4` and matrices 16-byte. A matrix occupies `columns * 16` bytes and
each column starts at a 16-byte boundary. The block size is rounded up to 16
bytes. `glGetActiveUniformBlockiv`, `glGetActiveUniformsiv` and
`glGetActiveUniformBlockName` report the resulting layout. A block with the same
name in both stages must have an identical layout or linking fails.

The engine refreshes every block from the buffer bound to the block's binding
point before each draw. `glUniformBlockBinding` selects the point and
`glBindBufferBase` / `glBindBufferRange` supply the data. `glGetUniformLocation`
returns -1 for block members; `glGetUniformBlockIndex` and
`glGetUniformIndices` are the reflection entry points. Block buffers are copied
into the program's uniform values, so the interpreter and the SLJIT tier see
the same data.

## Transform feedback

`glTransformFeedbackVaryings` selects vertex outputs before linking.
`GL_INTERLEAVED_ATTRIBS` packs the selected values into one record in the
order supplied to `glTransformFeedbackVaryings`; `GL_SEPARATE_ATTRIBS` writes each
varying to its own binding point.

Float scalars and vectors are supported, with up to 64 interleaved components or
four separate buffers. Names must match vertex `out` variables exactly.

`glBeginTransformFeedback` accepts `GL_POINTS`, `GL_LINES` or `GL_TRIANGLES`;
the draw mode must be compatible with the active mode. Capture writes the
selected varying values into `GL_TRANSFORM_FEEDBACK_BUFFER` bindings, honouring
the binding offset and size. `glPauseTransformFeedback` skips capture while the
object stays bound, and `glResumeTransformFeedback` continues at the saved
cursor. `glDrawTransformFeedback` replays the captured vertex count with the
current vertex arrays.

`glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, ...)` counts primitives
written while the query is active. The capture path calls the vertex shader
independently of rasterisation, so combining feedback with normal drawing runs
the vertex shader twice per vertex.

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

These are evaluated with the approximations in `src/glanceasy_fastmath.*` rather than
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

- Local structure definitions.
- `out` and `inout` parameters.
- Function overloading and recursion.
- Shader storage blocks.
- Implicit LOD selection: `texture2D` samples level zero, `textureLod` selects a
  level explicitly. `dFdx`, `dFdy` and `fwidth` return zero.
- Packing functions and precision qualifiers are parsed but have no effect.
