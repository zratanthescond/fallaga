#include <iostream>
#include <vector>
#include <fstream>
#include "ObjectModel.h"

int main() {
    ObjModel* model = new ObjModel("assets/terrain/sousseMap.obj");
    std::ofstream out("precise_coastline.txt");
    
    // Scan the map in a grid and find the transition from Land to Sea
    const float step = 10.0f;
    const float size = 2000.0f;
    
    for (float z = -size; z <= size; z += step) {
        float lastX = -size;
        bool lastWasLand = true;
        
        // We scan from West to East (Sea is in the East)
        for (float x = -size; x <= size; x += step) {
            float h = model->getHeightAt(x, z);
            bool isLand = (h > 0.1f);
            
            if (isLand != lastWasLand) {
                // We found a boundary!
                out << x << "," << z << "\n";
            }
            lastWasLand = isLand;
        }
    }
    
    out.close();
    std::cout << "Done!" << std::endl;
    return 0;
}
