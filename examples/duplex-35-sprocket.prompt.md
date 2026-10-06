# Duplex #35 sprocket

Prompt: “Build a double-strand #35 sprocket.”

The paired JSON creates an editable, 20-tooth duplex sprocket using the installed
native MCP service. The assumed shaft interface is a plain 3/4-inch (19.05 mm)
bore, with a type-B single-sided hub. No keyway or set-screw hole is specified.

Dimensions (millimeters):

- Chain pitch: 9.525; bushing diameter: 5.08.
- Two aligned rows, each 4.1148 wide, with 10.1346 center spacing.
- Face width: 14.2494; nominal outside diameter: 65.853483.
- Pitch diameter: 60.888117; hub diameter: 49.2125; overall length: 34.925.
- A 0.5 mm bevel reduces the tooth-tip diameter at each row face.

Chain data comes from [Tsubaki's 35-2RB dimensions](https://chains.ustsubaki.com/en-us/item/rs25-through-rs240-chains/rs35-roller-chains/35-2rb).
The 0.162-inch row width and hub/bore dimensions follow the 20-tooth entry in
[Martin's duplex sprocket catalog](https://pt.martinsprocket.com/docs/catalogs/power%20transmission/2_sprockets/secao_e.pdf).
This is a new parametric model, not a manufacturer-supplied part.

The tooth form uses the circular-seat, working-arc, straight-tangent and topping-
arc construction documented by [GEARS Educational Systems](https://d2t1xqejof9utc.cloudfront.net/files/204686/design_draw_sprocket_5.pdf?1599689096=).
For N=20, A=38 degrees and B=15.2 degrees. In millimeters, the three radii are
R=0.5025*Dr+0.0381=2.5908, E=1.3025*Dr+0.0381=6.6548, and
F=Dr*(0.8*cos(B)+1.4*cos(17-64/N)-1.3025)-0.0381=4.173734.
The construction uses exact cylinders and planar trims, preserving analytic arcs
in STEP. The two rows share the same finished tooth profile and angular phase.

All 65 features are ordinary supported native operations; no service update or
external CAD runtime was needed. Bore, hub and row dimensions remain parameters.
The 20-tooth layout is encoded by its instances and angle constants; changing
tooth count requires regenerating that layout rather than setting one parameter.
The fixed trimming boxes also bound practical edits; this is a #35 example,
not a universal sprocket generator.

Validation: one valid solid, volume 64,181.266497 mm³, 409 faces and 1,210 edges.
Independent native STEP read-back with optional healing disabled preserved the
solid and volume. 812 material/clearance probes checked both rows' bushing seats,
tooth phase, inter-row clearance and bore. Exact seat, working and topping face
radii were confirmed. STL has 5,264 triangles and 7,896 edges with two incident
triangles per edge at 0.00001 mm vertex quantization. These checks establish
nominal geometry; they do not specify material, load rating, or a manufactured
shaft fit. Detailed local evidence is in `build/sprocket35/validation.log`.
