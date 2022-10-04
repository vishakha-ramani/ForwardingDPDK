#ifndef __FORWARDER_COMMON_H
#define __FORWARDER_COMMON_H

#define TEST_RCU
#define TEST_RCU_CONSTRAINED
#define TEST_RCU_PER_PACKET_QUIESCENT
//#define WRITE_TIME_AFTER_LOOKUP_F
#define RESULT_PACKETS_FILENAME "result_forwarder_packets.txt" // comment if do not wish to write results

#define RX_POOL_SIZE 16383
#define DATA_RECEIVE_RING_SIZE ((data_receive_burst_size))
#define CONTROL_RECEIVE_RING_SIZE ((data_receive_burst_size))
#define DATA_SEND_RING_SIZE ((data_send_burst_size))
#define REPORT_WAIT_MS 500
#define TX_BURST_PERIOD_US 1


//#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED) // only calculate for rcu_u
#ifdef TEST_RCU // only calculate for rcu_u
#define RESULT_RCU_U_FILENAME "result_forwarder_rcu_u.txt"
#endif // defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)

#include "../common.h"
#include <rte_ether.h>
#include <rte_hash.h>
#include <rte_hash_crc.h>
#include <rte_malloc.h>
#include <rte_rwlock.h>


typedef struct {
    uint32_t seq;
    uint64_t control_time;
    uint64_t control_arrive_time_f;
    struct rte_ether_addr receiver_mac;
} fib_entry_t;


typedef struct {
    uint16_t node_id;
    uint32_t seq;
    uint64_t control_time;
    uint64_t control_arrive_time_f;
    uint64_t publish_time_f;
} control_packet_stat_t;

//#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED) // only calculate for rcu_u
#ifdef TEST_RCU
typedef struct {
    char type;
    uint64_t value;
} mem_event_t;

#define MEM_EVENT_TYPE_ALLOCATE 'A'
#define MEM_EVENT_TYPE_FREE 'F'
#define MEM_EVENT_TYPE_CONTROL_TIMESTAMP 'T'

#endif // defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)


extern struct rte_ether_addr receiver_data_mac;
extern struct rte_mempool *fib_entry_pool;
extern struct rte_hash *fib;
#ifdef TEST_RCU
extern struct rte_rcu_qsbr *qs_variable;
//#ifndef TEST_RCU_CONSTRAINED
extern mem_event_t *mem_events;
extern size_t mem_event_pos, mem_event_capacity;
//#endif // TEST_RCU_CONSTRAINED
#else // TEST_RCU
extern rte_rwlock_t rw_lock;
#endif // TEST_RCU

static inline size_t fib_entry_pool_size(size_t fib_size) {
#ifdef TEST_RCU
    #ifdef TEST_RCU_CONSTRAINED
    printf("RCU constrained mode, fib_size=%zd, fib_entry_pool_size=%zd\n", fib_size, fib_size + 1);
    return fib_size + 1;
#else // TEST_RCU_CONSTRAINED
    printf("RCU unconstrained mode, fib_size=%zd, fib_entry_pool_size=%zd\n", fib_size, fib_size * 4);
    return fib_size * 4;
#endif // TEST_RCU_CONSTRAINED
#else // TEST_RCU
    printf("RWL mode, fib_size=%zd, fib_entry_pool_size=%zd\n", fib_size, fib_size);
    return fib_size;
#endif // TEST_RCU
}

static inline fib_entry_t *
get_new_fib_entry(void) {
    fib_entry_t *fib_entry;
    int ret;

    ret = rte_mempool_get(fib_entry_pool, (void **)&fib_entry);
    if (ret)
        rte_exit(EXIT_FAILURE, "Cannot get fib entry from mempool!\n");

    return fib_entry;
}

#ifdef TEST_RCU
//#ifndef TEST_RCU_CONSTRAINED

static inline void
add_mem_event(char type, uint64_t value) {
    mem_event_t *event;
    if (unlikely(mem_event_pos >= mem_event_capacity))
        rte_exit(EXIT_FAILURE, "memory events %zd >= capacity %zd\n", mem_event_pos, mem_event_capacity);
    event = mem_events + (mem_event_pos++);
    event->type = type;
    event->value = value;
}
//#endif // TEST_RCU_CONSTRAINED

static void
free_fib_entry(void *p, void *key_data) {
    RTE_SET_USED(p);
//#ifndef TEST_RCU_CONSTRAINED // only calculate for rcu_u
    fib_entry_t *entry = (fib_entry_t *)key_data;
    add_mem_event(MEM_EVENT_TYPE_FREE, entry->seq);
//#endif // defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)
    rte_mempool_put(fib_entry_pool, key_data);
}

#endif // TEST_RCU

