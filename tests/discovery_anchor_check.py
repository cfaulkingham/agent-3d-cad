"""Verify final discovery anchors with independent Draft 2020-12 validation.

Developer-only: compile the exact final-anchor block from src/service.cpp in a
private JSON-only harness, then prove reversibility and compare independent
validation. The native product has no Python runtime dependency.

Usage: python tests/discovery_anchor_check.py --source .
       python tests/discovery_anchor_check.py --source . --catalog before-tools.json
"""
import argparse
import copy
import hashlib
import json
import pathlib
import random
import re
import subprocess
import tempfile

from jsonschema import Draft202012Validator

CHECKS = 0
MAPS = {"$defs", "properties", "patternProperties", "dependentSchemas"}
LITERALS = {"const", "enum", "default", "examples"}
SCOPES = {"$id", "$anchor", "$dynamicAnchor", "$dynamicRef", "$recursiveAnchor", "$recursiveRef"}


def require(value, reason):
    global CHECKS
    CHECKS += 1
    if not value:
        raise AssertionError(reason)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def visit(value, inspect):
    if isinstance(value, list):
        for child in value:
            visit(child, inspect)
    elif isinstance(value, dict):
        inspect(value)
        for key, child in value.items():
            if key in MAPS and isinstance(child, dict):
                for body in child.values():
                    visit(body, inspect)
            elif key not in LITERALS:
                visit(child, inspect)


def scoped(value):
    found = []
    visit(value, lambda node: found.append(True) if SCOPES.intersection(node) else None)
    return bool(found)


def object_schema(properties, required=(), **keywords):
    return {"type": "object", "properties": properties, "required": list(required),
            "additionalProperties": False, **keywords}


def fixture(name, schema, probes=(), unchanged=False):
    return {"name": name, "schema": schema, "probes": list(probes), "unchanged": unchanged}


