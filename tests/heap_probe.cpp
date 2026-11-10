#include "heap_probe.hpp"
#include <cassert>

size_t test_heap_bytes, test_heap_count, test_heap_attempts;
int test_heap_fail_after = -1;
namespace {
struct Block { void *pointer; size_t bytes; };
Block blocks[256];
}
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t bytes)
{
    ++test_heap_attempts;
    if (test_heap_fail_after == 0) return nullptr;
    if (test_heap_fail_after > 0) --test_heap_fail_after;
    void *pointer = __real_malloc(bytes);
    if (!pointer) return nullptr;
    for (auto &block : blocks) if (!block.pointer) {
        block = {pointer, bytes};
        test_heap_bytes += bytes; ++test_heap_count;
        return pointer;
    }
    assert(false && "heap probe capacity exceeded");
    return nullptr;
}
extern "C" void __wrap_free(void *pointer)
{
    if (!pointer) return;
    for (auto &block : blocks) if (block.pointer == pointer) {
        test_heap_bytes -= block.bytes; --test_heap_count;
        block = {};
        __real_free(pointer);
        return;
    }
    assert(false && "untracked/double free");
}
