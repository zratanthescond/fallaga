// roadfix_test.cpp
// Copie autonome des etapes de correction d'intersection de src/RoadSystem.cpp
// (steps 0, 1, 4, 4b, 5) sans aucune dependance projet. Fichier racine, ne touche
// a rien du projet : tu peux le patcher et tester librement.
//
// Petits types VEC2 :  g++ -std=c++17 -O1 roadfix_test.cpp -o roadfix_test.exe
// Petits types LICENCE : libre comme l'air.
//
// Resultats attendus pour le reseau de seed (A 101 + B 102 en L a 90 degres,
// D 104 boucle fermee, E 105 rabbatte sur l'avenue A, C 103 isole) :
//   caps=... clusters=... joints=... junction wedges=... pads=...

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Version MINIMAL de glm::vec2 (pas de glm pour rester autonome)
// ---------------------------------------------------------------------------
struct vec2 {
    float x = 0.f, y = 0.f;
    vec2() = default;
    vec2(float xx, float yy) : x(xx), y(yy) {}
    vec2 operator+(const vec2& o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(const vec2& o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
    vec2 operator/(float s) const { return {x / s, y / s}; }
    vec2& operator+=(const vec2& o) { x += o.x; y += o.y; return *this; }
    vec2& operator/=(float s) { x /= s; y /= s; return *this; }
    float dot(const vec2& o) const { return x * o.x + y * o.y; }
    float cross(const vec2& o) const { return x * o.y - y * o.x; }
    float length() const { return std::sqrt(x * x + y * y); }
    vec2 normalized() const { float l = length(); return l > 1e-4f ? (*this / l) : vec2(1, 0); }
};

enum class RoadType { UrbanStreet = 0, CountryDirt, ColonialAvenue };

struct RoadSegment {
    int id = 0;
    float width = 8.f;
    RoadType type = RoadType::UrbanStreet;
    bool closed = false;
    bool branch = false;               // nourri par le step 0 (retraction)
    std::vector<vec2> controlPoints;   // polyline simple (pas de catmull pour le test)
};

// Extrait le "spline" = liste de points courus. Ici, recopie des control points.
static std::vector<vec2> sampleSpline(const RoadSegment& s) {
    std::vector<vec2> pts = s.controlPoints;
    if (pts.empty()) pts.push_back(vec2(0, 0));
    return pts;
}

struct EndRef {
    int seg = -1;
    bool isBack = false;
    vec2 pos;        // position de la bouche (apres retraction si prise)
    float hw = 4.f;  // demi-largeur
};

// ---------------------------------------------------------------------------
// STEP 1 : clustering des extremites (joints <= 2.5 m)
// ---------------------------------------------------------------------------
struct Cluster {
    std::vector<int> ends;   // index dans "ends"
    vec2 center;
};

static bool equalEnd(const std::vector<EndRef>& ends, int a, int b) {
    return ends[a].seg == ends[b].seg;
}

static int countRealSegs(const std::vector<EndRef>& ends, const std::vector<int>& cl) {
    int seen = 0;
    for (size_t a = 0; a < cl.size(); ++a) {
        bool dup = false;
        for (size_t b = 0; b < a; ++b)
            if (equalEnd(ends, cl[b], cl[a])) { dup = true; break; }
        if (!dup) ++seen;
    }
    return seen;
}

static std::vector<Cluster> clusterEndpoints(const std::vector<EndRef>& ends,
                                             float tol = 2.5f) {
    std::vector<Cluster> clusters;
    std::vector<bool> used(ends.size(), false);
    for (size_t i = 0; i < ends.size(); ++i) {
        if (used[i]) continue;
        Cluster c;
        c.ends.push_back((int)i);
        used[i] = true;
        bool grew = true;
        while (grew) {
            grew = false;
            for (size_t j = 0; j < ends.size(); ++j) {
                if (used[j]) continue;
                for (int k : c.ends) {
                    if ((ends[j].pos - ends[k].pos).length() <= tol) {
                        c.ends.push_back((int)j);
                        used[j] = true;
                        grew = true;
                        break;
                    }
                }
            }
        }
        c.center = vec2(0, 0);
        for (int k : c.ends) c.center += ends[k].pos;
        c.center /= (float)c.ends.size();
        clusters.push_back(c);
    }
    return clusters;
}

// ---------------------------------------------------------------------------
// STEP 0 : retraction d'une extremite dans le corridor d'une route hote
//   (l'extremite est tiree jusqu'au bord de l'hote -> confluence fondue)
// ---------------------------------------------------------------------------
static void retractEnds(std::vector<RoadSegment>& segs,
                        std::vector<EndRef>& ends,
                        const std::vector<std::vector<vec2>>& splines) {
    const int S = (int)segs.size();
    // deja en jonction avec l'extremite d'une AUTRE route -> on ne retracte pas
    std::vector<bool> jointed(ends.size(), false);
    for (size_t i = 0; i < ends.size(); ++i)
        for (size_t j = 0; j < ends.size(); ++j)
            if (i != j && ends[i].seg != ends[j].seg &&
                (ends[i].pos - ends[j].pos).length() <= 2.5f)
                jointed[i] = jointed[j] = true;

    for (int e = 0; e < (int)ends.size(); ++e) {
        const int es = ends[e].seg;
        if (segs[es].closed) continue;
        if (jointed[e]) continue;   // deja colle a une autre route : rien a tirer
        const vec2 P = ends[e].pos;
        for (int h = 0; h < S && !segs[es].branch; ++h) {
            if (h == es) continue;
            const auto& hp = splines[h];
            const float hostHw = segs[h].width * 0.5f;
            const float tol = hostHw + 1.5f;

            // point le plus proche de P sur le spline hote (par segments)
            float best = 1e30f;
            vec2 Q = hp[0];
            vec2 perp(1, 0);
            for (size_t i = 0; i + 1 < hp.size(); ++i) {
                vec2 A = hp[i], B = hp[i + 1];
                vec2 AB = B - A;
                float lenAB = AB.length();
                if (lenAB < 1e-4f) continue;
                float t = ((P - A).dot(AB)) / (lenAB * lenAB);
                t = std::max(0.f, std::min(1.f, t));
                vec2 Qq = A + AB * t;
                vec2 pq = Qq - P;
                float d = pq.length();
                if (d < best) {
                    best = d;
                    Q = Qq;
                    perp = pq.normalized(); // pointe de Q vers P (donne le cote)
                }
            }

            // remarque: l'oracle exige aussi que P soit dans l'extension de l'hote
            if (best <= tol) {
                // tire P jusqu'au BORD de l'hote (cote perp)
                vec2 edge = Q + perp * hostHw;
                ends[e].pos = edge;

                // l'extremite "toujours debout" pour les caps / pads
                segs[es].branch = true;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Simulation : un mesh est une liste de sommets + indices (fake).
// On ne produit pas de vraie geometrie : on compte et on decrit.
// ---------------------------------------------------------------------------
struct TestMesh {
    std::vector<vec2> verts;
    std::vector<int> tris;
    int caps = 0, joints = 0, wedges = 0;
};

static vec2 memTangent(const EndRef& e, const std::vector<RoadSegment>& segs) {
    // tangente sortante de la bouche (pointe vers l'exterieur de la route)
    const auto& ctrl = segs[e.seg].controlPoints;
    vec2 d;
    if (e.isBack)
        d = ctrl[ctrl.size() - 2] - ctrl[ctrl.size() - 1];
    else
        d = ctrl[1] - ctrl[0];
    return d.normalized();
}

static vec2 memTangent(const EndRef& e, const std::vector<RoadSegment>& segs);

// ---------------------------------------------------------------------------
// STEP 4 : bouchons arrondis des vrais cul-de-sac
// STEP 4b : joints bout-a-bout (deux routes alignees, meme sens)
// STEP 5  : pads de jonction (2+ routes) — LE "CAESAR"
// ---------------------------------------------------------------------------
static TestMesh buildMesh(const std::vector<EndRef>& ends,
                          const std::vector<Cluster>& clusters,
                          const std::vector<RoadSegment>& segs) {
    TestMesh out;

    // STEP 4 : un cul-de-sac = cluster avec une seule route, et pas fermee
    for (int c = 0; c < (int)clusters.size(); ++c) {
        if (countRealSegs(ends, clusters[c].ends) == 1) {
            int id0 = clusters[c].ends[0];
            if (!segs[ends[id0].seg].closed && !segs[ends[id0].seg].branch)
                ++out.caps;   // un vrai cul-de-sac : cap arrondi
        }
    }

    // STEP 4b : joint bout-a-bout (2 routes dans le cluster, continue)
    for (int c = 0; c < (int)clusters.size(); ++c) {
        if (countRealSegs(ends, clusters[c].ends) == 2) ++out.joints;
    }

    // STEP 5 : pads de jonction
    for (int c = 0; c < (int)clusters.size(); ++c) {
        const std::vector<int>& cl = clusters[c].ends;
        if (countRealSegs(ends, cl) < 2) continue;

        const vec2 C = clusters[c].center;

        // chaque bouche -> 2 points de rive (L R) + tangente sortante + demi-largeur
        struct JnM { vec2 rimL, rimR, tan; float hw; };
        std::vector<JnM> mem;
        for (int id : cl) {
            const EndRef& e = ends[id];
            vec2 t = memTangent(e, segs);
            vec2 r2(-t.y, t.x); // perpendiculaire gauche
            JnM m = { e.pos - r2 * e.hw, e.pos + r2 * e.hw, t, e.hw };
            mem.push_back(m);
        }
        const int M = (int)mem.size();
        if (M < 2) continue;
        if (M == 2) {
            // continuation rectiligne quasi identique (meme ligne, meme largeur) :
            // le pont quad 4b suffit, un pad serait un sliver degeneres.
            // INDIQUER : |dot|>0.94 couvre la continuation DANS LE MEME SENS et
            // celle FACE A FACE ; le code du vrai moteur (ang < 0.35f) laisse
            // echapper le cas face-a-face -> a corriger la-bas aussi.
            bool parallel = std::fabs(mem[0].tan.dot(mem[1].tan)) > 0.94f;
            bool wideStep = std::fabs(mem[0].hw - mem[1].hw) >
                            0.2f * std::max(mem[0].hw, mem[1].hw);
            if (parallel && !wideStep) continue;
        }

        // tri des rives autour du centre (comme l'original)
        struct Pt { int m; float ang; vec2 p; };
        std::vector<Pt> ring;
        ring.reserve(M * 2);
        for (int i = 0; i < M; ++i) {
            ring.push_back({ i, 0.f, mem[i].rimR });
            ring.push_back({ i, 0.f, mem[i].rimL });
        }
        for (auto& pt : ring)
            pt.ang = std::atan2(pt.p.y - C.y, pt.p.x - C.x);
        std::sort(ring.begin(), ring.end(),
                  [](const Pt& a, const Pt& b) { return a.ang < b.ang; });

        // padLift : 0.02 si au moins un membre est une retraction (bord d'hote),
        // sinon 0.015. C'est le DECALLAGE en hauteur du pad (anti-z-fight).
        float padLift = 0.015f;
        for (int id : cl)
            if (segs[ends[id].seg].branch) { padLift = 0.02f; break; }

        // entire pad : chaque paire de rives consecutives d'angles differents
        for (int i = 0; i < (int)ring.size(); ++i) {
            const Pt& A = ring[i];
            const Pt& B = ring[(i + 1) % ring.size()];
            if (A.m == B.m) continue;
            out.wedges++;   // une nappe triangulaire dont le 3e sommet est C
            (void)padLift;  // en vrai : emitter avec lift (pas de geom ici)
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// main() de test
// ---------------------------------------------------------------------------
static void runCase(const std::vector<RoadSegment>& segs, const char* label) {
    std::vector<std::vector<vec2>> splines;
    std::vector<EndRef> ends;
    for (int sid = 0; sid < (int)segs.size(); ++sid) {
        const auto& s = segs[sid];
        splines.push_back(sampleSpline(s));
        const auto& pts = splines.back();
        if (s.closed) continue;   // boucle fermee : pas de bouche libre
        EndRef f; f.seg = sid; f.isBack = false;
        f.pos = pts.front(); f.hw = s.width * 0.5f;
        EndRef b; b.seg = sid; b.isBack = true;
        b.pos = pts.back();  b.hw = f.hw;
        ends.push_back(f);
        ends.push_back(b);
    }

    // STEP 0 : retraction (mute "ends" + marque seg.branch)
    retractEnds(const_cast<std::vector<RoadSegment>&>(segs),
                ends, splines);

    // STEP 1 : clustering APRES retraction (comme dans le vrai pipeline)
    std::vector<Cluster> clusters = clusterEndpoints(ends, 2.5f);

    TestMesh m = buildMesh(ends, clusters, segs);

    std::printf("[%s] ends=%zu clusters=%zu  caps=%d joints=%d junction_wedges=%d\n",
                label, ends.size(), clusters.size(), m.caps, m.joints, m.wedges);
    for (size_t c = 0; c < clusters.size(); ++c) {
        const auto& cl = clusters[c].ends;
        std::printf("    cluster %zu : %d route(s) a (%.1f, %.1f)  [",
                    c, countRealSegs(ends, cl), clusters[c].center.x, clusters[c].center.y);
        for (size_t k = 0; k < cl.size(); ++k)
            std::printf(" seg%d:%s ", ends[cl[k]].seg, ends[cl[k]].isBack ? "back" : "front");
        std::printf("]%s\n", segs[ends[cl[0]].seg].branch ? "  (retracte)" : "");
    }
}

int main() {
    // ---- seed identique a main.cpp : seedTestRoadNetwork ----
    RoadSegment a; a.id = 101; a.width = 12.f; a.type = RoadType::UrbanStreet;
    a.controlPoints = { {4190, -505}, {4280, -540}, {4370, -600} };

    RoadSegment b; b.id = 102; b.width = 7.f; b.type = RoadType::CountryDirt;
    b.controlPoints = { {4190, -505}, {4190, -440} };   // partage le point -> L a 90 degres

    RoadSegment c; c.id = 103; c.width = 6.f; c.type = RoadType::CountryDirt;
    c.controlPoints = { {4480, -700}, {4540, -720} };

    RoadSegment e; e.id = 105; e.width = 8.f; e.type = RoadType::ColonialAvenue;
    e.controlPoints = { {4235, -462}, {4235, -640} };    // back rabattra sur l'avenue A

    RoadSegment d; d.id = 104; d.width = 10.f; d.type = RoadType::UrbanStreet;
    d.controlPoints = { {4500, -500}, {4590, -470}, {4650, -550},
                        {4600, -630}, {4510, -600}, {4503, -503} };
    d.closed = true;

    std::vector<RoadSegment> seed = { a, b, c, e, d };
    runCase(seed, "seed 5 routes");

    // ---- cas 2 : deux routes colilineaires meme largeur (test du refus de pad) ----
    RoadSegment x; x.id = 201; x.width = 8.f;
    x.controlPoints = { {0, 0}, {10, 0}, {20, 0} };
    RoadSegment y; y.id = 202; y.width = 8.f;
    y.controlPoints = { {20, 0}, {30, 0}, {40, 0} };
    runCase({ x, y }, "colineaire meme largeur  -> pad refuse");

    // ---- cas 3 : deux routes en angle, largeur differente (pad attendu) ----
    RoadSegment p; p.id = 301; p.width = 10.f;
    p.controlPoints = { {0, 0}, {100, 0} };
    RoadSegment q; q.id = 302; q.width = 5.f;
    q.controlPoints = { {0, 0}, {0, 100} };
    runCase({ p, q }, "angle 90 degres, largeurs 10/5 -> pad attendu");

    // ---- cas 4 : trois routes en etoile ----
    RoadSegment s1; s1.id = 401; s1.width = 8.f;   s1.controlPoints = { {0, 0}, {50, 0} };
    RoadSegment s2; s2.id = 402; s2.width = 8.f;   s2.controlPoints = { {0, 0}, {-25, 43} };
    RoadSegment s3; s3.id = 403; s3.width = 8.f;   s3.controlPoints = { {0, 0}, {-25, -43} };
    runCase({ s1, s2, s3 }, "etoile a 3 routes   -> pads 3-membres");

    return 0;
}