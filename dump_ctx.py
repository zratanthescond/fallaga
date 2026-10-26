with open('src/Terrain.cpp', encoding='utf-8', errors='ignore') as f:
    c = f.read()
start = c.find('// Perform distance and frustum-based culling')
end = c.find('cachedCulledBatches[path] = std::move(culledMatrices);', start)
pre = c[max(0,start-1000):start]
chunk = c[start:end+80]
with open('cull_ctx.txt','w', encoding='utf-8') as out:
    out.write('===PRE===\n')
    out.write(pre)
    out.write('\n===CURR===\n')
    out.write(chunk)
print('done')
