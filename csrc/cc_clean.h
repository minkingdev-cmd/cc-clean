#ifndef CC_CLEAN_H
#define CC_CLEAN_H

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <direct.h>
#include <wincred.h>
#define PATH_SEP '\\'
#define PATH_MAX_LEN 4096
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PATH_SEP '/'
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_MAX_LEN PATH_MAX
#endif

#ifndef _WIN32
extern char **environ;
#endif

typedef struct {
  char *kind;
  char *identifier;
  bool exists;
  char *confidence;
  bool safe_clean;
  char *note;
  char *detail;
} Artifact;

typedef struct {
  Artifact *items;
  size_t len;
  size_t cap;
} ArtifactList;

typedef struct {
  bool action_clean;
  bool action_restore;
  bool include_related;
  bool purge_config_home;
  bool allow_unsafe_purge;
  bool allow_unsafe_restore;
  bool dry_run;
  bool yes;
  bool json;
  char *config_dir;
  char *backup_dir;
} Options;

typedef struct {
  FILE *manifest_fp;
  FILE *index_fp;
  char *backup_dir;
  char *files_root;
  bool enabled;
} BackupContext;

typedef struct {
  char *kind;
  char *identifier;
  char *status;
  char *message;
} CleanupResult;

typedef struct {
  CleanupResult *items;
  size_t len;
  size_t cap;
} CleanupResults;

int cc_clean_run(int argc, char **argv);

void normalize_separators(char *s);
char *expand_home(const char *path, const char *home);
char *get_home_dir(void);
char *tilde_path(const char *path, const char *home);
char *xstrdup(const char *s);
char *str_printf(const char *fmt, ...);
bool is_safe_purge_target_path(const char *path, const char *home);
char *get_default_config_home(const char *home);

void artifact_list_init(ArtifactList *list);
void artifact_list_free(ArtifactList *list);
void cleanup_results_init(CleanupResults *r);
void cleanup_results_free(CleanupResults *r);

bool backup_context_init(BackupContext *ctx, const char *backup_dir);
void backup_context_close(BackupContext *ctx);

void collect_artifacts(const char *home,
                       const char *config_home,
                       ArtifactList *runtime,
                       ArtifactList *related);
void build_cleanup_targets(const ArtifactList *runtime,
                           const ArtifactList *related,
                           bool include_related,
                           bool purge_config_home,
                           const char *config_home,
                           const char *home,
                           ArtifactList *targets);
void print_cleanup_plan(const ArtifactList *targets, bool dry_run);
bool confirm_action(const char *prompt);
void run_cleanup(const ArtifactList *targets,
                 bool dry_run,
                 CleanupResults *results,
                 const char *home,
                 const char *config_home,
                 BackupContext *backup_ctx);
int restore_from_backup_dir(const char *backup_dir,
                            bool dry_run,
                            const char *home,
                            bool allow_unsafe_restore,
                            CleanupResults *results);

void print_report_text(const char *config_home,
                       const char *home,
                       const ArtifactList *runtime,
                       const ArtifactList *related);
void print_cleanup_results(const CleanupResults *results);
void print_json_payload(const char *config_home,
                        const ArtifactList *runtime,
                        const ArtifactList *related,
                        const ArtifactList *targets,
                        const CleanupResults *results);

#endif
