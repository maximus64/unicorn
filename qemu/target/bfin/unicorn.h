#ifndef UC_QEMU_TARGET_BFIN_H
#define UC_QEMU_TARGET_BFIN_H

// functions to read & write registers
uc_err reg_read_bfin(void *env, int mode, unsigned int regid, void *value,
                      size_t *size);
uc_err reg_write_bfin(void *env, int mode, unsigned int regid,
                       const void *value, size_t *size, int *setpc);

void uc_init_bfin(struct uc_struct *uc);
#endif
