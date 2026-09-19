import os
import math

OUT_DIR = "assets/moniment"
os.makedirs(OUT_DIR, exist_ok=True)

class ObjWriter:
    def __init__(self, obj_path, mtl_path, mtl_filename):
        self.obj_file = open(obj_path, "w", encoding="utf-8")
        self.mtl_file = open(mtl_path, "w", encoding="utf-8")
        self.mtl_filename = mtl_filename
        self.verts = []
        self.norms = []
        self.uvs = []
        self.current_mat = None

        self.obj_file.write(f"mtllib {mtl_filename}\n")

    def add_material(self, name, diff_tex, kd=(1.0, 1.0, 1.0), d=1.0):
        self.mtl_file.write(f"newmtl {name}\n")
        self.mtl_file.write(f"Kd {kd[0]:.3f} {kd[1]:.3f} {kd[2]:.3f}\n")
        self.mtl_file.write("Ka 0.200 0.200 0.200\n")
        self.mtl_file.write("Ks 0.100 0.100 0.100\n")
        self.mtl_file.write("Ns 10.0\n")
        self.mtl_file.write(f"d {d:.2f}\n")
        self.mtl_file.write(f"map_Kd {diff_tex}\n\n")

    def use_material(self, name):
        if self.current_mat != name:
            self.obj_file.write(f"usemtl {name}\n")
            self.current_mat = name

    def add_v(self, x, y, z):
        self.verts.append((x, y, z))
        self.obj_file.write(f"v {x:.4f} {y:.4f} {z:.4f}\n")
        return len(self.verts)

    def add_vn(self, x, y, z):
        self.norms.append((x, y, z))
        self.obj_file.write(f"vn {x:.4f} {y:.4f} {z:.4f}\n")
        return len(self.norms)

    def add_vt(self, u, v):
        self.uvs.append((u, v))
        self.obj_file.write(f"vt {u:.4f} {v:.4f}\n")
        return len(self.uvs)

    def add_quad(self, v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn):
        # 2 triangles (v1, v2, v3) and (v1, v3, v4)
        self.obj_file.write(f"f {v1}/{vt1}/{vn} {v2}/{vt2}/{vn} {v3}/{vt3}/{vn}\n")
        self.obj_file.write(f"f {v1}/{vt1}/{vn} {v3}/{vt3}/{vn} {v4}/{vt4}/{vn}\n")

    def add_tri(self, v1, v2, v3, vt1, vt2, vt3, vn):
        self.obj_file.write(f"f {v1}/{vt1}/{vn} {v2}/{vt2}/{vn} {v3}/{vt3}/{vn}\n")

    def add_box(self, x_min, y_min, z_min, x_max, y_max, z_max, u_scale=1.0, v_scale=1.0):
        # Front (+Z)
        vn_f = self.add_vn(0, 0, 1)
        w = (x_max - x_min) * u_scale
        h = (y_max - y_min) * v_scale
        v1 = self.add_v(x_min, y_min, z_max); vt1 = self.add_vt(0, 0)
        v2 = self.add_v(x_max, y_min, z_max); vt2 = self.add_vt(w, 0)
        v3 = self.add_v(x_max, y_max, z_max); vt3 = self.add_vt(w, h)
        v4 = self.add_v(x_min, y_max, z_max); vt4 = self.add_vt(0, h)
        self.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_f)

        # Back (-Z)
        vn_b = self.add_vn(0, 0, -1)
        v5 = self.add_v(x_max, y_min, z_min); vt5 = self.add_vt(0, 0)
        v6 = self.add_v(x_min, y_min, z_min); vt6 = self.add_vt(w, 0)
        v7 = self.add_v(x_min, y_max, z_min); vt7 = self.add_vt(w, h)
        v8 = self.add_v(x_max, y_max, z_min); vt8 = self.add_vt(0, h)
        self.add_quad(v5, v6, v7, v8, vt5, vt6, vt7, vt8, vn_b)

        # Left (-X)
        d = (z_max - z_min) * u_scale
        vn_l = self.add_vn(-1, 0, 0)
        v9 = self.add_v(x_min, y_min, z_min); vt9 = self.add_vt(0, 0)
        v10 = self.add_v(x_min, y_min, z_max); vt10 = self.add_vt(d, 0)
        v11 = self.add_v(x_min, y_max, z_max); vt11 = self.add_vt(d, h)
        v12 = self.add_v(x_min, y_max, z_min); vt12 = self.add_vt(0, h)
        self.add_quad(v9, v10, v11, v12, vt9, vt10, vt11, vt12, vn_l)

        # Right (+X)
        vn_r = self.add_vn(1, 0, 0)
        v13 = self.add_v(x_max, y_min, z_max); vt13 = self.add_vt(0, 0)
        v14 = self.add_v(x_max, y_min, z_min); vt14 = self.add_vt(d, 0)
        v15 = self.add_v(x_max, y_max, z_min); vt15 = self.add_vt(d, h)
        v16 = self.add_v(x_max, y_max, z_max); vt16 = self.add_vt(0, h)
        self.add_quad(v13, v14, v15, v16, vt13, vt14, vt15, vt16, vn_r)

        # Top (+Y)
        vn_t = self.add_vn(0, 1, 0)
        v17 = self.add_v(x_min, y_max, z_max); vt17 = self.add_vt(0, 0)
        v18 = self.add_v(x_max, y_max, z_max); vt18 = self.add_vt(w, 0)
        v19 = self.add_v(x_max, y_max, z_min); vt19 = self.add_vt(w, d)
        v20 = self.add_v(x_min, y_max, z_min); vt20 = self.add_vt(0, d)
        self.add_quad(v17, v18, v19, v20, vt17, vt18, vt19, vt20, vn_t)

        # Bottom (-Y)
        vn_bt = self.add_vn(0, -1, 0)
        v21 = self.add_v(x_min, y_min, z_min); vt21 = self.add_vt(0, 0)
        v22 = self.add_v(x_max, y_min, z_min); vt22 = self.add_vt(w, 0)
        v23 = self.add_v(x_max, y_min, z_max); vt23 = self.add_vt(w, d)
        v24 = self.add_v(x_min, y_min, z_max); vt24 = self.add_vt(0, d)
        self.add_quad(v21, v22, v23, v24, vt21, vt22, vt23, vt24, vn_bt)

    def close(self):
        self.obj_file.close()
        self.mtl_file.close()


