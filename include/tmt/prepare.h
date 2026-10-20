/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TMT_PREPARE_H
#define TMT_PREPARE_H
#include <musica/musica.h>
typedef struct {
  const char *python;
  const char *compiler;
  const char *tools_directory;
  const char *fable_directory;
  const char *include_directory;
  const char *runtime_library;
  const char *cache_directory;
  int width;                 /* 0 selects 2; supported widths: 1, 2, 4, 8 */
  const char *mode;           /* NULL selects exact; exact or fast */
  const char *ordering;       /* NULL selects markowitz or minimum-fill */
} TmtPrepareOptions;
/* Zero initialize options or pass NULL for build/installation defaults.
 * Paths containing spaces are supported. Processes never use a shell.
 * On failure diagnostics name an isolated work directory in the cache.
 * Source preparation (including cache lookup) requires Python. */
void TmtPrepareMechanism(const char *, const TmtPrepareOptions *, String *, Error *);
/* Returns the ABI identity that prepared artifacts must match. */
const char *TmtNativeIdentity(void);
#endif
