/*
 * QEMU Blackfin CPU
 *
 * Copyright 2007-2023 Mike Frysinger
 * Copyright 2007-2011 Analog Devices, Inc.
 *
 * Licensed under the Lesser GPL 2 or later.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "exec/exec-all.h"
#include "migration/vmstate.h"

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

static void bfin_cpu_reset(DeviceState *dev)
{
    CPUState *cs = CPU(dev);
    BlackfinCPU *cpu = BFIN_CPU(cs);
    BlackfinCPUClass *acc = BFIN_CPU_GET_CLASS(cpu);
    CPUArchState *env = &cpu->env;

    acc->parent_reset(dev);

    env->pc = 0xEF000000;
}

static void bfin_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_mach_bfin;
    info->print_insn = print_insn_bfin;
}

static void bfin_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    BlackfinCPUClass *acc = BFIN_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    acc->parent_realize(dev, errp);
}

static ObjectClass *bfin_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    if (!cpu_model) {
        return NULL;
    }

    /* Try bare name first, e.g. bf537-bfin-cpu.  */
    oc = object_class_by_name(cpu_model);
    if (!oc) {
        /* If not found, maybe they used e.g. bf537, so expand it.  */
        char *typename;

        typename = g_strdup_printf(BLACKFIN_CPU_TYPE_NAME("%s"), cpu_model);
        oc = object_class_by_name(typename);
        g_free(typename);
    }

    if (!oc || !object_class_dynamic_cast(oc, TYPE_BLACKFIN_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }

    return oc;
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps bfin_sysemu_ops = {
    .get_phys_page_debug = bfin_cpu_get_phys_page_debug,
};
#endif

#ifdef CONFIG_TCG
static const struct TCGCPUOps bfin_tcg_ops = {
    .initialize = bfin_translate_init,
    .restore_state_to_opc = bfin_restore_state_to_opc,

#if !defined(CONFIG_USER_ONLY)
    .tlb_fill = bfin_cpu_tlb_fill,
    .do_interrupt = bfin_cpu_do_interrupt,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void bfin_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    BlackfinCPUClass *acc = BFIN_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, bfin_cpu_realizefn,
                                    &acc->parent_realize);

    device_class_set_parent_reset(dc, bfin_cpu_reset, &acc->parent_reset);

    cc->class_by_name = bfin_cpu_class_by_name;
    cc->has_work = bfin_cpu_has_work;
    cc->set_pc = bfin_cpu_set_pc;
    cc->gdb_read_register = bfin_cpu_gdb_read_register;
    cc->gdb_write_register = bfin_cpu_gdb_write_register;
    cc->dump_state = bfin_cpu_dump_state;
    cc->disas_set_info = bfin_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    dc->vmsd = &vmstate_bfin_cpu;
    cc->sysemu_ops = &bfin_sysemu_ops;
#endif

#ifdef CONFIG_TCG
    cc->tcg_ops = &bfin_tcg_ops;
#endif /* CONFIG_TCG */
}

static void bf5xx_cpu_initfn(Object *obj)
{
    // All BF5xx cpus have the same ISA (ignoring BF535).  They largely differ
    // in peripherals & on-chip memory (L1/L2).  They also have diff errata that
    // can affect the cores, but we probably won't bother emulating those.
}

#define DEFINE_BLACKFIN_CPU_TYPE(model)         \
    {                                           \
        .parent = TYPE_BLACKFIN_CPU,            \
        .instance_init = bf5xx_cpu_initfn,      \
        .name = BLACKFIN_CPU_TYPE_NAME(model),  \
    }

static const TypeInfo bfin_cpu_type_infos[] = {
    {
        .name = TYPE_BLACKFIN_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(BlackfinCPU),
        .abstract = true,
        .class_size = sizeof(BlackfinCPUClass),
        .class_init = bfin_cpu_class_init,
    },
    DEFINE_BLACKFIN_CPU_TYPE("bf504"),
    DEFINE_BLACKFIN_CPU_TYPE("bf506"),
    DEFINE_BLACKFIN_CPU_TYPE("bf512"),
    DEFINE_BLACKFIN_CPU_TYPE("bf514"),
    DEFINE_BLACKFIN_CPU_TYPE("bf516"),
    DEFINE_BLACKFIN_CPU_TYPE("bf518"),
    DEFINE_BLACKFIN_CPU_TYPE("bf522"),
    DEFINE_BLACKFIN_CPU_TYPE("bf523"),
    DEFINE_BLACKFIN_CPU_TYPE("bf524"),
    DEFINE_BLACKFIN_CPU_TYPE("bf525"),
    DEFINE_BLACKFIN_CPU_TYPE("bf526"),
    DEFINE_BLACKFIN_CPU_TYPE("bf527"),
    DEFINE_BLACKFIN_CPU_TYPE("bf531"),
    DEFINE_BLACKFIN_CPU_TYPE("bf532"),
    DEFINE_BLACKFIN_CPU_TYPE("bf533"),
    DEFINE_BLACKFIN_CPU_TYPE("bf534"),
  /*DEFINE_BLACKFIN_CPU_TYPE("bf535"),*/
    DEFINE_BLACKFIN_CPU_TYPE("bf536"),
    DEFINE_BLACKFIN_CPU_TYPE("bf537"),
    DEFINE_BLACKFIN_CPU_TYPE("bf538"),
    DEFINE_BLACKFIN_CPU_TYPE("bf539"),
    DEFINE_BLACKFIN_CPU_TYPE("bf542"),
    DEFINE_BLACKFIN_CPU_TYPE("bf544"),
    DEFINE_BLACKFIN_CPU_TYPE("bf547"),
    DEFINE_BLACKFIN_CPU_TYPE("bf548"),
    DEFINE_BLACKFIN_CPU_TYPE("bf549"),
    DEFINE_BLACKFIN_CPU_TYPE("bf561"),
    DEFINE_BLACKFIN_CPU_TYPE("bf592"),
#ifdef CONFIG_USER_ONLY
    DEFINE_BLACKFIN_CPU_TYPE("any"),
#endif
};

DEFINE_TYPES(bfin_cpu_type_infos);