# ==============================================================================
# MODEL 1: LA DOUANE / CAPITAINERIE (NEO-MOORISH CORNER LANDMARK)
# ==============================================================================
def build_douane_neomoorish():
    obj_p = os.path.join(OUT_DIR, "bldg_douane_neomoorish.obj")
    mtl_p = os.path.join(OUT_DIR, "bldg_douane_neomoorish.mtl")
    w = ObjWriter(obj_p, mtl_p, "bldg_douane_neomoorish.mtl")

    w.add_material("buff_stone", "textures/buff_stone.png")
    w.add_material("cream_plaster", "textures/cream_plaster.png")
    w.add_material("wrought_iron", "textures/wrought_iron.png")
    w.add_material("wood_shutter", "textures/wood_shutter.png")

    W, D, H = 28.0, 20.0, 10.5
    half_w, half_d = W / 2.0, D / 2.0

    # 1. Main building block
    w.use_material("buff_stone")
    w.add_box(-half_w, 0.0, -half_d, half_w, H, half_d, u_scale=0.15, v_scale=0.15)

    # 2. Projecting Monumental Moorish Portal (Center Front: X in [-4.5, 4.5], Z projecting to half_d + 1.2, H = 12.0)
    w.use_material("cream_plaster")
    w.add_box(-4.5, 0.0, half_d, 4.5, 12.0, half_d + 1.2, u_scale=0.2, v_scale=0.2)

    # Recessed Portal Door
    w.use_material("wood_shutter")
    w.add_box(-1.8, 0.0, half_d + 0.8, 1.8, 4.2, half_d + 1.22, u_scale=0.3, v_scale=0.3)

    # Moorish Horseshoe Arch Voussoir surround above portal
    w.use_material("buff_stone")
    # Left & Right pilasters of portal
    w.add_box(-4.6, 0.0, half_d + 1.15, -3.2, 12.2, half_d + 1.45, u_scale=0.2, v_scale=0.2)
    w.add_box(3.2, 0.0, half_d + 1.15, 4.6, 12.2, half_d + 1.45, u_scale=0.2, v_scale=0.2)
    w.add_box(-4.6, 11.5, half_d + 1.15, 4.6, 12.5, half_d + 1.45, u_scale=0.2, v_scale=0.2)

    # 3. Flanking Horseshoe Windows on Front Facade (4 windows)
    w.use_material("wood_shutter")
    win_xs = [-10.5, -6.5, 6.5, 10.5]
    for wx in win_xs:
        # Window opening recess
        w.add_box(wx - 1.1, 2.0, half_d - 0.1, wx + 1.1, 5.0, half_d + 0.12, u_scale=0.4, v_scale=0.4)

    # Iron security grilles on ground windows
    w.use_material("wrought_iron")
    for wx in win_xs:
        w.add_box(wx - 1.15, 1.9, half_d + 0.13, wx + 1.15, 5.1, half_d + 0.18, u_scale=1.0, v_scale=1.0)

    # 4. Stepped Moorish Merlon Crenellations (merlons en gradins) along front & sides
    w.use_material("cream_plaster")
    # Front parapet base
    w.add_box(-half_w - 0.2, H, half_d - 0.4, half_w + 0.2, H + 0.5, half_d + 0.4, u_scale=0.2, v_scale=0.2)
    # Merlons spaced every 1.6m
    merlon_w = 0.9
    for mx in [x * 1.6 for x in range(int(-half_w / 1.6), int(half_w / 1.6) + 1)]:
        if abs(mx) < 4.5: continue # Skip portal center
        # Step 1 (bottom)
        w.add_box(mx - merlon_w/2, H + 0.5, half_d - 0.3, mx + merlon_w/2, H + 1.0, half_d + 0.3, u_scale=0.3, v_scale=0.3)
        # Step 2 (middle)
        w.add_box(mx - merlon_w*0.35, H + 1.0, half_d - 0.25, mx + merlon_w*0.35, H + 1.4, half_d + 0.25, u_scale=0.3, v_scale=0.3)
        # Step 3 (top peak)
        w.add_box(mx - merlon_w*0.18, H + 1.4, half_d - 0.2, mx + merlon_w*0.18, H + 1.7, half_d + 0.2, u_scale=0.3, v_scale=0.3)

    # Merlons atop the Portal
    for px in [-3.0, -1.0, 1.0, 3.0]:
        w.add_box(px - 0.4, 12.0, half_d + 0.9, px + 0.4, 12.6, half_d + 1.3, u_scale=0.3, v_scale=0.3)
        w.add_box(px - 0.2, 12.6, half_d + 0.95, px + 0.2, 13.0, half_d + 1.25, u_scale=0.3, v_scale=0.3)

    # 5. South Wing Arched Loggia Arcade (X = +half_w flank)
    w.use_material("buff_stone")
    for lz in [-6.0, -1.0, 4.0]:
        w.add_box(half_w - 0.1, 0.0, lz - 1.2, half_w + 1.0, 5.5, lz + 1.2, u_scale=0.2, v_scale=0.2)

    w.close()
    print("Generated bldg_douane_neomoorish.obj")


