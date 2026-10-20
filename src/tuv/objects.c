/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#include "objects.h"
#include <math.h>

#define CHECK(obj,ret) do { NoError(e); if(!(obj)) {tmt_error(e,"Null object"); return ret;} } while(0)
static bool array_ok(const double *a,size_t n,Error *e) {
    if(n && !a) {tmt_error(e,"Null array");return false;}
    for(size_t i=0;i<n;++i) if(!isfinite(a[i])) {tmt_error(e,"Nonfinite array value");return false;}
    return true;
}
Grid *CreateGrid(const char *name,const char *units,size_t n,Error *e) {
    NoError(e);
    if(!name || !units || n>SIZE_MAX/sizeof(double)-1) {tmt_error(e,"Invalid grid");return NULL;}
    Grid *g=calloc(1,sizeof(*g));
    if(!g) {tmt_error(e,"Out of memory");return NULL;}
    g->refs=1;g->host=true;g->name=tmt_strdup(name);g->units=tmt_strdup(units);
    double *edges=calloc(n+1,sizeof(double));
    if(!g->name || !g->units || !edges) {free(edges);DeleteGrid(g,NULL);tmt_error(e,"Out of memory");return NULL;}
    for(size_t i=0;i<=n;++i) edges[i]=(double)i;
    tt_status code=tt_grid_from_edges(n+1,edges,&g->data); free(edges);
    if(!tmt_status(code,NULL,e)) {DeleteGrid(g,NULL);return NULL;} return g;
}
void DeleteGrid(Grid *g,Error *e) {NoError(e);if(g && --g->refs==0){tt_grid_destroy(&g->data);free(g->name);free(g->units);free(g);}}
const char *GetGridName(Grid *g,Error *e){CHECK(g,NULL);return g->name;}
const char *GetGridUnits(Grid *g,Error *e){CHECK(g,NULL);return g->units;}
size_t GetGridNumberOfSections(Grid *g,Error *e){CHECK(g,0);return g->data.cells;}
void SetGridEdges(Grid *g,double *v,size_t n,Error *e){
    CHECK(g,);if(!g->host || n!=g->data.cells+1){tmt_error(e,"Grid is read-only or dimensions differ");return;}
    if(array_ok(v,n,e)) tmt_status(tt_grid_update(&g->data,v,NULL),NULL,e);
}
void SetGridMidpoints(Grid *g,double *v,size_t n,Error *e){
    CHECK(g,);if(!g->host || n!=g->data.cells){tmt_error(e,"Grid is read-only or dimensions differ");return;}
    if(array_ok(v,n,e)) tmt_status(tt_grid_update(&g->data,g->data.edges,v),NULL,e);
}
#define GRID_GET(Name,field,extra) \
void GetGrid##Name(Grid *g,double *v,size_t n,Error *e){CHECK(g,);if(!v || n!=g->data.cells+extra){tmt_error(e,"Grid dimensions differ");return;}memcpy(v,g->data.field,n*sizeof(double));} \
double *GetGrid##Name##Pointer(Grid *g,Error *e){CHECK(g,NULL);return g->data.field;}
GRID_GET(Edges,edges,1)
GRID_GET(Midpoints,midpoints,0)

