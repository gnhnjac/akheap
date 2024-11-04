#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <time.h>
#include "heap.h"

char heap_buffer[0x1000000] __attribute__ ((aligned (0x1000))); // 10 MB heap, page aligned (this is constant size, now i use sbrk)

#define ITERATIONS 10000
#define ALOCS_PER_ITER 100
#define FREE_CHANCE 2

int main() {

   srand(time(NULL));

   heap h = create_heap();

   void *chunk_array[ALOCS_PER_ITER];

   size_t iter = 0;
   size_t frees = 0;

   clock_t begin = clock();

   while(iter < ITERATIONS)
   {

      // printf("iteration %d, heap size: 0x%x\n",iter,h.size);

      // printf("at j %d\n",j);

      for (int i = 0; i < ALOCS_PER_ITER; i++)
      {
         size_t alloc_size = rand() % 1024;

         chunk_array[i] = heap_allocate(&h,alloc_size);

      }

      for (int i = 0; i < ALOCS_PER_ITER; i++)
      {
         size_t should_free = rand() % 10;

         if (should_free >= FREE_CHANCE)
         {
            heap_free(&h,chunk_array[i]);
            frees++;
         }

      }

      iter++;

      
   }

   clock_t end = clock();

   double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;

   print_bin(&h.unsorted_bin);
   
   printf("%fs for %u iterations, %d allocations and %d frees, heap size: 0x%x\n",time_spent,ITERATIONS,ITERATIONS*ALOCS_PER_ITER,frees,h.size);

   return 0;
}