#include "x86_64/memory-manager/primary/primary_phys.hpp"

#include <stl/cstdlib/stdio.h>

#include "x86_64/libk++/align.h"
#include "x86_64/trace/strace.hpp"
#include "x86_64/scheduler/spinlock.hpp"

namespace firefly::kernel::mm::primary {
static libkern::Bitmap bitmap;
static uint32_t *arena;
static int64_t allocation_base;  //Base address for the linked list structure (Should never be freed, may be reused)
static size_t allocation_index = 0;
using libkern::align4k;


static void *early_alloc(struct stivale2_mmap_entry &entry, int size) {
    size_t ret = entry.base;
    entry.base += size;
    entry.length -= size;
    return reinterpret_cast<void *>(ret);
}

template <typename T>
T *small_alloc() {
    T *curr = reinterpret_cast<T *>(allocation_base + allocation_index);
    allocation_index += sizeof(T);
    *curr = T{};
    return curr;
}

void init(struct stivale2_struct_tag_memmap *mmap) {
    bool init_ok = false;

    // The highest possible *free* entry in the mmap
    uint64_t highest_page = 0;
    uint64_t top = 0;
    for (size_t i = 0; i < mmap->entries; i++) {
        if (mmap->memmap[i].type != STIVALE2_MMAP_USABLE)
            continue;

        top = mmap->memmap[i].base + mmap->memmap[i].length - 1;
        if (top > highest_page)
            highest_page = top;
    }

    // // DEBUG: Print mmap contents
    // for (size_t i = 0; i < mmap->entries; i++) {
    //     printf("(%d) %X-%X [ %X (%s) ]\n", i, mmap->memmap[i].base, mmap->memmap[i].base + mmap->memmap[i].length - 1, mmap->memmap[i].type, mmap->memmap[i].type == 1 ? "free" : "?");
    // }

    size_t bitmap_size = (highest_page / PAGE_SIZE / 8);
    align4k<size_t>(bitmap_size);

    // Iterate through mmap and find largest block to store the bitmap
    for (size_t i = 0; i < mmap->entries; i++) {
        if (mmap->memmap[i].type != STIVALE2_MMAP_USABLE)
            continue;

        if (mmap->memmap[i].length >= bitmap_size) {
            printf("Found entry to store the bitmap (%d bytes) at %X-%X\n", bitmap_size, mmap->memmap[i].base, mmap->memmap[i].base + mmap->memmap[i].length - 1);

            // Note: The entire memory contents are marked as used now, we free available memory after this
            arena = reinterpret_cast<uint32_t *>(early_alloc(mmap->memmap[i], bitmap_size));
            bitmap.init(arena, bitmap_size);
            bitmap.setall();
            init_ok = true;

            // Note: There is no need to resize this entry since `early_alloc` already did.
            break;
        }
    }
    if (!init_ok) {
        trace::panic("Failed to initialize the primary allocation structure");
    }

    for (size_t i = 0; i < mmap->entries; i++) {
        if (mmap->memmap[i].type != STIVALE2_MMAP_USABLE)
            continue;

        size_t base = bitmap.allocator_conversion(false, mmap->memmap[i].base);
        size_t end = bitmap.allocator_conversion(false, mmap->memmap[i].length);
        printf("Freeing %d pages at %X\n", end, bitmap.allocator_conversion(true, base));

        for (size_t i = base; i < base + end; i++) {
            auto success = bitmap.clear(i).success;
            if (!success) {
                trace::panic("Failed to free page during primary allocator setup");
            }
        }
    }

    // Setup the base address for the linked list.
    allocation_base = bitmap.find_first(libkern::BIT_SET);
    if (allocation_base == -1)
        trace::panic("No free memory");

    if (!bitmap.set(allocation_base).success)
        trace::panic("Failed to mark the primary allocators linked list as used!");
    
    allocation_base = bitmap.allocator_conversion(true, allocation_base);

    //NOTE: Never free bit 0 - It's a nullptr
}


/*
    Primary physical allocator.
    - Allocates up to 4095 pages of physical memory
    - All addresses returned are page aligned
    - All pages are initialized to zero
    - Panics on failure
    
    Returns a struct with an array of void pointers
    to account for non contiguous memory.
*/
primary_res_t *allocate(size_t pages) {
    // NOTE: There is no need to check for a single page to try and optimize
    // the allocation since this type of allocation would violate the core
    // prinicple of the kernel. (Each process has it's own allocator)
    
    primary_res_t *res = small_alloc<primary_res_t>();

    if (pages > PAGE_SIZE)
        return nullptr;

    for (size_t i = 0; i < pages; i++, res->count++) {
        auto bit = bitmap.find_first(libkern::BIT_SET);
        if (bit == -1)
            trace::panic("No free memory!");

        bitmap.set(bit);
        res->data[i] = reinterpret_cast<void*>(bitmap.allocator_conversion(true, bit));
    }

    allocation_index = 0;  //Reset linear allocator for next phys allocation

    // This isn't thread safe AT ALL btw, keep that in mind for future needs of a primary realloc()
    // We might need a special, thread safe realloc function if kernel service allocators need more
    // memory all of a sudden.
    return res;
}

static lock_t daemon_alloc_lock;
// A (Somewhat) MP safe version of allocate() for after-init allocations made by daemons
primary_res_t *daemon_late_alloc(size_t pages)
{
    acquire_lock(&daemon_alloc_lock);
    auto res = allocate(pages);
    release_lock(&daemon_alloc_lock);
    return res;
}

void deallocate(primary_res_t *allocation_structure)
{
    for (size_t i = 0; i < allocation_structure->count; i++)
    {
        auto status = bitmap.clear(
            static_cast<uint32_t>(
              bitmap.allocator_conversion(false, reinterpret_cast<size_t>(allocation_structure->data[i]))
            )
        );
        
        // TODO TODO TODO: panic needs to have variadic arguments
        if (!status.success)
            trace::panic("Could not free physical page at %X\n(This address is likely invalid)\n");
    }
}

}  // namespace firefly::kernel::mm::primary