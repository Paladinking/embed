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

TARGETS = {
    'Coff64' : ['VC64', 'MINGW'],
    'Coff32' : ['VC86'],
    'Elf64' : ['GCC64'],
    'Elf32' : ['GCC32']
}

MAKE_CC = {
    'VC64': lambda src, obj, out: ['cl', src] + ([obj] if obj else []) + ['/nologo', f'/Fe:{out}.exe'],
    'VC86': lambda src, obj, out: ['cl', src] + ([obj] if obj else []) + ['/nologo', f'/Fe:{out}.exe'],
    'MINGW': lambda src, obj, out: ['gcc', src] + ([obj] if obj else []) + ['-o', f'{out}.exe'],
    'GCC64': lambda src, obj, out:  ['wsl', 'gcc', src] + ([obj] if obj else []) + ['-o', f'{out}'],
    'GCC32': lambda src, obj, out:  ['wsl', 'gcc', '-m32', src] + ([obj] if obj else []) + ['-o', f'{out}']
}

COFF64_TARGETS = ['MSVC', 'MINGW']

def create_test(obj_name, cmp_name, fmt, files, executable):
    for target in TARGETS[obj_name]:
        if os.path.exists(f"out{obj_name}-{cmp_name}-{target}"):
            shutil.rmtree(f"out{obj_name}-{cmp_name}-{target}")
        os.mkdir(f"out{obj_name}-{cmp_name}-{target}")
        with open(f"test_{obj_name}-{cmp_name}-{target}.c", 'w') as file:
            file.write(f'#include "assets{obj_name}-{cmp_name}-{target}.h"\n#include <stdio.h>\n\n')
            file.write(WRITE_FILE)
            file.write('int main() {\n')
            for f in files:
                nam = os.path.split(f)[1]
                symbol = nam.replace('.', '_')
                file.write(f'    write_file("out{obj_name}-{cmp_name}-{target}/{nam}", {symbol}, {symbol}_size);\n')
            file.write('return 0;\n}\n')

        cmd = [executable, f"-oassets{obj_name}-{cmp_name}-{target}.obj", f"-hassets{obj_name}-{cmp_name}-{target}.h", f"-f{fmt}"] + files
        subprocess.run(cmd)


def main():
    files = glob.glob("assets/**/*")
    if len(sys.argv) > 2 and sys.argv[1] == '--create':
        cmd = MAKE_CC[sys.argv[2]]('../embed.c', '', f'embed-{sys.argv[2]}')
        print(cmd)
        subprocess.run(cmd)
        create_test("Coff64", sys.argv[2], "coff64", files, f"./embed-{sys.argv[2]}")
        create_test("Coff32", sys.argv[2], "coff32", files, f"./embed-{sys.argv[2]}")
        create_test("Elf64", sys.argv[2], "elf64", files, f"./embed-{sys.argv[2]}")
        create_test("Elf32", sys.argv[2], "elf32", files, f"./embed-{sys.argv[2]}")
        return

    if len(sys.argv) > 2 and sys.argv[1] == '--run':

        target = sys.argv[1]
        cmp = sys.argv[2]
    else:
        return
    res = glob.glob(f"out{target}-{cmp}/*")

    for file in files:
        name = os.path.split(file)[1]
        assert os.path.join(f"out{target}-{cmp}", name) in res, f"{file} does not exist in output"
        with open(file, 'rb') as f1:
            d1 = f1.read()
        with open(os.path.join(f"out{target}-{cmp}", name), 'rb') as f2:
            d2 = f2.read()
        assert d1 == d2, f"Data in {file} differs"
    print("Passed all tests")

if __name__ == "__main__":
    main()
