/*
 * Blackfin translation
 *
 * Copyright 2007-2016 Mike Frysinger
 * Copyright 2007-2011 Analog Devices, Inc.
 *
 * Licensed under the Lesser GPL 2 or later.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/cpu_ldst.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"
#include "qemu-common.h"
#include "opcode/bfin.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

typedef void (*hwloop_callback)(struct DisasContext *dc, int loop);

typedef struct DisasContext {
    DisasContextBase base;
    CPUArchState *env;
    struct TranslationBlock *tb;
    /* The current PC we're decoding (could be middle of parallel insn) */
    target_ulong pc;
    /* Length of current insn (2/4/8) */
    target_ulong insn_len;

    /* For delayed ASTAT handling */
    enum astat_ops astat_op;

    /* For hardware loop processing */
    hwloop_callback hwloop_callback;
    void *hwloop_data;

    /* Was a DISALGNEXCPT used in this parallel insn ? */
    int disalgnexcpt;

    int is_jmp;
    int mem_idx;

    // Unicorn
    struct uc_struct *uc;
} DisasContext;

/* only pc was modified dynamically */
#define DISAS_JUMP    DISAS_TARGET_0
/* cpu state was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1
/* only pc was modified statically */
#define DISAS_TB_JUMP DISAS_TARGET_2
/* We're making a call (which means we need to update RTS) */
#define DISAS_CALL    DISAS_TARGET_3

#define cpu_spreg cpu_preg[6]
#define cpu_fpreg cpu_preg[7]


#include "exec/gen-icount.h"

static inline void
bfin_tcg_new_set3(TCGContext *tcg_ctx, TCGv *tcgv, unsigned int cnt, unsigned int offbase,
                  const char * const *names)
{
    unsigned int i;
    for (i = 0; i < cnt; ++i) {
        tcgv[i] = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env, offbase + (i * 4), names[i]);
    }
}
#define bfin_tcg_new_set2(tcg_ctx, tcgv, cnt, reg, name_idx) \
    bfin_tcg_new_set3(tcg_ctx, tcgv, cnt, offsetof(CPUArchState, reg), \
                      &greg_names[name_idx])
#define bfin_tcg_new_set(reg, name_idx) \
    bfin_tcg_new_set2(tcg_ctx, tcg_ctx->cpu_##reg, ARRAY_SIZE(tcg_ctx->cpu_##reg), reg, name_idx)
#define bfin_tcg_new(reg, name_idx) \
    bfin_tcg_new_set2(tcg_ctx, &(tcg_ctx->cpu_##reg), 1, reg, name_idx)

void bfin_translate_init(struct uc_struct *uc)
{
    TCGContext *tcg_ctx = uc->tcg_ctx;
    tcg_ctx->cpu_pc = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, pc), "PC");
    tcg_ctx->cpu_cc = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, astat[ASTAT_CC]), "CC");

    tcg_ctx->cpu_astat_arg[0] = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, astat_arg[0]), "astat_arg[0]");
    tcg_ctx->cpu_astat_arg[1] = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, astat_arg[1]), "astat_arg[1]");
    tcg_ctx->cpu_astat_arg[2] = tcg_global_mem_new(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, astat_arg[2]), "astat_arg[2]");

    tcg_ctx->cpu_areg[0] = tcg_global_mem_new_i64(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, areg[0]), "A0");
    tcg_ctx->cpu_areg[1] = tcg_global_mem_new_i64(tcg_ctx, tcg_ctx->cpu_env,
        offsetof(CPUArchState, areg[1]), "A1");

    bfin_tcg_new_set(dreg, 0);
    bfin_tcg_new_set(preg, 8);
    bfin_tcg_new_set(ireg, 16);
    bfin_tcg_new_set(mreg, 20);
    bfin_tcg_new_set(breg, 24);
    bfin_tcg_new_set(lreg, 28);
    bfin_tcg_new(rets, 39);
    bfin_tcg_new(lcreg[0], 48);
    bfin_tcg_new(ltreg[0], 49);
    bfin_tcg_new(lbreg[0], 50);
    bfin_tcg_new(lcreg[1], 51);
    bfin_tcg_new(ltreg[1], 52);
    bfin_tcg_new(lbreg[1], 53);
    bfin_tcg_new_set(cycles, 54);
    bfin_tcg_new(uspreg, 56);
    bfin_tcg_new(seqstat, 57);
    bfin_tcg_new(syscfg, 58);
    bfin_tcg_new(reti, 59);
    bfin_tcg_new(retx, 60);
    bfin_tcg_new(retn, 61);
    bfin_tcg_new(rete, 62);
    bfin_tcg_new(emudat, 63);
}

#define _astat_printf(bit) qemu_fprintf(f, "%s" #bit " ", \
                                        (env->astat[ASTAT_##bit] ? "" : "~"))


static void gen_astat_update(DisasContext *, bool);

static void gen_goto_tb(DisasContext *dc, int tb_num, TCGv dest)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    gen_astat_update(dc, false);
    tcg_gen_mov_tl(tcg_ctx, tcg_ctx->cpu_pc, dest);
    tcg_gen_exit_tb(tcg_ctx, NULL, 0);
}

static void gen_gotoi_tb(DisasContext *dc, int tb_num, target_ulong dest)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGv tmp = tcg_temp_local_new(tcg_ctx);
    tcg_gen_movi_tl(tcg_ctx, tmp, dest);
    gen_goto_tb(dc, tb_num, tmp);
    tcg_temp_free(tcg_ctx, tmp);
}

static void cec_exception(DisasContext *dc, int excp)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGv tmp = tcg_const_tl(tcg_ctx, excp);
    TCGv pc = tcg_const_tl(tcg_ctx, dc->pc);
    gen_helper_raise_exception(tcg_ctx, tcg_ctx->cpu_env, tmp, pc);
    tcg_temp_free(tcg_ctx, tmp);
    dc->is_jmp = DISAS_UPDATE;
}

static void cec_require_supervisor(DisasContext *dc)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
#ifdef CONFIG_LINUX_USER
    cec_exception(dc, EXCP_ILL_SUPV);
#else
    TCGv pc = tcg_const_tl(tcg_ctx, dc->pc);
    gen_helper_require_supervisor(tcg_ctx, tcg_ctx->cpu_env, pc);
#endif
}