Profile *CreateProfile(const char *name,const char *units,Grid *grid,Error *e){
    NoError(e);if(!name || !units || !grid){tmt_error(e,"Invalid profile");return NULL;}
    Profile *p=calloc(1,sizeof(*p));if(!p){tmt_error(e,"Out of memory");return NULL;}
    p->refs=1;p->host=true;p->name=tmt_strdup(name);p->units=tmt_strdup(units);
    p->layers=calloc(grid->data.cells+1,sizeof(double));
    if(!p->name || !p->units || !p->layers || !tmt_status(tt_profile_create(grid->data.cells,&p->data),NULL,e)){
        DeleteProfile(p,NULL);if(!e || !e->code_)tmt_error(e,"Out of memory");return NULL;
    }return p;
}
void DeleteProfile(Profile *p,Error *e){NoError(e);if(p && --p->refs==0){tt_profile_destroy(&p->data);free(p->layers);free(p->name);free(p->units);free(p);}}
const char *GetProfileName(Profile *p,Error *e){CHECK(p,NULL);return p->name;}
const char *GetProfileUnits(Profile *p,Error *e){CHECK(p,NULL);return p->units;}
size_t GetProfileNumberOfSections(Profile *p,Error *e){CHECK(p,0);return p->data.cells;}
int tmt_profile_refresh(Profile *p,Error *e){return tmt_status(tt_profile_update(&p->data,p->data.edges,p->data.midpoints,p->layers,NULL,&p->top),NULL,e);}
#define PROFILE_ACCESS(Name,field,extra) \
void SetProfile##Name(Profile *p,double *v,size_t n,Error *e){CHECK(p,);if(!p->host || n!=p->data.cells+extra){tmt_error(e,"Profile is read-only or dimensions differ");return;}if(array_ok(v,n,e)){memcpy(p->field,v,n*sizeof(double));tmt_profile_refresh(p,e);}} \
void GetProfile##Name(Profile *p,double *v,size_t n,Error *e){CHECK(p,);if(!v || n!=p->data.cells+extra){tmt_error(e,"Profile dimensions differ");return;}memcpy(v,p->field,n*sizeof(double));} \
double *GetProfile##Name##Pointer(Profile *p,Error *e){CHECK(p,NULL);return p->field;}
PROFILE_ACCESS(EdgeValues,data.edges,1)
PROFILE_ACCESS(MidpointValues,data.midpoints,0)
void SetProfileLayerDensities(Profile *p,double *v,size_t n,Error *e){CHECK(p,);if(!p->host || !v || n!=p->data.cells){tmt_error(e,"Profile is read-only or dimensions differ");return;}memcpy(p->layers,v,n*sizeof(double));tmt_profile_refresh(p,e);}
void GetProfileLayerDensities(Profile *p,double *v,size_t n,Error *e){CHECK(p,);if(!v || n!=p->data.cells){tmt_error(e,"Profile dimensions differ");return;}memcpy(v,p->data.columns,n*sizeof(double));}
double *GetProfileLayerDensitiesPointer(Profile *p,Error *e){CHECK(p,NULL);return p->data.columns;}
void SetProfileExoLayerDensity(Profile *p,double v,Error *e){CHECK(p,);if(!p->host || !isfinite(v) || v<0){tmt_error(e,"Invalid or read-only exo-layer density");return;}p->top=v;tmt_profile_refresh(p,e);}
void CalculateProfileExoLayerDensity(Profile *p,double scale,Error *e){CHECK(p,);memcpy(p->layers,p->data.columns,p->data.cells*sizeof(double));SetProfileExoLayerDensity(p,p->data.edges[p->data.cells]*scale,e);}
double GetProfileExoLayerDensity(Profile *p,Error *e){CHECK(p,0);return p->top;}

Radiator *CreateRadiator(const char *name,Grid *height,Grid *wavelength,Error *e){
    NoError(e);size_t n;
    if(!name || !height || !wavelength || !tmt_size_product(height->data.cells,wavelength->data.cells,&n) || n>SIZE_MAX/sizeof(double)/3){tmt_error(e,"Invalid radiator");return NULL;}
    Radiator *r=calloc(1,sizeof(*r));if(!r){tmt_error(e,"Out of memory");return NULL;}
    r->refs=1;r->host=true;r->name=tmt_strdup(name);r->nz=height->data.cells;r->nw=wavelength->data.cells;
    r->od=calloc(n?n*3:1,sizeof(double));
    if(!r->name || !r->od){DeleteRadiator(r,NULL);tmt_error(e,"Out of memory");return NULL;}
    r->ssa=r->od+n;r->g=r->ssa+n;return r;
}
void DeleteRadiator(Radiator *r,Error *e){NoError(e);if(r && --r->refs==0){free(r->name);free(r->od);free(r);}}
const char *GetRadiatorName(Radiator *r,Error *e){CHECK(r,NULL);return r->name;}
size_t GetRadiatorNumberOfHeightSections(Radiator *r,Error *e){CHECK(r,0);return r->nz;}
size_t GetRadiatorNumberOfWavelengthSections(Radiator *r,Error *e){CHECK(r,0);return r->nw;}
#define RAD_ACCESS(Name,field,minimum,maximum) \
void SetRadiator##Name(Radiator *r,double *v,size_t nz,size_t nw,Error *e){CHECK(r,);if(!r->host || nz!=r->nz || nw!=r->nw){tmt_error(e,"Radiator is read-only or dimensions differ");return;}size_t n=nz*nw;if(!array_ok(v,n,e))return;memcpy(r->field,v,n*sizeof(double));} \
void GetRadiator##Name(Radiator *r,double *v,size_t nz,size_t nw,Error *e){CHECK(r,);if(!v || nz!=r->nz || nw!=r->nw){tmt_error(e,"Radiator dimensions differ");return;}memcpy(v,r->field,nz*nw*sizeof(double));} \
double *GetRadiator##Name##Pointer(Radiator *r,Error *e){CHECK(r,NULL);return r->field;}
RAD_ACCESS(OpticalDepths,od,0,INFINITY)
RAD_ACCESS(SingleScatteringAlbedos,ssa,0,1)
void SetRadiatorAsymmetryFactors(Radiator *r,double *v,size_t nz,size_t nw,size_t ns,Error *e){
    CHECK(r,);if(!r->host || !v || nz!=r->nz || nw!=r->nw || ns!=1){tmt_error(e,"Asymmetry requires one factor per layer and wavelength");return;}
    size_t n=nz*nw;if(!array_ok(v,n,e))return;
    memcpy(r->g,v,n*sizeof(double));
}
void GetRadiatorAsymmetryFactors(Radiator *r,double *v,size_t nz,size_t nw,size_t ns,Error *e){
    CHECK(r,);if(!v || nz!=r->nz || nw!=r->nw || !ns){tmt_error(e,"Radiator dimensions differ");return;}
    /* Legendre coefficients are powers of the asymmetry factor, as in TUV-x. */
    for(size_t m=0;m<ns;++m)for(size_t i=0;i<nz*nw;++i)v[m*nz*nw+i]=pow(r->g[i],(double)m+1);
}
double *GetRadiatorAsymmetryFactorsPointer(Radiator *r,Error *e){CHECK(r,NULL);return r->g;}

