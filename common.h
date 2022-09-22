#ifndef __COMMON_H
#define __COMMON_H


#include <stdint.h>
#include <getopt.h>
#include <stddef.h>
#include <rte_rwlock.h>
#include <rte_ethdev.h>
    
#define DATA_PKT_SIZE 60
#define CONTROL_PKT_SIZE 60
#define MBUF_CACHE_SIZE 250
#define MAX_LINE_WIDTH 250

#define ETHER_TYPE_DATA 10
#define ETHER_TYPE_CONTROL 100
#define ETHER_TYPE_WARMUP 1000 
#define MIN(v1, v2)	((v1) < (v2) ? (v1) : (v2))

static const struct rte_eth_conf port_conf_default;

static const struct rte_eth_conf port_conf_default = {
        .rxmode = {
                .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
        },
};


static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool, int data_tx_ring_size, int data_rx_ring_size, struct rte_ether_addr* my_data_mac);


typedef struct {
  struct rte_ether_hdr ether;
  uint32_t seq;
  uint64_t time_send;
  uint16_t dst_addr;
} common_t;

typedef struct {
  common_t common_header;
  uint64_t time_control;
  uint64_t time_control_arrive_f;
  uint64_t time_after_lookup_f;
  uint64_t time_exit_f;
} data_pkt_t;


typedef struct {
  common_t common_header;
} control_pkt_t;



/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool, int data_tx_ring_size, int data_rx_ring_size, struct rte_ether_addr* my_data_mac)
{
        struct rte_eth_conf port_conf = port_conf_default;
        const uint16_t rx_rings = 2, tx_rings = 2; //number of rx_rings and tx_rings changed from 1 to 2. 
        uint16_t nb_rxd = data_rx_ring_size;
        uint16_t nb_txd = data_tx_ring_size;
        int retval;
        uint16_t q;
        struct rte_eth_dev_info dev_info;
        struct rte_eth_txconf txconf;

        if (!rte_eth_dev_is_valid_port(port))
                return -1;

        retval = rte_eth_dev_info_get(port, &dev_info);
        if (retval != 0) {
                printf("Error during getting device (port %u) info: %s\n",
                                port, strerror(-retval));
                return retval;
        }

        if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
                port_conf.txmode.offloads |=
                        DEV_TX_OFFLOAD_MBUF_FAST_FREE;

        /* Configure the Ethernet device. */
        retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
        if (retval != 0)
                return retval;

        retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
        if (retval != 0)
                return retval;

        /* Allocate and set up 1 RX queue per Ethernet port. */
        for (q = 0; q < rx_rings; q++) {
                retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                                rte_eth_dev_socket_id(port), NULL, mbuf_pool);
                if (retval < 0)
                        return retval;
        }

        txconf = dev_info.default_txconf;
        txconf.offloads = port_conf.txmode.offloads;
        /* Allocate and set up 1 TX queue per Ethernet port. */
        for (q = 0; q < tx_rings; q++) {
                retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                rte_eth_dev_socket_id(port), &txconf);
                if (retval < 0)
                        return retval;
        }

        /* Start the Ethernet port. */
        retval = rte_eth_dev_start(port);
        if (retval < 0)
                return retval;

        /* Display the port MAC address. */
        //struct rte_ether_addr addr;
        retval = rte_eth_macaddr_get(port, my_data_mac);
        if (retval != 0)
                return retval;

        printf("Sender is Port %u with MAC address: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                        port,
                        my_data_mac->addr_bytes[0], my_data_mac->addr_bytes[1],
                        my_data_mac->addr_bytes[2], my_data_mac->addr_bytes[3],
                        my_data_mac->addr_bytes[4], my_data_mac->addr_bytes[5]);

        /* Enable RX in promiscuous mode for the Ethernet device. */
        retval = rte_eth_promiscuous_enable(port);
        if (retval != 0)
                return retval;

        return 0;
}

#endif


//#define data_send_burst_size 32
//#define data_receive_burst_size 64
//#define control_receive_burst_size 64
//#define data_tx_ring_size 64
//#define data_rx_ring_size 4096
//#define control_tx_ring_size 64
//#define control_rx_ring_size 64

//struct rte_ether_addr forwarder_data_mac, forwarder_control_mac;
//char *trace_filename;
//int trace_repeat;
//uint64_t cycles_per_packet;
//
//
//size_t data_packet_count, control_packet_count, total_packet_count;
//struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
//extern struct rte_ether_addr my_data_mac;//, my_control_mac;
//struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
//struct rte_mbuf **trace_packets;

//struct rte_ether_addr receiver_data_mac;
//struct rte_mempool *fib_entry_pool;
//struct rte_hash *fib;
//struct rte_rcu_qsbr *qs_variable;
//char *forward_list_filename;
