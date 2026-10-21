/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#define _DARWIN_C_SOURCE
#include "internal.h"
#include "../prepare/loader.h"
#include <musica/micm.h>
#include <tmt/prepare.h>
#include <tmt/fable.h>
#include <stdatomic.h>
#include <limits.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

struct MICM {
  atomic_size_t references;
  TmtModule module;
  tmc_solver *solver;
  MICMSolver type;
  double rtol, *atol;
  size_t n_atol, generation;
  tmc_backward_euler_parameters be;
  int tier1;
};
struct State {
  MICM *owner;
  tmc_state *native;
  double *concentrations, *parameters, *default_atol;
  Conditions *conditions;
  size_t generation;
  int parameters_initialized;
  size_t cells, n_groups, solve_calls;
  tmc_state **groups;
  FILE *trace;
};
static int tier1_requested(MICMSolver type) {
  const char *value = getenv("TMT_MICM_TIER1");
  return type == RosenbrockStandardOrder && value && !strcmp(value, "1");
}
static int native_error(int rc, tmc_error *err, Error *e) {
  if (rc == TMC_OK) return 1;
  ToError(err->category == TMC_ERROR_SPECIES ? "MICM Species" : "Fable Error", err->code, err->message, 2, e); return 0;
}
static int valid_type(MICMSolver t) { return t >= Rosenbrock && t <= RosenbrockDAE6StandardOrder && t != CudaRosenbrock; }
static int backward(MICMSolver t) { return t == BackwardEuler || t == BackwardEulerStandardOrder; }
size_t GetVectorSize(MICMSolver t) {
  if (!valid_type(t)) return 0;
  return (t == RosenbrockStandardOrder || t == BackwardEulerStandardOrder ||
          t == RosenbrockDAE4StandardOrder || t == RosenbrockDAE6StandardOrder) ? 1 : 2;
}
bool _IsCudaAvailable(Error *e) { NoError(e); return false; }
size_t GetMaximumNumberOfGridCells(MICM *m) { return m ? (GetVectorSize(m->type) == 1 ? INT_MAX - 8 : GetVectorSize(m->type)) : 0; }
bool TmtMicmUsesKernels(const MICM *m) { return m && tmc_solver_uses_kernels(m->solver); }
void MicmVersion(String *s) { CreateString(tmc_version(), s); }
void DeleteMicm(MICM *m, Error *e) {
  NoError(e);
  if (!m || atomic_fetch_sub(&m->references, 1) != 1) return;
  tmc_solver_free(m->solver); tmt_unload_module(&m->module); free(m->atol); free(m);
}
MICM *TmtCreateMicmPrepared(const char *path, MICMSolver type, Error *e) {
  NoError(e);
  if (!valid_type(type)) { tmt_error(e, "unsupported solver type"); return NULL; }
  MICM *m = calloc(1, sizeof *m);
  if (!m) { tmt_error(e, "allocation failed"); return NULL; }
  atomic_init(&m->references, 1); m->type = type;
  m->tier1 = tier1_requested(type);
  if (!tmt_load_module(path, &m->module, e)) { DeleteMicm(m, NULL); return NULL; }
  const TmtMechanismV1 *api = m->module.api;
  if (api->width != (m->tier1 ? 2 : (int)GetVectorSize(type))) { tmt_error(e, "prepared mechanism width does not match solver configuration"); DeleteMicm(m, NULL); return NULL; }
  m->rtol = api->relative_tolerance;
  tmc_rosenbrock_parameters ros = tmc_rosenbrock_three_stage();
  if (type == RosenbrockDAE4 || type == RosenbrockDAE4StandardOrder) ros = tmc_rosenbrock_four_stage_dae();
  if (type == RosenbrockDAE6 || type == RosenbrockDAE6StandardOrder) ros = tmc_rosenbrock_six_stage_dae();
  ros.constraint_init_tolerance = 1e-10;
  m->be = tmc_backward_euler_defaults();
  tmc_builder *builder = backward(type) ? tmc_builder_create_backward_euler(m->be) : tmc_builder_create_rosenbrock(ros);
  tmc_system system = api->system();
  tmc_reaction *reactions = NULL; int nr = api->reactions(&reactions);
  tmc_builder_set_system(builder, &system);
  tmc_builder_set_reactions(builder, reactions, nr);
  tmc_builder_set_width(builder, api->width);
  tmc_builder_set_kernels(builder, api->kernels());
  tmc_error err = {0};
  m->solver = tmc_builder_build(builder, &err);
  tmc_builder_free(builder); tmc_reactions_free(reactions, nr); tmc_system_free(&system);
  if (!m->solver) { tmt_error(e, err.message); DeleteMicm(m, NULL); return NULL; }
  if (!tmc_solver_uses_kernels(m->solver)) { tmt_error(e, "Fable rejected generated kernels (width or fingerprint mismatch)"); DeleteMicm(m, NULL); return NULL; }
  return m;
}
MICM *CreateMicm(const char *path, MICMSolver type, Error *e) {
  NoError(e);
  if (!path || !valid_type(type)) { tmt_error(e, "configuration path and supported solver type are required"); return NULL; }
  char manifest[PATH_MAX];
  int n = snprintf(manifest, sizeof manifest, "%s/manifest.json", path);
  struct stat st;
  if (n > 0 && n < (int)sizeof manifest && !stat(manifest, &st)) return TmtCreateMicmPrepared(path, type, e);
  if (stat(path, &st)) { ToError(MUSICA_PARSE_ERROR_CATEGORY, MUSICA_PARSE_ERROR_CODE_INVALID_CONFIG_FILE, "configuration path does not exist", 2, e); return NULL; }
  TmtPrepareOptions options = {0}; options.width = (int)GetVectorSize(type);
  if (tier1_requested(type)) { options.width = 2; options.mode = "fast"; options.ordering = "minimum-fill"; }
  String artifact = {0}; TmtPrepareMechanism(path, &options, &artifact, e);
  if (!artifact.value_) return NULL;
  MICM *m = TmtCreateMicmPrepared(artifact.value_, type, e);
  DeleteString(&artifact); return m;
}
MICM *CreateMicmFromConfigString(const char *text, MICMSolver type, Error *e) {
  NoError(e);
  if (!text || !valid_type(type)) { tmt_error(e, "configuration string and supported solver type are required"); return NULL; }
  const char *tmp = getenv("TMPDIR"); if (!tmp) tmp = "/tmp";
  char file[PATH_MAX]; int n = snprintf(file, sizeof file, "%s/tmt-input-XXXXXX", tmp);
  if (n < 0 || n >= (int)sizeof file) { tmt_error(e, "temporary path too long"); return NULL; }
  int fd = mkstemp(file);
  if (fd < 0) { tmt_error(e, "cannot create temporary configuration"); return NULL; }
  FILE *stream = fdopen(fd, "w");
  if (!stream) { close(fd); unlink(file); tmt_error(e, "cannot open temporary configuration"); return NULL; }
  int rc = fputs(text, stream); int closed = fclose(stream);
  if (rc < 0 || closed) { unlink(file); tmt_error(e, "cannot write temporary configuration"); return NULL; }
  MICM *m = CreateMicm(file, type, e); unlink(file); return m;
}
MICM *CreateMicmFromChemistryMechanism(const Chemistry *c, MICMSolver t, Error *e) {
  return CreateMicmFromConfigString(c ? c->configuration : NULL, t, e);
}
static const tmc_species *species(MICM *m, const char *name, Error *e) {
  if (!m || !name) { tmt_error(e, "solver and species name required"); return NULL; }
  const tmc_phase_species *s = tmc_phase_find(&tmc_solver_system(m->solver)->gas_phase, name);
  if (!s) { tmt_error(e, "unknown species"); return NULL; }
  return &s->species;
}
#define PROPERTY(Type, suffix, getter) \
Type GetSpeciesProperty##suffix(MICM *m, const char *name, const char *key, Error *e) { \
  NoError(e); Type value = 0; const tmc_species *s = species(m, name, e); \
  if (!key) { tmt_error(e, "property name required"); return value; } \
  if (s) { tmc_error err = {0}; native_error(getter(s, key, &value, &err), &err, e); } return value; \
}
PROPERTY(double, Double, tmc_species_get_double)
PROPERTY(int, Int, tmc_species_get_int)
PROPERTY(bool, Bool, tmc_species_get_bool)
void GetSpeciesPropertyString(MICM *m, const char *name, const char *key, String *out, Error *e) {
  NoError(e); const tmc_species *s = species(m, name, e);
  if (!key || !out) { tmt_error(e, "property name and output required"); return; }
  if (s) { const char *value = NULL; tmc_error err = {0};
    if (native_error(tmc_species_get_string(s, key, &value, &err), &err, e)) CreateString(value, out); }
}
static int tolerances(MICM *m, double rtol, const double *atol, size_t n, Error *e) {
  if (!isfinite(rtol) || rtol <= 0 || (n && (!atol || n != (size_t)tmc_solver_num_variables(m->solver)))) {
    tmt_error(e, "positive relative tolerance and one absolute tolerance per species required"); return 0;
  }
  for (size_t i = 0; i < n; ++i) if (!isfinite(atol[i]) || atol[i] <= 0) { tmt_error(e, "absolute tolerances must be positive and finite"); return 0; }
  double *copy = n ? malloc(n * sizeof *copy) : NULL;
  if (n && !copy) { tmt_error(e, "allocation failed"); return 0; }
  if (n) memcpy(copy, atol, n * sizeof *copy);
  free(m->atol); m->atol = copy; m->n_atol = n; m->rtol = rtol; ++m->generation; return 1;
}
void SetRosenbrockSolverParameters(MICM *m, const RosenbrockSolverParametersC *p, Error *e) {
  NoError(e);
  if (!m || !p || backward(m->type)) { tmt_error(e, "Rosenbrock parameters require a Rosenbrock solver"); return; }
  if (!p->max_number_of_steps || p->max_number_of_steps > INT_MAX || p->constraint_init_max_iterations > INT_MAX ||
      !p->constraint_init_max_iterations || !isfinite(p->constraint_init_tolerance) || p->constraint_init_tolerance <= 0 ||
      !isfinite(p->h_min) || !isfinite(p->h_max) || !isfinite(p->h_start) || p->h_min < 0 || p->h_max < 0 || p->h_start < 0 ||
      (p->h_max > 0 && p->h_max < p->h_min)) { tmt_error(e, "invalid Rosenbrock solver parameters"); return; }
  if (!tolerances(m, p->relative_tolerance, p->absolute_tolerances, p->num_absolute_tolerances, e)) return;
  tmc_rosenbrock_parameters ros = *tmc_solver_rosenbrock_parameters(m->solver);
  ros.h_min = p->h_min; ros.h_max = p->h_max; ros.h_start = p->h_start;
  ros.max_number_of_steps = (int)p->max_number_of_steps;
  ros.constraint_init_max_iterations = (int)p->constraint_init_max_iterations;
  ros.constraint_init_tolerance = p->constraint_init_tolerance;
  tmc_solver_set_rosenbrock_parameters(m->solver, &ros);
}
void SetBackwardEulerSolverParameters(MICM *m, const BackwardEulerSolverParametersC *p, Error *e) {
  NoError(e);
  if (!m || !p || !backward(m->type)) { tmt_error(e, "Backward Euler parameters require a Backward Euler solver"); return; }
  if (!p->max_number_of_steps || p->max_number_of_steps > INT_MAX || p->num_time_step_reductions != 5 || !p->time_step_reductions) {
    tmt_error(e, "Backward Euler requires positive step limit and five time-step reductions"); return;
  }
  for (size_t i = 0; i < 5; ++i) if (!isfinite(p->time_step_reductions[i]) || p->time_step_reductions[i] <= 0 || p->time_step_reductions[i] >= 1) {
    tmt_error(e, "time-step reductions must be between zero and one"); return;
  }
  if (!tolerances(m, p->relative_tolerance, p->absolute_tolerances, p->num_absolute_tolerances, e)) return;
  m->be.max_number_of_steps = (int)p->max_number_of_steps; m->be.time_step_reductions_count = 5;
  memcpy(m->be.time_step_reductions, p->time_step_reductions, 5 * sizeof(double));
  tmc_solver_set_backward_euler_parameters(m->solver, &m->be);
}
static double *copy_values(const double *values, size_t n, Error *e) {
  if (!n) return NULL;
  double *copy = malloc(n * sizeof *copy);
  if (!copy) { tmt_error(e, "allocation failed"); return NULL; }
  memcpy(copy, values, n * sizeof *copy); return copy;
}
void GetRosenbrockSolverParameters(MICM *m, RosenbrockSolverParametersC *p, Error *e) {
  NoError(e);
  if (!m || !p || backward(m->type)) { tmt_error(e, "Rosenbrock parameters require a Rosenbrock solver"); return; }
  const tmc_rosenbrock_parameters *ros = tmc_solver_rosenbrock_parameters(m->solver);
  *p = (RosenbrockSolverParametersC){m->rtol, copy_values(m->atol, m->n_atol, e), m->n_atol,
    ros->h_min, ros->h_max, ros->h_start, (size_t)ros->max_number_of_steps,
    (size_t)ros->constraint_init_max_iterations, ros->constraint_init_tolerance};
}
void GetBackwardEulerSolverParameters(MICM *m, BackwardEulerSolverParametersC *p, Error *e) {
  NoError(e);
  if (!m || !p || !backward(m->type)) { tmt_error(e, "Backward Euler parameters require a Backward Euler solver"); return; }
  *p = (BackwardEulerSolverParametersC){m->rtol, copy_values(m->atol, m->n_atol, e), m->n_atol,
    (size_t)m->be.max_number_of_steps, copy_values(m->be.time_step_reductions, (size_t)m->be.time_step_reductions_count, e), (size_t)m->be.time_step_reductions_count};
}
void FreeRosenbrockSolverParameters(RosenbrockSolverParametersC *p) { if (p) { free(p->absolute_tolerances); memset(p, 0, sizeof *p); } }
void FreeBackwardEulerSolverParameters(BackwardEulerSolverParametersC *p) {
  if (p) { free(p->absolute_tolerances); free(p->time_step_reductions); memset(p, 0, sizeof *p); }
}
void DeleteState(State *s, Error *e) {
  NoError(e); if (!s) return;
  for (size_t g = 0; g < s->n_groups; ++g) tmc_state_free(s->groups[g]);
  free(s->groups);
  if (s->trace) fclose(s->trace);
  tmc_state_free(s->native); DeleteMicm(s->owner, NULL);
  free(s->concentrations); free(s->parameters); free(s->default_atol); free(s->conditions); free(s);
}
State *CreateMicmState(MICM *m, size_t n, Error *e) {
  NoError(e);
  if (!m || !n || n > INT_MAX - 8) { tmt_error(e, "invalid number of grid cells"); return NULL; }
  State *s = calloc(1, sizeof *s);
  if (!s) { tmt_error(e, "allocation failed"); return NULL; }
  s->owner = m; atomic_fetch_add(&m->references, 1);
  s->cells = n;
  s->native = tmc_solver_create_state(m->solver, m->tier1 ? 1 : (int)n);
  if (!s->native) { DeleteState(s, NULL); tmt_error(e, "state allocation failed"); return NULL; }
  m->module.api->initial_state(s->native);
  s->default_atol = copy_values(tmc_state_absolute_tolerances(s->native),
                               (size_t)tmc_state_num_variables(s->native), e);
  if (!s->default_atol) { DeleteState(s, NULL); return NULL; }
  tmc_state_set_relative_tolerance(s->native, m->rtol);
  if (m->n_atol) tmc_state_set_absolute_tolerances(s->native, m->atol, (int)m->n_atol, NULL);
  tmc_dense *y = tmc_state_variables(s->native), *p = tmc_state_custom_rate_parameters(s->native);
  size_t ny = m->tier1 ? n * (size_t)y->n_cols : y->size;
  size_t np = m->tier1 ? n * (size_t)p->n_cols : p->size;
  s->concentrations = m->tier1 ? calloc(ny, sizeof(double)) : copy_values(y->data, ny, e);
  s->parameters = m->tier1 ? calloc(np, sizeof(double)) : copy_values(p->data, np, e);
  s->conditions = malloc(n * sizeof *s->conditions);
  if ((ny && !s->concentrations) || (np && !s->parameters) || !s->conditions) { DeleteState(s, NULL); tmt_error(e, "allocation failed"); return NULL; }
  _Static_assert(sizeof(Conditions) == sizeof(tmc_conditions), "conditions ABI");
  if (!m->tier1) memcpy(s->conditions, tmc_state_conditions(s->native), n * sizeof *s->conditions);
  else {
    size_t count = (n + 31) / 32;
    s->groups = calloc(count, sizeof *s->groups);
    if (!s->groups) { DeleteState(s, NULL); tmt_error(e, "group allocation failed"); return NULL; }
    s->n_groups = count;
    for (size_t g = 0; g < count; ++g) {
      size_t first = g * 32, cells = n - first < 32 ? n - first : 32;
      tmc_state *group = tmc_solver_create_state(m->solver, (int)cells);
      s->groups[g] = group;
      if (!group) { DeleteState(s, NULL); tmt_error(e, "group state allocation failed"); return NULL; }
      m->module.api->initial_state(group);
      tmc_state_set_relative_tolerance(group, m->rtol);
      tmc_state_set_absolute_tolerances(group, m->n_atol ? m->atol : s->default_atol, y->n_cols, NULL);
      tmc_dense *gy = tmc_state_variables(group), *gp = tmc_state_custom_rate_parameters(group);
      memcpy(s->conditions + first, tmc_state_conditions(group), cells * sizeof *s->conditions);
      for (size_t c = 0; c < cells; ++c) {
        for (int v = 0; v < y->n_cols; ++v) s->concentrations[(first+c)*y->n_cols+v] = tmc_dense_get(gy, (int)c, v);
        for (int v = 0; v < p->n_cols; ++v) s->parameters[(first+c)*p->n_cols+v] = tmc_dense_get(gp, (int)c, v);
      }
    }
  }
  s->generation = m->generation;
  return s;
}
Conditions *GetConditionsPointer(State *s, size_t *n, Error *e) {
  NoError(e); if (!s || !n) { tmt_error(e, "state and size output required"); return NULL; }
  *n = s->cells; return s->conditions;
}
double *GetOrderedConcentrationsPointer(State *s, size_t *n, Error *e) {
  NoError(e); if (!s || !n) { tmt_error(e, "state and size output required"); return NULL; }
  *n = s->owner->tier1 ? s->cells * (size_t)tmc_state_num_variables(s->native) : tmc_state_variables(s->native)->size;
  return s->concentrations;
}
double *GetOrderedRateParametersPointer(State *s, size_t *n, Error *e) {
  NoError(e); if (!s || !n) { tmt_error(e, "state and size output required"); return NULL; }
  *n = s->owner->tier1 ? s->cells * (size_t)tmc_state_custom_rate_parameters(s->native)->n_cols : tmc_state_custom_rate_parameters(s->native)->size;
  return s->parameters;
}
static void ordering(State *s, Mappings *out, int params, Error *e) {
  NoError(e); if (!s || !out) { tmt_error(e, "state and mappings output required"); return; }
  const tmc_name_map *map = params ? tmc_solver_parameter_map(s->owner->solver) : tmc_solver_variable_map(s->owner->solver);
  CreateMappings((size_t)tmc_name_map_size(map), out);
  if (out->size_ && !out->mappings_) { tmt_error(e, "allocation failed"); return; }
  for (size_t i = 0; i < out->size_; ++i) { CreateString(tmc_name_map_name(map, (int)i), &out->mappings_[i].name_); out->mappings_[i].index_ = i; }
}
void GetSpeciesOrdering(State *s, Mappings *out, Error *e) { ordering(s, out, 0, e); }
void GetUserDefinedRateParametersOrdering(State *s, Mappings *out, Error *e) { ordering(s, out, 1, e); }
#define COUNT(Name, expression) size_t Name(State *s, Error *e) { NoError(e); if (!s) { tmt_error(e, "state required"); return 0; } return (size_t)(expression); }
COUNT(GetNumberOfGridCells, s->cells)
COUNT(GetNumberOfSpecies, tmc_state_num_variables(s->native))
COUNT(GetNumberOfUserDefinedRateParameters, tmc_solver_num_custom_parameters(s->owner->solver))
static void strides(State *s, Error *e, size_t *row, size_t *col, int params) {
  NoError(e); if (!s || !row || !col) { tmt_error(e, "state and stride outputs required"); return; }
  tmc_dense *m = params ? tmc_state_custom_rate_parameters(s->native) : tmc_state_variables(s->native);
  if (s->owner->tier1) { *row = (size_t)m->n_cols; *col = 1; return; }
  *row = tmc_dense_row_stride(m); *col = tmc_dense_column_stride(m);
}
void GetConcentrationsStrides(State *s, Error *e, size_t *row, size_t *col) { strides(s, e, row, col, 0); }
void GetUserDefinedRateParametersStrides(State *s, Error *e, size_t *row, size_t *col) { strides(s, e, row, col, 1); }
#include "tier1.inc"
void MicmSolve(MICM *m, State *s, double dt, String *result, SolverResultStats *stats, Error *e) {
  NoError(e);
  if (!m || !s || s->owner != m || !result || !stats || !isfinite(dt) || dt < 0) { tmt_error(e, "invalid solver, state, time step, or result output"); return; }
  if (m->tier1) { tier1_solve(m, s, dt, result, stats, e); return; }
  tmc_dense *y = tmc_state_variables(s->native), *p = tmc_state_custom_rate_parameters(s->native);
  size_t nc = (size_t)tmc_state_num_cells(s->native);
  for (size_t i = 0; i < nc; ++i) {
    Conditions *c = &s->conditions[i];
    if (!isfinite(c->temperature) || c->temperature <= 0 || !isfinite(c->pressure) || c->pressure < 0 || !isfinite(c->air_density) || c->air_density < 0) {
      tmt_error(e, "conditions require positive temperature and nonnegative pressure and air density"); return;
    }
    for (int j = 0; j < y->n_cols; ++j) if (!isfinite(s->concentrations[tmc_dense_index(y, (int)i, j)])) { tmt_error(e, "nonfinite concentration"); return; }
    for (int j = 0; j < p->n_cols; ++j) if (!isfinite(s->parameters[tmc_dense_index(p, (int)i, j)])) { tmt_error(e, "nonfinite custom rate parameter"); return; }
  }
  int state_edit = s->generation != m->generation || memcmp(y->data, s->concentrations, y->size * sizeof(double));
  int rate_edit = p->size && memcmp(p->data, s->parameters, p->size * sizeof(double));
  int condition_edit = memcmp(tmc_state_conditions(s->native), s->conditions, nc * sizeof(Conditions));
  if (state_edit || condition_edit) tmc_state_invalidate_matrix(s->native);
  if (state_edit) tmc_state_set_next_step(s->native, 0);
  memcpy(y->data, s->concentrations, y->size * sizeof(double));
  if (p->size) memcpy(p->data, s->parameters, p->size * sizeof(double));
  memcpy(tmc_state_conditions(s->native), s->conditions, nc * sizeof(Conditions));
  if (s->generation != m->generation) {
    tmc_state_set_relative_tolerance(s->native, m->rtol);
    tmc_state_set_absolute_tolerances(s->native, m->n_atol ? m->atol : s->default_atol,
                                     tmc_state_num_variables(s->native), NULL);
    s->generation = m->generation;
  }
  if (!s->parameters_initialized || condition_edit || state_edit) tmc_solver_update_state_parameters(m->solver, s->native);
  else if (rate_edit) tmc_solver_update_custom_rate_constants(m->solver, s->native);
  s->parameters_initialized = 1;
  tmc_result r = tmc_solver_solve(m->solver, dt, s->native);
  y = tmc_state_variables(s->native);
  memcpy(s->concentrations, y->data, y->size * sizeof(double));
  CreateString(tmc_solver_state_name(r.state), result);
  *stats = (SolverResultStats){r.stats.function_calls, r.stats.jacobian_updates, r.stats.number_of_steps,
    r.stats.accepted, r.stats.rejected, r.stats.decompositions, r.stats.solves, r.stats.constraint_init_iterations, r.stats.final_time};
  if (r.state != TMC_CONVERGED) tmc_state_invalidate_matrix(s->native);
}

