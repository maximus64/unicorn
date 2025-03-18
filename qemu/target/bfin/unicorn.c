/* Unicorn Emulator Engine */
/* By Nguyen Anh Quynh <aquynh@gmail.com>, 2015-2021 */

#include "sysemu/cpus.h"
#include "cpu.h"
#include "unicorn_common.h"
#include "uc_priv.h"
#include "unicorn.h"

static int bfin_cpus_init(struct uc_struct *uc, const char *cpu_model)
{
    BlackfinCPU *cpu;

    cpu = cpu_bfin_init(uc);
    if (cpu == NULL) {
        return -1;
    }

    return 0;
}

static void bfin_set_pc(struct uc_struct *uc, uint64_t address)
{
    ((CPUBfinState *)uc->cpu->env_ptr)->pc = address;
}

static uint64_t bfin_get_pc(struct uc_struct *uc)
{
    return ((CPUBfinState *)uc->cpu->env_ptr)->pc;
}

static void bfin_release(void *ctx)
{

    int i;
    TCGContext *tcg_ctx = (TCGContext *)ctx;
    BlackfinCPU *cpu = (BlackfinCPU *)tcg_ctx->uc->cpu;
    CPUTLBDesc *d = cpu->neg.tlb.d;
    CPUTLBDescFast *f = cpu->neg.tlb.f;
    CPUTLBDesc *desc;
    CPUTLBDescFast *fast;

    release_common(ctx);
    for (i = 0; i < NB_MMU_MODES; i++) {
        desc = &(d[i]);
        fast = &(f[i]);
        g_free(desc->iotlb);
        g_free(fast->table);
    }

    // TODO: Anymore to free?
}

static void reg_reset(struct uc_struct *uc)
{
    CPUArchState *env = uc->cpu->env_ptr;

    memset(env->dreg, 0, sizeof(env->dreg));
    memset(env->preg, 0, sizeof(env->preg));
    memset(env->ireg, 0, sizeof(env->ireg));
    memset(env->mreg, 0, sizeof(env->mreg));
    memset(env->breg, 0, sizeof(env->breg));
    memset(env->lreg, 0, sizeof(env->lreg));

    env->rets = 0;

    memset(env->lcreg, 0, sizeof(env->lcreg));
    memset(env->ltreg, 0, sizeof(env->ltreg));
    memset(env->lbreg, 0, sizeof(env->lbreg));

    memset(env->cycles, 0, sizeof(env->cycles));

    env->uspreg = 0;
    env->seqstat = 0;
    env->syscfg = 0;
    env->reti = 0;
    env->retx = 0;
    env->retn = 0;
    env->rete = 0;
    env->emudat = 0;

    env->pc = 0;

    memset(env->astat, 0, sizeof(env->astat));
    env->astat_op = 0;
    memset(env->astat_arg, 0, sizeof(env->astat_arg));

}

DEFAULT_VISIBILITY
uc_err reg_read(void *_env, int mode, unsigned int regid, void *value,
                size_t *size)
{
    CPUBfinState *env = _env;
    uc_err ret = UC_ERR_ARG;

    if (regid >= UC_BFIN_REG_R0 && regid <= UC_BFIN_REG_R7) {
        CHECK_REG_TYPE(uint32_t);
        *(uint32_t *)value = env->dreg[regid - UC_BFIN_REG_R0];
    } else if (regid >= UC_BFIN_REG_P0 && regid <= UC_BFIN_REG_P5) {
        CHECK_REG_TYPE(uint32_t);
        *(uint32_t *)value = env->preg[regid - UC_BFIN_REG_P0];
    } else {
        switch (regid) {
        default:
            break;
        case UC_BFIN_REG_PC:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->pc;
            break;
        case UC_BFIN_REG_RETS:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->rets;
            break;
        case UC_BFIN_REG_RETI:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->reti;
            break;
        case UC_BFIN_REG_RETX:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->retx;
            break;
        case UC_BFIN_REG_RETN:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->retn;
            break;
        case UC_BFIN_REG_RETE:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->rete;
            break;
        case UC_BFIN_REG_EMUDAT:
            CHECK_REG_TYPE(uint32_t);
            *(uint32_t *)value = env->emudat;
            break;
        }
    }
    CHECK_RET_DEPRECATE(ret, regid);
    return ret;
}

DEFAULT_VISIBILITY
uc_err reg_write(void *_env, int mode, unsigned int regid, const void *value,
                 size_t *size, int *setpc)
{
    CPUBfinState *env = _env;
    uc_err ret = UC_ERR_ARG;

    if (regid >= UC_BFIN_REG_R0 && regid <= UC_BFIN_REG_R7) {
        CHECK_REG_TYPE(uint32_t);
        env->dreg[regid - UC_BFIN_REG_R0] = *(uint32_t *)value;
    } else if (regid >= UC_BFIN_REG_P0 && regid <= UC_BFIN_REG_P5) {
        CHECK_REG_TYPE(uint32_t);
        env->preg[regid - UC_BFIN_REG_P0] = *(uint32_t *)value;
    } else {
        switch (regid) {
        default:
            break;
        case UC_BFIN_REG_PC:
            CHECK_REG_TYPE(uint32_t);
            env->pc = *(uint32_t *)value;
            *setpc = 1;
            break;
        }
    }
    CHECK_RET_DEPRECATE(ret, regid);
    return ret;
}

DEFAULT_VISIBILITY
void uc_init(struct uc_struct *uc)
{
    uc->release = bfin_release;
    uc->reg_read = reg_read;
    uc->reg_write = reg_write;
    uc->reg_reset = reg_reset;
    uc->set_pc = bfin_set_pc;
    uc->get_pc = bfin_get_pc;
    uc->cpus_init = bfin_cpus_init;
    uc->cpu_context_size = offsetof(CPUBfinState, end_reset_fields);
    uc_common_init(uc);
}
