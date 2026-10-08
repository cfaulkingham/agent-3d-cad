# Explicit printer handoff

The native printer-planning module prepares a source-qualified, checksummed
offline handoff package from existing plain G-code. It can verify the reviewed
package again without the caller's original files. Neither operation uploads,
contacts a printer, publishes MQTT, executes G-code or starts physical work.
Native upload and print-start support are explicitly false in every result.

`cad_printer_handoff` exposes `plan` and `verify` through the shared Service,
MCP and CLI. Both actions support `cad_job` for bounded execution, cancellation
and durable request replay. HANDOFF records the executed integration and
relocated runtime evidence; physical hardware acceptance remains separate.

## Reviewed inputs

Plan requires `document_id`, committed `revision`, `action: "plan"`, an absolute
existing plain `.gcode` `path`, actual lowercase `expected_sha256` and `options`.
Optional `feature_id` defaults to the saved output. G-code/source association is
**caller declared**, not proof that the toolpaths reproduce that geometry.
Generate and retain native slicing provenance separately with
[SLICING.md](SLICING.md). No profiles are selected or inferred automatically.

```json
{
  "printer": {
    "backend": "manual",
    "id": "assessed_printer",
    "model": "Explicit assessed machine",
    "nozzle_diameter_mm": 0.4,
    "bed_type": "Explicit assessed plate",
    "handoff": "plain_gcode"
  },
  "profiles": {
    "machine": {"path": "/absolute/machine.json", "expected_sha256": "ACTUAL_RAW_HASH"},
    "process": {"path": "/absolute/process.json", "expected_sha256": "ACTUAL_RAW_HASH"},
    "filament": {"path": "/absolute/filament.json", "expected_sha256": "ACTUAL_RAW_HASH"}
  },
  "review": {
    "firmware": "marlin",
    "machine": {"name": "Illustrative analytical fixture", "motion_bounds_mm": [[-20,20],[-20,20],[0,100]]},
    "material": {"name": "Illustrative PLA targets", "nozzle_temperature_c": [190,230], "bed_temperature_c": [50,70]},
    "initial": {"units": "mm", "xyz_mode": "absolute", "extrusion_mode": "absolute", "position_mm": [0,0,0], "extruder_mm": 0}
  }
}
```

Replace illustrative values and hash placeholders with verified inputs. Each
profile is a checksummed, self-contained JSON snapshot with matching `type`
(`machine`, `process`, `filament`) and printable `name`. Nonempty `inherits` is
unsupported; resolve it explicitly before planning. Nonempty profile
`post_process` commands are rejected. Profiles are data: nothing in them is
executed. Other settings are preserved without claiming field-by-field
machine/filament compatibility or proof that the supplied G-code used them.

Printer model/bed labels have 1–64 printable UTF-8 bytes. Printer IDs use portable
model-identifier rules. Nozzle diameter is finite within 0.05–5 mm. Review
firmware/machine/material/initial fields follow [GCODE_REVIEW.md](GCODE_REVIEW.md).
Explicit limits include purge/park travel; omit unknown initial coordinates
instead of guessing them. The Marlin subset does not simulate proprietary Bambu
firmware, macros, homing, leveling, acceleration, collisions or temperatures.

`backend: "manual"` describes operator-managed transfer of plain G-code through
the target machine's documented interface. `backend: "bambu_lan"` describes the
optional external skill workflow below; it does not install or call that adapter.
Native actions are only `plan` and `verify`. `start`, `upload`, `run`, execution
flags, credentials, hosts and artifact-supplied commands are rejected.

## Package and re-verification

The package contains exact `original.gcode`, complete committed `source.json`,
native `review.json`, the three original profile snapshots, `plan.json` and a
portable `manifest.json`. The plan pins source document/revision/feature/model
SHA-256/kernel/native build, actual G-code hash/bytes, explicit options, profile
names, native findings, readiness and a relative artifact ledger. The manifest
also hashes the actual plan bytes. The filesystem remains editable; reviewed
hashes qualify the bytes and make changes detectable.

Static findings are computed by the existing bounded isolated native G-code
worker. Failed findings remain `fail`; unsupported behavior remains `unknown`.
Even a static `pass` produces overall printer readiness `unknown`, because
current printer state, actual profile/firmware compatibility, adapter setup,
physical preparation and print intent have not been established. Each unmet
prerequisite states that limitation. `installation_status: "not_checked"` means
unknown availability, not an installed or missing-tool claim.

