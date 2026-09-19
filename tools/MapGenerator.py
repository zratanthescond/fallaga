import os
import sys

try:
    import osmnx as ox
    import networkx as nx
    from shapely.geometry import Polygon
except ImportError:
    print("Please install requirements: pip install osmnx networkx shapely")
    sys.exit(1)

CITY_NAME = "Sousse, Tunisia"
import pathlib
OUTPUT_FILE = str(pathlib.Path(__file__).parent.parent / "src" / "ProceduralMapData.h")

print(f"Downloading street network for {CITY_NAME}...")
# Fetch walkable and draggable network
G = ox.graph_from_place(CITY_NAME, network_type="all")

print(f"Downloading building footprints for {CITY_NAME}...")
# Fetch buildings
# Use the newer tags parameter syntax 
tags = {"building": True}
buildings = ox.features_from_place(CITY_NAME, tags)
buildings = buildings.to_crs(epsg=32632) # Project to UTM for meters
G_proj = ox.project_graph(G, to_crs="EPSG:32632")

# We want to re-center the coordinates so that the center of Sousse is (0,0)
nodes, edges = ox.graph_to_gdfs(G_proj)
cx, cy = nodes.geometry.x.mean(), nodes.geometry.y.mean()

print(f"Center coordinates (Metric): {cx}, {cy}")

def format_vec2(x, y):
    # Scale down slightly if needed, or keep 1:1 meters
    scale = 1.0 
    return f"{ (x - cx)*scale :.2f}f, { -(y - cy)*scale :.2f}f"

with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <vector>\n")
    f.write("#include <string>\n")
    f.write("#include <glm/glm.hpp>\n\n")
    
    f.write("namespace ProceduralMap {\n\n")
    
    # Write Streets
    f.write("    struct MapStreet {\n")
    f.write("        std::string name;\n")
    f.write("        float width;\n")
    f.write("        std::vector<glm::vec2> points;\n")
    f.write("    };\n\n")
    
    f.write("    static const float raw_street_data[] = {\n")
    
    print("Exporting streets...")
    for u, v, key, data in G_proj.edges(keys=True, data=True):
        if "highway" in data and data.get("highway") in ["footway", "path", "steps"]:
            continue
            
        width = 10.0
        if "width" in data:
            try: width = float(data["width"])
            except: pass
            
        if "geometry" in data:
            coords = list(data["geometry"].coords)
        else:
            coords = [(G_proj.nodes[u]['x'], G_proj.nodes[u]['y']),
                      (G_proj.nodes[v]['x'], G_proj.nodes[v]['y'])]
            
        n = len(coords)
        f.write(f"        {width:.2f}f, {n}.0f")
        for x, y in coords:
            f.write(f", {format_vec2(x, y)}")
        f.write(",\n")
        
    f.write("    };\n\n")
    
    f.write("    inline std::vector<MapStreet> getStreets() {\n")
    f.write("        std::vector<MapStreet> streets;\n")
    f.write("        size_t i = 0;\n")
    f.write("        size_t total = sizeof(raw_street_data) / sizeof(float);\n")
    f.write("        while(i < total) {\n")
    f.write("            float w = raw_street_data[i++];\n")
    f.write("            int n = (int)raw_street_data[i++];\n")
    f.write("            std::vector<glm::vec2> pts;\n")
    f.write("            for(int k=0; k<n; ++k) {\n")
    f.write("                float x = raw_street_data[i++];\n")
    f.write("                float y = raw_street_data[i++];\n")
    f.write("                pts.emplace_back(x, y);\n")
    f.write("            }\n")
    f.write("            streets.push_back({\"Procedural Street\", w, std::move(pts)});\n")
    f.write("        }\n")
    f.write("        return streets;\n")
    f.write("    }\n\n")
    
    # Write Buildings
    f.write("    struct MapBuilding {\n")
    f.write("        float x, z;\n")
    f.write("        float width, depth;\n")
    f.write("        float height;\n")
    f.write("    };\n\n")
    
    f.write("    static const float raw_building_data[] = {\n")
    
    print("Exporting buildings...")
    for idx, row in buildings.iterrows():
        geom = row.geometry
        if not isinstance(geom, Polygon):
            continue
            
        # Get bounding box for simplicity
        minx, miny, maxx, maxy = geom.bounds
        w = (maxx - minx)
        d = (maxy - miny)
        bx = (minx + maxx) / 2.0
        by = (miny + maxy) / 2.0
        
        # Approximate height if available
        height = 15.0  # default height
        import pandas as pd
        if "height" in row and pd.notna(row["height"]):
            try: height = float(row["height"])
            except: pass
        elif "building:levels" in row and pd.notna(row["building:levels"]):
            try: height = float(row["building:levels"]) * 3.5
            except: pass
            
        f.write(f"        {bx - cx:.2f}f, {-(by - cy):.2f}f, {w:.2f}f, {d:.2f}f, {height:.2f}f,\n")
        
    f.write("    };\n\n")
    f.write("    inline std::vector<MapBuilding> getBuildings() {\n")
    f.write("        std::vector<MapBuilding> buildings;\n")
    f.write("        size_t i = 0;\n")
    f.write("        size_t total = sizeof(raw_building_data) / sizeof(float);\n")
    f.write("        while(i < total) {\n")
    f.write("            float x = raw_building_data[i++];\n")
    f.write("            float z = raw_building_data[i++];\n")
    f.write("            float w = raw_building_data[i++];\n")
    f.write("            float td = raw_building_data[i++];\n")
    f.write("            float h = raw_building_data[i++];\n")
    f.write("            buildings.push_back({x, z, w, td, h});\n")
    f.write("        }\n")
    f.write("        return buildings;\n")
    f.write("    }\n")
    f.write("}\n")

print(f"Successfully generated {OUTPUT_FILE}!")
