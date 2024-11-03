#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "heap.h"

char heap_buffer[0x1000000] __attribute__ ((aligned (0x1000))); // 10 MB heap, page aligned

int main() {

   srand(time(NULL));

   heap h = create_heap((void *)&heap_buffer,0x1000000);

   void *chunk_array[100];

   for (int j = 0; j < 100; j++)
   {

      printf("at j %d\n",j);

      for (int i = 0; i < 100; i++)
      {
         size_t alloc_size = rand() % 1024;

         chunk_array[i] = heap_allocate(&h,alloc_size);

      }

      for (int i = 0; i < 100; i++)
      {

         size_t should_free = rand() % 10;

         if (should_free > 5)
            heap_free(&h,chunk_array[i]);

      }

   }

   print_bin(&h.unsorted_bin);

   return 0;
}