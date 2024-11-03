#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct _free_chunk {

	size_t prev_size; // only if prev is free
	size_t chunk_size;
	struct _free_chunk *fwd_ptr; 
	struct _free_chunk **bin_ptr;

} __attribute__((packed)) free_chunk, *p_free_chunk;

typedef struct {

	size_t prev_size; // only if prev is free
	size_t chunk_size;
	char data[];

} used_chunk, *p_used_chunk;

typedef struct {

	size_t prev_size; // only if prev is free
	size_t chunk_size;

} chunk_metadata, *p_chunk_metadata;

#define FAST_BIN_AMT 10
#define FAST_BIN_CONSOLIDATION_THRESHOLD 0x10000 // 64kb threshold

#define SMALL_BIN_AMT 62
#define LARGE_BIN_AMT 63

typedef struct {

	void *start;
	size_t top;
	size_t size;

	p_free_chunk unsorted_bin;
	p_free_chunk fast_bins[FAST_BIN_AMT];
	p_free_chunk small_bins[SMALL_BIN_AMT];
	p_free_chunk large_bins[LARGE_BIN_AMT];

} heap, *p_heap;

#define PREV_IN_USE_BIT 1
#define MMAPPED_BIT 2
#define SECONDARY_ARENA_BIT 4
#define CHUNK_CONSOLIDATED_BIT 8
#define CHUNK_HEADER_SIZE sizeof(size_t)
#define CHUNK_METADATA_SIZE CHUNK_HEADER_SIZE*2
#define CHUNK_ALIGN 0x10
#define MIN_SIZE 0x10//CHUNK_ALIGN*2

#define CHUNK_HEADER(chunk) ((chunk-CHUNK_HEADER_SIZE))
#define IS_PREV_IN_USE(chunk) ((*(size_t *)CHUNK_HEADER(chunk)) & (PREV_IN_USE_BIT))
#define IS_MMAPPED(chunk) ((*(size_t *)CHUNK_HEADER(chunk)) & (MMAPPED_BIT))
#define IS_FROM_SECONDARY_ARENA(chunk) ((*(size_t *)CHUNK_HEADER(chunk)) & (SECONDARY_ARENA_BIT))
#define IS_CONSOLIDATED(chunk) ((*(size_t *)CHUNK_HEADER(chunk)) & (CHUNK_CONSOLIDATED_BIT))
#define CHUNK_SIZE(chunk) (((*((size_t *)CHUNK_HEADER(chunk)))&(~15)))
#define IS_IN_USE(chunk) (IS_PREV_IN_USE(CHUNK_HEADER(chunk)+CHUNK_SIZE(chunk)+CHUNK_HEADER_SIZE))

heap create_heap(void *heap_start, size_t heap_size);
void *heap_allocate(p_heap heap, size_t data_size);
void heap_free(p_heap heap, void *chunk);
void free_chunk_from_next_metadata(p_used_chunk chunk);
void insert_to_bin(p_free_chunk *bin, p_used_chunk chunk, bool unset_used_bit);
void remove_chunk_from_bin(p_free_chunk *bin, p_free_chunk chunk);


void print_chunk(void *chunk, bool print_data);
void print_heap(p_heap heap);
void print_bin(p_free_chunk *bin);