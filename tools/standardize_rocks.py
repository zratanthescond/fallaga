import os
import re

file_path = "assets/saves/city.txt"
temp_file = "assets/saves/city.txt.tmp"

# Pattern to match the rock paths
rock_pattern = re.compile(r"assets/terrain/rocks/rock_moss_set_\d+_4k\.obj")
new_path = "assets/Rock1/Rock1.obj"

count = 0
with open(file_path, 'r') as f_in, open(temp_file, 'w') as f_out:
    for line in f_in:
        new_line, n = rock_pattern.subn(new_path, line)
        f_out.write(new_line)
        count += n

os.replace(temp_file, file_path)
print(f"Successfully replaced {count} rock references with {new_path}")
