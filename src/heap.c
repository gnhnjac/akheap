#include "heap.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

heap create_heap(void *heap_start, size_t heap_size)
{

	heap _heap;

	// zero out heap structure and heap area
	memset(&_heap,0,sizeof(heap));
	memset(heap_start,0,heap_size);

	// initialize heap top chunk
	((p_free_chunk)(heap_start - CHUNK_HEADER_SIZE))->chunk_size = (heap_size)|PREV_IN_USE_BIT;

	// initialize heap attribs
	_heap.start = heap_start;
	_heap.size = heap_size;

	return _heap;

}

// return chunk from bin (chunk is at metadata)
void *take_from_bin(p_free_chunk *bin)
{

	if (!bin || !*bin)
		return 0;

	p_free_chunk chunk = *bin;

	size_t chunk_size = chunk->chunk_size&(~15);

	p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

	next_chunk_metadata->chunk_size |= PREV_IN_USE_BIT; // it is now in use

	*bin = (*bin)->fwd_ptr;

	chunk->bin_ptr = 0;

	return (void *)chunk;

}

void *take_from_unsorted_and_promote(p_free_chunk *unsorted, p_free_chunk *large, p_free_chunk *small, size_t size)
{

	if (!unsorted | !large | !small)
		return 0;

	p_free_chunk tmp = *unsorted;
	p_free_chunk prev = 0;

	while(tmp)
	{

		p_free_chunk chunk = tmp;

		size_t chunk_size = chunk->chunk_size&(~15);

		if (chunk_size == size)
		{

			p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

			next_chunk_metadata->chunk_size |= PREV_IN_USE_BIT; // it is now in use

			if (prev)
				prev->fwd_ptr = tmp->fwd_ptr;
			else
				(*unsorted) = (*unsorted)->fwd_ptr;

			//*unsorted = (*unsorted)->fwd_ptr;

			chunk->bin_ptr = 0;

			return (void *)chunk;
		}
		else
		{

			// // promote entry to small / large bin

			// tmp = tmp->fwd_ptr;
			// *unsorted = tmp;

			// // try small

			// if (chunk_size < MIN_SIZE + CHUNK_ALIGN * SMALL_BIN_AMT)
			// {

			// 	//chunk->bin_ptr = 0;

			// 	insert_to_bin(&small[(int)((size-MIN_SIZE)/CHUNK_ALIGN)], (p_used_chunk)chunk, false);

			// }
			// else // put in large
			// {


			// }

		}

		prev = tmp;

		tmp = tmp->fwd_ptr;

	}

	return 0;
}

