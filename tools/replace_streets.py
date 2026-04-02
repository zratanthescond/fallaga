import re
with open('c:/Users/honco/OneDrive/Bureau/fallaga/src/Terrain.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

new_impl = '''void Terrain::initStreets()
{
    streetPolylines.clear();
    auto streets = ProceduralMap::getStreets();
    for (const auto& st : streets) {
        StreetPolyline sp;
        sp.name = st.name;
        sp.width = st.width;
        sp.points = st.points;
        streetPolylines.push_back(std::move(sp));
    }
    std::cout << "[Terrain] Initialized " << streetPolylines.size() << " procedural streets" << std::endl;
}'''

text = re.sub(r'void Terrain::initStreets\(\)\s*\{[\s\S]*?std::cout\s*<<\s*\"\[Terrain\].*?size\(\).*?std::endl;\s*\}', new_impl, text, count=1)

with open('c:/Users/honco/OneDrive/Bureau/fallaga/src/Terrain.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print("Done!")
