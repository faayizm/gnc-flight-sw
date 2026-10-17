// ============================================================================
//  targets/mps2-an500/console.hpp -- text out of UART0, the board's console.
//
//  Polled, blocking, for start-up messages and a status line: nothing on the
//  flight path writes here. Not printf: newlib's printf formats a double by
//  calling malloc, and this program has no heap. So the few numbers printed
//  are integers, and fixed-point by hand.
// ============================================================================
#pragma once

#include <cstdint>

namespace console {

void init();
void put(const char* s);
void put_u(uint32_t v);
// v / 100, with two decimals: put_centi(1250) prints "12.50".
void put_centi(uint32_t v);

}  // namespace console
