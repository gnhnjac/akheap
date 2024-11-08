#include "heap.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

heap create_heap()//void *heap_start, size_t heap_size)
{

	size_t heap_size = HEAP_SBRK_SIZE;

	void *heap_start = sbrk(heap_size);

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
p_used_chunk take_from_bin(p_free_chunk *bin)
{

	if (!bin || !*bin)
		return 0;

	p_free_chunk chunk = *bin;

	size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

	p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

	next_chunk_metadata->chunk_size |= PREV_IN_USE_BIT; // it is now in use

	*bin = (*bin)->fwd_ptr;

	if (*bin)
		(*bin)->bin_ptr = chunk->bin_ptr;

	chunk->bin_ptr = 0;

	return (p_used_chunk)chunk;

}

void insert_to_large_bin(p_free_chunk *large, p_free_chunk chunk)
{

	size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

	size_t current_minimum = LARGE_BIN_MINIMUM;
	size_t current_bins = LARGE_BIN_AMT + LARGE_BIN_AMT%2;
	size_t total_bins = 0;
	size_t current_spacing = LARGE_BIN_MINIMUM_SPACING;

	while (current_bins > 0)
	{
		current_bins /= 2;

		if (current_bins == 1 || chunk_size < current_minimum + current_bins * current_spacing)
		{

			size_t large_index;

			if (current_bins == 1)
				large_index = LARGE_BIN_AMT-1;
			else
				large_index = total_bins + ((chunk_size - current_minimum)/current_spacing);

			chunk->fwd_ptr = large[large_index];
			chunk->bin_ptr = ((void *)&large[large_index]) - CHUNK_METADATA_SIZE;
			if (large[large_index])
				large[large_index]->bin_ptr = (p_free_chunk *)chunk;
			large[large_index] = chunk;
			break;

		}

		total_bins += current_bins;
		current_minimum += current_bins * current_spacing;
		current_spacing *= 8;
	}

}

void *split_chunk_and_put_remainder_in_unsorted(p_free_chunk *unsorted, p_free_chunk chunk, size_t new_size)
{

	size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

	size_t remainder_size = chunk_size - new_size;

	if (remainder_size < MIN_SIZE)
		return 0; // couldn't split chunk

	p_free_chunk remainder_chunk = ((void *)chunk) + new_size;

	remainder_chunk->chunk_size = remainder_size|PREV_IN_USE_BIT; // we're setting the prev in use bit...
	remainder_chunk->bin_ptr = 0; // it belongs to no bin yet

	insert_to_bin(unsorted,(p_used_chunk)remainder_chunk,true); // since the size now changed we need to update it in the next chunk prev size.

	chunk->chunk_size = new_size | (chunk->chunk_size&PREV_IN_USE_BIT); // set chunk size to new size and return it

	return (void *)chunk;
}

// search until large enough chunk is found
p_used_chunk take_from_large_bin(p_free_chunk *unsorted, p_free_chunk *large, size_t chunk_size)
{

	if (chunk_size < LARGE_BIN_MINIMUM)
		return 0;

	size_t current_minimum = LARGE_BIN_MINIMUM;
	size_t current_bins = LARGE_BIN_AMT + LARGE_BIN_AMT%2;
	size_t total_bins = 0;
	size_t current_spacing = LARGE_BIN_MINIMUM_SPACING;
	bool passed_corresponding_bin = false;
	size_t passed_corresponding_bin_index = 0; // true if already checked the bin corresponding to the min-max size, if so then just continue checking until a large enough chunk is found

	while (total_bins < LARGE_BIN_AMT && passed_corresponding_bin_index < LARGE_BIN_AMT)
	{
		current_bins /= 2;

		if (current_bins == 1 || chunk_size < current_minimum + current_bins * current_spacing || passed_corresponding_bin)
		{

			size_t large_index;

			if(passed_corresponding_bin) // if already passed corresponding bin then just iterate over the successing bins
				large_index = passed_corresponding_bin_index;
			else if (current_bins == 1)
				large_index = LARGE_BIN_AMT-1;
			else
				large_index = total_bins + ((chunk_size - current_minimum)/current_spacing);

			p_free_chunk tmp = large[large_index];
			p_free_chunk prev = 0;

			if (tmp)
			{

				while(tmp)
				{

					size_t tmp_size = tmp->chunk_size&(~CHUNK_FLAG_MASK);

					if (tmp_size >= chunk_size)
					{

						p_free_chunk chunk = split_chunk_and_put_remainder_in_unsorted(unsorted,tmp,chunk_size);

						if (!chunk)
							chunk = tmp;

						chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

						p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

						next_chunk_metadata->chunk_size |= PREV_IN_USE_BIT; // it is now in use

						if (tmp == large[large_index])
						{
							large[large_index] = chunk->fwd_ptr;
							if (large[large_index])
								large[large_index]->bin_ptr = chunk->bin_ptr;
						}
						else
						{
							prev->fwd_ptr = chunk->fwd_ptr;
							if (chunk->fwd_ptr)
								chunk->fwd_ptr->bin_ptr = (p_free_chunk *)prev;
						}

						chunk->bin_ptr = 0;

						return (p_used_chunk)chunk;

					}

					prev = tmp;
					tmp = tmp->fwd_ptr;

				}

			}

			passed_corresponding_bin = true;
			passed_corresponding_bin_index = large_index;

			//return 0; // the corresponding bin is empty / didn't find correct size in bin

		}

		if (passed_corresponding_bin)
			passed_corresponding_bin_index++;
		else
		{
			total_bins += current_bins;
			current_minimum += current_bins * current_spacing;
			current_spacing *= 8;
		}

	}

	return 0; // didn't find chunk big enough? return null
}

p_used_chunk take_from_unsorted_and_promote(p_free_chunk *unsorted, p_free_chunk *large, p_free_chunk *small, size_t size)
{

	if (!unsorted | !large | !small)
		return 0;

	p_free_chunk tmp = *unsorted;
	p_free_chunk prev = 0;

	while(tmp)
	{

		p_free_chunk chunk = tmp;

		size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

		if (chunk_size == size)
		{

			p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

			next_chunk_metadata->chunk_size |= PREV_IN_USE_BIT; // it is now in use

			*unsorted = (*unsorted)->fwd_ptr;

			if (*unsorted)
				(*unsorted)->bin_ptr = chunk->bin_ptr;

			chunk->bin_ptr = 0;

			return (p_used_chunk)chunk;
		}
		else
		{

			// promote entry to small / large bin

			tmp = tmp->fwd_ptr;
			*unsorted = tmp;

			// try small

			if (chunk_size < MIN_SIZE + CHUNK_ALIGN * SMALL_BIN_AMT)
			{

				size_t small_idx = (int)((chunk_size-MIN_SIZE)/CHUNK_ALIGN);

				chunk->fwd_ptr = small[small_idx];
				chunk->bin_ptr = ((void *)&small[small_idx]) - CHUNK_METADATA_SIZE;
				if (small[small_idx])
					small[small_idx]->bin_ptr = (p_free_chunk *)chunk;
				small[small_idx] = chunk;

			}
			else // put in large
			{

				insert_to_large_bin(large,chunk);

			}

		}

	}

	return 0;
}

p_free_chunk merge_backwards(p_heap heap, p_free_chunk chunk)
{

	if (!chunk)
		return 0;

	while(!(chunk->chunk_size&PREV_IN_USE_BIT))
	{


		p_free_chunk prev_chunk = ((void *)chunk) - chunk->prev_size;

		assert((prev_chunk->chunk_size&(~CHUNK_FLAG_MASK))==chunk->prev_size);

		prev_chunk->chunk_size += chunk->chunk_size&(~CHUNK_FLAG_MASK);

		remove_chunk_from_bin(chunk->bin_ptr,chunk); // since it's no longer a chunk after we merge it (it belongs to some bin currently)

		chunk->chunk_size |= CHUNK_CONSOLIDATED_BIT;

		chunk = prev_chunk;

	}

	if (((void *)chunk) + (chunk->chunk_size&(~CHUNK_FLAG_MASK)) + CHUNK_HEADER_SIZE >= heap->start + heap->top)
	{

		heap->top = (size_t)(((void *)chunk) + CHUNK_HEADER_SIZE - heap->start);
		((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|(chunk->chunk_size&PREV_IN_USE_BIT);
		remove_chunk_from_bin(chunk->bin_ptr,chunk);
		chunk = 0;
	}


	return chunk;

}

p_free_chunk merge_forwards(p_heap heap, p_free_chunk chunk)
{

	if (!chunk)
		return 0;

	while (true)
	{

		size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

		p_free_chunk next_chunk = ((void *)chunk) + chunk_size;

		if (((void *)next_chunk) + CHUNK_HEADER_SIZE >= heap->start + heap->top)
		{

			heap->top = (size_t)(((void *)chunk) + CHUNK_HEADER_SIZE - heap->start);
			((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|(chunk->chunk_size&PREV_IN_USE_BIT);
			remove_chunk_from_bin(chunk->bin_ptr,chunk);
			return 0;

		}

		if (!IS_IN_USE(((void *)next_chunk)+CHUNK_METADATA_SIZE))
		{

			chunk->chunk_size += next_chunk->chunk_size&(~CHUNK_FLAG_MASK);

			remove_chunk_from_bin(next_chunk->bin_ptr,next_chunk);

			next_chunk->chunk_size |= CHUNK_CONSOLIDATED_BIT;

		}
		else
			return chunk;

	}

	return 0; // not supposed to reach here

}

void free_bin_used_bit(p_free_chunk *bin)
{

	p_free_chunk tmp = *bin;

	while(tmp)
	{

		free_chunk_from_next_metadata((p_used_chunk)tmp);

		tmp = tmp->fwd_ptr;

	}

}

void consolidate_bin(p_heap heap, p_free_chunk *bin, p_free_chunk *tgt_bin)
{

	p_free_chunk tmp = *bin;

	while(tmp)
	{

		tmp->bin_ptr = 0; // make it so you can't remove entries from this bin
		tmp = tmp->fwd_ptr;

	}

	tmp = *bin;

	while(tmp)
	{
		p_free_chunk next_tmp = tmp->fwd_ptr;

		if (!(tmp->chunk_size&CHUNK_CONSOLIDATED_BIT)) // check if it hasn't already consolidated with another chunk
		{

			p_free_chunk merged_chunk = merge_backwards(heap, tmp);

			if (merged_chunk) // if not merged with top then merge it forwards
				merged_chunk = merge_forwards(heap,merged_chunk);

			if (merged_chunk) // no need to add it if it's merged with heap top
			{
				insert_to_bin(tgt_bin,(p_used_chunk)merged_chunk,true);
				merged_chunk->chunk_size |= CHUNK_CONSOLIDATED_BIT; // if the backwards merged chunk appears in the future
			}
			// no need for that since there's no case where 
			// else if (merged_chunk && (merged_chunk->chunk_size&CHUNK_CONSOLIDATED_BIT))
			// {
			// 	printf("here somehow?");
			// 	free_chunk_from_next_metadata((p_used_chunk)merged_chunk); // size has changed, update it
			// }
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

	// check large bin for matches

	chunk = take_from_large_bin(&heap->unsorted_bin,&heap->large_bins[0], size);

	if (chunk)
	{

		// can't do that since chunk size might be greated than requested size
		//chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // just for safety to prevent heap overflow

		return (void *)&chunk->data;

	}

	// allocate from top of heap

	if (size >= MMAP_THRESHOLD) // allocate with mmap
	{

		int fd = open("/dev/zero", O_RDWR);
		p_used_chunk chunk = (p_used_chunk)(mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE, fd, 0)-CHUNK_HEADER_SIZE);
		close(fd);

		chunk->chunk_size = size|MMAPPED_BIT;

		return (void *)&chunk->data;

	}
	else if (heap->top + size < heap->size)
	{

		p_used_chunk chunk = (p_used_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE); // prev size is part of previous chunk if not freed
		chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // if prev was in use then keep the bit

		heap->top += size;
		((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|PREV_IN_USE_BIT;

		return (void *)&chunk->data;

	}
	else
	{

		// extend heap with sbrk then give from top of heap

		while(heap->top + size >= heap->size)
		{
			sbrk(HEAP_SBRK_SIZE);
			heap->size += HEAP_SBRK_SIZE;
		}

		p_used_chunk chunk = (p_used_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE); // prev size is part of previous chunk if not freed
		chunk->chunk_size = size|(chunk->chunk_size&PREV_IN_USE_BIT); // if prev was in use then keep the bit

		heap->top += size;
		((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|PREV_IN_USE_BIT;

		return (void *)&chunk->data;

	}

	return 0;

}

void free_chunk_from_next_metadata(p_used_chunk chunk)
{

	size_t chunk_size = chunk->chunk_size&(~CHUNK_FLAG_MASK);

	p_chunk_metadata next_chunk_metadata = (p_chunk_metadata)(((void *)chunk) + chunk_size);

	next_chunk_metadata->prev_size = chunk_size;
	next_chunk_metadata->chunk_size &= ~PREV_IN_USE_BIT;

}

void remove_chunk_from_bin(p_free_chunk *bin, p_free_chunk chunk)
{

	if (!bin)// || !*bin)
	{
		return;
	}

	chunk->bin_ptr = 0;

	p_free_chunk tmp = (p_free_chunk)bin;

	tmp->fwd_ptr = chunk->fwd_ptr;

	if (chunk->fwd_ptr)
		chunk->fwd_ptr->bin_ptr = (p_free_chunk *)tmp;

	// no need to iterate over whole bin (previously bin_ptr was just a pointer to the bin), we can just point it to the prev entry. redundant overhead...
	// if (chunk == tmp)
	// {
	// 	(*bin) = (*bin)->fwd_ptr;
	// }
	// else
	// {

	// 	while(tmp->fwd_ptr)
	// 	{

	// 		p_free_chunk next_chunk = tmp->fwd_ptr;

	// 		if (next_chunk == chunk)
	// 		{

	// 			tmp->fwd_ptr = chunk->fwd_ptr;
	// 			return;

	// 		}

	// 		tmp = tmp->fwd_ptr;

	// 	}

	// }

}

// insert chunk to bin (chunk needs to be at metadata)
void insert_to_bin(p_free_chunk *bin, p_used_chunk chunk, bool unset_used_bit)
{

	assert(bin);

	p_free_chunk freed_chunk = (p_free_chunk)chunk;

	// handle case where chunk is part of a bin when it is being inserted into another bin
	if (freed_chunk->bin_ptr)
		remove_chunk_from_bin(freed_chunk->bin_ptr,freed_chunk);

	freed_chunk->bin_ptr = ((void *)bin)-CHUNK_METADATA_SIZE;

	if (*bin)
	{

		freed_chunk->fwd_ptr = *bin;
		(*bin)->bin_ptr = (p_free_chunk *)freed_chunk;

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
		assert(IS_IN_USE(chunk)); // make sure chunk is in use before freeing (if not mmapped)
		assert(((size_t)(chunk-CHUNK_HEADER_SIZE))%CHUNK_ALIGN == 0); // make sure chunk size is aligned correctly
		assert(chunk >= heap->start && chunk < heap->start + heap->top); // make sure chunk is within heap boundaries
	}
	else
	{

		// munmap it

		size_t chunk_size = CHUNK_SIZE(chunk);
		munmap(chunk-CHUNK_HEADER_SIZE,chunk_size);
		return;

	}

	assert(chunk != heap->start + heap->top + CHUNK_HEADER_SIZE); // make sure chunk isn't the heap top

	// if larger than 64kb then consolidate all fastbins and merge all freed chunks in fast and unsorted (put back in unsorted or merge to top of heap)

	// put in fast bin

	size_t chunk_size = CHUNK_SIZE(chunk);

	((p_free_chunk)(chunk-CHUNK_METADATA_SIZE))->bin_ptr = 0; // make sure it doesn't point to any bin (as it isn't in one as of now), otherwise it can lead to faulty addresses

	if (chunk_size < MIN_SIZE + CHUNK_ALIGN * FAST_BIN_AMT) // put it in corresponding fast bin
	{

		insert_to_bin(&heap->fast_bins[(int)((chunk_size-MIN_SIZE)/CHUNK_ALIGN)],chunk-CHUNK_METADATA_SIZE,false);
		return;

	}

	else if(chunk_size > FAST_BIN_CONSOLIDATION_THRESHOLD)
	{
		// consolidate and merge fast bins to unsorted bin, if heap top is large enough then give it back to the system

		for (int i = 0; i < FAST_BIN_AMT; i++)
		{

			free_bin_used_bit(&heap->fast_bins[i]);

		}

		for (int i = 0; i < FAST_BIN_AMT; i++)
		{

			consolidate_bin(heap, &heap->fast_bins[i], &heap->unsorted_bin);

		}

		remove_consolidation_bit_from_bin(&heap->unsorted_bin);

		// if top is large enough, give it back to the system
		if (heap->size - heap->top > HEAP_TOP_TRIM_THRESHOLD)
		{
			size_t trim_size = ((heap->size-heap->top) >> 1); // halve heap top (align to page size)
			trim_size -= trim_size%PAGE_SIZE;
			heap->size -= trim_size;
			bool prev_in_use = IS_PREV_IN_USE(heap->start + heap->top + CHUNK_HEADER_SIZE);
			((p_free_chunk)(heap->start + heap->top - CHUNK_HEADER_SIZE))->chunk_size = (heap->size-heap->top)|prev_in_use;

			// set brk top
			brk(heap->start+heap->size);
		}

	}

	p_free_chunk merged_chunk = merge_backwards(heap, chunk-CHUNK_METADATA_SIZE);

	if (merged_chunk)
	{

		//free_chunk_from_next_metadata((p_used_chunk)merged_chunk);

		merged_chunk = merge_forwards(heap,merged_chunk);
		// ** add merge forwards

		if (merged_chunk)
			insert_to_bin(&heap->unsorted_bin,(p_used_chunk)merged_chunk,true); // insert chunk to unsorted bin

	}

}

void print_chunk(void *chunk, bool print_data)
{

	size_t chunk_size = CHUNK_SIZE(chunk);

	printf("chunk at 0x%p: SIZE=%u,P=%d,M=%d,C=%d\n", chunk, chunk_size, IS_PREV_IN_USE(chunk) ? 1 : 0, IS_MMAPPED(chunk) ? 1 : 0, IS_CONSOLIDATED(chunk) ? 1 : 0);

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