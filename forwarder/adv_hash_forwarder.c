/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <inttypes.h>
#include <getopt.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <unistd.h>
#include <rte_common.h>
#include <rte_hash.h>
#include <rte_mbuf_dyn.h>
#include <sys/time.h>
#include <rte_time.h>
#include <rte_rcu_qsbr.h>
#include <rte_malloc.h>
#include <rte_atomic.h>
#include <rte_mempool.h>


#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS ((64*1024)-1)
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 256
#define PTP_PROTOCOL 0x88F7
#define HASH_ENTRIES 1024*2
#define CONTROL_BURST_SIZE 256
uint64_t rx_count; // global variable to keep track of the number of received packets (to be displayed every second)
uint64_t tx_count;
uint64_t rx_count_control;
struct rte_mempool *value_pool;

/* Rx/Tx callbacks variables - HW timestamping is not included since 
 * rte_mbuf_timestamp_t was not recognized. */
typedef uint64_t tsc_t;
static int tsc_dynfield_offset = -1;

static inline tsc_t *
tsc_field(struct rte_mbuf *mbuf)
{
    return RTE_MBUF_DYNFIELD(mbuf, tsc_dynfield_offset, tsc_t *);
}

static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
	},
};

static struct {
    uint64_t total_cycles;
    uint64_t total_queue_cycles;
    uint64_t total_pkts;
} latency_numbers;

#define TICKS_PER_CYCLE_SHIFT 16
static uint64_t ticks_per_cycle_mult;

/* Callback added to the RX port and applied to packets. 8< */
static uint16_t
add_timestamps(uint16_t port __rte_unused, uint16_t qidx __rte_unused,
        struct rte_mbuf **pkts, uint16_t nb_pkts,
        uint16_t max_pkts __rte_unused, void *_ __rte_unused)
{
    unsigned i;
    uint64_t now = rte_rdtsc();
    
    for (i = 0; i < nb_pkts; i++)
        *tsc_field(pkts[i]) = now;
    return nb_pkts;
}
/* >8 End of callback addition and application. */

/* Callback is added to the TX port. 8< */
static uint16_t
calc_latency(uint16_t port, uint16_t qidx __rte_unused,
        struct rte_mbuf **pkts, uint16_t nb_pkts, void *_ __rte_unused)
{   
    static uint64_t totalbatches = 0;
    uint64_t cycles = 0;
    uint64_t queue_ticks = 0;
    uint64_t now = rte_rdtsc();
    uint64_t ticks;
    unsigned i;
    for (i = 0; i < nb_pkts; i++) {
        cycles += now - *tsc_field(pkts[i]);
    }
    latency_numbers.total_cycles += cycles;
    latency_numbers.total_pkts += nb_pkts;
    totalbatches += 1;
    if (latency_numbers.total_pkts > (100 * 1000)) {
        printf("func - Latency = %"PRIu64" cycles %" PRIu64 " number\n",
        latency_numbers.total_cycles / latency_numbers.total_pkts, latency_numbers.total_pkts /totalbatches);
        latency_numbers.total_cycles = 0;
        latency_numbers.total_queue_cycles = 0;
        latency_numbers.total_pkts = 0;
        totalbatches = 0;
    }
    return nb_pkts;
}
/* >8 End of callback addition. */

struct value{
    struct rte_ether_addr dest_mac_addr; //destination mac address
    uint64_t t; //timestamp of address
};


rte_hash_free_key_data free_func(void *p, void *key_data)
{
    void *key_ptr = p;
    int *data = key_data;
    printf("Freeing %d(%p) from mempool\n", *data, data);
    //WRITER_DEBUG("Freeing %d(%p) from mempool", *data, data);
    rte_mempool_put(value_pool, key_data);
}

/* create a hash table with the given number of entries*/
static struct rte_hash *
create_hash_table(uint16_t num_entries)
{
    struct rte_hash *handle;
    struct rte_hash_parameters params = {
        .entries = num_entries,
        .key_len = sizeof(uint16_t),
        .socket_id = rte_socket_id(),
        .hash_func_init_val = 0,   
    };
    params.name = "forwarding table";
    params.extra_flag |= RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF ;

    handle = rte_hash_create (&params);
    if (handle == NULL) {
        rte_exit(EXIT_FAILURE, "Unable to create the hash table. \n");
    }
    printf("Created hash table with number of entries %"PRIu16"\n", num_entries);
    
    return handle;
}

