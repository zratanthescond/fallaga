#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
int main() {
    int w, h, ch;
    unsigned short* data = stbi_load_16("assets/terrain/heightmap.png", &w, &h, &ch, 1);
    if(data) {
        std::cout << "Loaded " << w << "x" << h << "\n";
        unsigned short min_v = 65535, max_v = 0;
        long long sum = 0;
        for (int i=0; i<w*h; ++i) {
            if (data[i] < min_v) min_v = data[i];
            if (data[i] > max_v) max_v = data[i];
            sum += data[i];
        }
        std::cout << "Min: " << min_v << ", Max: " << max_v << ", Mean: " << sum/(w*h) << "\n";
    } else {
        std::cout << "Failed to load\n";
    }
    return 0;
}
