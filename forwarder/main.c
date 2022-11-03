#include "common.h"
#include <signal.h>

int data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size;
int control_receive_burst_size;//, control_tx_ring_size, control_rx_ring_size;
struct rte_ether_addr receiver_data_mac;
char *forward_list_filename;
long long control_packet_count;
struct rte_mempool *fib_entry_pool;
struct rte_hash *fib;
struct rte_ring *data_receive_ring, *data_send_ring, *control_receive_ring;

#ifdef TEST_RCU
struct rte_rcu_qsbr *qs_variable;
//#ifndef TEST_RCU_CONSTRAINED
mem_event_t *mem_events;
size_t mem_event_pos, mem_event_capacity;
//#endif // TEST_RCU_CONSTRAINED
#else // TEST_RCU
rte_rwlock_t rw_lock;
#endif // TEST_RCU

static int rx_timestamp_dynfield_offset;
static uint16_t port_id_data; //, port_id_control;
static struct rte_ether_addr my_data_mac; //, my_control_mac;
static control_packet_stat_t *results;
static volatile uint64_t received_control_count = 0, received_data_count = 0,
        rx_dropped_data_count = 0, tx_dropped_data_count = 0, tx_out_dropped_data_count = 0,
        rx_dropped_control_count =0, other_packet_count = 0, my_tx_count = 0, tx_batches = 0;
static volatile bool running = true;

int
parse_args(int argc, char **argv);

int
parse_fib(void);


static inline uint64_t *
rx_timestamp_field(struct rte_mbuf *mbuf) {
    return RTE_MBUF_DYNFIELD(mbuf, rx_timestamp_dynfield_offset, uint64_t * );
}

static void sigintHandler(int sig)
{
    RTE_SET_USED(sig);
    running = false;
}


static uint16_t
rx_callback(uint16_t port_id,
            uint16_t queue,
            struct rte_mbuf *pkts[], uint16_t nb_pkts,
            uint16_t max_pkts,
            void *user_param) {
    RTE_SET_USED(port_id);
    RTE_SET_USED(queue);
    RTE_SET_USED(max_pkts);
    RTE_SET_USED(user_param);


    uint16_t i;
    uint64_t now;

    now = rte_rdtsc_precise();
    // adds a timestamp in the dynamic field
    for (i = 0; i < nb_pkts; i++)
        *rx_timestamp_field(pkts[i]) = now;

    return nb_pkts;
}

static uint16_t
data_tx_callback(uint16_t port_id,
                 uint16_t queue,
                 struct rte_mbuf *pkts[], uint16_t nb_pkts,
                 void *user_param) {
    RTE_SET_USED(port_id);
    RTE_SET_USED(queue);
    RTE_SET_USED(user_param);

    uint16_t i;
    uint64_t now;
    data_pkt_t * header;

    now = rte_rdtsc_precise();
    for (i = 0; i < nb_pkts; i++) {
        // we will only send data packets, safe to cast directly
        header = rte_pktmbuf_mtod(pkts[i], data_pkt_t * );
        header->time_exit_f = now;
    }
    return nb_pkts;
}


