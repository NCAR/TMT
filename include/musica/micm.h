/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TMT_MICM_H
#define TMT_MICM_H
#include <musica/musica.h>
#include <stdint.h>
typedef struct MICM MICM;
typedef struct State State;
/* TMT-owned serialized chemistry. Native objects from another installation
 * cannot cross this boundary; use a configuration or this explicit container. */
typedef struct { const char *configuration; } Chemistry;
typedef enum {
  UndefinedSolver, Rosenbrock, RosenbrockStandardOrder, BackwardEuler,
  BackwardEulerStandardOrder, CudaRosenbrock, RosenbrockDAE4,
  RosenbrockDAE4StandardOrder, RosenbrockDAE6, RosenbrockDAE6StandardOrder
} MICMSolver;
typedef struct { double temperature, pressure, air_density; } Conditions;
typedef struct {
  int64_t function_calls_, jacobian_updates_, number_of_steps_, accepted_, rejected_,
    decompositions_, solves_, constraint_init_iterations_;
  double final_time_;
} SolverResultStats;
typedef struct {
  double relative_tolerance;
  double *absolute_tolerances;
  size_t num_absolute_tolerances;
  double h_min, h_max, h_start;
  size_t max_number_of_steps, constraint_init_max_iterations;
  double constraint_init_tolerance;
} RosenbrockSolverParametersC;
typedef struct {
  double relative_tolerance;
  double *absolute_tolerances;
  size_t num_absolute_tolerances, max_number_of_steps;
  double *time_step_reductions;
  size_t num_time_step_reductions;
} BackwardEulerSolverParametersC;
MICM *CreateMicm(const char *, MICMSolver, Error *);
MICM *CreateMicmFromConfigString(const char *, MICMSolver, Error *);
MICM *CreateMicmFromChemistryMechanism(const Chemistry *, MICMSolver, Error *);
/* A prepared directory needs neither Python nor a compiler. Its generated
 * width must match the solver type (standard=1, vector=2). */
MICM *TmtCreateMicmPrepared(const char *, MICMSolver, Error *);
bool TmtMicmUsesKernels(const MICM *);
void DeleteMicm(MICM *, Error *);
void MicmSolve(MICM *, State *, double, String *, SolverResultStats *, Error *);
void MicmVersion(String *);
void GetSpeciesPropertyString(MICM *, const char *, const char *, String *, Error *);
double GetSpeciesPropertyDouble(MICM *, const char *, const char *, Error *);
int GetSpeciesPropertyInt(MICM *, const char *, const char *, Error *);
bool GetSpeciesPropertyBool(MICM *, const char *, const char *, Error *);
size_t GetMaximumNumberOfGridCells(MICM *);
bool _IsCudaAvailable(Error *);
size_t GetVectorSize(MICMSolver);
void SetRosenbrockSolverParameters(MICM *, const RosenbrockSolverParametersC *, Error *);
void SetBackwardEulerSolverParameters(MICM *, const BackwardEulerSolverParametersC *, Error *);
void GetRosenbrockSolverParameters(MICM *, RosenbrockSolverParametersC *, Error *);
void GetBackwardEulerSolverParameters(MICM *, BackwardEulerSolverParametersC *, Error *);
void FreeRosenbrockSolverParameters(RosenbrockSolverParametersC *);
void FreeBackwardEulerSolverParameters(BackwardEulerSolverParametersC *);
State *CreateMicmState(MICM *, size_t, Error *);
void DeleteState(State *, Error *);
Conditions *GetConditionsPointer(State *, size_t *, Error *);
double *GetOrderedConcentrationsPointer(State *, size_t *, Error *);
double *GetOrderedRateParametersPointer(State *, size_t *, Error *);
void GetSpeciesOrdering(State *, Mappings *, Error *);
void GetUserDefinedRateParametersOrdering(State *, Mappings *, Error *);
size_t GetNumberOfGridCells(State *, Error *);
size_t GetNumberOfSpecies(State *, Error *);
size_t GetNumberOfUserDefinedRateParameters(State *, Error *);
void GetConcentrationsStrides(State *, Error *, size_t *, size_t *);
void GetUserDefinedRateParametersStrides(State *, Error *, size_t *, size_t *);
#endif
