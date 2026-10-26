import sys
sys.stdout.reconfigure(encoding='utf-8')
with open('src/Terrain.cpp', encoding='utf-8', errors='ignore') as f:
    content = f.read()

start = content.find('// Perform distance and frustum-based culling')
if start == -1:
    print('not found')
    sys.exit()

end = content.find('cachedCulledBatches[path] = std::move(culledMatrices);', start)
chunk = content[start:end+80]
print(chunk)
