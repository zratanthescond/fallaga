# Quick Start: Enhanced Terrain Texture System

## TL;DR - What Changed

Your terrain now uses a **color-coded PNG mask** to identify which texture to apply at each location. 

| Color | Texture | Used For |
|-------|---------|----------|
| 🟦 Dark Gray (32,32,32) | Roads | Paved surfaces |
| 🟩 Dark Gray (64,64,64) | Gravel | General ground |
| ⬜ Medium Gray (128,128,128) | Rocks | Hills & cliffs |
| ⬜ Light Gray (192,192,192) | Rubble | Ruins & weathered |
| 🟧 Beige (255,200,100) | Beach | Sandy areas |

---

## 5-Minute Setup

### 1. Update Your Texture Map PNG

Open `assets/terrain/textureMap.png` in an image editor and paint regions with the colors above.

**Using GIMP/Photoshop:**
- Roads: Fill with RGB(32, 32, 32)
- Gravel: Fill with RGB(64, 64, 64)
- Rocks: Fill with RGB(128, 128, 128)
- Rubble: Fill with RGB(192, 192, 192)
- Beach: Fill with RGB(255, 200, 100)

### 2. Rebuild

```bash
cd fallaga/build
cmake ..
cmake --build . --config Release
```

### 3. Run

Launch your game - debug output will show:

```
[TerrainTextureLayer] Loaded texture mask: assets/terrain/textureMap.png
[TerrainManager] Layer 0 (Gravel/Concrete)
[TerrainManager] Layer 1 (Rocks)
...
```

---

## File Checklist

✓ Created:
- `src/TerrainTextureLayer.h` - Texture identification system
- `src/TerrainTextureLayer.cpp` - Implementation
- `TERRAIN_TEXTURE_README.md` - Full documentation
- `TEXTURE_SYSTEM_CHANGES.md` - Implementation details
- `tools/texture_layer_generator.py` - Helper script

✓ Modified:
- `src/TerrainManager.cpp` - Uses new system
- `src/shaders/terrain_chunk.frag` - Shader update

---

## Common Issues

**Q: Game won't compile?**
A: Run `cmake ..` again to regenerate build files. New files are auto-discovered.

**Q: Textures look wrong?**
A: Check your `textureMap.png` uses the exact RGB values shown above.

**Q: Still seeing old textures?**
A: Make sure `assets/terrain/` folder has all material subfolders.

---

## Advanced Usage

See `TERRAIN_TEXTURE_README.md` for:
- Programmatic mask generation
- Adding more texture types
- Debugging techniques
- Performance tuning
- Custom texture paths

---

## Need Help?

1. Check console output for errors
2. Use the analysis tool: `python tools/texture_layer_generator.py analyze`
3. Read full docs: `TERRAIN_TEXTURE_README.md`
4. Check file paths match what shader expects
