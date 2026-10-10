"""Verify exact production object-union hoisting with jsonschema==4.25.1.

Developer-only: compile statements extracted from src/service.cpp in a private
JSON-only harness, then compare independent Draft 2020-12 acceptance. No CAD
worker, repository build, service or packaged runtime dependency is involved.

Usage: python tests/discovery_hoist_check.py --source .
"""
import argparse
import copy
import hashlib
import itertools
import json
import pathlib
import subprocess
import tempfile

from jsonschema import Draft202012Validator

checks = 0
MISSING = object()
MAPS = {"$defs", "properties", "patternProperties", "dependentSchemas"}
LITERALS = {"const", "enum", "default", "examples"}


def require(value, reason):
    global checks
    checks += 1
    if not value:
        raise AssertionError(reason)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def object_schema(properties, required=(), extra=False, **keywords):
    return {"type": "object", "properties": properties, "required": list(required),
            "additionalProperties": extra, **keywords}


def branches(union="oneOf", parent=None, **keywords):
    common = {"type": "string", "minLength": 3, "maxLength": 6, "pattern": "^[a-z]+$"}
    values = [object_schema({"tag": {"const": tag}, "shared": copy.deepcopy(common),
                             "local": {"type": "integer", "minimum": i}},
                            ("tag", "shared")) for i, tag in enumerate(("a", "b"))]
    values.append(object_schema({"tag": {"const": "c"}, "other": {"type": "integer"}}, ("tag",)))
    return {**(parent or {}), union: values, **keywords}


def fixture(name, schema, fields=None, probes=None, unchanged=False):
    return {"name": name, "schema": schema, "fields": fields or {}, "probes": probes or [], "unchanged": unchanged}


def fixtures():
    standard = {"tag": ["a", "b", "c", "d", 1], "shared": ["abc", "a", "abcdefg", "ABCD", 3],
                "local": [0, 1, "x"], "other": [0, "x"]}
    result = [fixture("closed_different_allowed_fields", branches(), standard)]
    both = branches("anyOf")
    result.append(fixture("anyof_different_allowed_fields", both, standard))
    different = branches()
    different["oneOf"][1]["properties"]["shared"]["minLength"] = 5
    result.append(fixture("different_constraints", different, standard))
    overlap = branches("anyOf")
    overlap["anyOf"] = overlap["anyOf"][:2]
    for item in overlap["anyOf"]:
        item["properties"].pop("tag")
        item["required"] = ["shared"]
    result.append(fixture("anyof_overlapping_branches", overlap, {"shared": standard["shared"], "local": standard["local"]}))
    duplicate = copy.deepcopy(overlap)
    duplicate["oneOf"] = duplicate.pop("anyOf")
    result.append(fixture("oneof_overlapping_branches", duplicate, {"shared": standard["shared"], "local": standard["local"]}))
    opened = branches()
    opened["oneOf"][2]["additionalProperties"] = True
    result.append(fixture("open_branch", opened, standard))
    pattern = branches()
    pattern["oneOf"][2]["patternProperties"] = {"^shared$": {"type": "integer"}}
    result.append(fixture("pattern_branch", pattern, standard))
    parent_closed = branches(parent={"type": "object", "properties": {"tag": {"type": "string"}}, "additionalProperties": False})
    result.append(fixture("closed_parent", parent_closed, standard))
    parent_schema = branches(parent={"type": "object", "properties": {"tag": {"type": "string"}}, "additionalProperties": {"type": "integer"}})
    result.append(fixture("schema_parent_additional_properties", parent_schema, standard))
    declared_parent = branches(parent={"type": "object", "properties": {
        "tag": {"type": "string"}, "shared": copy.deepcopy(branches()["oneOf"][0]["properties"]["shared"])},
        "additionalProperties": {"type": "integer"}})
    result.append(fixture("declared_parent_additional_properties", declared_parent, standard))
    evaluated = branches(unevaluatedProperties=False)
    result.append(fixture("unevaluated_parent", evaluated, standard, unchanged=True))
    nested_evaluated = object_schema({"payload": branches()}, ("payload",), unevaluatedProperties=False)
    result.append(fixture("outer_unevaluated_annotations", nested_evaluated, probes=[{"payload": p} for p in probes_for(result[0])]))
    literal = {"oneOf": [{"type": "object", "properties": {"must_remain": {"type": "number"}}, "required": ["must_remain"]},
                         {"type": "object", "properties": {"other": {"const": False}}, "additionalProperties": False}]}
    keyword_props = {
        "const": {"const": literal}, "enum": {"enum": [literal, {"$ref": "#/$defs/not_a_reference"}, False, 0]},
        "default": {"type": "number", "default": literal},
        "oneOf": branches(), "examples": branches(), "$id": {"type": "string"}, "type": {"type": "string"},
        "properties": {"type": "string"}, "required": {"type": "string"}, "additionalProperties": {"type": "string"}}
    result.append(fixture("keyword_property_names_and_literals", object_schema(keyword_props), probes=[
        {}, {"const": literal, "enum": literal, "default": 4, "oneOf": {"tag": "a", "shared": "abc"}, "$id": "text"},
        {"examples": {"tag": "a", "shared": "abc"}}, {"examples": "bad"}, {"const": {}}, {"enum": True}, {"enum": False}, {"enum": 0}, {"oneOf": {"tag": "a", "shared": "a"}},
        {"type": "text", "properties": "text", "required": "text", "additionalProperties": "text"}]))
    false_schema = branches()
    for b in false_schema["oneOf"][:2]:
        b["properties"]["never"] = False
    result.append(fixture("boolean_property_schemas", false_schema, {"tag": ["a", "b", "c"], "shared": ["abc", "a"], "never": [False, 0, {}]}))
    reference = branches()
    reference["$defs"] = {"base": object_schema({"shared": {"type": "string"}}, extra=True)}
    for b in reference["oneOf"][:2]:
        b["$ref"] = "#/$defs/base"
    result.append(fixture("reference_siblings", reference, standard))
    pure_reference = {"$defs": {"a": branches(), "b": object_schema({"tag": {"const": "z"}}, ("tag",))},
                      "oneOf": [{"$ref": "#/$defs/a"}, {"$ref": "#/$defs/b"}]}
    result.append(fixture("pure_reference_branches", pure_reference, standard))
    scoped = branches()
    scoped["$id"] = "urn:agentcad:hoist:fixture"
    result.append(fixture("root_scope", scoped, standard, unchanged=True))
    anchored = branches()
    anchored["$defs"] = {"scope": {"$anchor": "child", "type": "number"}}
    anchored["properties"] = {"number": {"$ref": "#child"}}
    result.append(fixture("nested_anchor_scope", anchored, standard, unchanged=True))
    dynamic = {"$dynamicAnchor": "node", "anyOf": [object_schema({"child": {"$dynamicRef": "#node"}}, extra=True),
                                                       object_schema({"value": {"type": "integer"}}, extra=True)]}
    result.append(fixture("dynamic_scope", dynamic, probes=[{}, {"child": {"value": 1}}, {"child": "bad"}, {"value": "bad"}], unchanged=True))
    nonobjects = {"oneOf": [object_schema({"tag": {"const": "a"}}), {"type": "integer"}]}
    result.append(fixture("mixed_union_types", nonobjects, {"tag": ["a", "b"]}, unchanged=True))
    # Known annotation values are literal JSON even when shaped like schemas.
    example = branches()
    result.append(fixture("examples_literal_data", {"type": "object", "examples": [example]}, probes=[{}, {"x": 1}], unchanged=True))
    return result


