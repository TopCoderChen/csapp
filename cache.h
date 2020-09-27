#include "csapp.h"
#include <sys/time.h>

#define TYPES 6
extern const int cache_block_size[];
extern const int cache_cnt[];

typedef struct cache_block
{
    char *url;
    char *data;
    int datasize;
    int64_t time;  // timestamp, init to 0
    pthread_rwlock_t rwlock;
} cache_block;

typedef struct cache_type
{
    cache_block *cacheobjs;
    int size;  // length of the list.
} cache_type;

// Multi-level cache.
// Each caches[i] is a linkedlist of cache_block.
cache_type caches[TYPES];

// intialize cache with malloc
void init_cache();
// If miss cache return 0
// If hit cache & write content to fd, return 1
int read_cache(char *url, int fd);
//save value to cache
void write_cache(char *url, char *data, int len);
//free cache
void free_cache();
