#define _CRT_SECURE_NO_DEPRECATE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
typedef wchar_t* Filename_t;
#define OPEN(filename, mode) _wfopen(filename, L##mode)
#define PERROR(format, ...) fwprintf(stderr, L##format, __VA_ARGS__)
#define REMOVE(filename) _wremove(filename)
#define ENTRY wmain
#define CHAR wchar_t
#define LTR(c) L##c
#define STRLEN wcslen
#define STRCHR(s, l) wcschr(s, L##l)
#define realpath(N,R) _wfullpath((R),(N),256)
#ifdef _MSC_VER
#define STR_FORMAT L"%S"
#define F_FORMAT L"%s"
#else
#define STR_FORMAT L"%s"
#define F_FORMAT L"%S"
#endif
#else
typedef char* Filename_t;
#define OPEN(filename, mode) fopen(filename, mode)
#define PERROR(...) fprintf(stderr, __VA_ARGS__)
#define REMOVE(filename) remove(filename)
#define ENTRY main
#define CHAR char
#define LTR(c) c
#define STRLEN strlen
#define STRCHR(s, l) strchr(s, l)
#define STR_FORMAT "%s"
#define F_FORMAT "%s"
#endif


#define WRITE_U32(ptr, val) do { \
    (ptr)[0] = (val) & 0xff; \
    (ptr)[1] = ((val) >> 8) & 0xff; \
    (ptr)[2] = ((val) >> 16) & 0xff; \
    (ptr)[3] = ((val) >> 24) & 0xff; \
} while (0)

#define WRITE_U64(ptr, val) do {\
    (ptr)[0] = (val) & 0xff; \
    (ptr)[1] = ((val) >> 8) & 0xff; \
    (ptr)[2] = ((val) >> 16) & 0xff; \
    (ptr)[3] = ((val) >> 24) & 0xff; \
    (ptr)[4] = ((val) >> 32) & 0xff; \
    (ptr)[5] = ((val) >> 40) & 0xff; \
    (ptr)[6] = ((val) >> 48) & 0xff; \
    (ptr)[7] = ((val) >> 56) & 0xff; \
} while (0)

#define ALIGN_DIFF(val, p) (((val) % (p) == 0) ? (0) : (p) - ((val) % (p)))
#define ALIGN_TO(val, p) (val + ALIGN_DIFF(val, p))

bool write_all(FILE* file, uint32_t size, const uint8_t* buf) {
    uint32_t written = 0;
    while (written < size) {
        uint32_t w = (uint32_t)fwrite(buf + written, 1, size - written, file);
        if (w == 0) {
            return false;
        }
        written += w;
    }
    return true;
}

bool write_all_files(FILE* out, const Filename_t* names, const uint64_t* sizes,
        uint32_t no_entries, const Filename_t outname, uint64_t header_size, uint64_t alignment) {
    for (uint32_t i = 0; i < no_entries; ++i) {
        header_size += sizes[i];
        FILE *in = OPEN(names[i], "rb");
        if (in == NULL) {
            PERROR("Could not open file " F_FORMAT LTR("\n"), names[i]);
            return false;
        }
        uint8_t size_buf[8];
        WRITE_U64(size_buf, sizes[i]);
        if (fwrite(size_buf, 1, 8, out) != 8) {
            PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
            fclose(in);
            return false;
        }
        uint32_t to_read = (uint32_t)sizes[i];
        uint8_t data_buf[4096];
        while (to_read > 0) {
            uint32_t data_chunk = (uint32_t)fread(data_buf, 1, 4096, in);
            if (data_chunk == 0) {
                PERROR("Reading from " F_FORMAT LTR(" failed\n"), names[i]);
                fclose(in);
                return false;
            }
            if (!write_all(out, data_chunk, data_buf)) {
                PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
                fclose(in);
                return false;
            }
            to_read -= data_chunk;
        }
        fclose(in);
    }
    header_size += 8 * no_entries;
    if (ALIGN_DIFF(header_size, alignment) != 0) {
        uint32_t padding = ALIGN_DIFF(header_size, alignment);
        uint8_t null_data[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
        if (fwrite(null_data, 1, padding, out) != padding) {
            PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
            return false;
        }
    }
    return true;
}

void write_c_header(const char** symbol_names, uint64_t *input_sizes, uint32_t input_count, Filename_t header, bool readonly) {
    FILE* out = OPEN(header, "w");
    if (out == NULL) {
        PERROR("Could not open " F_FORMAT LTR("\n"), header);
        return;
    }

    if (fwrite("#pragma once\n#include <stdint.h>\n\n", 1, 34, out) != 34) {
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), header);
        goto error;
    }
    for (uint32_t i = 0; i < input_count; ++i) {
        const char* format = "extern uint64_t %s_size;\nextern uint8_t %s[%llu];\n\n";
        if (readonly) {
            format = "extern const uint64_t %s_size;\nextern const uint8_t %s[%llu];\n\n";
        }
        if (fprintf(out, format, symbol_names[i], symbol_names[i], (unsigned long long)input_sizes[i]) < 0) {
            PERROR("Writing to " F_FORMAT LTR(" failed\n"), header);
            goto error;
        }
    }

    fclose(out);
    return;
error:
    fclose(out);
    REMOVE(header);
}