static void
populate_hash_table(const struct rte_hash *h, uint16_t num_entries, struct mempool *value_pool)
{
    int ret;
    uint16_t i;
    uint16_t dst; // destination address
    uint16_t total = 0;
    struct value template = {{{0x98,0x03,0x9b,0x32,0x8d,0xda}}, 0};
    struct value *to_add;
    
    for(i = 0; i < num_entries; i++)
    {
        ret = rte_mempool_get(value_pool, (void **) &to_add);
        if (ret != 0) {
            rte_exit(EXIT_FAILURE, "Unable to get entry from the value pool \n");
        }
        memcpy(to_add, &template, sizeof(struct value));
        dst = (uint16_t)(100+i);

        ret = rte_hash_add_key_data(h, (void *)&dst, (void*)to_add);
        if(ret < 0)
            rte_exit(EXIT_FAILURE, "Unable to add entry %"PRIu16" in the hash table \n", dst);
        else
            total++;  
    }
    printf("Total number of keys added is %"PRIu16"\n", total);
}


/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
        struct rte_eth_rxconf rxconf;
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
        
        rxconf = dev_info.default_rxconf;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), &rxconf, mbuf_pool);
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
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Receiver is Port %u with MAC address: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	if (retval != 0)
		return retval;
        
        /* RX and TX callbacks are added to the ports. 8< */
	rte_eth_add_rx_callback(0, 0, add_timestamps, NULL);
	rte_eth_add_tx_callback(0, 0, calc_latency, NULL);
	/* >8 End of RX and TX callbacks. */

	return 0;
}

static int
lcore_stat(__rte_unused void *arg)
{
    for(; ;)
    {
        sleep(1); // report stats every second
        printf("Number of data packets received %"PRIu64 "\n", rx_count);
        printf("Number of data packets transmitted %"PRIu64 "\n", tx_count);
        printf("Number of control packets received %"PRIu64 "\n", rx_count_control);
    }
}

struct my_message{
    struct rte_ether_hdr eth_hdr;
    uint16_t type;
    uint16_t dst_addr;
    uint32_t seqNo;
    uint64_t T; // timestamp for data update
    uint64_t t; // timestamp for control packet
    char payload[10];
};

struct control_message{
    struct rte_ether_hdr eth_hdr;
    uint16_t type;
    uint16_t dst_addr;
    uint64_t t; // timestamp for control packet
};

struct receive_params{
    struct rte_hash *handle;
    uint16_t port;
    uint16_t queue_id;
};

