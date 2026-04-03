#include "cc_clean.h"

/* ------------------------- 参数解析 ------------------------- */

static void print_usage(const char *prog) {
  printf("用法: %s [check|clean|restore] [选项]\n", prog);
  printf("选项:\n");
  printf("  --purge-all              清理所有已知 Claude Code 痕迹（强制二次确认）\n");
  printf("  --config-dir <dir>       指定配置根目录\n");
  printf("  --backup-dir <dir>       指定备份目录（clean/restore）\n");
  printf("  --include-related        清理 settings/agents/agents.backup.*\n");
  printf("  --purge-config-home      删除整个配置根目录（高风险）\n");
  printf("  --allow-unsafe-purge     允许 purge 非默认 ~/.claude 目录（极高风险）\n");
  printf("  --allow-unsafe-restore   允许恢复到白名单之外的路径/系统存储（极高风险）\n");
  printf("  --dry-run                只显示计划，不执行删除\n");
  printf("  -y, --yes                跳过确认\n");
  printf("  --json                   输出 JSON\n");
  printf("  -h, --help               显示帮助\n");
}

static bool confirm_purge_all(void) {
  char buf[64];
  printf("即将执行 purge-all：这会删除当前机器上所有已知的 Claude Code 痕迹，包括配置、缓存、日志、历史、安装残留、shell/浏览器/IDE 集成及系统级项目。\n");
  printf("此操作不会因为 -y/--yes 而跳过确认。\n");
  printf("请输入 PURGE-ALL 继续：");
  fflush(stdout);
  if (!fgets(buf, sizeof(buf), stdin)) {
    return false;
  }
  size_t len = strlen(buf);
  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
    buf[--len] = '\0';
  }
  return strcmp(buf, "PURGE-ALL") == 0;
}

static Options parse_args(int argc, char **argv) {
  Options opt;
  memset(&opt, 0, sizeof(opt));
  opt.action_clean = false;
  opt.action_restore = false;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (strcmp(arg, "check") == 0) {
      opt.action_clean = false;
      opt.action_restore = false;
    } else if (strcmp(arg, "clean") == 0) {
      opt.action_clean = true;
      opt.action_restore = false;
    } else if (strcmp(arg, "restore") == 0) {
      opt.action_clean = false;
      opt.action_restore = true;
    } else if (strcmp(arg, "--purge-all") == 0) {
      opt.purge_all = true;
      opt.include_related = true;
      opt.purge_config_home = true;
    } else if (strcmp(arg, "--include-related") == 0) {
      opt.include_related = true;
    } else if (strcmp(arg, "--purge-config-home") == 0) {
      opt.purge_config_home = true;
    } else if (strcmp(arg, "--allow-unsafe-purge") == 0) {
      opt.allow_unsafe_purge = true;
    } else if (strcmp(arg, "--allow-unsafe-restore") == 0) {
      opt.allow_unsafe_restore = true;
    } else if (strcmp(arg, "--dry-run") == 0) {
      opt.dry_run = true;
    } else if (strcmp(arg, "-y") == 0 || strcmp(arg, "--yes") == 0) {
      opt.yes = true;
    } else if (strcmp(arg, "--json") == 0) {
      opt.json = true;
    } else if (strcmp(arg, "--config-dir") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--config-dir 需要参数\n");
        exit(2);
      }
      opt.config_dir = xstrdup(argv[++i]);
    } else if (strcmp(arg, "--backup-dir") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--backup-dir 需要参数\n");
        exit(2);
      }
      opt.backup_dir = xstrdup(argv[++i]);
    } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      print_usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "未知参数: %s\n", arg);
      print_usage(argv[0]);
      exit(2);
    }
  }
  return opt;
}

/* ------------------------- main ------------------------- */