const uint8_t ELF64_HEADER[] = {
    0x7f, 'E', 'L', 'F', // Magic
    0x2, // Class Elf 64
    0x1, // Data encoding little endian
    0x1, // Version 1
    0x0, 0x0, // ABI System V version 0
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // Padding
    0x1, 0x0, // Type relocatable object
    62, 0x0, // Machine AMD-64
    0x1, 0x0, 0x0, 0x0, // Object version 1
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // Entry point none
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // Program header offset
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Section header offset
    0x0, 0x0, 0x0, 0x0, // Flags 0
    0x40, 0x0, // Elf header size 64
    0x0, 0x0, 0x0, 0x0, // Size and number of program headers
    0x40, 0x0, 0x5, 0x0, // Size and number of section headers
    0x4, 0x0 // Index of .shstrtab
};

const uint8_t ELF32_HEADER[] = {
    0x7f, 'E', 'L', 'F', // Magic
    0x1, // Class Elf 32
    0x1, // Data encoding little endian
    0x1, // Version 1
    0x0, 0x0, // ABI System V version 0
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // Padding
    0x1, 0x0, // Type relocatable object
    3, 0x0, // Machine I80386
    0x1, 0x0, 0x0, 0x0, // Object version 1
    0x0, 0x0, 0x0, 0x0, // Entry point none
    0x0, 0x0, 0x0, 0x0, // Program header offset
    0xFF, 0xFF, 0xFF, 0xFF, // Section header offset
    0x0, 0x0, 0x0, 0x0, // Flags 0
    0x34, 0x0, // Elf header size 52
    0x0, 0x0, 0x0, 0x0, // Size and number of program headers
    0x28, 0x0, 0x5, 0x0, // Size and number of section headers
    0x4, 0x0 // Index of .shstrtab
};

const uint8_t ELF_SHSTRTAB[] = {
    '\0',
    '.', 'r', 'd', 'a', 't', 'a', '\0',
    '.', 's', 'y', 'm', 't', 'a', 'b', '\0',
    '.', 's', 't', 'r', 't', 'a', 'b', '\0',
    '.', 's', 'h', 's', 't', 'r', 't', 'a', 'b', '\0'
};

const uint8_t ELF64_SECTION_HEADERS[] = {
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL 
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // NULL
                                            //
    0x1, 0x0, 0x0, 0x0, // .(r)data name offset
    0x1, 0x0, 0x0, 0x0, // .(r)data type PROGBITS
    0x2, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .(r)data flags A
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .(r)data address
    0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .(r)data file offset
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .(r)data size
    0x0, 0x0, 0x0, 0x0, // .(r)data link 0
    0x0, 0x0, 0x0, 0x0, // .(r)data info 0
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .(r)data alignment 1
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .(r)data entry size 0
                                            //
    0x8, 0x0, 0x0, 0x0, // .symtab name offset
    0x2, 0x0, 0x0, 0x0, // .symtab type SYMTAB
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .symtab flags 0
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .symtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .symtab file offset
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .symtab size
    0x3, 0x0, 0x0, 0x0, // .symtab link 0
    0x1, 0x0, 0x0, 0x0, // .symtab info 1 local symbol
    0x8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .symtab alignment 8
    0x18, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .symtab entry size 0x18

    0x10, 0x0, 0x0, 0x0, // .strtab name offset
    0x3, 0x0, 0x0, 0x0, // .strtab type STRTAB
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .strtab flags 0
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .strtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .strtab file offset
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .strtab size
    0x0, 0x0, 0x0, 0x0, // strtab link 0
    0x0, 0x0, 0x0, 0x0, // strtab info 0
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .strtab alignment 1
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .strtab entry size 0

    0x18, 0x0, 0x0, 0x0, // .shstrtab name offset
    0x3, 0x0, 0x0, 0x0, // .shstrtab type STRTAB
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .shstrtab flags 0
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .shstrtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // .shstrtab file offset
    0x22, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .shstrtab size
    0x0, 0x0, 0x0, 0x0, // shstrtab link 0
    0x0, 0x0, 0x0, 0x0, // shstrtab info 0
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // .shstrtab alignment 1
    0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 // .shstrtab entry size 0
};

const uint8_t ELF32_SECTION_HEADERS[] = {
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL 
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL
    0x0, 0x0, 0x0, 0x0, // NULL

    0x1, 0x0, 0x0, 0x0, // .(r)data name offset
    0x1, 0x0, 0x0, 0x0, // .(r)data type PROGBITS
    0x2, 0x0, 0x0, 0x0, // .(r)data flags A
    0x0, 0x0, 0x0, 0x0, // .(r)data address
    0x34, 0x0, 0x0, 0x0, // .(r)data file offset
    0xFF, 0xFF, 0xFF, 0xFF, // .(r)data size
    0x0, 0x0, 0x0, 0x0, // .(r)data link 0
    0x0, 0x0, 0x0, 0x0, // .(r)data info 0
    0x1, 0x0, 0x0, 0x0, // .(r)data alignment 1
    0x0, 0x0, 0x0, 0x0, // .(r)data entry size 0
                                            //
    0x8, 0x0, 0x0, 0x0, // .symtab name offset
    0x2, 0x0, 0x0, 0x0, // .symtab type SYMTAB
    0x0, 0x0, 0x0, 0x0, // .symtab flags 0
    0x0, 0x0, 0x0, 0x0, // .symtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, // .symtab file offset
    0xFF, 0xFF, 0xFF, 0xFF, // .symtab size
    0x3, 0x0, 0x0, 0x0, // .symtab link 0
    0x1, 0x0, 0x0, 0x0, // .symtab info 1 local symbol
    0x4, 0x0, 0x0, 0x0, // .symtab alignment 4
    0x10, 0x0, 0x0, 0x0, // .symtab entry size 0x18

    0x10, 0x0, 0x0, 0x0, // .strtab name offset
    0x3, 0x0, 0x0, 0x0, // .strtab type STRTAB
    0x0, 0x0, 0x0, 0x0, // .strtab flags 0
    0x0, 0x0, 0x0, 0x0, // .strtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, // .strtab file offset
    0xFF, 0xFF, 0xFF, 0xFF, // .strtab size
    0x0, 0x0, 0x0, 0x0, // strtab link 0
    0x0, 0x0, 0x0, 0x0, // strtab info 0
    0x1, 0x0, 0x0, 0x0, // .strtab alignment 1
    0x1, 0x0, 0x0, 0x0, // .strtab entry size 0

    0x18, 0x0, 0x0, 0x0, // .shstrtab name offset
    0x3, 0x0, 0x0, 0x0, // .shstrtab type STRTAB
    0x0, 0x0, 0x0, 0x0, // .shstrtab flags 0
    0x0, 0x0, 0x0, 0x0, // .shstrtab address 0
    0xFF, 0xFF, 0xFF, 0xFF, // .shstrtab file offset
    0x22, 0x0, 0x0, 0x0, // .shstrtab size
    0x0, 0x0, 0x0, 0x0, // shstrtab link 0
    0x0, 0x0, 0x0, 0x0, // shstrtab info 0
    0x1, 0x0, 0x0, 0x0, // .shstrtab alignment 1
    0x1, 0x0, 0x0, 0x0, // .shstrtab entry size 0
};

