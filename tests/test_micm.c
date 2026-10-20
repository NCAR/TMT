/* Compare public C calls with direct Fable and an analytical solution. */
#include <musica/micm.h>
#include <tmt/prepare.h>
#include <tmt/mechanism.h>
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(Error *e) { if(e->code_) { fprintf(stderr, "%s\n", e->message_.value_); abort(); } }
static size_t index_at(size_t cell, size_t var, size_t nv, size_t width) { return ((cell/width)*nv+var)*width+cell%width; }
int main(int argc, char **argv) {
  assert(argc == 3); Error e = {0};
  for (int order = 1; order <= 2; ++order) {
    MICMSolver type = order == 1 ? RosenbrockStandardOrder : Rosenbrock;
    TmtPrepareOptions opts = {0}; opts.cache_directory = argv[2]; opts.width = order;
    String artifact = {0}; TmtPrepareMechanism(argv[1], &opts, &artifact, &e); check(&e);
    MICM *m = TmtCreateMicmPrepared(artifact.value_, type, &e); check(&e); assert(TmtMicmUsesKernels(m));
    char lib[8192]; snprintf(lib, sizeof lib, "%s/mechanism.so", artifact.value_);
    void *module = dlopen(lib, RTLD_NOW | RTLD_LOCAL); assert(module);
    void *symbol = dlsym(module, "tmt_mechanism_v1"); TmtMechanismEntry entry; memcpy(&entry, &symbol, sizeof entry);
    const TmtMechanismV1 *api = entry();
    tmc_system system = api->system(); tmc_reaction *reactions; int nr = api->reactions(&reactions);
    tmc_builder *builder = tmc_builder_create_rosenbrock(tmc_rosenbrock_three_stage());
    tmc_builder_set_system(builder, &system); tmc_builder_set_reactions(builder, reactions, nr); tmc_builder_set_width(builder, order);
    tmc_error ne = {0}; tmc_solver *direct = tmc_builder_build(builder, &ne); assert(direct && !tmc_solver_uses_kernels(direct));
    tmc_builder_free(builder); tmc_system_free(&system); tmc_reactions_free(reactions, nr);
    const size_t cells[] = {1, 3, 33};
    for (size_t test = 0; test < 3; ++test) {
      size_t nc = cells[test], size = 0, np = 0;
      State *s = CreateMicmState(m, nc, &e); check(&e);
      tmc_state *ref = tmc_solver_create_state(direct, (int)nc);
      Conditions *cond = GetConditionsPointer(s, &size, &e); check(&e); assert(size == nc);
      double *y = GetOrderedConcentrationsPointer(s, &size, &e); check(&e);
      double *params = GetOrderedRateParametersPointer(s, &np, &e); check(&e);
      size_t nv = GetNumberOfSpecies(s, &e); check(&e);
      Mappings names = {0}; GetSpeciesOrdering(s, &names, &e); check(&e);
      for (size_t c = 0; c < nc; ++c) {
        cond[c] = (Conditions){280.0+c, 101325., 40.};
        tmc_state_conditions(ref)[c] = (tmc_conditions){cond[c].temperature, cond[c].pressure, cond[c].air_density};
        for (size_t v = 0; v < nv; ++v) {
          double value = nv == 2 ? (!strcmp(names.mappings_[v].name_.value_, "A") ? 1.0+c*0.01 : 0) : 1e-9*(1+v%10);
          y[index_at(c,v,nv,order)] = value; tmc_state_set(ref, (int)c, (int)v, value);
        }
      }
      for (size_t i = 0; i < np; ++i) params[i] = 1e-5;
      if (np) memcpy(tmc_state_custom_rate_parameters(ref)->data, params, np*sizeof(double));
      for (int step = 0; step < 3; ++step) {
        String status = {0}; SolverResultStats stats;
        MicmSolve(m, s, 0.1, &status, &stats, &e); check(&e);
        tmc_solver_update_state_parameters(direct, ref);
        tmc_result r = tmc_solver_solve(direct, 0.1, ref);
        assert(r.state == TMC_CONVERGED); assert(!strcmp(status.value_, tmc_solver_state_name(TMC_CONVERGED)));
        assert(stats.final_time_ == 0.1);
        size_t after; assert(y == GetOrderedConcentrationsPointer(s, &after, &e)); check(&e); assert(after == size);
        for (size_t c = 0; c < nc; ++c) for (size_t v = 0; v < nv; ++v) {
          double actual = y[index_at(c,v,nv,order)], expected = tmc_state_get(ref,(int)c,(int)v);
          assert(fabs(actual-expected) <= 1e-12*fmax(fabs(expected),1e-20));
          if (nv == 2 && !strcmp(names.mappings_[v].name_.value_, "A")) assert(fabs(actual-(1+c*.01)*exp(-.2*.1*(step+1))) < 1e-7);
        }
        DeleteString(&status);
      }
      DeleteMappings(&names); tmc_state_free(ref); DeleteState(s, &e); check(&e);
    }
    /* State owns a solver reference even after the caller releases it. */
    State *survivor = CreateMicmState(m, 1, &e); check(&e); DeleteMicm(m, &e); check(&e);
    assert(GetNumberOfSpecies(survivor, &e) > 0); check(&e); DeleteState(survivor, &e); check(&e);
    tmc_solver_free(direct); dlclose(module); DeleteString(&artifact);
  }
  puts("native preparation, generated/generic parity, partial groups, stable views, and analytical decay passed");
}
