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
ALL_TARGETS = ['VC64', 'VC86', 'MINGW', 'GCC64', 'GCC32', 'CLANG64', 'CLANG32']

TARGETS = {
    'coff64' : ['VC64', 'MINGW'],
    'coff32' : ['VC86'],
    'elf64' : ['GCC64', 'CLANG64'],
    'elf32' : ['GCC32', 'CLANG32']
}

DIR = os.path.split(os.getcwd())[0]
DOCKER_BASE = ['docker', 'run', '-v', DIR + ':/home/me/embed', 'clang']

MAKE_CC = {
    'VC64': lambda src, obj, out: (['cl', src] + ([obj] if obj else []) + ['/nologo', f'/Fe:{out}.exe'], [f'{out}.exe']),
    'VC86': lambda src, obj, out: (['cl', src] + ([obj] if obj else []) + ['/nologo', f'/Fe:{out}.exe'], [f'{out}.exe']),
    'MINGW': lambda src, obj, out: (['gcc', src] + ([obj] if obj else ['-municode']) + ['-o', f'{out}.exe'], [f'{out}.exe']),
    'GCC64': lambda src, obj, out: (DOCKER_BASE + ['gcc', src] + ([obj] if obj else []) + ['-o', f'{out}'], DOCKER_BASE + [f'./{out}']),
    'GCC32': lambda src, obj, out: (DOCKER_BASE + ['gcc', '-m32', src] + ([obj] if obj else []) + ['-o', f'{out}'], DOCKER_BASE + [f'./{out}']),
    'CLANG64': lambda src, obj, out: (DOCKER_BASE + ['clang', src] + ([obj] if obj else []) + ['-o', out], DOCKER_BASE + [f'./{out}']),
    'CLANG32': lambda src, obj, out: (DOCKER_BASE + ['clang', '-m32', src] + ([obj] if obj else []) + ['-o', out], DOCKER_BASE + [f'./{out}']),
}

COFF64_TARGETS = ['MSVC', 'MINGW']

def create_test(obj_name, cmp_name, files, executable):
    for target in TARGETS[obj_name]:
        with open(f"test_{obj_name}-{cmp_name}-{target}.c", 'w') as file:
            file.write(f'#include "assets-{obj_name}-{cmp_name}-{target}.h"\n#include <stdio.h>\n\n')
            file.write(WRITE_FILE)
            file.write('int main() {\n')
            for f in files:
                nam = os.path.split(f)[1]
                symbol = nam.replace('.', '_')
                file.write(f'    write_file("out-{obj_name}-{cmp_name}-{target}/{nam}", {symbol}, {symbol}_size);\n')
            file.write('return 0;\n}\n')

        cmd = executable + [f"-oassets-{obj_name}-{cmp_name}-{target}.obj", f"-hassets-{obj_name}-{cmp_name}-{target}.h", f"-f{obj_name}"] + files
        subprocess.run(cmd)

def main():
    files = glob.glob("assets/**/*")
    if len(sys.argv) > 2:
        target = sys.argv[2]
    else:
        return

    if sys.argv[1] == '--create':
        cmd, exe = MAKE_CC[target]('../embed.c', '', f'embed-{target}')
        print(cmd, exe)
        subprocess.run(cmd)
        files = [file.replace('\\', '/') for file in files]
        for source in TARGETS:
            create_test(source, target, files, exe)
        return

    if sys.argv[1] == '--run':
        for source in TARGETS:
            if target in TARGETS[source]:
                for comp in ALL_TARGETS:
                    if os.path.exists(f"out-{source}-{comp}-{target}"):
                        shutil.rmtree(f"out-{source}-{comp}-{target}")
                    os.mkdir(f"out-{source}-{comp}-{target}")
                    cmd, exe = MAKE_CC[target](f'test_{source}-{comp}-{target}.c',
                                               f'assets-{source}-{comp}-{target}.obj',
                                               f'test_{source}-{comp}-{target}')
                    print(cmd)
                    subprocess.run(cmd)
                    subprocess.run(exe)
                    res = glob.glob(f"out-{source}-{comp}-{target}/*")
                    for file in files:
                        name = os.path.split(file)[1]
                        assert os.path.join(f"out-{source}-{comp}-{target}", name) in res, f"{file} does not exist in output"
                        with open(file, 'rb') as f1:
                            d1 = f1.read()
                        with open(os.path.join(f"out-{source}-{comp}-{target}", name), 'rb') as f2:
                            d2 = f2.read()
                        assert d1 == d2, f"Data in {file} differs"

        print("Passed all tests")
        return
    else:
        return


if __name__ == "__main__":
    main()
