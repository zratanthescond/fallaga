"""Sousse c.1930s terrain rebuild pipeline (plan: assets/terrain/heightmapAssets/

Sousse c.1930s Terrain Parallel Agent Build Plan.md).

Run from the repo root:

    python -m tools.pack_pipeline.run_build

Each agent is a stateless function `run(...)` that reads pack inputs, writes
versioned outputs into the run directory and returns a metrics dict.
"""

__version__ = "1.0.0"
