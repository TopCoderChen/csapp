#include "cache.h"

// 分级数组 for caches.
const int cache_block_size[] = {102, 1024, 5120, 10240, 25600, 102400};
const int cache_cnt[] = {40, 20, 20, 10, 12, 5};

static int64_t currentTimeMillis();

void init_cache()
{
    for (int i = 0; i < TYPES; i++)
    {
        caches[i].size = cache_cnt[i];
        caches[i].cacheobjs
            = (cache_block *)malloc(cache_cnt[i] * sizeof(cache_block));
        cache_block *j = caches[i].cacheobjs;
        int k;
        for (k = 0; k < cache_cnt[i]; j++, k++)
        {
            j->time = 0;
            j->datasize = 0;
            j->url = malloc(sizeof(char) * MAXLINE);
            strcpy(j->url, "");
            j->data = malloc(sizeof(char) * cache_block_size[i]);
            memset(j->data, 0, cache_block_size[i]);
            pthread_rwlock_init(&j->rwlock, NULL);
        }
    }
}

void free_cache()
{
    for (int i = 0; i < TYPES; i++)
    {
        cache_block *j = caches[i].cacheobjs;
        int k;
        for (k = 0; k < cache_cnt[i]; j++, k++)
        {
            free(j->url);
            free(j->data);
            pthread_rwlock_destroy(&j->rwlock);
        }
        free(caches[i].cacheobjs);
    }
}

int read_cache(char *url, int fd)
{

    int tar = 0, i = 0;
    cache_type cur;
    cache_block *p;
    printf("Reading cache for %s\n", url);
    for (; tar < TYPES; tar++)
    {
        cur = caches[tar];
        p = cur.cacheobjs;
        for(i = 0; i < cur.size; i++, p++)
        {
            if(p->time != 0 && strcmp(url, p->url) == 0) break;
        }
        if (i < cur.size) break;
    }

    if(i == cur.size)
    {
        printf("Read cache not found for %s\n", url);
        return 0;
    }
    pthread_rwlock_rdlock(&p->rwlock);
    if(strcmp(url, p->url) != 0)
    {
        pthread_rwlock_unlock(&p->rwlock);
        return 0;
    }
    pthread_rwlock_unlock(&p->rwlock);
    if (!pthread_rwlock_trywrlock(&p->rwlock))
    {
        // Update timestamp.
        p->time = currentTimeMillis();
        pthread_rwlock_unlock(&p->rwlock);
    }
    pthread_rwlock_rdlock(&p->rwlock);
    Rio_writen(fd, p->data, p->datasize);
    pthread_rwlock_unlock(&p->rwlock);
    return 1;
}

void write_cache(char *url, char *data, int len)
{
    // Find the appropriate level of caches.
    int level = 0;
    while (level < TYPES && len > cache_block_size[level])
    {
        level++;
    }
    if (level > TYPES) {
        printf("Cache is too small for %s with %d bytes\n", url, len);
    }

    printf("Writing cache for %s at %d bytes\n", url, cache_block_size[level]);

    // Find empty block
    cache_type cur = caches[level];
    // p is the pointer to the final target block.
    cache_block *p = cur.cacheobjs;
    int i;
    for(i = 0; i < cur.size; i++, p++)
    {
        if(p->time == 0)
        {
            break;
        }
    }

    // Find LRU (always linear scan the cache the smallest timestamp)
    cache_block *lru;
    int64_t min = currentTimeMillis();
    if(i == cur.size)
    {
        for(i = 0, lru = cur.cacheobjs; i < cur.size; i++, lru++)
        {
            pthread_rwlock_rdlock(&lru->rwlock);
            if(lru->time <= min)
            {
                min = lru->time;
                p = lru;
            }
            pthread_rwlock_unlock(&lru->rwlock);
        }
    }

    // Write cache.
    pthread_rwlock_wrlock(&p->rwlock);
    p->time = currentTimeMillis();
    p->datasize = len;
    memcpy(p->url, url, MAXLINE);
    memcpy(p->data, data, len);
    pthread_rwlock_unlock(&p->rwlock);
    
}

static int64_t currentTimeMillis()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    int64_t s1 = (int64_t)(time.tv_sec) * 1000;
    int64_t s2 = (time.tv_usec / 1000);
    return s1 + s2;
}
