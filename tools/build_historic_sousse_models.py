import numpy as np
import os
import math

class ObjMesh:
    def __init__(self, mtl_filename):
        self.vertices = []
        self.texcoords = []
        self.normals = []
        self.faces = [] # list of (material, [(v_idx, vt_idx, vn_idx), ...])
        self.current_mtl = "default"
        self.mtl_filename = mtl_filename
        self.materials = {} # name -> dict of properties

    def set_material(self, name):
        self.current_mtl = name

    def add_material(self, name, kd=(1,1,1), map_kd=None, map_bump=None, d=1.0):
        self.materials[name] = {
            "Kd": kd,
            "map_Kd": map_kd,
            "map_Bump": map_bump,
            "d": d
        }

    def add_vertex(self, x, y, z):
        self.vertices.append((float(x), float(y), float(z)))
        return len(self.vertices)

    def add_uv(self, u, v):
        self.texcoords.append((float(u), float(v)))
        return len(self.texcoords)

    def add_normal(self, nx, ny, nz):
        l = math.sqrt(nx*nx + ny*ny + nz*nz)
        if l > 1e-6:
            nx, ny, nz = nx/l, ny/l, nz/l
        self.normals.append((float(nx), float(ny), float(nz)))
        return len(self.normals)

    def add_triangle(self, v0, v1, v2, vt0, vt1, vt2, n=None):
        if n is None:
            # compute cross product
            p0 = np.array(self.vertices[v0-1])
            p1 = np.array(self.vertices[v1-1])
            p2 = np.array(self.vertices[v2-1])
            cp = np.cross(p1 - p0, p2 - p0)
            n_idx = self.add_normal(cp[0], cp[1], cp[2])
            n0 = n1 = n2 = n_idx
        elif isinstance(n, int):
            n0 = n1 = n2 = n
        else:
            n0, n1, n2 = n
        self.faces.append((self.current_mtl, [(v0, vt0, n0), (v1, vt1, n1), (v2, vt2, n2)]))

    def add_quad(self, v0, v1, v2, v3, vt0, vt1, vt2, vt3, n=None):
        # Triangulate quad into two triangles (0, 1, 2) and (0, 2, 3)
        self.add_triangle(v0, v1, v2, vt0, vt1, vt2, n)
        self.add_triangle(v0, v2, v3, vt0, vt2, vt3, n)

    def add_box(self, center, size, u_scale=1.0, v_scale=1.0):
        cx, cy, cz = center
        sx, sy, sz = size[0]*0.5, size[1]*0.5, size[2]*0.5

        # 8 corners
        p = [
            (cx - sx, cy - sy, cz + sz), # 0: Front-Bottom-Left
            (cx + sx, cy - sy, cz + sz), # 1: Front-Bottom-Right
            (cx + sx, cy + sy, cz + sz), # 2: Front-Top-Right
            (cx - sx, cy + sy, cz + sz), # 3: Front-Top-Left
            (cx + sx, cy - sy, cz - sz), # 4: Back-Bottom-Right
            (cx - sx, cy - sy, cz - sz), # 5: Back-Bottom-Left
            (cx - sx, cy + sy, cz - sz), # 6: Back-Top-Left
            (cx + sx, cy + sy, cz - sz), # 7: Back-Top-Right
        ]
        vi = [self.add_vertex(pt[0], pt[1], pt[2]) for pt in p]

        # Standard UVs
        t0 = self.add_uv(0.0, 0.0)
        t1 = self.add_uv(u_scale, 0.0)
        t2 = self.add_uv(u_scale, v_scale)
        t3 = self.add_uv(0.0, v_scale)

        # Normals
        n_front = self.add_normal(0, 0, 1)
        n_back  = self.add_normal(0, 0, -1)
        n_left  = self.add_normal(-1, 0, 0)
        n_right = self.add_normal(1, 0, 0)
        n_top   = self.add_normal(0, 1, 0)
        n_bot   = self.add_normal(0, -1, 0)

        # Faces
        self.add_quad(vi[0], vi[1], vi[2], vi[3], t0, t1, t2, t3, n_front) # Front
        self.add_quad(vi[4], vi[5], vi[6], vi[7], t0, t1, t2, t3, n_back)  # Back
        self.add_quad(vi[5], vi[0], vi[3], vi[6], t0, t1, t2, t3, n_left)  # Left
        self.add_quad(vi[1], vi[4], vi[7], vi[2], t0, t1, t2, t3, n_right) # Right
        self.add_quad(vi[3], vi[2], vi[7], vi[6], t0, t1, t2, t3, n_top)   # Top
        self.add_quad(vi[5], vi[4], vi[1], vi[0], t0, t1, t2, t3, n_bot)   # Bottom

    def add_cylinder(self, p1, p2, r1, r2, sides=12, u_scale=1.0, v_scale=1.0):
        # Cylinder from p1 to p2
        p1 = np.array(p1, dtype=np.float32)
        p2 = np.array(p2, dtype=np.float32)
        axis = p2 - p1
        length = np.linalg.norm(axis)
        if length < 1e-5:
            return
        z_dir = axis / length

        # Arbitrary perpendicular vector
        ref = np.array([0, 1, 0], dtype=np.float32) if abs(z_dir[1]) < 0.9 else np.array([1, 0, 0], dtype=np.float32)
        x_dir = np.cross(z_dir, ref)
        x_dir /= np.linalg.norm(x_dir)
        y_dir = np.cross(x_dir, z_dir)

        ring1_v = []
        ring2_v = []
        ring_n = []
        uvs1 = []
        uvs2 = []

        for i in range(sides + 1):
            theta = 2.0 * np.pi * i / sides
            c, s = np.cos(theta), np.sin(theta)
            n_vec = c * x_dir + s * y_dir
            ring_n.append(self.add_normal(n_vec[0], n_vec[1], n_vec[2]))

            pt1 = p1 + n_vec * r1
            pt2 = p2 + n_vec * r2
            ring1_v.append(self.add_vertex(pt1[0], pt1[1], pt1[2]))
            ring2_v.append(self.add_vertex(pt2[0], pt2[1], pt2[2]))

            u = (i / float(sides)) * u_scale
            uvs1.append(self.add_uv(u, 0.0))
            uvs2.append(self.add_uv(u, v_scale))

        for i in range(sides):
            v0 = ring1_v[i]
            v1 = ring1_v[i+1]
            v2 = ring2_v[i+1]
            v3 = ring2_v[i]

            vt0 = uvs1[i]
            vt1 = uvs1[i+1]
            vt2 = uvs2[i+1]
            vt3 = uvs2[i]

            n0 = ring_n[i]
            n1 = ring_n[i+1]
            n2 = ring_n[i+1]
            n3 = ring_n[i]

            self.add_triangle(v0, v1, v2, vt0, vt1, vt2, (n0, n1, n2))
            self.add_triangle(v0, v2, v3, vt0, vt2, vt3, (n0, n2, n3))

    def write_obj(self, filepath):
        os.makedirs(os.path.dirname(filepath), exist_ok=True)
        mtl_path = os.path.splitext(filepath)[0] + ".mtl"
        mtl_name = os.path.basename(mtl_path)

        # Write MTL
        with open(mtl_path, "w", encoding="utf-8") as f:
            for name, mat in self.materials.items():
                f.write(f"newmtl {name}\n")
                f.write(f"Ka 0.2 0.2 0.2\n")
                kd = mat.get("Kd", (1,1,1))
                f.write(f"Kd {kd[0]:.4f} {kd[1]:.4f} {kd[2]:.4f}\n")
                f.write(f"Ks 0.1 0.1 0.1\n")
                f.write(f"Ns 16.0\n")
                f.write(f"d {mat.get('d', 1.0):.4f}\n")
                f.write(f"illum 2\n")
                if mat.get("map_Kd"):
                    f.write(f"map_Kd {mat['map_Kd']}\n")
                if mat.get("map_Bump"):
                    f.write(f"map_Bump {mat['map_Bump']}\n")
                f.write("\n")

        # Write OBJ
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(f"mtllib {mtl_name}\n")
            for v in self.vertices:
                f.write(f"v {v[0]:.4f} {v[1]:.4f} {v[2]:.4f}\n")
            for vt in self.texcoords:
                f.write(f"vt {vt[0]:.4f} {vt[1]:.4f}\n")
            for vn in self.normals:
                f.write(f"vn {vn[0]:.4f} {vn[1]:.4f} {vn[2]:.4f}\n")

            current_m = None
            for mtl, face in self.faces:
                if mtl != current_m:
                    f.write(f"usemtl {mtl}\n")
                    current_m = mtl
                f.write("f " + " ".join(f"{idx[0]}/{idx[1]}/{idx[2]}" for idx in face) + "\n")
        print(f"Exported {filepath}: {len(self.vertices):,} verts, {len(self.faces):,} faces.")


