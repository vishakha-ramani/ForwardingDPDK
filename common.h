#ifndef __COMMON_H
#define __COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <getopt.h>

int data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size;
int control_tx_ring_size, control_rx_ring_size;
struct rte_ether_addr forwarder_data_mac, forwarder_control_mac;
char *trace_filename;
int trace_repeat;
uint64_t cycles_per_packet;


size_t data_packet_count, control_packet_count, total_packet_count;
struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
char *trace_filename;
int trace_repeat;
struct rte_ether_addr my_data_mac, my_control_mac;
struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
struct rte_mbuf **trace_packets;

struct rte_ether_addr receiver_data_mac;
struct rte_mempool *fib_entry_pool;
struct rte_hash *fib;
struct rte_rcu_qsbr *qs_variable;
size_t mem_event_pos, mem_event_capacity;
rte_rwlock_t rw_lock;


int control_receive_burst_size; //, control_tx_ring_size, control_rx_ring_size;
char *forward_list_filename;
long long control_packet_count;