const uint8_t ELF64_SYMTAB[] = {
    0xFF, 0xFF, 0xFF, 0xFF, // symbol size name
    0x10, 0x0, // symbol size info GLOBAL NOTYPE, reserved 0
    0x1, 0x0, // symbol size section index
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // symbol size value
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // symbol size size
    0xFF, 0xFF, 0xFF, 0xFF, // symbol name
    0x10, 0x0, // symbo info GLOBAL NOTYPE, reserved 0
    0x1, 0x0, // symbol section index
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // symbol value
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // symbol size
};

const uint8_t ELF32_SYMTAB[] = {
    0xFF, 0xFF, 0xFF, 0xFF, // symbol size name
    0xFF, 0xFF, 0xFF, 0xFF, // symbol size value
    0x0, 0x0, 0x0, 0x0, // symbol size size
    0x10, 0x0, // symbol size info GLOBAL NOTYPE, reserved 0
    0x1, 0x0, // symbol size section index
    0xFF, 0xFF, 0xFF, 0xFF, // symbol name
    0xFF, 0xFF, 0xFF, 0xFF, // symbol value
    0x0, 0x0, 0x0, 0x0, // symbol size
    0x10, 0x0, // symbo info GLOBAL NOTYPE, reserved 0
    0x1, 0x0 // symbol section index
};

void write_elf64_header(uint64_t data_size, uint32_t no_symbols, uint64_t strtab_size, uint8_t* out) {
    memcpy(out, ELF64_HEADER, sizeof(ELF64_HEADER));
    data_size += 8 * no_symbols + sizeof(ELF64_HEADER);
    data_size = ALIGN_TO(data_size, 8);
    data_size += 24 + no_symbols * sizeof(ELF64_SYMTAB) + sizeof(ELF_SHSTRTAB) + strtab_size;
    data_size = ALIGN_TO(data_size, 8);
    WRITE_U64(out + 40, data_size);
}

void write_elf32_header(uint32_t data_size, uint32_t no_symbols, uint32_t strtab_size, uint8_t* out) {
    memcpy(out, ELF32_HEADER, sizeof(ELF32_HEADER));
    data_size += 8 * no_symbols + sizeof(ELF32_HEADER);
    data_size = ALIGN_TO(data_size, 4);
    data_size += 16 + no_symbols * sizeof(ELF32_SYMTAB) + sizeof(ELF_SHSTRTAB) + strtab_size;
    data_size = ALIGN_TO(data_size, 4);
    WRITE_U32(out + 32, data_size);
}

uint8_t* write_elf64_symtab(const char** names, const uint64_t *size, uint32_t no_symbols, uint64_t *data_size) {
    *data_size = 24 + sizeof(ELF64_SYMTAB) * no_symbols + 1;
    for (uint32_t i = 0; i < no_symbols; ++i) {
        *data_size += 2 * strlen(names[i]) + 7;
    }
    uint8_t* data = malloc(*data_size);
    uint8_t* strtab = data + 24 + sizeof(ELF64_SYMTAB) * no_symbols;
    *strtab = 0;
    uint32_t strtab_ix = 1;
    uint64_t data_ix = 0;
    memset(data, 0, 24);
    for (uint32_t i = 0; i < no_symbols; ++i) {
        uint8_t* base = data + 24 + sizeof(ELF64_SYMTAB) * i;
        memcpy(base, ELF64_SYMTAB, sizeof(ELF64_SYMTAB));
        uint32_t name_len = (uint32_t) strlen(names[i]);
        memcpy(strtab + strtab_ix, names[i], name_len);
        memcpy(strtab + strtab_ix + name_len, "_size", 6);
        memcpy(strtab + strtab_ix + name_len + 6, names[i], name_len + 1);
        WRITE_U32(base, strtab_ix);
        WRITE_U32(base + 24, strtab_ix + name_len + 6);
        strtab_ix += 2 * name_len + 7;
        WRITE_U64(base + 8, data_ix);
        WRITE_U64(base + 24 + 8, data_ix + 8);
        data_ix += 8 + size[i];
    }
    return data;
}

