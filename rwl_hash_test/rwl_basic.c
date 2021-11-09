#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_rcu_qsbr.h>
#include <rte_rwlock.h>

#define READER_DEBUG(reader_id, ...) _READER_DEBUG(reader_id, __VA_ARGS__, "dummy")
#define _READER_DEBUG(reader_id, fmt, ...) printf("[%.1f, R_%zd] " fmt "%.0s\n",  \
        (double)(rte_get_tsc_cycles() - rte_start) / tsc_hz, reader_id, __VA_ARGS__)

#define WRITER_DEBUG(...) _WRITER_DEBUG(__VA_ARGS__, "dummy")
#define _WRITER_DEBUG(fmt, ...) printf("[%.1f, W] " fmt "%.0s\n", \
        (double)(rte_get_tsc_cycles() - rte_start) / tsc_hz, __VA_ARGS__)

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
} reader_args_t;

static uint64_t rte_start, tsc_hz;
static volatile int *shared_pointer;

action_list_t writer = {
        1,
        {
                {1, 2},
    //                {1, 1},
    //                {1, 1},
        }
};


action_list_t reader1 = {
        1,
        {
                {2, 1},
//                {3, 4},
        }
};
action_list_t reader2 = {
        2,
        {
                {3, 5},
                {1, 3},
        }
};

action_list_t *readers[] = {&reader1};

int reader_thread(void *args) {
    size_t i, reader_id, read_count;
    unsigned d;
    reader_args_t *reader_args = args;
    action_list_t *reader;
    rte_rwlock_t *rwl;
    volatile int *value_pointer, *shared_pointer_copy;

    reader_id = reader_args->reader_id;
    reader = readers[reader_id];
    read_count = reader->count;
    rwl = reader_args->rwl;

    READER_DEBUG(reader_id, "Starting reader, action_count=%zd", read_count);

    // delay 1s before start
    rte_delay_ms(1000);

    for (i = 0; i < read_count; i++) {
        // delay
        d = reader->actions[i].delay;
        READER_DEBUG(reader_id, "(%zd) Delay %us", i, d);
        rte_delay_ms(d * 1000);
        // read
        d = reader->actions[i].duration;

        rte_rwlock_read_lock(rwl);
        // have a local pointer point to the value, should use atomic
        value_pointer = __atomic_load_n(&shared_pointer, __ATOMIC_SEQ_CST);
        // no need to use atomic when accessing *value_pointer, since the object will never be updated
        READER_DEBUG(reader_id, "(%zd) Read %us, val=%d(%p)", i, d, *value_pointer, value_pointer);
        rte_delay_ms(d * 1000);
        rte_rwlock_read_unlock(rwl);
        
        // shared_pointer_copy for debug purpose, will not read shared_pointer at this stage when in real application
        shared_pointer_copy = __atomic_load_n(&shared_pointer, __ATOMIC_SEQ_CST);
        // however, application can still access *value_pointer (the object local pointer pointed to) at this stage
        READER_DEBUG(reader_id, "(%zd) Read %us end, val=%d(%p), shared=%d(%p)", i, d, *value_pointer, value_pointer, *shared_pointer_copy, shared_pointer_copy);
    }

    return 0;
}

void writer_thread(rte_rwlock_t *rwl) {
    size_t i;
    size_t write_count = writer.count;
    unsigned d;
    volatile int *next_val;

    WRITER_DEBUG("Starting writer, action_count=%zd", writer.count);

    WRITER_DEBUG("Init value, shared=%d(%p)", *shared_pointer, shared_pointer);

    // delay 1s before start
    rte_delay_ms(1000);

    for (i = 0; i < write_count; i++) {
        // delay
        d = writer.actions[i].delay;
        WRITER_DEBUG("(%zd) Delay %u", i, d);
        rte_delay_ms(d * 1000);

        // prepare write, no need to be atomic
        d = writer.actions[i].duration;
        rte_rwlock_write_lock(rwl);
        WRITER_DEBUG("(%zd) Write %us", i, d);
        //WRITER_DEBUG("before update: shared_pointer=%d(%p)", *shared_pointer, shared_pointer);
        next_val = rte_malloc("Next value", sizeof(int), RTE_CACHE_LINE_SIZE);
        memcpy(next_val, &i, sizeof(int));
        rte_delay_ms(d * 1000);
        //WRITER_DEBUG(" after update: val1=%d(%p), val2=%d(%p)", val_1, &val_1, val_2, &val_2);

        // exchange pointer (publish value), should use atomic
        WRITER_DEBUG("before atomic exchange: shared_value=%d(%p), next_val=%d(%p)",
                     *shared_pointer, shared_pointer, *next_val, next_val);
        next_val = __atomic_exchange_n(&shared_pointer, next_val, __ATOMIC_SEQ_CST);
        WRITER_DEBUG(" after atomic exchange: shared_value=%d(%p), next_val=%d(%p)",
                     *shared_pointer, shared_pointer, *next_val, next_val);
        WRITER_DEBUG("(%zd) Write %us end", i, d);
        rte_rwlock_write_unlock(rwl);

        WRITER_DEBUG("(%zd) Free %d(%p)", i, *next_val, next_val);
        rte_free(next_val);
//        WRITER_DEBUG("before free: val1=%d(%p), val2=%d(%p)", val_1, &val_1, val_2, &val_2);
//        *next_val = -1;
//        WRITER_DEBUG(" after free: val1=%d(%p), val2=%d(%p)", val_1, &val_1, val_2, &val_2);
    }
}

int main(int argc, char *argv[]) {
    int ret;
    unsigned int num_cores, lcore_id;
    size_t mem_size, num_readers, i;
    reader_args_t *reader_args;
    rte_rwlock_t *rwl;

    ret = rte_eal_init(argc, argv);
    printf("rte_eal_init returned %d\n", ret);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

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

    shared_pointer = rte_malloc("Shared pointer", sizeof(int), RTE_CACHE_LINE_SIZE);
    memset(shared_pointer, 0, sizeof(int));
    
    tsc_hz = rte_get_tsc_hz();
    rte_start = rte_get_tsc_cycles();
    printf("rte_start=%" PRIu64 "\n", rte_start);

    for (i = 0, lcore_id = -1; i < num_readers; i++) {
        lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        if (lcore_id == RTE_MAX_LCORE) rte_exit(EXIT_FAILURE, "Cannot get lcore_id for reader %zd", i);
        printf("lcore_id for reader %zd is %u\n", i, lcore_id);
        reader_args[i].reader_id = i;
        reader_args[i].rwl = rwl;
        rte_eal_remote_launch(reader_thread, reader_args + i, lcore_id);
    }

    writer_thread(rwl);

    rte_eal_mp_wait_lcore();

    rte_free(rwl);
    rte_free(reader_args);
    
    printf("duration=%.1f\n", (double) (rte_get_tsc_cycles() - rte_start) / (double) rte_get_tsc_hz());
}
