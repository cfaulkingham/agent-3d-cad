# Geometry regression fixtures

`periodic-cone.step` is one isolated roller from the user-provided carousel STEP
in the October 10, 2026 shaft-fit workflow. It contains eight original faces.
OCCT 8.0.1 accepts the B-rep but its default tessellator reports a self-intersecting
periodic trim and leaves one conical face without triangles. This small exact
solid preserves that regression; no user project path is required by tests.
