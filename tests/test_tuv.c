/* Consumer-level comparison against the unchanged native backend. */
#include <musica/tuvx.h>
#include <turbotuv_model.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define REQUIRE(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,e.message_.value_?e.message_.value_:"");exit(1);}}while(0)
static void compare(const char *path){
    Error e={0};tt_config *config=NULL;tt_model *native=NULL;tt_error te={0};
    REQUIRE(tt_config_read_file(path,&config,&te)==TT_OK);
    REQUIRE(tt_model_create(tt_config_root(config),NULL,&native,&te)==TT_OK);
    TUVX *model=CreateTuvx(path,NULL,NULL,NULL,&e);REQUIRE(model && !e.code_);
    tt_model_dimensions shape=tt_model_shape(native);
    size_t z,w,p,h,d;TmtTuvxDimensions(model,&z,&w,&p,&h,&d,&e);
    REQUIRE(!e.code_ && z==shape.levels && w==shape.wavelengths && p==shape.photolysis && h==shape.heating && d==shape.dose);
    double *photo=calloc(z*p+1,sizeof(double)),*heat=calloc(z*h+1,sizeof(double)),*dose=calloc(z*d+1,sizeof(double));
    double *flux=calloc(3*z*w,sizeof(double)),*irr=calloc(3*z*w,sizeof(double));
    REQUIRE(photo && heat && dose && flux && irr);
    Mappings names={0};GetPhotolysisRateConstantsOrdering(model,&names,&e);REQUIRE(!e.code_ && names.size_==p);
    for(size_t i=0;i<p;++i)REQUIRE(!strcmp(names.mappings_[i].name_.value_,tt_model_rate_name(native,TT_RATE_PHOTOLYSIS,i)));
    /* These angles round trip exactly through radians in double precision,
     * allowing a bitwise comparison at identical native input values. */
    const double angles[]={0,45,85,90,110};
    for(size_t k=0;k<sizeof(angles)/sizeof(*angles);++k){
        const tt_model_result *r=NULL;
        REQUIRE(tt_model_run(native,angles[k],1.03,&r,&te)==TT_OK);
        /* Public MUSICA-compatible calls take radians; native TurboTUV takes
         * degrees. Include oblique sun and night to catch unit regressions. */
        RunTuvx(model,angles[k]*acos(-1.0)/180.0,1.03,photo,heat,dose,flux,irr,&e);REQUIRE(!e.code_);
        REQUIRE(!memcmp(photo,r->photolysis,z*p*sizeof(double)));
        REQUIRE(!memcmp(heat,r->heating,z*h*sizeof(double)));
        REQUIRE(!memcmp(dose,r->dose,z*d*sizeof(double)));
        for(size_t i=0;i<z*w;++i){
            REQUIRE(flux[3*i]==r->radiation.direct_flux[i] && flux[3*i+1]==r->radiation.up_flux[i] && flux[3*i+2]==r->radiation.down_flux[i]);
            REQUIRE(irr[3*i]==r->radiation.direct_irradiance[i] && irr[3*i+1]==r->radiation.up_irradiance[i] && irr[3*i+2]==r->radiation.down_irradiance[i]);
        }
    }
    GridMap *grids=GetGridMap(model,&e);REQUIRE(grids && !e.code_);
    Grid *height=GetGrid(grids,"height","km",&e);REQUIRE(height && !e.code_);
    DeleteTuvx(model,&e);DeleteGridMap(grids,&e);
    REQUIRE(GetGridNumberOfSections(height,&e)==z-1);DeleteGrid(height,&e);
    DeleteMappings(&names);DeleteError(&e);tt_model_destroy(native);tt_config_destroy(config);
    free(photo);free(heat);free(dose);free(flux);free(irr);
}
int main(int argc,char **argv){
    Error e={0};REQUIRE(argc>1);
    for(int i=1;i<argc;++i)compare(argv[i]);
    REQUIRE(!CreateTuvx("/nonexistent/tmt.json",NULL,NULL,NULL,&e) && e.code_);
    Grid *g=CreateGrid("height","km",2,&e);REQUIRE(g && !e.code_);
    Profile *p=CreateProfile("air","molecule cm-3",g,&e);REQUIRE(p && !e.code_);
    double layers[]={2,7},edges[]={20,30,50};
    SetProfileLayerDensities(p,layers,2,&e);REQUIRE(!e.code_);
    SetProfileEdgeValues(p,edges,3,&e);REQUIRE(!e.code_);
    SetProfileExoLayerDensity(p,1,&e);REQUIRE(!e.code_);
    GetProfileLayerDensities(p,layers,2,&e);REQUIRE(layers[1]==8);
    CalculateProfileExoLayerDensity(p,1000,&e);REQUIRE(!e.code_);
    GetProfileLayerDensities(p,layers,2,&e);REQUIRE(layers[1]==50008);
    REQUIRE(GetProfileExoLayerDensity(p,&e)==50000);
    GridMap *m=CreateGridMap(&e);AddGrid(m,g,&e);DeleteGrid(g,&e);
    g=GetGrid(m,"height","km",&e);DeleteGridMap(m,&e);REQUIRE(GetGridNumberOfSections(g,&e)==2);
    DeleteProfile(p,&e);DeleteGrid(g,&e);DeleteError(&e);
    puts("TUV C compatibility: numerical, layout, mapping, ownership, and error checks passed");return 0;
}