# =============================================================================
# 1. BUILD HISTORIC MUSIC KIOSK (assets/moniment/kiosque_musique.obj)
# =============================================================================
def build_kiosque():
    mesh = ObjMesh("kiosque_musique.mtl")
    mesh.add_material("StoneBase", kd=(0.9, 0.88, 0.85), map_kd="textures/stone_blocks.png")
    mesh.add_material("FloorWood", kd=(0.7, 0.6, 0.45), map_kd="textures/wood_shutter.png")
    mesh.add_material("IronMetal", kd=(0.2, 0.22, 0.25), map_kd="textures/wrought_iron.png")
    mesh.add_material("ZincRoof",  kd=(0.8, 0.9, 0.85), map_kd="textures/kiosk_roof.png")

    R_base = 4.2
    H_base = 0.90
    sides = 8

    # 1. Octagonal Stone Plinth Base
    mesh.set_material("StoneBase")
    base_pts_bot = []
    base_pts_top = []
    uvs_bot = []
    uvs_top = []

    for i in range(sides + 1):
        angle = 2.0 * math.pi * i / sides + math.pi / 8.0
        x = R_base * math.cos(angle)
        z = R_base * math.sin(angle)
        base_pts_bot.append(mesh.add_vertex(x, 0.0, z))
        base_pts_top.append(mesh.add_vertex(x, H_base, z))
        u = i * 1.0
        uvs_bot.append(mesh.add_uv(u, 0.0))
        uvs_top.append(mesh.add_uv(u, 1.0))

    # Outer wall quads
    for i in range(sides):
        mesh.add_quad(
            base_pts_bot[i], base_pts_bot[i+1], base_pts_top[i+1], base_pts_top[i],
            uvs_bot[i], uvs_bot[i+1], uvs_top[i+1], uvs_top[i]
        )

    # 2. Wooden/Tile Floor
    mesh.set_material("FloorWood")
    c_top = mesh.add_vertex(0.0, H_base, 0.0)
    c_uv = mesh.add_uv(0.5, 0.5)
    n_up = mesh.add_normal(0, 1, 0)
    for i in range(sides):
        angle0 = 2.0 * math.pi * i / sides + math.pi / 8.0
        angle1 = 2.0 * math.pi * (i+1) / sides + math.pi / 8.0
        uv0 = mesh.add_uv(0.5 + 0.5 * math.cos(angle0), 0.5 + 0.5 * math.sin(angle0))
        uv1 = mesh.add_uv(0.5 + 0.5 * math.cos(angle1), 0.5 + 0.5 * math.sin(angle1))
        mesh.add_triangle(c_top, base_pts_top[i], base_pts_top[i+1], c_uv, uv0, uv1, n_up)

    # 3. Entrance Steps (Facing negative Z / Promenade)
    mesh.set_material("StoneBase")
    num_steps = 4
    step_w = 2.2
    for s in range(num_steps):
        sy = H_base * (s / float(num_steps))
        sh = H_base / float(num_steps)
        sz = -R_base - (num_steps - s) * 0.32
        mesh.add_box((0.0, sy + sh*0.5, sz + 0.16), (step_w, sh, 0.35), u_scale=1.0, v_scale=0.5)

    # 4. Eight Slender Wrought Iron Columns
    mesh.set_material("IronMetal")
    R_col = 3.65
    H_col = 3.20
    col_tops = []

    for i in range(sides):
        angle = 2.0 * math.pi * i / sides + math.pi / 8.0
        cx = R_col * math.cos(angle)
        cz = R_col * math.sin(angle)

        # Base block
        mesh.add_box((cx, H_base + 0.15, cz), (0.28, 0.30, 0.28), 0.5, 0.5)
        # Column shaft
        p_bot = (cx, H_base + 0.30, cz)
        p_top = (cx, H_base + H_col - 0.25, cz)
        mesh.add_cylinder(p_bot, p_top, 0.07, 0.07, sides=8, u_scale=1.0, v_scale=3.0)
        # Capital
        mesh.add_box((cx, H_base + H_col - 0.12, cz), (0.32, 0.25, 0.32), 0.5, 0.5)
        col_tops.append((cx, H_base + H_col, cz))

    # 5. Wrought-Iron Railings (between columns, skipping entrance facing -Z)
    for i in range(sides):
        angle_mid = 2.0 * math.pi * (i + 0.5) / sides + math.pi / 8.0
        # If facing entrance (-Z side), leave opening for stairs
        if -0.95 < math.sin(angle_mid) < -0.70:
            continue

        c1 = col_tops[i]
        c2 = col_tops[(i+1)%sides]
        mid_x = (c1[0] + c2[0]) * 0.5
        mid_z = (c1[2] + c2[2]) * 0.5

        # Top rail
        mesh.add_cylinder((c1[0], H_base + 0.95, c1[2]), (c2[0], H_base + 0.95, c2[2]), 0.035, 0.035, sides=6)
        # Bottom rail
        mesh.add_cylinder((c1[0], H_base + 0.12, c1[2]), (c2[0], H_base + 0.12, c2[2]), 0.03, 0.03, sides=6)

        # Vertical balusters
        for b in range(1, 6):
            t = b / 6.0
            bx = c1[0] * (1.0 - t) + c2[0] * t
            bz = c1[2] * (1.0 - t) + c2[2] * t
            mesh.add_cylinder((bx, H_base + 0.12, bz), (bx, H_base + 0.95, bz), 0.018, 0.018, sides=6)

    # 6. Roof Ring Beam / Entablature
    mesh.set_material("IronMetal")
    for i in range(sides):
        c1 = col_tops[i]
        c2 = col_tops[(i+1)%sides]
        mesh.add_cylinder((c1[0], c1[1], c1[2]), (c2[0], c2[1], c2[2]), 0.09, 0.09, sides=8)

    # 7. Two-Tiered Zinc Pavilion Roof
    mesh.set_material("ZincRoof")
    Y_roof_base = H_base + H_col
    R_eaves = 4.60
    Y_eaves = Y_roof_base + 0.15
    R_tier1 = 1.90
    Y_tier1 = Y_roof_base + 1.65

    # Eaves ring
    eaves_pts = []
    tier1_pts = []
    uvs_e = []
    uvs_t1 = []

    for i in range(sides + 1):
        angle = 2.0 * math.pi * i / sides + math.pi / 8.0
        ex = R_eaves * math.cos(angle)
        ez = R_eaves * math.sin(angle)
        eaves_pts.append(mesh.add_vertex(ex, Y_eaves, ez))

        t1x = R_tier1 * math.cos(angle)
        t1z = R_tier1 * math.sin(angle)
        tier1_pts.append(mesh.add_vertex(t1x, Y_tier1, t1z))

        u = i / float(sides)
        uvs_e.append(mesh.add_uv(u, 0.0))
        uvs_t1.append(mesh.add_uv(u, 0.6))

    # Lower roof sloped panels
    for i in range(sides):
        mesh.add_quad(
            eaves_pts[i], eaves_pts[i+1], tier1_pts[i+1], tier1_pts[i],
            uvs_e[i], uvs_e[i+1], uvs_t1[i+1], uvs_t1[i]
        )

    # Clerestory Drum
    mesh.set_material("IronMetal")
    Y_drum_top = Y_tier1 + 0.40
    drum_top_pts = []
    uvs_d_top = []
    for i in range(sides + 1):
        angle = 2.0 * math.pi * i / sides + math.pi / 8.0
        dtx = (R_tier1 * 0.95) * math.cos(angle)
        dtz = (R_tier1 * 0.95) * math.sin(angle)
        drum_top_pts.append(mesh.add_vertex(dtx, Y_drum_top, dtz))
        uvs_d_top.append(mesh.add_uv(i / float(sides), 1.0))

    for i in range(sides):
        mesh.add_quad(
            tier1_pts[i], tier1_pts[i+1], drum_top_pts[i+1], drum_top_pts[i],
            uvs_t1[i], uvs_t1[i+1], uvs_d_top[i+1], uvs_d_top[i]
        )

    # Upper Cupola Roof Dome
    mesh.set_material("ZincRoof")
    Y_apex = Y_drum_top + 1.25
    v_apex = mesh.add_vertex(0.0, Y_apex, 0.0)
    uv_apex = mesh.add_uv(0.5, 1.0)
    for i in range(sides):
        mesh.add_triangle(
            drum_top_pts[i], drum_top_pts[i+1], v_apex,
            uvs_d_top[i], uvs_d_top[i+1], uv_apex
        )

    # Ornamental Crown Spire / Finial
    mesh.set_material("IronMetal")
    mesh.add_cylinder((0.0, Y_apex, 0.0), (0.0, Y_apex + 0.85, 0.0), 0.04, 0.015, sides=6)
    # Finial ball
    mesh.add_box((0.0, Y_apex + 0.45, 0.0), (0.16, 0.16, 0.16), 0.5, 0.5)

    mesh.write_obj("assets/moniment/kiosque_musique.obj")


