#include "common.h"
#include <getopt.h>
#ifdef RATE_CONTROL
#include <math.h>
#endif

extern int data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size;
extern int control_tx_ring_size, control_rx_ring_size;
extern struct rte_ether_addr forwarder_data_mac, forwarder_control_mac;
extern char *trace_filename;
extern int trace_repeat;
#ifdef RATE_CONTROL
extern uint64_t cycles_per_packet;
#endif

int parse_args(int argc, char **argv);

#define PARAM_DATA_SEND_BURST_SIZE "data_send_burst"
#define PARAM_DATA_SEND_BURST_SIZE_SHORT "dsb"
#define DEFAULT_DATA_SEND_BURST_SIZE 32

#define PARAM_DATA_RECEIVE_BURST_SIZE "data_receive_burst"
#define PARAM_DATA_RECEIVE_BURST_SIZE_SHORT "drb"
#define DEFAULT_DATA_RECEIVE_BURST_SIZE 32

#define PARAM_DATA_TX_RING_SIZE "data_tx_ring"
#define PARAM_DATA_TX_RING_SIZE_SHORT "dtr"
#define DEFAULT_DATA_TX_RING_SIZE 256

#define PARAM_DATA_RX_RING_SIZE "data_rx_ring"
#define PARAM_DATA_RX_RING_SIZE_SHORT "drr"
#define DEFAULT_DATA_RX_RING_SIZE 256


#define PARAM_CONTROL_TX_RING_SIZE "control_tx_ring"
#define PARAM_CONTROL_TX_RING_SIZE_SHORT "ctr"
#define DEFAULT_CONTROL_TX_RING_SIZE 256

#define PARAM_CONTROL_RX_RING_SIZE "control_rx_ring"
#define PARAM_CONTROL_RX_RING_SIZE_SHORT "crr"
#define DEFAULT_CONTROL_RX_RING_SIZE 256

#define PARAM_TRACE_FILENAME "trace_filename"
#define PARAM_TRACE_FILENAME_SHORT "tf"

#define PARAM_TRACE_REPEAT "trace_repeat_times"
#define PARAM_TRACE_REPEAT_SHORT "trt"
#define DEFAULT_TRACE_REPEAT 1

#define PARAM_FORWARDER_CONTROL_MAC "forwarder_control_mac"
#define PARAM_FORWARDER_CONTROL_MAC_SHORT "fcm"

#ifdef RATE_CONTROL
#define PARAM_RATE_CONTROL_MPPS "million_packets_per_second"
#define PARAM_RATE_CONTROL_MPPS_SHORT "mpps"
#endif

#define PARAM_FORWARDER_DATA_MAC "forwarder_data_mac"
#define PARAM_FORWARDER_DATA_MAC_SHORT "fdm"

#define PARAM_HELP "help"

static const char short_options[] =
        "";

enum {
    /* long options mapped to a short option */

    /* first long only option value must be >= 256, so that we won't
     * conflict with short options */
    CMD_LINE_OPT_MIN_NUM = 256,
    CMD_LINE_OPT_DATA_SEND_BURST_SIZE,
    CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE,
    CMD_LINE_OPT_DATA_TX_RING_SIZE,
    CMD_LINE_OPT_DATA_RX_RING_SIZE,
    CMD_LINE_OPT_CONTROL_TX_RING_SIZE,
    CMD_LINE_OPT_CONTROL_RX_RING_SIZE,
    CMD_LINE_OPT_TRACE_FILENAME,
    CMD_LINE_OPT_TRACE_REPEAT,
    CMD_LINE_OPT_FORWARDER_CONTROL_MAC,
    CMD_LINE_OPT_FORWARDER_DATA_MAC,
#ifdef RATE_CONTROL
    CMD_LINE_OPT_RATE_CONTROL_MPPS,
#endif
    CMD_LINE_OPT_HELP
};