/* Maps retain objects. Get returns an owned reference, released with Delete. */
#define MAP_BASIC(Type,accessible) \
Type##Map *Create##Type##Map(Error *e){NoError(e);Type##Map *m=calloc(1,sizeof(*m));if(m)m->refs=1;else tmt_error(e,"Out of memory");return m;} \
void Delete##Type##Map(Type##Map *m,Error *e){NoError(e);if(m && --m->refs==0){for(size_t i=0;i<m->n;++i)Delete##Type(m->items[i],NULL);free(m->items);free(m);}} \
size_t GetNumberOf##Type##s(Type##Map *m,Error *e){CHECK(m,0);return m->n;} \
Type *Get##Type##ByIndex(Type##Map *m,size_t i,Error *e){CHECK(m,NULL);if(i>=m->n){tmt_error(e,"Map index out of range");return NULL;}if(!(accessible)){tmt_error(e,"Configured object is not host-accessible");return NULL;}m->items[i]->refs++;return m->items[i];} \
void Remove##Type##ByIndex(Type##Map *m,size_t i,Error *e){CHECK(m,);if(m->sealed || i>=m->n){tmt_error(e,"Map is read-only or index out of range");return;}Delete##Type(m->items[i],NULL);memmove(m->items+i,m->items+i+1,(m->n-i-1)*sizeof(*m->items));m->n--;}
MAP_BASIC(Grid,true)
MAP_BASIC(Profile,m->items[i]->host)
MAP_BASIC(Radiator,m->items[i]->host)
#define MAP_NAMED(Type) \
Type *Get##Type(Type##Map *m,const char *name,const char *units,Error *e){CHECK(m,NULL);if(name && units)for(size_t i=0;i<m->n;++i)if(!strcmp(m->items[i]->name,name)&&!strcmp(m->items[i]->units,units))return Get##Type##ByIndex(m,i,e);tmt_error(e,"Map name/units not found");return NULL;} \
void Remove##Type(Type##Map *m,const char *name,const char *units,Error *e){CHECK(m,);if(name && units)for(size_t i=0;i<m->n;++i)if(!strcmp(m->items[i]->name,name)&&!strcmp(m->items[i]->units,units)){Remove##Type##ByIndex(m,i,e);return;}tmt_error(e,"Map name/units not found");}
MAP_NAMED(Grid)
MAP_NAMED(Profile)
Radiator *GetRadiator(RadiatorMap *m,const char *name,Error *e){CHECK(m,NULL);if(name)for(size_t i=0;i<m->n;++i)if(!strcmp(m->items[i]->name,name))return GetRadiatorByIndex(m,i,e);tmt_error(e,"Radiator not found");return NULL;}
void RemoveRadiator(RadiatorMap *m,const char *name,Error *e){CHECK(m,);if(name)for(size_t i=0;i<m->n;++i)if(!strcmp(m->items[i]->name,name)){RemoveRadiatorByIndex(m,i,e);return;}tmt_error(e,"Radiator not found");}
#define MAP_ADD(Type) \
void Add##Type(Type##Map *m,Type *p,Error *e){CHECK(m,);if(!p || m->sealed){tmt_error(e,"Invalid object or read-only map");return;}for(size_t i=0;i<m->n;++i)if(!strcmp(m->items[i]->name,p->name)){tmt_error(e,"Duplicate map name");return;}Type **items=realloc(m->items,(m->n+1)*sizeof(*items));if(!items){tmt_error(e,"Out of memory");return;}m->items=items;m->items[m->n++]=p;p->refs++;}
MAP_ADD(Grid)
MAP_ADD(Profile)
MAP_ADD(Radiator)
