/* Copyright (C) 2026 University Corporation for Atmospheric Research
 * SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "internal.h"
#include "loader.h"
#include "paths.h"
#include <tmt/prepare.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

const char *TmtNativeIdentity(void) { return TMT_BUILD_ID; }

static int path_join(char *out, const char *a, const char *b, Error *e) {
  int n = snprintf(out, PATH_MAX, "%s/%s", a, b);
  if (n < 0 || n >= PATH_MAX) { tmt_error(e, "preparation path is too long"); return 0; }
  return 1;
}
static int process(const char *program, char *const argv[], const char *log, Error *e) {
  posix_spawn_file_actions_t actions;
  int rc = posix_spawn_file_actions_init(&actions);
  if (rc) { tmt_error(e, strerror(rc)); return 0; }
  rc = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log, O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t pid;
  if (!rc) rc = posix_spawnp(&pid, program, &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc) { char msg[PATH_MAX + 256]; snprintf(msg, sizeof msg, "cannot start %s: %s", program, strerror(rc)); tmt_error(e, msg); return 0; }
  int result;
  while (waitpid(pid, &result, 0) < 0) {
    if (errno == EINTR) continue;
    tmt_error(e, "waitpid failed during mechanism preparation"); return 0;
  }
  if (!WIFEXITED(result) || WEXITSTATUS(result)) {
    char msg[PATH_MAX + 128];
    snprintf(msg, sizeof msg, "preparation process %s (%d); diagnostics: %s",
             WIFSIGNALED(result) ? "terminated by signal" : "failed with exit status",
             WIFSIGNALED(result) ? WTERMSIG(result) : WEXITSTATUS(result), log);
    tmt_error(e, msg); return 0;
  }
  return 1;
}

int tmt_load_module(const char *directory, TmtModule *out, Error *e) {
  char manifest_path[PATH_MAX], library_path[PATH_MAX];
  if (!directory || !out || !path_join(manifest_path, directory, "manifest.json", e) ||
      !path_join(library_path, directory, "mechanism.so", e)) return 0;
  tt_config *doc = NULL; tt_error err = {0};
  if (!tmt_status(tt_config_read_file(manifest_path, &doc, &err), &err, e)) return 0;
  tt_config_node root = tt_config_root(doc);
  const char *identity = tt_config_string(tt_config_get(root, "native_identity"));
  const char *key = tt_config_string(tt_config_get(root, "cache_key"));
  int abi = 0, width = 0;
  tt_config_integer(tt_config_get(root, "abi"), &abi);
  tt_config_integer(tt_config_get(root, "width"), &width);
  if (!identity || strcmp(identity, TMT_BUILD_ID) || abi != TMT_MECHANISM_ABI || !key || strlen(key) != 64) {
    tt_config_destroy(doc); tmt_error(e, "prepared mechanism manifest is incompatible with this TMT runtime"); return 0;
  }
  void *library = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
  if (!library) { tmt_error(e, dlerror()); tt_config_destroy(doc); return 0; }
  void *symbol = dlsym(library, "tmt_mechanism_v1");
  TmtMechanismEntry entry = NULL;
  _Static_assert(sizeof entry == sizeof symbol, "POSIX function pointer representation");
  memcpy(&entry, &symbol, sizeof entry);
  const TmtMechanismV1 *api = entry ? entry() : NULL;
  if (!api || api->abi != TMT_MECHANISM_ABI || api->size != sizeof *api || !api->native_identity ||
      strcmp(api->native_identity, identity) || !api->cache_key || strcmp(api->cache_key, key) ||
      api->width != width || !api->system || !api->reactions || !api->kernels || !api->initial_state) {
    dlclose(library); tt_config_destroy(doc); tmt_error(e, "prepared mechanism export contract mismatch"); return 0;
  }
  tt_config_destroy(doc);
  out->library = library; out->api = api; return 1;
}
void tmt_unload_module(TmtModule *module) {
  if (module && module->library) dlclose(module->library);
  if (module) memset(module, 0, sizeof *module);
}

static int validate_kernels(const TmtMechanismV1 *api, Error *e) {
  tmc_system system = api->system();
  tmc_reaction *reactions = NULL;
  int count = api->reactions(&reactions);
  tmc_builder *builder = tmc_builder_create_rosenbrock(tmc_rosenbrock_three_stage());
  tmc_builder_set_system(builder, &system);
  tmc_builder_set_reactions(builder, reactions, count);
  tmc_builder_set_width(builder, api->width);
  tmc_builder_set_kernels(builder, api->kernels());
  tmc_error error = {0};
  tmc_solver *solver = tmc_builder_build(builder, &error);
  tmc_builder_free(builder);
  tmc_reactions_free(reactions, count);
  tmc_system_free(&system);
  int valid = solver && tmc_solver_uses_kernels(solver);
  if (!valid) tmt_error(e, solver ? "Fable rejected generated kernels before cache publication" : error.message);
  tmc_solver_free(solver);
  return valid;
}

static const char *option(const char *value, const char *env, const char *fallback) {
  if (value && *value) return value;
  const char *v = getenv(env);
  return v && *v ? v : fallback;
}
/* Locate installed support files relative to this library, while retaining
 * the configured source paths for an uninstalled developer build. */