static const struct option long_options[] = {
        {PARAM_DATA_SEND_BURST_SIZE,          required_argument, NULL, CMD_LINE_OPT_DATA_SEND_BURST_SIZE},
        {PARAM_DATA_SEND_BURST_SIZE_SHORT,    required_argument, NULL, CMD_LINE_OPT_DATA_SEND_BURST_SIZE},
        {PARAM_DATA_RECEIVE_BURST_SIZE,       required_argument, NULL, CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE},
        {PARAM_DATA_RECEIVE_BURST_SIZE_SHORT, required_argument, NULL, CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE},
        {PARAM_DATA_TX_RING_SIZE,             required_argument, NULL, CMD_LINE_OPT_DATA_TX_RING_SIZE},
        {PARAM_DATA_TX_RING_SIZE_SHORT,       required_argument, NULL, CMD_LINE_OPT_DATA_TX_RING_SIZE},
        {PARAM_DATA_RX_RING_SIZE,             required_argument, NULL, CMD_LINE_OPT_DATA_RX_RING_SIZE},
        {PARAM_DATA_RX_RING_SIZE_SHORT,       required_argument, NULL, CMD_LINE_OPT_DATA_RX_RING_SIZE},
        {PARAM_CONTROL_TX_RING_SIZE,          required_argument, NULL, CMD_LINE_OPT_CONTROL_TX_RING_SIZE},
        {PARAM_CONTROL_TX_RING_SIZE_SHORT,    required_argument, NULL, CMD_LINE_OPT_CONTROL_TX_RING_SIZE},
        {PARAM_CONTROL_RX_RING_SIZE,          required_argument, NULL, CMD_LINE_OPT_CONTROL_RX_RING_SIZE},
        {PARAM_CONTROL_RX_RING_SIZE_SHORT,    required_argument, NULL, CMD_LINE_OPT_CONTROL_RX_RING_SIZE},
        {PARAM_TRACE_FILENAME,                required_argument, NULL, CMD_LINE_OPT_TRACE_FILENAME},
        {PARAM_TRACE_FILENAME_SHORT,          required_argument, NULL, CMD_LINE_OPT_TRACE_FILENAME},
        {PARAM_TRACE_REPEAT,                  required_argument, NULL, CMD_LINE_OPT_TRACE_REPEAT},
        {PARAM_TRACE_REPEAT_SHORT,            required_argument, NULL, CMD_LINE_OPT_TRACE_REPEAT},
        {PARAM_FORWARDER_CONTROL_MAC,         required_argument, NULL, CMD_LINE_OPT_FORWARDER_CONTROL_MAC},
        {PARAM_FORWARDER_CONTROL_MAC_SHORT,   required_argument, NULL, CMD_LINE_OPT_FORWARDER_CONTROL_MAC},
        {PARAM_FORWARDER_DATA_MAC,            required_argument, NULL, CMD_LINE_OPT_FORWARDER_DATA_MAC},
        {PARAM_FORWARDER_DATA_MAC_SHORT,      required_argument, NULL, CMD_LINE_OPT_FORWARDER_DATA_MAC},
#ifdef RATE_CONTROL
        {PARAM_RATE_CONTROL_MPPS,             required_argument, NULL, CMD_LINE_OPT_RATE_CONTROL_MPPS},
        {PARAM_RATE_CONTROL_MPPS_SHORT,       required_argument, NULL, CMD_LINE_OPT_RATE_CONTROL_MPPS},
#endif
        {PARAM_HELP,                          no_argument,       NULL, CMD_LINE_OPT_HELP},
        {NULL,                                no_argument,       NULL, 0}
};