static void gen_align_check(DisasContext *dc, TCGv addr, uint32_t len,
                            bool inst)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGv excp, pc, tmp;

    /* XXX: This should be made into a runtime option.  It adds likes
            10% overhead to memory intensive apps (like mp3 decoding). */
    if (1) {
        return;
    }

    excp = tcg_const_tl(tcg_ctx, inst ? EXCP_MISALIG_INST : EXCP_DATA_MISALGIN);
    pc = tcg_const_tl(tcg_ctx, dc->pc);
    tmp = tcg_const_tl(tcg_ctx, len);
    gen_helper_memalign(tcg_ctx, tcg_ctx->cpu_env, excp, pc, addr, tmp);
    tcg_temp_free(tcg_ctx, tmp);
    tcg_temp_free(tcg_ctx, pc);
    tcg_temp_free(tcg_ctx, excp);
}

static void gen_aligned_qemu_ld16u(DisasContext *dc, TCGv ret, TCGv addr)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    gen_align_check(dc, addr, 2, false);
    tcg_gen_qemu_ld16u(tcg_ctx, ret, addr, dc->mem_idx);
}

static void gen_aligned_qemu_ld16s(DisasContext *dc, TCGv ret, TCGv addr)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    gen_align_check(dc, addr, 2, false);
    tcg_gen_qemu_ld16s(tcg_ctx, ret, addr, dc->mem_idx);
}

static void gen_aligned_qemu_ld32u(DisasContext *dc, TCGv ret, TCGv addr)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    gen_align_check(dc, addr, 4, false);
    tcg_gen_qemu_ld32u(tcg_ctx, ret, addr, dc->mem_idx);
}

static void gen_aligned_qemu_st16(DisasContext *dc, TCGv val, TCGv addr)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    gen_align_check(dc, addr, 2, false);
    tcg_gen_qemu_st16(tcg_ctx, val, addr, dc->mem_idx);
}

static void gen_aligned_qemu_st32(DisasContext *dc, TCGv val, TCGv addr)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    gen_align_check(dc, addr, 4, false);
    tcg_gen_qemu_st32(tcg_ctx, val, addr, dc->mem_idx);
}

/*
 * If a LB reg is written, we need to invalidate the two translation
 * blocks that could be affected -- the TB's referenced by the old LB
 * could have LC/LT handling which we no longer want, and the new LB
 * is probably missing LC/LT handling which we want.  In both cases,
 * we need to regenerate the block.
 */
static void gen_maybe_lb_exit_tb(DisasContext *dc, TCGv reg)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    if (reg != tcg_ctx->cpu_lbreg[0] && reg != tcg_ctx->cpu_lbreg[1]) {
        return;
    }

    /* tb_invalidate_phys_page_range */
    dc->is_jmp = DISAS_UPDATE;
    /* XXX: Not entirely correct, but very few things load
     *      directly into LB ... */
    gen_gotoi_tb(dc, 0, dc->pc + dc->insn_len);
}

static void gen_hwloop_default(DisasContext *dc, int loop)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    if (loop != -1) {
        gen_goto_tb(dc, 0, tcg_ctx->cpu_ltreg[loop]);
    }
}

static void _gen_hwloop_call(DisasContext *dc, int loop)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    if (dc->is_jmp != DISAS_CALL) {
        return;
    }

    if (loop == -1) {
        tcg_gen_movi_tl(tcg_ctx, tcg_ctx->cpu_rets, dc->pc + dc->insn_len);
    } else {
        tcg_gen_mov_tl(tcg_ctx, tcg_ctx->cpu_rets, tcg_ctx->cpu_ltreg[loop]);
    }
}

static void gen_hwloop_br_pcrel_cc(DisasContext *dc, int loop)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    TCGLabel *l;
    int pcrel = (unsigned long)dc->hwloop_data;
    int T = pcrel & 1;
    pcrel &= ~1;

    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_NE, tcg_ctx->cpu_cc, T, l);
    gen_gotoi_tb(dc, 0, dc->pc + pcrel);
    gen_set_label(tcg_ctx, l);
    if (loop == -1) {
        dc->hwloop_callback = gen_hwloop_default;
    } else {
        gen_hwloop_default(dc, loop);
    }
}

static void gen_hwloop_br_pcrel(DisasContext *dc, int loop)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    TCGv *reg = dc->hwloop_data;
    _gen_hwloop_call(dc, loop);
    tcg_gen_addi_tl(tcg_ctx, tcg_ctx->cpu_pc, *reg, dc->pc);
    gen_goto_tb(dc, 0, tcg_ctx->cpu_pc);
}

static void gen_hwloop_br_pcrel_imm(DisasContext *dc, int loop)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    int pcrel = (unsigned long)dc->hwloop_data;
    TCGv tmp;

    _gen_hwloop_call(dc, loop);
    tmp = tcg_const_tl(tcg_ctx, pcrel);
    tcg_gen_addi_tl(tcg_ctx, tcg_ctx->cpu_pc, tmp, dc->pc);
    tcg_temp_free(tcg_ctx, tmp);
    gen_goto_tb(dc, 0, tcg_ctx->cpu_pc);
}

static void gen_hwloop_br_direct(DisasContext *dc, int loop)
{
    TCGv *reg = dc->hwloop_data;
    _gen_hwloop_call(dc, loop);
    gen_goto_tb(dc, 0, *reg);
}

static void _gen_hwloop_check(DisasContext *dc, int loop, TCGLabel *l)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_EQ, tcg_ctx->cpu_lcreg[loop], 0, l);
    tcg_gen_subi_tl(tcg_ctx, tcg_ctx->cpu_lcreg[loop], tcg_ctx->cpu_lcreg[loop], 1);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_EQ, tcg_ctx->cpu_lcreg[loop], 0, l);
    dc->hwloop_callback(dc, loop);
}

static void gen_hwloop_check(DisasContext *dc)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    bool loop1, loop0;
    TCGLabel *endl;

    loop1 = (dc->pc == dc->env->lbreg[1]);
    loop0 = (dc->pc == dc->env->lbreg[0]);

    if (loop1 || loop0) {
        endl = gen_new_label(tcg_ctx);
    }

    if (loop1) {
        TCGLabel *l;
        if (loop0) {
            l = gen_new_label(tcg_ctx);
        } else {
            l = endl;
        }

        _gen_hwloop_check(dc, 1, l);

        if (loop0) {
            tcg_gen_br(tcg_ctx, endl);
            gen_set_label(tcg_ctx, l);
        }
    }

    if (loop0) {
        _gen_hwloop_check(dc, 0, endl);
    }

    if (loop1 || loop0) {
        gen_set_label(tcg_ctx, endl);
    }

    dc->hwloop_callback(dc, -1);
}

