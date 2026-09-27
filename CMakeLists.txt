cmake_minimum_required(VERSION 3.16)
project(kdiag CXX)

find_package(CURL REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# ---- core library (everything except main.cpp) ----------------------------
add_library(kdiag_core
  src/event.cpp
  src/collection_file.cpp
  src/exec.cpp
  src/kernel_line_classifier.cpp
  src/stage.cpp
  src/correlator.cpp
  src/diagnoser.cpp
  src/diagnosis_rules.cpp
  src/collect_command.cpp
  src/correlate_command.cpp
  src/diagnose_command.cpp
  src/explain_command.cpp
  src/evaluate_command.cpp
  src/ui_renderer.cpp
  src/evaluator.cpp
  src/llm_explainer.cpp
  src/collectors/dmesg_collector.cpp
  src/collectors/interrupts_collector.cpp
  src/collectors/pci_collector.cpp
  src/collectors/v4l2_sysfs_collector.cpp
  src/collectors/journalctl_collector.cpp
)
target_include_directories(kdiag_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)
target_link_libraries(kdiag_core PUBLIC CURL::libcurl)

# ---- CLI binary -------------------------------------------------------------
add_executable(kdiag src/main.cpp)
target_link_libraries(kdiag PRIVATE kdiag_core)

# ---- tests ------------------------------------------------------------------
option(KDIAG_BUILD_TESTS "Build unit tests (requires GTest)" ON)
if(KDIAG_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()

  add_executable(kdiag_tests
    tests/event_test.cpp
    tests/collection_file_test.cpp
    tests/kernel_line_classifier_test.cpp
    tests/dmesg_collector_test.cpp
    tests/interrupts_collector_test.cpp
    tests/pci_collector_test.cpp
    tests/v4l2_sysfs_collector_test.cpp
    tests/journalctl_collector_test.cpp
    tests/stage_test.cpp
    tests/correlator_test.cpp
    tests/diagnoser_test.cpp
    tests/diagnosis_rules_test.cpp
    tests/ui_renderer_test.cpp
    tests/evaluation_scenarios.cpp
    tests/llm_explainer_test.cpp
    tests/exec_test.cpp
  )
  target_link_libraries(kdiag_tests PRIVATE kdiag_core GTest::gtest GTest::gtest_main)
  target_include_directories(kdiag_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)

  include(GoogleTest)
  gtest_discover_tests(kdiag_tests)
endif()
