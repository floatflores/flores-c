#ifndef FLR_MEM_H
#define FLR_MEM_H

#include "flr_base.h"

// clang-format off
#if !defined(FLR_MEM_API)
    #if defined(FLR_MEM_STATIC)
        #define FLR_MEM_API static
    #elif defined(_WIN32)
        #if defined(FLR_MEM_BUILD_DLL)
            #define FLR_MEM_API __declspec(dllexport)
        #elif defined(FLR_MEM_USE_DLL)
            #define FLR_MEM_API __declspec(dllimport)
        #else
            #define FLR_MEM_API extern
        #endif
    #else
        #define FLR_MEM_API extern
    #endif
#endif
// clang-format on

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

typedef struct flr_mem_arena      flr_mem_arena;
typedef struct flr_mem_arena_temp flr_mem_arena_temp;

#define FLR_ARENA_BASE_POS (sizeof(flr_mem_arena))
#define FLR_ARENA_ALIGN (sizeof(void*))

struct flr_mem_arena
{
    flr_u64 reserve_size;
    flr_u64 commit_size;

    flr_u64 pos;
    flr_u64 commit_pos;
};

struct flr_mem_arena_temp
{
    flr_mem_arena* arena;
    flr_u64        start_pos;
};

FLR_MEM_API flr_mem_arena* flr_arena_create(flr_u64 reserve_size, flr_u64 commit_size);
FLR_MEM_API void           flr_arena_destroy(flr_mem_arena* arena);
FLR_MEM_API void*          flr_arena_push(flr_mem_arena* arena, flr_u64 size, flr_b32 non_zero);
FLR_MEM_API void           flr_arena_pop(flr_mem_arena* arena, flr_u64 size);
FLR_MEM_API void           flr_arena_pop_to(flr_mem_arena* arena, flr_u64 pos);
FLR_MEM_API void           flr_arena_clear(flr_mem_arena* arena);

FLR_MEM_API flr_mem_arena_temp flr_arena_temp_begin(flr_mem_arena* arena);
FLR_MEM_API void               flr_arena_temp_end(flr_mem_arena_temp temp);

static FLR_THREAD_LOCAL flr_mem_arena* scratch_arena_[2] = { NULL, NULL };
FLR_MEM_API flr_mem_arena_temp         flr_arena_scratch_get(flr_mem_arena** conflicts,
                                                             flr_u32         num_conflicts);
FLR_MEM_API void                       flr_arena_scratch_release(flr_mem_arena_temp scratch);

FLR_MEM_API flr_u32 flr_plat_get_pagesize(void);
FLR_MEM_API void*   flr_plat_mem_reserve(flr_u64 size);
FLR_MEM_API flr_b32 flr_plat_mem_commit(void* ptr, flr_u64 size);
FLR_MEM_API flr_b32 flr_plat_mem_decommit(void* ptr, flr_u64 size);
FLR_MEM_API flr_b32 flr_plat_mem_release(void* ptr, flr_u64 size);

#define FLR_PUSH_STRUCT_ARENA(arena, T) (T*)flr_arena_push((arena), sizeof(T), false)
#define FLR_PUSH_STRUCT_ARENA_NZ(arena, T) (T*)flr_arena_push((arena), sizeof(T), true)
#define FLR_PUSH_ARRAY_ARENA(arena, T, n) (T*)flr_arena_push((arena), sizeof(T) * (n), false)
#define FLR_PUSH_ARRAY_ARENA_NZ(arena, T, n) (T*)flr_arena_push((arena), sizeof(T) * (n), true)

typedef struct flr_mem_pool      flr_mem_pool;
typedef struct flr_mem_pool_node flr_mem_pool_node;

struct flr_mem_pool_node
{
    flr_mem_pool_node* next;
};

struct flr_mem_pool
{
    flr_mem_arena*     arena;
    flr_mem_pool_node* first_free;
    flr_u64            chunk_size;
    flr_u32            aligment;
};

