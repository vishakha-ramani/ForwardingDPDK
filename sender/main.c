#include "common.h"
#include <stdio.h>

int parse_args(int argc, char **argv);

int parse_trace(void);

int data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size;
int control_tx_ring_size, control_rx_ring_size;
size_t data_packet_count, control_packet_count, total_packet_count;
char *trace_filename;
int trace_repeat;
struct rte_ether_addr my_data_mac, my_control_mac, forwarder_data_mac, forwarder_control_mac;
struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
struct rte_mbuf **trace_packets;
packet_stat_t *results;
#ifdef RATE_CONTROL
uint64_t cycles_per_packet;
#endif


static struct rte_mempool *mbuf_pool_data_rx;
static uint16_t port_id_data;
static uint64_t start_time_cycles;

static volatile bool sending = true;
static volatile int final_wait = WAIT_AFTER_FINISH_MS;
static volatile uint64_t sent_packets = 0, remaining_send = 0;
static volatile uint64_t receive_error_type = 0, receive_error_seq = 0, receive_redundant = 0, receive_data = 0 ;
static volatile uint64_t batches_at_tx_callback = 0;
#ifdef RATE_CONTROL
static volatile uint64_t time_last_sent = 0;
#endif

static inline
void calculate_statistics() {
    uint64_t data_tx_count = 0;
    uint64_t data_rtt_sum = 0, data_on_forwarder_sum = 0, data_rx_count = 0;
    uint64_t i;
#ifdef RESULT_FILENAME
    FILE *output;
#endif
    packet_stat_t *stat = results;

#ifdef RESULT_FILENAME
    output = fopen(RESULT_FILENAME, "w");
#endif
    for (i = 0; i < total_packet_count; i++, stat++) {
        if (stat->is_control) {
#ifdef RESULT_FILENAME
            fprintf(output, "1 %"PRIu16" %zd\n",
                    rte_be_to_cpu_16(stat->dst_id),
                    stat->time_send);
#endif
        } else {
            data_tx_count++;
            if (stat->data.time_receive) { // data received
                data_rtt_sum += stat->data.time_receive - stat->time_send;
                data_on_forwarder_sum += stat->data.time_exit_f - stat->data.time_arrive_f;
                data_rx_count++;
            }
#ifdef RESULT_FILENAME
            fprintf(output, "0 %"PRIu16" %zd %zd %zd %zd %zd %zd %zd\n",
                    rte_be_to_cpu_16(stat->dst_id),
                    stat->time_send, stat->data.time_receive,
                    stat->data.time_control, stat->data.time_control_arrive_f,
                    stat->data.time_arrive_f, stat->data.time_after_lookup_f,
                    stat->data.time_exit_f);
#endif
        }
    }
#ifdef RESULT_FILENAME
    fflush(output);
    fclose(output);
    printf("File %s finished!\n", RESULT_FILENAME);
#endif
    printf("data_rtt_sum=%zd data_on_forwarder_sum=%zd data_rx_count=%zd data_tx_count=%zd\n",
           data_rtt_sum, data_on_forwarder_sum, data_rx_count, data_tx_count);
    printf("data_rtt=%.6f data_on_forwarder=%.6f\n",
           ((double) data_rtt_sum) / data_rx_count,
           ((double) data_on_forwarder_sum) / data_rx_count);
}

static inline void
warmup_send_thread() {
    printf("\nWarming up the sender.\n");
    uint16_t sent_in_burst;
    size_t batch_size;
    struct rte_mbuf **send_head = trace_packets + total_packet_count;
    uint64_t remaining_warmup = WARMUP_SIZE(data_send_burst_size);

    while (remaining_warmup > 0) {
        batch_size = MIN(remaining_warmup, (uint64_t) data_send_burst_size);

        sent_in_burst = rte_eth_tx_burst(port_id_data, 0, send_head, batch_size);

        send_head += sent_in_burst;
        remaining_warmup -= sent_in_burst;
    }
}

