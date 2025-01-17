#pragma once

#include <cstdint>

#include "firefly/memory-manager/mm.hpp"
#include "firefly/stivale2.hpp"

namespace firefly::kernel::core::paging {

enum class AccessFlags : int {
    Readonly = 1,
    ReadWrite = 3,
    UserReadOnly = 5,
    UserReadWrite = 7
};

// TODO: Implement me!
enum class CacheMode : int {
    None,
    Uncachable,
    WriteCombine,
    WriteThrough,
    WriteBack
};

void invalidatePage(const VirtualAddress page);
void invalidatePage(const uint64_t page);
void map(const uint64_t virtual_addr, const uint64_t physical_addr, AccessFlags access_flags, const uint64_t *pml_ptr);
void boot_map_extra_region(stivale2_struct_tag_memmap *mmap);
}  // namespace firefly::kernel::core::paging
