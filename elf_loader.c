#include "elf_loader.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
armu_load_elf(const char* path, struct Armu* armu, struct ArmuElfImageInfo* out_info)
{
    if (!path || !armu || !armu->mem || armu->mem_capacity == 0) {
        fprintf(stderr, "[elf] invalid arguments to armu_load_elf\n");
        return -1;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "[elf] failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    struct ElfFileHeader ehdr;
    if (fread(&ehdr, 1, sizeof(ehdr), file) != sizeof(ehdr)) {
        fprintf(stderr, "[elf] failed to read ELF header from %s\n", path);
        fclose(file);
        return -1;
    }

    if (ehdr.magic != ELF_MAGIC || ehdr.width != ELFCLASS64 || ehdr.version != EV_CURRENT) {
        fprintf(stderr, "[elf] unsupported ELF format in %s\n", path);
        fclose(file);
        return -1;
    }

    if (ehdr.phnum == 0 || ehdr.phentsize != sizeof(struct ElfProgHeader)) {
        fprintf(stderr, "[elf] unexpected program header layout in %s\n", path);
        fclose(file);
        return -1;
    }

    struct ElfProgHeader* phdrs = calloc(ehdr.phnum, sizeof(*phdrs));
    if (!phdrs) {
        fprintf(stderr, "[elf] out of memory while processing %s\n", path);
        fclose(file);
        return -1;
    }

    if (fseek(file, (long)ehdr.phoff, SEEK_SET) != 0) {
        fprintf(stderr, "[elf] failed to seek to program headers in %s\n", path);
        free(phdrs);
        fclose(file);
        return -1;
    }

    for (uint16_t i = 0; i < ehdr.phnum; ++i) {
        if (fread(&phdrs[i], sizeof(*phdrs), 1, file) != 1) {
            fprintf(stderr, "[elf] failed to read program header %u in %s\n", (unsigned)i, path);
            free(phdrs);
            fclose(file);
            return -1;
        }
    }

    memset(armu->mem, 0, armu->mem_capacity);

    uint64_t min_vaddr = UINT64_MAX;
    uint64_t max_vaddr = 0;
    int loaded_segments = 0;

    for (uint16_t i = 0; i < ehdr.phnum; ++i) {
        const struct ElfProgHeader* ph = &phdrs[i];
        if (ph->type != PT_LOAD) {
            continue;
        }

        uint64_t seg_start = ph->vaddr;
        uint64_t seg_file_size = ph->filesz;
        uint64_t seg_mem_size = ph->memsz;

        if (seg_mem_size == 0) {
            continue;
        }

        if (seg_start >= armu->mem_capacity || seg_mem_size > armu->mem_capacity ||
            seg_start + seg_mem_size > armu->mem_capacity) {
            fprintf(stderr, "[elf] segment 0x%llx-0x%llx exceeds emulator memory (%zu bytes) in %s\n",
                    (unsigned long long)seg_start,
                    (unsigned long long)(seg_start + seg_mem_size),
                    armu->mem_capacity,
                    path);
            free(phdrs);
            fclose(file);
            return -1;
        }

        if (seg_file_size > 0) {
            if (fseek(file, (long)ph->offset, SEEK_SET) != 0) {
                fprintf(stderr, "[elf] failed to seek to segment payload in %s\n", path);
                free(phdrs);
                fclose(file);
                return -1;
            }
            if (fread(armu->mem + seg_start, 1, (size_t)seg_file_size, file) != seg_file_size) {
                fprintf(stderr, "[elf] failed to read segment payload in %s\n", path);
                free(phdrs);
                fclose(file);
                return -1;
            }
        }

        if (seg_mem_size > seg_file_size) {
            memset(armu->mem + seg_start + seg_file_size, 0, (size_t)(seg_mem_size - seg_file_size));
        }

        uint64_t seg_end = seg_start + seg_mem_size;
        if (seg_start < min_vaddr) {
            min_vaddr = seg_start;
        }
        if (seg_end > max_vaddr) {
            max_vaddr = seg_end;
        }

        loaded_segments = 1;
    }

    free(phdrs);
    fclose(file);

    if (!loaded_segments) {
        fprintf(stderr, "[elf] no PT_LOAD segments in %s\n", path);
        return -1;
    }

    if (ehdr.entry >= armu->mem_capacity) {
        fprintf(stderr, "[elf] entry point 0x%llx outside emulator memory (%zu bytes) in %s\n",
                (unsigned long long)ehdr.entry,
                armu->mem_capacity,
                path);
        return -1;
    }

    if (max_vaddr > armu->mem_capacity) {
        max_vaddr = armu->mem_capacity;
    }

    armu->program_size = (size_t)max_vaddr;
    if (out_info) {
        out_info->entry = ehdr.entry;
        out_info->min_vaddr = min_vaddr;
        out_info->max_vaddr = max_vaddr;
    }

    return 0;
}