static void installed_defaults(char *tools, char *fable, char *includes, char *runtime) {
  Dl_info info;
  const char *(*function)(void) = TmtNativeIdentity;
  void *address = NULL;
  _Static_assert(sizeof address == sizeof function, "POSIX function pointer representation");
  memcpy(&address, &function, sizeof address);
  if (!dladdr(address, &info) || !info.dli_fname) return;
  char location[PATH_MAX];
  if (!realpath(info.dli_fname, location)) return;
  char *slash = strrchr(location, '/'); if (!slash) return; *slash = '\0';
  char probe[PATH_MAX];
  if (!path_join(probe, location, "../share/tmt/tools/stage_mechanism.py", NULL) || access(probe, R_OK)) return;
  path_join(tools, location, "../share/tmt/tools", NULL);
  path_join(fable, location, "../share/tmt/fable", NULL);
  path_join(includes, location, "../include", NULL);
#ifdef __APPLE__
  path_join(runtime, location, "libtmt_fable.dylib", NULL);
#else
  path_join(runtime, location, "libtmt_fable.so", NULL);
#endif
}
static int make_directory(const char *path, Error *e) {
  char copy[PATH_MAX];
  if (strlen(path) >= sizeof copy) { tmt_error(e, "cache path too long"); return 0; }
  strcpy(copy, path);
  for (char *p = copy + 1; ; ++p) {
    if (*p != '/' && *p != '\0') continue;
    char end = *p; *p = '\0';
    if (mkdir(copy, 0700) && errno != EEXIST) { tmt_error(e, "cannot create mechanism cache directory"); return 0; }
    *p = end; if (!end) break;
  }
  return 1;
}
/* Remove only the fixed files this preparation stage owns. Failed work is kept. */
static void clean_work(const char *work) {
  const char *files[] = {"compiler.log", "translate.log", "normalized.json", "manifest.json", "key.txt", "export.c"};
  char path[PATH_MAX];
  for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
    if (path_join(path, work, files[i], NULL)) unlink(path);
  }
  rmdir(work);
}