static int receive_data_thread(void *param) {
    RTE_SET_USED(param);

    struct rte_mbuf *buf_rx[data_receive_burst_size];
    uint16_t nb_rx, nb_to_data, nb_to_control, nb_free, i;
    //-----//
    uint16_t nb_tx, nb_data_rx;
    uint64_t batches = 0;
    //-------//
    uint16_t nb_control_sent, nb_data_sent, diff;
    data_pkt_t *header;
    uint16_t ether_type_control = ETHER_TYPE_CONTROL, ether_type_data = ETHER_TYPE_DATA;
    struct rte_ether_addr src_mac_addr;
    int retval = rte_eth_macaddr_get(0, &src_mac_addr);
    struct rte_ether_addr dst_mac_addr;
  //  static uint16_t my_rx[239982200] = {0};
   // static uint16_t my_tx[239982200] = {0};

    printf("\nCore %u receiving data packets.\n", rte_lcore_id());
	
    while(running) {
        nb_rx = rte_eth_rx_burst(port_id_data, 0, buf_rx, data_receive_burst_size);
        if (unlikely(!nb_rx)) continue;
        nb_to_data = nb_to_control = nb_free = 0;
        for (i = 0; i < nb_rx; i++) {
            header = rte_pktmbuf_mtod(buf_rx[i], data_pkt_t * );
            dst_mac_addr = header->common_header.ether.s_addr;
            if (likely(header->common_header.ether.ether_type == ether_type_data)){
                rte_ether_addr_copy(&src_mac_addr, &header->common_header.ether.s_addr);
                rte_ether_addr_copy(&dst_mac_addr, &header->common_header.ether.d_addr);
                header->time_control = 0; //entry->control_time;
                header->time_control_arrive_f = 0;//entry->control_arrive_time_f;
                header->common_header.time_send = *rx_timestamp_field(buf_rx[i]); // reuse time_send for time_arrive_f
#ifdef WRITE_TIME_AFTER_LOOKUP_F
                header->time_after_lookup_f = rte_rdtsc_precise();
#endif
                received_data_count++;
                nb_to_data++;
              }
            else {
                other_packet_count++;
		nb_free++;
            }
        }
        nb_tx = rte_eth_tx_burst(port_id_data, 0, buf_rx, nb_rx);
//	my_rx[batches] = nb_rx;
//	my_tx[batches] = nb_tx;
    	if(unlikely(nb_tx < nb_rx))
        {
		rte_pktmbuf_free_bulk(buf_rx + nb_tx, nb_rx-nb_tx);
           // uint16_t buf;
            //for(buf = nb_tx; buf < nb_rx; buf++)
             //   rte_pktmbuf_free(buf_rx[buf]);
        }
//	//my_tx_count = my_tx_count + nb_tx - nb_free;
//	batches++;
//	if(unlikely(received_data_count == 47996440))
//		break;
    }
    printf("Core %u (data receiver) finished!\n", rte_lcore_id());
    printf("batches: %"PRIu64"\n", batches);
//    for (int i = 0; i < batches; i++){
 //       printf("my_rx: %"PRIu16" my_tx: %"PRIu16"\n", my_rx[i], my_tx[i]);
  //  }
    return 0;
}

static int send_data_thread(void *param) {
    RTE_SET_USED(param);

    uint16_t nb_rx, nb_tx, diff;
    struct rte_mbuf *bufs[data_send_burst_size];
    uint64_t first_failed_try = 0, now;
    uint64_t tx_burst_period_cycles;

    tx_burst_period_cycles = rte_get_timer_hz() * TX_BURST_PERIOD_US / 1000000;
    printf("\nCore %u sending data packets, tx_burst_period_cycles=%"PRIu64".\n", rte_lcore_id(), tx_burst_period_cycles);

    while(running) {
        nb_rx = rte_ring_dequeue_bulk(data_send_ring, (void **) bufs, data_send_burst_size, NULL);
        if (unlikely(!nb_rx)) { // not enought packets to send
            now = rte_rdtsc_precise();
            if (first_failed_try == 0) { // this is the first failed try
                first_failed_try = now;
                continue;
            } else if (now - first_failed_try >=
                       tx_burst_period_cycles) { // waited long enough, try to send whatever is there
                nb_rx = rte_ring_dequeue_burst(data_send_ring, (void **) bufs, data_send_burst_size, NULL);
                first_failed_try = 0;
                if (unlikely(!nb_rx)) continue; // still no packets to send, do nothing
                // else, let through and send
            } else { continue; } // wait a bit longer
        } else { first_failed_try = 0; } // has enough packets to send
        nb_tx = rte_eth_tx_burst(port_id_data, 0, bufs, nb_rx);
        diff = nb_rx - nb_tx;
        tx_out_dropped_data_count += diff;
        if (unlikely(diff)) rte_pktmbuf_free_bulk(bufs + nb_tx, diff);
    }
    printf("Core %u (data sender) finished!\n", rte_lcore_id());

    return 0;
}