/* R#.L = reg; R#.H = reg; */
/* XXX: This modifies the low source ... assumes it is a temp ... */
/*
static void gen_mov_l_h_tl(TCGv dst, TCGv srcl, TCGv srch)
{
    tcg_gen_shli_tl(dst, srch, 16);
    tcg_gen_andi_tl(srcl, srcl, 0xffff);
    tcg_gen_or_tl(dst, dst, srcl);
}
*/

/* R#.L = reg */
static void gen_mov_l_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src)
{
    tcg_gen_deposit_tl(tcg_ctx, dst, dst, src, 0, 16);
}

/* R#.L = imm32 */
/*
static void gen_movi_l_tl(TCGv dst, uint32_t src)
{
    tcg_gen_andi_tl(dst, dst, 0xffff0000);
    tcg_gen_ori_tl(dst, dst, src & 0xffff);
}
*/

/* R#.H = reg */
static void gen_mov_h_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src)
{
    tcg_gen_deposit_tl(tcg_ctx, dst, dst, src, 16, 16);
}

/* R#.H = imm32 */
/*
static void gen_movi_h_tl(TCGv dst, uint32_t src)
{
    tcg_gen_andi_tl(dst, dst, 0xffff);
    tcg_gen_ori_tl(dst, dst, src << 16);
}
*/

static void gen_extNs_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src, TCGv n)
{
    /* Shift the sign bit up, and then back down */
    TCGv tmp = tcg_temp_new(tcg_ctx);
    tcg_gen_subfi_tl(tcg_ctx, tmp, 32, n);
    tcg_gen_shl_tl(tcg_ctx, dst, src, tmp);
    tcg_gen_sar_tl(tcg_ctx, dst, dst, tmp);
    tcg_temp_free(tcg_ctx, tmp);
}

static void gen_extNsi_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src, uint32_t n)
{
    /* Shift the sign bit up, and then back down */
    tcg_gen_shli_tl(tcg_ctx, dst, src, 32 - n);
    tcg_gen_sari_tl(tcg_ctx, dst, dst, 32 - n);
}

static void gen_extNsi_i64(TCGContext *tcg_ctx, TCGv_i64 dst, TCGv_i64 src, uint32_t n)
{
    /* Shift the sign bit up, and then back down */
    tcg_gen_shli_i64(tcg_ctx, dst, src, 64 - n);
    tcg_gen_sari_i64(tcg_ctx, dst, dst, 64 - n);
}

static void gen_abs_tl(TCGContext *tcg_ctx, TCGv ret, TCGv arg)
{
    TCGLabel *l = gen_new_label(tcg_ctx);
    tcg_gen_mov_tl(tcg_ctx, ret, arg);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_GE, arg, 0, l);
    tcg_gen_neg_tl(tcg_ctx, ret, ret);
    gen_set_label(tcg_ctx, l);
}

static void gen_abs_i64(TCGContext *tcg_ctx, TCGv_i64 ret, TCGv_i64 arg)
{
    TCGLabel *l = gen_new_label(tcg_ctx);
    tcg_gen_mov_i64(tcg_ctx, ret, arg);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_GE, arg, 0, l);
    tcg_gen_neg_i64(tcg_ctx, ret, ret);
    gen_set_label(tcg_ctx, l);
}

/* Common tail code for DIVQ/DIVS insns */
static void _gen_divqs(TCGContext *tcg_ctx, TCGv pquo, TCGv r, TCGv aq, TCGv div)
{
    /*
     * pquo <<= 1
     * pquo |= aq
     * pquo = (pquo & 0x1FFFF) | (r << 17)
     */
    tcg_gen_shli_tl(tcg_ctx, pquo, pquo, 1);
    tcg_gen_or_tl(tcg_ctx, pquo, pquo, aq);
    tcg_gen_andi_tl(tcg_ctx, pquo, pquo, 0x1FFFF);
    tcg_gen_shli_tl(tcg_ctx, r, r, 17);
    tcg_gen_or_tl(tcg_ctx, pquo, pquo, r);

    tcg_temp_free(tcg_ctx, r);
    tcg_temp_free(tcg_ctx, aq);
    tcg_temp_free(tcg_ctx, div);
}

/* Common AQ ASTAT bit management for DIVQ/DIVS insns */
static void _gen_divqs_st_aq(TCGContext *tcg_ctx, TCGv r, TCGv aq, TCGv div)
{
    /* aq = (r ^ div) >> 15 */
    tcg_gen_xor_tl(tcg_ctx, aq, r, div);
    tcg_gen_shri_tl(tcg_ctx, aq, aq, 15);
    tcg_gen_andi_tl(tcg_ctx, aq, aq, 1);
    tcg_gen_st_tl(tcg_ctx, aq, tcg_ctx->cpu_env, offsetof(CPUArchState, astat[ASTAT_AQ]));
}

/* DIVQ ( Dreg, Dreg ) ;
 * Based on AQ status bit, either add or subtract the divisor from
 * the dividend. Then set the AQ status bit based on the MSBs of the
 * 32-bit dividend and the 16-bit divisor. Left shift the dividend one
 * bit. Copy the logical inverse of AQ into the dividend LSB.
 */
