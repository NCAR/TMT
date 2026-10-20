/* Public-adapter and direct-Fable comparison under identical scenario inputs.
 * The CSV reader is unchanged source from the pinned Fable benchmark. */
#define _POSIX_C_SOURCE 200809L
#include <musica/micm.h>
#include <tmt/prepare.h>
#include <tmt/fable.h>
#include <tmt/mechanism.h>
#include <dlfcn.h>
#include <math.h>
#include <time.h>
#include "csv_table.h"
static void check(Error *e) { if(e->code_) fail(e->message_.value_); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9; }
static size_t at(int c,int v,int nv) { return ((size_t)(c/2)*nv+v)*2+c%2; }
static void *(*volatile copy_bytes)(void *, const void *, size_t) = memcpy;
int main(int argc,char **argv) {
  if(argc!=7) fail("usage: benchmark_micm CONFIG CACHE CELLS_CSV PHOTO_CSV DT STEPS");
  Error e={0}; TmtPrepareOptions opt={0};opt.cache_directory=argv[2];opt.width=2;
  double start=now();String artifact={0};TmtPrepareMechanism(argv[1],&opt,&artifact,&e);check(&e);
  double preparation=now()-start;
  start=now();String hit={0};TmtPrepareMechanism(argv[1],&opt,&hit,&e);check(&e);double cache_hit=now()-start;DeleteString(&hit);
  char path[8192];snprintf(path,sizeof path,"%s/mechanism.so",artifact.value_);
  void *module=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!module)fail(dlerror());
  void *symbol=dlsym(module,"tmt_mechanism_v1");TmtMechanismEntry entry;memcpy(&entry,&symbol,sizeof entry);const TmtMechanismV1 *api=entry();
  table cells=read_csv(argv[3]),photo=read_csv(argv[4]);double dt=atof(argv[5]);int steps=atoi(argv[6]);
  printf("{\"preparation_s\":%.9g,\"cache_hit_s\":%.9g,\"cells\":%d,\"steps\":%d,\"dt_s\":%.9g,\"profiles\":[\n",preparation,cache_hit,cells.n_rows,steps,dt);
  for(int profile=1;profile<=3;++profile) {
    start=now();MICM *m=TmtCreateMicmPrepared(artifact.value_,Rosenbrock,&e);check(&e);
    TmtConfigureFable(m,(TmtFableProfile)profile,&e);check(&e);double startup=now()-start;
    tmc_rosenbrock_parameters ros=profile==1?tmc_rosenbrock_four_stage_dae():tmc_rosenbrock_three_stage();
    ros.loop=profile==3?TMC_LOOP_EXTRAPOLATION:TMC_LOOP_ROW;ros.warm_start=1;ros.quantize=1;
    ros.controller_type=profile==2?TMC_CONTROLLER_H211B:TMC_CONTROLLER_MICM;
    ros.extrapolation_order=4;ros.extrapolation_sequence=TMC_SEQUENCE_SHIFTED;
    tmc_system system=api->system();tmc_reaction *reactions;int nr=api->reactions(&reactions);
    tmc_builder *b=tmc_builder_create_rosenbrock(ros);tmc_builder_set_system(b,&system);tmc_builder_set_reactions(b,reactions,nr);
    tmc_builder_set_width(b,2);tmc_builder_set_kernels(b,api->kernels());tmc_error ne={0};tmc_solver *direct=tmc_builder_build(b,&ne);
    if(!direct || !tmc_solver_uses_kernels(direct))fail("direct solver creation failed");
    tmc_builder_free(b);tmc_system_free(&system);tmc_reactions_free(reactions,nr);
    int nv=tmc_solver_num_variables(direct),np=tmc_solver_num_custom_parameters(direct);
    double *atol=malloc((size_t)nv*sizeof *atol);for(int v=0;v<nv;++v)atol[v]=profile==3?1e-22:1e-20;
    double adapted=0,native=0,copies=0,max_relative=0;size_t compared=0;
    for(int first=0;first<cells.n_rows;first+=32) {
      int nc=cells.n_rows-first;if(nc>32)nc=32;
      State *s=CreateMicmState(m,(size_t)nc,&e);check(&e);tmc_state *ref=tmc_solver_create_state(direct,nc);api->initial_state(ref);
      tmc_state_set_relative_tolerance(ref,profile==1?5e-3:profile==2?8e-3:5e-4);tmc_state_set_absolute_tolerances(ref,atol,nv,&ne);
      size_t ys,ps,cs;double *y=GetOrderedConcentrationsPointer(s,&ys,&e),*p=GetOrderedRateParametersPointer(s,&ps,&e);Conditions *conditions=GetConditionsPointer(s,&cs,&e);check(&e);
      for(int c=0;c<nc;++c) {
        for(int k=0;k<cells.n_columns;++k) {
          const char *name=cells.names[k];double val=cells.values[(size_t)(first+c)*cells.n_columns+k];
          int v=tmc_state_variable_index(ref,name),r=tmc_state_custom_parameter_index(ref,name);
          char qualified[160];if(r<0){snprintf(qualified,sizeof qualified,"PHOTO.%s",name);r=tmc_state_custom_parameter_index(ref,qualified);}
          if(r<0){snprintf(qualified,sizeof qualified,"USER.%s",name);r=tmc_state_custom_parameter_index(ref,qualified);}
          if(r<0){snprintf(qualified,sizeof qualified,"SURF.%s",name);r=tmc_state_custom_parameter_index(ref,qualified);}
          if(!strcmp(name,"temperature"))conditions[c].temperature=val;
          else if(!strcmp(name,"pressure"))conditions[c].pressure=val;
          else if(v>=0)y[at(c,v,nv)]=val;
          else if(r>=0)p[at(c,r,np)]=val;
          else fail(name);
        }
        conditions[c].air_density=conditions[c].pressure/(8.31446261815324*conditions[c].temperature);
        tmc_state_conditions(ref)[c]=(tmc_conditions){conditions[c].temperature,conditions[c].pressure,conditions[c].air_density};
        for(int v=0;v<nv;++v)tmc_state_set(ref,c,v,y[at(c,v,nv)]);
      }
      memcpy(tmc_state_custom_rate_parameters(ref)->data,p,ps*sizeof *p);
      double *base=malloc(ps*sizeof *base);memcpy(base,p,ps*sizeof *base);
      tmc_solver_update_state_parameters(direct,ref);
      for(int step=0;step<steps;++step) {
        double t=step*dt,factor=photo.values[1];
        int row=0;while(row+1<photo.n_rows && photo.values[(size_t)(row+1)*photo.n_columns]<=t)++row;
        factor=photo.values[(size_t)row*photo.n_columns+1];
        if(row+1<photo.n_rows) { double t0=photo.values[(size_t)row*photo.n_columns],t1=photo.values[(size_t)(row+1)*photo.n_columns];
          factor+=(photo.values[(size_t)(row+1)*photo.n_columns+1]-factor)*(t-t0)/(t1-t0); }
        for(int r=0;r<np;++r)if(!strncmp(tmc_solver_custom_parameter_label(direct,r),"PHOTO.j",7))for(int c=0;c<nc;++c) {
          p[at(c,r,np)]=base[at(c,r,np)]*factor;tmc_dense_set(tmc_state_custom_rate_parameters(ref),c,r,p[at(c,r,np)]);
        }
        tmc_solver_update_custom_rate_constants(direct,ref);
        start=now();tmc_result rr=tmc_solver_solve(direct,dt,ref);native+=now()-start;
        if(rr.state!=TMC_CONVERGED)fail(tmc_solver_state_name(rr.state));
        String state={0};SolverResultStats stats;start=now();MicmSolve(m,s,dt,&state,&stats,&e);adapted+=now()-start;check(&e);
        if(strcmp(state.value_,"Converged"))fail(state.value_);DeleteString(&state);
        for(int c=0;c<nc;++c)for(int v=0;v<nv;++v) {
          double expected=tmc_state_get(ref,c,v),actual=y[at(c,v,nv)],rel=fabs(expected-actual)/fmax(1e-14,fabs(expected));
          if(!isfinite(actual)||actual<0||rel>1e-10)fail("adapter/direct numerical mismatch");if(rel>max_relative)max_relative=rel;++compared;
        }
      }
      /* Isolated bandwidth cost for the two public concentration copies. */
      double *scratch=malloc(ys*sizeof *scratch);start=now();
      for(int i=0;i<100;++i){copy_bytes(scratch,y,ys*sizeof *y);copy_bytes(y,scratch,ys*sizeof *y);}
      copies+=(now()-start)*steps/100.;free(scratch);free(base);DeleteState(s,NULL);tmc_state_free(ref);
    }
    printf("%s{\"profile\":%d,\"prepared_startup_s\":%.9g,\"tmt_s\":%.9g,\"direct_s\":%.9g,\"estimated_concentration_copy_s\":%.9g,\"compared_values\":%zu,\"maximum_relative_difference\":%.9g}",profile==1?"":",\n",profile,startup,adapted,native,copies,compared,max_relative);fflush(stdout);
    free(atol);DeleteMicm(m,NULL);tmc_solver_free(direct);
  }
  puts("\n]}");free(cells.names);free(cells.values);free(photo.names);free(photo.values);dlclose(module);DeleteString(&artifact);
}
