/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_rcu_qsbr.h>
#include <rte_hash.h>
#include <rte_mempool.h>
#include <rte_common.h>
#include <rte_rwlock.h>

#define READER_DEBUG(reader_id, ...) _READER_DEBUG(reader_id, __VA_ARGS__, "dummy")
#define _READER_DEBUG(reader_id, fmt, ...) printf("[%.1f, R_%zd] " fmt "%.0s\n",  \
        (double)(rte_get_tsc_cycles() - rte_start) / tsc_hz, reader_id, __VA_ARGS__)

#define WRITER_DEBUG(...) _WRITER_DEBUG(__VA_ARGS__, "dummy")
#define _WRITER_DEBUG(fmt, ...) printf("[%.1f, W] " fmt "%.0s\n", \
        (double)(rte_get_tsc_cycles() - rte_start) / tsc_hz, __VA_ARGS__)



#define HASH_ENTRIES 8
#define MBUF_CACHE_SIZE 250
static uint64_t rte_start, tsc_hz;
static volatile int **shared_pointer;
struct rte_mempool *value_pool;

rte_hash_free_key_data free_func(void *p, void *key_data)
{
    void *key_ptr = p;
    int *data = key_data;
    //printf("Freeing %d(%p) from mempool\n", *data, data);
    WRITER_DEBUG("Freeing %d(%p) from mempool", *data, data);
    rte_mempool_put(value_pool, key_data);
}

/* create a hash table with the given number of entries*/
static struct rte_hash *
create_hash_table(uint16_t num_entries)
//create_hash_table(uint16_t num_entries, struct rte_rcu_qsbr_dq* dq)
{
    struct rte_hash *handle = NULL;
    struct rte_hash_parameters params = {
        .entries = num_entries,
        .key_len = sizeof(int),
        .socket_id = rte_socket_id(),
        .hash_func_init_val = 0,   
    };
    params.name = "hash_rcu";
    params.extra_flag |= RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF ;
    
    handle = rte_hash_create(&params);
    if (handle == NULL) {
        rte_exit(EXIT_FAILURE, "Unable to create the hash table. \n");
    }
    printf("Created hash table with number of entries %"PRIu16"\n", num_entries);
    
    return handle;
}

static void
populate_hash_table(const struct rte_hash *handle, struct rte_mempool *value_pool)
{
    int key;
    int retval;
    int init_val = 0;
    int *to_add;
    
    for (int i = 0; i < HASH_ENTRIES; i++) {
        key = 100 + i;
        retval = rte_mempool_get(value_pool, (void**)&to_add);
        if (retval != 0) {
            rte_exit(EXIT_FAILURE, "Unable to get entry from the value pool \n");
        }
        memcpy(to_add, &init_val, sizeof(int));
        retval = rte_hash_add_key_data(handle, (void*)&key, (void*)to_add);
        printf("Added key %d with data %d(%p) in hash table. \n", key, *to_add, to_add);
    }
}

typedef struct {
    unsigned int delay, duration;
} action_t;

typedef struct {
    size_t count;
    action_t actions[];
} action_list_t;

typedef struct {
    size_t reader_id;
    rte_rwlock_t *rwl;
    struct rte_hash *handle;
} reader_args_t;

typedef struct {
    rte_rwlock_t *rwl;
    struct rte_hash *handle;
} writer_args_t;

action_list_t writer = {
        4,
        {
                {1, 2},
                {3, 1},
                {3, 1},
                {1, 1},
//                {1, 1},
//                {1, 1},
        }
};


action_list_t reader1 = {
        3,
        {
                {1, 1},
                {2, 1},
                {3, 4},
        }
};
action_list_t reader2 = {
        2,
        {
                {3, 2},
                {1, 2},
        }
};

//action_list_t *readers[] = {&reader1, &reader2};
action_list_t *readers[] = {&reader1};

int reader_thread(void *args) {
    size_t i, reader_id, read_count;
    int key = 100;
    int retval, pos;
    unsigned d;
    reader_args_t *reader_args = args;
    action_list_t *reader;
    rte_rwlock_t *rwl;
    struct rte_hash *handle;
    volatile int *value_pointer, *shared_pointer_copy;
    
    reader_id = reader_args->reader_id;
    reader = readers[reader_id];
    read_count = reader->count;
    rwl = reader_args->rwl;
    handle = reader_args->handle;

    READER_DEBUG(reader_id, "Starting reader, action_count=%zd", read_count);

    // delay 1s before start
    rte_delay_ms(1000);

    for (i = 0; i < read_count; i++) {
        key = 100; // number of keys looked up will be equal to read_count starting from 100
        // delay
        d = reader->actions[i].delay;
        READER_DEBUG(reader_id, "(%zd) Delay %us", i, d);
        rte_delay_ms(d * 1000);
        // read
        d = reader->actions[i].duration;
        
        rte_rwlock_read_lock(rwl);
        value_pointer = rte_malloc("Read pointer", sizeof(int), RTE_CACHE_LINE_SIZE);
        pos = rte_hash_lookup_data(handle, &key, &value_pointer);
        
        // no need to use atomic when accessing *value_pointer, since the object will never be updated
        READER_DEBUG(reader_id, "(%zd) Read %us, val=%d(%p) for key %d", i, d, *value_pointer, value_pointer, key);
        rte_delay_ms(d * 1000);
        READER_DEBUG(reader_id, "(%zd) Read %us end", i, d);
        rte_rwlock_read_unlock(rwl);
    }
    return 0;
}