uint8_t* write_elf32_symtab(const char** names, const uint64_t *size, uint32_t no_symbols, uint64_t *data_size) {
    *data_size = 16 + sizeof(ELF32_SYMTAB) * no_symbols + 1;
    for (uint32_t i = 0; i < no_symbols; ++i) {
        *data_size += 2 * strlen(names[i]) + 7;
    } 
    uint8_t* data= malloc(*data_size);
    uint8_t* strtab = data + 16 + sizeof(ELF32_SYMTAB) * no_symbols;
    *strtab = 0;
    uint32_t strtab_ix = 1;
    uint32_t data_ix = 0;
    memset(data, 0, 16);
    for (uint32_t i = 0; i < no_symbols; ++i) {
        uint8_t* base = data + 16 + sizeof(ELF32_SYMTAB) * i;
        memcpy(base, ELF32_SYMTAB, sizeof(ELF32_SYMTAB));
        uint32_t name_len = (uint32_t) strlen(names[i]);
        memcpy(strtab + strtab_ix, names[i], name_len);
        memcpy(strtab + strtab_ix + name_len, "_size", 6);
        memcpy(strtab + strtab_ix + name_len + 6, names[i], name_len + 1);
        WRITE_U32(base, strtab_ix);
        WRITE_U32(base + 16, strtab_ix + name_len + 6);
        strtab_ix += 2 * name_len + 7;
        WRITE_U32(base + 4, data_ix);
        WRITE_U32(base + 16 + 4, data_ix + 8);
        data_ix += 8 + size[i];
    }
    return data;
}

void write_elf64_section_headers(uint64_t data_size, uint32_t no_symbols, uint64_t strtab_size, bool readonly, uint8_t* out) {
    memcpy(out, ELF64_SECTION_HEADERS, sizeof(ELF64_SECTION_HEADERS));
    if (!readonly) {
        out[72] |= 1;
    }
    data_size += 8 * no_symbols;
    WRITE_U64(out + 64 + 32, data_size);
    data_size = ALIGN_TO(data_size, 8);
    data_size += 0x40;
    WRITE_U64(out + 2 * 64 + 24, data_size);
    WRITE_U64(out + 2 * 64 + 32, (uint64_t)(24 + sizeof(ELF64_SYMTAB) * no_symbols));
    data_size += 24 + sizeof(ELF64_SYMTAB) * no_symbols;
    WRITE_U64(out + 3 * 64 + 24, data_size);
    WRITE_U64(out + 3 * 64 + 32, strtab_size);
    data_size += strtab_size;
    WRITE_U64(out + 4 * 64 + 24, data_size);
}

void write_elf32_section_headers(uint32_t data_size, uint32_t no_symbols, uint32_t strtab_size, bool readonly, uint8_t* out) {
    memcpy(out, ELF32_SECTION_HEADERS, sizeof(ELF32_SECTION_HEADERS));
    if (!readonly) {
        out[48] |= 1;
    }
    data_size += 8 * no_symbols;
    WRITE_U32(out + 40 + 20, data_size);
    data_size = ALIGN_TO(data_size, 4);
    data_size += 0x34;
    WRITE_U32(out + 2 * 40 + 16, data_size);
    WRITE_U32(out + 2 * 40 + 20, 16 + sizeof(ELF32_SYMTAB) * no_symbols);
    data_size += 16 + sizeof(ELF32_SYMTAB) * no_symbols;
    WRITE_U32(out + 3 * 40 + 16, data_size);
    WRITE_U32(out + 3 * 40 + 20, strtab_size);
    data_size += strtab_size;
    WRITE_U32(out + 4 * 40 + 16, data_size);
}


bool write_elf(const char** names, const Filename_t* files, const uint64_t* size,
        const uint32_t no_symbols, const Filename_t outname, bool readonly, bool elf32) {
    FILE *out = OPEN(outname, "wb");
    if (out == NULL) {
        PERROR("Could not create file " F_FORMAT LTR("\n"), outname);
        return false;
    }

    uint64_t data_size = 0;
    uint64_t strtab_size = 1;
    for (uint32_t i = 0; i < no_symbols; ++i) {
        data_size += size[i];
        strtab_size += 2 * strlen(names[i]) + 7;
        if (strtab_size > UINT32_MAX) {
            PERROR("" STR_FORMAT, "Total length of all symbol names to large\n");
            goto error;
        }
        if (elf32 && data_size > UINT32_MAX) {
            PERROR("" STR_FORMAT, "Elf32 object does not fit all data\n");
            goto error;
        }
    }

    uint8_t header[sizeof(ELF64_HEADER)];
    uint32_t hsize;
    if (elf32) {
        hsize = sizeof(ELF32_HEADER);
        write_elf32_header(data_size, no_symbols, strtab_size, header);
    } else {
        hsize = sizeof(ELF64_HEADER);
        write_elf64_header(data_size, no_symbols, strtab_size, header);
    }
    if (!write_all(out, hsize, header)) {
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
        goto error;
    }

    if (!write_all_files(out, files, size, no_symbols, outname, hsize, elf32 ? 4 : 8)) {
        goto error;
    }
    uint64_t symtab_size;
    uint8_t *symtab;
    if (elf32) {
        symtab = write_elf32_symtab(names, size, no_symbols, &symtab_size);
    } else {
        symtab = write_elf64_symtab(names, size, no_symbols, &symtab_size);
    }
    uint8_t shstrtab[sizeof(ELF_SHSTRTAB)];
    memcpy(shstrtab, ELF_SHSTRTAB, sizeof(ELF_SHSTRTAB));
    if (!readonly) {
        memcpy(shstrtab + 1, ".data", 6);
    }
    if (!write_all(out, symtab_size, symtab) || !write_all(out, sizeof(ELF_SHSTRTAB), shstrtab)) {
        free(symtab);
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
        goto error;
    }
    free(symtab);
    uint64_t fsize = symtab_size + sizeof(ELF_SHSTRTAB);
    uint32_t align = elf32 ? 4 : 8;
    if (ALIGN_DIFF(fsize, align) != 0) {
        uint8_t padding[] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (fwrite(padding, 1, ALIGN_DIFF(fsize, align), out) != ALIGN_DIFF(fsize, align)) {
            PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
            goto error;
        }
    }
    uint8_t section_headers[sizeof(ELF64_SECTION_HEADERS)];
    uint32_t sh_size;
    if (elf32) {
        sh_size = sizeof(ELF32_SECTION_HEADERS);
        write_elf32_section_headers(data_size, no_symbols, strtab_size, readonly, section_headers);
    } else {
        sh_size = sizeof(ELF64_SECTION_HEADERS);
        write_elf64_section_headers(data_size, no_symbols, strtab_size, readonly, section_headers);
    }
    if (!write_all(out, sh_size, section_headers)) {
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
        goto error;
    }

    fclose(out);
    return true;
error:
    fclose(out);
    REMOVE(outname);
    return false;
}

