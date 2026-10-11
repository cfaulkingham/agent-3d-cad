---
name: cad-print-handoff
description: Prepare native Agent CAD parts for installed slicing and a traceable printer handoff, including Bambu Studio or Bambu Connect when available on the user's host.
---

# Slice and hand off a saved part

Use the user's requested printer application and existing profiles. Native
modeling/export needs no extra runtime. A slicer or printer application is an
optional downstream dependency; do not install Python or a compiler to prepare
CAD. Read [supported paths](references/slicer-and-bambu.md) for exact boundaries.

1. Pin the committed source revision and selected solid(s), dimensions, intended
   printer/nozzle, material and build orientation. Use
   [cad-manufacturing](../cad-manufacturing/SKILL.md) for a requested process or
   orientation review. A camera or review frame does not rotate exported input.
2. Choose the available path:
   - Native `cad_slice` supports installed OrcaSlicer **2.4.2**, one source solid,
     explicit self-contained machine/process/filament profiles, Marlin FFF and
     High Temp Plate. For that supported combination plan, inspect, run and
     inspect the actual results using the reference below.
   - For Bambu printers/profiles, multi-part plates or another supported host
     slicer, export native `3mf` or `stl`, then use that installed application's
     actual UI/connector. Preserve mm scale, part identities and geometry-only
     import where offered. Native CAD's `3mf` is a model/placement archive,
     **not** a printer-ready `.gcode.3mf`.
   - If no suitable slicer/application is installed or accessible, complete the
     exact geometry export and handoff record. State the missing app/profile;
     do not claim slicing or app launch occurred.
3. Inspect the real sliced preview, supports, first layer, travel, material/bed
   setup and warnings. Retain actual slicer version, profiles, source hash,
   output hash and changed orientation/scale. If the application modifies the
   geometry or orientation, its output needs a fresh review of those changes.
4. When compatible plain G-code and explicit limits are available, use
   `cad_gcode_review` and optionally `cad_printer_handoff` plan/verify. Both are
   static/offline; preserve unknown proprietary commands and readiness. Native
   Marlin review cannot certify Bambu firmware or a zipped project.
5. For a requested Bambu handoff, open the verified sliced project in the
   available Studio/Connect app via its documented file workflow, inspect the
   actual loaded plate and selected printer, and report the observed result.
   Opening or preparing a job is not evidence that a printer accepted it.

Complete preparation and verification within the user's request. A request to
print a specific job supplies print intent; do not ask again just because this
skill was loaded. Before an authorized start verify the actual target, clear
plate, filament/nozzle/plate setup and current error state using available
evidence. If a required physical fact cannot be observed, identify that fact.
A request merely to export, slice or review supplies no upload/start intent.
Observe acceptance/status after a live action and report uncertainty honestly.

Do not set up LAN credentials, change firmware/security settings or introduce
third-party adapters as a side effect of a Studio/Connect handoff. If the user
explicitly chooses an installed LAN adapter, follow that adapter's actual
documentation and preserve this same source/output identity.
