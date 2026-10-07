# Duplex #35 sprocket, 13 teeth, keyed 5/8-inch bore

Request: “use the agent 3d cad to build a double-strand #35 sprocket - 13 teeth, 5/8\" bore, keyway”.

Saved with the installed native service as `duplex_35_sprocket_13t_keyed`,
revision 1, OpenCascade 8.0.1. The paired `.create.json` is the complete editable
intent. The 53 features use ordinary native operations; no service changes or
external CAD modeling runtime were needed.

| Dimension | Inches | Millimeters |
|---|---:|---:|
| Chain pitch | 0.375 | 9.525 |
| Bushing diameter | 0.200 | 5.08 |
| Tooth-row width, each | 0.162 | 4.1148 |
| Row center spacing | 0.399 | 10.1346 |
| Overall width across rows | 0.561 | 14.2494 |
| Nominal outside diameter | 1.750 | 44.45 |
| Calculated pitch diameter | 1.566968 | 39.800988 |
| Bore diameter | 0.625 | 15.875 |
| Keyway width | 0.1875 | 4.7625 |
| Keyway radial depth beyond bore | 0.079 | 2.0066 |
| Keyway roof to opposite bore wall | 0.704 | 17.8816 |
| Assumed type-B hub diameter | 1.109375 | 28.178125 |
| Overall axial length | 1.250 | 31.75 |

The keyway runs through the full length along +Y and accepts a nominal 3/16-inch
square key. Its radial depth is measured from the bore tangent, not from the
chord between the keyway corners. Both rows are in phase with 13 teeth each.
The hub projects on one side. Each row has a 0.5 mm tip bevel. No set screw was
requested or modeled. Manufacturing fits, material and load rating are unspecified.

Chain dimensions follow [Tsubaki 35-2RB](https://chains.ustsubaki.com/en-us/item/rs25-through-rs240-chains/rs35-roller-chains/35-2rb).
Outside diameter, row width and hub length follow [Tsubaki D35B13](https://catalog.tsubaki.ca/item/no-35-3-8-pitch-sprockets/no-35-3-8-pitch-multiple-strand-sprockets/d35b13);
the hub diameter uses 1-7/64 inches, consistent with its rounded 1.109-inch listing.
Keyway dimensions follow [G&G Manufacturing](https://www.ggmfg.com/engineering-information/bore-keyway-setscrew-information.php).
This is a newly authored model, not manufacturer-supplied CAD.

The exact circular seat, working arc, tangent flank and topping arc construction
is adapted from the existing 20-tooth example using the [GEARS tooth-form equations](https://d2t1xqejof9utc.cloudfront.net/files/204686/design_draw_sprocket_5.pdf?1599689096=).
For 13 teeth, A=39.615384615 degrees and B=13.692307692 degrees. Seat, working and
topping radii are 2.5908, 6.6548 and 4.248299077 mm. Tooth count is encoded in the
13 angular instances and geometry constants; changing it requires regenerating
the layout. Bore, keyway, row widths/spacing, hub and outside diameter are named
parameters. Fixed trimming extents limit practical edits.

Validation: one valid solid, 273 faces, 802 edges and volume 17,888.619493 mm3.
Independent STEP read-back with healing disabled retained one valid solid and
volume within 0.01 mm3. All 568 material/clearance probes passed, covering both
rows, bushing seating, tooth phase, inter-row clearance, bore and keyway roof/
sidewalls through the hub. Exact seat/working/topping cylindrical face counts
are 26/52/52. The STL has 3,512 triangles and 5,268 edges, each with two incident
triangles at 0.00001 mm vertex quantization. Native source read-back after exports
matches the authored model. The live Codex viewer was visually inspected.
Evidence is in `build/sprocket35-13t/`.

An initial layout submission had a duplicate feature ID and was rejected before
publication. The corrected job `duplex35_13t_keyed_20261006_v2` committed revision 1.
