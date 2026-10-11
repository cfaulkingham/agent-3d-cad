"""Equivalence and repeated-anchor-work regression tests; no SDK modifications."""
from copy import deepcopy
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from jsonschema import Draft202012Validator, SchemaError, ValidationError
from referencing import Registry
from schema_validators import DiscoveredTools, SchemaValidators


def error_tree(error):
    return (error.message, list(error.path), list(error.schema_path),
            [error_tree(child) for child in error.context])


class SchemaValidationTests(unittest.TestCase):
    def assert_equivalent(self, schema, instances):
        compiled = SchemaValidators().get('tool input', schema)
        for instance in instances:
            with self.subTest(instance=instance):
                fresh = Draft202012Validator(schema)
                self.assertEqual([error_tree(e) for e in compiled.iter_errors(instance)],
                                 [error_tree(e) for e in fresh.iter_errors(instance)])
                self.assertEqual(compiled.is_valid(instance), fresh.is_valid(instance))

    def test_local_anchors_closed_objects_and_union_errors(self):
        schema = {'$defs': {
            'dimension': {'$anchor': 'dimension', 'type': 'number', 'exclusiveMinimum': 0, 'maximum': 100},
            'label': {'$anchor': 'label', 'type': 'string', 'pattern': '^[a-z]+$'}},
            'type': 'object', 'properties': {'size': {'$ref': '#dimension'},
                                           'name': {'oneOf': [{'$ref': '#label'}, {'type': 'null'}]}},
            'required': ['size', 'name'], 'additionalProperties': False}
        self.assert_equivalent(schema, [None, {}, {'size': 2.5, 'name': 'part'}, {'size': 1, 'name': None},
            {'size': 0, 'name': 'part'}, {'size': True, 'name': 3}, {'size': 101, 'name': 'Bad'},
            {'size': 1, 'name': 'part', 'extra': True}])

    def test_nested_resource_ids_keep_equal_anchor_names_separate(self):
        schema = {'$id': 'https://example.invalid/root.json', '$defs': {
            'dimension': {'$id': 'dimension.json', '$anchor': 'value', 'type': 'integer'},
            'label': {'$id': 'label.json', '$anchor': 'value', 'type': 'string'}},
            'oneOf': [{'$ref': 'dimension.json#value'}, {'$ref': 'label.json#value'}]}
        self.assert_equivalent(schema, [1, 'part', None, True, 1.5, {}, []])

    def test_recursive_dynamic_anchor_validation(self):
        schema = {'$id': 'https://example.invalid/tree.json', '$dynamicAnchor': 'node',
            'type': 'object', 'properties': {'name': {'type': 'string'},
                'children': {'type': 'array', 'items': {'$dynamicRef': '#node'}}},
            'required': ['name'], 'additionalProperties': False}
        self.assert_equivalent(schema, [{'name': 'root'}, {'name': 'root', 'children': [{'name': 'leaf'}]},
            {'name': 'root', 'children': [{'name': 9}]}, {'name': 'root', 'children': [{'extra': True}]},
            {'name': 'root', 'children': False}])

    def test_boolean_schemas_and_pointer_references(self):
        for schema in (True, False, {'$defs': {'choice': {'enum': [1, 'one']}}, '$ref': '#/$defs/choice'}):
            self.assert_equivalent(schema, [None, True, 1, 'one', 'two', [], {}])

    def test_same_schema_reuses_but_mutation_rebuilds_checked_snapshot(self):
        schema = {'$defs': {'n': {'$anchor': 'n', 'type': 'integer', 'maximum': 10}}, '$ref': '#n'}
        cache = SchemaValidators(); original = cache.get('input', schema)
        self.assertIs(cache.get('input', deepcopy(schema)), original)
        schema['$defs']['n']['maximum'] = 4
        changed = cache.get('input', schema)
        self.assertIsNot(original, changed)
        self.assertTrue(original.is_valid(7))  # Snapshot did not silently change.
        self.assertFalse(changed.is_valid(7))
        self.assertTrue(changed.is_valid(4))
        schema['type'] = 23
        with self.assertRaises(SchemaError):
            cache.get('input', schema)
        with self.assertRaises(SchemaError):
            cache.get('input', schema)  # A failed refresh never returns stale success.

    def test_boolean_and_numeric_schema_values_are_not_cache_equal(self):
        cache = SchemaValidators()
        boolean = cache.get('input', {'const': True})
        number = cache.get('input', {'const': 1})
        self.assertIsNot(boolean, number)
        self.assertTrue(boolean.is_valid(True))
        self.assertFalse(boolean.is_valid(1))
        self.assertTrue(number.is_valid(1))
        self.assertFalse(number.is_valid(True))

    def test_discovery_sessions_and_input_output_remain_separate(self):
        tool = SimpleNamespace(input_schema={'type': 'integer'}, output_schema={'type': 'string'})
        first = DiscoveredTools({'tool': tool}); second = DiscoveredTools({'tool': tool})
        original = first.validator('tool', 'input_schema')
        self.assertIsNot(original, second.validator('tool', 'input_schema'))
        self.assertTrue(original.is_valid(3))
        self.assertFalse(first.validator('tool', 'output_schema').is_valid(3))
        first['tool'] = SimpleNamespace(input_schema={'type': 'boolean'}, output_schema={'type': 'null'})
        self.assertFalse(first.validator('tool', 'input_schema').is_valid(3))
        self.assertTrue(first.validator('tool', 'input_schema').is_valid(True))

    def test_unresolved_local_reference_remains_an_error(self):
        schema = {'$ref': '#missing'}
        errors = []
        for validator in (Draft202012Validator(schema), SchemaValidators().get('tool', schema)):
            with self.assertRaises(Exception) as caught:
                validator.validate({'value': 1})
            errors.append((type(caught.exception), str(caught.exception)))
        self.assertEqual(errors[0], errors[1])

    def test_every_instance_validates_without_repeated_anchor_crawl(self):
        schema = {'$defs': {f'n{i}': {'$anchor': f'n{i}', 'type': 'integer', 'maximum': i}
                           for i in range(80)}, '$ref': '#n79'}
        compiled = SchemaValidators().get('tool', schema)
        crawl = Registry.crawl
        calls = []
        def tracked(registry):
            calls.append(1)
            return crawl(registry)
        with patch.object(Registry, 'crawl', tracked):
            for value in (1, 80, 'wrong') * 5:
                self.assertEqual(compiled.is_valid(value), isinstance(value, int) and value <= 79)
        self.assertEqual(calls, [])
        with self.assertRaises(ValidationError):
            compiled.validate(80)
        with patch.object(Registry, 'crawl', tracked):
            for value in (1, 80, 'wrong') * 5:
                Draft202012Validator(schema).is_valid(value)
        self.assertGreaterEqual(len(calls), 15)


if __name__ == '__main__':
    unittest.main()
