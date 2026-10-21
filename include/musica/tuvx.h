/* Copyright (C) 2023-2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0
 * Public declarations adapted from the pinned MUSICA interface. */
#ifndef TMT_TUVX_H
#define TMT_TUVX_H
#include <musica/musica.h>
typedef struct Grid Grid;
typedef struct Profile Profile;
typedef struct Radiator Radiator;
typedef struct GridMap GridMap;
typedef struct ProfileMap ProfileMap;
typedef struct RadiatorMap RadiatorMap;
typedef struct RadiationFieldUpdater RadiationFieldUpdater;
typedef struct TUVX TUVX;
Grid* CreateGrid(const char* grid_name, const char* units, size_t num_sections, Error* error);
const char* GetGridName(Grid* grid, Error* error);
const char* GetGridUnits(Grid* grid, Error* error);
size_t GetGridNumberOfSections(Grid* grid, Error* error);
void DeleteGrid(Grid* grid, Error* error);
void SetGridEdges(Grid* grid, double edges[], size_t num_edges, Error* error);
void GetGridEdges(Grid* grid, double edges[], size_t num_edges, Error* error);
double* GetGridEdgesPointer(Grid* grid, Error* error);
void SetGridMidpoints(Grid* grid, double midpoints[], size_t num_midpoints, Error* error);
void GetGridMidpoints(Grid* grid, double midpoints[], size_t num_midpoints, Error* error);
double* GetGridMidpointsPointer(Grid* grid, Error* error);
Profile* CreateProfile(const char* profile_name, const char* units, Grid* grid, Error* error);
void DeleteProfile(Profile* profile, Error* error);
const char* GetProfileName(Profile* profile, Error* error);
const char* GetProfileUnits(Profile* profile, Error* error);
void SetProfileEdgeValues(Profile* profile, double edge_values[], size_t num_values, Error* error);
void GetProfileEdgeValues(Profile* profile, double edge_values[], size_t num_values, Error* error);
double* GetProfileEdgeValuesPointer(Profile* profile, Error* error);
void SetProfileMidpointValues(Profile* profile, double midpoint_values[], size_t num_values, Error* error);
void GetProfileMidpointValues(Profile* profile, double midpoint_values[], size_t num_values, Error* error);
double* GetProfileMidpointValuesPointer(Profile* profile, Error* error);
void SetProfileLayerDensities(Profile* profile, double layer_densities[], size_t num_values, Error* error);
void GetProfileLayerDensities(Profile* profile, double layer_densities[], size_t num_values, Error* error);
double* GetProfileLayerDensitiesPointer(Profile* profile, Error* error);
void SetProfileExoLayerDensity(Profile* profile, double exo_layer_density, Error* error);
void CalculateProfileExoLayerDensity(Profile* profile, double scale_height, Error* error);
double GetProfileExoLayerDensity(Profile* profile, Error* error);
size_t GetProfileNumberOfSections(Profile* profile, Error* error);
Radiator *CreateRadiator(const char *radiator_name, Grid *height_grid, Grid *wavelength_grid, Error *error);
void DeleteRadiator(Radiator *radiator, Error *error);
const char *GetRadiatorName(Radiator *radiator, Error *error);
void SetRadiatorOpticalDepths( Radiator *radiator, double *optical_depths, size_t num_vertical_layers, size_t num_wavelength_bins, Error *error);
void GetRadiatorOpticalDepths( Radiator *radiator, double *optical_depths, size_t num_vertical_layers, size_t num_wavelength_bins, Error *error);
double *GetRadiatorOpticalDepthsPointer(Radiator *radiator, Error *error);
void SetRadiatorSingleScatteringAlbedos( Radiator *radiator, double *single_scattering_albedos, size_t num_vertical_layers, size_t num_wavelength_bins, Error *error);
void GetRadiatorSingleScatteringAlbedos( Radiator *radiator, double *single_scattering_albedos, size_t num_vertical_layers, size_t num_wavelength_bins, Error *error);
double *GetRadiatorSingleScatteringAlbedosPointer(Radiator *radiator, Error *error);
void SetRadiatorAsymmetryFactors( Radiator *radiator, double *asymmetry_factor, size_t num_vertical_layers, size_t num_wavelength_bins, size_t num_streams, Error *error);
void GetRadiatorAsymmetryFactors( Radiator *radiator, double *asymmetry_factor, size_t num_vertical_layers, size_t num_wavelength_bins, size_t num_streams, Error *error);
double *GetRadiatorAsymmetryFactorsPointer(Radiator *radiator, Error *error);
size_t GetRadiatorNumberOfHeightSections(Radiator *radiator, Error *error);
size_t GetRadiatorNumberOfWavelengthSections(Radiator *radiator, Error *error);
GridMap *CreateGridMap(Error *error);
void DeleteGridMap(GridMap *grid_map, Error *error);
void AddGrid(GridMap *grid_map, Grid *grid, Error *error);
Grid *GetGrid(GridMap *grid_map, const char *grid_name, const char *grid_units, Error *error);
Grid *GetGridByIndex(GridMap *grid_map, size_t index, Error *error);
size_t GetNumberOfGrids(GridMap *grid_map, Error *error);
void RemoveGrid(GridMap *grid_map, const char *grid_name, const char *grid_units, Error *error);
void RemoveGridByIndex(GridMap *grid_map, size_t index, Error *error);
ProfileMap *CreateProfileMap(Error *error);
void DeleteProfileMap(ProfileMap *profile_map, Error *error);
void AddProfile(ProfileMap *profile_map, Profile *profile, Error *error);
Profile *GetProfile(ProfileMap *profile_map, const char *profile_name, const char *profile_units, Error *error);
Profile *GetProfileByIndex(ProfileMap *profile_map, size_t index, Error *error);
void RemoveProfile(ProfileMap *profile_map, const char *profile_name, const char *profile_units, Error *error);
void RemoveProfileByIndex(ProfileMap *profile_map, size_t index, Error *error);
size_t GetNumberOfProfiles(ProfileMap *profile_map, Error *error);
RadiatorMap *CreateRadiatorMap(Error *error);
void DeleteRadiatorMap(RadiatorMap *radiator_map, Error *error);
void AddRadiator(RadiatorMap *radiator_map, Radiator *radiator, Error *error);
Radiator *GetRadiator(RadiatorMap *radiator_map, const char *radiator_name, Error *error);
Radiator *GetRadiatorByIndex(RadiatorMap *radiator_map, size_t index, Error *error);
void RemoveRadiator(RadiatorMap *radiator_map, const char *radiator_name, Error *error);
void RemoveRadiatorByIndex(RadiatorMap *radiator_map, size_t index, Error *error);
size_t GetNumberOfRadiators(RadiatorMap *radiator_map, Error *error);
void DeleteRadiationFieldUpdater(RadiationFieldUpdater* updater, Error* error);
void UpdateRadiationField( RadiationFieldUpdater* updater, double* direct_actinic_flux, double* upward_actinic_flux, double* downward_actinic_flux, double* direct_irradiance, double* upward_irradiance, double* downward_irradiance, size_t num_vertical_interfaces, size_t num_wavelength_bins, Error* error);
TUVX* CreateTuvx(const char* config_path, GridMap* grids, ProfileMap* profiles, RadiatorMap* radiators, Error* error);
TUVX* CreateTuvxFromConfigString( const char* config_string, GridMap* grids, ProfileMap* profiles, RadiatorMap* radiators, Error* error);
void DeleteTuvx(const TUVX* tuvx, Error* error);
GridMap* GetGridMap(TUVX* tuvx, Error* error);
ProfileMap* GetProfileMap(TUVX* tuvx, Error* error);
RadiatorMap* GetRadiatorMap(TUVX* tuvx, Error* error);
RadiationFieldUpdater* GetRadiationFieldUpdater(TUVX* tuvx, Error* error);
void GetPhotolysisRateConstantsOrdering(TUVX* tuvx, Mappings* mappings, Error* error);
void GetHeatingRatesOrdering(TUVX* tuvx, Mappings* mappings, Error* error);
void GetDoseRatesOrdering(TUVX* tuvx, Mappings* mappings, Error* error);
/* Solar zenith angle is in radians, matching MUSICA's public interface. */
void RunTuvx( TUVX* tuvx, const double solar_zenith_angle, const double earth_sun_distance, double* const photolysis_rate_constants, double* const heating_rates, double* const dose_rates, double* const actinic_flux, double* const spectral_irradiance, Error* const error);
void TuvxVersion(String* tuvx_version);
/* TMT shape queries for callers allocating output buffers. */
void TmtTuvxDimensions(TUVX *, size_t *, size_t *, size_t *, size_t *, size_t *, Error *);
#endif
