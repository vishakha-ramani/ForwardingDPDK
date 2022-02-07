#ifndef __SENDER_COMMON_H
#define __SENDER_COMMON_H

#define RATE_CONTROL
#define RESULT_FILENAME "result_sender.txt" // comment if do not want to output result

#define REPORT_WAIT_MS 500
#define WAIT_AFTER_FINISH_MS 5000
#define RX_POOL_SIZE 16383

#include "../common.h"
#include <rte_eal.h>

#define WARMUP_SIZE(data_send_burst_size) ((data_send_burst_size) * 4)

typedef struct {
    uint64_t time_receive;
    uint64_t time_control;
    uint64_t time_control_arrive_f;
    uint64_t time_arrive_f;
    uint64_t time_after_lookup_f;
    uint64_t time_exit_f;
} data_packet_stat_t;

typedef struct {

} control_packet_stat_t;

typedef struct {
    bool is_control;
    uint16_t dst_id;
    uint64_t time_send;
    union {
        data_packet_stat_t data;
        control_packet_stat_t control;
    };
} packet_stat_t;

#endif //__SENDER_COMMON_H
