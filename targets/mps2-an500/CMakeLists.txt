# =============================================================================
#  The board build: HYPERSAT on an Arm MPS2 AN500 (Cortex-M7) under FreeRTOS.
#
#    cmake -S targets/mps2-an500 -B build-target \
#          -DCMAKE_TOOLCHAIN_FILE=targets/mps2-an500/toolchain-arm-none-eabi.cmake
#    cmake --build build-target          (or just: make target)
#
#  The same fsw_core and fsw_apps sources as the laptop build, from the same
#  list (cmake/fsw_sources.cmake), with the same warnings and the same
#  -fno-exceptions -fno-rtti. What differs is only underneath: the ports in
#  fsw/platform/freertos/, this directory's startup code, and FreeRTOS.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hypersat_mps2_an500 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(FSW_WERROR        "Treat warnings as errors"                     ON)
option(FSW_STRICT_FLIGHT "Build core and apps without exceptions/RTTI"  ON)

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(BOARD "${CMAKE_CURRENT_SOURCE_DIR}")
include(${REPO}/cmake/fsw_sources.cmake)
list(TRANSFORM FSW_CORE_SOURCES PREPEND "${REPO}/")
list(TRANSFORM FSW_APPS_SOURCES PREPEND "${REPO}/")

# Small code, and a stack-usage file (.su) next to every object for the
# budget report (tools/budget.py). No thread-safe statics: there is one task,
# and the guard would call into a C++ runtime this program does not have.
set(BOARD_CXX_FLAGS -fstack-usage -fno-threadsafe-statics)

# -----------------------------------------------------------------------------
# FreeRTOS, fetched at a pinned release rather than copied into the tree.
# -----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(freertos_kernel
  GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
  GIT_TAG        V11.1.0
  GIT_SHALLOW    TRUE)
FetchContent_GetProperties(freertos_kernel)
if(NOT freertos_kernel_POPULATED)
  FetchContent_Populate(freertos_kernel)
endif()
set(RTOS ${freertos_kernel_SOURCE_DIR})

# Only the scheduler itself: no queues, timers, event groups or heap.
add_library(freertos STATIC
  ${RTOS}/tasks.c
  ${RTOS}/list.c
  ${RTOS}/portable/GCC/ARM_CM4F/port.c)
target_include_directories(freertos PUBLIC
  ${BOARD} ${RTOS}/include ${RTOS}/portable/GCC/ARM_CM4F)
target_compile_options(freertos PRIVATE -fstack-usage)

# -----------------------------------------------------------------------------
# The flight code, exactly as on the laptop.
# -----------------------------------------------------------------------------
add_library(fsw_core STATIC ${FSW_CORE_SOURCES})
target_include_directories(fsw_core PUBLIC ${REPO}/fsw)
target_compile_options(fsw_core PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS} ${BOARD_CXX_FLAGS})

add_library(fsw_apps STATIC ${FSW_APPS_SOURCES})
target_link_libraries(fsw_apps PUBLIC fsw_core)
target_compile_options(fsw_apps PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS} ${BOARD_CXX_FLAGS})

# -----------------------------------------------------------------------------
# The board's ports. Flight rules apply here too: this is code that flies.
# -----------------------------------------------------------------------------
add_library(fsw_platform STATIC
  ${REPO}/fsw/platform/freertos/freertos_clock.cpp
  ${REPO}/fsw/platform/freertos/cmsdk_uart_link.cpp
  ${REPO}/fsw/platform/freertos/cmsdk_watchdog.cpp)
target_link_libraries(fsw_platform PUBLIC fsw_core freertos)
target_compile_options(fsw_platform PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS} ${BOARD_CXX_FLAGS})

# -----------------------------------------------------------------------------
# fsw.elf
# -----------------------------------------------------------------------------
add_executable(fsw.elf
  ${BOARD}/startup.cpp
  ${BOARD}/main.cpp
  ${BOARD}/console.cpp
  ${BOARD}/semihosting.cpp)
target_link_libraries(fsw.elf PRIVATE fsw_apps fsw_platform freertos)
target_compile_options(fsw.elf PRIVATE ${FSW_WARNINGS} ${FSW_FLIGHT_FLAGS} ${BOARD_CXX_FLAGS})
# No C runtime start files (startup.cpp is ours), newlib-nano for memcpy and
# the maths library, and no _sbrk anywhere: anything that wants malloc fails
# to link.
target_link_options(fsw.elf PRIVATE
  -T${BOARD}/mps2_an500.ld
  -nostartfiles --specs=nano.specs
  -Wl,-Map=${CMAKE_BINARY_DIR}/fsw.map
  -Wl,--print-memory-usage)
set_target_properties(fsw.elf PROPERTIES LINK_DEPENDS ${BOARD}/mps2_an500.ld)