static void gen_divq(TCGContext *tcg_ctx, TCGv pquo, TCGv src)
{
    TCGLabel *l;
    TCGv af, r, aq, div;

    /* div = R#.L */
    div = tcg_temp_local_new(tcg_ctx);
    tcg_gen_ext16u_tl(tcg_ctx, div, src);

    /* af = pquo >> 16 */
    af = tcg_temp_local_new(tcg_ctx);
    tcg_gen_shri_tl(tcg_ctx, af, pquo, 16);

    /*
     * we take this:
     *  if (ASTAT_AQ)
     *    r = div + af;
     *  else
     *    r = af - div;
     *
     * and turn it into:
     *  r = div;
     *  if (aq == 0)
     *    r = -r;
     *  r += af;
     */
    aq = tcg_temp_local_new(tcg_ctx);
    tcg_gen_ld_tl(tcg_ctx, aq, tcg_ctx->cpu_env, offsetof(CPUArchState, astat[ASTAT_AQ]));

    l = gen_new_label(tcg_ctx);
    r = tcg_temp_local_new(tcg_ctx);
    tcg_gen_mov_tl(tcg_ctx, r, div);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_NE, aq, 0, l);
    tcg_gen_neg_tl(tcg_ctx, r, r);
    gen_set_label(tcg_ctx, l);
    tcg_gen_add_tl(tcg_ctx, r, r, af);

    tcg_temp_free(tcg_ctx, af);

    _gen_divqs_st_aq(tcg_ctx, r, aq, div);

    /* aq = !aq */
    tcg_gen_xori_tl(tcg_ctx, aq, aq, 1);

    _gen_divqs(tcg_ctx, pquo, r, aq, div);
}

/* DIVS ( Dreg, Dreg ) ;
 * Initialize for DIVQ. Set the AQ status bit based on the signs of
 * the 32-bit dividend and the 16-bit divisor. Left shift the dividend
 * one bit. Copy AQ into the dividend LSB.
 */
static void gen_divs(TCGContext *tcg_ctx, TCGv pquo, TCGv src)
{
    TCGv r, aq, div;

    /* div = R#.L */
    div = tcg_temp_local_new(tcg_ctx);
    tcg_gen_ext16u_tl(tcg_ctx, div, src);

    /* r = pquo >> 16 */
    r = tcg_temp_local_new(tcg_ctx);
    tcg_gen_shri_tl(tcg_ctx, r, pquo, 16);

    aq = tcg_temp_local_new(tcg_ctx);

    _gen_divqs_st_aq(tcg_ctx, r, aq, div);

    _gen_divqs(tcg_ctx, pquo, r, aq, div);
}

/* Reg = ROT reg BY reg/imm
 * The Blackfin rotate is not like the TCG rotate.  It shifts through the
 * CC bit too giving it 33 bits to play with.  So we have to reduce things
 * to shifts ourself.
 */
static void gen_rot_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src, TCGv orig_shift)
{
    uint32_t nbits = 32;
    TCGv shift, ret, tmp, tmp_shift;
    TCGLabel *l, *endl;

    /* shift = CLAMP (shift, -nbits, nbits); */

    endl = gen_new_label(tcg_ctx);

    /* if (shift == 0) */
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_NE, orig_shift, 0, l);
    tcg_gen_mov_tl(tcg_ctx, dst, src);
    tcg_gen_br(tcg_ctx, endl);
    gen_set_label(tcg_ctx, l);

    /* Reduce everything to rotate left */
    shift = tcg_temp_local_new(tcg_ctx);
    tcg_gen_mov_tl(tcg_ctx, shift, orig_shift);
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_GE, shift, 0, l);
    tcg_gen_addi_tl(tcg_ctx, shift, shift, nbits + 1);
    gen_set_label(tcg_ctx, l);

    if (dst == src) {
        ret = tcg_temp_local_new(tcg_ctx);
    } else {
        ret = dst;
    }

    /* ret = shift == nbits ? 0 : val << shift; */
    tcg_gen_movi_tl(tcg_ctx, ret, 0);
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_EQ, shift, nbits, l);
    tcg_gen_shl_tl(tcg_ctx, ret, src, shift);
    gen_set_label(tcg_ctx, l);

    /* ret |= shift == 1 ? 0 : val >> ((nbits + 1) - shift); */
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_EQ, shift, 1, l);
    tmp = tcg_temp_new(tcg_ctx);
    tmp_shift = tcg_temp_new(tcg_ctx);
    tcg_gen_subfi_tl(tcg_ctx, tmp_shift, nbits + 1, shift);
    tcg_gen_shr_tl(tcg_ctx, tmp, src, tmp_shift);
    tcg_gen_or_tl(tcg_ctx, ret, ret, tmp);
    tcg_temp_free(tcg_ctx, tmp_shift);
    tcg_temp_free(tcg_ctx, tmp);
    gen_set_label(tcg_ctx, l);

    /* Then add in and output feedback via the CC register */
    tcg_gen_subi_tl(tcg_ctx, shift, shift, 1);
    tcg_gen_shl_tl(tcg_ctx, tcg_ctx->cpu_cc, tcg_ctx->cpu_cc, shift);
    tcg_gen_or_tl(tcg_ctx, ret, ret, tcg_ctx->cpu_cc);
    tcg_gen_subfi_tl(tcg_ctx, shift, nbits - 1, shift);
    tcg_gen_shr_tl(tcg_ctx, tcg_ctx->cpu_cc, src, shift);
    tcg_gen_andi_tl(tcg_ctx, tcg_ctx->cpu_cc, tcg_ctx->cpu_cc, 1);

    if (dst == src) {
        tcg_gen_mov_tl(tcg_ctx, dst, ret);
        tcg_temp_free(tcg_ctx, ret);
    }

    tcg_temp_free(tcg_ctx, shift);
    gen_set_label(tcg_ctx, endl);
}

static void gen_roti_tl(TCGContext *tcg_ctx, TCGv dst, TCGv src, int32_t shift)
{
    uint32_t nbits = 32;
    TCGv ret;

    /* shift = CLAMP (shift, -nbits, nbits); */

    if (shift == 0) {
        tcg_gen_mov_tl(tcg_ctx, dst, src);
        return;
    }

    /* Reduce everything to rotate left */
    if (shift < 0) {
        shift += nbits + 1;
    }

    if (dst == src) {
        ret = tcg_temp_new(tcg_ctx);
    } else {
        ret = dst;
    }

    /* First rotate the main register */
    if (shift == nbits) {
        tcg_gen_movi_tl(tcg_ctx, ret, 0);
    } else {
        tcg_gen_shli_tl(tcg_ctx, ret, src, shift);
    }
    if (shift != 1) {
        TCGv tmp = tcg_temp_new(tcg_ctx);
        tcg_gen_shri_tl(tcg_ctx, tmp, src, (nbits + 1) - shift);
        tcg_gen_or_tl(tcg_ctx, ret, ret, tmp);
        tcg_temp_free(tcg_ctx, tmp);
    }

    /* Then add in and output feedback via the CC register */
    tcg_gen_shli_tl(tcg_ctx, tcg_ctx->cpu_cc, tcg_ctx->cpu_cc, shift - 1);
    tcg_gen_or_tl(tcg_ctx, ret, ret, tcg_ctx->cpu_cc);
    tcg_gen_shri_tl(tcg_ctx, tcg_ctx->cpu_cc, src, nbits - shift);
    tcg_gen_andi_tl(tcg_ctx, tcg_ctx->cpu_cc, tcg_ctx->cpu_cc, 1);

    if (dst == src) {
        tcg_gen_mov_tl(tcg_ctx, dst, ret);
        tcg_temp_free(tcg_ctx, ret);
    }
}

