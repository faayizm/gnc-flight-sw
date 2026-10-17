// ============================================================================
//  targets/mps2-an500/console.cpp
// ============================================================================
#include "console.hpp"

#include "board.hpp"
#include "platform/freertos/cortex_m.hpp"

namespace console {

namespace {

namespace cm = fsw::platform::cm;

void put_char(char c) {
    while ((cm::reg<uint32_t>(board::kUart0 + 0x04) & 1u) != 0) {}   // TX full
    cm::reg<uint32_t>(board::kUart0 + 0x00) = static_cast<uint8_t>(c);
}

}  // namespace

void init() {
    cm::reg<uint32_t>(board::kUart0 + 0x10) = board::kUartBaudDiv;
    cm::reg<uint32_t>(board::kUart0 + 0x08) = 1u;   // TX enable, no interrupts
}

void put(const char* s) {
    for (; *s != '\0'; ++s) {
        if (*s == '\n') { put_char('\r'); }
        put_char(*s);
    }
}

void put_u(uint32_t v) {
    char buf[11];
    int  n = 0;
    do {
        buf[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0) { put_char(buf[--n]); }
}

void put_centi(uint32_t v) {
    put_u(v / 100);
    put_char('.');
    put_char(static_cast<char>('0' + (v / 10) % 10));
    put_char(static_cast<char>('0' + v % 10));
}

}  // namespace console