const uint8_t COFF_HEADER[] = {
    0x64, 0x86, // Machine x64
    0x1, 0x0, // NumberOfSections 1
    0x0, 0x0, 0x0, 0x0, // Timestamp 0
    0xFF, 0xFF, 0xFF, 0xFF, // Coff table file offset
    0xFF, 0xFF, 0xFF, 0xFF, // Number of symbols
    0x0, 0x0, // Size of optional header 0
    0x0, 0x0 // Characteristics 0
};

const uint8_t COFF_SECTION_HEADER[] = {
    '.', 'r', 'd', 'a', 't', 'a', 0x0, 0x0, // Name
    0x0, 0x0, 0x0, 0x0, // VirtualSize
    0x0, 0x0, 0x0, 0x0, // VirtualAdress
    0xFF, 0xFF, 0xFF, 0xFF, // SizeOfRawData
    0x3c, 0x0, 0x0, 0x0, // PointerToRawData
    0x0, 0x0, 0x0, 0x0, // PointerToRelocations
    0x0, 0x0, 0x0, 0x0, // PointerToLinenumbers
    0x0, 0x0, // NumberOfRelocations
    0x0, 0x0, // NumberOfLinenumbers
    0x40, 0x0, 0x10, 0x40 // Characteristics Initialized, 1-byte aligned, read
};

const uint8_t COFF_SYMBOL_ENTRY[] = {
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, // name
    0x0, 0x0, 0x0, 0x0, // offset
    0x1, 0x0, // section number
    0x0, 0x0, // type
    0x2, // storage class
    0x0 // Aux symbols
};


void write_coff_header(uint32_t data_size, uint8_t* data, uint32_t no_symbols, bool coff32) {
   memcpy(data, COFF_HEADER, sizeof(COFF_HEADER));
   data_size = data_size + 8 * no_symbols;
   if ((20 + 40 + data_size) % 8 != 0) {
       data_size += 8 - ((20 + 40 + data_size) % 8);
   }
   uint32_t coff_offset = 20 + 40 + data_size;
   WRITE_U32(data + 8, coff_offset);
   WRITE_U32(data + 12, no_symbols * 2);
   if (coff32) {
       data[0] = 0x4C;
       data[1] = 0x01;
   }
}

void write_coff_section_header(uint32_t data_size, uint32_t no_symbols, bool readonly, uint8_t* data) {
    memcpy(data, COFF_SECTION_HEADER, sizeof(COFF_SECTION_HEADER));
    if (!readonly) {
        memcpy(data, ".data", 6);
        data[39] |= 0x80;
    }
    data_size = data_size + 8 * no_symbols;
    data[16] = data_size & 0xff;
    data[17] = (data_size >> 8) & 0xff;
    data[18] = (data_size >> 16) & 0xff;
    data[19] = (data_size >> 24) & 0xff;
}

