"""A2 Historian.

Supplies era rules only (allowed / forbidden asset lists, palettes, heights,
ship types) for Sousse c.1930s. It never touches geometry.

Sources consulted (see era_rules.json "sources"): Britannica, Wikipedia
(Sousse / Medina of Sousse / French protectorate of Tunisia), UNESCO WHC 498.
Claims are marked verified / unverified.
"""

from __future__ import annotations

import os

from . import common


def run(run_dir):
    rules = {
        "agent": "A2",
        "era": "Sousse, Tunisia, French protectorate, c.1930s",
        "allowed": {
            "landmarks": [
                {"name": "Medina walls", "verified": True,
                 "note": "Byzantine and Aghlabid ramparts (UNESCO WHC 498)."},
                {"name": "Great Mosque of Sousse", "verified": True,
                 "note": "Founded 9th c. by the Aghlabid emir Abu al-Abbas Muhammad."},
                {"name": "Ribat of Sousse", "verified": True,
                 "note": "9th c. monastery-fortress."},
                {"name": "Kasbah", "verified": True,
                 "note": "Part of the medina precinct (UNESCO WHC 498)."},
                {"name": "Souks / covered bazaars", "verified": True},
                {"name": "Port moles, quays, warehouses", "verified": True,
                 "note": "Port enlarged under the protectorate."},
            ],
            "buildings": [
                {"type": "medina house", "style": "whitewashed, flat/low roofs, 1-3 floors",
                 "roof": "flat or low terracotta", "verified": True},
                {"type": "ville nouvelle block", "style": "colonial, arcaded ground floor",
                 "roof": "ochre / terracotta tile, 2-4 floors", "verified": True},
                {"type": "port warehouse", "style": "stone/brick, long low sheds", "verified": False},
                {"type": "rail station & lines", "style": "colonial railway",
                 "verified": True, "note": "Railways built under the protectorate."},
            ],
            "ships": [
                {"type": "steamship / coaster", "verified": True},
                {"type": "sailing fishing boat", "verified": True},
                {"type": "phosphate / olive-oil cargo ship", "verified": True,
                 "note": "Port handled mostly phosphates and olive oil."},
            ],
            "vehicles": [
                {"type": "early truck / lorry", "verified": False},
                {"type": "1920s-30s car", "verified": False},
                {"type": "horse cart / donkey", "verified": True},
            ],
            "vegetation": [
                {"type": "olive grove", "verified": True,
                 "note": "Coastal plain cultivated with olive trees."},
                {"type": "esparto grass (steppe)", "verified": True},
                {"type": "palm (urban gardens)", "verified": False},
            ],
        },
        "forbidden": [
            {"token": "port_elkantaoui", "why": "Port El Kantaoui built 1979."},
            {"token": "marina", "why": "Tourist marina is post-1960s."},
            {"token": "hotel_strip", "why": "Hotel complexes are post-1960s tourism."},
            {"token": "university", "why": "University of Sousse founded 1986."},
            {"token": "highrise", "why": "No towers in the 1930s."},
            {"token": "modern_highway", "why": "No motorways in the 1930s."},
            {"token": "container_crane", "why": "Container handling is modern."},
        ],
        "palette": {
            "medina_wall": "#EAE3D6",
            "medina_roof": "#C9A87C",
            "ville_nouvelle_roof": "#B5623C",
            "ochre_soil": "#B0703F",
            "olive": "#5B6B3A",
            "steppe": "#C9BE79",
            "sand": "#E4D6B0",
            "salt_crust": "#EFEFE8",
        },
        "heights_m": {"medina": [3.0, 10.0], "ville_nouvelle": [8.0, 15.0],
                      "warehouse": [5.0, 9.0]},
        "population_context": {
            "sousse_1899": 7000, "pre_ww1": 25000,
            "note": "All lived in the walled medina in 1899; French protectorate "
                    "expanded the port and built railways and public buildings.",
            "verified": True,
        },
        "sources": [
            {"ref": "Britannica, 'Sousse'",
             "url": "https://www.britannica.com/place/Sousse"},
            {"ref": "Wikipedia, 'Sousse'",
             "url": "https://en.wikipedia.org/wiki/Sousse"},
            {"ref": "Wikipedia, 'Medina of Sousse'",
             "url": "https://en.wikipedia.org/wiki/Medina_of_Sousse"},
            {"ref": "UNESCO WHC 498, Medina of Sousse",
             "url": "https://whc.unesco.org/en/list/498"},
            {"ref": "Wikipedia, 'French protectorate of Tunisia'",
             "url": "https://en.wikipedia.org/wiki/French_protectorate_of_Tunisia"},
        ],
    }
    common.save_json(rules, os.path.join(run_dir, "era_rules.json"))
    return {"agent": "A2", "allowed_groups": list(rules["allowed"].keys()),
            "forbidden": len(rules["forbidden"]), "sources": len(rules["sources"])}