static int forward_data_thread(void *params) {
    RTE_SET_USED(params);

    uint16_t nb_rx, nb_tx, nb_send, nb_free, i, diff;
    struct rte_mbuf *bufs[data_receive_burst_size], *to_free[data_receive_burst_size];
    data_pkt_t * header;
    unsigned  lcore_id = rte_lcore_id();
    int ret;

    printf("\nCore %u forwarding data packets.\n", lcore_id);
#ifdef TEST_RCU
    rte_rcu_qsbr_thread_register(qs_variable, lcore_id);
    rte_rcu_qsbr_thread_online(qs_variable, lcore_id);
#endif

    while(running) {
#if defined(TEST_RCU) && !defined(TEST_RCU_PER_PACKET_QUIESCENT)
        rte_rcu_qsbr_quiescent(qs_variable, lcore_id);
#endif
        nb_rx = rte_ring_dequeue_burst(data_receive_ring, (void **) bufs, data_receive_burst_size, NULL);
        if (unlikely(!nb_rx)){
#if defined(TEST_RCU) && defined(TEST_RCU_PER_PACKET_QUIESCENT)
            rte_rcu_qsbr_quiescent(qs_variable, lcore_id);
#endif
            continue;
        }

        nb_send = nb_free = 0;
        for (i = 0; i < nb_rx; i++) {
#if defined(TEST_RCU) && defined(TEST_RCU_PER_PACKET_QUIESCENT)
            rte_rcu_qsbr_quiescent(qs_variable, lcore_id);
#endif
            header = rte_pktmbuf_mtod(bufs[i], data_pkt_t * );
            ret = handle_data_packet(header, lcore_id);
            if (likely(!ret)) {

                rte_ether_addr_copy(&my_data_mac, &header->common_header.ether.s_addr);
                header->common_header.time_send = *rx_timestamp_field(bufs[i]); // reuse time_send for time_arrive_f
#ifdef WRITE_TIME_AFTER_LOOKUP_F
                header->time_after_lookup_f = rte_rdtsc_precise();
#endif
                received_data_count++;
                bufs[nb_send++] = bufs[i];
            } else {
                // failed to process the data packet
                to_free[nb_free++] = bufs[i];
                other_packet_count++;
            }
        }
        if (likely(nb_send)) {
            nb_tx = rte_ring_enqueue_burst(data_send_ring, (void **) bufs, nb_send, NULL);
            diff = nb_send - nb_tx;
            tx_dropped_data_count += diff;
            if (unlikely(diff)) rte_pktmbuf_free_bulk(bufs + nb_tx, nb_send - nb_tx);
        }
        if (unlikely(nb_free)) rte_pktmbuf_free_bulk(to_free, nb_free);
    }
    printf("Core %u (data forwarder) finished!\n", lcore_id);

    return 0;
}

static int control_thread(void *params) {
    RTE_SET_USED(params);

    struct rte_mbuf *bufs[control_receive_burst_size];
    uint16_t nb_rx, i;
    control_pkt_t *header;
    control_packet_stat_t *tmp_result;

    printf("\nCore %u receiving data packets.\n", rte_lcore_id());
    tmp_result = results;


    while(running) {
        nb_rx = rte_ring_dequeue_burst(control_receive_ring, (void **) bufs, control_receive_burst_size, NULL);
        if (unlikely(!nb_rx)) continue;

        for (i = 0; i < nb_rx; i++) {
            header = rte_pktmbuf_mtod(bufs[i], control_pkt_t * );
            if (unlikely(received_control_count >= (uint64_t)control_packet_count)) {
                running = false;
                rte_exit(EXIT_FAILURE, "Too many control packets! Consider increasing control_packet_count in the param.\n");
            }
            tmp_result->node_id = header->common_header.dst_addr;
            tmp_result->seq = header->common_header.seq;
            tmp_result->control_time = header->common_header.time_send;
            tmp_result->control_arrive_time_f = *rx_timestamp_field(bufs[i]);
//                printf("node_id=%"PRIu16" seq=%"PRIu32" control_time=%"PRIu64 " arrive_f=%"PRIu64"\n",
//                        rte_cpu_to_be_16(tmp_result->node_id),
//                        tmp_result->seq,
//                        tmp_result->control_time,
//                        tmp_result->control_arrive_time_f);
            update_fib_entry(tmp_result);

            tmp_result++;
            received_control_count++;
        }
        rte_pktmbuf_free_bulk(bufs, nb_rx);
    }
    printf("Core %u (data receiver) finished!\n", rte_lcore_id());
    return 0;
}

static inline void report_status() {
    uint64_t start_time_cycles = rte_rdtsc_precise();
    while(running) {
        uint64_t time_cycles = rte_rdtsc_precise();
        printf("[%14"PRIu64"] rx_ctrl=%zd rx_data=%zd rx_data_drop=%zd rx_ctrl_drop=%zd sum=%zd tx_drop=%zd tx_drop2=%zd other_pkt=%zd, my_tx_count=%zd, tx_batches=%zd\n",
                time_cycles - start_time_cycles,
                received_control_count, received_data_count,
                rx_dropped_data_count, rx_dropped_control_count,
                received_control_count + received_data_count + rx_dropped_data_count + rx_dropped_control_count,
                tx_dropped_data_count, tx_out_dropped_data_count,
                other_packet_count, my_tx_count, tx_batches);
        rte_delay_ms(REPORT_WAIT_MS);
    }
    printf("Core %u (status reporter) finished!\n", rte_lcore_id());
}

