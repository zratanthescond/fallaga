import os
import re

cpp_file = "C:/Users/honco/OneDrive/Bureau/fallaga/src/generatedTerrain.cpp"
with open(cpp_file, "r") as f:
    code = f.read()

# We need to wipe out the contents of these functions and replace them with our new map-aligned geometry.
# Medina: Lines 199 to 234
code = re.sub(r'static std::vector<Vec2> getMedinaWallPolygon\(\) \{.*?\}', 
              r'''static std::vector<Vec2> getMedinaWallPolygon() {
    // Traced exactly from the green medina walls in the user map
    return {
        {900, 364},  // SW corner
        {900, 724},  // NW corner
        {1300, 724}, // NE corner
        {1300, 364}, // SE corner
        {1100, 264}, // S wedge point
        {900, 364}   // close
    };
}''', code, flags=re.DOTALL)

# Kasbah: 239 to 246
code = re.sub(r'static std::vector<Vec2> getKasbahPolygon\(\) \{.*?\}', 
              r'''static std::vector<Vec2> getKasbahPolygon() {
    return {
        {900, 364},
        {900, 464},
        {1000, 464},
        {1000, 364},
        {900, 364}
    };
}''', code, flags=re.DOTALL)

# Port: 254 to 273
code = re.sub(r'static std::vector<Vec2> getPortPolygon\(\) \{.*?\}', 
              r'''static std::vector<Vec2> getPortPolygon() {
    // Square port basin exactly east of the Medina per the map
    return {
        {1360, 364}, // SW inner
        {1360, 724}, // NW inner
        {1900, 724}, // NE outer
        {1900, 364}, // SE outer
        {1360, 364}
    };
}''', code, flags=re.DOTALL)

# Port Basin: 276 to 288
code = re.sub(r'static std::vector<Vec2> getPortBasinPolygon\(\) \{.*?\}', 
              r'''static std::vector<Vec2> getPortBasinPolygon() {
    return {
        {1380, 384},
        {1380, 704},
        {1880, 704},
        {1880, 384},
        {1380, 384}
    };
}''', code, flags=re.DOTALL)

# Military Camp: 294 to 301
code = re.sub(r'static std::vector<Vec2> getMilitaryCampPolygon\(\) \{.*?\}', 
              r'''static std::vector<Vec2> getMilitaryCampPolygon() {
    return {
        {600, 364},
        {600, 724}, // west fields
        {850, 724},
        {850, 364},
        {600, 364}
    };
}''', code, flags=re.DOTALL)

# Strip out the hardcoded internal Medina blocks from initCityBlocks to prevent rejection
# The procedural logic handles random buildings beautifully.
code = re.sub(r'buildings\.push_back\(\{.*?TerrainType::MEDINA_BUILDING\}\);', '', code)

with open(cpp_file, "w") as f:
    f.write(code)

print("Injected tracing logic into generatedTerrain.cpp.")