# ==============================================================================
# MODEL 2: ANNEXE DOUANE / DEPOTS (WARM PEACH WITH RED AWNING)
# ==============================================================================
def build_douane_annexe():
    obj_p = os.path.join(OUT_DIR, "bldg_douane_annexe.obj")
    mtl_p = os.path.join(OUT_DIR, "bldg_douane_annexe.mtl")
    w = ObjWriter(obj_p, mtl_p, "bldg_douane_annexe.mtl")

    w.add_material("peach_plaster", "textures/peach_plaster.png")
    w.add_material("awning_striped", "textures/awning_striped.png")
    w.add_material("wood_shutter", "textures/wood_shutter.png")
    w.add_material("cream_plaster", "textures/cream_plaster.png")
    w.add_material("wrought_iron", "textures/wrought_iron.png")

    W, D, H = 24.0, 18.0, 9.5
    half_w, half_d = W / 2.0, D / 2.0

    # 1. Main structure in warm peach terracotta lime plaster
    w.use_material("peach_plaster")
    w.add_box(-half_w, 0.0, -half_d, half_w, H, half_d, u_scale=0.15, v_scale=0.15)

    # 2. Ground-floor Shaded Doors (5 bays)
    w.use_material("wood_shutter")
    bays = [-8.0, -4.0, 0.0, 4.0, 8.0]
    for bx in bays:
        w.add_box(bx - 1.2, 0.0, half_d - 0.1, bx + 1.2, 3.4, half_d + 0.15, u_scale=0.3, v_scale=0.3)

    # 3. Continuous Cantilevered Red Awning (marquise) projecting 2.5m over ground floor
    w.use_material("awning_striped")
    # Awning sloping plane: from (y=3.8, z=half_d) down to (y=2.8, z=half_d + 2.5)
    vn_aw = w.add_vn(0.0, 0.6, 0.8)
    v1 = w.add_v(-half_w, 3.8, half_d); vt1 = w.add_vt(0, 0)
    v2 = w.add_v(half_w, 3.8, half_d); vt2 = w.add_vt(12, 0)
    v3 = w.add_v(half_w, 2.8, half_d + 2.5); vt3 = w.add_vt(12, 2)
    v4 = w.add_v(-half_w, 2.8, half_d + 2.5); vt4 = w.add_vt(0, 2)
    w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_aw)
    # Underside of awning
    vn_aw_under = w.add_vn(0.0, -0.6, -0.8)
    w.add_quad(v4, v3, v2, v1, vt4, vt3, vt2, vt1, vn_aw_under)

    # Awning support iron brackets
    w.use_material("wrought_iron")
    for bx in [-11.0, -6.0, 0.0, 6.0, 11.0]:
        w.add_box(bx - 0.06, 2.5, half_d, bx + 0.06, 3.8, half_d + 2.4, u_scale=0.5, v_scale=0.5)

    # 4. Upper Floor French Windows (5 windows with white plaster surrounds & green shutters)
    w.use_material("cream_plaster")
    for bx in bays:
        # Molded surround
        w.add_box(bx - 1.4, 5.0, half_d, bx + 1.4, 8.2, half_d + 0.18, u_scale=0.3, v_scale=0.3)

    w.use_material("wood_shutter")
    for bx in bays:
        # Shutter panels
        w.add_box(bx - 1.1, 5.2, half_d + 0.15, bx + 1.1, 8.0, half_d + 0.25, u_scale=0.3, v_scale=0.3)

    # 5. Roof cornice and parapet
    w.use_material("cream_plaster")
    w.add_box(-half_w - 0.3, H - 0.4, -half_d - 0.3, half_w + 0.3, H + 0.6, half_d + 0.3, u_scale=0.2, v_scale=0.2)

    w.close()
    print("Generated bldg_douane_annexe.obj")


