"""Independent developer-test validators with discovery-owned schema snapshots.

Every instance still runs the complete Draft 2020-12 validator. Pre-crawling only
indexes local anchors/subresources once; it never memoizes validation outcomes.
The official MCP SDK and its own validation remain untouched.
"""
from copy import deepcopy
import json

from jsonschema import Draft202012Validator
from referencing import Registry
from referencing.jsonschema import DRAFT202012


class SchemaValidators:
    def __init__(self):
        self._entries = {}

    def get(self, key, schema):
        # Python considers True == 1; JSON Schema constant/enum types do not.
        # Canonical JSON preserves that distinction and ignores dictionary order.
        identity = json.dumps(schema, sort_keys=True, separators=(',', ':'), allow_nan=False)
        cached = self._entries.get(key)
        if cached is not None and cached[0] == identity:
            return cached[1]
        snapshot = deepcopy(schema)
        Draft202012Validator.check_schema(snapshot)
        resource = DRAFT202012.create_resource(snapshot)
        registry = Registry().with_resource(resource.id() or '', resource).crawl()
        validator = Draft202012Validator(snapshot, registry=registry)
        # Publish only after a complete successful schema check/index build.
        self._entries[key] = (identity, validator)
        return validator


class DiscoveredTools(dict):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._validators = SchemaValidators()

    def validator(self, name, attribute):
        return self._validators.get((name, attribute), getattr(self[name], attribute))
