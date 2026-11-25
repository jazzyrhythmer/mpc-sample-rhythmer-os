# SPDX-License-Identifier: AGPL-3.0-only
"""Where a test finds a source: Rhythmer's own under src/, the engine, model
and files under core/src/ (ellic-core, the core/ submodule)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(name):
    """`name` as src/rhythmer/..., wherever it lives now."""
    path = ROOT / name
    return str(path if path.exists() else ROOT / "core" / name)