# =============================================================================
# 2. BUILD MEDITERRANEAN DATE PALM TREE (assets/moniment/date_palm.obj)
# =============================================================================
def build_date_palm():
    mesh = ObjMesh("date_palm.mtl")
    mesh.add_material("PalmTrunk", kd=(0.85, 0.75, 0.65), map_kd="textures/palm_bark.png")
    mesh.add_material("PalmFrond", kd=(1.0, 1.0, 1.0), map_kd="textures/palm_frond.png", d=0.99)

    mesh.set_material("PalmTrunk")

    # Trunk Parameters
    trunk_rings = 18
    trunk_height = 9.0
    ring_h = trunk_height / float(trunk_rings)
    sides = 10

    # Organic subtle lean
    ring_centers = []
    ring_radii = []

    for i in range(trunk_rings + 1):
        t = i / float(trunk_rings)
        # Gentle natural curvature
        cx = math.sin(t * math.pi * 0.85) * 0.45
        cz = (1.0 - math.cos(t * math.pi * 0.6)) * 0.25
        cy = t * trunk_height

        # Radius: thick at base, slender waist, wider crown collar
        r = 0.32 * (1.0 - t * 0.40) + 0.08 * (t ** 2)
        ring_centers.append((cx, cy, cz))
        ring_radii.append(r)

    # Build Trunk Rings
    ring_verts = []
    ring_uvs = []
    for r_idx in range(trunk_rings + 1):
        cx, cy, cz = ring_centers[r_idx]
        radius = ring_radii[r_idx]
        v_row = []
        uv_row = []
        for s in range(sides + 1):
            theta = 2.0 * math.pi * s / float(sides)
            vx = cx + radius * math.cos(theta)
            vz = cz + radius * math.sin(theta)
            v_row.append(mesh.add_vertex(vx, cy, vz))
            uv_row.append(mesh.add_uv(s / float(sides) * 2.0, r_idx * 0.6))
        ring_verts.append(v_row)
        ring_uvs.append(uv_row)

    for r_idx in range(trunk_rings):
        for s in range(sides):
            v0 = ring_verts[r_idx][s]
            v1 = ring_verts[r_idx][s+1]
            v2 = ring_verts[r_idx+1][s+1]
            v3 = ring_verts[r_idx+1][s]

            vt0 = ring_uvs[r_idx][s]
            vt1 = ring_uvs[r_idx][s+1]
            vt2 = ring_uvs[r_idx+1][s+1]
            vt3 = ring_uvs[r_idx+1][s]

            mesh.add_quad(v0, v1, v2, v3, vt0, vt1, vt2, vt3)

    # Fronds Canopy
    mesh.set_material("PalmFrond")
    crown_x, crown_y, crown_z = ring_centers[-1]

    # 3 Tiers of fronds
    tiers = [
        {"count": 8,  "pitch_start": 55, "pitch_end": 15, "length": 3.6, "width": 0.85},  # Upper tier
        {"count": 10, "pitch_start": 35, "pitch_end": -10, "length": 4.2, "width": 0.95}, # Middle tier
        {"count": 10, "pitch_start": 10, "pitch_end": -45, "length": 4.0, "width": 0.90}, # Lower drooping tier
    ]

    for tier_idx, tier in enumerate(tiers):
        count = tier["count"]
        p_start = math.radians(tier["pitch_start"])
        p_end   = math.radians(tier["pitch_end"])
        frond_len = tier["length"]
        frond_w   = tier["width"]

        for i in range(count):
            # Golden ratio azimuth distribution
            yaw = 2.0 * math.pi * (i / float(count)) + tier_idx * 0.38
            segments = 6

            # Build frond ribbon curve
            spine_pts = []
            for s in range(segments + 1):
                st = s / float(segments)
                # Interpolate pitch along curve (starts upward, arches gracefully downward)
                pitch = p_start * (1.0 - st) + p_end * (st ** 1.3)

                dist_horiz = frond_len * st * math.cos(pitch)
                dist_vert  = frond_len * st * math.sin(pitch)

                fx = crown_x + dist_horiz * math.cos(yaw)
                fz = crown_z + dist_horiz * math.sin(yaw)
                fy = crown_y + dist_vert

                spine_pts.append((fx, fy, fz))

            # Ribbon lateral vector (perpendicular to spine in horizontal plane)
            perp_x = -math.sin(yaw)
            perp_z = math.cos(yaw)

            # Generate quad strips for frond
            v_left = []
            v_right = []
            uv_left = []
            uv_right = []

            for s in range(segments + 1):
                st = s / float(segments)
                fx, fy, fz = spine_pts[s]
                # Frond width profile: narrow base, wide center, tapers to tip
                w_profile = math.sin(st * math.pi) ** 0.65 * (frond_w * 0.5)

                v_left.append(mesh.add_vertex(fx - perp_x * w_profile, fy, fz - perp_z * w_profile))
                v_right.append(mesh.add_vertex(fx + perp_x * w_profile, fy, fz + perp_z * w_profile))

                uv_left.append(mesh.add_uv(0.0, st))
                uv_right.append(mesh.add_uv(1.0, st))

            # Double-sided frond quads so they are visible from above and below
            for s in range(segments):
                # Front face
                mesh.add_quad(
                    v_left[s], v_right[s], v_right[s+1], v_left[s+1],
                    uv_left[s], uv_right[s], uv_right[s+1], uv_left[s+1]
                )
                # Back face
                mesh.add_quad(
                    v_right[s], v_left[s], v_left[s+1], v_right[s+1],
                    uv_right[s], uv_left[s], uv_left[s+1], uv_right[s+1]
                )

    mesh.write_obj("assets/moniment/date_palm.obj")


