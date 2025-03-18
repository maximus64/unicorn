/* This file is released under LGPL2.
   See COPYING.LGPL2 in root directory for more details
*/

#ifndef UNICORN_BFIN_H
#define UNICORN_BFIN_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4201)
#endif

//> Blackfin registers
typedef enum uc_bfin_reg {
    UC_BFIN_REG_INVALID = 0,
    //> General purpose registers
    UC_BFIN_REG_A0,
    UC_BFIN_REG_A1,

    UC_BFIN_REG_R0,
    UC_BFIN_REG_R1,
    UC_BFIN_REG_R2,
    UC_BFIN_REG_R3,
    UC_BFIN_REG_R4,
    UC_BFIN_REG_R5,
    UC_BFIN_REG_R6,
    UC_BFIN_REG_R7,

    UC_BFIN_REG_P0,
    UC_BFIN_REG_P1,
    UC_BFIN_REG_P2,
    UC_BFIN_REG_P3,
    UC_BFIN_REG_P4,
    UC_BFIN_REG_P5,
    UC_BFIN_REG_SP,
    UC_BFIN_REG_FP,

    UC_BFIN_REG_LT0,
    UC_BFIN_REG_LT1,

    UC_BFIN_REG_LC0,
    UC_BFIN_REG_LC1,

    UC_BFIN_REG_LB0,
    UC_BFIN_REG_LB1,

    UC_BFIN_REG_I0,
    UC_BFIN_REG_I1,
    UC_BFIN_REG_I2,
    UC_BFIN_REG_I3,

    UC_BFIN_REG_M0,
    UC_BFIN_REG_M1,
    UC_BFIN_REG_M2,
    UC_BFIN_REG_M3,

    UC_BFIN_REG_B0,
    UC_BFIN_REG_B1,
    UC_BFIN_REG_B2,
    UC_BFIN_REG_B3,

    UC_BFIN_REG_L0,
    UC_BFIN_REG_L1,
    UC_BFIN_REG_L2,
    UC_BFIN_REG_L3,

    UC_BFIN_REG_ASTAT,
    UC_BFIN_REG_RETS,
    UC_BFIN_REG_RETI,
    UC_BFIN_REG_RETX,
    UC_BFIN_REG_RETN,
    UC_BFIN_REG_RETE,

    UC_BFIN_REG_EMUDAT,
    UC_BFIN_REG_SEQSTAT,
    UC_BFIN_REG_SYSCFG,
    UC_BFIN_REG_USP,

    UC_BFIN_REG_PC, // PC register

    UC_BFIN_REG_ENDING, // <-- mark the end of the list or registers

    //> Alias registers
} uc_bfin_reg;

#ifdef __cplusplus
}
#endif

#endif