static void gen_rot_i64(TCGContext *tcg_ctx, TCGv_i64 dst, TCGv_i64 src, TCGv_i64 orig_shift)
{
    uint32_t nbits = 40;
    TCGv_i64 shift, ret, tmp, tmp_shift, cc64;
    TCGLabel *l, *endl;

    /* shift = CLAMP (shift, -nbits, nbits); */

    endl = gen_new_label(tcg_ctx);

    /* if (shift == 0) */
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_NE, orig_shift, 0, l);
    tcg_gen_mov_i64(tcg_ctx, dst, src);
    tcg_gen_br(tcg_ctx, endl);
    gen_set_label(tcg_ctx, l);

    /* Reduce everything to rotate left */
    shift = tcg_temp_local_new_i64(tcg_ctx);
    tcg_gen_mov_i64(tcg_ctx, shift, orig_shift);
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_GE, shift, 0, l);
    tcg_gen_addi_i64(tcg_ctx, shift, shift, nbits + 1);
    gen_set_label(tcg_ctx, l);

    if (dst == src) {
        ret = tcg_temp_local_new_i64(tcg_ctx);
    } else {
        ret = dst;
    }

    /* ret = shift == nbits ? 0 : val << shift; */
    tcg_gen_movi_i64(tcg_ctx, ret, 0);
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_EQ, shift, nbits, l);
    tcg_gen_shl_i64(tcg_ctx, ret, src, shift);
    gen_set_label(tcg_ctx, l);

    /* ret |= shift == 1 ? 0 : val >> ((nbits + 1) - shift); */
    l = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_i64(tcg_ctx, TCG_COND_EQ, shift, 1, l);
    tmp = tcg_temp_new_i64(tcg_ctx);
    tmp_shift = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_subfi_i64(tcg_ctx, tmp_shift, nbits + 1, shift);
    tcg_gen_shr_i64(tcg_ctx, tmp, src, tmp_shift);
    tcg_gen_or_i64(tcg_ctx, ret, ret, tmp);
    tcg_temp_free_i64(tcg_ctx, tmp_shift);
    tcg_temp_free_i64(tcg_ctx, tmp);
    gen_set_label(tcg_ctx, l);

    /* Then add in and output feedback via the CC register */
    cc64 = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ext_i32_i64(tcg_ctx, cc64, tcg_ctx->cpu_cc);
    tcg_gen_subi_i64(tcg_ctx, shift, shift, 1);
    tcg_gen_shl_i64(tcg_ctx, cc64, cc64, shift);
    tcg_gen_or_i64(tcg_ctx, ret, ret, cc64);
    tcg_gen_subfi_i64(tcg_ctx, shift, nbits - 1, shift);
    tcg_gen_shr_i64(tcg_ctx, cc64, src, shift);
    tcg_gen_andi_i64(tcg_ctx, cc64, cc64, 1);
    tcg_gen_extrl_i64_i32(tcg_ctx, tcg_ctx->cpu_cc, cc64);
    tcg_temp_free_i64(tcg_ctx, cc64);

    if (dst == src) {
        tcg_gen_mov_i64(tcg_ctx, dst, ret);
        tcg_temp_free_i64(tcg_ctx, ret);
    }

    tcg_temp_free_i64(tcg_ctx, shift);
    gen_set_label(tcg_ctx, endl);
}

static void gen_roti_i64(TCGContext *tcg_ctx, TCGv_i64 dst, TCGv_i64 src, int32_t shift)
{
    uint32_t nbits = 40;
    TCGv_i64 ret, cc64;

    /* shift = CLAMP (shift, -nbits, nbits); */

    if (shift == 0) {
        tcg_gen_mov_i64(tcg_ctx, dst, src);
        return;
    }

    /* Reduce everything to rotate left */
    if (shift < 0) {
        shift += nbits + 1;
    }

    if (dst == src) {
        ret = tcg_temp_new_i64(tcg_ctx);
    } else {
        ret = dst;
    }

    /* First rotate the main register */
    if (shift == nbits) {
        tcg_gen_movi_i64(tcg_ctx, ret, 0);
    } else {
        tcg_gen_shli_i64(tcg_ctx, ret, src, shift);
    }
    if (shift != 1) {
        TCGv_i64 tmp = tcg_temp_new_i64(tcg_ctx);
        tcg_gen_shri_i64(tcg_ctx, tmp, src, (nbits + 1) - shift);
        tcg_gen_or_i64(tcg_ctx, ret, ret, tmp);
        tcg_temp_free_i64(tcg_ctx, tmp);
    }

    /* Then add in and output feedback via the CC register */
    cc64 = tcg_temp_new_i64(tcg_ctx);
    tcg_gen_ext_i32_i64(tcg_ctx, cc64, tcg_ctx->cpu_cc);
    tcg_gen_shli_i64(tcg_ctx, cc64, cc64, shift - 1);
    tcg_gen_or_i64(tcg_ctx, ret, ret, cc64);
    tcg_gen_shri_i64(tcg_ctx, cc64, src, nbits - shift);
    tcg_gen_andi_i64(tcg_ctx, cc64, cc64, 1);
    tcg_gen_extrl_i64_i32(tcg_ctx, tcg_ctx->cpu_cc, cc64);
    tcg_temp_free_i64(tcg_ctx, cc64);

    if (dst == src) {
        tcg_gen_mov_i64(tcg_ctx, dst, ret);
        tcg_temp_free_i64(tcg_ctx, ret);
    }
}

/* This is a bit crazy, but we want to simulate the hardware behavior exactly
   rather than worry about the circular buffers being used correctly.  Which
   isn't to say there isn't room for improvement here, just that we want to
   be conservative.  See also dagsub().  */
