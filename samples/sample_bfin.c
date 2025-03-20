/* Unicorn Emulator Engine */
/* By Khoa Hoang, 2025 */

/* Sample code to demonstrate how to emulate Blackfin code */

#include <unicorn/unicorn.h>
#include <string.h>

// code to be emulated
// r0.l = 0x4141; r0.h = 0x4242; r1 = 0x32
#define CODE "\x00\xe1\x41\x41\x40\xe1\x42\x42\x91\x61\x00\x00"

// memory address where emulation starts
#define ADDRESS 0x10000

static void hook_block(uc_engine *uc, uint64_t address, uint32_t size,
                       void *user_data)
{
    printf(">>> Tracing basic block at 0x%" PRIx64 ", block size = 0x%x\n",
           address, size);
}

static void hook_code(uc_engine *uc, uint64_t address, uint32_t size,
                      void *user_data)
{
    printf(">>> Tracing instruction at 0x%" PRIx64
           ", instruction size = 0x%x\n",
           address, size);
}

static void test_blackfin(void)
{
    uc_engine *uc;
    uc_err err;
    uc_hook trace1, trace2;

    uint32_t r0 = 0x0; // r0 register
    uint32_t r1 = 0x0; // r1 register

    printf("Emulate Blackfin code\n");

    // Initialize emulator in Blackfin mode
    err = uc_open(UC_ARCH_BFIN, UC_MODE_LITTLE_ENDIAN, &uc);
    if (err) {
        printf("Failed on uc_open() with error returned: %u (%s)\n", err,
               uc_strerror(err));
        return;
    }

    // map 2MB memory for this emulation
    uc_mem_map(uc, ADDRESS, 2 * 1024 * 1024, UC_PROT_ALL);

    // write machine code to be emulated to memory
    uc_mem_write(uc, ADDRESS, CODE, sizeof(CODE) - 1);

    // tracing all basic blocks with customized callback
    uc_hook_add(uc, &trace1, UC_HOOK_BLOCK, hook_block, NULL, 1, 0);

    // tracing one instruction at ADDRESS with customized callback
    uc_hook_add(uc, &trace2, UC_HOOK_CODE, hook_code, NULL, ADDRESS,
                ADDRESS + sizeof(CODE) - 1);

    // emulate machine code in infinite time (last param = 0), or when
    // finishing all the code.
    err = uc_emu_start(uc, ADDRESS, ADDRESS + sizeof(CODE) - 1, 0, 0);
    if (err) {
        printf("Failed on uc_emu_start() with error returned: %u\n", err);
    }

    // now print out some registers
    printf(">>> Emulation done. Below is the CPU context\n");

    uc_reg_read(uc, UC_BFIN_REG_R0, &r0);
    printf(">>> r0 = 0x%x\n", r0);

    uc_reg_read(uc, UC_BFIN_REG_R1, &r1);
    printf(">>> r1 = 0x%x\n", r1);

    uc_close(uc);
}

int main(int argc, char **argv, char **envp)
{
    test_blackfin();

    return 0;
}