static inline void write_results() {
    uint64_t i;
    control_packet_stat_t *stat = results;
    uint64_t total_publish_delay = 0;

#ifdef RESULT_PACKETS_FILENAME
    FILE *output;

    output = fopen(RESULT_PACKETS_FILENAME, "w");
    if (unlikely(!output))
        rte_exit(EXIT_FAILURE, "Cannot open result file: " RESULT_PACKETS_FILENAME "\n");
#endif

    for (i = 0; i < received_control_count; i++, stat++) {
#ifdef RESULT_PACKETS_FILENAME
        fprintf(output, "%"PRIu32" %"PRIu16" %"PRIu64" %"PRIu64" %"PRIu64"\n",
                stat->seq,
                rte_be_to_cpu_16(stat->node_id),
                stat->control_time,
                stat->control_arrive_time_f,
                stat->publish_time_f);
#endif
        total_publish_delay += stat->publish_time_f - stat->control_arrive_time_f;
    }
#ifdef RESULT_PACKETS_FILENAME
    fflush(output);
    fclose(output);
    printf("File %s finished!\n", RESULT_PACKETS_FILENAME);
#endif
    printf("publish_delay=%.6f\n", ((double)total_publish_delay) / received_control_count);

//#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)
#ifdef TEST_RCU
    FILE *output_mem;
    output_mem = fopen(RESULT_RCU_U_FILENAME, "w");
    if (unlikely(!output_mem))
        rte_exit(EXIT_FAILURE, "Cannot open rcu mem events file: " RESULT_RCU_U_FILENAME "\n");

    for (i = 0; i < mem_event_pos; i++)
        fprintf(output_mem, "%c %zd\n", mem_events[i].type, mem_events[i].value);
    fflush(output_mem);
    fclose(output_mem);

#endif

}