uint8_t* write_coff_symbol_table(const char** names, const uint64_t *size, uint32_t no_symbols, bool coff32, uint32_t *data_size) {
    *data_size = 2 * sizeof(COFF_SYMBOL_ENTRY) * no_symbols + 4;
    for (uint32_t i = 0; i < no_symbols; ++i) {
        uint32_t name_len = (uint32_t)strlen(names[i]);
        if (coff32) {
            ++name_len;
        }
        if (name_len > 3) {
            if (name_len > 8) {
                *data_size += name_len * 2 + 7;
            } else {
                *data_size += name_len + 6;
            }
        }
    }

    uint8_t *data = malloc(*data_size);
    uint8_t *strtable = data + 2 * sizeof(COFF_SYMBOL_ENTRY) * no_symbols;
    uint32_t strtable_offset = 0x4;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < no_symbols; ++i) {
        uint32_t name_len = (uint32_t)strlen(names[i]);
        if (coff32) {
            ++name_len;
        }
        uint32_t data_offset = 2 * sizeof(COFF_SYMBOL_ENTRY) * i;
        memcpy(data + data_offset, COFF_SYMBOL_ENTRY, sizeof(COFF_SYMBOL_ENTRY));
        memcpy(data + data_offset + sizeof(COFF_SYMBOL_ENTRY), COFF_SYMBOL_ENTRY, sizeof(COFF_SYMBOL_ENTRY));
        if (name_len > 3) {
            if (coff32) {
                *(strtable + strtable_offset) = '_';
                memcpy(strtable + strtable_offset + 1, names[i], name_len - 1);
            } else {
                memcpy(strtable + strtable_offset, names[i], name_len);
            }
            memcpy(strtable + strtable_offset + name_len, "_size", 6);
            WRITE_U32(data + data_offset + 4, strtable_offset);
            strtable_offset += name_len + 6;
        } else {
            if (coff32) {
                *(data + data_offset) = '_';
                memcpy(data + data_offset + 1, names[i], name_len - 1);
            } else {
                memcpy(data + data_offset, names[i], name_len);
            }
            memcpy(data + data_offset + name_len, "_size", 5);
        }
        WRITE_U32(data + data_offset + 8, offset);
        if (name_len > 8) {
            if (coff32) {
                *(strtable + strtable_offset) = '_';
                memcpy(strtable + strtable_offset + 1, names[i], name_len);
            } else {
                memcpy(strtable + strtable_offset, names[i], name_len + 1);
            }
            WRITE_U32(data + data_offset + sizeof(COFF_SYMBOL_ENTRY) + 4, strtable_offset);
            strtable_offset += name_len + 1;
        } else {
            if (coff32) {
                *(data + data_offset + sizeof(COFF_SYMBOL_ENTRY)) = '_';
                memcpy(data + data_offset + sizeof(COFF_SYMBOL_ENTRY) + 1, names[i], name_len - 1);
            } else {
                memcpy(data + data_offset + sizeof(COFF_SYMBOL_ENTRY), names[i], name_len);
            }
        }
        WRITE_U32(data + data_offset + sizeof(COFF_SYMBOL_ENTRY) + 8, offset + 8);
        offset += (uint32_t)size[i] + 8;
    }
    WRITE_U32(strtable, strtable_offset);
    return data;
}

bool write_coff(const char** names, const Filename_t* files, const uint64_t* size,
        const uint32_t no_symbols, const Filename_t outname, bool readonly, bool coff32) {
    FILE* out = OPEN(outname, "wb");
    if (out == NULL) {
        PERROR("Could not create file " F_FORMAT LTR("\n"), outname);
        return false;
    }

    uint8_t header[sizeof(COFF_HEADER) + sizeof(COFF_SECTION_HEADER)];
    uint64_t full_size = 0;
    for (uint64_t i = 0; i < no_symbols; ++i) {
        full_size += size[i];
        if (full_size > INT32_MAX) {
            PERROR("" STR_FORMAT, "COFF-objects only support up to 2 GB of data\n");
            return false;
        }
    }
    write_coff_header(full_size, header, no_symbols, coff32);
    write_coff_section_header(full_size, no_symbols, readonly, header + sizeof(COFF_HEADER));
    if (fwrite(header, 1, sizeof(header), out) != sizeof(header)) {
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
        goto error;
    }
    if (!write_all_files(out, files, size, no_symbols, outname, sizeof(COFF_HEADER) + sizeof(COFF_SECTION_HEADER), 8)) {
        goto error;
    }

    uint32_t symbol_table_size;
    uint8_t *symbol_table_buf = write_coff_symbol_table(names, size, no_symbols, coff32, &symbol_table_size);
    if (!write_all(out, symbol_table_size, symbol_table_buf)) {
        PERROR("Writing to " F_FORMAT LTR(" failed\n"), outname);
        goto error;
    }
    fclose(out);
    free(symbol_table_buf);

    return true;
error:
    fclose(out);
    REMOVE(outname);
    return false;
}

char* to_symbol(const CHAR* ptr, bool replace_dot, bool format) {
    size_t len = STRLEN(ptr);
    char* res = malloc(len + 1);
    for (size_t ix = 0; ix < len; ++ix) {
        if (ptr[ix] == LTR('.') && replace_dot) {
            res[ix] = '_';
        } else if ((ptr[ix] == LTR('.') && replace_dot) || ptr[ix] == LTR('_')) {
            res[ix] = '_';
        } else if (ptr[ix] >= LTR('a') && ptr[ix] <= LTR('z')) {
            res[ix] = (char)('a' + (ptr[ix] - LTR('a')));
        } else if (ptr[ix] >= LTR('A') && ptr[ix] <= LTR('Z')) {
            res[ix] = (char)('A' + (ptr[ix] - LTR('A')));
        } else if (ptr[ix] >= LTR('0') && ptr[ix] <= LTR('9')) {
            res[ix] = (char)('0' + (ptr[ix] - LTR('0')));
        } else if (format && ptr[ix] == LTR('%')) { 
            if (ptr[ix + 1] != LTR('f') && ptr[ix + 1] != LTR('d') && 
                    ptr[ix + 1] != LTR('n') && ptr[ix + 1] != LTR('e') && ptr[ix + 1] != LTR('x')) {
                return NULL;
            }
            res[ix] = '%';
        } else {
            free(res);
            return NULL;
        }
    }
    res[len] = '\0';
    return res;
}

