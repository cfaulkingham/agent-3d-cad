# Declarative review playback

The live Sequences panel saves native mechanism and presentation keyframes.
Play, Pause, Seek, Speed and Loop preview these definitions in the current view.
The clock is local to the viewer; reopening restores the last native sample and
its speed/loop options **paused**. This is visual kinematic review. It does not
simulate forces, collisions, swept clearance, acceleration or physical safety.
No model or artifact JavaScript, expressions, callbacks or commands execute.

## Saved definitions

`cad_viewer` accepts a closed `action:"sequence"` with current `view_id`,
`evaluation_id` and `operation`. No public tool was added. Operations are:

| Operation | Additional fields | Result |
|---|---|---|
| `list` | None | Qualified current context, including `sequences` and `playback` |
| `save` | `sequence:{name,frames}` | Create or replace the named definition |
| `delete` | `name` | Delete the named definition; retire its current position |
| `options` | `name`, optional `speed`, `loop` | Store speed in [0.1,4] and boolean looping |
| `seek` | `name`, `time_s` | Admit one native sample, or apply a pure presentation sample |

A persisted view/document holds at most 16 definitions. Names use 1–64 UTF-8
bytes without control characters or all-whitespace content. Each input definition
is at most 256 KiB and contains 2–64 keyframes. Times start at zero, strictly
increase, and end at most 3,600 seconds. Every frame contains exactly `time_s`,
`presentation` and `joints`. Presentation uses the existing closed clip/explode
contract; appearance, camera, visibility and annotations remain independent.

```json
{"action":"sequence","view_id":"main","evaluation_id":"eval_current",
 "operation":"save","sequence":{"name":"Coordinated review","frames":[
  {"time_s":0,"presentation":{"clip":null,"explode":{"distance_mm":0,"directions":[]}},
   "joints":[{"assembly_id":"mechanism","values":[
    {"mate_id":"hinge","coordinate":"angle_deg","value":0},
    {"mate_id":"spindle_joint","coordinate":"travel_mm","value":0}]}]},
  {"time_s":2,"presentation":{"clip":null,"explode":{"distance_mm":20,"directions":[]}},
   "joints":[{"assembly_id":"mechanism","values":[
    {"mate_id":"hinge","coordinate":"angle_deg","value":90},
    {"mate_id":"spindle_joint","coordinate":"travel_mm","value":18}]}]}
 ]}}
```

Each frame may select up to 16 reachable mechanism definitions. For every
selected definition it supplies **all independent coordinates exactly once**,
and never a driven coordinate. All selected frames retain the same definitions
and coordinate keys. At most 126 independent coordinates are supported in total
per frame. `assembly_id` names a source definition: repeated occurrences of that
definition share its joints, as in ordinary composed motion controls. Different
definitions can move together. Omitted definitions keep committed source values.
Endpoints must satisfy native joint limits, couplings and source validation.

Joint coordinates, exploded distance and clipping offset interpolate linearly
between adjacent frames. Coordinates match by definition/mate/coordinate identity,
regardless of endpoint array order. Clip enablement, unit normal and kept side,
and the complete set of explicit unit explode directions remain fixed throughout
a definition. A `joints:[]` sequence controls presentation only and keeps the
current native pose. Native geometry is still required; external read-only
artifact sessions cannot save or play native sequences.

## Qualification and source preservation

Saving requires the current committed display. The native service adds a closed
`source` containing `document_id`, `revision`, `feature_id`, original
`evaluation_id` and the SHA-256 of the canonical committed model JSON. Callers
cannot provide or replace this identity. Each seek revalidates the committed
document, revision, output feature, model hash, current displayed evaluation and
complete sampled coordinate vector under existing publication locks.

A source revision change retires the displayed timeline position. Old definitions
remain inspectable with their original source identities, and seek/options reject
them explicitly. They are never rebound or silently pruned into new intent.
Retargeting the view to another document clears definitions and playback state.
Save/delete/list replies always use current display headers, even when ordinary
saved `cad_context` remains explicitly stale. Lists and deletes also work on a
qualified draft. Definitions, options and seeks never write source HEAD.

Joint samples use the existing bounded, cancellable native motion-preview worker
path, starting from the committed base model rather than accumulating prior
draft edits. Old geometry picks are cleared before admission. Successful native
mesh publication supplies a new draft evaluation and complete preview operations.
Saving a pose or resetting uses the existing explicit motion workflow; it stops
playback and retires the timeline claim. Pure presentation samples change no
geometry or evaluation ID. Their geometry selections retain original source
coordinates, and apparent exploded gaps never become measured clearance.

Ready sync and context expose `sequences` and nullable `playback`:

```json
{"name":"Coordinated review","time_s":1,"speed":1.5,"loop":true,
 "source":{"document_id":"articulated_arm","revision":1,
  "evaluation_id":"eval_original","feature_id":"mechanism","model_sha256":"…"},
 "state":"displayed"}
```

`state:"unapplied"` selects options without claiming evaluated geometry;
`pending` identifies an admitted but unevaluated sample; `displayed` confirms the
native pose currently shown. Agent snapshots include this time and source
identity; changed time invalidates a pending request even if the evaluation did
not change. Errors do not claim the target sample as displayed.

The viewer uses a monotonic clock and admits only one native evaluation at a
time. It polls loading work without queueing intermediate samples. Speed scales
time; Loop wraps within the definition duration; non-looping playback stops at
the endpoint. Heavy geometry can skip visual samples and is not real-time
playback. Pause prevents future admission while an already admitted native sample
finishes. A lost acknowledgment pauses the clock and reconciles through read-only
sync; it never repeats an uncertain mutation.

New joint evaluations retire exact sections and measurements. A pure presentation
seek retains matching native caps, and changed plane/exploded placement retires
and cancels them outside writer locks. Camera, appearance and visibility do not
retire geometry. Manual presentation or preset changes stop playback; changed
presentation retires its timeline claim. Screenshots capture the actual current
frame through the existing renderer, with no claim of video export.

## Capturing frames in the viewer

Start at time zero and use Add current pose. Adjust native joints or apply a saved
pose, wait for evaluation, choose a later time and capture it. Presentation-only
changes can also be captured. Capturing omits driven joints automatically.
After collecting at least two frames, Reset pose to the committed source before
Save sequence. The UI retains captures across draft evaluations at the same
source revision. A revision or document change clears unfinished captures.

Native, controller, real MCP, schema, SDK and relocated-package evidence is
recorded in HANDOFF. Those checks do not establish rendered browser or host
acceptance; actual browser execution remains a separate integration gate.
