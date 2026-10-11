"""Developer-only generator for the checked-in native gripper documents.

End users load gripper.create.json directly; no Python runs in the CAD service.
Dimensions describe a reference mechanism, not a sourced or certified product.
"""
from __future__ import annotations

import json
import copy
from pathlib import Path


def model_request():
    features = []

    def feature(name, kind, **values):
        features.append(dict(id=name, type=kind, **values))
        return name

    def box(name, origin, size):
        return feature(name, "box", origin=origin, size=size)

    def cylinder(name, radius, height, origin=(0, 0, 0)):
        return feature(name, "cylinder", radius=radius, height=height, origin=list(origin))

    def holes(name, source, centers, radius, depth):
        for i, center in enumerate(centers):
            source = feature(name if i == len(centers) - 1 else f"{name}_hole_{i}", "hole",
                             input=source, origin=center, axis=[0, 0, 1], radius=radius, depth=depth)
        return source

    def ring(name, outer, inner, z, height):
        return holes(name, cylinder(name + "_blank", outer, height, [0, 0, z]),
                     [[0, 0, z]], inner, height)

    def frame(origin=(0, 0, 0)):
        return dict(origin=list(origin), normal=[0, 0, 1], x_direction=[1, 0, 0])

    def rigid(name, parent, child, origin=(0, 0, 0)):
        return dict(id=name, type="rigid", parent=parent, child=child,
                    parent_frame=frame(origin), child_frame=frame())

    def part(name, source, translation=None, rotation=None):
        p = dict(id=name, input=source)
        if translation is not None or rotation is not None:
            p["placement"] = {}
            if translation is not None:
                p["placement"]["translation"] = translation
            if rotation is not None:
                p["placement"]["rotation"] = rotation
        return p

    length = {"parameter": "proximal_length"}
    distal_length = {"parameter": "distal_length"}
    expression = lambda op, args: {"expression": dict(op=op, args=args, unit="mm")}
    subtract = lambda a, b: expression("subtract", [a, b])
    add = lambda a, b: expression("add", [a, b])

    base = box("base_blank", [-60, -42, 0], [120, 72, {"parameter": "base_thickness"}])
    base = holes("base_mounting_holes", base,
                 [[x, y, 0] for x in (-50, 50) for y in (-32, 20)] +
                 [[x + dx, 12 + dy, 0] for x in (-34, 34) for dx in (-8, 8) for dy in (-8, 8)],
                 1.7, {"parameter": "base_thickness"})
    base = holes("base_plate", base, [[-34, 12, 0], [34, 12, 0]], 2.7,
                 {"parameter": "base_thickness"})
    mount = box("mount_blank", [-13, -13, 0], [26, 26, 6])
    mount = holes("finger_mount", mount, [[x, y, 0] for x in (-8, 8) for y in (-8, 8)], 1.7, 6)
    mount = holes("mount", mount, [[0, 0, 0]], 2.7, 6)

    def link(name, half_width, length_value, z, height):
        middle = box(name + "_web", [-half_width, 0, z], [half_width * 2, length_value, height])
        bottom = cylinder(name + "_heel", half_width, height, [0, 0, z])
        tip = cylinder(name + "_tip", half_width, height, [0, length_value, z])
        a = feature(name + "_outline_a", "fuse", left=middle, right=bottom)
        a = feature(name + "_outline", "fuse", left=a, right=tip)
        a = holes(name + "_bores", a, [[0, 0, z], [0, length_value, z]], 3.5, height)
        pocket = box(name + "_pocket", [-3, 10, z + 2], [6, subtract(length_value, 20), height - 2])
        return feature(name, "cut", left=a, right=pocket)

    proximal = link("proximal", 7, length, 8, 8)
    distal = link("distal", 6, distal_length, 17, 6)
    # A top bridge contacts the distal top face over finite area; skirts sit
    # outside its 12 mm width. Symmetric ribs let both fingers reuse one pad.
    pad = box("grip_pad_bridge", [-7.5, subtract(distal_length, 2), 23], [15, 17, 4])
    for side, x in (("left", -7.5), ("right", 6)):
        skirt = box("pad_skirt_" + side, [x, subtract(distal_length, 2), 17], [1.5, 17, 6])
        pad = feature("pad_with_" + side + "_skirt", "fuse", left=pad, right=skirt)
    for side, x in (("left", -8.2), ("right", 7.5)):
        for index, z in enumerate((18, 21)):
            rib = box(f"pad_rib_{side}_{index}", [x, add(distal_length, 6), z], [0.7, 9, 1])
            last = side == "right" and index == 1
            pad = feature("pad" if last else f"pad_with_{side}_rib_{index}", "fuse", left=pad, right=rib)

    ring("pivot_seat", 8, 2.7, 6, 2)
    ring("pivot_cap", 8, 2.7, 16, 2)
    ring("proximal_bush", 3.4, 2.6, 8, 8)
    ring("distal_bush", 3.4, 2.6, 17, 6)
    ring("joint_washer", 4.5, 2.6, 16, 1)
    ring("lower_washer", 4.5, 2.6, 7, 1)
    cylinder("support_post", 2.5, 10, [0, 0, 6])
    shaft = cylinder("pivot_shaft", 2.5, 21, [0, 0, -1])
    head = cylinder("pivot_head", 4.5, 2, [0, 0, 20])
    feature("pivot_pin", "fuse", left=shaft, right=head)
    shaft = cylinder("knuckle_shaft", 2.5, 17, [0, 0, 7])
    head = cylinder("knuckle_head", 4.5, 2, [0, 0, 24])
    feature("knuckle_pin", "fuse", left=shaft, right=head)
    shaft = cylinder("screw_shaft", 1.4, 13, [0, 0, -7])
    head = cylinder("screw_head", 2.6, 3, [0, 0, 6])
    feature("mount_screw", "fuse", left=shaft, right=head)

    parts = [part("mount", "mount"), part("proximal", proximal), part("distal", distal), part("pad", pad)]
    mates = [dict(id="base_hinge", type="revolute", parent="mount", child="proximal",
                  parent_frame=frame(), child_frame=frame(), angle_deg={"parameter": "curl_angle"}, angle_limits_deg=[-10, 0]),
             dict(id="knuckle", type="revolute", parent="proximal", child="distal",
                  parent_frame=frame([0, length, 0]), child_frame=frame(), angle_limits_deg=[-10, 0]),
             rigid("pad_mount", "distal", "pad")]

    for name, source, parent, origin in [
        ("lower_seat", "pivot_seat", "mount", [0, 0, 0]),
        ("upper_cap", "pivot_cap", "mount", [0, 0, 0]),
        ("base_pin", "pivot_pin", "mount", [0, 0, 0]),
        ("base_bush", "proximal_bush", "proximal", [0, 0, 0]),
        ("tip_bush", "proximal_bush", "proximal", [0, length, 0]),
        ("knuckle_pin", "knuckle_pin", "proximal", [0, length, 0]),
        ("knuckle_washer", "joint_washer", "proximal", [0, length, 0]),
        ("knuckle_lower_washer", "lower_washer", "proximal", [0, length, 0]),
        ("distal_bush", "distal_bush", "distal", [0, 0, 0]),
        ("tip_distal_bush", "distal_bush", "distal", [0, distal_length, 0]),
        ("rear_post_left", "support_post", "mount", [-10, 0, 0]),
        ("rear_post_right", "support_post", "mount", [10, 0, 0]),
    ]:
        parts.append(part(name, source))
        mates.append(rigid(name + "_attachment", parent, name, origin))
    for i, (x, y) in enumerate(((-8, -8), (8, -8), (-8, 8), (8, 8))):
        name = f"mount_screw_{i + 1}"
        parts.append(part(name, "mount_screw"))
        mates.append(rigid(name + "_attachment", "mount", name, [x, y, 0]))
    # The mirrored moving definition shares every exact source part. Reversing
    # both joint datum normals changes handedness without reflecting solids or
    # placing the mounting bracket above the finger.
    feature("finger", "assembly", parts=parts, mates=mates,
            couplings=[dict(id="curl", source=dict(mate_id="base_hinge", coordinate="angle_deg"),
                            target=dict(mate_id="knuckle", coordinate="angle_deg"), ratio=1)],
            poses=[dict(id=name, values=[dict(mate_id="base_hinge", coordinate="angle_deg", value=value)])
                   for name, value in (("open", 0), ("approach", -5), ("grasp", -10))],
            bom=[dict(input=source, part_number="GR-" + source.upper(), description=description, material=material)
                 for source, description, material in [
                     ("mount", "Machined finger mount", "Aluminum, design assumption"),
                     ("proximal", "Pocketed proximal link", "Polymer, process TBD"),
                     ("distal", "Pocketed distal link", "Polymer, process TBD"),
                     ("pad", "Ribbed replaceable grip pad", "Elastomer, process TBD"),
                     ("mount_screw", "Unsourced simplified 3 mm screw envelope", "Steel, assumed"),
                 ]])
    right = copy.deepcopy(features[-1])
    right["id"] = "right_finger"
    for mate in right["mates"]:
        if mate["type"] == "revolute":
            for key in ("parent_frame", "child_frame"):
                mate[key]["normal"] = [0, 0, -1]
                mate[key]["x_direction"] = [-1, 0, 0]
    features.append(right)

    outer = box("housing_outer", [-18, -38, -24], [36, 36, 22])
    inner = box("housing_inner", [-16, -36, -22], [32, 32, 20])
    feature("actuator_housing", "cut", left=outer, right=inner)
    box("actuator_envelope", [-11, -32, -20], [22, 24, 16])
    box("housing_bottom", [-18, -38, -26], [36, 36, 2])
    cylinder("spacer", 3, 2, [0, 0, -2])
    root_parts = [part("base", base),
                  part("left", "finger", [-34, 12, {"parameter": "base_thickness"}]),
                  part("right", "right_finger", [34, 12, {"parameter": "base_thickness"}]),
                  part("housing", "actuator_housing"), part("actuator", "actuator_envelope"),
                  part("bottom", "housing_bottom")]
    for i, (x, y) in enumerate(((-13, -33), (13, -33), (-13, -7), (13, -7))):
        root_parts.append(part(f"housing_spacer_{i + 1}", "spacer", [x, y, 0]))
    feature("gripper", "assembly", parts=root_parts,
            bom=[dict(input="base_plate", part_number="GR-BASE", description="Mounting plate", material="Aluminum, assumed"),
                 dict(input="actuator_envelope", part_number="GR-ACTUATOR-ENVELOPE",
                      description="Unsourced actuator clearance envelope; no drive transmission", material="Unspecified")])
    return dict(document_id="articulated_gripper", model=dict(schema_version=1, units="mm",
                parameters=dict(proximal_length=48, distal_length=32, base_thickness=8, curl_angle=0),
                features=features, output="gripper"))


