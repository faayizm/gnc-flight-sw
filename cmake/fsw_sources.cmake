# =============================================================================
#  The flight code's source lists and compiler flags, shared by every build:
#  the laptop (CMakeLists.txt) and the board (targets/mps2-an500/). One list,
#  so the board can never quietly fly a different set of files.
# =============================================================================

set(FSW_CORE_SOURCES
  fsw/core/scheduler.cpp
  fsw/core/param_store.cpp
)

set(FSW_APPS_SOURCES
  fsw/apps/ttc/space_packet.cpp
  fsw/apps/ttc/pus.cpp
  fsw/apps/ttc/ttc_app.cpp
  fsw/apps/ttc/channel_coding.cpp
  fsw/apps/ttc/tm_framer.cpp
  fsw/apps/ttc/tc_receiver.cpp
  fsw/apps/io/sim_bridge.cpp
  fsw/apps/io/sim_io_app.cpp
  fsw/apps/eps/eps_app.cpp
  fsw/apps/modemgr/mode_manager.cpp
  fsw/apps/adcs/adcs_app.cpp
  fsw/apps/adcs/ephemeris.cpp
  fsw/apps/adcs/mekf.cpp
  fsw/apps/adcs/orbit_prop.cpp
  fsw/apps/fdir/fdir_app.cpp
  fsw/spacecraft.cpp
)

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