int cc_clean_run(int argc, char **argv) {
  Options opt = parse_args(argc, argv);
  char *home = get_home_dir();
  char *config_home =
      opt.config_dir ? expand_home(opt.config_dir, home) : get_default_config_home(home);
  normalize_separators(config_home);

  if (opt.backup_dir) {
    char *tmp = expand_home(opt.backup_dir, home);
    free(opt.backup_dir);
    opt.backup_dir = tmp;
    normalize_separators(opt.backup_dir);
  }

  if (opt.action_restore) {
    if (!opt.backup_dir) {
      fprintf(stderr, "restore 需要 --backup-dir\n");
      free(home);
      free(config_home);
      free(opt.config_dir);
      return 2;
    }
    if (!opt.dry_run && !opt.yes) {
      char *prompt = str_printf("将从备份目录恢复内容：%s，是否继续？", opt.backup_dir);
      bool ok = confirm_action(prompt);
      free(prompt);
      if (!ok) {
        printf("已取消。\n");
        free(home);
        free(config_home);
        free(opt.config_dir);
        free(opt.backup_dir);
        return 1;
      }
    }
    CleanupResults restore_results;
    int rc = restore_from_backup_dir(opt.backup_dir, opt.dry_run, home,
                                     opt.allow_unsafe_restore, &restore_results);
    if (opt.json) {
      ArtifactList empty1, empty2, empty3;
      artifact_list_init(&empty1);
      artifact_list_init(&empty2);
      artifact_list_init(&empty3);
      print_json_payload(config_home, &empty1, &empty2, &empty3, &restore_results);
      artifact_list_free(&empty1);
      artifact_list_free(&empty2);
      artifact_list_free(&empty3);
    } else {
      print_cleanup_results(&restore_results);
    }
    cleanup_results_free(&restore_results);
    free(home);
    free(config_home);
    free(opt.config_dir);
    free(opt.backup_dir);
    return rc;
  }

  ArtifactList runtime;
  ArtifactList related;
  artifact_list_init(&runtime);
  artifact_list_init(&related);
  collect_artifacts(home, config_home, &runtime, &related);

  if (!opt.action_clean) {
    if (opt.json) {
      print_json_payload(config_home, &runtime, &related, NULL, NULL);
    } else {
      print_report_text(config_home, home, &runtime, &related);
    }
    artifact_list_free(&runtime);
    artifact_list_free(&related);
    free(home);
    free(config_home);
    free(opt.config_dir);
    return 0;
  }

  ArtifactList targets;
  if (opt.purge_config_home && !opt.purge_all &&
      !is_safe_purge_target_path(config_home, home) &&
      !opt.allow_unsafe_purge) {
    fprintf(stderr,
            "拒绝执行 --purge-config-home：目标目录不在允许范围内（仅默认 ~/.claude 允许）。\n"
            "如确认目录可信且确需强制清理，请额外加 --allow-unsafe-purge。\n");
    artifact_list_free(&runtime);
    artifact_list_free(&related);
    free(home);
    free(config_home);
    free(opt.config_dir);
    free(opt.backup_dir);
    return 2;
  }
  build_cleanup_targets(&runtime, &related, opt.purge_all, opt.include_related,
                        opt.purge_config_home, config_home, home, &targets);

  if (!opt.json) {
    print_report_text(config_home, home, &runtime, &related);
    if (!(opt.purge_all && !opt.dry_run)) {
      print_cleanup_plan(&targets, opt.dry_run);
    }
    printf("\n");
  }

  if (targets.len == 0) {
    if (opt.json) {
      CleanupResults empty;
      cleanup_results_init(&empty);
      print_json_payload(config_home, &runtime, &related, &targets, &empty);
      cleanup_results_free(&empty);
    }
    artifact_list_free(&targets);
    artifact_list_free(&runtime);
    artifact_list_free(&related);
    free(home);
    free(config_home);
    free(opt.config_dir);
    return 0;
  }

  if (!opt.dry_run && opt.purge_all) {
    if (!opt.json) {
      printf("== purge-all 强制确认 ==\n");
      print_cleanup_plan(&targets, false);
      printf("\n");
    }
    if (!confirm_purge_all()) {
      if (!opt.json) {
        printf("已取消。\n");
      }
      artifact_list_free(&targets);
      artifact_list_free(&runtime);
      artifact_list_free(&related);
      free(home);
      free(config_home);
      free(opt.config_dir);
      free(opt.backup_dir);
      return 1;
    }
  } else if (!opt.dry_run && !opt.yes) {
    const char *warn = "将执行保守清理，是否继续？";
    if (opt.include_related) {
      warn = "将执行扩展清理（包含用户相关配置），是否继续？";
    }
    if (opt.purge_config_home) {
      warn = "将执行高风险清理（删除整个配置根目录），是否继续？";
    }
    if (!confirm_action(warn)) {
      if (!opt.json) {
        printf("已取消。\n");
      }
      artifact_list_free(&targets);
      artifact_list_free(&runtime);
      artifact_list_free(&related);
      free(home);
      free(config_home);
      free(opt.config_dir);
      return 1;
    }
  }

  BackupContext backup_ctx;
  memset(&backup_ctx, 0, sizeof(backup_ctx));
  if (!opt.dry_run) {
    if (!opt.backup_dir && !opt.json) {
      char buf[PATH_MAX_LEN];
      printf("请输入备份目录（留空表示不备份）: ");
      fflush(stdout);
      if (fgets(buf, sizeof(buf), stdin)) {
        size_t blen = strlen(buf);
        while (blen > 0 && (buf[blen - 1] == '\n' || buf[blen - 1] == '\r')) {
          buf[--blen] = '\0';
        }
        if (blen > 0) {
          opt.backup_dir = expand_home(buf, home);
          normalize_separators(opt.backup_dir);
        }
      }
    }
    if (opt.backup_dir) {
      if (!backup_context_init(&backup_ctx, opt.backup_dir)) {
        fprintf(stderr, "初始化备份目录失败: %s\n", opt.backup_dir);
        artifact_list_free(&targets);
        artifact_list_free(&runtime);
        artifact_list_free(&related);
        free(home);
        free(config_home);
        free(opt.config_dir);
        free(opt.backup_dir);
        return 1;
      }
    }
  }

  CleanupResults results;
  run_cleanup(&targets, opt.dry_run, &results, home, config_home, &backup_ctx);

  if (opt.json) {
    print_json_payload(config_home, &runtime, &related, &targets, &results);
  } else {
    print_cleanup_results(&results);
  }

  backup_context_close(&backup_ctx);
  cleanup_results_free(&results);
  artifact_list_free(&targets);
  artifact_list_free(&runtime);
  artifact_list_free(&related);
  free(home);
  free(config_home);
  free(opt.config_dir);
  free(opt.backup_dir);
  return 0;
}
