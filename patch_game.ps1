# patch_game.ps1 - Apply two edits to Game.cpp
$f = 'c:\Users\honco\OneDrive\Bureau\fallaga\src\Game.cpp'
$c = [System.IO.File]::ReadAllText($f)

# ── PATCH 1: snapPt - add draft-loop snapping ──────────────────────────────
$old1 = @'
                                // Snap onto the nearest existing road ENDPOINT (first/last control point)
                                auto snapPt = [&](const glm::vec3& in) -> glm::vec3 {
                                    if (!roadSnapToRoads) return in;
                                    glm::vec3 out = in;
                                    float bestD = roadSnapTolerance;
                                    for (const auto& seg : rg->segments) {
                                        const auto& cps = seg.controlPoints;
                                        if (cps.empty()) continue;
                                        auto probe = [&](const RoadPoint& cp) {
                                            float dx = out.x - cp.pos.x, dz = out.z - cp.pos.y;
                                            float d = std::sqrt(dx * dx + dz * dz);
                                            if (d < bestD) { bestD = d; out = glm::vec3(cp.pos.x, in.y, cp.pos.y); }
                                        };
                                        probe(cps.front());
                                        if (cps.size() > 1) probe(cps.back());
                                    }
                                    return out;
                                };
'@

$new1 = @'
                                // Snap onto the nearest existing road ENDPOINT (first/last control point)
                                // Also snap to the draft's own FIRST point so the user can close a loop.
                                auto snapPt = [&](const glm::vec3& in) -> glm::vec3 {
                                    if (!roadSnapToRoads) return in;
                                    glm::vec3 out = in;
                                    float bestD = roadSnapTolerance;
                                    // Snap to existing committed road endpoints
                                    for (const auto& seg : rg->segments) {
                                        const auto& cps = seg.controlPoints;
                                        if (cps.empty()) continue;
                                        auto probe = [&](const RoadPoint& cp) {
                                            float dx = out.x - cp.pos.x, dz = out.z - cp.pos.y;
                                            float d = std::sqrt(dx * dx + dz * dz);
                                            if (d < bestD) { bestD = d; out = glm::vec3(cp.pos.x, in.y, cp.pos.y); }
                                        };
                                        probe(cps.front());
                                        if (cps.size() > 1) probe(cps.back());
                                    }
                                    // Snap to the draft's own first point (closes a loop / round road)
                                    if (roadDraftPoints.size() >= 3) {
                                        const glm::vec3& front = roadDraftPoints.front();
                                        float dx = out.x - front.x, dz = out.z - front.z;
                                        float d = std::sqrt(dx * dx + dz * dz);
                                        if (d < bestD) { bestD = d; out = glm::vec3(front.x, in.y, front.z); }
                                    }
                                    return out;
                                };
'@

if ($c.Contains($old1.Replace("`r`n", "`n").Replace("`n", "`r`n"))) {
    Write-Host "PATCH1: CRLF match OK"
    $c = $c.Replace($old1.Replace("`r`n", "`n").Replace("`n", "`r`n"), $new1.Replace("`r`n", "`n").Replace("`n", "`r`n"))
} elseif ($c.Contains($old1.Replace("`r`n", "`n"))) {
    Write-Host "PATCH1: LF match OK"
    $c = $c.Replace($old1.Replace("`r`n", "`n"), $new1.Replace("`r`n", "`n"))
} else {
    Write-Error "PATCH1: target NOT FOUND in file!"
    exit 1
}

# ── PATCH 2: commitRoadDraft - detect closed loop and set ns.closed = true ─
$old2 = @'
        ns.type = (RoadType)roadDrawType;
        ns.width = roadDrawWidth;
        ns.shoulderWidth = roadDrawShoulder;
        // Ramer-Douglas-Peucker simplification (2D x/z) so freehand drags don't bake
        // in every 5m sample — keeps drawn roads smooth with far fewer control points.
'@

$new2 = @'
        ns.type = (RoadType)roadDrawType;
        ns.width = roadDrawWidth;
        ns.shoulderWidth = roadDrawShoulder;

        // ── Closed-loop detection ──────────────────────────────────────────────────
        // When the last draft point coincides with the first (user snapped the
        // cursor back to the start, or manually positioned it near the start),
        // mark this as a closed ring road so the mesh builder emits a seamless
        // loop with no seam kink, no texture squash, and no end caps.
        const float loopThresh = std::max(ns.width, 15.0f);
        const glm::vec3& dFront = roadDraftPoints.front();
        const glm::vec3& dBack  = roadDraftPoints.back();
        float dLoop = std::sqrt((dBack.x - dFront.x) * (dBack.x - dFront.x) +
                                (dBack.z - dFront.z) * (dBack.z - dFront.z));
        bool isClosedLoop = (roadDraftPoints.size() >= 4) && (dLoop < loopThresh);
        if (isClosedLoop) {
            // Snap the back point exactly onto the front so the mesh seam is pixel-perfect
            roadDraftPoints.back() = glm::vec3(dFront.x, dBack.y, dFront.z);
            ns.closed = true;
            std::cout << "[Road] Detected closed loop (gap=" << dLoop << "m, thresh=" << loopThresh << "m)\n";
        }

        // Ramer-Douglas-Peucker simplification (2D x/z) so freehand drags don't bake
        // in every 5m sample — keeps drawn roads smooth with far fewer control points.
'@

if ($c.Contains($old2.Replace("`r`n", "`n").Replace("`n", "`r`n"))) {
    Write-Host "PATCH2: CRLF match OK"
    $c = $c.Replace($old2.Replace("`r`n", "`n").Replace("`n", "`r`n"), $new2.Replace("`r`n", "`n").Replace("`n", "`r`n"))
} elseif ($c.Contains($old2.Replace("`r`n", "`n"))) {
    Write-Host "PATCH2: LF match OK"
    $c = $c.Replace($old2.Replace("`r`n", "`n"), $new2.Replace("`r`n", "`n"))
} else {
    Write-Error "PATCH2: target NOT FOUND in file!"
    exit 1
}

# ── PATCH 3: update commit log to mention CLOSED LOOP ─────────────────────
$old3 = @'
        std::cout << "[Road] Committed road id=" << ns.id
                  << " (" << ns.controlPoints.size() << " control points"
                  << (skipped > 0 ? ", auto-simplified " + std::to_string(skipped) + " pts" : "") << ")\n";
'@

$new3 = @'
        std::cout << "[Road] Committed road id=" << ns.id
                  << " (" << ns.controlPoints.size() << " control points"
                  << (ns.closed ? ", CLOSED LOOP" : "")
                  << (skipped > 0 ? ", auto-simplified " + std::to_string(skipped) + " pts" : "") << ")\n";
'@

if ($c.Contains($old3.Replace("`r`n", "`n").Replace("`n", "`r`n"))) {
    Write-Host "PATCH3: CRLF match OK"
    $c = $c.Replace($old3.Replace("`r`n", "`n").Replace("`n", "`r`n"), $new3.Replace("`r`n", "`n").Replace("`n", "`r`n"))
} elseif ($c.Contains($old3.Replace("`r`n", "`n"))) {
    Write-Host "PATCH3: LF match OK"
    $c = $c.Replace($old3.Replace("`r`n", "`n"), $new3.Replace("`r`n", "`n"))
} else {
    Write-Error "PATCH3: target NOT FOUND in file!"
    exit 1
}

[System.IO.File]::WriteAllText($f, $c, [System.Text.Encoding]::UTF8)
Write-Host "Game.cpp patched successfully."