static int
send_thread(void *param) {
    RTE_SET_USED(param);

    size_t batch_size;
    uint16_t sent_in_burst;
    uint64_t start, end, batches = 0;
#ifdef RATE_CONTROL
    uint64_t token_batch_size;
//    uint64_t times[200], zzz = 0;
#endif
    struct rte_mbuf **send_head;

    printf("\nCore %u sending packets.\n", rte_lcore_id());

    send_head = trace_packets;
    remaining_send = total_packet_count;
    sent_packets = 0;

    // prepare and send some junk packets
    warmup_send_thread();

    printf("\nStart to send in 1s.\n");
    rte_delay_us_sleep(1000000);

    start = rte_rdtsc_precise();
#ifdef RATE_CONTROL
    // we fill the bucket with data_send_burst_size packets
//    time_last_sent = start - data_send_burst_size * cycles_per_packet;
    time_last_sent = start - cycles_per_packet;
//    times[zzz++] = time_last_sent;
#endif

    while (remaining_send > 0) {
        batch_size = MIN(remaining_send, (uint64_t) data_send_burst_size);
#ifdef RATE_CONTROL
        token_batch_size = rte_rdtsc_precise();
//        if (unlikely(zzz < 200)) times[zzz] = token_batch_size;
        token_batch_size = (token_batch_size - time_last_sent) / cycles_per_packet;
        batch_size = MIN(batch_size,  token_batch_size);
        if (unlikely(!batch_size)) // not enough tokens to send a packet
            continue;
//        zzz++;
#endif

        sent_in_burst = rte_eth_tx_burst(port_id_data, 0, send_head, batch_size);

        send_head += sent_in_burst;
        sent_packets += sent_in_burst;
        remaining_send -= sent_in_burst;
        batches++;
#ifdef RATE_CONTROL
        time_last_sent += sent_in_burst * cycles_per_packet;
#endif
    }

    end = rte_rdtsc_precise();

    printf("sent packets: %zd duration: %"PRIu64" batches: %"PRIu64
#ifdef RATE_CONTROL
                " cycles_per_pkt=%.6f/%"PRIu64
#endif
                "\n",
            total_packet_count, end - start, batches
#ifdef RATE_CONTROL
            , (double)(end - start) / total_packet_count, cycles_per_packet
#endif
            );

//#ifdef RATE_CONTROL
//    for (token_batch_size = 0; token_batch_size < 200; token_batch_size++)
//        printf("%"PRIu64"\n", times[token_batch_size]);
//#endif

    sending = false;
    return 0;
}

static int
receive_thread(void *param) {
    RTE_SET_USED(param);

    struct rte_mbuf *bufs[data_receive_burst_size];
    uint16_t nb_rx;

    printf("\nCore %u receiving data packets.\n", rte_lcore_id());
    now = rte_rdtsc_precise();
    while (sending || final_wait > 0) {
        nb_rx = rte_eth_rx_burst(port_id_data, 0, bufs, data_receive_burst_size);
        
        for (i = 0; i < nb_rx; i++) {
            header = rte_pktmbuf_mtod(bufs[i], data_pkt_t * );
            if (likely(bufs[i]->data_len >= DATA_PKT_SIZE && header->common_header.ether.ether_type == ETHER_TYPE_DATA)) {
                seq = header->common_header.seq;
                if (likely(seq < (total_packet_count))) {
                    stat = results + seq;
                    if (unlikely(stat->is_control)) {
                        receive_error_type++;
                    } else if (likely(!stat->data.time_receive)) {
                        
                        receive_data++;
                    } else {
                        receive_redundant++;
                    }
                } else {
                    receive_error_seq++;
                }
            }
    }
        // no need to do anything here, everything done in the data_rx_callback
        if (likely(nb_rx))
            rte_pktmbuf_free_bulk(bufs, nb_rx);
    }
    printf("receive thread ended!\n");
    return 0;
}


static inline void inner_report() {
    uint64_t time_cycles = rte_rdtsc_precise();
    printf("[%14"PRIu64"] sent=%zd remaining=%zd rx_err_type=%zd rx_err_seq=%zd rx_rdnt=%zd rx_data=%zd batch@rx=%"PRIu64" "
                       #ifdef RATE_CONTROL
                       " token=%"PRIu64
                       #endif
                       "\n",
            time_cycles - start_time_cycles,
            sent_packets, remaining_send,
            receive_error_type, receive_error_seq, receive_redundant, receive_data, batches_at_tx_callback
#ifdef RATE_CONTROL
            , (time_cycles - time_last_sent) / cycles_per_packet
#endif
            );
}

static int report_status() {
    start_time_cycles = rte_rdtsc_precise();
    inner_report();
    while (sending || final_wait > 0) {
        rte_delay_ms(REPORT_WAIT_MS);
        if (!sending) final_wait -= REPORT_WAIT_MS;
        inner_report();
    }
    return 0;
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
    common_t * header;

    now = rte_rdtsc_precise();
    for (i = 0; i < nb_pkts; i++) {
        header = rte_pktmbuf_mtod(pkts[i], common_t * );
        header->time_send = results[header->seq].time_send = now;
    }
    batches_at_tx_callback++;
    return nb_pkts;
}


