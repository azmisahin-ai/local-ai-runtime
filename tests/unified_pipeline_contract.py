#!/usr/bin/env python3
"""Guard the one-run/build-once release contract against accidental regressions."""
from __future__ import annotations

import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/pipeline.yml").read_text(encoding="utf-8")
PUBLISHER = (ROOT / "scripts/Publish-LocalAIRuntime.ps1").read_text(encoding="utf-8-sig")


class UnifiedPipelineContractTests(unittest.TestCase):
    def test_tag_push_is_not_a_workflow_trigger(self) -> None:
        trigger_block = WORKFLOW.split("permissions:", 1)[0]
        self.assertNotIn("tags:", trigger_block)
        self.assertIn("branches:\n      - main", trigger_block)

    def test_platform_jobs_use_the_planned_version(self) -> None:
        for job in ("windows", "linux", "macos"):
            match = re.search(rf"(?ms)^  {job}:\n(.*?)(?=^  [a-z0-9-]+:\n|\Z)", WORKFLOW)
            self.assertIsNotNone(match, f"missing job: {job}")
            block = match.group(1)
            self.assertIn("needs: [plan]", block)
            self.assertIn("LAR_RELEASE_VERSION: ${{ needs.plan.outputs.version }}", block)

    def test_release_reuses_same_run_artifacts_without_building(self) -> None:
        start = WORKFLOW.index("  publish-release:\n")
        end = WORKFLOW.index("\n\n  publish-pages:", start)
        release_block = WORKFLOW[start:end]
        self.assertIn("needs: [plan, windows, linux, macos]", release_block)
        self.assertIn("pattern: candidate-*", release_block)
        self.assertIn("git tag -a", release_block)
        self.assertIn("gh release create", release_block)
        self.assertNotIn("cmake --build", release_block)
        self.assertNotIn("cmake -S", release_block)

    def test_pages_waits_for_release_and_final_report_is_present(self) -> None:
        start = WORKFLOW.index("  publish-pages:\n")
        end = WORKFLOW.index("\n\n  final-report:", start)
        pages_block = WORKFLOW[start:end]
        self.assertIn("needs: [plan, windows, linux, macos, publish-release]", pages_block)
        self.assertIn("needs.publish-release.result == 'success'", pages_block)
        self.assertIn("  final-report:", WORKFLOW)
        for stage in ("Windows x64", "Linux x64", "macOS x64 + arm64", "Tag + GitHub Release", "GitHub Pages"):
            self.assertIn(stage, WORKFLOW)

    def test_local_publisher_does_not_create_tags_or_wait_for_a_second_run(self) -> None:
        self.assertNotIn("Invoke-Git -GitArgs @('tag', '-a'", PUBLISHER)
        self.assertNotIn("tagged CI", PUBLISHER)
        self.assertIn("single end-to-end pipeline", PUBLISHER)
        self.assertIn("Get-VersionTagsAtCommit", PUBLISHER)


if __name__ == "__main__":
    unittest.main(verbosity=2)