void my_receive(struct receive_params *p)
{
    int retval;
    struct my_message *my_pkt;
    uint16_t eth_type; 
    rx_count = 0;
    tx_count = 0;
    struct rte_hash *handle = p->handle;
    uint16_t port = p->port;
    struct rte_ether_addr src_mac_addr;
    retval = rte_eth_macaddr_get(port, &src_mac_addr); // get MAC address of Port 0 on node1-1
    uint16_t key;
    struct value *lkp_val;
    
    
    //printf("Measured frequency of counter is %"PRIu64"\n", rte_get_tsc_hz());
    
    printf("\nCore %u receiving data packets. [Ctrl+C to quit]\n", rte_lcore_id());
    
    uint64_t totalcycles = 0;
    uint64_t totalpackets = 0;
    uint64_t totalbatches = 0;
    
    /* Receive maximum of max_packets */
    for(;;){
        /* Get burst of RX packets, from first port of pair. */
        struct rte_mbuf *bufs[BURST_SIZE];
        const uint16_t nb_rx = rte_eth_rx_burst(port, 0,
                        bufs, BURST_SIZE);
        
        if (unlikely(nb_rx == 0))
                continue;
        
        uint64_t now = rte_rdtsc_precise();
        
        for(int i = 0; i < nb_rx; i++)
        {
            my_pkt = rte_pktmbuf_mtod(bufs[i], struct my_message *);
            eth_type = rte_be_to_cpu_16(my_pkt->eth_hdr.ether_type);
            
            if(unlikely(eth_type != PTP_PROTOCOL))
                continue;

            //printf("Packet length %"PRIu32"\n",rte_pktmbuf_pkt_len(bufs[i]));
            rx_count = rx_count + 1;
            key = my_pkt->dst_addr;
            printf("Looking up \n");
            retval = rte_hash_lookup_data(handle, (void*)&key, (void **)&lkp_val);
            printf("Looked up\n");
            if(unlikely(retval < 0)){
                printf("Error looking up for key %"PRIu16"\n", key);
                continue;
            }
            memcpy(&my_pkt->t, &lkp_val->t, sizeof(uint64_t));
            rte_ether_addr_copy(&src_mac_addr, &my_pkt->eth_hdr.s_addr);
            rte_ether_addr_copy(&lkp_val->dest_mac_addr, &my_pkt->eth_hdr.d_addr);
        }
        
        
        uint64_t time_diff = rte_rdtsc_precise() - now;
        totalcycles += time_diff;
        totalpackets += nb_rx;
        totalbatches += 1;
        
        if (totalpackets > (100 * 1000)) {
        printf("Latency = %"PRIu64" cycles %"PRIu64" number\n",
        totalcycles / totalpackets, totalpackets/totalbatches);
        totalcycles = 0;
        totalpackets = 0;
        totalbatches = 0;
        }

        const uint16_t nb_tx = rte_eth_tx_burst(port, 0, bufs, nb_rx);
        tx_count = tx_count + nb_tx;
        
        if(unlikely(nb_tx < nb_rx))
        {
            uint16_t buf;
            for(buf = nb_tx; buf < nb_rx; buf++)
                rte_pktmbuf_free(bufs[buf]);
        }
    }
}


