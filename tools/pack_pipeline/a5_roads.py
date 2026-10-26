"""A5 Road Geometry from the registered road-surface raster.

The supplied 4096x4096 raster is already projected into terrain space. This
worker preserves it verbatim, resamples it to 2017 for CPU/material queries,
and extracts a centerline graph with a width profile for road rendering.
"""

from __future__ import annotations

import os

import numpy as np
from scipy import ndimage

from . import common, contract

try:
    import cv2
except Exception:  # pragma: no cover
    cv2 = None


def _neighbors(x, y, pixels):
    found = []
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dx == 0 and dy == 0:
                continue
            q = (x + dx, y + dy)
            if q not in pixels:
                continue
            # Prefer the cardinal links when a diagonal merely cuts a corner.
            if dx and dy and ((x + dx, y) in pixels or (x, y + dy) in pixels):
                continue
            found.append(q)
    return found


def _rdp_indices(points, tolerance):
    """Return indices for an RDP simplification, preserving width samples."""
    if len(points) < 3 or tolerance <= 0:
        return np.arange(len(points), dtype=np.int32)
    pts = np.asarray(points, dtype=np.float64)
    keep = np.zeros(len(pts), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        start, end = stack.pop()
        if end <= start + 1:
            continue
        a, b = pts[start], pts[end]
        ab = b - a
        length = float(np.hypot(*ab))
        segment = pts[start + 1:end]
        if length < 1e-9:
            distances = np.hypot(*(segment - a).T)
        else:
            distances = np.abs(ab[0] * (a[1] - segment[:, 1]) -
                               (a[0] - segment[:, 0]) * ab[1]) / length
        local = int(np.argmax(distances))
        if distances[local] > tolerance:
            idx = start + 1 + local
            keep[idx] = True
            stack.append((start, idx))
            stack.append((idx, end))
    return np.flatnonzero(keep)


def _extract_graph(mask, radius_px, urban_mask):
    """Trace a globally skeletonized binary mask into node-linked polylines."""
    skeleton = common.skeletonize(mask, max_iter=200).astype(bool)
    ys, xs = np.nonzero(skeleton)
    pixels = set(zip(xs.tolist(), ys.tolist()))
    adjacency = {p: _neighbors(p[0], p[1], pixels) for p in pixels}
    special = {p for p, n in adjacency.items() if len(n) != 2}

    # Each connected cluster of endpoints/junction pixels becomes one graph node.
    special_image = np.zeros(mask.shape, np.uint8)
    for x, y in special:
        special_image[y, x] = 1
    node_of = {}
    nodes = []
    if cv2 is not None and special:
        count, labels, stats, centroids = cv2.connectedComponentsWithStats(
            special_image, connectivity=8)
        label_to_node = {}
        for label in range(1, count):
            area = int(stats[label, cv2.CC_STAT_AREA])
            if area == 0:
                continue
            center = (float(centroids[label, 0]), float(centroids[label, 1]))
            terrain = [center[0] / contract.ROAD_MASK_PX_PER_TERRAIN_PX,
                       center[1] / contract.ROAD_MASK_PX_PER_TERRAIN_PX]
            world = contract.terrain_px_to_world(*terrain)
            node_id = len(nodes)
            nodes.append({"id": node_id, "terrain_px": terrain,
                          "world_m": list(world), "kind": "junction"})
            label_to_node[label] = node_id
        for x, y in special:
            node_of[(x, y)] = label_to_node[int(labels[y, x])]

    # Closed loops have no degree-1/3 pixel. Give each such component one anchor.
    if cv2 is not None and pixels:
        skel_image = skeleton.astype(np.uint8)
        count, labels = cv2.connectedComponents(skel_image, connectivity=8)
        seen_labels = set(int(v) for v in labels[ys, xs] if v > 0)
        labels_with_nodes = {int(labels[y, x]) for x, y in node_of}
        for label in seen_labels - labels_with_nodes:
            pos = np.flatnonzero(labels.ravel() == label)
            if not pos.size:
                continue
            point = (int(pos[0] % mask.shape[1]), int(pos[0] // mask.shape[1]))
            terrain = [point[0] / contract.ROAD_MASK_PX_PER_TERRAIN_PX,
                       point[1] / contract.ROAD_MASK_PX_PER_TERRAIN_PX]
            nodes.append({"id": len(nodes), "terrain_px": terrain,
                          "world_m": list(contract.terrain_px_to_world(*terrain)),
                          "kind": "loop_anchor"})
            node_of[point] = len(nodes) - 1

    edges = []
    visited = set()

    def edge_key(a, b):
        return (a, b) if a < b else (b, a)

    def node_for(point):
        terrain = [point[0] / contract.ROAD_MASK_PX_PER_TERRAIN_PX,
                   point[1] / contract.ROAD_MASK_PX_PER_TERRAIN_PX]
        nodes.append({"id": len(nodes), "terrain_px": terrain,
                      "world_m": list(contract.terrain_px_to_world(*terrain)),
                      "kind": "endpoint"})
        node_of[point] = len(nodes) - 1
        return len(nodes) - 1

    starts = [(p, q) for p in sorted(node_of, key=lambda p: (p[1], p[0]))
              for q in adjacency[p] if q not in node_of]
    for start, first in starts:
        if edge_key(start, first) in visited:
            continue
        start_id = node_of[start]
        path = [start, first]
        visited.add(edge_key(start, first))
        previous, current = start, first
        end_id = None
        for _ in range(len(pixels) + 1):
            if current in node_of:
                end_id = node_of[current]
                break
            choices = [q for q in adjacency[current]
                       if q != previous and edge_key(current, q) not in visited]
            if not choices:
                end_id = node_for(current)
                break
            nxt = min(choices, key=lambda p: (p[1], p[0]))
            visited.add(edge_key(current, nxt))
            path.append(nxt)
            previous, current = current, nxt
        if end_id is None or len(path) < 2:
            continue

        pts = np.asarray(path, dtype=np.float64)
        radii = np.asarray([radius_px[y, x] for x, y in path], dtype=np.float64)
        tolerance_mask_px = 1.5 / (contract.METRES_PER_PX /
                                   contract.ROAD_MASK_PX_PER_TERRAIN_PX)
        keep = _rdp_indices(pts, tolerance_mask_px)
        pts = pts[keep]
        radii = radii[keep]
        terrain_px = pts / contract.ROAD_MASK_PX_PER_TERRAIN_PX
        width_profile = 2.0 * radii * contract.METRES_PER_PX / contract.ROAD_MASK_PX_PER_TERRAIN_PX
        width = float(np.median(width_profile))
        mx, my = terrain_px[len(terrain_px) // 2]
        ix = int(np.clip(round(mx), 0, contract.GRID - 1))
        iy = int(np.clip(round(my), 0, contract.GRID - 1))
        urban = bool(urban_mask[iy, ix])
        cls = "main" if width >= 18.0 else "secondary" if urban or width >= 9.0 else "track"
        edges.append({
            "id": len(edges), "class": cls, "width_m": width,
            "width_profile_m": width_profile.tolist(),
            "a": start_id, "b": end_id,
            "polyline_terrain_px": terrain_px.tolist(),
            "polyline_mask_px": pts.astype(int).tolist(),
        })

    return nodes, edges, skeleton


def _extract_polygons(mask, min_area_px=8):
    polygons = []
    if cv2 is None:
        return polygons
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask.astype(np.uint8), 8)
    for label in range(1, count):
        if stats[label, cv2.CC_STAT_AREA] < min_area_px:
            continue
        component = (labels == label).astype(np.uint8) * 255
        contours, _ = cv2.findContours(component, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        for contour in contours:
            approx = cv2.approxPolyDP(contour, 0.35, True).reshape(-1, 2)
            if len(approx) < 3:
                continue
            terrain_px = (approx.astype(np.float64) /
                          contract.ROAD_MASK_PX_PER_TERRAIN_PX).tolist()
            polygons.append({"id": len(polygons), "kind": "quay_or_paved_island",
                             "terrain_px": terrain_px})
    return polygons


def rasterize_graph(graph, polygons, size=contract.ROAD_MASK_CANVAS):
    """Rebuild a road coverage raster from graph widths and polygon geometry."""
    canvas = np.zeros((size, size), np.uint8)
    if cv2 is None:
        return canvas
    radius_per_mask_px = (contract.METRES_PER_PX /
                          contract.ROAD_MASK_PX_PER_TERRAIN_PX)

    def stamp_disk(cx, cy, radius):
        # EDT radii are distances between pixel centers; reducing by 0.1 px
        # gives the same edge inclusion as the source's antialiased mask raster.
        radius = max(0.0, float(radius) - 0.1)
        extent = max(0.5, radius)
        x0 = max(0, int(np.floor(cx - extent)))
        x1 = min(size, int(np.ceil(cx + extent)) + 1)
        y0 = max(0, int(np.floor(cy - extent)))
        y1 = min(size, int(np.ceil(cy + extent)) + 1)
        yy, xx = np.ogrid[y0:y1, x0:x1]
        canvas[y0:y1, x0:x1][(xx - cx) ** 2 + (yy - cy) ** 2 <= radius ** 2] = 255

    graph_samples = graph.get("rasterization_samples_mask_px")
    graph_widths = graph.get("rasterization_width_profile_m")
    if graph_samples is not None and graph_widths is not None:
        for (x, y), width in zip(graph_samples, graph_widths):
            stamp_disk(float(x), float(y), width / (2.0 * radius_per_mask_px))
    else:
        for edge in graph.get("edges", []):
            pts = np.asarray(edge.get("polyline_mask_px", []), dtype=np.float64)
            widths = edge.get("width_profile_m", [])
            if len(pts) < 1 or len(widths) != len(pts):
                continue
            for (x, y), width in zip(pts, widths):
                stamp_disk(x, y, width / (2.0 * radius_per_mask_px))
    for polygon in polygons:
        pts = np.rint(np.asarray(polygon["terrain_px"]) *
                      contract.ROAD_MASK_PX_PER_TERRAIN_PX).astype(np.int32)
        if len(pts) >= 3:
            cv2.fillPoly(canvas, [pts], 255, lineType=cv2.LINE_8)
    return canvas > 0


def run(run_dir, road_json=None, offset=(None, None), simplify_px=None):
    del road_json, offset, simplify_px  # retained for CLI compatibility; raster is registered.
    source_gray = common.load_gray(contract.ROAD_SURFACE_MASK)
    source_mask = source_gray > 127
    road2017 = common.load_road_mask_2017()
    bands = (common.load_gray(contract.ROAD_BANDS_MASK) > 127
             if os.path.exists(contract.ROAD_BANDS_MASK) else source_mask)
    quay_islands = (common.load_gray(contract.ROAD_QUAY_ISLANDS_MASK) > 127
                    if os.path.exists(contract.ROAD_QUAY_ISLANDS_MASK)
                    else np.zeros_like(source_mask))
    if source_mask.shape != (contract.ROAD_MASK_CANVAS, contract.ROAD_MASK_CANVAS):
        raise ValueError("registered road surface source must be 4096x4096")

    urban_rgb = common.load_rgb(contract.URBAN_MASK)
    urban = np.any(urban_rgb > 40, axis=2)
    radius_px = ndimage.distance_transform_edt(bands)
    nodes, edges, skeleton = _extract_graph(bands, radius_px, urban)
    polygons = _extract_polygons(quay_islands)

    graph = {
        "source": os.path.relpath(contract.ROAD_SURFACE_MASK, contract.REPO_ROOT),
        "coordinate_source": "registered terrain-space mask; no re-projection",
        "metres_per_mask_px": contract.METRES_PER_PX / contract.ROAD_MASK_PX_PER_TERRAIN_PX,
        "registration_matrix_map_to_2017px": contract.ROAD_MATRIX,
        "node_count": len(nodes), "edge_count": len(edges),
        "nodes": nodes, "edges": edges,
    }
    center_y, center_x = np.nonzero(skeleton)
    graph["rasterization_samples_mask_px"] = np.column_stack(
        [center_x, center_y]).tolist()
    graph["rasterization_width_profile_m"] = (
        2.0 * radius_px[skeleton] * contract.METRES_PER_PX /
        contract.ROAD_MASK_PX_PER_TERRAIN_PX).tolist()

    # Preserve the supplied source exactly. The graph round-trip is stored
    # separately and measured by A14 against this immutable source copy.
    common.save_gray(source_gray, os.path.join(run_dir, "road_surface_mask_4096.png"))
    common.save_gray(road2017.astype(np.uint8) * 255,
                     os.path.join(run_dir, "road_surface_mask_2017.png"))
    common.save_gray(skeleton.astype(np.uint8) * 255,
                     os.path.join(run_dir, "road_centerlines_4096.png"))
    roundtrip = rasterize_graph(graph, polygons)
    common.save_gray(roundtrip.astype(np.uint8) * 255,
                     os.path.join(run_dir, "road_graph_rerasterized_4096.png"))
    common.save_json(graph, os.path.join(run_dir, "road_graph.json"))
    common.save_json({"polygons": polygons}, os.path.join(run_dir, "road_polygons.json"))
    common.save_gray(source_mask[::4, ::4].astype(np.uint8) * 255,
                     os.path.join(run_dir, "road_mask_preview.png"))

    return {
        "road_graph": graph, "road_polygons": polygons,
        "mask_2017": road2017, "mask_4096_path": contract.ROAD_SURFACE_MASK,
        "edge_count": len(edges), "node_count": len(nodes),
        "polygon_count": len(polygons), "coverage_frac": float(source_mask.mean()),
        "source": os.path.relpath(contract.ROAD_SURFACE_MASK, contract.REPO_ROOT),
    }
