#include "heap.h"

void worldgen_heap_push(worldgen_heap *heap, double key, int index)
{
    int child = heap->size++;
    while (child > 0) {
        int parent = (child - 1) / 2;
        if (heap->items[parent].key <= key) {
            break;
        }
        heap->items[child] = heap->items[parent];
        child = parent;
    }
    heap->items[child].key = key;
    heap->items[child].index = index;
}

worldgen_heap_item worldgen_heap_pop(worldgen_heap *heap)
{
    worldgen_heap_item top = heap->items[0];
    worldgen_heap_item last = heap->items[--heap->size];
    int parent = 0;
    while (1) {
        int child = parent * 2 + 1;
        if (child >= heap->size) {
            break;
        }
        if (child + 1 < heap->size && heap->items[child + 1].key < heap->items[child].key) {
            child++;
        }
        if (last.key <= heap->items[child].key) {
            break;
        }
        heap->items[parent] = heap->items[child];
        parent = child;
    }
    heap->items[parent] = last;
    return top;
}
