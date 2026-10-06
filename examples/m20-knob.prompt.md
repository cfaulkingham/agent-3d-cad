# M20 coarse-thread knob

Prompt: “Make a knob with a male 20mm coarse threaded end.”

The paired `m20-knob.create.json` is the editable native model for this request.
The chosen dimensions are a right-hand M20 × 2.5 thread, 20 mm exposed stud,
45 mm nominal grip diameter, 18 mm grip height, eight finger scallops, and
1.2 mm rounds on the grip's top and bottom edges. Overall height is 38 mm.
The stud overlaps the grip by 1 mm and has a 45-degree lead at its free end.

M20's coarse pitch is 2.5 mm; see the
[KIPP metric thread table](https://www.kippusa.com/en-us/measurement-units).
The continuous exact helical feature has nominal 60-degree flanks and a flat
root. It does not specify a certified fit class or additive-manufacturing
clearance. Actual mating fit depends on the fabrication process.

The agent interprets the prompt into structured native features and calls
`cad_create`, followed by `cad_open` and `cad_export`. The MCP service itself
does not run a language model. All dimensions remain named parameters in the
saved document; use `cad_apply` to edit a parameter with the current revision.
