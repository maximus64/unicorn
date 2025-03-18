/*
 * QEMU Blackfin CPU
 *
 * Copyright 2007-2023 Mike Frysinger
 * Copyright 2007-2011 Analog Devices, Inc.
 *
 * Licensed under the Lesser GPL 2 or later.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"

static void bfin_cpu_set_pc(CPUState *cs, vaddr value)
{
    BlackfinCPU *cpu = BFIN_CPU(cs);
    CPUArchState *env = &cpu->env;

    env->pc = value;
}

static void bfin_restore_state_to_opc(CPUState *cs,
                                      const TranslationBlock *tb,
                                      const uint64_t *data)
{
    BlackfinCPU *cpu = BFIN_CPU(cs);
    CPUArchState *env = &cpu->env;

    env->pc = data[0];
}

static bool bfin_cpu_has_work(CPUState *cpu)
{
    return cpu->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}

static void bfin_cpu_reset(CPUState *dev)
{
    CPUState *cs = CPU(dev);
    BlackfinCPU *cpu = BFIN_CPU(cs);
    BlackfinCPUClass *acc = BFIN_CPU_GET_CLASS(cpu);
    CPUArchState *env = &cpu->env;

    acc->parent_reset(dev);

    env->pc = 0xEF000000;
}

static void bfin_cpu_realizefn(struct uc_struct *uc, CPUState *dev)
{
    CPUState *cs = CPU(dev);

    cpu_exec_realizefn(cs);

    cpu_reset(cs);
    qemu_init_vcpu(cs);
}

static void bfin_cpu_class_init(struct uc_struct *uc, CPUClass *oc)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->has_work = bfin_cpu_has_work;
    cc->set_pc = bfin_cpu_set_pc;
#ifdef CONFIG_TCG
    cc->tcg_initialize = bfin_translate_init;
    cc->tlb_fill = bfin_cpu_tlb_fill;
#endif
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_debug = bfin_cpu_get_phys_page_debug;
#endif
}

static void bf5xx_cpu_initfn(struct uc_struct *uc, CPUState *obj)
{
    // All BF5xx cpus have the same ISA (ignoring BF535).  They largely differ
    // in peripherals & on-chip memory (L1/L2).  They also have diff errata that
    // can affect the cores, but we probably won't bother emulating those.
}

BlackfinCPU *cpu_bfin_init(struct uc_struct *uc)
{
    BlackfinCPU *cpu;
    CPUState *cs;
    CPUClass *cc;

    cpu = qemu_memalign(8, sizeof(*cpu));
    if (cpu == NULL) {
        return NULL;
    }
    memset((void*)cpu, 0, sizeof(*cpu));

    cs = (CPUState *)cpu;
    cc = (CPUClass *)&cpu->cc;
    cs->cc = cc;
    cs->uc = uc;
    uc->cpu = cs;

    /* init CPUClass */
    cpu_class_init(uc, cc);
    /* init SPARCCPUClass */
    bfin_cpu_class_init(uc, cc);
    /* init CPUState */
    cpu_common_initfn(uc, cs);

    /* init SPARCCPU */
    bf5xx_cpu_initfn(uc, cs);
    /* realize SPARCCPU */
    bfin_cpu_realizefn(uc, cs);
    /* realize CPUState */

    // init address space
    cpu_address_space_init(cs, 0, cs->memory);

    qemu_init_vcpu(cs);

    return cpu;
}
