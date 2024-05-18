import glob
import subprocess
import os
import shutil
import sys

WRITE_FILE = """
void write_file(char* name, const uint8_t *data, uint64_t size) {
    FILE* out = fopen(name, "wb");
    uint64_t written = 0;
    while (written < size) {
        uint64_t w = fwrite(data + written, 1, size - written, out);
        if (w == 0) {
            return;
        }
        written += w;
    }
    fclose(out);
}
"""

def create_test(obj_name, cmp_name, fmt, files, executable):
    if os.path.exists(f"out{obj_name}-{cmp_name}"):
        shutil.rmtree(f"out{obj_name}-{cmp_name}")
    os.mkdir(f"out{obj_name}-{cmp_name}")
    with open(f"test_{obj_name}.c", 'w') as file:
        file.write(f'#include "assets{obj_name}.h"\n#include <stdio.h>\n\n')
        file.write(WRITE_FILE)
        file.write('int main() {\n')
        for f in files:
            nam = os.path.split(f)[1]
            symbol = nam.replace('.', '_')
            file.write(f'    write_file("out{obj_name}-{cmp_name}/{nam}", {symbol}, {symbol}_size);\n')
        file.write('return 0;\n}\n')

    cmd = [executable, f"-oassets{obj_name}-{cmp_name}.obj", f"-hassets{obj_name}-{cmp_name}.h", f"-f{fmt}"] + files
    subprocess.run(cmd)


def main():
    files = glob.glob("assets/**/*")
    if len(sys.argv) > 2 and sys.argv[1] == '--create':
        create_test("Coff64", sys.argv[2], "coff64", files, f"./embed-{sys.argv[2]}")
        create_test("Coff32", sys.argv[2], "coff32", files, f"./embed-{sys.argv[2]}")
        create_test("Elf64", sys.argv[2], "elf64", files, f"./embed-{sys.argv[2]}")
        create_test("Elf32", sys.argv[2], "elf32", files, f"./embed-{sys.argv[2]}")
        return

    if len(sys.argv) > 1:
        if sys.argv[1] == '--create':
            print("Missing compiler argument")
            return
        target = sys.argv[1]
    else:
        return
    res = glob.glob(f"out{target}/*")

    for file in files:
        name = os.path.split(file)[1]
        assert os.path.join(f"out{target}", name) in res, f"{file} does not exist in output"
        with open(file, 'rb') as f1:
            d1 = f1.read()
        with open(os.path.join(f"out{target}", name), 'rb') as f2:
            d2 = f2.read()
        assert d1 == d2, f"Data in {file} differs"
        print(file, "correct")
    print("Passed all tests")

if __name__ == "__main__":
    main()
