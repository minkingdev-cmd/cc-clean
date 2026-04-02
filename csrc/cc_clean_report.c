#include "cc_clean.h"

/* ------------------------- JSON / 报告输出 ------------------------- */

static void json_escape(FILE *out, const char *s) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
    switch (*p) {
    case '\\':
      fputs("\\\\", out);
      break;
    case '"':
      fputs("\\\"", out);
      break;
    case '\n':
      fputs("\\n", out);
      break;
    case '\r':
      fputs("\\r", out);
      break;
    case '\t':
      fputs("\\t", out);
      break;
    default:
      if (*p < 32) {
        fprintf(out, "\\u%04x", *p);
      } else {
        fputc(*p, out);
      }
    }
  }
  fputc('"', out);
}

static void print_artifact_json(FILE *out, const Artifact *a) {
  fputs("{\"kind\":", out);
  json_escape(out, a->kind);
  fputs(",\"identifier\":", out);
  json_escape(out, a->identifier);
  fprintf(out, ",\"exists\":%s", a->exists ? "true" : "false");
  fputs(",\"confidence\":", out);
  json_escape(out, a->confidence);
  fprintf(out, ",\"safe_clean\":%s", a->safe_clean ? "true" : "false");
  fputs(",\"note\":", out);
  json_escape(out, a->note);
  fputs(",\"detail\":", out);
  json_escape(out, a->detail ? a->detail : "");
  fputc('}', out);
}

static size_t count_exists(const ArtifactList *list) {
  size_t n = 0;
  for (size_t i = 0; i < list->len; ++i) {
    if (list->items[i].exists) {
      n++;
    }
  }
  return n;
}

void print_report_text(const char *config_home,
                              const char *home,
                              const ArtifactList *runtime,
                              const ArtifactList *related) {
  char *display = tilde_path(config_home, home);
  printf("配置根目录: %s\n", display);
  free(display);

  printf("== 检查结果 ==\n");
  printf("高置信度运行痕迹：%zu 个\n", count_exists(runtime));
  printf("相关但不默认清理的痕迹：%zu 个\n\n", count_exists(related));

  if (count_exists(runtime) == 0) {
    printf("[高置信度运行痕迹] 未发现\n\n");
  } else {
    printf("[高置信度运行痕迹]\n");
    for (size_t i = 0; i < runtime->len; ++i) {
      const Artifact *a = &runtime->items[i];
      if (!a->exists) {
        continue;
      }
      printf("- [%s] %s\n", a->kind, a->identifier);
      printf("  说明: %s\n", a->note);
      if (a->detail && *a->detail) {
        printf("  内容: %s\n", a->detail);
      }
    }
    printf("\n");
  }

  if (count_exists(related) == 0) {
    printf("[相关但不默认清理的痕迹] 未发现\n\n");
  } else {
    printf("[相关但不默认清理的痕迹]\n");
    for (size_t i = 0; i < related->len; ++i) {
      const Artifact *a = &related->items[i];
      if (!a->exists) {
        continue;
      }
      printf("- [%s] %s\n", a->kind, a->identifier);
      printf("  说明: %s\n", a->note);
      if (a->detail && *a->detail) {
        printf("  内容: %s\n", a->detail);
      }
    }
    printf("\n");
  }
}

void print_cleanup_results(const CleanupResults *results) {
  printf("== 清理结果 ==\n");
  for (size_t i = 0; i < results->len; ++i) {
    const CleanupResult *r = &results->items[i];
    printf("- [%s] %s %s\n", r->status, r->kind, r->identifier);
    if (r->message && *r->message) {
      printf("  说明: %s\n", r->message);
    }
  }
}
void print_json_payload(const char *config_home,
                               const ArtifactList *runtime,
                               const ArtifactList *related,
                               const ArtifactList *targets,
                               const CleanupResults *results) {
  printf("{\n  \"config_home\": ");
  json_escape(stdout, config_home);
  printf(",\n  \"runtime\": [");
  for (size_t i = 0; i < runtime->len; ++i) {
    if (i > 0) {
      printf(", ");
    }
    print_artifact_json(stdout, &runtime->items[i]);
  }
  printf("],\n  \"related\": [");
  for (size_t i = 0; i < related->len; ++i) {
    if (i > 0) {
      printf(", ");
    }
    print_artifact_json(stdout, &related->items[i]);
  }
  printf("]");

  if (targets) {
    printf(",\n  \"cleanup_targets\": [");
    for (size_t i = 0; i < targets->len; ++i) {
      if (i > 0) {
        printf(", ");
      }
      print_artifact_json(stdout, &targets->items[i]);
    }
    printf("]");
  }

  if (results) {
    printf(",\n  \"cleanup_results\": [");
    for (size_t i = 0; i < results->len; ++i) {
      if (i > 0) {
        printf(", ");
      }
      printf("{\"kind\":");
      json_escape(stdout, results->items[i].kind);
      printf(",\"identifier\":");
      json_escape(stdout, results->items[i].identifier);
      printf(",\"status\":");
      json_escape(stdout, results->items[i].status);
      printf(",\"message\":");
      json_escape(stdout, results->items[i].message);
      printf("}");
    }
    printf("]");
  }
  printf("\n}\n");
}

