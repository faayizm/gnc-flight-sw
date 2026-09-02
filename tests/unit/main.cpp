// ============================================================================
//  tests/unit/main.cpp -- runs every registered test.
// ============================================================================
#include "framework.hpp"

int main() {
    std::printf("HYPERSAT flight software -- unit tests\n");
    return ::testing::run_all();
}