def fixtures():
    small = {"type": "integer", "minimum": 0, "maximum": 10}
    base = object_schema({name: {"$ref": "#/$defs/private"} for name in ("a", "b", "c", "d")},
                         **{"$defs": {"private": copy.deepcopy(small)}})
    probes = [{}, {"a": 0}, {"b": 10}, {"c": -1}, {"d": 11}, {"a": "bad"}, {"a": 1, "b": 2}]
    result = [fixture("profitable_whole_refs", base, probes)]
    siblings = copy.deepcopy(base)
    siblings["properties"]["a"]["maximum"] = 3
    result.append(fixture("reference_sibling_conjunction", siblings, probes + [{"a": 3}, {"a": 4}]))
    pointer = copy.deepcopy(base)
    pointer["$defs"]["private"] = object_schema({"value": small})
    pointer["properties"]["leaf"] = {"$ref": "#/$defs/private/properties/value"}
    result.append(fixture("pointer_suffix_preserved", pointer, [{"leaf": 1}, {"leaf": -1}, {"a": {"value": 1}}, {"a": {"value": -1}}]))
    recursive = object_schema({"first": {"$ref": "#/$defs/node"}, "second": {"$ref": "#/$defs/node"}, "third": {"$ref": "#/$defs/node"}},
        **{"$defs": {"node": object_schema({"value": small, "child": {"$ref": "#/$defs/node"}}, ("value",))}})
    result.append(fixture("recursive_definitions", recursive, [{}, {"first": {"value": 1}}, {"first": {"value": 1, "child": {"value": 2}}},
        {"first": {"value": 1, "child": {"value": "bad"}}}, {"second": {"child": {"value": 1}}}]))
    mutual = object_schema({"a": {"$ref": "#/$defs/a"}, "b": {"$ref": "#/$defs/a"}, "c": {"$ref": "#/$defs/b"}},
        **{"$defs": {"a": object_schema({"value": {"type": "integer"}, "other": {"$ref": "#/$defs/b"}}),
                     "b": object_schema({"value": {"type": "string"}, "other": {"$ref": "#/$defs/a"}})}})
    result.append(fixture("mutual_recursive_definitions", mutual, [{}, {"a": {"value": 1, "other": {"value": "yes", "other": {"value": 2}}}},
        {"a": {"other": {"value": 1}}}, {"c": {"value": "yes"}}, {"c": {"value": 1}}]))
    literals = copy.deepcopy(base)
    literal = {"$ref": "#/$defs/private", "$anchor": "literal_anchor", "$id": "urn:literal", "properties": {"a": {"$ref": "#/$defs/private"}}}
    literals["properties"].update({"const": {"const": literal}, "enum": {"enum": [literal, False, [literal]]},
        "default": {"type": "integer", "default": literal}, "examples": {"type": "integer", "examples": [literal]}})
    result.append(fixture("literal_json_preserved", literals, probes + [{"const": literal}, {"const": {}}, {"enum": literal}, {"enum": False}, {"enum": True}]))
    maps = copy.deepcopy(base)
    maps["properties"].update({name: {"$ref": "#/$defs/private"} for name in ("$ref", "$anchor", "const", "enum", "default", "examples", "properties", "$defs")})
    maps["patternProperties"] = {"^x_": {"$ref": "#/$defs/private"}}
    maps["dependentSchemas"] = {"a": {"properties": {"d": {"$ref": "#/$defs/private"}}}}
    result.append(fixture("schema_maps_and_keyword_property_names", maps, [{name: value} for name in maps["properties"] for value in (1, "bad")] + [{"x_ok": 1}, {"x_bad": -1}]))
    public = copy.deepcopy(base)
    public["$defs"].update({"model": copy.deepcopy(small), "operation": copy.deepcopy(small)})
    public["properties"].update({name: {"$ref": "#/$defs/" + target} for target in ("model", "operation") for name in [target + str(i) for i in range(5)]})
    result.append(fixture("public_roots_preserved", public, [{"model0": 1}, {"model1": -1}, {"operation4": 3}, {"operation0": "bad"}]))
    boolean = object_schema({"yes": {"$ref": "#/$defs/yes"}, "no": {"$ref": "#/$defs/no"}}, **{"$defs": {"yes": True, "no": False}})
    result.append(fixture("boolean_definitions_unchanged", boolean, [{}, {"yes": 1}, {"no": 1}], unchanged=True))
    escaped = object_schema({"a": {"$ref": "#/$defs/a~1b"}, "b": {"$ref": "#/$defs/a~1b"}, "c": {"$ref": "#/$defs/a~1b"}, "tilde": {"$ref": "#/$defs/a~0b"}},
        **{"$defs": {"a/b": small, "a~b": {"type": "string"}}})
    result.append(fixture("escaped_definition_names", escaped, [{"a": 1}, {"b": -1}, {"tilde": "yes"}, {"tilde": 1}]))
    unprofitable = object_schema({"a": {"$ref": "#/$defs/x"}}, **{"$defs": {"x": small}})
    result.append(fixture("unprofitable_unchanged", unprofitable, [{"a": 1}, {"a": -1}], unchanged=True))
    result.append(fixture("no_definitions_unchanged", object_schema({"a": small}), [{"a": 1}, {"a": -1}], unchanged=True))
    for keyword, value in [("$id", "urn:agentcad:anchors:root"), ("$anchor", "root"), ("$dynamicAnchor", "root"), ("$recursiveAnchor", "root")]:
        original = copy.deepcopy(base);original[keyword] = value
        result.append(fixture("preexisting_root_" + keyword, original, probes, unchanged=True))
    nested = copy.deepcopy(base)
    nested["$defs"]["private"]["$anchor"] = "original"
    nested["properties"]["a"] = {"$ref": "#original"}
    result.append(fixture("preexisting_nested_anchor", nested, probes, unchanged=True))
    shadowed = {"$defs": {"private": {"type": "string"}}, "type": "object", "properties": {}}
    for name in ("a", "b", "c"):
        shadowed["properties"][name] = {"$id": "urn:agentcad:anchors:nested:" + name,
            "$defs": {"private": small}, "type": "object", "properties": {"p": {"$ref": "#/$defs/private"}}}
    result.append(fixture("nested_resource_local_references", shadowed, [{"a": {"p": 1}}, {"b": {"p": "bad"}}, {"c": {"p": 11}}], unchanged=True))
    dynamic = {"$dynamicAnchor": "node", "type": "object", "properties": {"value": small, "child": {"$dynamicRef": "#node"}}, "$defs": copy.deepcopy(base["$defs"])}
    result.append(fixture("dynamic_reference_unchanged", dynamic, [{"value": 1, "child": {"value": 2}}, {"child": {"value": "bad"}}], unchanged=True))
    recursive_reference = copy.deepcopy(base)
    recursive_reference["$defs"]["untouched"] = {"$recursiveRef": "#"}
    result.append(fixture("recursive_reference_unchanged", recursive_reference, probes, unchanged=True))
    rng = random.Random(890174)
    for index in range(150):
        definitions = {}; properties = {}
        for i in range(rng.randrange(1, 90)):
            name = str(i) if index % 5 == 0 else "definition_" + str(i)
            definitions[name] = copy.deepcopy(rng.choice([small, {"type": "string", "minLength": 2}, True, False, {"enum": [0, 1, "yes"]}]))
            for j in range(rng.randrange(1, 7)):
                properties["field_" + str(i) + "_" + str(j)] = {"$ref": "#/$defs/" + name}
                if j % 3 == 0 and isinstance(definitions[name], dict):
                    properties["field_" + str(i) + "_" + str(j)]["description"] = "Reference sibling must remain exact"
        schema = object_schema(properties, **{"$defs": definitions})
        samples = [{}, {"unexpected": 1}]
        names = list(properties)
        for _ in range(10):
            selected = rng.sample(names, min(len(names), rng.randrange(1, 6)))
            samples.append({name: rng.choice([0, 1, -1, "a", "yes", None, False, {}]) for name in selected})
        result.append(fixture("anchor_fuzz_" + str(index), schema, samples))
    return result