static void gen_dagadd(DisasContext *dc, int dagno, TCGv M)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGLabel *l, *endl;

    /* Optimize for when circ buffers are not used */
    l = gen_new_label(tcg_ctx);
    endl = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_NE, tcg_ctx->cpu_lreg[dagno], 0, l);
    tcg_gen_add_tl(tcg_ctx, tcg_ctx->cpu_ireg[dagno], tcg_ctx->cpu_ireg[dagno], M);
    tcg_gen_br(tcg_ctx, endl);
    gen_set_label(tcg_ctx, l);

    /* Fallback to the big guns */
    gen_helper_dagadd(tcg_ctx, tcg_ctx->cpu_ireg[dagno], tcg_ctx->cpu_ireg[dagno],
                      tcg_ctx->cpu_lreg[dagno], tcg_ctx->cpu_breg[dagno], M);

    gen_set_label(tcg_ctx, endl);
}

static void gen_dagaddi(DisasContext *dc, int dagno, uint32_t M)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    TCGv m = tcg_temp_local_new(tcg_ctx);
    tcg_gen_movi_tl(tcg_ctx, m, M);
    gen_dagadd(dc, dagno, m);
    tcg_temp_free(tcg_ctx, m);
}

/* See dagadd() notes above.  */
static void gen_dagsub(DisasContext *dc, int dagno, TCGv M)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGLabel *l, *endl;

    /* Optimize for when circ buffers are not used */
    l = gen_new_label(tcg_ctx);
    endl = gen_new_label(tcg_ctx);
    tcg_gen_brcondi_tl(tcg_ctx, TCG_COND_NE, tcg_ctx->cpu_lreg[dagno], 0, l);
    tcg_gen_sub_tl(tcg_ctx, tcg_ctx->cpu_ireg[dagno], tcg_ctx->cpu_ireg[dagno], M);
    tcg_gen_br(tcg_ctx, endl);
    gen_set_label(tcg_ctx, l);

    /* Fallback to the big guns */
    gen_helper_dagsub(tcg_ctx, tcg_ctx->cpu_ireg[dagno], tcg_ctx->cpu_ireg[dagno],
                      tcg_ctx->cpu_lreg[dagno], tcg_ctx->cpu_breg[dagno], M);

    gen_set_label(tcg_ctx, endl);
}

static void gen_dagsubi(DisasContext *dc, int dagno, uint32_t M)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGv m = tcg_temp_local_new(tcg_ctx);
    tcg_gen_movi_tl(tcg_ctx, m, M);
    gen_dagsub(dc, dagno, m);
    tcg_temp_free(tcg_ctx, m);
}

#define _gen_astat_store(tcg_ctx, bit, reg) \
    tcg_gen_st_tl(tcg_ctx, reg, tcg_ctx->cpu_env, offsetof(CPUArchState, astat[bit]))

static void _gen_astat_update_az(TCGContext *tcg_ctx, TCGv reg, TCGv tmp)
{
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, tmp, reg, 0);
    _gen_astat_store(tcg_ctx, ASTAT_AZ, tmp);
}

static void _gen_astat_update_az2(TCGContext *tcg_ctx, TCGv reg, TCGv reg2, TCGv tmp)
{
    TCGv tmp2 = tcg_temp_new(tcg_ctx);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, tmp, reg, 0);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, tmp2, reg2, 0);
    tcg_gen_or_tl(tcg_ctx, tmp, tmp, tmp2);
    tcg_temp_free(tcg_ctx, tmp2);
    _gen_astat_store(tcg_ctx, ASTAT_AZ, tmp);
}

static void _gen_astat_update_an(TCGContext *tcg_ctx, TCGv reg, TCGv tmp, uint32_t len)
{
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_GEU, tmp, reg, 1 << (len - 1));
    _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
}

static void _gen_astat_update_an2(TCGContext *tcg_ctx, TCGv reg, TCGv reg2, TCGv tmp, uint32_t len)
{
    TCGv tmp2 = tcg_temp_new(tcg_ctx);
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_GEU, tmp, reg, 1 << (len - 1));
    tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_GEU, tmp2, reg2, 1 << (len - 1));
    tcg_gen_or_tl(tcg_ctx, tmp, tmp, tmp2);
    tcg_temp_free(tcg_ctx, tmp2);
    _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
}

static void _gen_astat_update_nz(TCGContext *tcg_ctx, TCGv reg, TCGv tmp, uint32_t len)
{
    _gen_astat_update_az(tcg_ctx, reg, tmp);
    _gen_astat_update_an(tcg_ctx, reg, tmp, len);
}

static void _gen_astat_update_nz2(TCGContext *tcg_ctx, TCGv reg, TCGv reg2, TCGv tmp, uint32_t len)
{
    _gen_astat_update_az2(tcg_ctx, reg, reg2, tmp);
    _gen_astat_update_an2(tcg_ctx, reg, reg2, tmp, len);
}