int
main(int argc, char *argv[]) {
    int ret;
    uint16_t nb_ports;
    unsigned nb_lcores, receive_lcore_id, forward_lcore_id, send_lcore_id, control_lcore_id;
    struct rte_mempool *mbuf_pool_data_rx, *mbuf_pool_control_rx;

    static const struct rte_mbuf_dynfield rx_timestamp_dynfield_desc = {
            .name = "rx_timestamp",
            .size = sizeof(uint64_t),
            .align = __alignof__(uint64_t),
    };

    /* Initialize the Environment Abstraction Layer (EAL). */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    argc -= ret;
    argv += ret;

    ret = parse_args(argc, argv);
    if (unlikely(ret < 0))
        rte_exit(EXIT_FAILURE, "Error with parse args\n");
    printf("\n");

    /* Make sure that there are at least 2 ports available */
    nb_ports = rte_eth_dev_count_avail();
    printf("Number of ports available %"PRIu16"\n", nb_ports);
    if (unlikely(nb_ports < 1))
        rte_exit(EXIT_FAILURE, "Must have at least 1 port, for data and control\n");

    /* Make sure that there are at least 5 lcores available */
    nb_lcores = rte_lcore_count();
    printf("Number of lcores available %u\n", nb_lcores);
    if (unlikely(nb_lcores < 5))
        rte_exit(EXIT_FAILURE,
                 "Must have at least 5 cores, 1 for receive, 1 for data plane process, 1 for control plane process, 1 for (data plane) send, and 1 for stat\n");
    printf("\n");

    // prepare fib and related data structures
    parse_fib();
    printf("\n");

    if (unlikely(signal(SIGINT, sigintHandler) == SIG_ERR))
        rte_exit(EXIT_FAILURE, "Cannot register signal SIGINT handler.\n");

    /* Initialize space for results */
    results = rte_malloc("RESULTS", sizeof(control_packet_stat_t) * control_packet_count, sizeof(void *));
    if (unlikely(!results))
        rte_exit(EXIT_FAILURE, "Failed in creating results\n");

//#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED) // only calculate for rcu_u
#ifdef TEST_RCU
    mem_event_capacity = control_packet_count * 3;
    mem_event_pos = 0;
    mem_events = rte_malloc("EVENTS", sizeof(mem_event_t) * mem_event_capacity, sizeof(void *)); // per control has 1 add, 0/1 free (for the previous entry) and 1 time event
    if (unlikely(!mem_events))
        rte_exit(EXIT_FAILURE, "Failed in creating mem_events\n");
#endif

    /* Initialize data packet pool for rx */
    mbuf_pool_data_rx = rte_pktmbuf_pool_create("MBUF_POOL_DATA_RX", RX_POOL_SIZE,
                                                MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (unlikely(!mbuf_pool_data_rx))
        rte_exit(EXIT_FAILURE, "Failed in creating mbuf_pool_data_rx\n");

    /* Initialize control packet pool for rx */
    mbuf_pool_control_rx = rte_pktmbuf_pool_create("MBUF_POOL_CTRL_RX", RX_POOL_SIZE,
                                                   MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (unlikely(!mbuf_pool_control_rx))
        rte_exit(EXIT_FAILURE, "Failed in creating mbuf_pool_control_rx\n");

    /* Initialize ring between rx and data process */
    data_receive_ring = rte_ring_create("RING_DATA_RX", DATA_RECEIVE_RING_SIZE, rte_socket_id(),
                                        RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (unlikely(!data_receive_ring))
        rte_exit(EXIT_FAILURE, "Failed in creating data_receive_ring\n");

    /* Initialize ring between rx and control process */
    control_receive_ring = rte_ring_create("RING_CONTROL_RX", CONTROL_RECEIVE_RING_SIZE, rte_socket_id(),
                                        RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (unlikely(!control_receive_ring))
        rte_exit(EXIT_FAILURE, "Failed in creating control_receive_ring\n");

    /* Initialize ring between data process and data tx */
    data_send_ring = rte_ring_create("RING_DATA_TX", DATA_SEND_RING_SIZE, rte_socket_id(),
                                     RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (unlikely(!data_send_ring))
        rte_exit(EXIT_FAILURE, "Failed in creating data_send_ring\n");

    /* register dynamic field rx_timestamp */
    rx_timestamp_dynfield_offset = rte_mbuf_dynfield_register(&rx_timestamp_dynfield_desc);
    if (rx_timestamp_dynfield_offset < 0)
        rte_exit(EXIT_FAILURE, "Cannot register mbuf rx_timestamp_dynfield_offset\n");


    /* Initialize data port */
    port_id_data = rte_eth_find_next_owned_by(0, RTE_ETH_DEV_NO_OWNER);
    if (unlikely(port_id_data >= RTE_MAX_ETHPORTS))
        rte_exit(EXIT_FAILURE, "Cannot get data port\n");
    port_init(port_id_data, mbuf_pool_data_rx, data_tx_ring_size, data_rx_ring_size, &my_data_mac);
    rte_eth_add_tx_callback(port_id_data, 0, data_tx_callback, NULL);
    rte_eth_add_rx_callback(port_id_data, 0, rx_callback, NULL);
    printf("\n");

    // start data send lcore
    send_lcore_id = rte_get_next_lcore(-1, 1, 0);
    if (unlikely(send_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Send core required!\n");
    //ret = rte_eal_remote_launch(send_data_thread, NULL, send_lcore_id);
    // if (unlikely(ret))
    //     rte_exit(EXIT_FAILURE, "Failed to launch send_data_thread\n");

    // start data process lcore
    forward_lcore_id = rte_get_next_lcore(send_lcore_id, 1, 0);
    if (unlikely(forward_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Forward core required!\n");
    // ret = rte_eal_remote_launch(forward_data_thread, NULL, forward_lcore_id);
    // if (unlikely(ret))
    //     rte_exit(EXIT_FAILURE, "Failed to launch forward_data_thread\n");

    // start control process lcore
    control_lcore_id = rte_get_next_lcore(forward_lcore_id, 1, 0);
    if (unlikely(control_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Control core required!\n");
    // ret = rte_eal_remote_launch(control_thread, NULL, control_lcore_id);
    // if (unlikely(ret))
    //     rte_exit(EXIT_FAILURE, "Failed to launch control_thread\n");

    // start data receive lcore
    receive_lcore_id = rte_get_next_lcore(control_lcore_id, 1, 0);
    if (unlikely(receive_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Receive core required!\n");
    ret = rte_eal_remote_launch(receive_data_thread, NULL, receive_lcore_id);
    if (unlikely(ret))
        rte_exit(EXIT_FAILURE, "Failed to launch receive_data_thread\n");

    report_status();
    rte_eal_mp_wait_lcore();
    write_results();

#if defined(TEST_RCU) && !defined(TEST_RCU_PER_PACKET_QUIESCENT)
    printf("TEST_RCU_PER_BATCH_QUIESCENT\n");
#endif
#if defined(TEST_RCU) && defined(TEST_RCU_PER_PACKET_QUIESCENT)
    printf("TEST_RCU_PER_PACKET_QUIESCENT\n");
#endif
#if defined(TEST_RCU) && !defined(TEST_RCU_CONSTRAINED)
    printf("TEST_RCU_UNCONSTRAINED\n");
#endif
#if defined(TEST_RCU) && defined(TEST_RCU_CONSTRAINED)
    printf("TEST_RCU_CONSTRAINED\n");
#endif

}
