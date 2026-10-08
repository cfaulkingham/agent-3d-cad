# Native robot-description handoff

`cad_robot_export(document_id, revision, robot, feature_id?)` exports one saved
assembly using the same native datum frames and source solids as CAD queries.
`robot.format` is `urdf`, `srdf`, or `sdf`. URDF and SRDF requests both create the
paired `model.urdf` and `model.srdf`; `path` selects the requested one. SDF creates
`model.sdf`, targeting SDFormat 1.12. No Python or model-supplied program executes.
This is a description and mesh handoff, not a dynamics or planning service.

The returned directory contains relative mesh references, source STL meshes,
`robot.json` (the design/planning ledger), and a manifest with file sizes and
SHA-256 hashes. Move the whole directory together. The complete directory is
published after successful native evaluation and cancellation checking. Failure
removes staging and preserves HEAD, history and earlier export directories.
`cad_job` supports the same arguments for bounded asynchronous execution.

## Frame and coordinate ledger

- Native model dimensions and STL vertices are millimeters. XML translations
  are meters, angular coordinates radians, and mesh scale is explicitly 0.001.
- Each physical link is `part_<part_id>`. A mated link's frame is its authored
  child datum; an unmated link uses its source frame. Visual and collision
  origins undo that datum transform so the source mesh remains reusable.
- Unmated roots retain their native placements and attach through fixed joints
  to `world`. URDF contains a frame-only `world` link; SDF uses the world parent.
- A cylindrical mate becomes a revolute joint `mate_<id>_angle`, an intermediate
  `carrier_<id>` link, and a prismatic joint `mate_<id>_travel`. Both degrees of
  freedom, limits and couplings remain explicit. The carrier has no geometry.
- The exported zero pose equals the saved CAD pose. For each coordinate,
  `q_export = (q_native - source_value) * si_scale`. The ledger records those
  values and all native frame matrices (row-major, mm translations). Limits and
  named poses are shifted and scaled by this same equation.
- Couplings become mimic relationships. Their multiplier is the native ratio
  times `target_si_scale / source_si_scale`. Their exported offset is zero:
  both coordinates were centered on a saved pose that already satisfies the
  native coupling, including its authored offset. No coupling is dropped.
- SDF link poses explicitly reference `__model__`; each joint frame explicitly
  references its child. Joint axes are the local positive Z direction.

These conventions follow the [URDF parser's joint contract](https://github.com/ros/urdfdom/blob/master/urdf_parser/src/joint.cpp)
and [SDFormat 1.12 joint and mimic semantics](https://sdformat.org/spec/1.12/joint/).
Consumer support for mimic joints, especially mixed angular/linear relationships,
must be checked for the chosen simulator/controller.

## Physical inputs are explicit

`robot.joint_properties` must provide exactly one record per moving coordinate:

```json
{"mate_id":"hinge","coordinate":"angle_deg","effort":5,"velocity":0.2}
```

Effort is maximum torque in N·m for angular coordinates or force in N for linear
coordinates. Velocity is rad/s or m/s, respectively. Effort must be nonnegative
and velocity positive. These are supplied physical limits, not values inferred
from the CAD joint's travel range. No defaults are inserted.

URDF may omit inertials for kinematic review. SDF requires an inertial for every
physical part and every cylindrical carrier, so a simulator cannot silently
substitute its default mass. Supply `robot.inertials` records in this form:

```json
{"link":"part_lever","mass_kg":0.1,"center_of_mass_m":[0,0,0],
 "inertia_kg_m2":[0.00002,0.00003,0.00004,0,0,0]}
```

The center of mass is expressed in the exported link frame. The symmetric
inertia tensor is about that center, aligned to the link axes, in order
`[ixx, iyy, izz, ixy, ixz, iyz]`. Mass must be positive; the tensor must be
positive definite and satisfy physical principal-moment triangle inequalities.
Unknown/duplicate links and coordinates fail explicitly. Supplied properties
are saved verbatim in the ledger; their numerical validity does not prove they
describe the real mechanism. In particular, a cylindrical carrier's inertia
must come from a deliberate physical decomposition, not an invented tiny mass.

## SRDF planning ledger

The paired SRDF contains a `mechanism` group covering the assembly's moving
joints, and its named poses in exported SI coordinates. Group states specify
independent joints only; URDF mimic relationships determine driven coordinates.
Rigid assemblies use their physical links as group members. This group supports
whole-mechanism pose inspection, not an inferred arm/gripper or IK chain.
No end effector, TCP, controller, virtual mobility, passive-joint classification,
or disabled-collision matrix is invented. No MoveIt configuration is generated.

## Example and bounds

Create `examples/articulated-arm.create.json`, then call the tool with
`examples/articulated-arm.robot.json`. The example's effort and velocity values
are **illustrative test values**, not measured motor ratings. It deliberately
omits mass/inertia and makes no dynamics claim. To request SDF, supply physically
justified inertials for `part_ground`, `part_lever`, `part_slide`, `part_spindle`
and `carrier_spindle_joint`, then change the format to `sdf`.

The native assembly limits apply. Exports have at most 64 distinct source meshes,
64 MiB per artifact and 256 MiB total. Source STL uses the existing 0.1 mm linear
and 0.5 rad angular tessellation tolerances; XML frames remain exact to floating
point serialization. Collision meshes equal visual meshes. No convex
decomposition, clearance analysis or dynamics validation is implied.

`tests/robot_export_test.py` parses the output with Python's independent XML
parser and computes URDF/SDF forward kinematics from the XML, comparing source
mesh placements against native geometry at saved, named and sampled poses. It
also checks mixed-unit mimic conversions, cylindrical decomposition, multiple
roots, rotated/offset datums, STL units, physical-data rejection, unchanged
history and asynchronous export. Python is a developer test dependency only.
External ROS/MoveIt/Gazebo validation remains separate from these checks.