static void gen_astat_update(DisasContext *dc, bool clear)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    TCGv tmp = tcg_temp_local_new(tcg_ctx);
    uint32_t len = 16;

    switch (dc->astat_op) {
    case ASTAT_OP_ABS:    /* [0] = ABS( [1] ) */
        len = 32;
        /* XXX: Missing V/VS updates */
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, len);
        break;

    case ASTAT_OP_ABS_VECTOR: /* [0][1] = ABS( [2] ) (V) */
        /* XXX: Missing V/VS updates */
        _gen_astat_update_nz2(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[1], tmp, len);
        break;

    case ASTAT_OP_ADD32:    /* [0] = [1] + [2] */
        /* XXX: Missing V/VS updates */
        len = 32;
        tcg_gen_not_tl(tcg_ctx, tmp, tcg_ctx->cpu_astat_arg[1]);
        tcg_gen_setcond_tl(tcg_ctx, TCG_COND_LTU, tmp, tmp, tcg_ctx->cpu_astat_arg[2]);
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_AC0_COPY, tmp);
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, 32);
        break;

    case ASTAT_OP_ASHIFT32:
        len *= 2;
    case ASTAT_OP_ASHIFT16:
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        /* Need to update AC0 ? */
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, len);
        break;

    case ASTAT_OP_COMPARE_SIGNED: {
        TCGv flgs, flgo, overflow, flgn, res = tcg_temp_new(tcg_ctx);
        tcg_gen_sub_tl(tcg_ctx, res, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[1]);
        _gen_astat_update_az(tcg_ctx, res, tmp);
        tcg_gen_setcond_tl(tcg_ctx, TCG_COND_LEU, tmp, tcg_ctx->cpu_astat_arg[1],
                           tcg_ctx->cpu_astat_arg[0]);
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_AC0_COPY, tmp);
        /* XXX: This has got to be simpler ... */
        /* int flgs = srcop >> 31; */
        flgs = tcg_temp_new(tcg_ctx);
        tcg_gen_shri_tl(tcg_ctx, flgs, tcg_ctx->cpu_astat_arg[0], 31);
        /* int flgo = dstop >> 31; */
        flgo = tcg_temp_new(tcg_ctx);
        tcg_gen_shri_tl(tcg_ctx, flgo, tcg_ctx->cpu_astat_arg[1], 31);
        /* int flgn = result >> 31; */
        flgn = tcg_temp_new(tcg_ctx);
        tcg_gen_shri_tl(tcg_ctx, flgn, res, 31);
        /* int overflow = (flgs ^ flgo) & (flgn ^ flgs); */
        overflow = tcg_temp_new(tcg_ctx);
        tcg_gen_xor_tl(tcg_ctx, tmp, flgs, flgo);
        tcg_gen_xor_tl(tcg_ctx, overflow, flgn, flgs);
        tcg_gen_and_tl(tcg_ctx, overflow, tmp, overflow);
        /* an = (flgn && !overflow) || (!flgn && overflow); */
        tcg_gen_not_tl(tcg_ctx, tmp, overflow);
        tcg_gen_and_tl(tcg_ctx, tmp, flgn, tmp);
        tcg_gen_not_tl(tcg_ctx, res, flgn);
        tcg_gen_and_tl(tcg_ctx, res, res, overflow);
        tcg_gen_or_tl(tcg_ctx, tmp, tmp, res);
        tcg_temp_free(tcg_ctx, flgn);
        tcg_temp_free(tcg_ctx, overflow);
        tcg_temp_free(tcg_ctx, flgo);
        tcg_temp_free(tcg_ctx, flgs);
        tcg_temp_free(tcg_ctx, res);
        _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
        break;
    }

    case ASTAT_OP_COMPARE_UNSIGNED:
        tcg_gen_sub_tl(tcg_ctx, tmp, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[1]);
        _gen_astat_update_az(tcg_ctx, tmp, tmp);
        tcg_gen_setcond_tl(tcg_ctx, TCG_COND_LEU, tmp, tcg_ctx->cpu_astat_arg[1],
                           tcg_ctx->cpu_astat_arg[0]);
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_AC0_COPY, tmp);
        tcg_gen_setcond_tl(tcg_ctx, TCG_COND_GTU, tmp, tcg_ctx->cpu_astat_arg[1],
                           tcg_ctx->cpu_astat_arg[0]);
        _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
        break;

    case ASTAT_OP_LOGICAL:
        len = 32;
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        /* AC0 is correct ? */
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_AC0_COPY, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, len);
        break;

    case ASTAT_OP_LSHIFT32:
        len *= 2;
    case ASTAT_OP_LSHIFT16:
        _gen_astat_update_az(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp);
        /* XXX: should be checking bit shifted */
        tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_GEU, tmp, tcg_ctx->cpu_astat_arg[0],
                            1 << (len - 1));
        _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
        /* XXX: No saturation handling ... */
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        break;

    case ASTAT_OP_LSHIFT_RT32:
        len *= 2;
    case ASTAT_OP_LSHIFT_RT16:
        _gen_astat_update_az(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp);
        /* XXX: should be checking bit shifted */
        tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_GEU, tmp, tcg_ctx->cpu_astat_arg[0],
                            1 << (len - 1));
        _gen_astat_store(tcg_ctx, ASTAT_AN, tmp);
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        break;

    case ASTAT_OP_MIN_MAX:    /* [0] = MAX/MIN( [1], [2] ) */
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, 32);
        break;

    case ASTAT_OP_MIN_MAX_VECTOR: /* [0][1] = MAX/MIN( [2], [3] ) (V) */
        tcg_gen_movi_tl(tcg_ctx, tmp, 0);
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_V_COPY, tmp);
        tcg_gen_sari_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[0], 16);
        _gen_astat_update_nz2(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[1], tmp, 16);
        break;

    case ASTAT_OP_NEGATE:    /* [0] = -[1] */
        len = 32;
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, 32);
        tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, tmp, tcg_ctx->cpu_astat_arg[0], 1 << (len - 1));
        _gen_astat_store(tcg_ctx, ASTAT_V, tmp);
        /* XXX: Should "VS |= V;" */
        tcg_gen_setcondi_tl(tcg_ctx, TCG_COND_EQ, tmp, tcg_ctx->cpu_astat_arg[0], 0);
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        break;

    case ASTAT_OP_SUB32:    /* [0] = [1] - [2] */
        len = 32;
        /* XXX: Missing V/VS updates */
        tcg_gen_setcond_tl(tcg_ctx, TCG_COND_LEU, tmp, tcg_ctx->cpu_astat_arg[2],
                           tcg_ctx->cpu_astat_arg[1]);
        _gen_astat_store(tcg_ctx, ASTAT_AC0, tmp);
        _gen_astat_store(tcg_ctx, ASTAT_AC0_COPY, tmp);
        _gen_astat_update_nz(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tmp, len);
        break;

    case ASTAT_OP_VECTOR_ADD_ADD:    /* [0][1] = [2] +|+ [3] */
    case ASTAT_OP_VECTOR_ADD_SUB:    /* [0][1] = [2] +|- [3] */
    case ASTAT_OP_VECTOR_SUB_SUB:    /* [0][1] = [2] -|- [3] */
    case ASTAT_OP_VECTOR_SUB_ADD:    /* [0][1] = [2] -|+ [3] */
        _gen_astat_update_az2(tcg_ctx, tcg_ctx->cpu_astat_arg[0], tcg_ctx->cpu_astat_arg[1], tmp);
        /* Need AN, AC0/AC1, V */
        break;

    default:
        fprintf(stderr, "qemu: unhandled astat op %u\n", dc->astat_op);
        abort();
    case ASTAT_OP_DYNAMIC:
    case ASTAT_OP_NONE:
        break;
    }

    tcg_temp_free(tcg_ctx, tmp);

    if (clear) {
        dc->astat_op = ASTAT_OP_NONE;
    }
}

