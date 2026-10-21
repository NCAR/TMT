/* Independent direct Fable states check the adapter's public row-major views,
 * group boundaries, host edits, tolerance propagation and atomic failure. */
#define _POSIX_C_SOURCE 200809L
#include <musica/micm.h>
#include <tmt/prepare.h>
#include <tmt/mechanism.h>
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(Error *e) {
  if (e->code_) { fprintf(stderr, "%s\n", e->message_.value_); abort(); }
}
static void solve(MICM *m, State *s, double dt) {
  Error e = {0}; String status = {0}; SolverResultStats stats;
  MicmSolve(m, s, dt, &status, &stats, &e); check(&e);
  assert(!strcmp(status.value_, tmc_solver_state_name(TMC_CONVERGED)));
  assert(stats.final_time_ == dt); DeleteString(&status);
}
int main(int argc, char **argv) {
  assert(argc == 3); Error e = {0};
  assert(setenv("TMT_MICM_TIER1", "1", 1) == 0);
  TmtPrepareOptions opts = {0}; opts.cache_directory = argv[2]; opts.width = 2;
  opts.mode = "fast"; opts.ordering = "minimum-fill";
  String artifact = {0}; TmtPrepareMechanism(argv[1], &opts, &artifact, &e); check(&e);
  MICM *m = TmtCreateMicmPrepared(artifact.value_, RosenbrockStandardOrder, &e); check(&e);
  assert(TmtMicmUsesKernels(m));
  char path[8192]; snprintf(path, sizeof path, "%s/mechanism.so", artifact.value_);
  void *module = dlopen(path, RTLD_NOW | RTLD_LOCAL); assert(module);
  void *symbol = dlsym(module, "tmt_mechanism_v1"); assert(symbol);
  TmtMechanismEntry entry; memcpy(&entry, &symbol, sizeof entry);
  const TmtMechanismV1 *api = entry();
  tmc_system system = api->system(); tmc_reaction *reactions; int nr = api->reactions(&reactions);
  tmc_rosenbrock_parameters ros = tmc_rosenbrock_three_stage(); ros.constraint_init_tolerance = 1e-10;
  tmc_builder *builder = tmc_builder_create_rosenbrock(ros);
  tmc_builder_set_system(builder, &system); tmc_builder_set_reactions(builder, reactions, nr);
  tmc_builder_set_width(builder, 2); tmc_builder_set_kernels(builder, api->kernels());
  tmc_error ne = {0}; tmc_solver *direct = tmc_builder_build(builder, &ne); assert(direct);
  tmc_builder_free(builder); tmc_system_free(&system); tmc_reactions_free(reactions, nr);

  const size_t counts[] = {1, 31, 32, 33, 65};
  for (size_t test = 0; test < sizeof counts / sizeof *counts; ++test) {
    size_t nc = counts[test], ng = (nc+31)/32, ny, np, ncond, row, col;
    State *s = CreateMicmState(m, nc, &e); check(&e);
    assert(GetNumberOfGridCells(s, &e) == nc); check(&e);
    double *y = GetOrderedConcentrationsPointer(s, &ny, &e); check(&e); assert(ny == nc*2);
    double *p = GetOrderedRateParametersPointer(s, &np, &e); check(&e); assert(np == nc);
    Conditions *cond = GetConditionsPointer(s, &ncond, &e); check(&e); assert(ncond == nc);
    GetConcentrationsStrides(s, &e, &row, &col); check(&e); assert(row == 2 && col == 1);
    GetUserDefinedRateParametersStrides(s, &e, &row, &col); check(&e); assert(row == 1 && col == 1);
    tmc_state *refs[3];
    for (size_t g = 0; g < ng; ++g) {
      refs[g] = tmc_solver_create_state(direct, (int)(nc-g*32 < 32 ? nc-g*32 : 32));
      api->initial_state(refs[g]); tmc_state_set_relative_tolerance(refs[g], api->relative_tolerance);
    }
    for (size_t c = 0; c < nc; ++c) {
      cond[c] = (Conditions){270+c, 95000+c*10, 40+c*.01};
      y[c*2] = 1+c*.02; y[c*2+1] = .1+c*.003; p[c] = .02+c*.001;
    }
    for (int step = 0; step < 5; ++step) {
      /* Changes cross group boundaries and include a rate-only update. */
      if (step == 1) for (size_t c = 0; c < nc; ++c) p[c] *= 1.7;
      if (step == 2) for (size_t c = 0; c < nc; ++c) { y[c*2] *= 1.01; cond[c].temperature += 3; }
      if (step == 3) {
        RosenbrockSolverParametersC params; GetRosenbrockSolverParameters(m, &params, &e); check(&e);
        params.relative_tolerance = 2e-9; params.num_absolute_tolerances = 2;
        free(params.absolute_tolerances); params.absolute_tolerances = malloc(2*sizeof(double));
        params.absolute_tolerances[0] = 3e-15; params.absolute_tolerances[1] = 7e-15;
        SetRosenbrockSolverParameters(m, &params, &e); check(&e);
        FreeRosenbrockSolverParameters(&params);
      }
      RosenbrockSolverParametersC params; GetRosenbrockSolverParameters(m, &params, &e); check(&e);
      for (size_t g = 0; g < ng; ++g) {
        tmc_state *ref = refs[g];
        for (int c = 0; c < tmc_state_num_cells(ref); ++c) {
          size_t public = g*32+(size_t)c;
          tmc_state_set(ref, c, 0, y[public*2]); tmc_state_set(ref, c, 1, y[public*2+1]);
          tmc_dense_set(tmc_state_custom_rate_parameters(ref), c, 0, p[public]);
          tmc_state_conditions(ref)[c] = (tmc_conditions){cond[public].temperature, cond[public].pressure, cond[public].air_density};
        }
        tmc_state_set_relative_tolerance(ref, params.relative_tolerance);
        if (params.num_absolute_tolerances) tmc_state_set_absolute_tolerances(ref, params.absolute_tolerances, 2, NULL);
        if (step == 0 || step == 2 || step == 3) { tmc_state_invalidate_matrix(ref); tmc_state_set_next_step(ref, 0); }
        tmc_solver_update_state_parameters(direct, ref);
        tmc_result r = tmc_solver_solve(direct, .3, ref); assert(r.state == TMC_CONVERGED);
      }
      FreeRosenbrockSolverParameters(&params);
      solve(m, s, .3);
      assert(y == GetOrderedConcentrationsPointer(s, &ny, &e)); check(&e);
      for (size_t c = 0; c < nc; ++c) for (int v = 0; v < 2; ++v) {
        double expected = tmc_state_get(refs[c/32], (int)(c%32), v);
        assert(fabs(y[c*2+(size_t)v]-expected) <= 2e-14*fmax(fabs(expected),1e-20));
      }
    }
    /* Reject invalid host data before advancing any group, then recover. */
    double *before = malloc(ny*sizeof(double)); memcpy(before, y, ny*sizeof(double));
    double old_temperature = cond[nc-1].temperature; cond[nc-1].temperature = NAN;
    String status = {0}; SolverResultStats stats;
    MicmSolve(m, s, .3, &status, &stats, &e); assert(e.code_); DeleteError(&e);
    assert(!memcmp(before, y, ny*sizeof(double))); cond[nc-1].temperature = old_temperature;
    solve(m, s, .3);
    /* Make the first group slow enough to converge in one large step, while
     * a later group fails. Even completed groups must not publish progress. */
    if (nc > 32) for (size_t c = 0; c < 32; ++c) { cond[c].temperature = 7.; p[c] = 0.; }
    memcpy(before, y, ny*sizeof(double));
    RosenbrockSolverParametersC params; GetRosenbrockSolverParameters(m, &params, &e); check(&e);
    size_t limit = params.max_number_of_steps; double h_start = params.h_start;
    params.max_number_of_steps = 1; params.h_start = 1000;
    SetRosenbrockSolverParameters(m, &params, &e); check(&e);
    if (nc > 32) {
      State *probe = CreateMicmState(m, 32, &e); check(&e);
      size_t size;
      double *py = GetOrderedConcentrationsPointer(probe, &size, &e); check(&e);
      memcpy(py, y, 64*sizeof(double));
      memcpy(GetOrderedRateParametersPointer(probe, &size, &e), p, 32*sizeof(double)); check(&e);
      memcpy(GetConditionsPointer(probe, &size, &e), cond, 32*sizeof(Conditions)); check(&e);
      solve(m, probe, 1000);
      assert(memcmp(py, y, 64*sizeof(double)) != 0);
      DeleteState(probe, &e); check(&e);
    }
    MicmSolve(m, s, 1000, &status, &stats, &e); check(&e);
    assert(strcmp(status.value_, tmc_solver_state_name(TMC_CONVERGED)));
    assert(stats.final_time_ == 0 && !memcmp(before, y, ny*sizeof(double))); DeleteString(&status);
    params.max_number_of_steps = limit; params.h_start = h_start;
    SetRosenbrockSolverParameters(m, &params, &e); check(&e);
    FreeRosenbrockSolverParameters(&params);
    State *fresh = CreateMicmState(m, nc, &e); check(&e);
    size_t size; memcpy(GetOrderedConcentrationsPointer(fresh, &size, &e), y, ny*sizeof(double)); check(&e);
    memcpy(GetOrderedRateParametersPointer(fresh, &size, &e), p, np*sizeof(double)); check(&e);
    memcpy(GetConditionsPointer(fresh, &size, &e), cond, nc*sizeof(Conditions)); check(&e);
    solve(m, s, .3); solve(m, fresh, .3);
    assert(!memcmp(y, GetOrderedConcentrationsPointer(fresh, &size, &e), ny*sizeof(double))); check(&e);
    DeleteState(fresh, &e); check(&e); free(before);
    for (size_t g = 0; g < ng; ++g) tmc_state_free(refs[g]);
    DeleteState(s, &e); check(&e);
  }
  DeleteMicm(m, &e); check(&e); tmc_solver_free(direct); dlclose(module); DeleteString(&artifact);
  puts("Tier 1 direct Fable parity, partial groups, host edits, stable views, tolerance updates and failure recovery passed");
}
