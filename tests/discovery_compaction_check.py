"""Independent developer check of final discovery inlining; jsonschema==4.25.1.

Compare two actual native catalogs. Optionally compile the exact source pass in
an isolated harness and exercise the fixture; never runs a CAD service or writes
its source. No compiler/Python dependency is added to the packaged product.

Usage: python tests/discovery_compaction_check.py BEFORE.json AFTER.json --source .
The baseline must immediately precede the final inlining pass, with the same
tool contracts and alias names. Any unrelated change is deliberately rejected.
"""
import argparse
import copy
import hashlib
import json
import pathlib
import subprocess
import tempfile

from jsonschema import Draft202012Validator
from jsonschema.exceptions import SchemaError

MAPS = {"$defs", "properties", "patternProperties", "dependentSchemas"}
LITERALS = {"const", "enum", "default"}
SCOPES = {"$id", "$anchor", "$dynamicAnchor", "$dynamicRef", "$recursiveAnchor", "$recursiveRef"}
PUBLIC = {"model", "operation"}
checks = 0


def require(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def canonical(value):
    # Preserve JSON value types: Python considers True == 1 and False == 0.
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def walk(value):
    yield value
    if isinstance(value, list):
        for child in value:
            yield from walk(child)
    elif isinstance(value, dict):
        for key, child in value.items():
            if key in MAPS and isinstance(child, dict):
                for schema in child.values():
                    yield from walk(schema)
            elif key not in LITERALS:
                yield from walk(child)


def catalog(path):
    value = json.loads(path.read_text(encoding="utf-8"))
    tools = value["tools"] if isinstance(value, dict) else value
    require(isinstance(tools, list), "Catalog must be an array or {tools:array}")
    require(len({tool["name"] for tool in tools}) == len(tools), "Duplicate tool names")
    return tools


def standalone(schema):
    Draft202012Validator.check_schema(schema)
    for node in walk(schema):
        if not isinstance(node, dict) or "$ref" not in node:
            continue
        ref = node["$ref"]
        require(isinstance(ref, str) and ref.startswith("#/"), "Reference leaves standalone schema")
        target = schema
        for token in ref[2:].split("/"):
            token = token.replace("~1", "/").replace("~0", "~")
            require(isinstance(target, dict) and token in target, "Missing reference target: " + ref)
            target = target[token]


def expanded_removed(before, after):
    definitions = before.get("$defs", {})
    removed = set(definitions) - set(after.get("$defs", {}))
    require(not (PUBLIC & removed), "Public model/operation root removed")

    def expand(value, active=()):
        if isinstance(value, list):
            return [expand(child, active) for child in value]
        if not isinstance(value, dict):
            return value
        ref = value.get("$ref")
        if len(value) == 1 and isinstance(ref, str) and ref.startswith("#/$defs/") and ref[8:] in removed:
            name = ref[8:]
            require(name not in active, "Recursive definition was removed: " + name)
            return expand(definitions[name], (*active, name))
        result = {}
        for key, child in value.items():
            if key in MAPS and isinstance(child, dict):
                result[key] = {name: expand(body, active) for name, body in child.items()}
            else:
                result[key] = copy.deepcopy(child) if key in LITERALS else expand(child, active)
        return result

    result = expand(before)
    if "$defs" in result:
        for name in removed:
            result["$defs"].pop(name, None)
        if not result["$defs"]:
            del result["$defs"]
    return result, removed


def compare(before, after):
    require(len(before) == len(after), "Tool count changed")
    removed_count = 0
    for original, final in zip(before, after):
        metadata = lambda tool: {key: value for key, value in tool.items() if key not in {"inputSchema", "outputSchema"}}
        require(canonical(metadata(original)) == canonical(metadata(final)), "Tool metadata/order changed")
        for key in ("inputSchema", "outputSchema"):
            old, new = original[key], final[key]
            standalone(old)
            standalone(new)
            expected, removed = expanded_removed(old, new)
            require(canonical(expected) == canonical(new), original["name"] + "." + key + " changed a constraint, literal, reference or field")
            if any(isinstance(node, dict) and SCOPES & node.keys() for node in walk(old)):
                require(canonical(old) == canonical(new), "Scoped schema was inlined")
            removed_count += len(removed)
    return removed_count


def json_include_path(source, explicit, build_dirs):
    if explicit:
        include = explicit.resolve()
        require((include / "nlohmann/json.hpp").is_file(), "--json-include must contain nlohmann/json.hpp")
        return include
    candidates = [source / ".deps/json-3.12.0/include", source / "build/_deps/nlohmann_json-src/include"]
    candidates.extend(sorted(source.glob("*/_deps/nlohmann_json-src/include")))
    candidates.extend(directory / "_deps/nlohmann_json-src/include" for directory in build_dirs)
    include = next((path for path in candidates if (path / "nlohmann/json.hpp").is_file()), None)
    require(include is not None, "JSON developer headers missing; pass --json-include /path/to/include")
    return include


def source_fixture(source, compiler, fixture, json_include, build_dirs):
    # Compile exact production statements, not a Python copy of the algorithm.
    # Fail if the reviewed source boundaries change; never guess a new boundary.
    service = (source / "src/service.cpp").read_text(encoding="utf-8")
    model = (source / "src/model.cpp").read_text(encoding="utf-8")
    start = service.index("      // Final aliases make the cost of every private definition comparable.")
    block = service[start:service.index("\n    }\n  }\n  return tools;", start)]
    functions = []
    for name in ("collect_references", "prune_definitions"):
        start = model.index("void " + name + "(")
        functions.append(model[start:model.index("\n}", start) + 2])
    code = """#include <nlohmann/json.hpp>
static_assert(NLOHMANN_JSON_VERSION_MAJOR==3 && NLOHMANN_JSON_VERSION_MINOR==12 && NLOHMANN_JSON_VERSION_PATCH==0,
              "Compaction fixtures require the pinned nlohmann JSON 3.12.0");
#include <fstream>
#include <map>
#include <set>
#include <vector>
#include <functional>
using Json=nlohmann::json;
""" + "\n".join(functions) + "\nvoid compact(Json& schema){\n" + block + "\n}\n" + """int main(int argc,char** argv){
  if(argc!=3)return 2;
  Json tools;std::ifstream(argv[1])>>tools;
  for(auto& tool:tools)for(const auto* key:{"inputSchema","outputSchema"})compact(tool[key]);
  std::ofstream(argv[2])<<tools.dump()<<"\\n";
}
"""
    include = json_include_path(source, json_include, build_dirs)
    with tempfile.TemporaryDirectory(prefix="agentcad-discovery-proof-") as temporary:
        root = pathlib.Path(temporary)
        cpp, executable, output = root / "proof.cpp", root / "proof", root / "after.json"
        cpp.write_text(code, encoding="utf-8")
        subprocess.run([compiler, "-std=c++20", "-O2", "-I", str(include), str(cpp), "-o", str(executable)], check=True, timeout=120)
        command = [str(executable), str(fixture), str(output)]
        subprocess.run(command, check=True, timeout=30)
        first = output.read_bytes()
        subprocess.run(command, check=True, timeout=30)
        require(output.read_bytes() == first, "Production fixture output is nondeterministic")
        return json.loads(first)


def fixture_controls(before, after):
    compare(before, after)
    by_name = {tool["name"]: tool for tool in after}
    recursive = by_name["recursive"]
    pointer = by_name["pointer_and_siblings"]
    for key in ("inputSchema", "outputSchema"):
        require({"left", "right"} <= recursive[key]["$defs"].keys(), "Recursive fixture definitions removed")
        require({"pointer", "sibling"} <= pointer[key]["$defs"].keys(), "Pointer/sibling fixture definition removed")
    probes = [{}, {"a": 3, "b": "ab", "const": "a", "enum": "b", "default": "c"}, {"a": 2}, {"a": 6}, {"a": "3"},
              {"r": {"next": {"next": {}}}}, {"p": 4, "s": 5}, {"p": "bad", "s": 1}, {"x": True},
              {"literal_const": {"$ref": "#/$defs/missing"}}]
    for old, new in zip(before, after):
        if old["name"].startswith("scoped_"):
            continue
        for probe in probes:
            require(Draft202012Validator(old["inputSchema"]).is_valid(probe) == Draft202012Validator(new["inputSchema"]).is_valid(probe), "Fixture accepted/rejected behavior changed")
    # Negative controls prove that equality/ref checks reject weakened semantics.
    for change in ("bound", "literal", "boolean_type", "recursive", "pointer", "sibling", "scope", "public_root"):
        bad = copy.deepcopy(after)
        first = bad[0]["inputSchema"]
        if change == "bound":
            first["properties"]["a"]["minimum"] = 2
        elif change == "literal":
            first["properties"]["literal_const"]["const"]["$ref"] = "#/$defs/changed"
        elif change == "boolean_type":
            first["properties"]["literal_boolean"]["const"] = 0
        elif change == "public_root":
            del first["$defs"]["model"]
        elif change == "recursive":
            bad[1]["inputSchema"]["$defs"].pop("left")
        elif change in {"pointer", "sibling"}:
            bad[2]["inputSchema"]["$defs"].pop(change)
        else:
            bad[3]["inputSchema"]["$defs"].pop("single")
            bad[3]["inputSchema"]["properties"]["x"] = {"type": "boolean"}
        try:
            compare(before, bad)
        except (AssertionError, KeyError, ValueError, SchemaError):
            pass
        else:
            raise AssertionError("Negative control escaped detection: " + change)
        require(True, "Rejected " + change + " negative control")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=pathlib.Path)
    parser.add_argument("after", type=pathlib.Path)
    parser.add_argument("--source", type=pathlib.Path, help="Also compile exact current source pass privately and run safeguard fixtures")
    parser.add_argument("--compiler", default="c++")
    parser.add_argument("--json-include", type=pathlib.Path, help="Include directory containing nlohmann/json.hpp 3.12.0; otherwise try local/Fetched headers")
    args = parser.parse_args()
    before, after = catalog(args.before), catalog(args.after)
    removed = compare(before, after)
    if args.source:
        fixture = pathlib.Path(__file__).with_name("discovery_compaction_fixture.json")
        fixture_controls(catalog(fixture), source_fixture(args.source.resolve(), args.compiler, fixture.resolve(), args.json_include,
                                                         (args.before.resolve().parent, args.after.resolve().parent)))
    print(json.dumps({"checks": checks, "tools": len(after), "removed_definitions": removed,
                      "before_sha256": hashlib.sha256(args.before.read_bytes()).hexdigest(),
                      "after_sha256": hashlib.sha256(args.after.read_bytes()).hexdigest(),
                      "source_fixtures": bool(args.source),
                      "source_service_sha256": hashlib.sha256((args.source / "src/service.cpp").read_bytes()).hexdigest() if args.source else None}))


if __name__ == "__main__":
    main()
