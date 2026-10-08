# Native static G-code review

`cad_gcode_review` inspects an existing plain `.gcode` artifact with explicit
Marlin command semantics, machine bounds, material target ranges and initial
state. It preserves the exact input bytes and a portable review package. It
does not generate toolpaths, execute G-code, contact a printer or approve a print.
Installed OrcaSlicer plan/run jobs are now implemented in [SLICING.md](SLICING.md).
Intentional physical printer handoff remains planned.

## Inputs

Supply `document_id`, a committed `revision`, an absolute existing `.gcode`
`path`, its actual lowercase 64-digit `expected_sha256`, and `options` below.
Optional `feature_id` defaults to the saved output and must name a declared
feature. This CAD association is **caller declared**, not proof that these
toolpaths reproduce the feature. Hashes qualify the unchanged bytes being
reviewed; they do not establish their origin.

```json
{
  "firmware": "marlin",
  "machine": {
    "name": "Illustrative analytical fixture",
    "motion_bounds_mm": [[-20, 20], [-20, 20], [0, 100]],
    "home_position_mm": [0, 0, 0]
  },
  "material": {
    "name": "Illustrative target ranges",
    "nozzle_temperature_c": [190, 230],
    "bed_temperature_c": [50, 70]
  },
  "initial": {
    "units": "mm",
    "xyz_mode": "absolute",
    "extrusion_mode": "absolute",
    "position_mm": [0, 0, 0],
    "extruder_mm": 0
  }
}
```

These fixture limits are not printer settings. Supply the actual assessed
motion envelope, including purge/park travel beyond the bed, and material ranges.
Each range is finite `[min,max]` with `min < max`. Coordinates are millimeters
within ±1,000,000; temperatures are Celsius within 0..500. Names contain 1..64
printable UTF-8 bytes. All objects reject undeclared fields.

Initial `units` (`mm` or `inch`), XYZ mode and extrusion mode (`absolute` or
`relative`) are required. Optional position and extruder values are explicitly
in millimeters, even when the initial program uses inches. Omit an unknown
position or E value; the inspector retains unknown sweeps/deltas. Optional home
coordinates must lie within the supplied bounds. Homing travel and actual
firmware home offsets remain unknown even when a caller declares its endpoint.
This subset assumes initial Celsius targets and linear filament-length E units;
it does not read persistent machine settings. M149, M200 or tool changes in the
artifact invalidate the relevant assumptions.

## Findings and coverage

Six checks report `pass`, `fail` or `unknown`, a method, finding counts and up to
64 witnesses each. `fail` dominates `unknown` in the overall status. Line 0
denotes an artifact-wide finding. A failed or unknown inspection is a successful
tool result containing those findings; it is never relabeled as approved.

| Check | Measured scope |
|---|---|
| `syntax` | UTF-8 text, finite numeric words, comments, duplicate numeric parameters and optional original-byte XOR checksums; at least one supported movement |
| `commanded_bounds` | Known linear endpoints and exact in-plane quarter extrema of single-turn circular arcs against caller bounds |
| `temperature_targets` | Explicit positive M104/M109/M140/M190 targets against supplied ranges; zero disables a heater; positive nozzle and bed targets required |
| `extrusion` | Programmed E length deltas, resets, retractions and positive extrusion accompanying measured spatial motion |
| `firmware_commands` | Unsupported commands/parameters and firmware-dependent actions remain unknown |
| `position_tracking` | Unknown starts, frames and unmeasured complete sweeps remain unknown |

The inspector tracks G20/G21 units, G90/G91 XYZ modes, M82/M83 extrusion
overrides, and G92 logical coordinate offsets and E resets. Under Marlin,
G90/G91 clear an M82/M83 override. G17/G18/G19 select XY/ZX/YZ planes. G2/G3
support relative I/J/K centers or signed R radii; negative R selects the major
arc. Full circles with in-plane centers and linear orthogonal helical travel
include their extrema. Absolute-center extensions, extra turns and out-of-plane
center parameters are unsupported. Inconsistent/impossible arc geometry fails
the syntax check with the numerical method's tolerance.

Unknown G commands can invalidate the coordinate frame; unsupported tool,
volumetric-extrusion or offset changes invalidate dependent assumptions.
Unsupported temperature-unit commands invalidate the Celsius interpretation.
Subsequent target records retain their programmed value with unknown units.
Homing, leveling and firmware macros cannot silently pass complete swept bounds.
Statistics retain supported motion counts, known commanded bounds, programmed
extrusion/retraction lengths, bounded target/unknown-command lists and skipped
sweeps. They do not simulate physical firmware, acceleration, collisions,
deposition, temperatures or geometric toolpath equivalence. A static pass is
limited to the declared checks and caller assumptions.

## Artifact and job contract

The result qualifies the source document/revision/feature/model hash/native build,
raw G-code hash/bytes, raw report hash/bytes and absolute `path`, `artifact_path`
and `source_path`. The directory contains:

- `original.gcode`: exact unchanged bytes, including original comments.
- `review.json`: versioned source identity, caller association, input options,
  raw artifact identity and complete findings/coverage.
- `source.json`: complete committed editable record.
- `manifest.json`: relative paths, actual hashes and byte counts for the three
  artifacts, `physical_print_started: false`, `process_approval: "not_evaluated"`.

The expected raw hash must match before any review is staged; otherwise
`artifact_mismatch` publishes nothing. Unsupported findings preserve the original
artifact. Private staging, bounded isolated native inspection and an atomic
directory rename prevent partial publication. Failures/timeouts/cancellation
preserve prior artifacts and document HEAD/history. Hash ledgers remain usable
after moving the package or removing the caller's original file.

Use `cad_job` with `tool: "cad_gcode_review"` for cancellable inspection and
durable result replay. It shares the existing four-worker slots, job memory/wall
budgets and native Windows/POSIX containment. The parser additionally caps input
at 64 MiB, 4,096 bytes per line, one million lines and 30 seconds of inspection.
It never passes artifact text to a shell, interpreter, compiler or firmware.

This native operation needs no installed slicer, Python, Node or developer tools.
The separate real OrcaSlicer example is recorded in HANDOFF; its source and
profile evidence do not turn a caller-declared association into native slicing
provenance. The larger workflow acceptance remains in
[COMPOSITION_FABRICATION_REVIEW.md](COMPOSITION_FABRICATION_REVIEW.md).

Command interpretation was checked against the primary Marlin
[G90](https://marlinfw.org/docs/gcode/G090.html),
[M83](https://marlinfw.org/docs/gcode/M083.html),
[G92](https://marlinfw.org/docs/gcode/G092.html) and
[arc](https://marlinfw.org/docs/gcode/G002-G003.html) documentation. Decimal
word parsing is independently implemented; the
[versioned firmware parser](https://github.com/MarlinFirmware/Marlin/blob/2.1.2.5/Marlin/src/gcode/parser.h)
also avoids treating an E word as a numeric exponent. These references explain
the supported interpretation, not the configuration or behavior of a physical
printer.
