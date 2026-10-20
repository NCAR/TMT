#ifndef TMT_INTERNAL_H
#define TMT_INTERNAL_H
#include <musica/musica.h>
#include <turbotuv_config.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
char *tmt_strdup(const char *);
void tmt_error(Error *, const char *);
int tmt_status(tt_status, const tt_error *, Error *);
int tmt_size_product(size_t, size_t, size_t *);
#endif
