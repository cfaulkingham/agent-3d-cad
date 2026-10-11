# Supported native slicing and application handoff

## Native Orca plan/run

Use the installed tool schema and packaged native SLICING.md contract. The
supported driver is fixed to OrcaSlicer 2.4.2; it is not a general CLI runner.
`cad_slice` with `action: plan` needs document/revision, optional source
`feature_id` and `options`:

- `backend: orcaslicer`, `version: 2.4.2`;
- `executable: {path, expected_sha256}` for the native executable;
- `profiles: {machine, process, filament}`, each `{path, expected_sha256}`;
- `bed_type: High Temp Plate`;
- `review` with `firmware: marlin`, machine name and actual
  `motion_bounds_mm: [[xmin,xmax],[ymin,ymax],[zmin,zmax]]`, material name and
  `nozzle_temperature_c`/`bed_temperature_c` allowed ranges, and known initial
  `units`, `xyz_mode`, `extrusion_mode`. Omit unknown positions; do not assume
  machine home or exclude purge/park travel from the supplied bounds.

Profiles must have matching `type`, explicit names, no unresolved `inherits`
and no post-processing commands. Machine declares Marlin/FFF; process and
filament explicitly include its name in `compatible_printers`. Preserve profile
source evidence while resolving inheritance. Do not relabel a Bambu profile
as Marlin to pass validation.

Inspect plan source summary, exact artifact hashes, intended placement and
fixed invocation. Run with only document/revision, `action: run`, `plan_path`
and `expected_sha256` from the returned plan. Both actions support `cad_job`.
The native run checks version, rebuilds the same exact source, checks effective
profile identities and retains logs/settings/G-code/review/manifest. XY is
arranged on the bed; source Z orientation is preserved. Review actual settings
and findings. Tool completion or `physical_print_started: false` is not a
printer readiness result. Missing/changed tools, unsupported profiles and failed
output checks are actionable failures; no generic fallback flags are supported.

## Plain G-code plan/verify

`cad_printer_handoff` plans from a committed source, actual G-code path/hash and
explicit printer/profile/review options described by its schema. It snapshots
the source, G-code, profiles and native findings into a portable hash ledger.
Call `action: verify` with the same document/revision, returned `plan_path` and
`expected_sha256`; inspect recomputed findings. It supports only `plan` and
`verify`, not upload/start. Overall readiness remains unknown because actual
device state and firmware/profile compatibility are not established. The link
between independently supplied G-code and CAD is caller-declared.

## Bambu Studio / Connect

Check the host for installed applications and their current documented file
workflow. Use an available native app connector or UI automation. For geometry,
export `cad_export` with `format: 3mf` (or `stl`) and inspect the imported model
in Studio. Retain printer, nozzle, plate, filament and process selections; inspect
the sliced preview and save the application's sliced project. Do not rename a
model 3MF or plain G-code into `.gcode.3mf`, or inject replacement toolpaths into
an unrelated printer template.

Bambu's [official integration explanation](https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/)
describes Connect as a printer communication path for third-party slicers.
That January 2025 article is architectural context, not proof of current
installation, firmware support or successful handoff. Check the current
[Connect documentation](https://wiki.bambulab.com/en/software/bambu-connect)
and installed application's behavior. If that documentation is inaccessible,
record the limitation and use the observed application's file-open flow rather
than guessing a URL handler or its arguments.

Only open a sliced project the app actually accepts for the target printer.
Verify displayed filename/plate, printer identity, material mappings and warnings.
Retain the file's hash and application version. If the host cannot operate the
app, provide that verified file and precise remaining UI step; do not claim to
have opened or sent it. Native Marlin review of extracted plain commands cannot
replace the application's understanding of proprietary commands or plate metadata.

The upstream [Bambu Studio repository](https://github.com/bambulab/BambuStudio)
is a primary source for current platform/support information. Application
versions and firmware behavior change; recheck them when executing a handoff.
No hardware start or host-app acceptance is established by a packaging test.
