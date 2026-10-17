// ============================================================================
//  targets/mps2-an500/console.hpp -- text out of UART0, the board's console.
//
//  Polled, blocking, for start-up messages and a status line: nothing on the
//  flight path writes here. Not printf, not even snprintf: newlib-nano's
//  snprintf refers to malloc (for the formats that grow their buffer), so
//  linking it at all brings in a heap this program does not have. So the few
//  numbers printed are integers, and fixed-point by hand.
// ============================================================================
#pragma once

#include <cstdint>

namespace console {

void init();
void put(const char* s);
void put_u(uint32_t v);
// Right-aligned in a field `width` characters wide.
void put_u(uint32_t v, int width);
// v / 100, with two decimals: put_centi(1250) prints "12.50".
void put_centi(uint32_t v);

}  // namespace console
