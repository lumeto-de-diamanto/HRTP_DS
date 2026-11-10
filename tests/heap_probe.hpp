#pragma once
#include <cstddef>

// Link-time probes for direct malloc/free calls from the host-linked game
// sources. Not a census of libc internals, C++ new, or DS audio-service heaps.
extern size_t test_heap_bytes, test_heap_count, test_heap_attempts;
extern int test_heap_fail_after;
