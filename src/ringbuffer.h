#include <stdint.h>

/* Structure defines a 1-dimensional strided array */
typedef struct{
    void* arr;
    size_t capacity;

    size_t head;
    size_t to_end;
} Ringbuffer;
