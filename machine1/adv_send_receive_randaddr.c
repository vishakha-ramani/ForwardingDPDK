/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <unistd.h>
#include <rte_vhost.h>
#include <stdbool.h>
#include <sys/time.h>
#include <rte_time.h>
#include <rte_random.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS ((64*1024)-1)
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 256
#define CONTROL_BURST_SIZE 1
#define PTP_PROTOCOL 0x88F7
#define HASH_ENTRIES 1024
uint64_t rx_count;
uint64_t startTime;
uint64_t cyclesSum;
uint64_t ctrlnow[HASH_ENTRIES];
uint64_t data_sent;
uint64_t control_sent;
uint16_t num_batches;


static struct rte_mempool *mbuf_pool;

static const struct rte_eth_conf port_conf_default = {
        .rxmode = {
                .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
        },
};

struct send_params{
    struct rte_mempool *mbuf_pool;
    uint16_t port;
    uint16_t queue_id;
    uint64_t max_packets;
};


/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
        struct rte_eth_conf port_conf = port_conf_default;
        const uint16_t rx_rings = 2, tx_rings = 2; //number of rx_rings and tx_rings changed from 1 to 2. 
        uint16_t nb_rxd = RX_RING_SIZE;
        uint16_t nb_txd = TX_RING_SIZE;
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
        struct rte_ether_addr addr;
        retval = rte_eth_macaddr_get(port, &addr);
        if (retval != 0)
                return retval;

        printf("Sender is Port %u with MAC address: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                        port,
                        addr.addr_bytes[0], addr.addr_bytes[1],
                        addr.addr_bytes[2], addr.addr_bytes[3],
                        addr.addr_bytes[4], addr.addr_bytes[5]);

        /* Enable RX in promiscuous mode for the Ethernet device. */
        retval = rte_eth_promiscuous_enable(port);
        if (retval != 0)
                return retval;

        return 0;
}

