import re

cpp_file = "C:/Users/honco/OneDrive/Bureau/fallaga/src/generatedTerrain.cpp"
with open(cpp_file, "r") as f:
    code = f.read()

def clear_func(name, empty_type):
    global code
    pattern = r'static std::vector<' + empty_type + r'> ' + name + r'\(\) \{.*?\}'
    replacement = 'static std::vector<' + empty_type + '> ' + name + '() {\n    return {};\n}'
    code = re.sub(pattern, replacement, code, flags=re.DOTALL)

clear_func('getMedinaWallPolygon', 'Vec2')
clear_func('getKasbahPolygon', 'Vec2')
clear_func('getPortPolygon', 'Vec2')
clear_func('getPortBasinPolygon', 'Vec2')
clear_func('getMilitaryCampPolygon', 'Vec2')
clear_func('initCityBlocks', 'BuildingBlock')

with open(cpp_file, "w") as f:
    f.write(code)

print("Succesfully removed all procedural wall geometries and city blocks!")
