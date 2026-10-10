#ifndef FRONTIER_WORLDGEN_HEAP_H
#define FRONTIER_WORLDGEN_HEAP_H

/**
 * @file
 * Binary min-heap of tile indices, used by the drainage flood and road pathfinding.
 * The caller allocates items with room for every push.
 */

typedef struct {
    double key;
    int index;
} worldgen_heap_item;

typedef struct {
    worldgen_heap_item *items;
    int size;
} worldgen_heap;

void worldgen_heap_push(worldgen_heap *heap, double key, int index);

/**
 * Removes and returns the item with the smallest key. The heap must not be empty.
 */
worldgen_heap_item worldgen_heap_pop(worldgen_heap *heap);

#endif // FRONTIER_WORLDGEN_HEAP_H
