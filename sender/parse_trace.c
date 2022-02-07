#include <rte_malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common.h"
#include "common.h"

extern int data_send_burst_size;
extern size_t data_packet_count, control_packet_count, total_packet_count;
extern struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
extern char *trace_filename;
extern int trace_repeat;
extern struct rte_ether_addr my_data_mac, my_control_mac, forwarder_data_mac, forwarder_control_mac;
extern struct rte_mempool *mbuf_pool_control_tx, *mbuf_pool_data_tx;
extern struct rte_mbuf **trace_packets;
extern packet_stat_t *results;

int
parse_trace(void);

int
parse_trace(void) {
    FILE *trace_file;
    char line[MAX_LINE_WIDTH];
    const char *tok;
    int is_control, j;
    uint16_t node_id;
    size_t i;
    struct rte_mbuf *pkt;
    common_t * common_header;
    size_t warmup_size = WARMUP_SIZE(data_send_burst_size);

    trace_file = fopen(trace_filename, "r");
    if (unlikely(!trace_file))
        rte_exit(EXIT_FAILURE, "Cannot open trace file: %s\n", trace_filename);


    printf("reading the file to get counts\n");
    data_packet_count = control_packet_count = 0;
    while (fgets(line, MAX_LINE_WIDTH, trace_file)) {
        tok = strtok(line, ",\n");
        is_control = atoi(tok);
        if (is_control) control_packet_count++;
        else data_packet_count++;
    }
    total_packet_count = data_packet_count + control_packet_count;
    printf("data_packet_count=%zd, control_packet_count=%zd, total_packet_count=%zd\n",
           data_packet_count, control_packet_count, total_packet_count);

    printf("creating mbuf_pool_data_tx\n");
    /* Initialize data packet pool for tx */
    mbuf_pool_data_tx = rte_pktmbuf_pool_create("MBUF_POOL_DATA_TX", data_packet_count * trace_repeat + warmup_size,
                                                0, 0, RTE_PKTMBUF_HEADROOM + DATA_PKT_SIZE,
                                                rte_socket_id());
    if (unlikely(!mbuf_pool_data_tx))
        rte_exit(EXIT_FAILURE, "Failed in creating mbuf_pool_data_tx\n");

    printf("creating mbuf_pool_control_tx\n");
    /* Initialize control packet pool for tx */
    mbuf_pool_control_tx = rte_pktmbuf_pool_create("MBUF_POOL_CONTROL_TX", control_packet_count * trace_repeat,
                                                   0, 0, RTE_PKTMBUF_HEADROOM + CONTROL_PKT_SIZE,
                                                   rte_socket_id());
    if (unlikely(!mbuf_pool_control_tx))
        rte_exit(EXIT_FAILURE, "Failed in creating mbuf_pool_control_rx\n");

    printf("creating trace_packets\n");
    trace_packets = (struct rte_mbuf **) rte_malloc("DATA_TRACE", sizeof(void *) * (total_packet_count * trace_repeat + warmup_size),
                                                    sizeof(void *));
    if (unlikely(!trace_packets))
        rte_exit(EXIT_FAILURE, "Failed in creating trace_packets\n");

    printf("creating results\n");
    results = (packet_stat_t *) rte_zmalloc("PACKET_RESULTS", sizeof(packet_stat_t) * total_packet_count * trace_repeat,
                                           sizeof(void *));
    if (unlikely(!results))
        rte_exit(EXIT_FAILURE, "Failed in creating results\n");

    i = 0;
    printf("reading the file again to fill the result array\n");
    rewind(trace_file);
    while (fgets(line, MAX_LINE_WIDTH, trace_file)) {
        tok = strtok(line, ",\n");
        is_control = atoi(tok);
        tok = strtok(NULL, ",\n");
        node_id = (uint16_t) atoi(tok);
        for (j = 0; j < trace_repeat; j++) {
            results[j * total_packet_count + i].is_control = is_control;
            results[j * total_packet_count + i].dst_id = rte_cpu_to_be_16(node_id);
        }
        i++;
    }
    fclose(trace_file);

    total_packet_count *= trace_repeat;
    data_packet_count = 0, control_packet_count = 0;
    printf("generating packets\n");
    for (i = 0; i < total_packet_count; i++) {
        if (results[i].is_control) {
            trace_packets[i] = pkt = rte_pktmbuf_alloc(mbuf_pool_control_tx);
            if (unlikely(!pkt))
                rte_exit(EXIT_FAILURE, "Failed in allocating a control packet, i=%zd\n", i);
            pkt->data_len = pkt->pkt_len = CONTROL_PKT_SIZE;
            common_header = rte_pktmbuf_mtod(pkt, common_t * );
            rte_ether_addr_copy(&my_control_mac, &common_header->ether.s_addr);
            rte_ether_addr_copy(&forwarder_control_mac, &common_header->ether.d_addr);
            common_header->ether.ether_type = ETHER_TYPE_CONTROL; // be order
            common_header->seq = i; // local order, only used by myself
            common_header->dst_addr = results[i].dst_id; // be order, already converted in result
            control_packet_count++;
        } else {
            trace_packets[i] = pkt = rte_pktmbuf_alloc(mbuf_pool_data_tx);
            if (unlikely(!pkt))
                rte_exit(EXIT_FAILURE, "Failed in allocating a data packet, i==%zd\n", i);
            pkt->data_len = pkt->pkt_len = DATA_PKT_SIZE;
            common_header = rte_pktmbuf_mtod(pkt, common_t * );
            rte_ether_addr_copy(&my_data_mac, &common_header->ether.s_addr);
            rte_ether_addr_copy(&forwarder_data_mac, &common_header->ether.d_addr);
            common_header->ether.ether_type = ETHER_TYPE_DATA; // be order
            common_header->seq = i; // local order, only used by myself
            common_header->dst_addr = results[i].dst_id; // be order, already converted in result
            data_packet_count++;
        }
    }
    // put warmup packets at the end
    for(; i < total_packet_count + warmup_size; i++) {
        trace_packets[i] = pkt = rte_pktmbuf_alloc(mbuf_pool_data_tx);
        if (unlikely(!pkt))
            rte_exit(EXIT_FAILURE, "Failed in allocating a data packet, i==%zd\n", i);
        pkt->data_len = pkt->pkt_len = DATA_PKT_SIZE;
        common_header = rte_pktmbuf_mtod(pkt, common_t * );
        rte_ether_addr_copy(&my_data_mac, &common_header->ether.s_addr);
        rte_ether_addr_copy(&forwarder_data_mac, &common_header->ether.d_addr);
        common_header->ether.ether_type = ETHER_TYPE_WARMUP; // be order
        common_header->seq = 0; // local order, only used by myself
        common_header->dst_addr = 0; // be order, already converted in result
    }

    rte_mempool_dump(stdout, mbuf_pool_data_tx);
    rte_mempool_dump(stdout, mbuf_pool_control_tx);
    printf("data_packet_count=%zd, control_packet_count=%zd, total packet count=%zd\n",
           data_packet_count, control_packet_count, total_packet_count);


    return 0;
}