def production(source, compiler, include, values):
    service = (source / "src/service.cpp").read_text(encoding="utf-8")
    marker = "    // Final local anchors shorten profitable whole-definition references."
    start = service.index(marker)
    end = service.index("    // Factor context-independent assertions into local reference siblings.", start)
    block = service[start:end]
    code = """#include <nlohmann/json.hpp>
static_assert(NLOHMANN_JSON_VERSION_MAJOR==3 && NLOHMANN_JSON_VERSION_MINOR==12 && NLOHMANN_JSON_VERSION_PATCH==0);
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <vector>
#include <string_view>
#include <iostream>
using Json=nlohmann::json;
void compact(Json& schema){for(int once=0;once<1;++once){
""" + block + """\n}}
int main(){Json schemas;std::cin>>schemas;for(auto& schema:schemas)compact(schema);std::cout<<schemas.dump()<<"\\n";}
"""
    if include is None:
        choices = [source / ".deps/json-3.12.0/include", source / "build/_deps/nlohmann_json-src/include"]
        include = next((path for path in choices if (path / "nlohmann/json.hpp").is_file()), None)
    require(include is not None and (include / "nlohmann/json.hpp").is_file(), "Pinned JSON headers missing; pass --json-include")
    with tempfile.TemporaryDirectory(prefix="agentcad-anchor-proof-") as tmp:
        root = pathlib.Path(tmp);cpp, exe = root / "proof.cpp", root / "proof"
        cpp.write_text(code, encoding="utf-8")
        subprocess.run([compiler, "-std=c++20", "-O2", "-I", str(include), str(cpp), "-o", str(exe)], check=True, timeout=120)
        data = canonical(values)
        first = subprocess.run([str(exe)], input=data, text=True, capture_output=True, check=True, timeout=30).stdout
        second = subprocess.run([str(exe)], input=data, text=True, capture_output=True, check=True, timeout=30).stdout
        require(first == second, "Production anchor pass is nondeterministic")
        repeated = subprocess.run([str(exe)], input=first, text=True, capture_output=True, check=True, timeout=30).stdout
        require(first == repeated, "Production anchor pass is not idempotent")
    return json.loads(first), hashlib.sha256(block.encode()).hexdigest(), hashlib.sha256(service.encode()).hexdigest()


