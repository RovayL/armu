#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "armu.h"
#include "elf_loader.h"
#include "tests.h"

#ifndef ARMU_START_STUB_PATH
#define ARMU_START_STUB_PATH "fibonacci_start.S"
#endif

#ifndef ARMU_AARCH64_COMPILER
#define ARMU_AARCH64_COMPILER "aarch64-linux-gnu-gcc"
#endif

#ifndef FIB_ELF_AVAILABLE
#define FIB_ELF_AVAILABLE 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char** environ;

static void
print_usage(const char* prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s                         Run built-in test suite\n"
            "  %s <elf>                   Run the specified AArch64 ELF image\n"
            "  %s --run-elf <elf>         Run the specified AArch64 ELF image\n"
            "  %s --run-c <source.c>      Cross-compile the C source and run the result\n"
            "Options:\n"
            "  --stack-top=<addr>         Override initial SP (hex or decimal)\n"
            "  --dump-regs                Dump general-purpose registers after execution\n"
            "  --keep-temp                Preserve temporary ELF produced by --run-c\n"
            "  -h, --help                 Show this help\n",
            prog, prog, prog, prog);
}

static bool
parse_u64(const char* text, uint64_t* out)
{
    if (!text || !out) {
        return false;
    }
    errno = 0;
    char* end = NULL;
    uint64_t value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = value;
    return true;
}

static uint64_t
default_stack_top(uint64_t high_water)
{
    uint64_t stack_top = ARMU_MEM_SIZE - 0x1000;
    if (stack_top <= high_water) {
        stack_top = ARMU_MEM_SIZE - 0x10;
    }
    stack_top &= ~UINT64_C(0xF);
    return stack_top;
}

static void
dump_registers(const struct Armu* armu)
{
    for (int i = 0; i < 31; ++i) {
        if (i % 4 == 0) {
            printf("\n");
        }
        printf("X%-2d=0x%016" PRIx64 "  ", i, armu->regs[i]);
    }
    printf("\nSP =0x%016" PRIx64 "\n", armu->regs[31]);
}

static int
run_elf_image(const char* path, uint64_t stack_top_override, bool dump_regs)
{
    struct Armu armu = {0};
    static uint8_t memory[ARMU_MEM_SIZE];
    armu.mem = memory;
    armu.mem_capacity = sizeof(memory);

    struct ArmuElfImageInfo info = {0};
    if (armu_load_elf(path, &armu, &info) != 0) {
        return 1;
    }

    armu.pc = info.entry;
    uint64_t stack_top = stack_top_override ? stack_top_override : default_stack_top(info.max_vaddr);
    stack_top &= ~UINT64_C(0xF);
    if (stack_top >= ARMU_MEM_SIZE) {
        fprintf(stderr, "[cli] requested stack top 0x%016" PRIx64 " exceeds memory size 0x%zx\n",
                stack_top, sizeof(memory));
        return 1;
    }
    armu.regs[31] = stack_top;

    armu_run(&armu);

    printf("[cli] Execution complete: X0=%" PRIu64 " (0x%016" PRIx64 "), PC=0x%016" PRIx64 "\n",
           armu.regs[0], armu.regs[0], armu.pc);

    if (dump_regs) {
        dump_registers(&armu);
    }

    if (armu.pc != armu.program_size) {
        fprintf(stderr, "[cli] warning: program terminated with PC=0x%016" PRIx64
                " (expected 0x%016zx)\n",
                armu.pc, armu.program_size);
    }

    return 0;
}