void
receive_control(struct receive_params *p)
{
    int retval;
    struct control_message *ctrl;
    uint16_t eth_type; 
    uint16_t key;
    struct value val;
    
    //printf("Measured frequency of counter is %"PRIu64"\n", rte_get_tsc_hz());
    
    printf("\nCore %u receiving control packets. [Ctrl+C to quit]\n", rte_lcore_id());
    
    struct rte_hash *handle = p->handle;
    uint16_t port = p->port;
    uint16_t rxq = p->queue_id;
    
    for(;;){
        struct rte_mbuf *bufs[CONTROL_BURST_SIZE];
        const uint16_t nb_rx = rte_eth_rx_burst(port, rxq,
                        bufs, CONTROL_BURST_SIZE);
        
        if (unlikely(nb_rx == 0))
                continue;
        
        for(int i = 0; i < nb_rx; i++)
        {
            ctrl = rte_pktmbuf_mtod(bufs[i], struct control_message *);
            eth_type = rte_be_to_cpu_16(ctrl->eth_hdr.ether_type);

            /* Check for control packet of interest and ignore other broadcasts 
             messages */
            if(likely(eth_type == PTP_PROTOCOL))
            {
                //printf("Packet length %"PRIu32"\n",rte_pktmbuf_pkt_len(bufs[i]));
                rx_count_control += 1;
                key = ctrl->dst_addr;
                rte_ether_addr_copy(&ctrl->eth_hdr.s_addr, &val.dest_mac_addr);
                memcpy(&val.t, &ctrl->t, sizeof(uint64_t));
                retval = rte_hash_add_key_data(handle, (void*)&key, (void*)&val);
                if(unlikely(retval < 0)){
                    rte_exit(EXIT_FAILURE, "Unable to add entry %"PRIu16
                            "in the hash table \n", key);
                    continue;
                }      
            }
            rte_pktmbuf_free(bufs[i]);
        }  
    }
}
/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    //struct rte_mempool *value_pool;
    unsigned nb_ports;
    uint16_t portid;
    uint16_t port;
    unsigned lcore_id;
    struct rte_hash * handle;
    unsigned int num_cores;
    size_t mem_size, num_readers;
    struct rte_rcu_qsbr *qv;
    struct rte_hash_rcu_config rcu_cfg = {0};
    struct receive_params data = {handle, 0, 0};
    struct receive_params control = {handle, 1, 0};
    
    struct option lgopts[] = {
    { NULL,  0, 0, 0 }
    };
    int opt, option_index;
    
    static const struct rte_mbuf_dynfield tsc_dynfield_desc = {
        .name = "example_bbdev_dynfield_tsc",
        .size = sizeof(tsc_t),
        .align = __alignof__(tsc_t),
    };

    /* Initialize the Environment Abstraction Layer (EAL). */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
            rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    optind = 1; /* reset getopt lib */
    
    /* Get the number of ports */
    nb_ports = rte_eth_dev_count_avail();

    /* Creates a new mempool in memory to hold the mbufs. */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
            MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    
    value_pool = rte_mempool_create("VALUE_POOL", 65535, sizeof(struct value), MBUF_CACHE_SIZE, 0,
                                    NULL, NULL, NULL, NULL,
                                    rte_socket_id(), MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET);

    if (value_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create value pool\n");
    
    tsc_dynfield_offset = rte_mbuf_dynfield_register(&tsc_dynfield_desc);
    if (tsc_dynfield_offset < 0)
        rte_exit(EXIT_FAILURE, "Cannot register mbuf field\n");

    /* Initialize all ports. */
    RTE_ETH_FOREACH_DEV(portid)
            if (port_init(portid, mbuf_pool) != 0)
                    rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
                                    portid);
    
    /*
     * Check that the port is on the same NUMA node as the polling thread
     * for best performance.
     */
    RTE_ETH_FOREACH_DEV(port)
            if (rte_eth_dev_socket_id(port) > 0 &&
                            rte_eth_dev_socket_id(port) !=
                                            (int)rte_socket_id())
                    printf("WARNING, port %u is on remote NUMA node to "
                                    "polling thread.\n\tPerformance will "
                                    "not be optimal.\n", port);
    
    
    num_readers = -1; // -1 because there is an lcorestat function to be launched on separate core
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
            //enabled_core_ids[num_cores] = lcore_id;
            num_readers++;
    }
    
    num_cores = rte_lcore_count();
    printf("num_cores=%u\n", num_cores);
    // we need to start 3 threads, 1 for writer and 2 for readers
    if (num_cores < num_readers + 1) rte_exit(EXIT_FAILURE, "# of cores has to be %zd or more.\n", num_readers + 1);
    
    // prepare RCU
    mem_size = rte_rcu_qsbr_get_memsize(num_readers);
    printf("The size of the memory required by a Quiescent State variable is %zu\n", mem_size);

    qv = rte_zmalloc("RCU QSBR", mem_size, RTE_CACHE_LINE_SIZE);
    if (!qv) rte_exit(EXIT_FAILURE, "Cannot malloc qv");

    ret = rte_rcu_qsbr_init(qv, num_readers);
    if (ret) rte_exit(EXIT_FAILURE, "Cannot perform qsbr init");

    /* Create and populate hash table*/
    printf("Creating hash table. \n");
    handle = create_hash_table(HASH_ENTRIES+1);
    
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
    
    printf("Populating hash table\n");
    populate_hash_table(handle, HASH_ENTRIES, value_pool);
    
    lcore_id = rte_get_next_lcore(-1, 1, 0);
    if(lcore_id == RTE_MAX_LCORE)
    {
        rte_exit(EXIT_FAILURE, "Slave core id required!");
    }
    rte_eal_remote_launch(lcore_stat, NULL, lcore_id);
    
    
    lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
    if(lcore_id == RTE_MAX_LCORE)
    {
        rte_exit(EXIT_FAILURE, "Slave core id required!");
    }
    //rte_eal_remote_launch((lcore_function_t *)my_receive, &data, lcore_id);
    
    //receive_control(&control);
    
    my_receive(&data);
    
    //rte_eal_mp_wait_lcore();
    
    return 0;
}