p_free_chunk merge_backwards(p_heap heap, p_free_chunk chunk)
{

	while(!(chunk->chunk_size&PREV_IN_USE_BIT))
	{

		chunk->chunk_size |= CHUNK_CONSOLIDATED_BIT; // pointless as we are removing it from it's bin

		p_free_chunk prev_chunk = ((void *)chunk) - chunk->prev_size;

		prev_chunk->chunk_size += chunk->chunk_size&(~15);

		remove_chunk_from_bin(chunk->bin_ptr,chunk);

		chunk = prev_chunk;

	}

	if (((void *)chunk) + (chunk->chunk_size&(~15)) >= heap->start + heap->top)
	{
		printf("Reached here");
		heap->top = (size_t)(((void *)chunk) + CHUNK_HEADER_SIZE - heap->start);
		((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|(chunk->chunk_size&PREV_IN_USE_BIT);
		remove_chunk_from_bin(chunk->bin_ptr,chunk);
		chunk = 0;
	}


	return chunk;

}

void free_bin_used_bit(p_free_chunk *bin)
{

	p_free_chunk tmp = *bin;

	while(tmp)
	{

		free_chunk_from_next_metadata((p_used_chunk)tmp);

		tmp = tmp->fwd_ptr;

	}

	tmp = *bin;

}

void consolidate_bin(p_heap heap, p_free_chunk *bin, p_free_chunk *tgt_bin)
{

	p_free_chunk tmp = *bin;

	while(tmp)
	{
		p_free_chunk next_tmp = tmp->fwd_ptr;

		if (!(tmp->chunk_size&CHUNK_CONSOLIDATED_BIT))
		{
			p_free_chunk merged_chunk = merge_backwards(heap, tmp);

			if (merged_chunk && !(merged_chunk->chunk_size&CHUNK_CONSOLIDATED_BIT))
			{
				insert_to_bin(tgt_bin,(p_used_chunk)merged_chunk,false);
			}

			if (merged_chunk)
				merged_chunk->chunk_size |= CHUNK_CONSOLIDATED_BIT;

		}

		tmp = next_tmp;

	}

	*bin = 0;

}

void remove_consolidation_bit_from_bin(p_free_chunk *bin)
{

	p_free_chunk tmp = *bin;

	while (tmp)
	{

		tmp->chunk_size &= ~CHUNK_CONSOLIDATED_BIT;

		tmp = tmp->fwd_ptr;

	}

}

void *heap_allocate(p_heap heap, size_t data_size)
{

	size_t size = data_size + CHUNK_HEADER_SIZE;
	size += CHUNK_ALIGN - size % CHUNK_ALIGN;

	if (size < MIN_SIZE)
		size = MIN_SIZE;

	// do bin checks...

	// check fast bin

	if (size < MIN_SIZE + CHUNK_ALIGN * FAST_BIN_AMT) // then check in the fast bin corresponding to the size
	{

		p_used_chunk chunk = take_from_bin(&heap->fast_bins[(int)((size-MIN_SIZE)/CHUNK_ALIGN)]);

		if (chunk)
		{

			chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // just for safety to prevent heap overflow

			return (void *)&chunk->data;

		}

	}

	// check small bin

	if ( size < MIN_SIZE + CHUNK_ALIGN * SMALL_BIN_AMT)
	{

		p_used_chunk chunk = take_from_bin(&heap->small_bins[(int)((size-MIN_SIZE)/CHUNK_ALIGN)]);

		if (chunk)
		{

			chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // just for safety to prevent heap overflow

			return (void *)&chunk->data;

		}

	}

	// consolidate fast bins and put the merged chunks in unsorted bin

	for (int i = 0; i < FAST_BIN_AMT; i++)
	{

		free_bin_used_bit(&heap->fast_bins[i]);

	}

	for (int i = 0; i < FAST_BIN_AMT; i++)
	{
		consolidate_bin(heap, &heap->fast_bins[i], &heap->unsorted_bin);

	}

	remove_consolidation_bit_from_bin(&heap->unsorted_bin);

	// check unsorted bin for matches (and promote to small and large on the way)

	p_used_chunk chunk = take_from_unsorted_and_promote(&heap->unsorted_bin, &heap->large_bins[0], &heap->small_bins[0], size);

	if (chunk)
	{

		chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // just for safety to prevent heap overflow

		return (void *)&chunk->data;

	}

	// allocate from top of heap

	if (heap->top + size < heap->size)
	{

		p_used_chunk chunk = (p_used_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE); // prev size is part of previous chunk if not freed
		chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // if prev was in use then keep the bit

		heap->top += size;
		((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|PREV_IN_USE_BIT;

		return (void *)&chunk->data;

	}
	else
	{

		// extend heap with sbrk

		// if can't then extend it with mmap

	}

	return 0;

}

void free_chunk_from_next_metadata(p_used_chunk chunk)
{

	size_t chunk_size = chunk->chunk_size&(~15);

	p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

	next_chunk_metadata->prev_size = chunk_size;
	next_chunk_metadata->chunk_size &= ~PREV_IN_USE_BIT;

}

void remove_chunk_from_bin(p_free_chunk *bin, p_free_chunk chunk)
{

	if (!bin || !*bin)
		return;

	p_free_chunk tmp = *bin;

	if (chunk == tmp)
	{
		(*bin) = (*bin)->fwd_ptr;
	}
	else
	{

		while(tmp->fwd_ptr)
		{

			p_free_chunk next_chunk = tmp->fwd_ptr;

			if (next_chunk == chunk)
			{

				tmp->fwd_ptr = chunk->fwd_ptr;
				return;

			}

			tmp = tmp->fwd_ptr;

		}

	}

}

// insert chunk to bin (chunk needs to be at metadata)
void insert_to_bin(p_free_chunk *bin, p_used_chunk chunk, bool unset_used_bit)
{

	assert(bin);

	p_free_chunk freed_chunk = (p_free_chunk)chunk;

	// handle case where chunk is part of a bin when it is being inserted into another bin
	if (freed_chunk->bin_ptr)
		remove_chunk_from_bin(freed_chunk->bin_ptr,freed_chunk);

	freed_chunk->bin_ptr = bin;

	if (*bin)
	{

		freed_chunk->fwd_ptr = *bin;

	}
	else
		freed_chunk->fwd_ptr = 0;
	
	*bin = freed_chunk;

	if (unset_used_bit)
	{

		free_chunk_from_next_metadata(chunk);

	}

}

void heap_free(p_heap heap, void *chunk)
{

	if (!IS_MMAPPED(chunk))
	{

		if (!IS_IN_USE(chunk))
		{
			printf("%p,%p,%x\n",heap,chunk,((p_free_chunk)(chunk-CHUNK_METADATA_SIZE))->chunk_size);
			//print_bin(&heap->unsorted_bin);
		}
		assert(IS_IN_USE(chunk)); // make sure chunk is in use before freeing (if not mmapped)
	}
	else
	{

		// munmap it

	}

	assert(chunk != heap->start + heap->top + CHUNK_HEADER_SIZE); // make sure chunk isn't the heap top

	// if larger than 64kb then consolidate all fastbins and merge all freed chunks in fast and unsorted (put back in unsorted or merge to top of heap)

	// put in fast bin

	size_t chunk_size = CHUNK_SIZE(chunk);

	if (chunk_size < MIN_SIZE + CHUNK_ALIGN * FAST_BIN_AMT) // put it in corresponding fast bin
	{

		insert_to_bin(&heap->fast_bins[(int)((chunk_size-MIN_SIZE)/CHUNK_ALIGN)],chunk-CHUNK_METADATA_SIZE,false);
		return;

	}

	else if(chunk_size > FAST_BIN_CONSOLIDATION_THRESHOLD)
	{

		// consolidate and merge fast bins to unsorted bin

		for (int i = 0; i < FAST_BIN_AMT; i++)
		{

			free_bin_used_bit(&heap->fast_bins[i]);

		}

		for (int i = 0; i < FAST_BIN_AMT; i++)
		{

			consolidate_bin(heap, &heap->fast_bins[i], &heap->unsorted_bin);

		}

		remove_consolidation_bit_from_bin(&heap->unsorted_bin);

	}

	p_free_chunk merged_chunk = merge_backwards(heap, chunk-CHUNK_METADATA_SIZE);
	// ** add merge forwards

	if (merged_chunk)
		insert_to_bin(&heap->unsorted_bin,(p_used_chunk)merged_chunk,true); // insert chunk to unsorted bin

}

void print_chunk(void *chunk, bool print_data)
{

	size_t chunk_size = CHUNK_SIZE(chunk);

	printf("chunk at 0x%p: SIZE=%u,P=%d,M=%d,A=%d,C=%d\n", chunk, chunk_size, IS_PREV_IN_USE(chunk) ? 1 : 0, IS_MMAPPED(chunk) ? 1 : 0, IS_FROM_SECONDARY_ARENA(chunk) ? 1 : 0, IS_CONSOLIDATED(chunk) ? 1 : 0);

	if (!print_data)
		return;

	int j = 0;
	for (int i = 0; i < (chunk_size-CHUNK_HEADER_SIZE) * 2; i++)
	{
		if (i % 16 == 0)
			putchar('\n');
		else if (i % 8 == 0)
		{
			j -= 8;
			printf("| ");
		}
		else if (i == (chunk_size-CHUNK_HEADER_SIZE) * 2 - (chunk_size-CHUNK_HEADER_SIZE)%8)
		{

			j -= (chunk_size-CHUNK_HEADER_SIZE)%8;
			printf("| ");

		}

		if ((i >= (chunk_size-CHUNK_HEADER_SIZE) * 2 - (chunk_size-CHUNK_HEADER_SIZE)%8) || (i % 16 >= 8))
			printf("%c ",*((char *)chunk + j));
		else
			printf("%02x ",(*((char *)chunk + j))&0xFF);

		j++;
	}

}

void print_heap(p_heap heap)
{

	size_t cur = 0;

	while (cur < heap->size)
	{

		void *cur_chunk = (void *)(heap->start+cur+CHUNK_HEADER_SIZE);

		if (cur < heap->top)
			print_chunk(cur_chunk, true);
		else
			print_chunk(cur_chunk, false);

		cur += CHUNK_SIZE(cur_chunk);

		putchar('\n');

	}

}

void print_bin(p_free_chunk *bin)
{

	printf("bin at 0x%p:\n",bin);

	p_free_chunk tmp = *bin;

	while(tmp)
	{

		print_chunk(((void *)tmp)+CHUNK_METADATA_SIZE,false);

		tmp = tmp->fwd_ptr;

	}

}