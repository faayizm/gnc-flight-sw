// ============================================================================
//  targets/mps2-an500/semihosting.hpp -- asking the debugger (here, QEMU)
//  for things a bare board does not have.
//
//  Semihosting is a convention: the program executes `bkpt 0xAB` with an
//  operation number in r0, and whatever is attached -- a debug probe, or an
//  emulator started with -semihosting-config enable=on -- does it on the
//  program's behalf. Used here for two things only: the command line, so the
//  board takes the same --time-scale option as the laptop, and an exit for
//  runs that should end (--max-ticks).
//
//  With nothing attached, the bkpt is a fault. The board's HardFault handler
//  recognises that case and returns -1 to the caller, so the same image runs
//  on a board without a debugger: no arguments, defaults for everything.
// ============================================================================
#pragma once

#include <cstddef>

namespace semihosting {

// The command line QEMU was given (-semihosting-config arg=...), as one
// string, or false if there is no semihosting host.
bool command_line(char* buf, size_t size);

// End the run. Under QEMU, the emulator exits with `status`.
[[noreturn]] void exit(int status);

}  // namespace semihosting