def intern_fixtures():
    properties = {"properties": {}, "additionalProperties": False,
                  "$ref": {"type": "string", "minLength": 3, "maxLength": 8},
                  "value": {"type": "string", "minLength": 3, "maxLength": 8, "pattern": "^[a-z]+$"}}
    repeated = object_schema({name: object_schema(copy.deepcopy(properties)) for name in ("a", "b", "c")})
    literal = branches()
    examples = object_schema({"value": {"$ref": "#/$defs/base"}}, **{"$defs": {"base": {"type": "integer"}},
        "examples": [{"$ref": "#/$defs/not_a_reference"}, literal, copy.deepcopy(literal)]})
    keyword = object_schema({name: copy.deepcopy(branches()) for name in ("const", "enum", "default", "examples")})
    return [fixture("intern_keyword_property_maps", repeated, probes=[{}, {"a": {}}, {"a": {"value": "abc"}},
        {"a": {"value": "a"}}, {"b": {"properties": 1}}, {"c": {"additionalProperties": False}},
        {"c": {"$ref": "abc"}}, {"a": {"$ref": "a"}}]),
        fixture("intern_examples_literal_references", examples, probes=[{}, {"value": 1}, {"value": "bad"}]),
        fixture("intern_literal_keyword_property_names", keyword, probes=[{}, {"const": {"tag": "a", "shared": "abc"}},
        {"enum": {"tag": "a", "shared": "a"}}, {"default": {"tag": "c"}}, {"examples": {"tag": "b", "shared": "abc"}},
        {"examples": {"tag": "b", "shared": "a"}}])]


def probes_for(case):
    fields = case["fields"]
    if fields:
        names = tuple(fields)
        for values in itertools.product(*([MISSING, *fields[name]] for name in names)):
            yield {name: value for name, value in zip(names, values) if value is not MISSING}
    yield from case["probes"]
    for value in (None, 0, False, True, "x", [], {}, {"extra": 1}):
        yield value


def literals(value):
    result = set()
    def walk(node):
        if isinstance(node, list):
            for child in node:
                walk(child)
        elif isinstance(node, dict):
            for key, child in node.items():
                if key in MAPS and isinstance(child, dict):
                    for body in child.values():
                        walk(body)
                elif key in LITERALS:
                    result.add((key, canonical(child)))
                else:
                    walk(child)
    walk(value)
    return result