static uint16_t
data_rx_callback(uint16_t port_id,
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
    data_pkt_t * header;
    uint32_t seq;
    packet_stat_t *stat;

    now = rte_rdtsc_precise();
    for (i = 0; i < nb_pkts; i++) {
        header = rte_pktmbuf_mtod(pkts[i], data_pkt_t * );
        if (likely(pkts[i]->data_len >= DATA_PKT_SIZE && header->common_header.ether.ether_type == ETHER_TYPE_DATA)) {
            seq = header->common_header.seq;
            if (likely(seq < (total_packet_count))) {
                stat = results + seq;
                if (unlikely(stat->is_control)) {
                    receive_error_type++;
                } else if (likely(!stat->data.time_receive)) {
                    stat->data.time_receive = now;
                    stat->data.time_arrive_f = header->common_header.time_send; // reused
                    stat->data.time_control = header->time_control;
                    stat->data.time_control_arrive_f = header->time_control_arrive_f;
                    stat->data.time_after_lookup_f = header->time_after_lookup_f;
                    stat->data.time_exit_f = header->time_exit_f;
                    receive_data++;
                } else {
                    receive_redundant++;
                }
            } else {
                receive_error_seq++;
            }
        }
    }
    return nb_pkts;
}


int
main(int argc, char *argv[]) {
    int ret;
    uint16_t nb_ports;
    unsigned nb_lcores, send_lcore_id, receive_lcore_id;


    printf("sizeof(data_pkt_t)=%zd/%zd, sizeof(control_pkt_t)=%zd/%zd\n",
           sizeof(data_pkt_t), DATA_PKT_SIZE, sizeof(control_pkt_t), CONTROL_PKT_SIZE);

    /* Initialize the Environment Abstraction Layer (EAL). */
    ret = rte_eal_init(argc, argv);
    if (unlikely(ret < 0))
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    argc -= ret;
    argv += ret;
    printf("Cycles/sec=%"PRIu64"\n", rte_get_timer_hz());
    printf("\n");

    /* Parse customized args */
    ret = parse_args(argc, argv);
    if (unlikely(ret < 0))
        rte_exit(EXIT_FAILURE, "Error with parse args\n");
    printf("\n");

    /* Make sure that there are at least 1 port available */
    nb_ports = rte_eth_dev_count_avail();
    printf("Number of ports available %"PRIu16"\n", nb_ports);
    if (unlikely(nb_ports < 1))
        rte_exit(EXIT_FAILURE, "Must have at least 1 ports, for both data and control\n");

    /* Make sure that there are at least 2 lcores available */
    nb_lcores = rte_lcore_count();
    printf("Number of lcores available %u\n", nb_lcores);
    if (unlikely(nb_lcores < 3))
        rte_exit(EXIT_FAILURE, "Must have at least 3 cores, 1 for sending, 1 for receiving, and 1 for stat\n");
    printf("\n");

    /* Initialize data packet pool for rx */
    mbuf_pool_data_rx = rte_pktmbuf_pool_create("MBUF_POOL_DATA_RX",
                                                RX_POOL_SIZE, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                                rte_socket_id());
    if (unlikely(!mbuf_pool_data_rx))
        rte_exit(EXIT_FAILURE, "Failed in creating mbuf_pool_data_rx\n");

    /* Initialize port */
    port_id_data = rte_eth_find_next_owned_by(0, RTE_ETH_DEV_NO_OWNER);
    if (unlikely(port_id_data >= RTE_MAX_ETHPORTS))
        rte_exit(EXIT_FAILURE, "Cannot get data port\n");
    port_init(port_id_data, mbuf_pool_data_rx, data_tx_ring_size, data_rx_ring_size, &my_data_mac);
    rte_eth_add_tx_callback(port_id_data, 0, data_tx_callback, NULL);
   // rte_eth_add_rx_callback(port_id_data, 0, data_rx_callback, NULL);
    printf("\n");

    rte_ether_addr_copy(&my_data_mac, &my_control_mac);
    ret = parse_trace();
    if (unlikely(ret < 0))
        rte_exit(EXIT_FAILURE, "Error with parse trace\n");
    printf("\n");

    // start receive lcore
    receive_lcore_id = rte_get_next_lcore(-1, 1, 0);
    if (unlikely(receive_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Receive core required!\n");
    rte_eal_remote_launch(receive_thread, NULL, receive_lcore_id);


    send_lcore_id = rte_get_next_lcore(receive_lcore_id, 1, 0);
    if (unlikely(send_lcore_id == RTE_MAX_LCORE))
        rte_exit(EXIT_FAILURE, "Send core required!\n");
    rte_eal_remote_launch(send_thread, NULL, send_lcore_id);

//report_status();

    rte_eal_mp_wait_lcore();

  //  calculate_statistics();


    return 0;
}