FLR_MEM_API flr_mem_pool* flr_mem_pool_init(flr_mem_arena* arena, flr_u64 chunk_size,
                                            flr_u32 alignment);
FLR_MEM_API void*         flr_mem_pool_alloc(flr_mem_pool* pool);
FLR_MEM_API void          flr_mem_pool_free(flr_mem_pool* pool, void* ptr);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // !FLR_MEM_H

#define FLR_ARENA_IMPLEMENTATION
#ifdef FLR_ARENA_IMPLEMENTATION

FLR_MEM_API flr_mem_arena*
flr_arena_create(flr_u64 reserve_size, flr_u64 commit_size)
{
    flr_u32 page_size = flr_plat_get_pagesize();

    reserve_size = FLR_ALIGN_UP_POW2(reserve_size, page_size);
    commit_size  = FLR_ALIGN_UP_POW2(commit_size, page_size);

    flr_mem_arena* arena = flr_plat_mem_reserve(reserve_size);

    if (arena == NULL) { return NULL; }
    if (!flr_plat_mem_commit(arena, commit_size))
    {
        flr_plat_mem_release(arena, reserve_size);
        return NULL;
    }

    arena->reserve_size = reserve_size;
    arena->commit_size  = commit_size;
    arena->pos          = FLR_ARENA_BASE_POS;
    arena->commit_pos   = commit_size;

    return arena;
}

FLR_MEM_API void
flr_arena_destroy(flr_mem_arena* arena)
{
    flr_plat_mem_release(arena, arena->reserve_size);
}

FLR_MEM_API void*
flr_arena_push(flr_mem_arena* arena, flr_u64 size, flr_b32 non_zero)
{
    flr_u64 pos_aligned = FLR_ALIGN_UP_POW2(arena->pos, FLR_ARENA_ALIGN);
    flr_u64 new_pos     = pos_aligned + size;

    if (new_pos > arena->reserve_size) { return NULL; }

    if (new_pos > arena->commit_pos)
    {
        flr_u64 new_commit_pos = new_pos;
        new_commit_pos += arena->commit_size - 1;
        new_commit_pos -= new_commit_pos % arena->commit_size;
        new_commit_pos = FLR_MIN(new_commit_pos, arena->reserve_size);

        flr_u8* mem         = (flr_u8*)arena + arena->commit_pos;
        flr_u64 commit_size = new_commit_pos - arena->commit_pos;

        if (!flr_plat_mem_commit(mem, commit_size)) { return NULL; }

        arena->commit_pos = new_commit_pos;
    }

    arena->pos = new_pos;

    flr_u8* out = (flr_u8*)arena + pos_aligned;

    if (!non_zero) { memset(out, 0, size); }

    return out;
}

FLR_MEM_API void
flr_arena_pop(flr_mem_arena* arena, flr_u64 size)
{
    size = FLR_MIN(size, arena->pos - FLR_ARENA_BASE_POS);
    arena->pos -= size;
}

FLR_MEM_API void
flr_arena_pop_to(flr_mem_arena* arena, flr_u64 pos)
{
    flr_u64 size = pos < arena->pos ? arena->pos - pos : 0;
    flr_arena_pop(arena, size);
}

FLR_MEM_API void
flr_arena_clear(flr_mem_arena* arena)
{
    flr_arena_pop_to(arena, FLR_ARENA_BASE_POS);
}

FLR_MEM_API flr_mem_arena_temp
flr_arena_temp_begin(flr_mem_arena* arena)
{
    return (flr_mem_arena_temp){ .arena = arena, .start_pos = arena->pos };
}

FLR_MEM_API void
flr_arena_temp_end(flr_mem_arena_temp temp)
{
    flr_arena_pop_to(temp.arena, temp.start_pos);
}

