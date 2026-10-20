/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#include "internal.h"

char *tmt_strdup(const char *text) {
    if (!text) text = "";
    size_t n = strlen(text) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, text, n);
    return copy;
}
int tmt_size_product(size_t a, size_t b, size_t *out) {
    if (b && a > SIZE_MAX / b) return 0;
    *out = a * b; return 1;
}
void CreateString(const char *text, String *out) {
    if (!out) return;
    out->value_ = tmt_strdup(text); out->size_ = out->value_ ? strlen(out->value_) : 0;
}
void DeleteString(String *s) { if (s) { free(s->value_); *s = (String){0}; } }
void DeleteError(Error *e) { if (e) { DeleteString(&e->category_); DeleteString(&e->message_); *e = (Error){0}; } }
void NoError(Error *e) { DeleteError(e); }
void ToError(const char *category, int code, const char *message, int severity, Error *e) {
    if (!e) return;
    DeleteError(e); e->code_ = code; e->severity_ = severity;
    CreateString(category, &e->category_); CreateString(message, &e->message_);
}
void tmt_error(Error *e, const char *message) { ToError("TMT Error", 1, message, 2, e); }
int tmt_status(tt_status code, const tt_error *detail, Error *e) {
    if (code == TT_OK) { NoError(e); return 1; }
    ToError("TurboTUV Error", (int)code, detail && detail->message[0] ? detail->message : tt_status_string(code), 2, e);
    return 0;
}
Mapping *AllocateMappingArray(size_t n) { return calloc(n, sizeof(Mapping)); }
void CreateMappings(size_t n, Mappings *out) {
    if (!out) return;
    *out = (Mappings){AllocateMappingArray(n), n};
    if (n && !out->mappings_) out->size_ = 0;
}
size_t FindMappingIndex(Mappings map, const char *name, Error *e) {
    NoError(e);
    if (name) for (size_t i=0; i<map.size_; ++i)
        if (map.mappings_[i].name_.value_ && !strcmp(map.mappings_[i].name_.value_, name)) return map.mappings_[i].index_;
    ToError("MUSICA Error", 2, "Mapping name not found", 2, e); return 0;
}
void DeleteMapping(Mapping *m) { if (m) DeleteString(&m->name_); }
void DeleteMappings(Mappings *m) {
    if (!m) return;
    for (size_t i=0; i<m->size_; ++i) DeleteMapping(m->mappings_ + i);
    free(m->mappings_); *m=(Mappings){0};
}
void LoadConfigurationFromString(const char *text, Configuration *out, Error *e) {
    NoError(e);
    if (!text || !out) { tmt_error(e,"Null configuration input"); return; }
    tt_config *doc=NULL; tt_error detail={0};
    if (tmt_status(tt_config_parse(text,strlen(text),&doc,&detail),&detail,e)) { DeleteConfiguration(out); out->data_=doc; }
}
void LoadConfigurationFromFile(const char *path, Configuration *out, Error *e) {
    NoError(e);
    if (!path || !out) { tmt_error(e,"Null configuration input"); return; }
    tt_config *doc=NULL; tt_error detail={0};
    if (tmt_status(tt_config_read_file(path,&doc,&detail),&detail,e)) { DeleteConfiguration(out); out->data_=doc; }
}
void DeleteConfiguration(Configuration *c) { if(c) { tt_config_destroy(c->data_); c->data_=NULL; } }
void CreateIndexMappings(Configuration c, IndexMappingOptions option, Mappings src, Mappings dst, IndexMappings *out, Error *e) {
    NoError(e);
    if (!out || !c.data_ || (option!=MapAny && option!=MapAll)) { tmt_error(e,"Invalid mapping configuration/options"); return; }
    tt_config_node root=tt_config_root(c.data_);
    if (tt_config_type(root)!=TT_CONFIG_SEQUENCE) { tmt_error(e,"Mappings must be a sequence"); return; }
    size_t n=tt_config_size(root); IndexMappings result={calloc(n,sizeof(IndexMapping)),0};
    if(n && !result.mappings_) { tmt_error(e,"Out of memory"); return; }
    for(size_t i=0;i<n;++i) {
        tt_config_node item=tt_config_at(root,i);
        const char *s=tt_config_string(tt_config_get(item,"source")), *d=tt_config_string(tt_config_get(item,"target"));
        if(!s || !d) { tmt_error(e,"Mapping requires source and target names"); goto fail; }
        Error local={0}; size_t si=FindMappingIndex(src,s,&local);
        if(local.code_) { DeleteError(&local); if(option==MapAny) continue; ToError(MUSICA_ERROR_CATEGORY,MUSICA_ERROR_CODE_MAPPING_NOT_FOUND,"Source mapping not found",2,e); goto fail; }
        size_t di=FindMappingIndex(dst,d,&local);
        if(local.code_) { DeleteError(&local); if(option==MapAny) continue; ToError(MUSICA_ERROR_CATEGORY,MUSICA_ERROR_CODE_MAPPING_NOT_FOUND,"Target mapping not found",2,e); goto fail; }
        double scale=1; tt_config_node value=tt_config_get(item,"scale factor");
        if(tt_config_type(value)!=TT_CONFIG_MISSING && tt_config_number(value,&scale)!=TT_OK) { tmt_error(e,"Invalid scale factor"); goto fail; }
        result.mappings_[result.size_++]=(IndexMapping){si,di,scale};
    }
    *out=result; return;
fail: free(result.mappings_);
}
size_t GetIndexMappingsSize(IndexMappings m) { return m.size_; }
void CopyData(IndexMappings m, const double *src, double *dst) {
    if(!src || !dst) return;
    for(size_t i=0;i<m.size_;++i) dst[m.mappings_[i].target_]=src[m.mappings_[i].source_]*m.mappings_[i].scale_factor_;
}
void DeleteIndexMappings(IndexMappings *m) { if(m) {free(m->mappings_); *m=(IndexMappings){0};} }
void MusicaVersion(String *s) { CreateString("TMT 0.1.0 (MUSICA-compatible)",s); }