#if FIB_ELF_AVAILABLE
static int
compile_c_to_elf(const char* src_path, char* out_path, size_t out_path_len, bool keep_temp)
{
    if (!src_path || !out_path || out_path_len == 0) {
        return -1;
    }

    char tmp_template[] = "/tmp/armuXXXXXX";
    int fd = mkstemp(tmp_template);
    if (fd < 0) {
        fprintf(stderr, "[cli] mkstemp failed: %s\n", strerror(errno));
        return -1;
    }
    close(fd);
    if (!keep_temp) {
        unlink(tmp_template);
    }
    if (strlen(tmp_template) + 1 > out_path_len) {
        fprintf(stderr, "[cli] output buffer too small for temporary path\n");
        return -1;
    }
    strncpy(out_path, tmp_template, out_path_len);
    out_path[out_path_len - 1] = '\0';

    char* const args[] = {
        (char*)ARMU_AARCH64_COMPILER,
        "-Os",
        "-fno-stack-protector",
        "-fomit-frame-pointer",
        "-fno-asynchronous-unwind-tables",
        "-nostdlib",
        "-static",
        "-Wl,--nmagic",
        "-Wl,--build-id=none",
        "-Wl,-Ttext=0x400000",
        (char*)src_path,
        (char*)ARMU_START_STUB_PATH,
        "-o",
        out_path,
        NULL,
    };

    pid_t pid = 0;
    int spawn_status = posix_spawn(&pid, ARMU_AARCH64_COMPILER, NULL, NULL, args, environ);
    if (spawn_status != 0) {
        fprintf(stderr, "[cli] failed to launch %s: %s\n", ARMU_AARCH64_COMPILER, strerror(spawn_status));
        return -1;
    }

    int wait_status = 0;
    if (waitpid(pid, &wait_status, 0) < 0) {
        fprintf(stderr, "[cli] waitpid failed: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
        fprintf(stderr, "[cli] compiler exited with status %d\n", wait_status);
        return -1;
    }

    if (keep_temp) {
        printf("[cli] kept temporary ELF at %s\n", out_path);
    }

    return 0;
}
#else
static int
compile_c_to_elf(const char* src_path, char* out_path, size_t out_path_len, bool keep_temp)
{
    (void)src_path;
    (void)out_path;
    (void)out_path_len;
    (void)keep_temp;
    fprintf(stderr, "[cli] cross-compiler support not available in this build\n");
    return -1;
}
#endif

static int
handle_cli(int argc, char** argv)
{
    const char* elf_path = NULL;
    const char* source_path = NULL;
    uint64_t stack_top = 0;
    bool stack_forced = false;
    bool dump_regs = false;
    bool keep_temp = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strcmp(arg, "--run-elf") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[cli] missing argument for --run-elf\n");
                return 1;
            }
            elf_path = argv[++i];
        } else if (strcmp(arg, "--run-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[cli] missing argument for --run-c\n");
                return 1;
            }
            source_path = argv[++i];
        } else if (strncmp(arg, "--stack-top=", 12) == 0) {
            if (!parse_u64(arg + 12, &stack_top)) {
                fprintf(stderr, "[cli] invalid stack top value: %s\n", arg + 12);
                return 1;
            }
            stack_forced = true;
        } else if (strcmp(arg, "--stack-top") == 0) {
            if (i + 1 >= argc || !parse_u64(argv[i + 1], &stack_top)) {
                fprintf(stderr, "[cli] invalid stack top value\n");
                return 1;
            }
            ++i;
            stack_forced = true;
        } else if (strcmp(arg, "--dump-regs") == 0) {
            dump_regs = true;
        } else if (strcmp(arg, "--keep-temp") == 0) {
            keep_temp = true;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-' && elf_path == NULL && source_path == NULL) {
            elf_path = arg;
        } else {
            fprintf(stderr, "[cli] unknown option: %s\n", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (source_path && elf_path) {
        fprintf(stderr, "[cli] specify either an ELF or a C source, not both\n");
        return 1;
    }

    if (!source_path && !elf_path) {
        return -1; /* signal to run tests */
    }

    if (source_path) {
        char tmp_path[PATH_MAX] = {0};
        if (compile_c_to_elf(source_path, tmp_path, sizeof(tmp_path), keep_temp) != 0) {
            return 1;
        }
        int rc = run_elf_image(tmp_path, stack_forced ? stack_top : 0, dump_regs);
        if (!keep_temp) {
            unlink(tmp_path);
        }
        return rc;
    }

    return run_elf_image(elf_path, stack_forced ? stack_top : 0, dump_regs);
}

int
main(int argc, char** argv)
{
    int cli_status = handle_cli(argc, argv);
    if (cli_status >= 0) {
        return cli_status;
    }

    int status = 0;
    status |= test_simple_arith_ldr_str();
    status |= test_load_store_addressing();
    status |= test_flags_and_sp();
    status |= test_fibonacci_elf();
    status |= test_branching();

    if (status == 0) {
        printf("All tests passed\n");
    }

    return status;
}
