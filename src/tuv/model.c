/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#include "objects.h"

static const tt_dataset *resolve_dataset(void *context,const char *path) {
    (void)context;
    if(!path)return NULL;
    /* MUSICA's distributed configurations use this package-resource prefix. */
    const char prefix[]="configs/tuvx/";
    if(!strncmp(path,prefix,sizeof(prefix)-1))path+=sizeof(prefix)-1;
    return tt_dataset_find(path);
}

static TUVX *create_model(tt_config *doc,GridMap *grids,ProfileMap *profiles,RadiatorMap *radiators,Error *e) {
    TUVX *t=calloc(1,sizeof(*t));
    if(!t){tmt_error(e,"Out of memory");return NULL;}
    t->refs=1;t->grids=CreateGridMap(e);t->profiles=CreateProfileMap(e);t->radiators=CreateRadiatorMap(e);
    size_t ng=grids?grids->n:0,np=profiles?profiles->n:0,nr=radiators?radiators->n:0;
    tt_named_grid *gs=calloc(ng?ng:1,sizeof(*gs));
    tt_named_profile *ps=calloc(np?np:1,sizeof(*ps));
    tt_named_radiator *rs=calloc(nr?nr:1,sizeof(*rs));
    if(!t->grids || !t->profiles || !t->radiators || !gs || !ps || !rs){tmt_error(e,"Out of memory");goto fail;}
    for(size_t i=0;i<ng;++i){Grid *g=grids->items[i];gs[i]=(tt_named_grid){g->name,g->units,&g->data};AddGrid(t->grids,g,e);if(e && e->code_)goto fail;}
    for(size_t i=0;i<np;++i){Profile *p=profiles->items[i];if(!tmt_profile_refresh(p,e))goto fail;ps[i]=(tt_named_profile){p->name,p->units,&p->data};AddProfile(t->profiles,p,e);if(e && e->code_)goto fail;}
    for(size_t i=0;i<nr;++i){Radiator *r=radiators->items[i];rs[i]=(tt_named_radiator){r->name,r->nz,r->nw,{r->od,r->ssa,r->g,0}};AddRadiator(t->radiators,r,e);if(e && e->code_)goto fail;}
    tt_model_inputs inputs={ng,gs,np,ps,nr,rs};tt_error detail={0};tt_config_node root=tt_config_root(doc);
    tt_data_source data={resolve_dataset,NULL};
    if(!tmt_status(tt_model_create_with_inputs(root,&data,&inputs,&t->model,&detail),&detail,e))goto fail;
    const char *solver=tt_config_string(tt_config_get(tt_config_get(tt_config_get(root,"radiative transfer"),"solver"),"type"));
    t->host_radiation=solver && !strcmp(solver,"from host");
    /* Configured objects are independent read-only snapshots. Host objects are
     * retained and synchronized at each run, including direct pointer edits. */
    tt_config_node list=tt_config_get(root,"grids");
    for(size_t i=0;i<tt_config_size(list);++i){
        tt_config_node item=tt_config_at(list,i);
        const char *name=tt_config_string(tt_config_get(item,"name")),*units=tt_config_string(tt_config_get(item,"units"));
        const tt_grid *v=tt_model_grid(t->model,name,units);
        if(!v){tmt_error(e,"Configured grid unavailable");goto fail;}
        Grid *g=CreateGrid(name,units,v->cells,e);if(!g)goto fail;
        if(!tmt_status(tt_grid_update(&g->data,v->edges,v->midpoints),NULL,e)){DeleteGrid(g,NULL);goto fail;}
        g->host=false;AddGrid(t->grids,g,e);DeleteGrid(g,NULL);if(e && e->code_)goto fail;
    }
    list=tt_config_get(root,"profiles");
    for(size_t i=0;i<tt_config_size(list);++i){
        tt_config_node item=tt_config_at(list,i);
        const char *name=tt_config_string(tt_config_get(item,"name")),*units=tt_config_string(tt_config_get(item,"units"));
        const tt_profile *v=tt_model_profile(t->model,name,units);
        if(!v){tmt_error(e,"Configured profile unavailable");goto fail;}
        Grid shape={0};shape.data.cells=v->cells;
        Profile *p=CreateProfile(name,units,&shape,e);if(!p)goto fail;
        size_t n=v->cells;
        memcpy(p->data.edges,v->edges,(n+1)*sizeof(double));
        memcpy(p->data.midpoints,v->midpoints,n*sizeof(double));
        memcpy(p->data.differences,v->differences,n*sizeof(double));
        memcpy(p->data.columns,v->columns,n*sizeof(double));
        memcpy(p->data.extended_columns,v->extended_columns,(n+1)*sizeof(double));
        memcpy(p->data.burden,v->burden,n*sizeof(double));
        memcpy(p->layers,v->extended_columns,n*sizeof(double));p->top=v->extended_columns[n];
        p->host=false;AddProfile(t->profiles,p,e);DeleteProfile(p,NULL);if(e && e->code_)goto fail;
    }
    list=tt_config_get(tt_config_get(root,"radiative transfer"),"radiators");
    tt_model_dimensions dimensions=tt_model_shape(t->model);
    for(size_t i=0;i<tt_config_size(list);++i){
        const char *name=tt_config_string(tt_config_get(tt_config_at(list,i),"name"));
        const tt_radiator *v=tt_model_radiator(t->model,name);
        if(!v){tmt_error(e,"Configured radiator unavailable");goto fail;}
        Grid z={0},w={0};z.data.cells=dimensions.levels-1;w.data.cells=dimensions.wavelengths;
        Radiator *r=CreateRadiator(name,&z,&w,e);if(!r)goto fail;
        size_t n=r->nz*r->nw;
        memcpy(r->od,v->optical_depth,n*sizeof(double));
        memcpy(r->ssa,v->single_scattering_albedo,n*sizeof(double));
        memcpy(r->g,v->asymmetry,n*sizeof(double));
        r->host=false;AddRadiator(t->radiators,r,e);DeleteRadiator(r,NULL);if(e && e->code_)goto fail;
    }
    free(gs);free(ps);free(rs);NoError(e);return t;
fail:
    free(gs);free(ps);free(rs);DeleteTuvx(t,NULL);return NULL;
}
TUVX *CreateTuvx(const char *path,GridMap *g,ProfileMap *p,RadiatorMap *r,Error *e){
    NoError(e);if(!path){tmt_error(e,"Null configuration path");return NULL;}
    tt_config *doc=NULL;tt_error detail={0};
    if(!tmt_status(tt_config_read_file(path,&doc,&detail),&detail,e))return NULL;
    TUVX *t=create_model(doc,g,p,r,e);tt_config_destroy(doc);return t;
}
TUVX *CreateTuvxFromConfigString(const char *text,GridMap *g,ProfileMap *p,RadiatorMap *r,Error *e){
    NoError(e);if(!text){tmt_error(e,"Null configuration string");return NULL;}
    tt_config *doc=NULL;tt_error detail={0};
    if(!tmt_status(tt_config_parse(text,strlen(text),&doc,&detail),&detail,e))return NULL;
    TUVX *t=create_model(doc,g,p,r,e);tt_config_destroy(doc);return t;
}
void DeleteTuvx(const TUVX *handle,Error *e){
    NoError(e);TUVX *t=(TUVX *)handle;
    if(t && --t->refs==0){tt_model_destroy(t->model);DeleteGridMap(t->grids,NULL);DeleteProfileMap(t->profiles,NULL);DeleteRadiatorMap(t->radiators,NULL);free(t);}
}
#define MODEL_MAP(Type,field) \
Type##Map *Get##Type##Map(TUVX *t,Error *e){NoError(e);if(!t){tmt_error(e,"Null model");return NULL;}t->field->refs++;return t->field;}
MODEL_MAP(Grid,grids)
MODEL_MAP(Profile,profiles)
MODEL_MAP(Radiator,radiators)
static void rate_map(TUVX *t,tt_rate_kind kind,Mappings *out,Error *e){
    NoError(e);if(!t || !out){tmt_error(e,"Null model or mappings");return;}
    tt_model_dimensions s=tt_model_shape(t->model);
    size_t n=kind==TT_RATE_PHOTOLYSIS?s.photolysis:kind==TT_RATE_HEATING?s.heating:s.dose;
    CreateMappings(n,out);
    if(n && !out->mappings_){tmt_error(e,"Out of memory");return;}
    for(size_t i=0;i<n;++i){CreateString(tt_model_rate_name(t->model,kind,i),&out->mappings_[i].name_);out->mappings_[i].index_=i;}
}
void GetPhotolysisRateConstantsOrdering(TUVX *t,Mappings *m,Error *e){rate_map(t,TT_RATE_PHOTOLYSIS,m,e);}
void GetHeatingRatesOrdering(TUVX *t,Mappings *m,Error *e){rate_map(t,TT_RATE_HEATING,m,e);}
void GetDoseRatesOrdering(TUVX *t,Mappings *m,Error *e){rate_map(t,TT_RATE_DOSE,m,e);}
void TmtTuvxDimensions(TUVX *t,size_t *z,size_t *w,size_t *p,size_t *h,size_t *d,Error *e){
    NoError(e);if(!t){tmt_error(e,"Null model");return;}tt_model_dimensions s=tt_model_shape(t->model);
    if(z)*z=s.levels;
    if(w)*w=s.wavelengths;
    if(p)*p=s.photolysis;
    if(h)*h=s.heating;
    if(d)*d=s.dose;
}
static int synchronize(TUVX *t,Error *e){
    for(size_t i=0;i<t->grids->n;++i){Grid *g=t->grids->items[i];if(!g->host)continue;
        const tt_grid *v=tt_model_grid(t->model,g->name,g->units);if(!v)continue;
        if(v->cells!=g->data.cells){tmt_error(e,"Grid dimensions changed");return 0;}
        if(!memcmp(v->edges,g->data.edges,(v->cells+1)*sizeof(double)) && !memcmp(v->midpoints,g->data.midpoints,v->cells*sizeof(double)))continue;
        if(!tmt_status(tt_model_update_grid(t->model,g->name,g->units,g->data.cells,g->data.edges,g->data.midpoints),NULL,e))return 0;
    }
    for(size_t i=0;i<t->profiles->n;++i){Profile *p=t->profiles->items[i];if(!p->host || !tt_model_profile(t->model,p->name,p->units))continue;
        size_t n=p->data.cells;memcpy(p->layers,p->data.columns,n*sizeof(double));if(n)p->layers[n-1]-=p->top;
        if(!tmt_status(tt_model_update_profile(t->model,p->name,p->units,n,p->data.edges,p->data.midpoints,p->layers,NULL,&p->top),NULL,e))return 0;
    }
    for(size_t i=0;i<t->radiators->n;++i){Radiator *r=t->radiators->items[i];if(!r->host || !tt_model_radiator(t->model,r->name))continue;
        tt_named_radiator named={r->name,r->nz,r->nw,{r->od,r->ssa,r->g,0}};
        if(!tmt_status(tt_model_set_radiator(t->model,&named),NULL,e))return 0;
    }return 1;
}
void RunTuvx(TUVX *t,double zenith,double distance,double *photo,double *heat,double *dose,double *flux,double *irr,Error *e){
    NoError(e);if(!t){tmt_error(e,"Null model");return;}if(!synchronize(t,e))return;
    const tt_model_result *result=NULL;tt_error detail={0};
    if(!tmt_status(tt_model_run(t->model,zenith,distance,&result,&detail),&detail,e))return;
    tt_model_dimensions s=tt_model_shape(t->model);
    if(photo)memcpy(photo,result->photolysis,s.levels*s.photolysis*sizeof(double));
    if(heat)memcpy(heat,result->heating,s.levels*s.heating*sizeof(double));
    if(dose)memcpy(dose,result->dose,s.levels*s.dose*sizeof(double));
    for(size_t i=0;i<s.levels*s.wavelengths;++i){
        if(flux){flux[3*i]=result->radiation.direct_flux[i];flux[3*i+1]=result->radiation.up_flux[i];flux[3*i+2]=result->radiation.down_flux[i];}
        if(irr){irr[3*i]=result->radiation.direct_irradiance[i];irr[3*i+1]=result->radiation.up_irradiance[i];irr[3*i+2]=result->radiation.down_irradiance[i];}
    }
    /* Refresh diagnostic snapshots after dynamically calculated optics. */
    for(size_t i=0;i<t->radiators->n;++i){Radiator *r=t->radiators->items[i];if(r->host)continue;
        const tt_radiator *v=tt_model_radiator(t->model,r->name);if(!v)continue;
        size_t n=r->nz*r->nw;memcpy(r->od,v->optical_depth,n*sizeof(double));memcpy(r->ssa,v->single_scattering_albedo,n*sizeof(double));memcpy(r->g,v->asymmetry,n*sizeof(double));
    }
}
RadiationFieldUpdater *GetRadiationFieldUpdater(TUVX *t,Error *e){
    NoError(e);if(!t){tmt_error(e,"Null model");return NULL;}if(!t->host_radiation)return NULL;
    RadiationFieldUpdater *u=malloc(sizeof(*u));if(!u){tmt_error(e,"Out of memory");return NULL;}u->model=t;t->refs++;return u;
}
void DeleteRadiationFieldUpdater(RadiationFieldUpdater *u,Error *e){NoError(e);if(u){DeleteTuvx(u->model,NULL);free(u);}}
void UpdateRadiationField(RadiationFieldUpdater *u,double *direct,double *up,double *down,double *ed,double *eu,double *en,size_t nz,size_t nw,Error *e){
    NoError(e);if(!u){tmt_error(e,"Null radiation updater");return;}
    tt_model_dimensions s=tt_model_shape(u->model->model);
    if(nz!=s.levels || nw!=s.wavelengths){tmt_error(e,"Radiation dimensions differ");return;}
    tt_radiation r={direct,down,up,ed,en,eu};tmt_status(tt_model_set_radiation(u->model->model,&r),NULL,e);
}
void TuvxVersion(String *s){CreateString("TurboTUV 0.1.0 (TMT)",s);}