Verification requires only `document_id`, `revision`, `action: "verify"`, the
returned absolute `plan_path` and exact `expected_sha256`. It checks the reviewed
plan, every relative artifact hash/byte count, source/build identity, original
profile hashes/names and manifest. It recomputes native static findings and
readiness rather than trusting a rehashed report that claims approval. A package
can relocate and verify after all original G-code/profile/template inputs are
removed. Historical source revisions remain explicit; a different source/build
does not silently become equivalent.

Publication uses a fresh private stage, then one atomic directory rename under
the document writer lock. Every failure preserves source HEAD/history and prior
exports. Native bounds are 64 MiB per G-code/template, 1 MiB per profile, 2 MiB
per plan/review JSON and 128 MiB / 16 entries per complete package. The static
parser retains its line/time limits and native job budget checks. Verification
never promotes missing, partial, stale or modified artifacts.

Package publication and job-result storage are separate writes. Committed source
mutations have receipt recovery; offline printer packages do not. If a package
publishes and the coordinator stops before recording its result, the job can
become `interrupted`; a result-storage failure reports an explicit failed job
while the package remains. An interrupted retry with the same request ID, or a
deliberate retry of a failed job with a new ID, can create another package.
Inspect exports and re-verify the actual plan hash before deciding to retain,
remove or retry a leftover package. A failed/interrupted job does not prove that
no artifact was published. This uncertainty has no printer or source-mutation
effect: neither planning nor verification can upload or start physical work.

## Optional Bambu LAN workflow

The installed `bambu-labs` skill is the runtime source of this external workflow.
Its helper uses Python and its own explicit machine configuration; these are
optional printer dependencies, never dependencies of ordinary native CAD,
export or native offline planning. Follow the installed skill's onboarding and
first-use checklist. Store access codes in ignored local `bambu-printers.json`;
the native package intentionally contains no host/access-code/serial fields.

Enable LAN Only and Developer Mode where supported by the installed printer
firmware, verify the intended printer's identity and read current status before
authorized live work. Bambu's own explanation describes Developer Mode as
leaving local MQTT/FTP interfaces open for third-party tools, with protocols
outside its official support; this does not establish compatibility for a
particular current device. [Bambu's third-party integration statement](https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/)

For the installed skill's documented A1 Mini workflow, use
`handoff: "template_project"` and an explicit same-printer template:

```json
"template_project": {
  "path": "/absolute/known-good.gcode.3mf",
  "expected_sha256": "ACTUAL_RAW_HASH",
  "printer_model": "A1 Mini",
  "plate": 1
}
```

The declared model must match the target label; plate is 1–16. Native planning
snapshots the exact file and checks its `.gcode.3mf` suffix and ZIP local-header
signature only. Archive contents, selected plate, same-printer compatibility and
generated project are explicitly **unknown** until the external helper validates
them. No template is unpacked or substituted into toolpaths by the native module.
The skill's plain `gcode_file` path is diagnostic for A1 Mini: the installed skill
records rejected/ignored starts, so do not infer that a successful file upload
can start that printer.

After the native reviewed package verifies, the external sequence is:

1. Verify the optional adapter installation and private printer configuration.
2. Read current status and check the intended machine, firmware, storage and
   errors. Resolve failed or unsupported findings before proceeding.
3. Inspect the exact helper dry-run payload against the package's G-code hash,
   intended printer, template and plate. Without `--execute`, `send` prints its
   plan and does not perform upload/start.
4. If upload is explicitly authorized, perform upload-only and verify the result.
5. If the user explicitly requested this specific print start, state the physical
   checks, issue the helper's start request with its required flags, confirm
   acceptance from status/UI and observe heat-up/homing/first layer. If intent,
   device state or validation is unclear, stop before the physical request.

Example dry-run using the installed helper, a reviewed package and configured
printer ID (no real request is performed by this documentation):

```bash
python <installed-bambu-skill>/scripts/bambu_lan_print.py send \
  --printer assessed_printer \
  --gcode /absolute/reviewed-package/original.gcode \
  --handoff template-project \
  --template-project /absolute/reviewed-package/template.gcode.3mf \
  --plate 1 --action upload-start
```

Live upload-only adds `--execute` with `--action upload`. A live start requires
both `--execute --confirm-start-print` and specific human authorization for that
job. An explicit request to print/start supplies that authorization; preparing,
reviewing, slicing or uploading does not. State the checks for a clear build
plate, correct plate/filament/nozzle, safe surroundings and nearby operator before
the live command. MQTT publication is only a request, not proof of acceptance or
physical progress. Any created print artifact also receives the installed
`cad-viewer` handoff, or an explicit report if that viewer cannot start.

No printer was contacted, uploaded to, started or hardware-certified by the
development fixtures. Supervised hardware acceptance remains a separate gate.
