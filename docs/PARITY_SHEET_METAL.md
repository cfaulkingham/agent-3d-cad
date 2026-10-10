# Sheet-metal trees, folds and mapped cuts

The `sheet_metal` feature preserves one ordered, editable forming recipe. It
uses the original planar sketch, nominal thickness and caller-supplied K-factor.
Both formed and developed outputs are exact OCCT solids. The native runtime
has no Python or sibling-repository dependency.

## Named attachments and chains

Each item in `flanges` has a unique local `id`, `inside_radius`, `angle_deg`,
`length`, and exactly one attachment:

- `edge`: an expected-count geometric selector on a straight outer boundary of
  the source sketch, as before.
- `parent`: the ID of an earlier flange. `attachment` is `tip` (default),
  `start`, or `end` of that parent's straight leg. These are explicit geometric
  design references, not face/edge enumeration indices. An attachment can be
  used once. Parent IDs remain local when a feature becomes a portable component.
- `fold_line`: two world-space points in the source sketch plane. The directed
  axis crossed with the sketch normal determines the moving side. The line is
  the stationary-side bend tangent. A rectangular region of the existing blank,
  of width equal to the line length and developed length equal to bend allowance
  plus `length`, is consumed and formed. The region must exist completely and
  contain no preexisting perforation; map such cuts explicitly below. A
  stationary region must remain. This performs a fold through an existing blank
  instead of adding material beyond its outer edge.

The straight `length` is measured after the bend tangent. `start_gap` and
`end_gap` shorten the attachment axis before constructing its bend and leg.
Chained attachments are derived independently in the formed and developed
frames, so any parent edit propagates through the complete tree. A side
attachment's width is its actual edge length, including a parent's miter angle.
Missing parents, forward references, duplicate attachments, removed attachment
edges and ambiguous original geometric selectors fail explicitly.

An equal-angle, opposite-sign pair of bends gives a jog. For bends with inside
radius R, thickness T, signed magnitude A and first straight length L, the
parallel-face offset is `2*(R+T/2)*(1-cos(A)) + L*sin(A)` at the geometric middle
surface. Use the actual signed frames to choose the desired direction. Each
bend preserves its separate radius, ID and bend allowance.

`hem: true` permits exactly +180 or -180 degrees and an explicit positive
inside radius, forming an open safety hem. Other bends retain their previous
0.01° minimum and 179.99° exclusive maximum. A zero-radius crushed/closed hem is
not inferred. A 180° bend's tangent-based bend deduction is undefined and is
reported as JSON `null`; the finite developed allowance remains `pi*(R+K*T)`.

## Reliefs and miters

`relief: {"width": W, "depth": D}` generates two rectangular endpoint notches
into the stationary base, with the supplied width/depth. It also shortens the
bend by W at each end, in addition to explicit gaps. This provides both bend
endpoint relief and separated corners when used on adjacent base walls. The
same exact notch is retained in the developed blank. Relief dimensions must
be positive; no process-dependent minimum is guessed. This option applies to
base edges and internal folds. Pre-cut a parent leg using `cuts` when a child
needs its own attachment relief.

`miter: {"start_deg": A, "end_deg": B}` trims the straight leg's endpoints
inward from the bend tangent. Each degree is in [0,89). At distance x along
the leg, its available axis range is `[x*tan(A), width-x*tan(B)]`. These are
exact planar cuts in the formed part and exact line boundaries in the blank.
The tip must retain positive width. The bend itself retains its full axial
width; relief and explicit gaps provide bend-end clearance. The service does
not infer neighboring flange pairs or automatically solve a preferred corner.

## Cuts through bends

Each flange may carry up to 32 `cuts`, each with:

```json
{"offset":10,"width":5,"from":1,"to":12}
```

`offset` and `width` locate the cut along the shortened, oriented bend axis.
Cut width and developed length must each be at least 0.00001 mm.
`from` and `to` locate it along the developed region, starting at the bend's
stationary tangent. Values must stay within the named flange. A cut crossing
the bend-end tangent is divided into an exact annular-sector cut and an exact
planar through-cut. No faceted approximation of the cylindrical wall is used.
The angular interval follows neutral-axis arclength, so K-factor changes update
both the developed cut and the corresponding formed angles. All cuts preserve
nominal physical thickness on the remaining material.

Overlapping cut rectangles and rectangles crossing a miter boundary are rejected
rather than silently double-counting removed material. A child cannot attach
to a parent edge removed by a cut. Disconnected material or geometric collisions
also fail. Curved/freeform cut outlines and arbitrary imported-sheet unfolding
are outside this bounded contract.

## Physical and developed accounting

Every bend uses allowance `abs(angle)*(R+K*T)` and the formed annular volume uses
`abs(angle)*(R+T/2)*T*width`. Leg trapezoids, generated relief and mapped cut
volumes are included in independent analytic accounting. An internal fold first
removes its complete developed region from the base; rebuilding the flat mode
therefore retains the original blank size instead of adding a second region.
At K=0.5, formed and developed volumes agree. Other K values intentionally use
different developed and physical modeled lengths; reports never claim plastic
volume conservation or material/process certification.

Both modes must pass BRep validity, closed-shell/positive-volume checks, exact
pairwise material-collision checks and the complete native self-interference
checker. Aggregate volume must match the intent-derived analytic result. A
swept-Bezier checker abort has a narrow exact-basis remedy: a tensor product of
the original Bezier coefficients and the two extrusion endpoints produces an
identical B-spline support surface with unchanged parameter curves. Exact knot
insertion supplies stable quadrature spans. Bounds,
volume and topology counts are checked before repeating full validation. This
also avoids the native swept-Bezier area quadrature error; an independent
Simpson integral of the authored cubic verifies the final area. No tessellation,
approximate surface fit or tolerance increase is used.
Native topology history is recorded where the untouched original base remains
available. After base trimming, the dependency and intent references remain
valid; the service does not invent a face-history map through constructed cuts.

## Representative enclosure and verification

`examples/sheet-enclosure.create.json` makes an 80 by 60 mm, 1.5 mm-thick base,
four relieved 20 mm walls, an 8 mm return lip and a 3 mm open safety hem. Its
formed and flat features export independently. Changing `lip`, `thickness` or
`k` rebuilds the captured recipe, including the hem's formed placement and the
developed bend lines. The developed output is aligned with the world XY plane
for the ordinary exact top-view cutting DXF workflow.

The dedicated `parity_sheet` suite covers analytic three-bend lengths and
volumes, named side attachment, opposite-sign jogs, exact 180° hems, miter
trapezoids, relief subtraction, folding existing blanks, cuts crossing a
cylindrical bend, and non-midplane K mapping. An independent STEP reader checks
solid validity, material volume and exact cylinder radii in the enclosure.
Lifecycle coverage includes independent formed/flat STEP/STL/3MF exports and
a developed-outline DXF, cold rebuilds, feature-cache reuse, parameter edits,
failed-edit rollback, durable create/edit jobs and a self-contained component
after deleting its source.
The existing `sheet_metal` suite retains direct-bend, source-selector, export,
job and transactional regression coverage.

Cross-platform and desktop-host acceptance remains separate from native geometry
verification. This feature does not recognize an arbitrary imported sheet,
perform forming simulation, infer tooling, or certify bend/relief dimensions.

Run the native and independent contract checks with:

```sh
ctest --test-dir build -R '^(sheet_metal|parity_sheet)$' --output-on-failure
python tests/parity_sheet_schema_tests.py /absolute/path/to/agent-3d-cad
```