def reverse_generated_anchors(old, new, name):
    reversed_schema = copy.deepcopy(new);before_defs = old.get("$defs", {});after_defs = reversed_schema.get("$defs", {})
    require(set(before_defs) == set(after_defs), name + ": definition names changed")
    anchors = {}
    for key, body in after_defs.items():
        previous = before_defs[key]
        if isinstance(body, dict) and "$anchor" in body and (not isinstance(previous, dict) or "$anchor" not in previous):
            anchor = body.pop("$anchor")
            require(key not in ("model", "operation"), name + ": public root received an anchor")
            require(isinstance(anchor, str) and re.fullmatch(r"[A-Za-z][A-Za-z0-9._:-]*", anchor) is not None, name + ": invalid anchor name")
            require(anchor not in anchors, name + ": duplicate generated anchor")
            anchors[anchor] = "#/$defs/" + key
    def undo(node):
        if isinstance(node.get("$ref"), str) and node["$ref"].startswith("#"):
            anchor = node["$ref"][1:]
            if anchor in anchors:
                node["$ref"] = anchors[anchor]
    visit(reversed_schema, undo)
    require(canonical(old) == canonical(reversed_schema), name + ": reversing anchors/references did not recover the exact original")
    return len(anchors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--json-include", type=pathlib.Path)
    parser.add_argument("--catalog", type=pathlib.Path, help="Optional pre-anchor native tool catalog to quantify and prove reversibility")
    args = parser.parse_args();cases = fixtures();catalog = None
    if args.catalog:
        catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
        for tool in catalog:
            for key in ("inputSchema", "outputSchema"):
                cases.append(fixture(tool["name"] + "." + key, tool[key]))
    output, block_hash, source_hash = production(args.source.resolve(), args.compiler, args.json_include, [case["schema"] for case in cases])
    require(len(output) == len(cases), "Production changed fixture count")
    acceptance = 0;generated = 0
    for case, new in zip(cases, output):
        old = case["schema"];name = case["name"]
        Draft202012Validator.check_schema(old);Draft202012Validator.check_schema(new)
        if case["unchanged"] or scoped(old):
            require(canonical(old) == canonical(new), name + ": protected/scoped schema changed")
        require(len(canonical(new)) <= len(canonical(old)), name + ": anchor pass increased schema bytes")
        generated += reverse_generated_anchors(old, new, name)
        a, b = Draft202012Validator(old), Draft202012Validator(new)
        for probe in case["probes"] + [None, 0, False, True, "x", [], {}, {"extra": 1}]:
            require(a.is_valid(probe) == b.is_valid(probe), name + ": acceptance differs for " + canonical(probe));acceptance += 1
    by_name = {case["name"]: schema for case, schema in zip(cases, output)}
    require("$anchor" in output[0]["$defs"]["private"], "Profitable reference optimization did not run")
    for name in ("recursive_definitions", "mutual_recursive_definitions", "schema_maps_and_keyword_property_names", "literal_json_preserved", "pointer_suffix_preserved"):
        require(any(isinstance(body, dict) and "$anchor" in body for body in by_name[name]["$defs"].values()), name + ": eligible anchors were incorrectly skipped")
    require(by_name["pointer_suffix_preserved"]["properties"]["leaf"]["$ref"] == "#/$defs/private/properties/value", "Pointer suffix reference changed")
    require(output[0]["properties"]["a"]["$ref"].startswith("#") and not output[0]["properties"]["a"]["$ref"].startswith("#/$defs/"), "Whole definition reference was not shortened")
    # Independent validator must expose wrong anchor targets and changed sibling constraints.
    bad = copy.deepcopy(output[1]);bad["properties"]["a"]["maximum"] = 10
    require(not Draft202012Validator(cases[1]["schema"]).is_valid({"a": 4}) and Draft202012Validator(bad).is_valid({"a": 4}), "Negative control missed reference sibling weakening")
    bad = copy.deepcopy(output[0]);bad["$defs"]["private"]["minimum"] = -1
    require(not Draft202012Validator(cases[0]["schema"]).is_valid({"a": -1}) and Draft202012Validator(bad).is_valid({"a": -1}), "Negative control missed referenced body weakening")
    report = {"checks": CHECKS, "fixtures": len(cases), "acceptance_probes": acceptance, "generated_anchors": generated,
              "production_block_sha256": block_hash, "source_service_sha256": source_hash}
    if catalog is not None:
        after = copy.deepcopy(catalog);index = len(cases) - 2 * len(catalog)
        for tool in after:
            for key in ("inputSchema", "outputSchema"):
                tool[key] = output[index];index += 1
        report.update(catalog_before_bytes=len(canonical(catalog)), catalog_after_bytes=len(canonical(after)),
                      catalog_saved_bytes=len(canonical(catalog)) - len(canonical(after)))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
