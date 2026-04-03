cmake_minimum_required(VERSION 3.16)
project(cc_clean C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

function(cfg_set_test_meta test_name labels timeout)
  set(extra_props ${ARGN})
  set_tests_properties(${test_name} PROPERTIES
    LABELS "${labels}"
    TIMEOUT ${timeout}
    ${extra_props}
  )
endfunction()

add_executable(cc_clean
  csrc/cc_clean.c
)
set_target_properties(cc_clean PROPERTIES OUTPUT_NAME "cc-clean")

if(MSVC)
  target_compile_options(cc_clean PRIVATE /W4)
else()
  target_compile_options(cc_clean PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_link_libraries(cc_clean PRIVATE Advapi32)
endif()

if(BUILD_TESTING)
  if(WIN32)
    add_test(
      NAME cfg_help_flags
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-HelpFlags
    )
    cfg_set_test_meta(cfg_help_flags "cli;safety" 15)
    add_test(
      NAME cfg_no_system_call
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-NoSystemCall
    )
    cfg_set_test_meta(cfg_no_system_call "source;safety" 15)
    add_test(
      NAME cfg_purge_guard
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-PurgeGuard
    )
    cfg_set_test_meta(cfg_purge_guard "cleanup;safety;dangerous" 30 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_backup_restore_flow
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-BackupAndRestoreFlow
    )
    cfg_set_test_meta(cfg_backup_restore_flow "backup;restore;filesystem;manifest" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_restore_json
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-RestoreJsonOutput
    )
    cfg_set_test_meta(cfg_restore_json "json;restore;manifest" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_json_include_related
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-JsonOutputAndIncludeRelated
    )
    cfg_set_test_meta(cfg_json_include_related "json;cleanup" 30 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_strict_restore
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-StrictRestoreRemovesExtras
    )
    cfg_set_test_meta(cfg_strict_restore "restore;filesystem;dangerous" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_allow_unsafe_purge
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-AllowUnsafePurgeOnTempDir
    )
    cfg_set_test_meta(cfg_allow_unsafe_purge "cleanup;unsafe;dangerous" 30 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_windows_registry_credential
      COMMAND powershell -ExecutionPolicy Bypass -File
              ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_windows.ps1
              -Tests Test-WindowsRegistryAndCredentialRoundTrip
    )
    cfg_set_test_meta(cfg_windows_registry_credential
      "windows;windows-only;registry;credential;backup;restore;dangerous;manifest"
      90
      RESOURCE_LOCK "fg_windows_state")
  else()
    add_test(
      NAME cfg_help_flags
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_help_flags
    )
    cfg_set_test_meta(cfg_help_flags "cli;safety" 15)
    add_test(
      NAME cfg_no_system_call
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_no_system_call
    )
    cfg_set_test_meta(cfg_no_system_call "source;safety" 15)
    add_test(
      NAME cfg_purge_guard
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_purge_guard
    )
    cfg_set_test_meta(cfg_purge_guard "cleanup;safety;dangerous" 30 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_backup_restore_flow
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_backup_and_restore_flow
    )
    cfg_set_test_meta(cfg_backup_restore_flow "backup;restore;filesystem;manifest" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_restore_json
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_restore_json_output
    )
    cfg_set_test_meta(cfg_restore_json "json;restore;manifest" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_json_include_related
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_json_output_and_include_related
    )
    cfg_set_test_meta(cfg_json_include_related "json;cleanup" 30 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_strict_restore
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_strict_restore_removes_extras
    )
    cfg_set_test_meta(cfg_strict_restore "restore;filesystem;dangerous" 45 RESOURCE_LOCK "fg_fs_state")
    add_test(
      NAME cfg_allow_unsafe_purge
      COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_posix.sh
              test_allow_unsafe_purge_on_temp_dir
    )
    cfg_set_test_meta(cfg_allow_unsafe_purge "cleanup;unsafe;dangerous" 30 RESOURCE_LOCK "fg_fs_state")

    if(DEFINED ENV{FG_ENABLE_DOCKER_E2E})
      add_test(
        NAME cfg_container_default_clean
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_container_e2e.sh
                test_container_default_clean
      )
      cfg_set_test_meta(cfg_container_default_clean "e2e;docker;cleanup" 300 RESOURCE_LOCK "fg_docker_state")

      add_test(
        NAME cfg_container_include_related
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_container_e2e.sh
                test_container_include_related
      )
      cfg_set_test_meta(cfg_container_include_related "e2e;docker;cleanup;include-related" 300 RESOURCE_LOCK "fg_docker_state")

      add_test(
        NAME cfg_container_include_installation
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_container_e2e.sh
                test_container_include_installation
      )
      cfg_set_test_meta(cfg_container_include_installation "e2e;docker;cleanup;installation" 300 RESOURCE_LOCK "fg_docker_state")

      add_test(
        NAME cfg_container_purge_all
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/test_cc_clean_container_e2e.sh
                test_container_purge_all
      )
      cfg_set_test_meta(cfg_container_purge_all "e2e;docker;cleanup;purge-all" 300 RESOURCE_LOCK "fg_docker_state")
    endif()
  endif()
endif()