def production(source, compiler, include, values, stage="hoist"):
    service = (source / "src/service.cpp").read_text(encoding="utf-8")
    start = service.index("    // Common object constraints apply once around a union.")
    marker = "    // Intern profitable repeated schema subtrees" if stage == "hoist" else "    // Every standalone definition name is local."
    end = service.index(marker, start)
    block = service[start:end]
    functions = []
    if stage != "hoist":
        model_source = (source / "src/model.cpp").read_text(encoding="utf-8")
        for name in ("collect_references", "prune_definitions"):
            begin = model_source.index("void " + name + "(")
            functions.append(model_source[begin:model_source.index("\n}", begin) + 2])
    require("if(!hoist_scoped)hoist(schema);" in block, "Production hoist boundary/guard changed")
    code = """#include <nlohmann/json.hpp>
static_assert(NLOHMANN_JSON_VERSION_MAJOR==3 && NLOHMANN_JSON_VERSION_MINOR==12 && NLOHMANN_JSON_VERSION_PATCH==0);
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <vector>
#include <iostream>
using Json=nlohmann::json;
""" + "\n".join(functions) + "\nvoid compact(Json& schema){\n" + block + """\n}
int main(){Json schemas;std::cin>>schemas;for(auto& schema:schemas)compact(schema);std::cout<<schemas.dump()<<"\\n";}
"""
    if include is None:
        choices = [source / ".deps/json-3.12.0/include", source / "build/_deps/nlohmann_json-src/include"]
        include = next((p for p in choices if (p / "nlohmann/json.hpp").is_file()), None)
    require(include is not None and (include / "nlohmann/json.hpp").is_file(), "Pinned JSON headers missing; pass --json-include")
    with tempfile.TemporaryDirectory(prefix="agentcad-hoist-proof-") as tmp:
        root = pathlib.Path(tmp)
        cpp, exe = root / "proof.cpp", root / "proof"
        cpp.write_text(code, encoding="utf-8")
        subprocess.run([compiler, "-std=c++20", "-O2", "-I", str(include), str(cpp), "-o", str(exe)], check=True, timeout=120)
        data = canonical(values)
        first = subprocess.run([str(exe)], input=data, text=True, capture_output=True, check=True, timeout=30).stdout
        second = subprocess.run([str(exe)], input=data, text=True, capture_output=True, check=True, timeout=30).stdout
        require(first == second, "Production hoist is nondeterministic")
    return json.loads(first), hashlib.sha256(block.encode()).hexdigest(), hashlib.sha256(service.encode()).hexdigest()


def compare(case, new):
    old = case["schema"]
    Draft202012Validator.check_schema(old)
    Draft202012Validator.check_schema(new)
    require(literals(old) == literals(new), case["name"] + ": changed literal annotation data")
    if case["unchanged"]:
        require(canonical(old) == canonical(new), case["name"] + ": protected/mixed schema changed")
    a, b = Draft202012Validator(old), Draft202012Validator(new)
    count = 0
    for probe in probes_for(case):
        accepted, transformed = a.is_valid(probe), b.is_valid(probe)
        require(accepted == transformed, case["name"] + ": changed acceptance for " + canonical(probe))
        count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--json-include", type=pathlib.Path)
    args = parser.parse_args()
    values = fixtures()
    after, block_hash, service_hash = production(args.source.resolve(), args.compiler, args.json_include, [v["schema"] for v in values])
    probes = sum(compare(case, schema) for case, schema in zip(values, after))
    require(len(after) == len(values), "Production changed fixture count")
    # These assertions require the intended branch-subset optimization to run.
    require(after[0]["properties"]["shared"] == values[0]["schema"]["oneOf"][0]["properties"]["shared"], "Constraint was not hoisted from two closed declaring branches")
    require("shared" not in after[0]["oneOf"][2]["properties"], "Hoist expanded a closed branch's allowed fields")
    require(set(after[0]["required"]) == {"tag"}, "Common required intersection changed")
    # Negative controls ensure independent validation notices actual weakening.
    for name, edit in (("minimum", lambda s: s["properties"]["shared"].update(minLength=1)),
                       ("required", lambda s: s.pop("required")),
                       ("allowed_field", lambda s: s["oneOf"][2]["properties"].update(shared={}))):
        bad = copy.deepcopy(after[0])
        edit(bad)
        old_validator, bad_validator = Draft202012Validator(values[0]["schema"]), Draft202012Validator(bad)
        require(any(old_validator.is_valid(p) != bad_validator.is_valid(p) for p in probes_for(values[0])), "Negative control missed " + name)
    intern_cases = intern_fixtures()
    intern_after, intern_hash, intern_service_hash = production(args.source.resolve(), args.compiler, args.json_include,
        [v["schema"] for v in intern_cases], stage="intern")
    intern_probes = sum(compare(case, schema) for case, schema in zip(intern_cases, intern_after))
    require(len(intern_after) == len(intern_cases), "Production changed intern fixture count")
    print(json.dumps({"checks": checks, "fixtures": len(values), "acceptance_probes": probes,
                      "intern_fixtures": len(intern_cases), "intern_acceptance_probes": intern_probes,
                      "production_block_sha256": block_hash, "source_service_sha256": service_hash,
                      "production_intern_block_sha256": intern_hash, "intern_source_service_sha256": intern_service_hash}))


if __name__ == "__main__":
    main()
