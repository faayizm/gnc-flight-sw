# =============================================================================
#  Unit tests.
#
#  Deliberately no external test framework. tests/framework.hpp is about a
#  hundred lines and has no dependencies, which means the test suite builds and
#  runs on a machine with no network, inside any container, and on a
#  cross-compiler for a target where GoogleTest would not build at all. For a
#  project whose whole point is portability, a test suite that only runs on a
#  developer laptop would be a poor advertisement.
# =============================================================================

set(FSW_TEST_SOURCES
  unit/test_bytes.cpp
  unit/test_crc.cpp
  unit/test_space_packet.cpp
  unit/test_pus.cpp
  unit/test_scheduler.cpp
  unit/test_param_store.cpp
  unit/test_bus.cpp
  unit/test_containers.cpp
)

add_executable(fsw_tests unit/main.cpp ${FSW_TEST_SOURCES})
target_link_libraries(fsw_tests PRIVATE fsw_apps)
target_include_directories(fsw_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_options(fsw_tests PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME unit COMMAND fsw_tests)