static int
lcore_stat(__rte_unused void *arg)
{
    for(; ;)
    {
        sleep(1); // report stats every second
        printf("Number of \033[;32mdata\033[0m packets received %"PRIu64 "\n", rx_count);
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

void my_receive()
{
    int retval;
    struct my_message *my_pkt;
    uint16_t eth_type; 
    rx_count = 0; 
    uint64_t now;
    uint64_t addrTime = 0;
    uint64_t totalcycles = 0;
    uint64_t totalpackets = 0;
    uint64_t totalbatches = 0;
    uint16_t destAddr;
    
    //printf("Measured frequency of receive machine is %"PRIu64"\n", rte_get_tsc_hz());
    
    printf("\nCore %u receiving packets. [Ctrl+C to quit]\n", rte_lcore_id());
    
    /* Run until the application is quit or killed. */
    for (;;) {
        
        /* Get burst of RX packets, from first port of pair. */
        struct rte_mbuf *bufs[BURST_SIZE];
        uint16_t nb_rx = rte_eth_rx_burst(0, 0,
                        bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
                continue;
        
        /* Get the current timestamp for this received batch*/
        now = rte_rdtsc_precise();
        
        
        for(int i = 0; i < nb_rx; i++)
        {
            my_pkt = rte_pktmbuf_mtod(bufs[i], struct my_message *);
            eth_type = rte_be_to_cpu_16(my_pkt->eth_hdr.ether_type);
            
            /* Check for data packet of interest and ignore other broadcasts 
             messages */
            if(unlikely(eth_type != PTP_PROTOCOL))
                continue;
            
            destAddr = my_pkt->dst_addr;
            if(unlikely(my_pkt->t != ctrlnow[100-destAddr]))
                continue;
            rx_count = rx_count + 1;
            /* get timestamp of address update*/
            //addrTime += now - my_pkt->t;
            //printf("Address timestamp looked up at forwarder %"PRIu64"\n", my_pkt->t);
            totalcycles += now - my_pkt->T;
            totalpackets += 1;
            
            rte_pktmbuf_free(bufs[i]);
        }
        
        totalbatches += 1;
        if (totalpackets > (100 * 1000)) {
            printf("Latency = %"PRIu64" cycles %"PRIu64" pkts per batch\n",
            totalcycles / totalpackets, totalpackets/totalbatches);
            totalcycles = 0;
            totalpackets = 0;
            totalbatches = 0;
        }
    }
}

static int
my_send(struct send_params *p)
{
    int retval;
    uint64_t now;
    
    struct rte_mempool *mbuf_pool = p->mbuf_pool;
    uint16_t port = p->port;
    uint16_t queue_id = p->queue_id;
    uint64_t max_packets = p->max_packets;

    uint32_t seq_num = 0;
    struct rte_mbuf *bufs[BURST_SIZE];
    struct rte_ether_addr src_mac_addr;
    retval = rte_eth_macaddr_get(port, &src_mac_addr); // get MAC address of Port 0 on node1-1
    struct rte_ether_addr dst_mac_addr = {{0xb8,0x59,0x9f,0xdd,0xbd,0x94}}; //MAC address 98:03:9b:32:7d:32 //b8:59:9f:dd:bd:94
    struct my_message *my_pkt;
    
    uint16_t rand;
    data_sent=0;
    uint16_t sent_packets = BURST_SIZE;

    
    //printf("Measured frequency of data packet sending machine is %"PRIu64"\n", rte_get_tsc_hz());


    do{
        /* Adding same timestamp to a batch of packets*/
        now = rte_rdtsc_precise();
        
        rand = 100 + rte_rand()%(HASH_ENTRIES-100+1);
        
        for(int i = 0; i < sent_packets; i ++)
        {
            seq_num = seq_num + 1;
            //rand = 100 + rte_rand()%(HASH_ENTRIES-100+1);
            bufs[i] = rte_pktmbuf_alloc(mbuf_pool);
            if (unlikely(bufs[i] == NULL)) {
                printf("Couldn't "
                    "allocate memory for mbuf.\n");
                break;
            }
            my_pkt = rte_pktmbuf_mtod(bufs[i], struct my_message*);
            memcpy(my_pkt->payload, "Hello2021", 10);
            my_pkt->payload[9] = 0; // ensure termination 
            
            /* Adding data packet fields*/
            my_pkt->T = now; //add timestamp to data packet
            my_pkt->t = 0; // empty timestamp of control packet
            my_pkt->seqNo = seq_num;
            //my_pkt->dst_addr = 101;
            my_pkt->dst_addr = rand;
            my_pkt->type = 1;

            rte_ether_addr_copy(&src_mac_addr, &my_pkt->eth_hdr.s_addr);
            rte_ether_addr_copy(&dst_mac_addr, &my_pkt->eth_hdr.d_addr);
            my_pkt->eth_hdr.ether_type = htons(PTP_PROTOCOL);
            int pkt_size = sizeof(struct my_message);
            bufs[i]->pkt_len = pkt_size;
            bufs[i]->data_len = pkt_size;
            //printf("Sent \033[;32mdata\033[0m packets with destination address %"PRIu32"\n", rand);
        }

        sent_packets = rte_eth_tx_burst(port, queue_id, bufs, BURST_SIZE);
        data_sent = data_sent + sent_packets;
        num_batches+=1;
        //rte_delay_ms(1);
        //printf("Sent data packets with destination address %"PRIu32"\n", rand);
    }
    while(data_sent < max_packets);
    
    printf("\nNumber of \033[;32mdata\033[0m packets transmitted by logical core % "PRId64 " is %"PRId64 "\n", rte_lcore_id(), data_sent);
    
        /* Free any unsent packets. */
    if (unlikely(sent_packets < BURST_SIZE)) {
            uint16_t buf;
            for (buf = sent_packets; buf < BURST_SIZE; buf++)
                    rte_pktmbuf_free(bufs[buf]);
        }
    
    return 0;
}

/* send control packets*/
static int
send_control(struct send_params *p)
{
    int retval;
    struct rte_mempool *mbuf_pool = p->mbuf_pool;
    uint16_t port = p->port;
    uint16_t queue_id = p->queue_id;
    uint64_t max_packets = p->max_packets;
    
    struct rte_mbuf *bufs[CONTROL_BURST_SIZE];
    struct rte_ether_addr src_mac_addr;
    retval = rte_eth_macaddr_get(port, &src_mac_addr); // get MAC address of Port 0 on node1-1
    struct rte_ether_addr dst_mac_addr = {{0xb8,0x59,0x9f,0xdd,0xbd,0x95}}; //MAC address 98:03:9b:32:7d:33 // b8:59:9f:dd:bd:95
    struct control_message *ctrl;
    uint64_t ctrlTS; //timestamp of control packet (ctrlnow tracks the TS for each address)
    
    //printf("Measured frequency of control packet sending machine is %"PRIu64"\n", rte_get_tsc_hz());
    
    uint16_t rand;
    //printf("Random number generated is %"PRIu32"\n", rand);
    control_sent=0;
    uint16_t sent_packets = CONTROL_BURST_SIZE;
    do{        
        //rand = 100 + rte_rand()%10;
        ctrlTS = rte_rdtsc();
        rand = 100 + rte_rand()%(HASH_ENTRIES-100+1);
        while(num_batches%4)
            rte_pause();
        
        for(int i = 0; i < sent_packets; i ++)
        {
            bufs[i] = rte_pktmbuf_alloc(mbuf_pool);
            if (unlikely(bufs[i] == NULL)) {
                printf("Couldn't "
                    "allocate memory for mbuf.\n");
                break;
            }
            ctrl = rte_pktmbuf_mtod(bufs[i], struct control_message*);
            ctrl->type = 2;
            ctrl->t = ctrlTS;
            //rand = 100 + rte_rand()%(HASH_ENTRIES-100+1);
            ctrl->dst_addr = rand;
            ctrlnow[rand-100] = ctrlTS;
            //ctrl->dst_addr = 101;
            rte_ether_addr_copy(&src_mac_addr, &ctrl->eth_hdr.s_addr);
            rte_ether_addr_copy(&dst_mac_addr, &ctrl->eth_hdr.d_addr);
            ctrl->eth_hdr.ether_type = htons(PTP_PROTOCOL);
            int pkt_size = sizeof(struct control_message);
            bufs[i]->pkt_len = pkt_size;
            bufs[i]->data_len = pkt_size;
            //printf("Sent \033[;35mcotrol\033[0mtimestamp %"PRIu64" for destination %"PRIu32"\n", ctrlTS, rand);
        }
        sent_packets = rte_eth_tx_burst(port, queue_id, bufs, CONTROL_BURST_SIZE);
//        if (sent_packets!=0)
//            printf("Sent \033[;35mcotrol\033[0m timestamp %"PRIu64" for destination %"PRIu32"\n", ctrlTS, rand);
        control_sent = control_sent + sent_packets;
        rte_delay_ms(1);
    }
    while(control_sent < max_packets);
    
    printf("\nNumber of control packets transmitted by logical core % "PRId64 " is %"PRId64 "\n", rte_lcore_id(), control_sent);
    
    /* Free any unsent packets. */
    if (unlikely(sent_packets < CONTROL_BURST_SIZE)) {
            uint16_t buf;
            for (buf = sent_packets; buf < CONTROL_BURST_SIZE; buf++)
                    rte_pktmbuf_free(bufs[buf]);
        }
    
    return 0;
}


/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
    unsigned nb_ports;
    uint16_t portid;
    uint16_t port;
//    uint64_t num_data = BURST_SIZE*4; 
//    uint64_t num_control = CONTROL_BURST_SIZE*4;
    uint64_t num_data = BURST_SIZE*4096; // = 1048576
    uint64_t num_control = CONTROL_BURST_SIZE*100000; // number of control updates
    unsigned lcore_id;

    /* Initialize the Environment Abstraction Layer (EAL). */
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
            rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    nb_ports = rte_eth_dev_count_avail();
    printf("Number of ports available %"PRIu16 "\n", nb_ports);
    
    /* Creates a new mempool in memory to hold the mbufs. */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
            MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

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
    

    struct send_params data2 = {mbuf_pool, 0, 0, num_data};
    struct send_params control = {mbuf_pool, 1, 0, num_control};

    /* call my_send function on another lcore*/
    lcore_id = rte_get_next_lcore(-1, 1, 0);
    if(lcore_id == RTE_MAX_LCORE)
    {
        rte_exit(EXIT_FAILURE, "Slave core id required!");
    }
    rte_eal_remote_launch((lcore_function_t *)my_send, &data2, lcore_id); //on lcore 4
    
    
    lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
    if(lcore_id == RTE_MAX_LCORE)
    {
        rte_exit(EXIT_FAILURE, "Slave core id required!");
    }
    //rte_eal_remote_launch((lcore_function_t *)send_control, &control, lcore_id); //on lcore 6
    
    /* call lcore stat on another lcore*/
    lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
    if(lcore_id == RTE_MAX_LCORE)
    {
        rte_exit(EXIT_FAILURE, "Slave core id required!");
    }
    rte_eal_remote_launch(lcore_stat, NULL, lcore_id); //on lcore 8
   
    my_receive(); //on lcore2
    return 0;
}