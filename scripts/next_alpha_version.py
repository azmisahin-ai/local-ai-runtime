#!/usr/bin/env python3
"""Choose the next alpha tag, reusing a tag already attached to HEAD on reruns."""
from __future__ import annotations

import re
import subprocess
from collections.abc import Iterable

_TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")


def parse_numeric_version(tag: str) -> tuple[int, int, int] | None:
    match = _TAG_RE.fullmatch(tag.strip())
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def next_alpha_tag(tags: Iterable[str], tags_at_head: Iterable[str] = ()) -> str:
    current = [tag.strip() for tag in tags_at_head if parse_numeric_version(tag)]
    if current:
        return max(current, key=lambda item: (parse_numeric_version(item), item))

    versions = [parsed for tag in tags if (parsed := parse_numeric_version(tag)) is not None]
    if not versions:
        return "v0.1.0-alpha"
    major, minor, patch = max(versions)
    return f"v{major}.{minor}.{patch + 1}-alpha"


def _git_lines(*args: str) -> list[str]:
    result = subprocess.run(["git", *args], check=True, text=True, capture_output=True)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main() -> int:
    tags = _git_lines("tag", "--list", "v*")
    tags_at_head = _git_lines("tag", "--points-at", "HEAD")
    print(next_alpha_tag(tags, tags_at_head))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
