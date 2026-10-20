#include <musica/micm.h>
#include <math.h>
#include <stdio.h>
int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr,"usage: chemistry CONFIG_OR_PREPARED_DIRECTORY\n"); return 2; }
  Error error={0}; MICM *solver=CreateMicm(argv[1],RosenbrockStandardOrder,&error);
  if (!solver) { fprintf(stderr,"%s\n",error.message_.value_); DeleteError(&error); return 1; }
  State *state=CreateMicmState(solver,1,&error);
  size_t size; Conditions *conditions=GetConditionsPointer(state,&size,&error);
  conditions[0]=(Conditions){300,100000,100000/(8.31446261815324*300)};
  Mappings species={0}; GetSpeciesOrdering(state,&species,&error);
  size_t a=FindMappingIndex(species,"A",&error);
  double *concentration=GetOrderedConcentrationsPointer(state,&size,&error); concentration[a]=1;
  String status={0}; SolverResultStats stats;
  MicmSolve(solver,state,1,&status,&stats,&error);
  int failed=error.code_ || fabs(concentration[a]-exp(-.2))>1e-6;
  printf("%s: A=%.12g, time=%.6g s\n",status.value_,concentration[a],stats.final_time_);
  DeleteString(&status); DeleteMappings(&species); DeleteState(state,NULL); DeleteMicm(solver,NULL); DeleteError(&error);
  return failed;
}
