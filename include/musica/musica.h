/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#ifndef TMT_MUSICA_H
#define TMT_MUSICA_H
#include <stddef.h>
#include <stdbool.h>
#include <musica/utils/error.h>

typedef struct { char *value_; size_t size_; } String;
typedef struct { int code_, severity_; String category_, message_; } Error;
typedef struct { String name_; size_t index_; } Mapping;
typedef struct { Mapping *mappings_; size_t size_; } Mappings;
typedef struct { size_t source_, target_; double scale_factor_; } IndexMapping;
typedef struct { IndexMapping *mappings_; size_t size_; } IndexMappings;
typedef struct { void *data_; } Configuration;
typedef enum { UndefinedMapping, MapAny, MapAll } IndexMappingOptions;

/* Initialize output structs to zero; release owned strings/errors/mappings
 * with the matching Delete function. Errors may be reused after initialization. */
void CreateString(const char *, String *);
void DeleteString(String *);
void DeleteError(Error *);
void NoError(Error *);
void ToError(const char *, int, const char *, int, Error *);
Mapping *AllocateMappingArray(size_t);
void CreateMappings(size_t, Mappings *);
size_t FindMappingIndex(Mappings, const char *, Error *);
void DeleteMapping(Mapping *);
void DeleteMappings(Mappings *);
void LoadConfigurationFromString(const char *, Configuration *, Error *);
void LoadConfigurationFromFile(const char *, Configuration *, Error *);
void DeleteConfiguration(Configuration *);
void CreateIndexMappings(Configuration, IndexMappingOptions, Mappings, Mappings, IndexMappings *, Error *);
size_t GetIndexMappingsSize(IndexMappings);
void CopyData(IndexMappings, const double *, double *);
void DeleteIndexMappings(IndexMappings *);
void MusicaVersion(String *);

#endif
