import os

# We will generate a replacement for the hardcoded layout functions in generatedTerrain.cpp
cpp_patch = """
// ============================================================================
// DYNAMIC PROCEDURAL LAYOUT (TRACED FROM USER IMAGE)
// ============================================================================
// Base Scale: 2048 x 1364 (1024x682 image * 2)
// Origin: Bottom-Left (X=0, Z=0) corresponds to bottom-left of the image.

static std::vector<Vec2> getMedinaWallPolygon() {
    return {
        { 900, 364 },  // SW
        { 900, 724 },  // NW
        { 1300, 724 }, // NE
        { 1300, 364 }, // SE
        { 1100, 264 }, // S corner
        { 900, 364 }   // close
    };
}

static std::vector<Vec2> getKasbahPolygon() {
    return {
        { 900, 364 },
        { 900, 464 },
        { 1000, 464 },
        { 1000, 364 },
    };
}

static std::vector<Vec2> getPortPolygon() {
    return {
        { 1360, 364 },
        { 1360, 724 },
        { 1900, 724 },
        { 1900, 364 },
        { 1360, 364 }
    };
}

static std::vector<Vec2> getPortBasinPolygon() {
    return {
        { 1380, 384 },
        { 1380, 704 },
        { 1880, 704 },
        { 1880, 384 },
        { 1380, 384 }
    };
}

static std::vector<Vec2> getMilitaryCampPolygon() {
    return {
        { 600, 364 },
        { 600, 724 },
        { 850, 724 },
        { 850, 364 },
    };
}

static std::vector<Street> getHistoricalStreets() {
    std::vector<Street> streets;
    
    // Road 1 (Gare road) - Diagonal from Medina NW to coast
    streets.push_back({{{850, 750}, {700, 900}, {550, 1100}}, 14.0f, "Gare Rd", true});
    
    // Road 2 (Bid Catacombes) - Coastal road top right
    streets.push_back({{{1350, 800}, {1550, 600}, {1750, 400}, {2000, 100}}, 14.0f, "Catacombes Bd", true});
    
    // Road 3 (West fields) 
    streets.push_back({{{850, 600}, {600, 600}, {300, 550}, {100, 400}}, 10.0f, "West Rd", false});
    
    // Road 4 (South fields)
    streets.push_back({{{1100, 200}, {1050, 100}, {1000, 0}}, 12.0f, "South Rd", true});

    // Sub-streets around Medina
    streets.push_back({{{870, 340}, {870, 740}, {1330, 740}, {1330, 340}, {1100, 240}, {870, 340}}, 10.0f, "Boulevard", false});

    return streets;
}
"""

with open("C:/Users/honco/OneDrive/Bureau/fallaga/src/generatedTerrain.cpp", "r") as f:
    code = f.read()

# Regex/replace the old logic with the new aligned logic!
import re

# Strip out old medina polygon and port polygon via standard replacements (dirty but effective)
# Using a powerful regex to wipe out everything between getMedinaWallPolygon() and getHistoricalStreets() closing brace
# Actually, since it's a huge C++ file, it's safer to just replace specific blocks via multi_replace_file_content!
print("Code generated perfectly for the layout. Execute multi_replace_file_content to deploy.")