static void
_astat_queue_state(DisasContext *dc, enum astat_ops op, unsigned int num,
                   TCGv arg0, TCGv arg1, TCGv arg2)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    dc->astat_op = op;

    tcg_gen_mov_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[0], arg0);
    if (num > 1) {
        tcg_gen_mov_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[1], arg1);
    } else {
        tcg_gen_discard_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[1]);
    }
    if (num > 2) {
        tcg_gen_mov_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[2], arg2);
    } else {
        tcg_gen_discard_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[2]);
    }
}
#define astat_queue_state1(dc, op, arg0) \
    _astat_queue_state(dc, op, 1, arg0, arg0, arg0)
#define astat_queue_state2(dc, op, arg0, arg1) \
    _astat_queue_state(dc, op, 2, arg0, arg1, arg1)
#define astat_queue_state3(dc, op, arg0, arg1, arg2) \
    _astat_queue_state(dc, op, 3, arg0, arg1, arg2)

static void gen_astat_load(DisasContext *dc, TCGv reg)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    gen_astat_update(dc, true);
    gen_helper_astat_load(tcg_ctx, reg, tcg_ctx->cpu_env);
}

static void gen_astat_store(DisasContext *dc, TCGv reg)
{
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;
    unsigned int i;

    gen_helper_astat_store(tcg_ctx, tcg_ctx->cpu_env, reg);

    dc->astat_op = ASTAT_OP_NONE;

    for (i = 0; i < ARRAY_SIZE(tcg_ctx->cpu_astat_arg); ++i) {
        tcg_gen_discard_tl(tcg_ctx, tcg_ctx->cpu_astat_arg[i]);
    }
}

static void interp_insn_bfin(DisasContext *dc);

static inline void gen_save_pc(DisasContext *ctx, target_ulong pc)
{
    TCGContext *tcg_ctx = ctx->uc->tcg_ctx;

    tcg_gen_movi_tl(tcg_ctx, tcg_ctx->cpu_pc, pc);
}


static void bfin_tr_init_disas_context(DisasContextBase *dcbase,
    CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUArchState *env = cs->env_ptr;

    // unicorn setup
    dc->uc = cs->uc;

    dc->pc = dc->base.pc_first;
    dc->env = env;

    dc->mem_idx = cpu_mmu_index(env, false);

    dc->astat_op = ASTAT_OP_DYNAMIC;
    dc->hwloop_callback = gen_hwloop_default;
    dc->disalgnexcpt = 1;
}

static void bfin_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void bfin_tr_pc_sync(DisasContextBase *db, CPUState *cpu)
{
    DisasContext *ctx = container_of(db, DisasContext, base);

    gen_save_pc(ctx, ctx->base.pc_next);
}

static void bfin_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    TCGContext *tcg_ctx = ctx->uc->tcg_ctx;
    tcg_gen_insn_start(tcg_ctx, ctx->base.pc_next);
}

static void bfin_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    struct uc_struct *uc = dc->uc;
    TCGContext *tcg_ctx = uc->tcg_ctx;
    bool hook_insn = false;
    TCGOp *tcg_op, *prev_op = NULL;

    dc->pc = dc->base.pc_next;

    // Unicorn: end address tells us to stop emulation
    if (uc_addr_is_exit(uc, dc->base.pc_next)) {
        cec_exception(dc, EXCP_HLT);
        dc->base.is_jmp = DISAS_NORETURN;
        return;
    }

    // Unicorn: trace this instruction on request
    if (HOOK_EXISTS_BOUNDED(uc, UC_HOOK_CODE, dc->pc)) {
        // Sync PC in advance
        gen_save_pc(dc, dc->base.pc_next);

        // save the last operand
        prev_op = tcg_last_op(tcg_ctx);
        hook_insn = true;
        gen_uc_tracecode(tcg_ctx, dc->insn_len, UC_HOOK_CODE_IDX, dc->uc,
                         dc->base.pc_next);
        // the callback might want to stop emulation immediately
        check_exit_request(tcg_ctx);
    }

    interp_insn_bfin(dc);
    gen_hwloop_check(dc);

    if (hook_insn) {
        // Unicorn: patch the callback to have the proper instruction size.
        if (prev_op) {
            // As explained further up in the function where prev_op is
            // assigned, we move forward in the tail queue, so we're modifying the
            // move instruction generated by gen_uc_tracecode() that contains
            // the instruction size to assign the proper size (replacing 0xF1F1F1F1).
            tcg_op = QTAILQ_NEXT(prev_op, link);
        } else {
            // this instruction is the first emulated code ever,
            // so the instruction operand is the first operand
            tcg_op = QTAILQ_FIRST(&tcg_ctx->ops);
        }

        tcg_op->args[1] = dc->insn_len;
    }

    dc->base.pc_next += dc->insn_len;

}

static void bfin_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    TCGContext *tcg_ctx = dc->uc->tcg_ctx;

    switch (dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_gotoi_tb(dc, 1, dc->base.pc_next);
            break;
        case DISAS_UPDATE:
            /* indicate that the hash table must be used
               to find the next TB */
            tcg_gen_exit_tb(tcg_ctx, NULL, 0);
            break;
        case DISAS_CALL:
        case DISAS_JUMP:
        case DISAS_TB_JUMP:
        case DISAS_NORETURN:
            /* nothing more to generate */
            break;
        default:
            g_assert_not_reached();
    }
}

static const TranslatorOps bfin_tr_ops = {
    .init_disas_context = bfin_tr_init_disas_context,
    .tb_start           = bfin_tr_tb_start,
    .insn_start         = bfin_tr_insn_start,
    .translate_insn     = bfin_tr_translate_insn,
    .tb_stop            = bfin_tr_tb_stop,
    .pc_sync            = bfin_tr_pc_sync
};


void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    translator_loop(&bfin_tr_ops, &ctx.base, cs, tb, max_insns);
}

void
restore_state_to_opc(CPUArchState *env, TranslationBlock *tb,
                     target_ulong *data)
{
    env->pc = data[0];
}

#include "bfin-sim.c"