# ==============================================================================
# MODEL 3: LE GRAND HOTEL DE SOUSSE (3-STORY FLAGSHIP FRENCH COLONIAL)
# ==============================================================================
def build_grand_hotel():
    obj_p = os.path.join(OUT_DIR, "bldg_grand_hotel.obj")
    mtl_p = os.path.join(OUT_DIR, "bldg_grand_hotel.mtl")
    w = ObjWriter(obj_p, mtl_p, "bldg_grand_hotel.mtl")

    w.add_material("cream_plaster", "textures/cream_plaster.png")
    w.add_material("stone_blocks", "textures/stone_blocks.png")
    w.add_material("wrought_iron", "textures/wrought_iron.png")
    w.add_material("wood_shutter", "textures/wood_shutter.png")
    w.add_material("roof_tile_red", "textures/roof_tile_red.png")
    w.add_material("awning_striped", "textures/awning_striped.png")

    W, D, H = 32.0, 20.0, 15.0
    half_w, half_d = W / 2.0, D / 2.0

    # 1. Ground floor rusticated stone base (0 to 4.5m)
    w.use_material("stone_blocks")
    w.add_box(-half_w, 0.0, -half_d, half_w, 4.5, half_d, u_scale=0.2, v_scale=0.2)

    # 2. Upper stories in cream Paris-stone plaster (4.5m to 12.5m)
    w.use_material("cream_plaster")
    w.add_box(-half_w, 4.5, -half_d, half_w, 12.5, half_d, u_scale=0.15, v_scale=0.15)

    # 3. Ground floor Arcaded Cafe Openings (6 bays)
    bays = [-12.5, -7.5, -2.5, 2.5, 7.5, 12.5]
    w.use_material("wood_shutter")
    for bx in bays:
        w.add_box(bx - 1.4, 0.0, half_d - 0.1, bx + 1.4, 3.8, half_d + 0.12, u_scale=0.3, v_scale=0.3)

    # Scalloped cafe awnings over ground floor openings
    w.use_material("awning_striped")
    for bx in bays:
        vn_aw = w.add_vn(0.0, 0.5, 0.8)
        v1 = w.add_v(bx - 1.6, 4.2, half_d); vt1 = w.add_vt(0, 0)
        v2 = w.add_v(bx + 1.6, 4.2, half_d); vt2 = w.add_vt(2, 0)
        v3 = w.add_v(bx + 1.6, 3.2, half_d + 1.5); vt3 = w.add_vt(2, 1)
        v4 = w.add_v(bx - 1.6, 3.2, half_d + 1.5); vt4 = w.add_vt(0, 1)
        w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_aw)

    # 4. Continuous 2nd-Floor Cantilevered Balcony Slab (y = 4.5m to 4.8m, projecting 1.8m)
    w.use_material("stone_blocks")
    w.add_box(-half_w, 4.5, half_d, half_w, 4.8, half_d + 1.8, u_scale=0.2, v_scale=0.2)

    # Ornate Cast-Iron Balustrade Railings (y = 4.8m to 5.8m)
    w.use_material("wrought_iron")
    # Front rail
    w.add_box(-half_w, 4.8, half_d + 1.75, half_w, 5.8, half_d + 1.82, u_scale=0.5, v_scale=0.5)
    # Side rails
    w.add_box(-half_w, 4.8, half_d, -half_w + 0.08, 5.8, half_d + 1.8, u_scale=0.5, v_scale=0.5)
    w.add_box(half_w - 0.08, 4.8, half_d, half_w, 5.8, half_d + 1.8, u_scale=0.5, v_scale=0.5)

    # 5. Second Floor French Doors with Green Louvered Shutters (6 bays)
    w.use_material("wood_shutter")
    for bx in bays:
        w.add_box(bx - 1.2, 4.8, half_d + 0.05, bx + 1.2, 8.2, half_d + 0.20, u_scale=0.3, v_scale=0.3)

    # 6. Third Floor French Windows with Louvered Shutters & Molded Sills (6 bays)
    w.use_material("wood_shutter")
    for bx in bays:
        w.add_box(bx - 1.1, 9.0, half_d + 0.05, bx + 1.1, 11.8, half_d + 0.20, u_scale=0.3, v_scale=0.3)

    w.use_material("cream_plaster")
    for bx in bays:
        # Molded window sill & pediment
        w.add_box(bx - 1.3, 8.8, half_d, bx + 1.3, 9.0, half_d + 0.35, u_scale=0.3, v_scale=0.3)
        w.add_box(bx - 1.3, 11.8, half_d, bx + 1.3, 12.2, half_d + 0.35, u_scale=0.3, v_scale=0.3)

    # 7. Classical Dentil Cornice (y = 12.3m to 12.8m)
    w.use_material("cream_plaster")
    w.add_box(-half_w - 0.4, 12.3, -half_d - 0.4, half_w + 0.4, 12.8, half_d + 0.4, u_scale=0.2, v_scale=0.2)

    # 8. Hipped Red Terracotta Tile Mansard Roof (12.8m to 15.5m)
    w.use_material("roof_tile_red")
    mansard_inset = 2.2
    top_y = 15.5
    bot_y = 12.8

    # Front slope
    vn_rf = w.add_vn(0.0, 0.7, 0.7)
    v1 = w.add_v(-half_w, bot_y, half_d); vt1 = w.add_vt(0, 0)
    v2 = w.add_v(half_w, bot_y, half_d); vt2 = w.add_vt(10, 0)
    v3 = w.add_v(half_w - mansard_inset, top_y, half_d - mansard_inset); vt3 = w.add_vt(10, 3)
    v4 = w.add_v(-half_w + mansard_inset, top_y, half_d - mansard_inset); vt4 = w.add_vt(0, 3)
    w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_rf)

    # Back slope
    vn_rb = w.add_vn(0.0, 0.7, -0.7)
    v5 = w.add_v(half_w, bot_y, -half_d); vt5 = w.add_vt(0, 0)
    v6 = w.add_v(-half_w, bot_y, -half_d); vt6 = w.add_vt(10, 0)
    v7 = w.add_v(-half_w + mansard_inset, top_y, -half_d + mansard_inset); vt7 = w.add_vt(10, 3)
    v8 = w.add_v(half_w - mansard_inset, top_y, -half_d + mansard_inset); vt8 = w.add_vt(0, 3)
    w.add_quad(v5, v6, v7, v8, vt5, vt6, vt7, vt8, vn_rb)

    # Left slope
    vn_rl = w.add_vn(-0.7, 0.7, 0.0)
    v9 = w.add_v(-half_w, bot_y, -half_d); vt9 = w.add_vt(0, 0)
    v10 = w.add_v(-half_w, bot_y, half_d); vt10 = w.add_vt(6, 0)
    v11 = w.add_v(-half_w + mansard_inset, top_y, half_d - mansard_inset); vt11 = w.add_vt(6, 3)
    v12 = w.add_v(-half_w + mansard_inset, top_y, -half_d + mansard_inset); vt12 = w.add_vt(0, 3)
    w.add_quad(v9, v10, v11, v12, vt9, vt10, vt11, vt12, vn_rl)

    # Right slope
    vn_rr = w.add_vn(0.7, 0.7, 0.0)
    v13 = w.add_v(half_w, bot_y, half_d); vt13 = w.add_vt(0, 0)
    v14 = w.add_v(half_w, bot_y, -half_d); vt14 = w.add_vt(6, 0)
    v15 = w.add_v(half_w - mansard_inset, top_y, -half_d + mansard_inset); vt15 = w.add_vt(6, 3)
    v16 = w.add_v(half_w - mansard_inset, top_y, half_d - mansard_inset); vt16 = w.add_vt(0, 3)
    w.add_quad(v13, v14, v15, v16, vt13, vt14, vt15, vt16, vn_rr)

    # Flat top roof cap
    vn_rt = w.add_vn(0.0, 1.0, 0.0)
    w.add_quad(v4, v3, v8, v7, vt4, vt3, vt8, vt7, vn_rt)

    w.close()
    print("Generated bldg_grand_hotel.obj")


