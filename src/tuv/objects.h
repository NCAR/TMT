#ifndef TMT_TUV_OBJECTS_H
#define TMT_TUV_OBJECTS_H
#include "internal.h"
#include <musica/tuvx.h>
#include <turbotuv_model.h>
struct Grid { size_t refs; char *name,*units; tt_grid data; bool host; };
struct Profile { size_t refs; char *name,*units; tt_profile data; double *layers,top; bool host; };
struct Radiator { size_t refs; char *name; size_t nz,nw; double *od,*ssa,*g; bool host; };
struct GridMap { size_t refs,n; Grid **items; bool sealed; };
struct ProfileMap { size_t refs,n; Profile **items; bool sealed; };
struct RadiatorMap { size_t refs,n; Radiator **items; bool sealed; };
struct TUVX { size_t refs; tt_model *model; GridMap *grids; ProfileMap *profiles; RadiatorMap *radiators; bool host_radiation; };
struct RadiationFieldUpdater { TUVX *model; };
int tmt_profile_refresh(Profile *, Error *);
#endif