def artifacts():
    request = model_request()
    return {"gripper.create.json": request,
            "gripper.close.edit.json": dict(document_id=request["document_id"], expected_revision=1,
                operations=[dict(op="apply_pose", assembly_id=name, pose_id="grasp")
                            for name in ("finger", "right_finger")]),
            "gripper.review.json": dict(document_id=request["document_id"], revision=1, feature_id="proximal",
                options=dict(profile=dict(process="fdm", orientation=dict(build_direction=[0, 0, 1], x_direction=[1, 0, 0]),
                    build_envelope_mm=[180, 180, 180], minimum_wall_mm=1.5, overhang_angle_deg=45))),
            "gripper.manufacture.json": dict(document_id=request["document_id"], revision=1,
                options=dict(drawings=False, formats=["step", "stl"],
                    notes=["Reference geometry only; unsourced hardware envelopes and no physical drive transmission.",
                           "Process and material choices require actual machine/vendor data before fabrication."],
                    parts=[dict(feature_id="proximal", process="fdm", material="Polymer, TBD"),
                           dict(feature_id="distal", process="fdm", material="Polymer, TBD"),
                           dict(feature_id="base_plate", process="cnc", material="Aluminum, assumed")]))}


if __name__ == "__main__":
    for name, value in artifacts().items():
        Path(__file__).with_name(name).write_text(json.dumps(value, indent=2) + "\n")