# ==============================================================================
# MODEL 4: LE CERCLE MILITAIRE (NEOCLASSICAL WITH PILASTERS & BALUSTRADE)
# ==============================================================================
def build_cercle_militaire():
    obj_p = os.path.join(OUT_DIR, "bldg_cercle_militaire.obj")
    mtl_p = os.path.join(OUT_DIR, "bldg_cercle_militaire.mtl")
    w = ObjWriter(obj_p, mtl_p, "bldg_cercle_militaire.mtl")

    w.add_material("buff_stone", "textures/buff_stone.png")
    w.add_material("cream_plaster", "textures/cream_plaster.png")
    w.add_material("wood_shutter", "textures/wood_shutter.png")
    w.add_material("wrought_iron", "textures/wrought_iron.png")

    W, D, H = 26.0, 18.0, 11.5
    half_w, half_d = W / 2.0, D / 2.0

    # 1. Main building structure
    w.use_material("cream_plaster")
    w.add_box(-half_w, 0.0, -half_d, half_w, H, half_d, u_scale=0.15, v_scale=0.15)

    # 2. Neoclassical Ashlar Pilasters (4 colossal order pilasters flanking bays)
    w.use_material("buff_stone")
    pilaster_xs = [-half_w + 0.6, -3.5, 3.5, half_w - 0.6]
    for px in pilaster_xs:
        w.add_box(px - 0.6, 0.0, half_d, px + 0.6, H, half_d + 0.45, u_scale=0.2, v_scale=0.2)

    # 3. Central Monumental Entrance Portal with Classical Triangular Pediment
    # Entrance door
    w.use_material("wood_shutter")
    w.add_box(-1.8, 0.0, half_d, 1.8, 3.8, half_d + 0.15, u_scale=0.3, v_scale=0.3)
    # Portal frame and entablature
    w.use_material("buff_stone")
    w.add_box(-2.5, 0.0, half_d + 0.1, -1.8, 4.2, half_d + 0.55, u_scale=0.2, v_scale=0.2)
    w.add_box(1.8, 0.0, half_d + 0.1, 2.5, 4.2, half_d + 0.55, u_scale=0.2, v_scale=0.2)
    w.add_box(-2.8, 4.2, half_d + 0.1, 2.8, 4.8, half_d + 0.65, u_scale=0.2, v_scale=0.2)
    # Triangular pediment above door
    vn_ped = w.add_vn(0.0, 0.6, 0.8)
    v1 = w.add_v(-2.8, 4.8, half_d + 0.6); vt1 = w.add_vt(0, 0)
    v2 = w.add_v(2.8, 4.8, half_d + 0.6); vt2 = w.add_vt(2, 0)
    v3 = w.add_v(0.0, 6.2, half_d + 0.6); vt3 = w.add_vt(1, 1)
    w.add_tri(v1, v2, v3, vt1, vt2, vt3, vn_ped)

    # 4. Flanking Symmetrical Windows (Ground and First Floor)
    win_bays = [-9.5, -6.5, 6.5, 9.5]
    w.use_material("wood_shutter")
    for bx in win_bays:
        # Ground floor windows
        w.add_box(bx - 1.1, 1.2, half_d - 0.05, bx + 1.1, 3.6, half_d + 0.15, u_scale=0.3, v_scale=0.3)
        # Upper floor windows
        w.add_box(bx - 1.1, 6.5, half_d - 0.05, bx + 1.1, 9.2, half_d + 0.15, u_scale=0.3, v_scale=0.3)

    # Upper center window above entrance
    w.add_box(-1.2, 6.8, half_d - 0.05, 1.2, 9.5, half_d + 0.15, u_scale=0.3, v_scale=0.3)

    # 5. Balustraded Roof Parapet with Corner Finials
    w.use_material("buff_stone")
    # Base cornice
    w.add_box(-half_w - 0.3, H - 0.3, -half_d - 0.3, half_w + 0.3, H + 0.3, half_d + 0.3, u_scale=0.2, v_scale=0.2)
    # Balustrade rail
    w.add_box(-half_w, H + 0.3, half_d - 0.1, half_w, H + 1.2, half_d + 0.2, u_scale=0.4, v_scale=0.4)
    # Corner decorative piers
    w.add_box(-half_w - 0.2, H + 0.3, half_d - 0.2, -half_w + 0.8, H + 1.6, half_d + 0.4, u_scale=0.2, v_scale=0.2)
    w.add_box(half_w - 0.8, H + 0.3, half_d - 0.2, half_w + 0.2, H + 1.6, half_d + 0.4, u_scale=0.2, v_scale=0.2)

    w.close()
    print("Generated bldg_cercle_militaire.obj")


