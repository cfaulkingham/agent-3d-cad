"""Deadline attribution checks; uses the same pinned developer SDK as the smoke."""

import asyncio
from contextlib import redirect_stderr
import io
import unittest
from unittest.mock import patch

import mcp_sdk_smoke as smoke


class LifecycleDeadlineTests(unittest.IsolatedAsyncioTestCase):
    async def test_explicit_session_budget_cancels_and_reports_its_own_deadline(self):
        output = io.StringIO()
        cleaned_up = False
        with redirect_stderr(output):
            with self.assertRaisesRegex(TimeoutError, "auto lifecycle exceeded 0.02s"):
                async with smoke.lifecycle("auto", timeout_seconds=.02):
                    try:
                        await asyncio.Event().wait()
                    finally:
                        cleaned_up = True
        self.assertTrue(cleaned_up)
        self.assertIn("starting (0.02s deadline)", output.getvalue())
        self.assertIn("auto: timed out", output.getvalue())

    async def test_longer_session_budget_keeps_inner_timeout_attribution(self):
        output = io.StringIO()
        # An expired default must not override the explicit full-suite budget.
        with patch.object(smoke, "LIFECYCLE_TIMEOUT_SECONDS", 0), redirect_stderr(output):
            async with smoke.lifecycle("auto", timeout_seconds=1):
                await asyncio.sleep(0)
            with self.assertRaises(TimeoutError) as caught:
                async with smoke.lifecycle("call", timeout_seconds=1):
                    await asyncio.wait_for(asyncio.Event().wait(), timeout=.01)
        self.assertNotIn("lifecycle exceeded", str(caught.exception))
        self.assertIn("auto: passed", output.getvalue())
        self.assertIn("call: failed", output.getvalue())
        self.assertNotIn("timed out", output.getvalue())

    async def test_expiry_cancels_work_and_next_lifecycle_still_runs(self):
        cleaned_up = False
        output = io.StringIO()
        with patch.object(smoke, "LIFECYCLE_TIMEOUT_SECONDS", .02), redirect_stderr(output):
            with self.assertRaisesRegex(TimeoutError, "first lifecycle exceeded"):
                async with smoke.lifecycle("first"):
                    try:
                        await asyncio.Event().wait()
                    finally:
                        cleaned_up = True
            async with smoke.lifecycle("next"):
                await asyncio.sleep(0)
        self.assertTrue(cleaned_up)
        self.assertIn("first: timed out", output.getvalue())
        self.assertIn("next: passed", output.getvalue())

    async def test_inner_timeout_is_not_relabelled_as_lifecycle_expiry(self):
        error = TimeoutError("individual call exceeded its deadline")
        output = io.StringIO()
        with redirect_stderr(output):
            with self.assertRaises(TimeoutError) as caught:
                async with smoke.lifecycle("inner"):
                    raise error
        self.assertIs(caught.exception, error)
        self.assertIn("inner: failed", output.getvalue())
        self.assertNotIn("timed out", output.getvalue())

    async def test_assertion_propagates_and_checks_are_stage_local(self):
        error = AssertionError("geometry mismatch")
        output = io.StringIO()
        with patch.object(smoke, "checks", 50), redirect_stderr(output):
            with self.assertRaises(AssertionError) as caught:
                async with smoke.lifecycle("assertion"):
                    smoke.require(True, "earlier independent assertion")
                    raise error
        self.assertIs(caught.exception, error)
        self.assertIn("assertion: failed", output.getvalue())
        self.assertIn("1 checks", output.getvalue())


if __name__ == "__main__":
    unittest.main()