char* get_symbol(const char* format, const Filename_t filename, uint32_t n) {
    Filename_t f = realpath(filename, NULL);
    Filename_t file = f;
    char* dirs[10];
    char* ext = NULL;
    char* base = NULL;
    uint32_t dir_count = 0;
    while (file != NULL) {
        CHAR* c = STRCHR(file, '/');
#ifdef _WIN32
        CHAR* c2 = STRCHR(file, '\\');
        c = (c2 < c || c == NULL) ? c2 : c;
#endif
        if (c == NULL) {
            break;
        }
        CHAR old = *c;
        if (dir_count == 10) {
            memmove(dirs, dirs + 1, 9 * sizeof(char*));
        } else {
            ++dir_count;
        }
        *c = LTR('\0');
        dirs[10 - dir_count] = to_symbol(file, true, false);
        file = c + 1;
        *c = old;
    }
    memmove(dirs, dirs + 10 - dir_count, dir_count * sizeof(char*));
    for (int i = dir_count; i < 10; ++i) {
        dirs[i] = NULL;
    }
    if (file != NULL) {
        uint32_t len = (uint32_t)STRLEN(file);
        for (uint32_t ix = len - 1;; --ix) {
            if (ix == 0) {
                ext = malloc(1);
                *ext = '\0';
                base = to_symbol(file, true, false);
                break;
            } else if (file[ix] == LTR('.')) {
                ext = to_symbol(file + ix + 1, false, false);
                file[ix] = LTR('\0');
                base = to_symbol(file, true, false);
                file[ix] = LTR('.');
                break;
            }
        }
    }
    char* res = NULL;
    char* dest = malloc(10);
    uint32_t cap = 10;
    size_t ix = 0;
    for (size_t i = 0; format[i] != 0; ++i) {
        if (format[i] != '%') {
            if (ix == cap) {
                dest = realloc(dest, cap * 2);
                cap *= 2;
            }
            dest[ix++] = format[i];
        } else {
            ++i;
            size_t len;
            char* b = "";
            if (format[i] == 'n') {
                char buf[32];
                b = buf;
                len = sprintf(buf, "%u", n);
            } else if (format[i] == 'f') {
                if (base == NULL) {
                    free(dest);
                    goto cleanup;
                }
                len = strlen(base);
                b = base;
            } else if (format[i] == 'e' || format[i] == 'x') {
                if (ext == NULL) {
                    free(dest);
                    goto cleanup;
                }
                len = strlen(ext);
                b = ext;
                if (len > 0 && format[i] == 'e') {
                    if (ix == cap) {
                        dest = realloc(dest, cap * 2);
                        cap *= 2;
                    }
                    dest[ix++] = '_';
                }
            } else if (format[i] == 'd') {
                int dir_ix = 0;
                if (format[i + 1] >= '0' && format[i + 1] <= '9') {
                    dir_ix = format[i + 1] - '0';
                    ++i;
                }
                if (dirs[dir_ix] == NULL) {
                    // Missing directory is not an error
                    continue;
                }
                len = strlen(dirs[dir_ix]);
                b = dirs[dir_ix];
            } else {
                len = 0;
            }
            while (ix + len >= cap) {
                dest = realloc(dest, cap * 2);
                cap *= 2;
            }
            memcpy(dest + ix, b, len);
            ix += len;
        }
    }
    if (ix == cap) {
        dest = realloc(dest, ix + 1);
    }
    dest[ix] = '\0';
    res = dest;
cleanup:
    free(f);
    free(ext);
    free(base);
    for (uint32_t i = 0; i < dir_count; ++i) {
        free(dirs[i]);
    }
    return res;
}

enum Format {
    COFF64, COFF32, ELF64, ELF32, NONE
};


