import sys
sys.stdout.reconfigure(encoding='utf-8')
with open('src/Terrain.cpp', encoding='utf-8', errors='ignore') as f:
    content = f.read()

start = content.find('void Terrain::renderOpaque')
if start == -1:
    print('not found')
    sys.exit()
# find end of function roughly
end = content.find('\nvoid Terrain::', start + 1)
if end == -1:
    end = start + 4000
chunk = content[start:end]
for i,l in enumerate(chunk.splitlines(), 0):
    if 'grass' in l.lower() or 'foliage' in l.lower() or 'culled' in l.lower() or 'frustum' in l.lower() or 'debug' in l.lower() or 'camera' in l.lower():
        print(f'{i+start}: {l}')
