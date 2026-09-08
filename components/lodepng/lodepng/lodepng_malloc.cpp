/* The allocators lodepng calls when it is built with
 * LODEPNG_NO_COMPILE_ALLOCATORS, plus an optional chunk tracker for debugging
 * icon-decode memory use. Freestanding on purpose: this used to include
 * <Arduino.h>, which no longer exists on either target. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifndef DEBUG_LODEPNG_MALLOC
#define DEBUG_LODEPNG_MALLOC 0
#endif

#if DEBUG_LODEPNG_MALLOC

#define MEMCHUNK_COUNT 256

static struct
{
    void* ptr;
    size_t size;
} mem_chunks[MEMCHUNK_COUNT];

static bool mem_chunks_initialized = false;

void add_ptr(void* ptr, size_t size)
{
    if (mem_chunks_initialized == false)
    {
        for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
        {
            mem_chunks[i].ptr = NULL;
            mem_chunks[i].size = 0;
        }
        mem_chunks_initialized = true;
    }

    for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
        if (mem_chunks[i].ptr == NULL)
        {
            mem_chunks[i].ptr = ptr;
            mem_chunks[i].size = size;
            printf("add_ptr(%p, %zu)\r\n", ptr, size);
            break;
        }
}

void del_ptr(void* ptr)
{
    for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
    {
        if (mem_chunks[i].ptr == ptr)
        {
            printf("del_ptr(%p) size=%zu\r\n", ptr, mem_chunks[i].size);
            mem_chunks[i].ptr = NULL;
            mem_chunks[i].size = 0;
            break;
        }
    }
}

size_t get_size(void* ptr)
{
    for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
        if (mem_chunks[i].ptr == ptr)
            return mem_chunks[i].size;
    return 0;
}

size_t get_memsize(void)
{
    size_t retval = 0;

    for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
        retval += mem_chunks[i].size;

    return retval;
}

size_t get_chunk_count(void)
{
    size_t retval = 0;

    for (size_t i = 0; i < MEMCHUNK_COUNT; ++i)
    {
        if (mem_chunks[i].ptr != NULL)
            retval++;
    }

    return retval;
}
#endif


void* lodepng_malloc(size_t size)
{
    void* ptr = malloc(size);

#if DEBUG_LODEPNG_MALLOC
    if (ptr != NULL)
    {
        add_ptr(ptr, size);
        printf("lodepng_malloc: Size: %zu Sum: %zu Chunks: %zu\r\n", size, get_memsize(), get_chunk_count());
    }
    else
    {
        printf("lodepng_malloc failed: Size: %zu Sum: %zu Chunks: %zu\r\n", size, get_memsize(), get_chunk_count());
    }
#endif

  return ptr;
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
 #if DEBUG_LODEPNG_MALLOC
    printf("lodepng_realloc: Old: %zu New: %zu Sum: %zu\r\n", get_size(ptr), new_size, get_memsize() - get_size(ptr) + new_size);

    if (ptr != NULL)
    {
        del_ptr(ptr);
    }
#endif

    void* new_ptr = realloc(ptr, new_size);

#if DEBUG_LODEPNG_MALLOC
    if (new_ptr != NULL)
    {
        add_ptr(new_ptr, new_size);
    }
    else
    {
        printf("lodepng_realloc failed\r\n");
    }
#endif

    return new_ptr;
}

void lodepng_free(void* ptr)
{
#if DEBUG_LODEPNG_MALLOC
    printf("lodepng_free: Size: %zu Sum: %zu Chunks: %zu\r\n", get_size(ptr), get_memsize() - get_size(ptr), get_chunk_count());
    del_ptr(ptr);
#endif

    free(ptr);
}
