"""Prove the production assertion factoring pass preserves standalone schemas.

Developer-only JSON harness; no Python dependency in the shipped service.
"""
import argparse
import copy
import hashlib
import json
import pathlib
import subprocess
import tempfile

from jsonschema import Draft202012Validator
from discovery_anchor_check import canonical, fixtures, visit

SAFE = {"type", "const", "enum", "minimum", "maximum", "exclusiveMinimum", "exclusiveMaximum",
        "multipleOf", "minItems", "maxItems", "uniqueItems", "minLength", "maxLength", "pattern",
        "minProperties", "maxProperties", "required"}
checks = 0


def require(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def production(source, values):
    text = (source / "src/service.cpp").read_text()
    start = text.index("    // Factor context-independent assertions into local reference siblings.")
    end = text.index("\n  }\n  return tools;", start)
    block = text[start:end]
    headers = """#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <set>
#include <map>
#include <vector>
#include <string_view>
#include <iostream>
using Json=nlohmann::json;
"""
    code = headers + "void compact(Json& schema){\n" + block + "\n}\n" + """
int main(){Json schemas;std::cin>>schemas;for(auto& schema:schemas)compact(schema);std::cout<<schemas.dump();}
"""
    include = source / ".deps/json-3.12.0/include"
    require((include / "nlohmann/json.hpp").is_file(), "Pinned JSON headers missing")
    with tempfile.TemporaryDirectory(prefix="agentcad-assertion-proof-") as tmp:
        root = pathlib.Path(tmp);cpp = root / "proof.cpp";exe = root / "proof"
        cpp.write_text(code)
        subprocess.run(["c++", "-std=c++20", "-O2", "-I", str(include), str(cpp), "-o", str(exe)], check=True, timeout=120)
        outputs = [subprocess.run([str(exe)], input=canonical(values), text=True, capture_output=True,
                                  check=True, timeout=60).stdout for _ in range(2)]
        require(outputs[0] == outputs[1], "Factoring is not deterministic")
    return json.loads(outputs[0]), hashlib.sha256(block.encode()).hexdigest()


def inverse(old, new, name):
    restored = copy.deepcopy(new);before = old.get("$defs", {});bodies = {}
    for key in set(restored.get("$defs", {})) - set(before):
        body = restored["$defs"].pop(key)
        anchor = body.pop("$anchor")
        require(set(body) <= SAFE, name + ": contextual or annotation keyword moved")
        require(anchor not in bodies, name + ": duplicate anchor")
        bodies[anchor] = body
    if "$defs" not in old:
        require(not restored.get("$defs"), name + ": unexpected definitions")
        restored.pop("$defs", None)
    def undo(node):
        ref = node.get("$ref")
        if isinstance(ref, str) and ref[1:] in bodies:
            node.pop("$ref")
            for key, value in bodies[ref[1:]].items():
                require(key not in node, name + ": assertion overwritten")
                node[key] = copy.deepcopy(value)
    visit(restored, undo)
    require(canonical(restored) == canonical(old), name + ": inverse did not recover exact original JSON")
    return len(bodies)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--catalog", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args();cases = fixtures()
    # Independent accepted/rejected probes exercise assertion conjunction with
    # properties, patternProperties, unions, array items and unevaluated fields.
    common = {"type": "object", "required": ["long_mandatory_name", "another_mandatory_name"],
              "minProperties": 2, "maxProperties": 3}
    schema = {"type": "object", "properties": {str(i): {**common, "properties": {
        "long_mandatory_name": {"type": "integer"}, "another_mandatory_name": {"type": "string"}},
        "additionalProperties": False} for i in range(12)}}
    valid = {"0": {"long_mandatory_name": 1, "another_mandatory_name": "yes"}}
    cases.append({"name": "profitable_contextual_siblings", "schema": schema,
                  "probes": [valid, {"0": {}}, {"0": {"long_mandatory_name": "bad", "another_mandatory_name": "yes"}},
                             {"0": {**valid["0"], "extra": 1}}, {"0": []}]})
    literal = copy.deepcopy(schema)
    literal["properties"]["literal"] = {"const": {"$id": "urn:literal", "$ref": "#/properties/type", "type": "object"}}
    cases.append({"name": "literal_resource_names", "schema": literal, "probes": [valid, {"literal": {"$id": "urn:literal", "$ref": "#/properties/type", "type": "object"}}]})
    pointer = copy.deepcopy(schema);pointer["properties"]["pointer"] = {"$ref": "#/properties/0/properties/long_mandatory_name"}
    cases.append({"name": "keyword_pointer_target", "schema": pointer, "probes": [valid, {"pointer": 1}, {"pointer": "bad"}], "unchanged": True})
    numbers = {"type": "object", "properties": {str(i): {"type": "number", "minimum": 1000000 if i % 2 else 1000000.0, "maximum": 2000000} for i in range(20)}}
    cases.append({"name": "integer_float_literal_identity", "schema": numbers, "probes": [{"0": 1000000}, {"0": 999999}, {"1": 2000001}]})
    catalog = json.loads(args.catalog.read_text()) if args.catalog else None
    if catalog:
        for tool in catalog:
            for key in ("inputSchema", "outputSchema"):
                cases.append({"name": tool["name"] + "." + key, "schema": tool[key], "probes": []})
    output, digest = production(args.source.resolve(), [case["schema"] for case in cases])
    made = 0;accepted = 0
    for case, new in zip(cases, output):
        old = case["schema"];name = case["name"]
        Draft202012Validator.check_schema(old);Draft202012Validator.check_schema(new)
        require(new.get("type") == old.get("type"), name + ": explicit root type changed")
        made += inverse(old, new, name)
        require(len(canonical(new)) <= len(canonical(old)), name + ": catalog grew")
        if case.get("unchanged"):
            require(canonical(new) == canonical(old), name + ": protected schema changed")
        a, b = Draft202012Validator(old), Draft202012Validator(new)
        for value in case["probes"] + [None, False, 0, "x", [], {}, {"extra": 1}]:
            require(a.is_valid(value) == b.is_valid(value), name + ": acceptance differs for " + canonical(value));accepted += 1
    require(made > 0, "Factoring never ran")
    if catalog:
        transformed = copy.deepcopy(catalog);index = len(cases) - len(catalog) * 2
        for tool in transformed:
            for key in ("inputSchema", "outputSchema"):
                tool[key] = output[index];index += 1
        if args.output:
            args.output.write_text(canonical(transformed))
        print("catalog bytes", len(canonical(catalog)), "->", len(canonical(transformed)))
    print(json.dumps({"checks": checks, "fixtures": len(cases), "acceptance_probes": accepted,
                      "factored_definitions": made, "production_block_sha256": digest}))


if __name__ == "__main__":
    main()
