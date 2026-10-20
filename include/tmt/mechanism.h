/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TMT_MECHANISM_H
#define TMT_MECHANISM_H
#include <tmc.h>
#define TMT_MECHANISM_ABI 1
/* Allocations returned by these functions belong to the single Fable runtime.
 * The module must stay loaded until its solver and states have been freed. */
typedef struct {
  unsigned abi;
  unsigned size;
  const char *native_identity;
  const char *cache_key;
  int width;
  double relative_tolerance;
  tmc_system (*system)(void);
  int (*reactions)(tmc_reaction **);
  int (*initial_state)(tmc_state *);
  const tmc_kernels *(*kernels)(void);
} TmtMechanismV1;
typedef const TmtMechanismV1 *(*TmtMechanismEntry)(void);
#endif
