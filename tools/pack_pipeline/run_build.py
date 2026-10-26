"""run_build.py

A0 Orchestrator entry point. Builds the job graph, runs the data agents in
dependency order, writes run_manifest.json and a wave summary, and stops on a
red hard gate (A1, G3/G4/G5).

CLI
---
    python -m tools.pack_pipeline.run_build
    python -m tools.pack_pipeline.run_build --roads-json assets/roads/roads_manual.json
    python -m tools.pack_pipeline.run_build --no-offset
    python -m tools.pack_pipeline.run_build --run-id 20260101-120000
"""

from __future__ import annotations

import argparse
import os
import sys

from . import (__version__, a1_verify, a2_historian, a4_layers, a5_roads,
               a6_water, a7_port, a8_vegetation, a9_engine, a14_qa, common,
               contract)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Sousse c.1930s terrain rebuild pipeline")
    # Deprecated compatibility flags; the revised plan consumes its registered
    # terrain-space raster and never shifts/retraces the legacy road JSON.
    ap.add_argument("--roads-json", default=None, help=argparse.SUPPRESS)
    ap.add_argument("--plan-offset", action="store_true", default=True, help=argparse.SUPPRESS)
    ap.add_argument("--no-plan-offset", action="store_false", dest="plan_offset",
                    help=argparse.SUPPRESS)
    ap.add_argument("--run-id", default=None)
    ap.add_argument("--output-root", default=contract.OUTPUT_ROOT)
    ap.add_argument("--compare-run", default=None,
                    help="compare deterministic data outputs with a prior run")
    ap.add_argument("--skip-vegetation", action="store_true")
    args = ap.parse_args(argv)

    run_id = args.run_id or common.make_run_id()
    run_dir = common.ensure_dir(os.path.join(args.output_root, run_id))
    print(f"[A0] run {run_id} -> {os.path.relpath(run_dir, contract.REPO_ROOT)}")

    results = {}

    # Wave 0 ---------------------------------------------------------------
    print("[W0] A1 verifier ...")
    verify = a1_verify.run(run_dir)
    results["A1"] = {"passed": verify["passed"],
                     "invariants": len(verify["invariants"])}
    if not verify["passed"]:
        print("[A0] A1 FAILED (hard stop). See verify_report.json")
        _write_manifest(run_dir, run_id, args, results, verify, None)
        return 2

    print("[W0] A2 historian ...")
    results["A2"] = a2_historian.run(run_dir)

    # Wave 1 ---------------------------------------------------------------
    print("[W1] A5 road geometry ...")
    road = a5_roads.run(run_dir)
    results["A5"] = {"edges": road["edge_count"], "nodes": road["node_count"],
                     "polygons": road["polygon_count"],
                     "coverage_frac": round(road["coverage_frac"], 4),
                     "source": road["source"]}

    print("[W1] A4 layer prep ...")
    layers = a4_layers.run(run_dir, road["mask_2017"])
    results["A4"] = {"max_sum_error_land": layers["max_sum_error_land"],
                     "sea_ground_nonzero_px": layers["sea_ground_nonzero_px"]}

    print("[W1] A6 water data ...")
    water = a6_water.run(run_dir)
    results["A6"] = {"shoreline_segments": water["water_data"]["shoreline_segments"],
                     "oued_channels": water["oued_channels"],
                     "basin_polygons": water["basin_polygons"]}

    print("[W1] A7 port geometry ...")
    port = a7_port.run(run_dir)
    results["A7"] = port

    veg = None
    if not args.skip_vegetation:
        print("[W1] A8 vegetation density ...")
        veg = a8_vegetation.run(run_dir, road["mask_2017"])
        results["A8"] = veg
    else:
        results["A8"] = {"skipped": True}

    # Wave 2 (engine sheets) ----------------------------------------------
    print("[W2] A9/A3/A10-A13 engine settings sheets ...")
    results["A9"] = a9_engine.run(run_dir, road_info=road, water_info=water,
                                  port_info=port, veg_info=veg)

    # Wave 3 (QA) ----------------------------------------------------------
    print("[W3] A14 QA ...")
    era = common.load_json(os.path.join(run_dir, "era_rules.json"))
    qa = a14_qa.run(run_dir, verify, road, water, port, veg, era,
                    compare_run_dir=args.compare_run)
    results["A14"] = {"failed": qa["failed"], "hard_stop": qa["hard_stop"]}

    manifest = _write_manifest(run_dir, run_id, args, results, verify, road)
    summary = {
        "run_id": run_id,
        "run_dir": os.path.relpath(run_dir, contract.REPO_ROOT),
        "verify_passed": verify["passed"],
        "qa_hard_stop": qa["hard_stop"],
        "qa_failed": qa["failed"],
        "results": results,
    }
    common.save_json(summary, os.path.join(run_dir, "summary.json"))

    print("\n===== SUMMARY =====")
    for k in sorted(results):
        print(f"  {k}: {results[k]}")
    print(f"  QA failed gates: {qa['failed'] or 'none'}")
    print(f"  QA hard stop:    {qa['hard_stop']}")
    print(f"  outputs:         {os.path.relpath(run_dir, contract.REPO_ROOT)}")
    return 0 if not qa["hard_stop"] else 1


def _write_manifest(run_dir, run_id, args, results, verify, road):
    inputs = {}
    for name, path in [
        ("r16", contract.R16), ("splat_A", contract.SPLAT_A),
        ("splat_B", contract.SPLAT_B), ("sea_mask", contract.SEA_MASK),
        ("sebkha_mask", contract.SEBKHA_MASK), ("oued_mask", contract.OUED_MASK),
        ("urban_mask", contract.URBAN_MASK), ("slope_map", contract.SLOPE_MAP),
        ("normal_map", contract.NORMAL_MAP), ("ao_map", contract.AO_MAP),
        ("roads_surface_mask_4096", contract.ROAD_SURFACE_MASK),
        ("roads_bands_mask_4096", contract.ROAD_BANDS_MASK),
        ("roads_quay_islands_mask_4096", contract.ROAD_QUAY_ISLANDS_MASK),
        ("roads_registration", contract.ROAD_REGISTRATION),
        ("port_platform_mask", contract.PORT_PLATFORM_MASK),
    ]:
        inputs[name] = {
            "path": os.path.relpath(path, contract.REPO_ROOT),
            "sha256": common.sha256_file(path) if os.path.exists(path) else None,
            "bytes": common.file_size(path),
        }
    missing = {k: v for k, v in verify.get("files", {}).items()
               if v.get("expected_by_plan") and not v.get("exists")}

    manifest = {
        "run_id": run_id,
        "pipeline_version": __version__,
        "plan": "assets/terrain/heightmapAssets/Sousse c.1930s Terrain "
                "Parallel Agent Build Plan (1).md",
        "config": verify["grid"],
        "seeds": {"global": 1930},
        "inputs": inputs,
        "missing_from_pack": sorted(missing.keys()),
        "agents": results,
    }
    common.save_json(manifest, os.path.join(run_dir, "run_manifest.json"))
    return manifest


if __name__ == "__main__":
    sys.exit(main())
