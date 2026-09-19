# Enhanced Terrain Texture Layer System

## Overview

The terrain now uses a **multi-layer PNG-based texture identification system** that automatically applies the correct textures based on color values in your `textureMap.png`.

## Texture Layers (0-4)

Your terrain supports 5 different texture types:

| Layer | Type | Color ID | Use Case |
|-------|------|----------|----------|
| 0 | Gravel/Concrete | Dark Gray (64, 64, 64) | General ground, plains |
| 1 | Rocks | Mid Gray (128, 128, 128) | Rocky hills, cliffs |
| 2 | Rubble | Light Gray (192, 192, 192) | Ruins, weathered areas |
| 3 | Tarred Gravel | Very Dark (32, 32, 32) | Roads, paved surfaces |
| 4 | Sandy Gravel | Beige (255, 200, 100) | Beaches, sandy areas |

## How It Works

### 1. **Texture Identification (PNG Mask)**

The `assets/terrain/textureMap.png` acts as a layer identification map. Each pixel's RGB color tells the shader which texture to apply:

```
┌─────────────────────────────────────┐
│ textureMap.png Layout               │
├─────────────────────────────────────┤
│ (32,32,32)   → Roads (Layer 3)      │
│ (64,64,64)   → Gravel (Layer 0)     │
│ (128,128,128) → Rocks (Layer 1)     │
│ (192,192,192) → Rubble (Layer 2)    │
│ (255,200,100) → Beach (Layer 4)     │
└─────────────────────────────────────┘
```

### 2. **Shader Processing**

The fragment shader (`terrain_chunk.frag`) performs these steps:

1. **Sample the mask** at the current UV coordinates
2. **Identify the primary texture layer** by analyzing the color
3. **Sample the corresponding texture array** (diffuse, normal, displacement)
4. **Blend with secondary layer** based on terrain slope
5. **Apply parallax occlusion mapping** for depth effect

### 3. **Texture Arrays**

All textures are loaded into OpenGL texture arrays:

- `terrainLayerArray` - Diffuse (color) maps for all 5 layers
- `terrainNormalArray` - Normal maps for all 5 layers
- `terrainDispArray` - Displacement maps for parallax effects

## Creating Your Texture Layer Mask

### Option 1: Paint Directly in Image Editor

Use GIMP, Photoshop, or similar to paint your `textureMap.png`:

1. Create an image matching your heightmap dimensions
2. Fill regions with the appropriate colors:
   - Roads: Fill with `rgb(32, 32, 32)` - very dark gray
   - Gravel: Fill with `rgb(64, 64, 64)` - dark gray
   - Rocks: Fill with `rgb(128, 128, 128)` - medium gray
   - Rubble: Fill with `rgb(192, 192, 192)` - light gray
   - Beach: Fill with `rgb(255, 200, 100)` - beige/sandy
3. Save as PNG (8-bit or higher)

### Option 2: Use the Texture Generator Script

```bash
# Analyze your current texture mask
python tools/texture_layer_generator.py analyze assets/terrain/textureMap.png

# Create a sample/reference mask
python tools/texture_layer_generator.py create
```

### Option 3: Programmatic Generation

Create a simple script to generate the mask based on terrain features:

```python
from PIL import Image
import numpy as np

# Example: Generate mask based on height
height = Image.open('assets/terrain/heightmap.png')
height_arr = np.array(height)

# Normalize to 0-1
height_norm = height_arr / 65535.0 if height_arr.dtype == np.uint16 else height_arr / 255.0

# Create texture mask
mask = np.zeros((*height_arr.shape, 3), dtype=np.uint8)

# High terrain = rocks
mask[height_norm > 0.7] = [128, 128, 128]

# Medium terrain = gravel
mask[(height_norm > 0.3) & (height_norm <= 0.7)] = [64, 64, 64]

# Low terrain = beach/sand
mask[height_norm <= 0.3] = [255, 200, 100]

# Save
Image.fromarray(mask).save('assets/terrain/textureMap.png')
```