static void
usage(const char *prgname) {
    printf("%s [EAL options] -- [options]/--" PARAM_HELP "\n"
           "  --" PARAM_HELP ": print this help\n"
           "  [options]:\n"
           "    --" PARAM_DATA_SEND_BURST_SIZE "/--" PARAM_DATA_SEND_BURST_SIZE_SHORT " DATA_SEND_BURST_SIZE: burst size for sending data packets, must be > 0 and <= %d\n"
           "    --" PARAM_DATA_RECEIVE_BURST_SIZE "/--" PARAM_DATA_RECEIVE_BURST_SIZE_SHORT " DATA_RECEIVE_BURST_SIZE: burst size for receiving data packets, must be > 0 and <= %d, and be divisible by 8\n"
           "    --" PARAM_DATA_TX_RING_SIZE "/--" PARAM_DATA_TX_RING_SIZE_SHORT " DATA_TX_RING_SIZE: tx ring size for data packets, must be > 0 and <= %d\n"
           "    --" PARAM_DATA_RX_RING_SIZE "/--" PARAM_DATA_RX_RING_SIZE_SHORT " DATA_RX_RING_SIZE: rx ring size for data packets, must be > 0 and <= %d\n"
           "    --" PARAM_CONTROL_TX_RING_SIZE "/--" PARAM_CONTROL_TX_RING_SIZE_SHORT " CONTROL_TX_RING_SIZE: tx ring size for control packets, must be > 0 and <= %d\n"
           "    --" PARAM_CONTROL_RX_RING_SIZE "/--" PARAM_CONTROL_RX_RING_SIZE_SHORT " CONTROL_RX_RING_SIZE: rx ring size for control packets, must be > 0 and <= %d\n"
           "    --" PARAM_TRACE_FILENAME "/--" PARAM_TRACE_FILENAME_SHORT " TRACE_FILENAME: filename that stores the trace, in CSV format, first column: isControl(0/1), second column: nodeID\n"
           "    --" PARAM_TRACE_REPEAT "/--" PARAM_TRACE_REPEAT_SHORT " TRACE_REPEAT_TIMES: # of times to repeat the trace in 1 experiment. must be > 0 and the total # of packets should be enough to store in the memory\n"
           "    --" PARAM_FORWARDER_CONTROL_MAC "/--" PARAM_FORWARDER_CONTROL_MAC_SHORT " FORWARDER_CONTROL_MAC: the ether address of the control port on the forwarder\n"
           "    --" PARAM_FORWARDER_DATA_MAC "/--" PARAM_FORWARDER_DATA_MAC_SHORT " FORWARDER_DATA_MAC: the ether address of the data port on the forwarder\n"
#ifdef RATE_CONTROL
           "    --" PARAM_RATE_CONTROL_MPPS "/--" PARAM_RATE_CONTROL_MPPS_SHORT " SENDING_RATE_IN_MPPS: the sending rate in million packets per second (MPPS). The real speed can be (closely) equal to the value or the link speed in DPDK, whichever is lower\n"
#endif
           ,
           prgname,
           UINT16_MAX,
           UINT16_MAX,
           UINT16_MAX,
           UINT16_MAX,
           UINT16_MAX,
           UINT16_MAX);
}

