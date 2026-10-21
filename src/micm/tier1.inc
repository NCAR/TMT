/* Internal implementation: preserve the standard-order public arrays while
 * solving independent groups of at most 32 cells with width-2 kernels. */
static void tier1_trace(State *s, double dt, const struct timespec *start,
                        const SolverResultStats *stats, int converged) {
  const char *directory = getenv("TMT_MICM_TRACE_DIR");
  if (!directory || !*directory) return;
  struct timespec finish;
  clock_gettime(CLOCK_MONOTONIC, &finish);
  if (!s->trace) {
    static atomic_size_t sequence;
    char path[PATH_MAX];
    size_t id = atomic_fetch_add(&sequence, 1);
    int n = snprintf(path, sizeof path, "%s/micm.%ld.%zu.csv", directory, (long)getpid(), id);
    if (n < 0 || n >= (int)sizeof path) return;
    s->trace = fopen(path, "w");
    if (!s->trace) return;
    fputs("rank,cells,groups,call,dt,seconds,rhs,jacobians,steps,accepted,rejected,lu,solves,converged\n", s->trace);
  }
  const char *rank = getenv("PMI_RANK");
  double seconds = (double)(finish.tv_sec-start->tv_sec) + 1e-9*(finish.tv_nsec-start->tv_nsec);
  fprintf(s->trace, "%d,%zu,%zu,%zu,%.17g,%.9f,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%d\n",
          rank ? atoi(rank) : -1, s->cells, s->n_groups, s->solve_calls, dt, seconds,
          (long long)stats->function_calls_, (long long)stats->jacobian_updates_,
          (long long)stats->number_of_steps_, (long long)stats->accepted_,
          (long long)stats->rejected_, (long long)stats->decompositions_,
          (long long)stats->solves_, converged);
  fflush(s->trace);
}

static void tier1_solve(MICM *m, State *s, double dt, String *result,
                        SolverResultStats *stats, Error *e) {
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  *stats = (SolverResultStats){0};
  size_t nv = (size_t)tmc_solver_num_variables(m->solver);
  size_t np = (size_t)tmc_solver_num_custom_parameters(m->solver);
  /* Validate the entire public state before modifying any group's state. */
  for (size_t c = 0; c < s->cells; ++c) {
    Conditions *condition = &s->conditions[c];
    if (!isfinite(condition->temperature) || condition->temperature <= 0 ||
        !isfinite(condition->pressure) || condition->pressure < 0 ||
        !isfinite(condition->air_density) || condition->air_density < 0) {
      tmt_error(e, "invalid Tier 1 cell conditions"); return;
    }
    for (size_t v = 0; v < nv; ++v) if (!isfinite(s->concentrations[c*nv+v])) {
      tmt_error(e, "nonfinite concentration"); return;
    }
    for (size_t v = 0; v < np; ++v) if (!isfinite(s->parameters[c*np+v])) {
      tmt_error(e, "nonfinite custom rate parameter"); return;
    }
  }
  ++s->solve_calls;
  for (size_t g = 0; g < s->n_groups; ++g) {
    tmc_state *group = s->groups[g];
    tmc_dense *y = tmc_state_variables(group), *p = tmc_state_custom_rate_parameters(group);
    size_t first = g * 32, cells = (size_t)tmc_state_num_cells(group);
    int state_edit = s->generation != m->generation || !s->parameters_initialized;
    int condition_edit = memcmp(tmc_state_conditions(group), s->conditions+first, cells*sizeof(Conditions)) != 0;
    int rate_edit = 0;
    /* Copy width-2 blocks in their contiguous storage order. The final odd
     * lane is unused by Fable and remains at its initialized padding value. */
    for (size_t c = 0; c < cells; c += 2) {
      size_t lanes = cells-c < 2 ? cells-c : 2;
      for (size_t v = 0; v < nv; ++v) for (size_t lane = 0; lane < lanes; ++lane) {
        size_t local = c*nv+2*v+lane;
        double value = s->concentrations[(first+c+lane)*nv+v];
        state_edit |= y->data[local] != value; y->data[local] = value;
      }
      for (size_t v = 0; v < np; ++v) for (size_t lane = 0; lane < lanes; ++lane) {
        size_t local = c*np+2*v+lane;
        double value = s->parameters[(first+c+lane)*np+v];
        rate_edit |= p->data[local] != value; p->data[local] = value;
      }
    }
    if (state_edit || condition_edit) tmc_state_invalidate_matrix(group);
    if (state_edit) tmc_state_set_next_step(group, 0);
    memcpy(tmc_state_conditions(group), s->conditions+first, cells*sizeof(Conditions));
    if (s->generation != m->generation) {
      tmc_state_set_relative_tolerance(group, m->rtol);
      tmc_state_set_absolute_tolerances(group, m->n_atol ? m->atol : s->default_atol, (int)nv, NULL);
    }
    if (!s->parameters_initialized || condition_edit || state_edit) tmc_solver_update_state_parameters(m->solver, group);
    else if (rate_edit) tmc_solver_update_custom_rate_constants(m->solver, group);
    tmc_result r = tmc_solver_solve(m->solver, dt, group);
    stats->function_calls_ += r.stats.function_calls;
    stats->jacobian_updates_ += r.stats.jacobian_updates;
    stats->number_of_steps_ += r.stats.number_of_steps;
    stats->accepted_ += r.stats.accepted;
    stats->rejected_ += r.stats.rejected;
    stats->decompositions_ += r.stats.decompositions;
    stats->solves_ += r.stats.solves;
    stats->constraint_init_iterations_ += r.stats.constraint_init_iterations;
    if (r.state != TMC_CONVERGED) {
      /* Groups may have advanced different amounts. Publish no partial state;
       * the next solve repacks the unchanged public input and resets all groups. */
      s->parameters_initialized = 0;
      CreateString(tmc_solver_state_name(r.state), result);
      tier1_trace(s, dt, &start, stats, 0);
      return;
    }
  }
  for (size_t g = 0; g < s->n_groups; ++g) {
    tmc_dense *y = tmc_state_variables(s->groups[g]);
    size_t first = g*32, cells = (size_t)y->n_rows;
    for (size_t c = 0; c < cells; c += 2) {
      size_t lanes = cells-c < 2 ? cells-c : 2;
      for (size_t v = 0; v < nv; ++v) for (size_t lane = 0; lane < lanes; ++lane)
        s->concentrations[(first+c+lane)*nv+v] = y->data[c*nv+2*v+lane];
    }
  }
  s->generation = m->generation;
  s->parameters_initialized = 1;
  stats->final_time_ = dt;
  CreateString(tmc_solver_state_name(TMC_CONVERGED), result);
  tier1_trace(s, dt, &start, stats, 1);
}