/* State work arrays depend on the method, so profile changes require a solver
 * with no outstanding state references. Rebuild before publishing the change. */
void TmtConfigureFable(MICM *m, TmtFableProfile profile, Error *e) {
  NoError(e);
  if (!m || m->tier1 || backward(m->type) || atomic_load(&m->references) != 1 || profile < 1 || profile > 3) {
    tmt_error(e, "Fable profile requires a Rosenbrock solver without existing states and a profile in [1,3]"); return;
  }
  tmc_rosenbrock_parameters ros = profile == 1 ? tmc_rosenbrock_four_stage_dae() : tmc_rosenbrock_three_stage();
  ros.loop = profile == 3 ? TMC_LOOP_EXTRAPOLATION : TMC_LOOP_ROW;
  ros.quantize = 1; ros.warm_start = 1;
  ros.controller_type = profile == 2 ? TMC_CONTROLLER_H211B : TMC_CONTROLLER_MICM;
  ros.extrapolation_order = 4; ros.extrapolation_sequence = TMC_SEQUENCE_SHIFTED;
  double rtol = profile == 1 ? 5e-3 : profile == 2 ? 8e-3 : 5e-4;
  double atol = profile == 3 ? 1e-22 : 1e-20;
  const TmtMechanismV1 *api = m->module.api;
  tmc_system system = api->system(); tmc_reaction *reactions = NULL; int n = api->reactions(&reactions);
  tmc_builder *b = tmc_builder_create_rosenbrock(ros);
  tmc_builder_set_system(b, &system); tmc_builder_set_reactions(b, reactions, n);
  tmc_builder_set_width(b, api->width); tmc_builder_set_kernels(b, api->kernels());
  tmc_error err = {0}; tmc_solver *replacement = tmc_builder_build(b, &err);
  tmc_builder_free(b); tmc_reactions_free(reactions, n); tmc_system_free(&system);
  if (!replacement) { tmt_error(e, err.message); return; }
  if (!tmc_solver_uses_kernels(replacement)) { tmc_solver_free(replacement); tmt_error(e, "Fable rejected profile kernels"); return; }
  size_t nv = (size_t)tmc_solver_num_variables(replacement);
  double *tolerance = malloc(nv * sizeof *tolerance);
  if (!tolerance) { tmc_solver_free(replacement); tmt_error(e, "allocation failed"); return; }
  for (size_t i = 0; i < nv; ++i) tolerance[i] = atol;
  tmc_solver_free(m->solver); m->solver = replacement;
  free(m->atol); m->atol = tolerance; m->n_atol = nv; m->rtol = rtol; ++m->generation;
}