## Customizing Texture Types

To change which texture files are used for each layer, edit `TerrainTextureLayer.cpp`:

```cpp
const TerrainTextureLayer::TextureTypeInfo TerrainTextureLayer::TEXTURE_INFO[NUM_LAYERS] = {
    {GRAVEL_CONCRETE, "Gravel/Concrete", 
     "assets/terrain/YOUR_DIFFUSE.jpg",      // Diffuse map
     "assets/terrain/YOUR_NORMAL.png",       // Normal map
     "assets/terrain/YOUR_DISP.png",         // Displacement map
     glm::vec3(64, 64, 64)},                 // Identification color
    // ... other layers
};
```

## Advanced Features

### Smooth Blending Between Textures

The system automatically blends between texture layers:

- **Slope blending**: Steeper terrain shows secondary texture layer (rocks)
- **Color-based blending**: Boundaries between colors smooth blend textures
- **Distance-based parallax**: Close-up uses parallax, far away is simplified

### Normal Mapping

Each texture layer can have its own normal map for realistic surface details:

- Rocks: Show surface relief
- Roads: Smooth with slight imperfections
- Beach: Sandy ripples

### Parallax Occlusion Mapping (POM)

When camera is within 60 units, the shader applies displacement-based parallax:

- Creates depth impression without extra geometry
- Uses displacement maps from each texture layer
- Fades with distance for performance

## Debugging

### View Texture Layer Identification

To debug which layer is being selected at each point, temporarily modify the shader to visualize:

```glsl
// In terrain_chunk.frag, replace the color output:
FragColor = vec4(vec3(primaryLayer) / 4.0, 1.0);  // Shows layers as grayscale
```

### Check Mask Loading

Look for debug output in console:

```
[TerrainTextureLayer] Loaded texture mask: assets/terrain/textureMap.png (512x512x4)
[TerrainManager] Layer 0 (Gravel/Concrete)
[TerrainManager] Layer 1 (Rocks)
[TerrainManager] Layer 2 (Rubble)
[TerrainManager] Layer 3 (Tarred Gravel (Road))
[TerrainManager] Layer 4 (Sandy Gravel (Beach))
```

## Performance Considerations

- **Texture arrays** are more efficient than separate textures
- **Triplanar mapping** eliminates stretching but uses 3x sampling
- **Parallax occlusion** is distance-culled (only within 60 units)
- **Layer blending** is optimized - only 2 layers blended at a time

## Common Issues

### Issue: Everything looks like one texture

**Cause**: `textureMap.png` not found or colors don't match expected values

**Solution**:
1. Verify file exists: `assets/terrain/textureMap.png`
2. Check colors with analysis script
3. Ensure PNG is in RGB or RGBA format

### Issue: Seams between texture regions

**Cause**: Sharp color boundaries in texture mask

**Solution**: Use slight gradients or antialiasing in your mask around region boundaries

### Issue: Performance issues

**Cause**: Parallax occlusion enabled at all distances

**Solution**: Already handled - parallax only applies within 60 units of camera

## Adding New Texture Types

To add a 6th texture type:

1. Update `TerrainTextureLayer.h`:
   ```cpp
   enum TextureType : int {
       // ... existing
       NEW_TEXTURE = 5,
       NUM_LAYERS = 6
   };
   ```

2. Add texture info to `TEXTURE_INFO` array in `.cpp`

3. Update shader to handle layer 5

4. Update texture array loading to include new layer

## Related Files

- `src/TerrainTextureLayer.h/cpp` - Layer identification system
- `src/TerrainManager.cpp` - Texture loading and setup
- `src/shaders/terrain_chunk.frag` - Shader implementation
- `assets/terrain/textureMap.png` - Your layer identification mask
- `tools/texture_layer_generator.py` - Helper scripts