/* Parse the argument given in the command line of the application */
int
parse_args(int argc, char **argv) {
    int opt, ret;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    bool has_forwarder_control_mac = false, has_forwarder_data_mac = false;
#ifdef RATE_CONTROL
    bool has_rate_mpps = false;
    double mpps = 0;
#endif
    char addr_str_buf[RTE_ETHER_ADDR_FMT_SIZE];

    data_send_burst_size = DEFAULT_DATA_SEND_BURST_SIZE;
    data_receive_burst_size = DEFAULT_DATA_RECEIVE_BURST_SIZE;
    data_tx_ring_size = DEFAULT_DATA_TX_RING_SIZE;
    data_rx_ring_size = DEFAULT_DATA_RX_RING_SIZE;
    control_tx_ring_size = DEFAULT_CONTROL_TX_RING_SIZE;
    control_rx_ring_size = DEFAULT_CONTROL_RX_RING_SIZE;
    trace_repeat = DEFAULT_TRACE_REPEAT;
    trace_filename = NULL;

    argvopt = argv;

    while ((opt = getopt_long(argc, argvopt, short_options,
                              long_options, &option_index)) != EOF) {
        switch (opt) {
            case CMD_LINE_OPT_DATA_SEND_BURST_SIZE:
                data_send_burst_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE:
                data_receive_burst_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_DATA_TX_RING_SIZE:
                data_tx_ring_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_DATA_RX_RING_SIZE:
                data_rx_ring_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_CONTROL_TX_RING_SIZE:
                control_tx_ring_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_CONTROL_RX_RING_SIZE:
                control_rx_ring_size = atoi(optarg);
                break;
            case CMD_LINE_OPT_TRACE_FILENAME:
                trace_filename = strdup(optarg);
                break;
            case CMD_LINE_OPT_TRACE_REPEAT:
                trace_repeat = atoi(optarg);
                break;
            case CMD_LINE_OPT_FORWARDER_CONTROL_MAC:
                has_forwarder_control_mac = true;
                ret = rte_ether_unformat_addr(optarg, &forwarder_control_mac);
                if (unlikely(ret))
                    rte_exit(EXIT_FAILURE, "Failed in parsing MAC address of " PARAM_FORWARDER_CONTROL_MAC "\n");
                break;
            case CMD_LINE_OPT_FORWARDER_DATA_MAC:
                has_forwarder_data_mac = true;
                ret = rte_ether_unformat_addr(optarg, &forwarder_data_mac);
                if (unlikely(ret))
                    rte_exit(EXIT_FAILURE, "Failed in parsing MAC address of " PARAM_FORWARDER_DATA_MAC "\n");
                break;
#ifdef RATE_CONTROL
            case CMD_LINE_OPT_RATE_CONTROL_MPPS:
                has_rate_mpps = true;
                mpps = atof(optarg);
                break;
#endif
            case CMD_LINE_OPT_HELP:
                usage(prgname);
                rte_exit(EXIT_SUCCESS, "\n");
            default:
                usage(prgname);
                rte_exit(EXIT_FAILURE, "Invalid option detected.\n");
        }
    }

    printf("data_send_burst_size=%d, "
           "data_receive_burst_size=%d, "
           "data_tx_ring_size=%d, "
           "data_rx_ring_size=%d, "
           "control_tx_ring_size=%d, "
           "control_rx_ring_size=%d, "
           "\n",
           data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size,
           control_tx_ring_size, control_rx_ring_size);


    if (unlikely(data_receive_burst_size <= 0 || data_receive_burst_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RECEIVE_BURST_SIZE " should be > 0 and <= %d\n", UINT16_MAX);
    if (unlikely(data_receive_burst_size % 8))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RECEIVE_BURST_SIZE " must be divisible by 8\n");

    if (unlikely(data_tx_ring_size <= 0 || data_tx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_TX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(data_rx_ring_size <= 0 || data_rx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(control_tx_ring_size <= 0 || control_tx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_CONTROL_TX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(control_rx_ring_size <= 0 || control_rx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_CONTROL_RX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    printf("trace repeat=%d\n", trace_repeat);
    if (unlikely(trace_repeat <= 0))
        rte_exit(EXIT_FAILURE, "Trace repeat should be > 0");
    if (unlikely(!trace_filename))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_TRACE_FILENAME "\n");
    printf("trace_filename=%s\n", trace_filename);

    if (unlikely(!has_forwarder_control_mac))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_FORWARDER_CONTROL_MAC "\n");
    if (unlikely(!has_forwarder_data_mac))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_FORWARDER_DATA_MAC "\n");

    rte_ether_format_addr(addr_str_buf, sizeof(addr_str_buf), &forwarder_control_mac);
    printf("forwarder control_mac=%s", addr_str_buf);
    rte_ether_format_addr(addr_str_buf, sizeof(addr_str_buf), &forwarder_data_mac);
    printf(", data_mac=%s\n", addr_str_buf);

#ifdef RATE_CONTROL
    if (unlikely(!has_rate_mpps))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_RATE_CONTROL_MPPS ", or comment out RATE_CONTROL to send as fast as we can\n");
    printf("input MPPS=%.9f, ", mpps);
    cycles_per_packet = (uint64_t)round(rte_get_timer_hz() / (mpps * 1000000));
    mpps = rte_get_timer_hz() / 1000000.0 / cycles_per_packet;
    printf("cycles_per_packet=%"PRIu64", real PPS=%.9f\n", cycles_per_packet, mpps * 1000000);
    if (unlikely(mpps < (1 / 1000000.0) || cycles_per_packet <= 0)) // better not use the system if the user doesn't want to send even 1 pkt/sec
        rte_exit(EXIT_FAILURE, PARAM_RATE_CONTROL_MPPS " too small (< 1pps) or it yields a cycles_per_packet < 0\n");
#endif

    if (optind >= 0)
        argv[optind - 1] = prgname;

    ret = optind - 1;
    optind = 1; /* reset getopt lib */
    return ret;
}
