# Bounded parametric expressions

Expression values are explicit JSON trees, never executable source. A numeric
literal or named numeric parameter takes the unit required by its use. Every
expression declares its result `unit`: `mm`, `mm2`, `deg`, `rad`, or
`dimensionless`. Nested expressions must match the operand units below.

| Operations | Arguments | Result and operand units |
|---|---|---|
| `add`, `subtract`, `min`, `max` | 2 | Both operands have the result unit |
| `multiply`, `divide` | 2 | First operand has result unit; second is dimensionless |
| `negate`, `abs`, `floor`, `ceil`, `round` | 1 | Operand has result unit; round uses nearest integer with half values away from zero |
| `sqrt` | 1 | `mm` result requires `mm2` operand; dimensionless result requires dimensionless operand |
| `square` | 1 | `mm2` result requires `mm` operand; dimensionless result requires dimensionless operand |
| `sin`, `cos`, `tan` | 1 | Radian operand, dimensionless result |
| `sin_deg`, `cos_deg`, `tan_deg` | 1 | Degree operand, dimensionless result |
| `asin`, `acos`, `atan` | 1 | Dimensionless operand; result explicitly `deg` or `rad` |
| `atan2` | 2: y, x | Same-unit operands, declared by optional `argument_unit` (default dimensionless); result `deg` or `rad` |
| `pow` | 2 | Dimensionless operands/result; real finite domain only |
| `exp`, `log` | 1 | Dimensionless operands/result; natural exponential/logarithm |
| `less`, `less_equal`, `greater`, `greater_equal`, `equal`, `not_equal` | 2 | Same-unit operands, declared by optional `argument_unit` (default dimensionless); dimensionless 0 or 1 result |
| `not` / `and`, `or` | 1 / 2 | Dimensionless operands/result; zero is false, nonzero true |
| `if` | 3: condition, then, else | Dimensionless condition; both branches have result unit |
| `clamp` | 3: value, minimum, maximum | All operands have result unit; minimum must not exceed maximum |

Comparisons are ordinary numeric comparisons with no inferred geometric tolerance.
The existing multiplication contract remains unchanged: multiplying two lengths
is not implicitly accepted as an area. Use `square` for the explicit supported
unit conversion. `argument_unit` is only permitted for comparisons and `atan2`.

Every tree, including inactive branches, has at most 128 nodes and 16 levels;
all fields, operations, arities, references, units and literal bounds are checked.
Only the selected `if` branch is numerically evaluated. This permits a guarded
division or square root while still rejecting unknown parameters or unsupported
operations in an inactive branch. Both branches remain tracked dependencies.

Finite numeric inputs and computed intermediate results remain bounded to
±1,000,000. Invalid domains fail before publication: division by zero; negative
square roots; logarithms of nonpositive numbers; inverse sine/cosine outside
[-1,1]; `atan2(0,0)`; zero raised to a nonpositive power; negative bases with
noninteger exponents; overflow. Tangent rejects angles whose cosine has magnitude
at most 1e-12, as well as results beyond the ordinary numeric bound.

For a length derived from an editable angle:

```json
{"expression":{"op":"multiply","args":[10,
  {"expression":{"op":"sin_deg","args":[{"parameter":"angle"}],"unit":"dimensionless"}}
],"unit":"mm"}}
```

Linear and circular pattern `count` now accepts a dimensionless scalar. Its
evaluated value must be an exact integer from 2 to 64; it is never rounded or
clamped implicitly. Changing a count creates/removes copies through the normal
atomic revision transaction. Circular patterns still forbid a duplicate full-turn
endpoint. A full ring can use `count: {parameter: "n"}` and an `angle_deg`
expression dividing 360 degrees by that same dimensionless parameter.

Dependency caches and portable components track parameter references throughout
the tree, including count expressions and both conditional branches. Saved
documents remain self-contained. The `parametric` CTest covers analytic math,
invalid domains, bounded trees, unit errors, lazy conditions, real pattern-count
edits, exact exported STEP readback, failed-edit rollback and portable components.
