#ifndef TMT_LOADER_H
#define TMT_LOADER_H
#include <tmt/mechanism.h>
#include <musica/musica.h>
typedef struct { void *library; const TmtMechanismV1 *api; } TmtModule;
int tmt_load_module(const char *, TmtModule *, Error *);
void tmt_unload_module(TmtModule *);
#endif