// init hash using RCU
static inline void
init_hash(size_t capacity) {
    int ret;
    size_t sz;
    struct rte_hash_parameters fib_parameters = {
            .name = "FIB",
            .key_len = sizeof(uint16_t),
            .entries = capacity,
            .hash_func = rte_hash_crc,
            .hash_func_init_val = 0,
            .socket_id = rte_socket_id(),
            .reserved = 0,
#ifdef TEST_RCU
            .extra_flag = RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF, // use LF for RCU
#else // TEST_RCU
            .extra_flag = 0, // no need for RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY, since we will add lock ourselves
#endif  // TEST_RCU
    };

#ifdef TEST_RCU
    struct rte_hash_rcu_config rcu_config = {0};
#endif  // TEST_RCU

    fib = rte_hash_create(&fib_parameters);
    if (unlikely(!fib))
        rte_exit(EXIT_FAILURE, "Cannot create hashtable for fib\n");

#ifdef TEST_RCU
    sz = rte_rcu_qsbr_get_memsize(RTE_MAX_LCORE);
    if (unlikely(sz == 1))
        rte_exit(EXIT_FAILURE, "Cannot get size of rcu qsbr\n");

    qs_variable = rte_zmalloc("rcu_qs", sz, RTE_CACHE_LINE_SIZE);
    if (unlikely(!qs_variable))
        rte_exit(EXIT_FAILURE, "Cannot malloc qs_variable\n");

    ret = rte_rcu_qsbr_init(qs_variable, RTE_MAX_LCORE);
    if (unlikely(ret))
        rte_exit(EXIT_FAILURE, "Cannot init qs_variable\n");


    rcu_config.v = qs_variable;
    rcu_config.free_key_data_func = free_fib_entry;
    rcu_config.dq_size = fib_entry_pool_size(capacity);
    // use default value for other configurations
#ifdef TEST_RCU_CONSTRAINED
    rcu_config.mode = RTE_HASH_QSBR_MODE_SYNC;
#else // TEST_RCU_CONSTRAINED
    rcu_config.mode = RTE_HASH_QSBR_MODE_DQ;
#endif // TEST_RCU_CONSTRAINED
    ret = rte_hash_rcu_qsbr_add(fib, &rcu_config);
    if (unlikely(ret))
        rte_exit(EXIT_FAILURE, "Cannot add rcu_qsbr for fib\n");
#else // TEST_RCU
    rte_rwlock_init(&rw_lock);
#endif // TEST_RCU
}

static inline int
handle_data_packet(data_pkt_t *pkt, unsigned lcore_id) {
#ifndef TEST_RCU
    RTE_SET_USED(lcore_id);
#endif // TEST_RCU

    fib_entry_t *entry;
    int ret;

#ifdef TEST_RCU
    rte_rcu_qsbr_lock(qs_variable, lcore_id);
#else // TEST_RCU
    rte_rwlock_read_lock(&rw_lock);
#endif // TEST_RCU
    
    ret = rte_hash_lookup_data(fib, &pkt->common_header.dst_addr, (void **)&entry);
    if (unlikely(ret < 0)) {
        printf("Cannot find fib for node: %"PRIu16"\n", rte_be_to_cpu_16(pkt->common_header.dst_addr));
        ret = -1;
        goto end;
    }

    rte_ether_addr_copy(&entry->receiver_mac, &pkt->common_header.ether.d_addr);
    pkt->time_control = entry->control_time;
    pkt->time_control_arrive_f = entry->control_arrive_time_f;
    ret = 0;

end:
#ifdef TEST_RCU
    rte_rcu_qsbr_unlock(qs_variable, lcore_id);
#else // TEST_RCU
    rte_rwlock_read_unlock(&rw_lock);
#endif // TEST_RCU
    return ret;
}

static inline void
update_fib_entry(control_packet_stat_t *pkt_info) {
    fib_entry_t *fib_entry;
#ifdef TEST_RCU
    fib_entry = get_new_fib_entry();
//#ifndef TEST_RCU_CONSTRAINED
    add_mem_event(MEM_EVENT_TYPE_ALLOCATE, pkt_info->seq);
//#endif //TEST_RCU_CONSTRAINED
#else // TEST_RCU
    int ret;
    rte_rwlock_write_lock(&rw_lock);

    ret = rte_hash_lookup_data(fib, &pkt_info->node_id, (void **)&fib_entry);
    if (unlikely(ret < 0))
        rte_exit(EXIT_FAILURE, "Cannot find entry: %"PRIu16" in fib\n", rte_be_to_cpu_16(pkt_info->node_id));
#endif // TEST_RCU

    rte_ether_addr_copy(&receiver_data_mac, &fib_entry->receiver_mac);
    fib_entry->seq = pkt_info->seq;
    fib_entry->control_time = pkt_info->control_time;
    fib_entry->control_arrive_time_f = pkt_info->control_arrive_time_f;

#ifdef TEST_RCU
//    printf("before add_key_data\n");
    rte_hash_add_key_data(fib, &pkt_info->node_id, fib_entry);
//    printf("after add_key_data\n");
#else // TEST_RCU
    rte_rwlock_write_unlock(&rw_lock);
#endif // TEST_RCU

    pkt_info->publish_time_f = rte_rdtsc_precise();
//#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED) // only calculate for rcu_u
#ifdef TEST_RCU
    add_mem_event(MEM_EVENT_TYPE_CONTROL_TIMESTAMP, pkt_info->publish_time_f);
#endif // defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)

}

#endif //__FORWARDER_COMMON_H