# ==============================================================================
# MODEL 5: IMMEUBLE PICHON (RESIDENTIAL & COMMERCIAL BOULEVARD CORNER)
# ==============================================================================
def build_immeuble_pichon():
    obj_p = os.path.join(OUT_DIR, "bldg_immeuble_pichon.obj")
    mtl_p = os.path.join(OUT_DIR, "bldg_immeuble_pichon.mtl")
    w = ObjWriter(obj_p, mtl_p, "bldg_immeuble_pichon.mtl")

    w.add_material("cream_plaster", "textures/cream_plaster.png")
    w.add_material("awning_striped", "textures/awning_striped.png")
    w.add_material("wood_shutter", "textures/wood_shutter.png")
    w.add_material("roof_tile_red", "textures/roof_tile_red.png")

    W, D, H = 22.0, 16.0, 10.0
    half_w, half_d = W / 2.0, D / 2.0

    # 1. Main structure in pale ochre/cream plaster
    w.use_material("cream_plaster")
    w.add_box(-half_w, 0.0, -half_d, half_w, H, half_d, u_scale=0.15, v_scale=0.15)

    # 2. Ground-floor Shops with Individual Striped Awnings (4 bays)
    shop_xs = [-7.5, -2.5, 2.5, 7.5]
    w.use_material("wood_shutter")
    for sx in shop_xs:
        w.add_box(sx - 1.6, 0.0, half_d - 0.05, sx + 1.6, 3.2, half_d + 0.12, u_scale=0.3, v_scale=0.3)

    w.use_material("awning_striped")
    for sx in shop_xs:
        vn_aw = w.add_vn(0.0, 0.6, 0.8)
        v1 = w.add_v(sx - 1.8, 3.5, half_d); vt1 = w.add_vt(0, 0)
        v2 = w.add_v(sx + 1.8, 3.5, half_d); vt2 = w.add_vt(2, 0)
        v3 = w.add_v(sx + 1.8, 2.6, half_d + 1.6); vt3 = w.add_vt(2, 1)
        v4 = w.add_v(sx - 1.8, 2.6, half_d + 1.6); vt4 = w.add_vt(0, 1)
        w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_aw)

    # 3. Upper Floor Shuttered Windows (4 bays)
    w.use_material("wood_shutter")
    for sx in shop_xs:
        w.add_box(sx - 1.1, 5.0, half_d - 0.05, sx + 1.1, 7.8, half_d + 0.15, u_scale=0.3, v_scale=0.3)

    # 4. Hipped Red Terracotta Roof
    w.use_material("roof_tile_red")
    r_inset = 2.0
    bot_y = H
    top_y = H + 2.8

    vn_rf = w.add_vn(0.0, 0.7, 0.7)
    v1 = w.add_v(-half_w, bot_y, half_d); vt1 = w.add_vt(0, 0)
    v2 = w.add_v(half_w, bot_y, half_d); vt2 = w.add_vt(8, 0)
    v3 = w.add_v(half_w - r_inset, top_y, half_d - r_inset); vt3 = w.add_vt(8, 3)
    v4 = w.add_v(-half_w + r_inset, top_y, half_d - r_inset); vt4 = w.add_vt(0, 3)
    w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn_rf)

    vn_rb = w.add_vn(0.0, 0.7, -0.7)
    v5 = w.add_v(half_w, bot_y, -half_d); vt5 = w.add_vt(0, 0)
    v6 = w.add_v(-half_w, bot_y, -half_d); vt6 = w.add_vt(8, 0)
    v7 = w.add_v(-half_w + r_inset, top_y, -half_d + r_inset); vt7 = w.add_vt(8, 3)
    v8 = w.add_v(half_w - r_inset, top_y, -half_d + r_inset); vt8 = w.add_vt(0, 3)
    w.add_quad(v5, v6, v7, v8, vt5, vt6, vt7, vt8, vn_rb)

    vn_rt = w.add_vn(0.0, 1.0, 0.0)
    w.add_quad(v4, v3, v8, v7, vt4, vt3, vt8, vt7, vn_rt)

    w.close()
    print("Generated bldg_immeuble_pichon.obj")


