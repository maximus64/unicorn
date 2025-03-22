#include "unicorn_test.h"

const uint64_t code_start = 0x1000;
const uint64_t code_len = 0x4000;

static void uc_common_setup(uc_engine **uc, uc_arch arch, uc_mode mode,
                            const char *code, uint64_t size)
{
    OK(uc_open(arch, mode, uc));
    OK(uc_mem_map(*uc, code_start, code_len, UC_PROT_ALL));
    OK(uc_mem_write(*uc, code_start, code, size));
}

static void test_bfin_mov(void)
{
    char code[] = "\x00\xe1\x41\x41"; // r0.l = 0x4141
    uint32_t r_pc, r_r0;
    uc_engine *uc;

    uc_common_setup(&uc, UC_ARCH_BFIN, UC_MODE_LITTLE_ENDIAN, code,
                    sizeof(code) - 1);

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_BFIN_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_BFIN_REG_PC, &r_pc));

    TEST_CHECK(r_r0 == 0x4141);
    TEST_CHECK(r_pc == code_start + sizeof(code) - 1);

    OK(uc_close(uc));
}

static void test_bfin_relative_jump(void)
{
    /*
     *     r0.l = 0x4141
     *     jump 1f
     *     r0.l = 0xbad0
     * 1:
     *     r0.h = 0x4242
     */
    char code[] = "\x00\xe1\x41\x41\x03\x20\x00\xe1\xd0\xba\x40\xe1\x42\x42";
    uint32_t r_pc, r_r0;
    uc_engine *uc;

    uc_common_setup(&uc, UC_ARCH_BFIN, UC_MODE_LITTLE_ENDIAN, code,
                    sizeof(code) - 1);

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_BFIN_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_BFIN_REG_PC, &r_pc));

    TEST_CHECK(r_r0 == 0x42424141);
    TEST_CHECK(r_pc == code_start + sizeof(code) - 1);

    OK(uc_close(uc));
}

static void test_bfin_hardware_loop(void)
{
    /*
     *     r0 = 0;
     *     p1 = 16;
     *     p0 = 0x2000;
     *     lsetup(_begin, _end) lc1 = p1;
     * _begin:
     *     B[p0++] = r0
     *     r0 += 1
     * _end:
     *     nop
     */
    char code[] = "\x00\x60\x81\x68\x28\xe1\x00\x20\xb2\xe0\x04\x10\x00\x9a\x08\x64\x00\x00";
    uint32_t r_pc, r_r0;
    uc_engine *uc;

    uc_common_setup(&uc, UC_ARCH_BFIN, UC_MODE_LITTLE_ENDIAN, code,
                    sizeof(code) - 1);

    OK(uc_emu_start(uc, code_start, code_start + sizeof(code) - 1, 0, 0));

    OK(uc_reg_read(uc, UC_BFIN_REG_R0, &r_r0));
    OK(uc_reg_read(uc, UC_BFIN_REG_PC, &r_pc));

    TEST_CHECK(r_r0 == 16);
    TEST_CHECK(r_pc == code_start + sizeof(code) - 1);

    for(int i = 0; i < 16; i++) {
        uint8_t b = 0;
        OK(uc_mem_read(uc, 0x2000 + i, &b, 1));
        TEST_CHECK(b == i);
    }

    OK(uc_close(uc));
}

TEST_LIST = {
                {"test_bfin_mov", test_bfin_mov},
                {"test_bfin_relative_jump", test_bfin_relative_jump},
                {"test_bfin_hardware_loop", test_bfin_hardware_loop},
                {NULL, NULL}
            };
