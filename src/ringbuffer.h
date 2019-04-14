#include <stdint.h>
#include <stdbool.h>

typedef struct{
    int fd;
    uint8_t* buffer;

    size_t capacity;
    size_t head;
    size_t to_end;
    size_t wrap;
    bool wrapped;
} Ringbuffer;