# ==============================================================================
# MODEL 6: QUAYSIDE HISTORIC LAMP POST & SIGNAL MAST
# ==============================================================================
def build_quayside_lamp_post():
    obj_p = os.path.join(OUT_DIR, "quayside_lamp_post.obj")
    mtl_p = os.path.join(OUT_DIR, "quayside_lamp_post.mtl")
    w = ObjWriter(obj_p, mtl_p, "quayside_lamp_post.mtl")

    w.add_material("wrought_iron", "textures/wrought_iron.png")

    w.use_material("wrought_iron")

    # 1. Octagonal Pedestal Base (y = 0 to 1.2m)
    num_seg = 8
    r_base = 0.45
    r_mid = 0.32
    h_base = 1.2

    for i in range(num_seg):
        a1 = (i / num_seg) * 2.0 * math.pi
        a2 = ((i + 1) / num_seg) * 2.0 * math.pi
        x1_b, z1_b = r_base * math.cos(a1), r_base * math.sin(a1)
        x2_b, z2_b = r_base * math.cos(a2), r_base * math.sin(a2)
        x1_t, z1_t = r_mid * math.cos(a1), r_mid * math.sin(a1)
        x2_t, z2_t = r_mid * math.cos(a2), r_mid * math.sin(a2)

        mid_a = (a1 + a2) * 0.5
        vn = w.add_vn(math.cos(mid_a), 0.1, math.sin(mid_a))

        v1 = w.add_v(x1_b, 0.0, z1_b); vt1 = w.add_vt(i / 8.0, 0)
        v2 = w.add_v(x2_b, 0.0, z2_b); vt2 = w.add_vt((i + 1) / 8.0, 0)
        v3 = w.add_v(x2_t, h_base, z2_t); vt3 = w.add_vt((i + 1) / 8.0, 1)
        v4 = w.add_v(x1_t, h_base, z1_t); vt4 = w.add_vt(i / 8.0, 1)
        w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn)

    # 2. Tapered Mast (y = 1.2 to 7.2m)
    r_top = 0.14
    h_mast = 7.2
    for i in range(num_seg):
        a1 = (i / num_seg) * 2.0 * math.pi
        a2 = ((i + 1) / num_seg) * 2.0 * math.pi
        x1_b, z1_b = r_mid * math.cos(a1), r_mid * math.sin(a1)
        x2_b, z2_b = r_mid * math.cos(a2), r_mid * math.sin(a2)
        x1_t, z1_t = r_top * math.cos(a1), r_top * math.sin(a1)
        x2_t, z2_t = r_top * math.cos(a2), r_top * math.sin(a2)

        mid_a = (a1 + a2) * 0.5
        vn = w.add_vn(math.cos(mid_a), 0.05, math.sin(mid_a))

        v1 = w.add_v(x1_b, h_base, z1_b); vt1 = w.add_vt(i / 8.0, 0)
        v2 = w.add_v(x2_b, h_base, z2_b); vt2 = w.add_vt((i + 1) / 8.0, 0)
        v3 = w.add_v(x2_t, h_mast, z2_t); vt3 = w.add_vt((i + 1) / 8.0, 4)
        v4 = w.add_v(x1_t, h_mast, z1_t); vt4 = w.add_vt(i / 8.0, 4)
        w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn)

    # 3. Dual Lantern Curved Arms and Crossbar (y = 6.8 to 7.8m)
    w.add_box(-1.6, 6.8, -0.08, 1.6, 7.0, 0.08, u_scale=1.0, v_scale=1.0)

    # Left & Right Lanterns
    for lx in [-1.5, 1.5]:
        w.add_box(lx - 0.35, 6.2, -0.35, lx + 0.35, 7.2, 0.35, u_scale=1.0, v_scale=1.0)
        # Lantern cap
        w.add_box(lx - 0.42, 7.2, -0.42, lx + 0.42, 7.45, 0.42, u_scale=1.0, v_scale=1.0)

    w.close()
    print("Generated quayside_lamp_post.obj")