FLR_MEM_API flr_mem_arena_temp
flr_arena_scratch_get(flr_mem_arena** conflicts, flr_u32 num_conflicts)
{
    flr_i32 scratch_index = -1;

    for (flr_i32 i = 0; i < 2; i++)
    {
        flr_b32 conflict_found = false;
        for (flr_u32 j = 0; j < num_conflicts; j++)
        {
            if (scratch_arena_[i] == conflicts[j])
            {
                conflict_found = true;
                break;
            }
        }

        if (!conflict_found)
        {
            scratch_index = i;
            break;
        }
    }

    if (scratch_index == -1) { return (flr_mem_arena_temp){ 0 }; }

    flr_mem_arena** selected = &scratch_arena_[scratch_index];

    if (*selected == NULL) { *selected = flr_arena_create(FLR_MiB(64), FLR_MiB(1)); }

    return flr_arena_temp_begin(*selected);
}

FLR_MEM_API void
flr_arena_scratch_release(flr_mem_arena_temp scratch)
{
    flr_arena_temp_end(scratch);
}

#ifdef _WIN32

#include <windows.h>

FLR_MEM_API flr_u32
flr_plat_get_pagesize(void)
{
    SYSTEM_INFO sysinfo = { 0 };
    GetSystemInfo(&sysinfo);

    return sysinfo.dwPageSize;
}

FLR_MEM_API void*
flr_plat_mem_reserve(flr_u64 size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
}

FLR_MEM_API flr_b32
flr_plat_mem_commit(void* ptr, flr_u64 size)
{
    void* ret = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    return ret != NULL;
}

FLR_MEM_API flr_b32
flr_plat_mem_decommit(void* ptr, flr_u64 size)
{
    return VirtualFree(ptr, size, MEM_DECOMMIT);
}

FLR_MEM_API flr_b32
flr_plat_mem_release(void* ptr, flr_u64 size)
{
    return VirtualFree(ptr, size, MEM_RELEASE);
}

#endif // _WIN32

#ifdef __linux__

#include <sys/mman.h>
#include <unistd.h>

FLR_MEM_API flr_u32
flr_plat_get_pagesize(void)
{
    return sysconf(_SC_PAGESIZE);
}

FLR_MEM_API void*
flr_plat_mem_reserve(flr_u64 size)
{
    void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
}

FLR_MEM_API flr_b32
flr_plat_mem_commit(void* ptr, flr_u64 size)
{
    int result = mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return result == 0;
}

FLR_MEM_API flr_b32
flr_plat_mem_decommit(void* ptr, flr_u64 size)
{
    int result = madvise(ptr, size, MADV_DONTNEED);
    return result == 0;
}

FLR_MEM_API flr_b32
flr_plat_mem_release(void* ptr, flr_u64 size)
{
    int result = munmap(ptr, size);
    return result == 0;
}

#endif // __linux__

FLR_MEM_API flr_mem_pool*
flr_mem_pool_init(flr_mem_arena* arena, flr_u64 chunk_size, flr_u32 alignment)
{
    flr_u64 actual_chunk_size = FLR_MAX(chunk_size, sizeof(flr_mem_pool_node));
    actual_chunk_size         = FLR_ALIGN_UP_POW2(actual_chunk_size, alignment);

    flr_mem_pool* pool = FLR_PUSH_STRUCT_ARENA(arena, flr_mem_pool);
    if (!pool) { return NULL; }

    pool->arena      = arena;
    pool->aligment   = alignment;
    pool->chunk_size = actual_chunk_size;
    pool->first_free = NULL;

    return pool;
}

FLR_MEM_API void*
flr_mem_pool_alloc(flr_mem_pool* pool)
{
    if (pool->first_free != NULL)
    {
        void* result     = pool->first_free;
        pool->first_free = pool->first_free->next;
        return result;
    }

    return flr_arena_push(pool->arena, pool->chunk_size, false);
}

FLR_MEM_API void
flr_mem_pool_free(flr_mem_pool* pool, void* ptr)
{
    if (ptr == NULL) { return; }

    flr_mem_pool_node* node = (flr_mem_pool_node*)ptr;
    node->next              = pool->first_free;
    pool->first_free        = node;
}

#endif // FLR_ARENA_IMPLEMENTATION