void TmtPrepareMechanism(const char *input, const TmtPrepareOptions *given, String *artifact, Error *e) {
  NoError(e);
  if (!input || !artifact) { tmt_error(e, "mechanism input and artifact output are required"); return; }
  TmtPrepareOptions zero = {0}; const TmtPrepareOptions *o = given ? given : &zero;
  char default_tools[PATH_MAX] = TMT_DEFAULT_TOOLS, default_fable[PATH_MAX] = TMT_DEFAULT_FABLE;
  char default_include[PATH_MAX] = TMT_DEFAULT_INCLUDE, default_runtime[PATH_MAX] = TMT_DEFAULT_RUNTIME;
  installed_defaults(default_tools, default_fable, default_include, default_runtime);
  const char *python = option(o->python, "TMT_PYTHON", TMT_DEFAULT_PYTHON);
  const char *compiler = option(o->compiler, "TMT_CC", TMT_DEFAULT_COMPILER);
  const char *tools = option(o->tools_directory, "TMT_TOOLS", default_tools);
  const char *fable = option(o->fable_directory, "TMT_FABLE", default_fable);
  const char *includes = option(o->include_directory, "TMT_INCLUDE", default_include);
  const char *runtime = option(o->runtime_library, "TMT_FABLE_LIBRARY", default_runtime);
  const char *mode = o->mode ? o->mode : "exact";
  const char *ordering = o->ordering ? o->ordering : "markowitz";
  const char *sysroot = option(NULL, "TMT_SYSROOT", TMT_DEFAULT_SYSROOT);
  int width = o->width ? o->width : 2;
  if ((width != 1 && width != 2 && width != 4 && width != 8) ||
      (strcmp(mode, "exact") && strcmp(mode, "fast")) ||
      (strcmp(ordering, "markowitz") && strcmp(ordering, "minimum-fill"))) {
    tmt_error(e, "unsupported generator width, mode, or ordering"); return;
  }
  char default_cache[PATH_MAX];
  const char *home = getenv("HOME");
  if (!path_join(default_cache, home ? home : "/tmp", ".cache/tmt/mechanisms", e)) return;
  const char *cache = option(o->cache_directory, "TMT_CACHE", default_cache);
  if (!make_directory(cache, e)) return;
  char cache_abs[PATH_MAX], input_abs[PATH_MAX];
  if (!realpath(cache, cache_abs) || !realpath(input, input_abs)) { tmt_error(e, "cannot resolve mechanism input or cache path"); return; }
  char work[PATH_MAX], log[PATH_MAX], script[PATH_MAX], width_text[16], compiler_abs[PATH_MAX];
  if (!realpath(compiler, compiler_abs)) { tmt_error(e, "compiler must be an existing absolute or relative executable path"); return; }
  if (!path_join(work, cache_abs, ".prepare-XXXXXX", e)) return;
  if (!mkdtemp(work)) { tmt_error(e, "cannot create preparation work directory"); return; }
  if (!path_join(log, work, "compiler.log", e)) return;
  char *version_argv[] = {compiler_abs, "--version", NULL};
  if (!process(compiler_abs, version_argv, log, e)) return;
  if (!path_join(script, tools, "stage_mechanism.py", e) || !path_join(log, work, "translate.log", e)) return;
  snprintf(width_text, sizeof width_text, "%d", width);
  char *stage_argv[] = {(char *)python, script, "--input", input_abs, "--work", work, "--fable", (char *)fable,
    "--runtime", (char *)runtime, "--compiler", compiler_abs, "--native-id", TMT_BUILD_ID,
    "--width", width_text, "--mode", (char *)mode, "--ordering", (char *)ordering,
    "--sysroot", (char *)sysroot, "--sanitize", TMT_SANITIZE ? "1" : "0", NULL};
  if (!process(python, stage_argv, log, e)) return;
  char keyfile[PATH_MAX], key[66] = {0};
  if (!path_join(keyfile, work, "key.txt", e)) return;
  FILE *stream = fopen(keyfile, "r");
  if (!stream) { tmt_error(e, "translator did not produce a cache key"); return; }
  size_t count = fread(key, 1, 65, stream); fclose(stream);
  key[64] = '\0';
  if (count != 65 || strspn(key, "0123456789abcdef") != 64) { tmt_error(e, "invalid generated cache key"); return; }
  char final[PATH_MAX], lock_path[PATH_MAX], lock_name[72];
  snprintf(lock_name, sizeof lock_name, "%s.lock", key);
  if (!path_join(final, cache_abs, key, e) || !path_join(lock_path, cache_abs, lock_name, e)) return;
  int lock = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (lock < 0) { tmt_error(e, "cannot open mechanism cache lock"); return; }
  int locked;
  do { locked = flock(lock, LOCK_EX); } while (locked < 0 && errno == EINTR);
  if (locked < 0) { close(lock); tmt_error(e, "cannot lock mechanism cache"); return; }
  TmtModule module = {0};
  struct stat st;
  if (!stat(final, &st)) {
    if (tmt_load_module(final, &module, e)) { tmt_unload_module(&module); CreateString(final, artifact); clean_work(work); }
    close(lock); return;
  }
  char config[PATH_MAX], stem[PATH_MAX];
  if (!path_join(config, work, "normalized.json", e) || !path_join(stem, work, "mechanism", e) ||
      !path_join(script, fable, "tools/generate.py", e) || !path_join(log, work, "generate.log", e)) { close(lock); return; }
  char *generator_argv[] = {(char *)python, script, config, stem, "--name", "mechanism", "--width", width_text,
    "--mode", (char *)mode, "--ordering", (char *)ordering, NULL};
  if (!process(python, generator_argv, log, e)) { close(lock); return; }
  char source[PATH_MAX], wrapper[PATH_MAX], output[PATH_MAX], fable_include[PATH_MAX], width_define[32];
  if (!path_join(source, work, "mechanism.c", e) || !path_join(wrapper, work, "export.c", e) ||
      !path_join(output, work, "mechanism.so", e) || !path_join(fable_include, fable, "include", e) ||
      !path_join(log, work, "compile.log", e)) { close(lock); return; }
  snprintf(width_define, sizeof width_define, "-DTMC_WIDTH=%d", width);
#ifdef __APPLE__
  const char *shared_flag = "-dynamiclib";
#else
  const char *shared_flag = "-shared";
#endif
  char *compile_argv[48] = {compiler_abs, "-std=c11", "-O3", "-fPIC", "-ffp-contract=off", (char *)shared_flag,
    width_define, "-I", (char *)includes, "-I", fable_include, source, wrapper, (char *)runtime, "-lm", "-o", output, NULL};
  size_t argc = 0; while (compile_argv[argc]) ++argc;
  if (*sysroot) { compile_argv[argc++] = "-isysroot"; compile_argv[argc++] = (char *)sysroot; }
  if (TMT_SANITIZE) { compile_argv[argc++] = "-fsanitize=address,undefined"; compile_argv[argc++] = "-fno-omit-frame-pointer"; }
  compile_argv[argc] = NULL;
  if (!process(compiler_abs, compile_argv, log, e)) { close(lock); return; }
  if (!tmt_load_module(work, &module, e)) { close(lock); return; }
  int valid = validate_kernels(module.api, e);
  tmt_unload_module(&module);
  if (!valid) { close(lock); return; }
  if (rename(work, final)) { tmt_error(e, "cannot atomically publish mechanism cache entry"); close(lock); return; }
  CreateString(final, artifact);
  close(lock);
}