# ==============================================================================
# MODEL 7: MARITIME MOORING BOLLARD
# ==============================================================================
def build_mooring_bollard():
    obj_p = os.path.join(OUT_DIR, "mooring_bollard.obj")
    mtl_p = os.path.join(OUT_DIR, "mooring_bollard.mtl")
    w = ObjWriter(obj_p, mtl_p, "mooring_bollard.mtl")

    w.add_material("wrought_iron", "textures/wrought_iron.png")
    w.use_material("wrought_iron")

    # Base flange (y = 0 to 0.12m, r = 0.42)
    # Cylindrical body (y = 0.12 to 0.75m, r = 0.28)
    # Mushroom head (y = 0.75 to 0.95m, r = 0.38)
    num_seg = 10
    profile = [(0.0, 0.45), (0.12, 0.42), (0.14, 0.28), (0.72, 0.28), (0.80, 0.40), (0.92, 0.38), (0.96, 0.10)]

    for p_idx in range(len(profile) - 1):
        y1, r1 = profile[p_idx]
        y2, r2 = profile[p_idx + 1]
        for i in range(num_seg):
            a1 = (i / num_seg) * 2.0 * math.pi
            a2 = ((i + 1) / num_seg) * 2.0 * math.pi
            x1_b, z1_b = r1 * math.cos(a1), r1 * math.sin(a1)
            x2_b, z2_b = r1 * math.cos(a2), r1 * math.sin(a2)
            x1_t, z1_t = r2 * math.cos(a1), r2 * math.sin(a1)
            x2_t, z2_t = r2 * math.cos(a2), r2 * math.sin(a2)

            mid_a = (a1 + a2) * 0.5
            vn = w.add_vn(math.cos(mid_a), 0.2, math.sin(mid_a))

            v1 = w.add_v(x1_b, y1, z1_b); vt1 = w.add_vt(i / 10.0, y1)
            v2 = w.add_v(x2_b, y1, z2_b); vt2 = w.add_vt((i + 1) / 10.0, y1)
            v3 = w.add_v(x2_t, y2, z2_t); vt3 = w.add_vt((i + 1) / 10.0, y2)
            v4 = w.add_v(x1_t, y2, z1_t); vt4 = w.add_vt(i / 10.0, y2)
            w.add_quad(v1, v2, v3, v4, vt1, vt2, vt3, vt4, vn)

    w.close()
    print("Generated mooring_bollard.obj")


# ==============================================================================
# MODEL 8: DRESSED STONE PARK CURBING (SQUARE DE LA MARINE)
# ==============================================================================
def build_park_curbing():
    obj_p = os.path.join(OUT_DIR, "park_curbing.obj")
    mtl_p = os.path.join(OUT_DIR, "park_curbing.mtl")
    w = ObjWriter(obj_p, mtl_p, "park_curbing.mtl")

    w.add_material("buff_stone", "textures/buff_stone.png")
    w.use_material("buff_stone")

    # 6m length stone curbing block (L = 6.0, W = 0.35, H = 0.30)
    w.add_box(-3.0, 0.0, -0.175, 3.0, 0.30, 0.175, u_scale=0.5, v_scale=1.0)

    w.close()
    print("Generated park_curbing.obj")


if __name__ == "__main__":
    build_douane_neomoorish()
    build_douane_annexe()
    build_grand_hotel()
    build_cercle_militaire()
    build_immeuble_pichon()
    build_quayside_lamp_post()
    build_mooring_bollard()
    build_park_curbing()
    print("All distinct waterfront 3D models generated successfully!")
