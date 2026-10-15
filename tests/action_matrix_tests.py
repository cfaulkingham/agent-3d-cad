"""The matrix must fail closed when coverage or a previously passing edit is lost."""
import copy
import unittest
import action_matrix as matrix


class RatchetTests(unittest.TestCase):
    def setUp(self):
        self.baseline = {'version': matrix.VERSION, 'corpus_sha256': 'corpus', 'cases': {
            'box/edge.fillet/a': {'state': 'passed', 'edit_state': 'passed'},
            'box/edge.fillet/b': {'state': 'refused', 'edit_state': 'not_run'}}}
        self.inventory = list(self.baseline['cases'])
        self.result = {'key': self.inventory[0], 'state': 'passed', 'edit_state': 'passed'}

    def errors(self, result=None, inventory=None):
        return matrix.baseline_errors(self.baseline, 'corpus',
            self.inventory if inventory is None else inventory, [self.result if result is None else result])

    def test_pass(self):
        self.assertFalse(self.errors())

    def test_lost_entity_is_regression_even_in_sample(self):
        self.assertTrue(self.errors(inventory=self.inventory[:1]))

    def test_missing_selector_does_not_hide_case(self):
        result = dict(self.result, state='unselectable', edit_state='not_run')
        self.assertTrue(self.errors(result))

    def test_edit_regression_is_independent_of_action_success(self):
        self.assertTrue(self.errors(dict(self.result, edit_state='refused')))

    def test_timeout_cannot_replace_a_known_refusal(self):
        self.assertTrue(self.errors({'key': self.inventory[1], 'state': 'timeout', 'edit_state': 'not_run'}))

    def test_known_failure_can_improve(self):
        self.assertFalse(self.errors(dict(self.result, key=self.inventory[1])))

    def test_invariants_and_process_crashes_are_never_blessable_successes(self):
        for state in ('invariant_failure', 'infrastructure_failure'):
            self.assertTrue(self.errors(dict(self.result, key=self.inventory[1], state=state)))

    def test_corpus_change_requires_full_review(self):
        self.assertTrue(matrix.baseline_errors(self.baseline, 'different', self.inventory, [self.result]))

    def test_action_definition_changes_require_review(self):
        self.baseline['case_hashes'] = {'a': 'old'}
        self.assertTrue(matrix.baseline_errors(self.baseline, 'corpus', self.inventory, [self.result], {'a': 'new'}))

    def test_case_identity_ignores_evaluation_enumeration(self):
        face = {'id': 'face-1', 'surface_kind': 'plane', 'center_mm': [1., 2., 3.], 'area_mm2': 10.}
        self.assertEqual(matrix.entity_key(face), matrix.entity_key(dict(face, id='face-99')))
        self.assertNotEqual(matrix.entity_key(face), matrix.entity_key(dict(face, area_mm2=11.)))

    def test_saved_selector_tracks_scale_and_preserves_cardinality(self):
        entity = {'selector': {'type': 'geometric', 'feature_id': 'body', 'surface_kind': 'plane',
            'expected_count': 1, 'center': {'point': [1, 2, 3], 'tolerance': 1e-5},
            'area': {'value': 12, 'tolerance': 1e-5}, 'normal': {'vector': [0, 0, 1], 'tolerance': 1e-6}}}
        before = copy.deepcopy(entity)
        rule = matrix.selector(entity)
        self.assertEqual(entity, before)
        self.assertEqual(rule['expected_count'], 1)
        self.assertEqual(rule['normal'], entity['selector']['normal'])
        self.assertEqual(rule['area']['value']['expression']['unit'], 'mm2')
        self.assertIn('scale', matrix.canonical(rule['area']['value']))


if __name__ == '__main__':
    unittest.main()