int ENTRY(int argc, CHAR** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);
#endif
    int status = 1;
    if (argc < 2) {
#ifdef _WIN32
#ifdef _WIN64
#ifdef _MSC_VER
        PERROR("" STR_FORMAT, "x64-windows MSVC build\n");
#else
        PERROR("" STR_FORMAT, "x64-windows MinGW build\n");
#endif
#else
#ifdef _MSC_VER
        PERROR("" STR_FORMAT, "x86-windows MSVC build\n");
#else
        PERROR("" STR_FORMAT, "x86-windows MinGW build\n");
#endif
#endif
#else
#ifdef __x86_64__
        PERROR("" STR_FORMAT, "x64 GCC build\n");
#else
        PERROR("" STR_FORMAT, "x86 GCC build\n");
#endif
#endif
        return 1;
    }

    Filename_t outname = NULL;
    Filename_t header = NULL;
    Filename_t* input_names = malloc(argc * sizeof(Filename_t));
    char** symbol_names = malloc(argc * sizeof(char*));
    uint64_t *input_sizes = malloc(argc * sizeof(uint64_t));
    char* format = NULL;
    enum Format out_format = NONE;
    bool readonly = true;

    uint32_t input_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == LTR('-')) {
            uint32_t ix = 2;
            CHAR val = argv[i][1];
            if (val != LTR('o') && val != LTR('s') && val != LTR('f') && val != LTR('w') && val != LTR('h')) {
                PERROR("Unkown flag " F_FORMAT LTR("\n"), argv[i]);
                goto cleanup;
            }
            if (val != LTR('w') && val != LTR('\0') && argv[i][2] == LTR('\0')) {
                ++i;
                ix = 0;
                if (i == argc) {
                    PERROR("No value specified for " F_FORMAT LTR("\n"), argv[i - 1]);
                    goto cleanup;
                }
            }
            if (val == LTR('o')) {
                if (outname != NULL) {
                    PERROR("" STR_FORMAT, "Extra output files specified\n");
                    goto cleanup;
                }
                uint32_t len = 1 + (uint32_t)STRLEN(argv[i] + ix);
                outname = malloc(len * sizeof(CHAR));
                memcpy(outname, argv[i] + ix, len * sizeof(CHAR));
            } else if (val == LTR('s')) {
                if (format != NULL) {
                    PERROR("" STR_FORMAT, "Multiple symbol formats specified\n");
                    goto cleanup;
                }
                format = to_symbol(argv[i] + ix, false, true);
                if (format == NULL) {
                    PERROR("Invalid symbol format " F_FORMAT LTR("\n"), argv[i] + ix);
                    goto cleanup;
                }
            } else if (val == LTR('f')) {
                if (out_format != NONE) {
                    PERROR("" STR_FORMAT, "Multiple object formats specified\n");
                    goto cleanup;
                }
                char* format = to_symbol(argv[i] + ix, false, false);
                if (format != NULL) {
                    if (strcmp(format, "coff64") == 0) {
                        out_format = COFF64;
                    } else if (strcmp(format, "elf64") == 0) {
                        out_format = ELF64;
                    } else if (strcmp(format, "coff32") == 0) {
                        out_format = COFF32;
                    } else if (strcmp(format, "elf32") == 0) {
                        out_format = ELF32;
                    }
                    free(format);
                }
                if (out_format == NONE) {
                    PERROR("Unsupported object format " F_FORMAT LTR("\n"), argv[i] + ix);
                    goto cleanup;
                }
            } else if (val == LTR('w')) {
                readonly = false;
            } else if (val == LTR('h')) {
                if (header != NULL) {
                    PERROR("" STR_FORMAT, "Multiple header files specified\n");
                    goto cleanup;
                }
                header = argv[i] + ix;
            }
        } else {
            CHAR *sep = STRCHR(argv[i], ':');
            if (sep != NULL) {
                uint32_t len = (uint32_t)(sep - argv[i]);
                input_names[input_count] = malloc((len + 1) * sizeof(CHAR));
                memcpy(input_names[input_count], argv[i], len * sizeof(CHAR));
                input_names[input_count][len] = LTR('\0');
                *sep = LTR('\0');
                CHAR* symbol_name_root = sep + 1;
                if (*symbol_name_root == LTR('\0')) {
                    PERROR("Invalid argument " F_FORMAT LTR("\n"), argv[i]);
                    goto cleanup;
                }
                symbol_names[input_count] = to_symbol(symbol_name_root, false, false);
                if (symbol_names[input_count] == NULL) {
                    PERROR("Invalid symbol name " F_FORMAT LTR("\n"), symbol_name_root);
                    goto cleanup;
                }
            } else {
                uint32_t len = (uint32_t)STRLEN(argv[i]);
                input_names[input_count] = malloc((len + 1) * sizeof(CHAR));
                memcpy(input_names[input_count], argv[i], (len + 1) * sizeof(CHAR));
                symbol_names[input_count] = NULL;
            }
            ++input_count;
        }
    }
    if (input_count == 0) {
        PERROR("" STR_FORMAT, "No input files specified\n");
        goto cleanup;
    }
    if (out_format == NONE) {
#ifdef _WIN32
    #ifdef _WIN64
        out_format = COFF64;
    #else
        out_format = COFF32;
    #endif
#else
    #ifdef __x86_64__
        out_format = ELF64;
    #else
        out_format = ELF32;
    #endif
#endif
    }
    if (outname == NULL) {
        outname = malloc(10 * sizeof(CHAR));
        if (out_format == COFF64) {
            memcpy(outname, LTR("embed.obj"), 10 * sizeof(CHAR));
        } else {
            memcpy(outname, LTR("embed.o"), 8 * sizeof(CHAR));
        }
    }
    if (format == NULL) {
        format = malloc(5);
        memcpy(format, "%f%e", 5);
    }

    for (uint32_t i = 0; i < input_count; ++i) {
        FILE *f = OPEN(input_names[i], "rb");
        long size;
        if (f == NULL || fseek(f, 0, SEEK_END) != 0) {
            PERROR("Failed to open input file " F_FORMAT LTR("\n"), input_names[i]);
            if (f != NULL) {
                fclose(f);
            }
            goto cleanup;
        }
        if ((size = ftell(f)) < 0) {
            // This is likely the case on windows. COFF is limited to 4GB anyways...
            PERROR("File " F_FORMAT LTR(" to large\n"), input_names[i]);
            fclose(f);
            goto cleanup;
        }
        input_sizes[i] = size;
        fclose(f);
    }  

    for (uint32_t i = 0; i < input_count; ++i) {
        if (symbol_names[i] == NULL) {
            symbol_names[i] = get_symbol(format, input_names[i], i);
            if (symbol_names[i] == NULL) {
                PERROR("Format gives invalid symbol for " F_FORMAT LTR("\n"), input_names[i]);
                goto cleanup;
            }
        }
    }

    for (uint32_t i = 0; i < input_count; ++i) {
        for (uint32_t j = i + 1; j < input_count; ++j) {
            if (strcmp(symbol_names[i], symbol_names[j]) == 0) {
                PERROR("Duplicate symbol name " STR_FORMAT LTR("\n"), symbol_names[i]);
                goto cleanup;
            }
        }
    }

    for (uint32_t i = 0; i < input_count; ++i) {
        PERROR("File: " F_FORMAT LTR(", symbol: ") STR_FORMAT LTR(", size: %llu\n"), input_names[i], symbol_names[i], 
                (unsigned long long)input_sizes[i]);
    }

    if (out_format == COFF64 || out_format  == COFF32) {
        write_coff((const char**)symbol_names, input_names, input_sizes, input_count, outname, readonly, out_format == COFF32);
    } else {
        write_elf((const char**)symbol_names, input_names, input_sizes, input_count, outname, readonly, out_format == ELF32);
    }
    if (header != NULL) {
        write_c_header((const char**)symbol_names, input_sizes, input_count, header, readonly);
    }

    status = 0;
cleanup:
    free(outname);
    for (uint32_t i = 0; i < input_count; ++i) {
        free(input_names[i]);
        free(symbol_names[i]);
    }
    free(input_names);
    free(format);
    free(symbol_names);
    free(input_sizes);
    return status;
}
