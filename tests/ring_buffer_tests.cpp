#include <iostream>

#include "marketdata/SpscRing.h"

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "ring_buffer_tests failed: " << message << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    marketdata::SpscRing<int> ring(3);
    int value = 0;

    if (!check(ring.empty(), "new ring should be empty")) return 1;
    if (!check(!ring.pop(value), "empty pop should fail")) return 1;
    if (!check(ring.push(1), "push 1 failed")) return 1;
    if (!check(ring.push(2), "push 2 failed")) return 1;
    if (!check(ring.push(3), "push 3 failed")) return 1;
    if (!check(!ring.push(4), "full ring push should fail")) return 1;

    if (!check(ring.pop(value), "pop 1 failed")) return 1;
    if (!check(value == 1, "expected 1")) return 1;
    if (!check(ring.push(4), "wraparound push failed")) return 1;

    if (!check(ring.pop(value), "pop 2 failed")) return 1;
    if (!check(value == 2, "expected 2")) return 1;
    if (!check(ring.pop(value), "pop 3 failed")) return 1;
    if (!check(value == 3, "expected 3")) return 1;
    if (!check(ring.pop(value), "pop 4 failed")) return 1;
    if (!check(value == 4, "expected 4")) return 1;
    if (!check(!ring.pop(value), "empty pop after drain should fail")) return 1;
    if (!check(ring.empty(), "drained ring should be empty")) return 1;

    std::cout << "ring_buffer_tests passed\n";
    return 0;
}
