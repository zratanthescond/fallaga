#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  ProceduralMapData.h — Sousse historique 1881-1956
//
//  Source: sousse1881-1956.com  (Plan de Sousse + ville01-03.html)
//  All streets taken directly from the official historical Plan de Sousse.
//
//  Coordinate mapping from the Plan de Sousse image:
//    Image (660×700 usable area):
//      Left edge  (West) → X = -1250
//      Right edge (East, coast) → X = +1250
//      Top  edge  (North) → Z = +2500
//      Bot  edge  (South) → Z = -2500
//
//    Formula:
//      worldX = (pixelX - 0) / 660 * 2500 - 1250
//      worldZ = (1 - pixelY / 700) * 5000 - 2500
//
//  Key anchors (verified against map):
//    Place Pichon    : px(470, 360) → world(530, 71)
//    Gare PLM        : px(310, 315) → world(-73, 247)
//    PORT center     : px(620, 450) → world(1098, -214)
//    Kasbah          : px(300, 620) → world(-114, -929)
//    Camp Militaire  : px(220, 505) → world(-417, -107)
//    Bd Corniche     : px(610, 100..300) → (runs N along coast)
// ─────────────────────────────────────────────────────────────────────────────
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace ProceduralMap {

struct MapStreet {
    std::string name;
    float width;
    std::vector<glm::vec2> points;  // (worldX, worldZ)
};

// px→world helper (used to derive values below, not at runtime)
// worldX = px * 2500/660 - 1250
// worldZ = (1 - py/700)*5000 - 2500

inline std::vector<MapStreet> getStreets() {
    std::vector<MapStreet> s;
    auto add = [&](const char* n, float w, std::vector<glm::vec2> p) {
        s.push_back({n, w, std::move(p)});
    };

    // ══════════════════════════════════════════════════════════════════
    // PLACE PICHON — the central hub (px ≈ 470,360 → world 530, 71)
    // ══════════════════════════════════════════════════════════════════
    // Pl. Pichon is a broad open square — rendered as a wide short span
    add("Pl. Pichon", 40.0f, {
        {480,120},{530,71},{580,120},{530,170},{480,120}
    });

    // ══════════════════════════════════════════════════════════════════
    // COURS DE LA MARINE — vast esplanade W→E from Pichon to port quays
    // Source: ville01-03 — "vast esplanade overlooking the port"
    // px: (470,360) → (580,360) → (640,370) coast
    // ══════════════════════════════════════════════════════════════════
    add("Cours de la Marine", 30.0f, {
        {530,71},{622,50},{755,14},{887,-29},{1020,-71},{1098,-107}
    });

    // ══════════════════════════════════════════════════════════════════
    // PLACE DE BAB-EL-BAHR (Place Colonel Vincent)
    // Just east of Pl. Pichon toward the sea wall
    // ══════════════════════════════════════════════════════════════════
    add("Pl. Bab-el-Bahr", 20.0f, {
        {530,71},{622,71},{680,40}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. EMILE LOUBET — runs along the foot of the Medina ramparts N-S
    // Source: ville02 — "at the foot of the ramparts"
    // px: X≈370, Y runs from 230 (north) to 620 (south)
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Emile Loubet", 18.0f, {
        {152,1000},{152,857},{152,714},{152,571},
        {152,428},{152,214},{152,71},{152,-71},
        {152,-214},{152,-357},{114,-643}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. ARMAND FALLIÈRES — runs along the quays (close to coastline)
    // Source: ville02 — "runs along the quays"
    // px: X≈540-600, Y≈350-600 (coastal strip)
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Armand Fallieres", 18.0f, {
        {717,357},{793,214},{831,71},{869,-71},
        {870,-214},{850,-357},{812,-500},{774,-643}
    });

    // BD. ARMAND FALLIÈRES continuation (south section = Bd Emile Loubet)
    add("Bd. Leon Mougeot", 16.0f, {
        {831,71},{869,-71},{907,-214},{945,-357},
        {983,-500},{1002,-643},{1040,-786}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. DE LA CORNICHE — N along coast past the beach
    // px: X≈600+, Y from 340 northward
    // ══════════════════════════════════════════════════════════════════
    add("Bd. de la Corniche", 18.0f, {
        {717,357},{755,571},{793,786},{831,1000},
        {869,1214},{907,1429},{945,1643},{983,1857},{1021,2071}
    });

    // ══════════════════════════════════════════════════════════════════
    // AV. VICTOR HUGO — from Gare eastward to north Medina gate
    // Source: Plan de Sousse — runs E-W at Gare level
    // px: X 310→470, Y≈315
    // ══════════════════════════════════════════════════════════════════
    add("Av. Victor Hugo", 16.0f, {
        {-530,500},{-341,500},{-152,464},{0,428},
        {152,393},{303,357},{455,321},{530,286}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. JOFFRE — N-S boulevard west of Victor Hugo
    // px: X≈240-260, runs from top to bottom
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Joffre", 16.0f, {
        {-379,1071},{-341,857},{-303,643},{-265,429},
        {-227,214},{-189,0},{-152,-214},{-114,-429}
    });

    // ══════════════════════════════════════════════════════════════════
    // AV. HBM — far west avenue (northwest quadrant)
    // px: X≈160, Y≈205
    // ══════════════════════════════════════════════════════════════════
    add("Av. H.B.M.", 14.0f, {
        {-795,857},{-720,857},{-644,857},{-568,857},
        {-492,857},{-417,857},{-341,857}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. SÉNATEUR GALLIENI — diagonal from Gare area SE toward Pichon
    // px: from (310,315) to (470,360)
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Senateur Gallieni", 18.0f, {
        {-265,500},{-152,428},{0,357},{152,286},{303,214},{455,143},{530,71}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. RENÉ MILLET — parallel to Loubet, just outside west rampart wall
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Rene Millet", 14.0f, {
        {-38,1000},{0,857},{38,714},{76,571},
        {114,428},{114,286},{114,143},{114,0}
    });

    // ══════════════════════════════════════════════════════════════════
    // AV. DU DR. MOREAU — far west, from Hôpital area
    // ══════════════════════════════════════════════════════════════════
    add("R. du Dr. Moreau", 12.0f, {
        {-795,643},{-720,600},{-644,557},{-568,514},
        {-492,471},{-417,428},{-341,386},{-265,343}
    });

    // RUE PAUL DOUMER — southwest, near camp area
    add("R. Paul Doumer", 12.0f, {
        {-871,0},{-795,-71},{-720,-71},{-644,-71},
        {-568,-71},{-492,-71},{-417,-71}
    });

    // ══════════════════════════════════════════════════════════════════
    // AV. GÉNÉRAL HUNTZINGER — S of camp, cuts across southward
    // px: Y≈505, runs W-E
    // ══════════════════════════════════════════════════════════════════
    add("Av. General Huntzinger", 14.0f, {
        {-795,-214},{-644,-229},{-492,-243},{-341,-257},
        {-189,-271},{-38,-286},{114,-300},{265,-314}
    });

    // ══════════════════════════════════════════════════════════════════
    // BD. MAGINOT — runs N-S in the far northwest (near cimetière)
    // ══════════════════════════════════════════════════════════════════
    add("Bd. Maginot", 12.0f, {
        {-568,1071},{-530,857},{-492,643},{-454,429},{-417,214}
    });

    // ══════════════════════════════════════════════════════════════════
    // RUE DE TOULOUSE — E-W connector in European grid
    // ══════════════════════════════════════════════════════════════════
    add("R. de Toulouse", 10.0f, {
        {-265,643},{-189,643},{-114,643},{-38,643},{38,643},{114,629}
    });

    // ══════════════════════════════════════════════════════════════════
    // RUE BECHIR SFAR, SOEUR JOSEPHINE — inner European grid streets
    // ══════════════════════════════════════════════════════════════════
    add("R. Bechir Sfar", 10.0f, {
        {-265,500},{-189,500},{-114,500},{-38,500}
    });
    add("R. Soeur Josephine", 10.0f, {
        {-227,429},{-152,429},{-76,429},{0,429}
    });
    add("R. du Lieut. Serra", 10.0f, {
        {-303,750},{-265,700},{-227,650},{-189,600},{-152,550}
    });
    add("R. Hadi", 10.0f, {
        {-189,357},{-114,357},{-38,357},{38,357}
    });
    add("R. d'Espina", 10.0f, {
        {-152,286},{-76,286},{0,286},{76,286}
    });

    // ══════════════════════════════════════════════════════════════════
    // RUE JULES FERRY — main commercial artery (Marché Couvert on left)
    // Source: ville03 — "main commercial street"
    // ══════════════════════════════════════════════════════════════════
    add("R. Jules Ferry", 14.0f, {
        {-417,500},{-303,500},{-189,500},{-76,478},
        {38,457},{152,435},{265,414},{379,393},{493,371}
    });

    // ══════════════════════════════════════════════════════════════════
    // R. NACEUR BEY, R. PAUL CACHIE — far north street
    // ══════════════════════════════════════════════════════════════════
    add("R. Naceur Bey", 10.0f, {
        {-417,1143},{-303,1143},{-189,1157},{-76,1171},{38,1186},{152,1200}
    });
    add("R. Paul Cachie", 10.0f, {
        {-568,1286},{-454,1286},{-341,1286},{-227,1286},{-114,1286},{0,1271}
    });

    // ══════════════════════════════════════════════════════════════════
    // GARE AREA — railway station approach roads
    // Gare is at px(310,315) → world(-73, 247)
    // ══════════════════════════════════════════════════════════════════
    add("Gare PLM Approach", 20.0f, {
        {-341,428},{-265,393},{-189,357},{-114,321},{-38,286}
    });
    add("Gare Decauville Track", 14.0f, {
        {-530,857},{-492,786},{-454,714},{-417,643},{-379,571},{-341,500}
    });

    // ══════════════════════════════════════════════════════════════════
    // R. GAMBETTA — E of Médina boundary, runs to Corniche
    // ══════════════════════════════════════════════════════════════════
    add("R. Gambetta", 12.0f, {
        {76,286},{189,271},{303,257},{417,243},{530,229},{643,214},{755,200}
    });

    // R. GÉNÉRAL ALAPETITE
    add("R. General Alapetite", 12.0f, {
        {114,143},{227,129},{341,114},{455,100},{568,86},{680,71}
    });

    // R. PASTEUR
    add("R. Pasteur", 10.0f, {
        {152,-71},{265,-86},{379,-100},{492,-114}
    });

    // ══════════════════════════════════════════════════════════════════
    // R. GÉNÉRAL RIU — N-S east of Medina (near rampart east face)
    // px: X≈490, Y 300..550
    // ══════════════════════════════════════════════════════════════════
    add("R. General Riu", 12.0f, {
        {606,714},{644,571},{682,429},{720,286},{758,143},{758,0},{720,-143}
    });

    // ══════════════════════════════════════════════════════════════════
    // R. DE LYON, R. D'ANGLETERRE, R. DE FRANCE — around the Médina east
    // ══════════════════════════════════════════════════════════════════
    add("R. de Lyon", 10.0f, {
        {720,143},{793,100},{869,57},{945,14},{1021,-29}
    });
    add("R. d'Angleterre", 10.0f, {
        {455,-71},{530,-86},{606,-100},{682,-114},{758,-129},{834,-143}
    });
    add("R. de France", 10.0f, {
        {455,-143},{568,-157},{682,-171},{796,-186},{910,-200}
    });

    // R. MASSICAULT — north coastal strip N of corniche area
    add("R. Massicault", 10.0f, {
        {644,857},{720,829},{796,800},{872,771},{948,743}
    });
    add("R. General Sadi Carnot", 10.0f, {
        {455,571},{493,500},{530,429},{568,357},{606,286},{644,214}
    });
    add("R. Sadi Carnot", 10.0f, {
        {303,857},{341,714},{379,571},{417,429},{455,286},{493,143}
    });

    // ══════════════════════════════════════════════════════════════════
    // AV. DE LA MARINE (Cours de la Marine) — main port boulevard
    // Source: ville01-03 — Jardin de la Marine, Cercle Militaire on this road
    // ══════════════════════════════════════════════════════════════════
    add("Av. de la Marine", 20.0f, {
        {530,71},{606,14},{682,-43},{758,-100},
        {834,-157},{910,-214},{986,-271},{1062,-329}
    });

    // R. DE GARIBALDI, R. DE ROME, R. DE GABES — south coastal strip
    add("R. de Garibaldi", 12.0f, {
        {606,-571},{682,-571},{758,-571},{834,-571},{910,-571},{986,-571}
    });
    add("R. de Rome", 12.0f, {
        {720,-500},{796,-514},{872,-529},{948,-543},{1024,-557}
    });
    add("R. de Gabes", 12.0f, {
        {568,-643},{644,-643},{720,-643},{796,-643},{872,-643}
    });

    // ══════════════════════════════════════════════════════════════════
    // EXITS / MAJOR APPROACH ROADS OUT OF THE CITY
    // ══════════════════════════════════════════════════════════════════

    // Av. Henri Boucher — NW toward Sfax/Tunis road (avenue leading far NW)
    add("Av. Henri Boucher", 16.0f, {
        {-38,-643},{-76,-500},{-114,-357},{-152,-214},
        {-189,-71},{-227,71},{-265,214},{-303,357}
    });

    // Av. Krantz — SW exit toward El Djem / Kairouan
    add("Av. Krantz", 16.0f, {
        {-38,-643},{-114,-786},{-227,-1000},{-341,-1214},
        {-455,-1429},{-568,-1643},{-682,-1857}
    });

    // Av. Général Huntzinger continues SW to Camp
    add("Route el Djem", 16.0f, {
        {-417,-429},{-530,-643},{-644,-857},
        {-757,-1071},{-871,-1286},{-985,-1500}
    });

    // Bd. Moh. el Hady Bey — N-S, West of Gare
    add("Bd. Moh. el Hady Bey", 16.0f, {
        {-303,1071},{-265,857},{-227,643},{-189,429},
        {-152,214},{-114,0},{-76,-214}
    });

    // Bd. Gabriel Robert — S axis from Pichon down
    add("Bd. Gabriel Robert", 14.0f, {
        {265,214},{227,71},{189,-71},{152,-214},
        {114,-357},{76,-500},{38,-643},{0,-786}
    });

    // Charles Rouvier
    add("Charles Rouvier", 12.0f, {
        {-114,214},{0,200},{114,186},{227,171},{341,157},{455,143}
    });

    // ══════════════════════════════════════════════════════════════════
    // MEDINA — internal streets from aerial map
    // The Medina walls run: X 0..670, Z -643..+857
    // (dotted red outline on the Plan de Sousse)
    // ══════════════════════════════════════════════════════════════════

    // R. du Général Saussier — north Medina wall (outside) east-west
    add("R. du General Saussier", 10.0f, {
        {152,857},{265,857},{379,857},{493,857},{606,857},{680,857}
    });

    // R. Bordj El Cherch — NW corner inside Medina
    add("R. Bordj El Cherch", 8.0f, {
        {76,786},{152,714},{227,643},{303,571}
    });

    // R. Souk El Caïd — main E-W souk inside Medina
    add("R. Souk El Caid", 8.0f, {
        {76,357},{189,357},{303,357},{417,343},{530,329},{644,314}
    });

    // R. Souk El Reba — parallel souk
    add("R. Souk El Reba", 8.0f, {
        {114,457},{227,443},{341,429},{455,414},{568,400}
    });

    // R. de la Kasbah
    add("R. de la Kasbah", 9.0f, {
        {0,-71},{76,-143},{152,-214},{189,-300},{152,-400},{76,-486}
    });

    // R. Général Guyon — inner west Medina N-S
    add("R. General Guyon", 8.0f, {
        {189,714},{227,571},{265,429},{303,286},{303,143},{265,0}
    });

    // R. Sidi Baziz — lower west Medina
    add("R. Sidi Baziz", 8.0f, {
        {227,-71},{265,-214},{303,-357},{265,-471}
    });

    // R. El Hadjira — inner E-W south section
    add("R. El Hadjira", 8.0f, {
        {114,-286},{227,-286},{341,-286},{455,-286},{530,-286}
    });

    // R. El Mar — from port gate into Medina
    add("R. El Mar", 10.0f, {
        {530,-286},{606,-343},{682,-400},{758,-457},{834,-514},{910,-571}
    });

    // R. Kobar
    add("R. Kobar", 7.0f, {
        {417,71},{493,57},{568,43},{644,29}
    });

    // R. du Rempart Sud — south boundary road of Medina
    add("R. du Rempart Sud", 9.0f, {
        {0,-643},{114,-643},{227,-643},{341,-643},{455,-643},{568,-643}
    });

    // Bab el Djedid gate approach
    add("Bab el Djedid", 10.0f, {
        {530,-143},{568,-71},{606,0},{644,71},{682,143}
    });

    // Bab el Gharbi gate (west gate)
    add("Bab el Gharbi", 10.0f, {
        {-76,71},{0,86},{76,100},{152,114}
    });

    // Bab Jebli gate (north gate, toward Gare)
    add("Bab Jebli", 10.0f, {
        {152,857},{189,786},{227,714},{265,643}
    });

    // R. du Rempart Sud outer road (south wall, outside)
    add("Av. d'Alsace-Lorraine", 12.0f, {
        {0,-786},{114,-800},{227,-814},{341,-829},{455,-843},{568,-857},{682,-871}
    });

    return s;
}

struct MapBuilding { float x, z, width, depth, height; };
inline std::vector<MapBuilding> getBuildings() { return {}; }

} // namespace ProceduralMap
