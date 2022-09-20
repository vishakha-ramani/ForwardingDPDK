#ifndef __COMMON_H
#define __COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <getopt.h>
#include <stddef.h>
#include <rte_rwlock.h>
#include <rte_ethdev.h>
    
#define DATA_PKT_SIZE 60
#define CONTROL_PKT_SIZE 60
#define MBUF_CACHE_SIZE 250
#define MAX_LINE_WIDTH 250

#define ETHER_TYPE_CONTROL 0
#define ETHER_TYPE_DATA 1
#define ETHER_TYPE_WARMUP 2 

int data_send_burst_size=32;
int data_receive_burst_size=64;
int data_tx_ring_size=64;
int data_rx_ring_size=4096;
int control_tx_ring_size=64;
int control_rx_ring_size=64;
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
// size_t mem_event_pos, mem_event_capacity;
// rte_rwlock_t rw_lock;


int control_receive_burst_size; //, control_tx_ring_size, control_rx_ring_size;
char *forward_list_filename;
// long long control_packet_count;


typedef struct{
  struct rte_ether_hdr ether;
  uint32_t seq;
  uint64_t time_send;
  uint16_t dst_addr;
} common_t;

typedef struct{
  common_t common_header;
  uint64_t time_control;
  uint64_t time_control_arrive_f;
  uint64_t time_after_lookup_f;
  uint64_t time_exit_f;
} data_pkt_t;


typedef struct{
  common_t common_header;
} control_pkt_t;
#endif