void writer_thread(writer_args_t *args) {
    size_t i;
    size_t write_count = writer.count;
    unsigned d;
    int key = 100;
    int retval, pos;
    writer_args_t *writer_args = args;
    struct rte_rcu_qsbr *qv = writer_args->qv;
    struct rte_hash *handle = writer_args->handle;
    volatile int *next_val;
    
    WRITER_DEBUG("Starting writer, action_count=%zd", writer.count);

    // delay 1s before start
    rte_delay_ms(1000);

    for (i = 0; i < write_count; i++) {
        // delay
        d = writer.actions[i].delay;
        WRITER_DEBUG("(%zd) Delay %us", i, d);
        rte_delay_ms(d * 1000);

        // prepare write, no need to be atomic
        next_val = rte_malloc("Next Value", sizeof(int), RTE_CACHE_LINE_SIZE);
        memcpy(next_val, &i, sizeof(int));
        WRITER_DEBUG("(%zd) New value = %d(%p)", i,  *next_val, next_val);

        key = 100;
        d = writer.actions[i].duration;
        WRITER_DEBUG("(%zd) Write %us for key %d with value %d", i, d, key, *next_val );
        pos = rte_hash_lookup(handle, (void*)&key);
        rte_delay_ms(d * 1000); //describes write time
        retval = rte_hash_add_key_data(handle, (void*)&key, (void*)next_val);
        WRITER_DEBUG("(%zd) Write %us end", i, d);
    }
}

int main(int argc, char *argv[]) {
    int ret;
    unsigned int num_cores, lcore_id;
    size_t mem_size, num_readers, i;
    reader_args_t *reader_args;
    rte_rwlock_t rwl;
    
    ret = rte_eal_init(argc, argv);
    printf("rte_eal_init returned %d\n", ret);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot init EAL\n");
    
    value_pool = rte_mempool_create("VALUE_POOL", 2047*5, sizeof(int), MBUF_CACHE_SIZE, 0,
                                    NULL, NULL, NULL, NULL,
                                    SOCKET_ID_ANY, MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET);
    
    if (value_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create value pool\n");

    num_readers = sizeof(readers) / sizeof(void *);
    printf("# of readers = %zd\n", num_readers);

    num_cores = rte_lcore_count();
    printf("num_cores=%u\n", num_cores);
    // we need to start 3 threads, 1 for writer and 2 for readers
    if (num_cores < num_readers + 1) rte_exit(EXIT_FAILURE, "# of cores has to be %zd or more.\n", num_readers + 1);

    printf("malloc reader_args, size=%zd\n", sizeof(reader_args_t) * num_readers);
    reader_args = rte_zmalloc("reader_args", sizeof(reader_args_t) * num_readers, 0);
    if (!reader_args) rte_exit(EXIT_FAILURE, "Cannot malloc reader args");

    // prepare RWL
    rwl = rte_malloc(NULL, sizeof(rte_rwlock_t), RTE_CACHE_LINE_SIZE);
    if(rwl == NULL) rte_exit(EXIT_FAILURE, "Cannot malloc rwl");
    rte_rwlock_init(rwl);
    
    /* Create and populate shared data structure i.e. hash table*/
    printf("Creating hash table. \n");
    struct rte_hash * handle;
    handle = create_hash_table(HASH_ENTRIES);
    
//    /* Add RCU QSBR to hash table */
//    printf("Adding RCU QSBR to hash table \n");
//    rcu_cfg.v = qv;
//    rcu_cfg.mode = RTE_HASH_QSBR_MODE_DQ;
//    rcu_cfg.key_data_ptr = handle;
//    rcu_cfg.free_key_data_func = free_func;
//    //rcu_cfg.trigger_reclaim_limit = 1;
//    /* Attach RCU QSBR to hash table */
//    ret = rte_hash_rcu_qsbr_add(handle, &rcu_cfg);
//    if (ret < 0) rte_exit(EXIT_FAILURE, "Attach RCU QSBR to hash table failed\n");
    
    /*Populate hash table*/
    printf("Populating hash table\n");
    populate_hash_table(handle, value_pool);

    tsc_hz = rte_get_tsc_hz();
    rte_start = rte_get_tsc_cycles();
    printf("rte_start=%" PRIu64 "\n", rte_start);

    for (i = 0, lcore_id = -1; i < num_readers; i++) {
        lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        if (lcore_id == RTE_MAX_LCORE) rte_exit(EXIT_FAILURE, "Cannot get lcore_id for reader %zd", i);
        printf("lcore_id for reader %zd is %u\n", i, lcore_id);
        reader_args[i].reader_id = i;
        reader_args[i].rwl = rwl;
        reader_args[i].handle = handle;
        rte_eal_remote_launch(reader_thread, reader_args + i, lcore_id);
    }
    
    writer_args_t writer_args = {rwl, handle};
    writer_thread(&writer_args);

    rte_eal_mp_wait_lcore();
    
    rte_hash_free(handle);

//    rte_free(qv);
//    rte_free(reader_args);
//    rte_free(shared_pointer);
//    rte_free(&writer_args);
    
    printf("duration=%.1f\n", (double) (rte_get_tsc_cycles() - rte_start) / (double) rte_get_tsc_hz());
    
    return 0;
}