# =============================================================================
# 3. BUILD COLONIAL WATERFRONT BUILDING (assets/moniment/colonial_hotel_ville.obj)
# =============================================================================
def build_colonial_building():
    mesh = ObjMesh("colonial_hotel_ville.mtl")
    mesh.add_material("PlasterWall", kd=(0.95, 0.93, 0.88), map_kd="textures/cream_plaster.png")
    mesh.add_material("AshlarStone", kd=(0.88, 0.85, 0.80), map_kd="textures/stone_blocks.png")
    mesh.add_material("RoofTiles",   kd=(0.85, 0.50, 0.35), map_kd="textures/roof_tile_red.png")
    mesh.add_material("WoodShutters",kd=(0.40, 0.55, 0.50), map_kd="textures/wood_shutter.png")
    mesh.add_material("IronRailing", kd=(0.20, 0.22, 0.25), map_kd="textures/wrought_iron.png")

    W = 24.0 # Width along street
    D = 14.0 # Depth
    H_g = 5.0 # Ground floor height
    H_f = 4.4 # First floor height
    H_total = H_g + H_f

    # 1. Main Building Mass (Plaster Walls)
    mesh.set_material("PlasterWall")
    # Centered at (0, H_total*0.5, 0), so back is at -D/2 and front facade is at +D/2
    mesh.add_box((0.0, H_total * 0.5, 0.0), (W, H_total, D), u_scale=6.0, v_scale=3.0)

    # 2. Ground Floor Moorish Arcades (5 Horseshoe Arches on Front Facade)
    num_arches = 5
    arch_span = W / float(num_arches)
    pier_w = 0.70
    arch_w = arch_span - pier_w

    mesh.set_material("AshlarStone")

    # Base water plinth along front facade
    mesh.add_box((0.0, 0.40, D * 0.5 + 0.15), (W + 0.4, 0.80, 0.35), u_scale=8.0, v_scale=0.5)

    # Quoins on building corners
    mesh.add_box((-W*0.5 + 0.35, H_total*0.5, D*0.5 + 0.05), (0.75, H_total, 0.25), u_scale=1.0, v_scale=6.0)
    mesh.add_box(( W*0.5 - 0.35, H_total*0.5, D*0.5 + 0.05), (0.75, H_total, 0.25), u_scale=1.0, v_scale=6.0)

    # Arcaded Piers
    for i in range(num_arches + 1):
        px = -W * 0.5 + i * arch_span
        # Pier column
        mesh.add_box((px, H_g * 0.42, D * 0.5 + 0.20), (pier_w, H_g * 0.75, 0.45), u_scale=1.0, v_scale=2.5)
        # Pier capital
        mesh.add_box((px, H_g * 0.82, D * 0.5 + 0.22), (pier_w + 0.15, 0.25, 0.50), u_scale=1.0, v_scale=0.5)

    # Horseshoe Arch Spandrels
    for i in range(num_arches):
        x_center = -W * 0.5 + (i + 0.5) * arch_span
        # Spandrel panel above arch
        mesh.add_box((x_center, H_g - 0.40, D * 0.5 + 0.18), (arch_w, 0.85, 0.40), u_scale=2.0, v_scale=0.8)

    # Recessed Front Entrance Doors & Windows behind arcade
    mesh.set_material("WoodShutters")
    for i in range(num_arches):
        x_center = -W * 0.5 + (i + 0.5) * arch_span
        if i == 2: # Center entrance double doors
            mesh.add_box((x_center, 1.70, D * 0.5 - 0.02), (2.0, 3.4, 0.15), u_scale=1.0, v_scale=1.5)
        else: # Arched windows
            mesh.add_box((x_center, 1.80, D * 0.5 - 0.02), (1.6, 2.6, 0.15), u_scale=1.0, v_scale=1.5)

    # 3. Terracotta Tile Awning / Cornice between Ground & First Floor
    mesh.set_material("RoofTiles")
    awning_pts_bot = [(-W*0.5 - 0.2, H_g, D*0.5 + 0.0), (W*0.5 + 0.2, H_g, D*0.5 + 0.0)]
    # Slopes down and out
    awning_pts_top = [(-W*0.5 - 0.2, H_g - 0.45, D*0.5 + 0.90), (W*0.5 + 0.2, H_g - 0.45, D*0.5 + 0.90)]
    mesh.add_box((0.0, H_g - 0.22, D*0.5 + 0.45), (W + 0.5, 0.25, 0.95), u_scale=12.0, v_scale=1.5)

    # 4. First Floor: Symmetrical French Windows with Balconies
    num_windows = 7
    win_spacing = W / float(num_windows + 1)

    mesh.set_material("AshlarStone")
    # Continuous grand balcony slab
    mesh.add_box((0.0, H_g + 0.15, D * 0.5 + 0.65), (W - 1.2, 0.30, 1.30), u_scale=10.0, v_scale=0.5)

    # Balcony supporting corbels
    for i in range(num_windows + 2):
        cx = -W*0.5 + 1.2 + i * ((W - 2.4) / float(num_windows + 1))
        mesh.add_box((cx, H_g - 0.20, D * 0.5 + 0.45), (0.35, 0.45, 0.90), u_scale=0.5, v_scale=0.5)

    # Wrought-iron balcony railing
    mesh.set_material("IronRailing")
    rail_y = H_g + 0.30 + 0.50
    # Top rail
    mesh.add_cylinder((-W*0.5 + 0.6, rail_y, D*0.5 + 1.25), (W*0.5 - 0.6, rail_y, D*0.5 + 1.25), 0.03, 0.03, sides=6)
    # Bottom rail
    mesh.add_cylinder((-W*0.5 + 0.6, H_g + 0.35, D*0.5 + 1.25), (W*0.5 - 0.6, H_g + 0.35, D*0.5 + 1.25), 0.025, 0.025, sides=6)
    # Balusters
    for b in range(45):
        t = b / 44.0
        bx = (-W*0.5 + 0.6) * (1.0 - t) + (W*0.5 - 0.6) * t
        mesh.add_cylinder((bx, H_g + 0.35, D*0.5 + 1.25), (bx, rail_y, D*0.5 + 1.25), 0.015, 0.015, sides=6)

    # French Windows with Shutters
    mesh.set_material("WoodShutters")
    for i in range(num_windows):
        wx = -W*0.5 + (i + 1) * win_spacing
        # Window pane
        mesh.add_box((wx, H_g + 1.85, D*0.5 + 0.02), (1.4, 2.6, 0.10), u_scale=1.0, v_scale=1.5)
        # Left open shutter
        mesh.add_box((wx - 0.95, H_g + 1.85, D*0.5 + 0.08), (0.50, 2.5, 0.06), u_scale=0.5, v_scale=1.5)
        # Right open shutter
        mesh.add_box((wx + 0.95, H_g + 1.85, D*0.5 + 0.08), (0.50, 2.5, 0.06), u_scale=0.5, v_scale=1.5)

    # 5. Roofline Cornice & North African Stepped Merlons (Crenellations)
    mesh.set_material("AshlarStone")
    # Projecting classical cornice
    mesh.add_box((0.0, H_total + 0.20, D*0.5 + 0.20), (W + 0.6, 0.40, 0.65), u_scale=10.0, v_scale=0.5)

    # Parapet base wall
    mesh.add_box((0.0, H_total + 0.65, D*0.5 + 0.15), (W + 0.4, 0.50, 0.35), u_scale=8.0, v_scale=0.5)

    # Stepped Merlons (Crenellations) along the front parapet
    num_merlons = 15
    merlon_spacing = W / float(num_merlons)
    for i in range(num_merlons):
        mx = -W*0.5 + (i + 0.5) * merlon_spacing
        # Bottom step of merlon
        mesh.add_box((mx, H_total + 1.05, D*0.5 + 0.15), (0.90, 0.30, 0.30), u_scale=0.5, v_scale=0.5)
        # Middle step
        mesh.add_box((mx, H_total + 1.30, D*0.5 + 0.15), (0.60, 0.25, 0.28), u_scale=0.4, v_scale=0.4)
        # Top pyramid cap
        mesh.add_box((mx, H_total + 1.50, D*0.5 + 0.15), (0.30, 0.20, 0.26), u_scale=0.3, v_scale=0.3)

    mesh.write_obj("assets/moniment/colonial_hotel_ville.obj")


if __name__ == "__main__":
    print("[*] Building high quality historical Sousse 3D models...")
    build_kiosque()
    build_date_palm()
    build_colonial_building()
    print("[+] All 3 models built successfully!")
