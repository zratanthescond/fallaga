import os
import re

cpp_file = "C:/Users/honco/OneDrive/Bureau/fallaga/src/generatedTerrain.cpp"
with open(cpp_file, "r") as f:
    code = f.read()

new_streets = r'''static std::vector<Street> getHistoricalStreets() {
    std::vector<Street> streets;
    
    // 1. Gare Road (North-West to Train Station)
    streets.push_back({{{950, 724}, {800, 900}, {600, 1100}}, 14.0f, "Gare Rd", true});
    
    // 2. Boulevard de la Corniche (Bid Catacombes)
    streets.push_back({{{1360, 750}, {1500, 600}, {1700, 400}, {1900, 200}}, 14.0f, "Catacombes Bd", true});
    
    // 3. West Field Roads
    streets.push_back({{{900, 600}, {600, 650}, {300, 700}, {100, 750}}, 10.0f, "West Rd", false});
    streets.push_back({{{900, 450}, {600, 400}, {300, 450}, {100, 500}}, 10.0f, "West Rd 2", false});

    // 4. Southern Roads (roundabout 1)
    streets.push_back({{{1100, 264}, {1000, 150}, {950, 50}, {900, 0}}, 12.0f, "South Rd", true});
    streets.push_back({{{900, 364}, {800, 250}, {700, 150}, {600, 0}}, 10.0f, "South West Rd", false});

    // 5. Encircling Medina Boulevard (the yellow road surrounding the green)
    streets.push_back({{{880, 344}, {880, 744}, {1320, 744}, {1320, 344}, {1100, 244}, {880, 344}}, 10.0f, "Medina Ring", true});

    // 6. Port Access
    streets.push_back({{{1300, 700}, {1360, 700}}, 12.0f, "Port Access N", true});
    streets.push_back({{{1300, 400}, {1360, 400}}, 12.0f, "Port Access S", true});

    return streets;
}'''

code = re.sub(r'static std::vector<Street> getHistoricalStreets\(\) \{.*?return streets;\n\}', new_streets, code, flags=re.DOTALL)

with open(cpp_file, "w") as f:
    f.write(code)

print("Updated Streets layout to match map traces.")
