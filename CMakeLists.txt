# =============================================================================
#  HYPERSAT flight software -- build definition.
#
#  Four targets, and the separation between them is the architecture:
#
#    fsw_core       portable flight core. No OS headers, no allocation after
#                   init, no exceptions, no RTTI. This is what would be
#                   cross-compiled for a flight processor unchanged.
#    fsw_apps       the applications: TT&C now, ADCS/EPS/FDIR later. Same
#                   constraints as the core.
#    fsw_platform   POSIX adapters. The ONLY code permitted to include a
#                   system header. Replacing this directory is what "porting"
#                   means here.
#    fsw            the executable: fsw_core + fsw_apps + fsw_platform + main.
#
#  The strictness on fsw_core and fsw_apps is not decoration. Building them
#  with -fno-exceptions -fno-rtti proves mechanically that no exception or
#  dynamic_cast has crept into the flight path, which is a property that is
#  very easy to lose by accident and very hard to audit by reading.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hypersat_fsw VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(FSW_WERROR       "Treat warnings as errors"                      ON)
option(FSW_STRICT_FLIGHT "Build core and apps without exceptions/RTTI"  ON)
option(FSW_BUILD_TESTS  "Build the unit tests"                          ON)

# Warnings the flight code is expected to compile clean under. -Wconversion is
# included deliberately: silent narrowing is how a 32-bit counter ends up
# truncated into a 16-bit telemetry field without anyone noticing.
set(FSW_WARNINGS
  -Wall -Wextra -Wpedantic
  -Wconversion -Wsign-conversion
  -Wshadow -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-align -Wunused -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion -Wformat=2
)
if(FSW_WERROR)
  list(APPEND FSW_WARNINGS -Werror)
endif()

set(FSW_FLIGHT_FLAGS "")
if(FSW_STRICT_FLIGHT)
  list(APPEND FSW_FLIGHT_FLAGS -fno-exceptions -fno-rtti)
endif()

# -----------------------------------------------------------------------------
# fsw_core -- portable flight core
# -----------------------------------------------------------------------------
add_library(fsw_core STATIC
  fsw/core/scheduler.cpp
  fsw/core/param_store.cpp
)
target_include_directories(fsw_core PUBLIC ${CMAKE_SOURCE_DIR}/fsw)
target_compile_options(fsw_core PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS})

# -----------------------------------------------------------------------------
# fsw_apps -- the applications
# -----------------------------------------------------------------------------
add_library(fsw_apps STATIC
  fsw/apps/ttc/space_packet.cpp
  fsw/apps/ttc/pus.cpp
  fsw/apps/ttc/ttc_app.cpp
)
target_link_libraries(fsw_apps PUBLIC fsw_core)
target_compile_options(fsw_apps PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS})

# -----------------------------------------------------------------------------
# fsw_platform -- POSIX adapters. Not built with the flight flags, because the
# hosted adapters legitimately use parts of the standard library that the
# flight core forgoes.
# -----------------------------------------------------------------------------
add_library(fsw_platform STATIC
  fsw/platform/posix/posix_clock.cpp
  fsw/platform/posix/tcp_server_link.cpp
  fsw/platform/posix/posix_watchdog.cpp
  fsw/platform/posix/posix_file_storage.cpp
)
target_link_libraries(fsw_platform PUBLIC fsw_core)
target_compile_options(fsw_platform PRIVATE ${FSW_WARNINGS})

# -----------------------------------------------------------------------------
# fsw -- the flight software executable
# -----------------------------------------------------------------------------
add_executable(fsw fsw/main.cpp)
target_link_libraries(fsw PRIVATE fsw_apps fsw_platform)
target_compile_options(fsw PRIVATE ${FSW_WARNINGS})

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(FSW_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
