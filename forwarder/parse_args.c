#include <getopt.h>
#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include "../common.h"

extern int data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size;
extern int control_receive_burst_size; //, control_tx_ring_size, control_rx_ring_size;
extern struct rte_ether_addr receiver_data_mac;
extern char *forward_list_filename;
extern long long control_packet_count;

int
parse_args(int argc, char **argv);

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

#define PARAM_CONTROL_RECEIVE_BURST_SIZE "control_receive_burst"
#define PARAM_CONTROL_RECEIVE_BURST_SIZE_SHORT "crb"
#define DEFAULT_CONTROL_RECEIVE_BURST_SIZE 32

//#define PARAM_CONTROL_TX_RING_SIZE "control_tx_ring"
//#define PARAM_CONTROL_TX_RING_SIZE_SHORT "ctr"
//#define DEFAULT_CONTROL_TX_RING_SIZE 256

//#define PARAM_CONTROL_RX_RING_SIZE "control_rx_ring"
//#define PARAM_CONTROL_RX_RING_SIZE_SHORT "crr"
//#define DEFAULT_CONTROL_RX_RING_SIZE 256

#define PARAM_FORWARD_LIST_FILENAME "forward_list_filename"
#define PARAM_FORWARD_LIST_FILENAME_SHORT "flf"

#define PARAM_RECEIVER_DATA_MAC "receiver_data_mac"
#define PARAM_RECEIVER_DATA_MAC_SHORT "rdm"

#define PARAM_CONTROL_PACKET_COUNT "control_packet_count"
#define PARAM_CONTROL_PACKET_COUNT_SHORT "cpc"

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
    CMD_LINE_OPT_CONTROL_RECEIVE_BURST_SIZE,
//    CMD_LINE_OPT_CONTROL_TX_RING_SIZE,
//    CMD_LINE_OPT_CONTROL_RX_RING_SIZE,
    CMD_LINE_OPT_FORWARD_LIST_FILENAME,
    CMD_LINE_OPT_RECEIVER_DATA_MAC,
    CMD_LINE_OPT_CONTROL_PACKET_COUNT,
    CMD_LINE_OPT_HELP
};

static const struct option long_options[] = {
        {PARAM_DATA_SEND_BURST_SIZE,                required_argument, NULL, CMD_LINE_OPT_DATA_SEND_BURST_SIZE},
        {PARAM_DATA_SEND_BURST_SIZE_SHORT,          required_argument, NULL, CMD_LINE_OPT_DATA_SEND_BURST_SIZE},
        {PARAM_DATA_RECEIVE_BURST_SIZE,             required_argument, NULL, CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE},
        {PARAM_DATA_RECEIVE_BURST_SIZE_SHORT,       required_argument, NULL, CMD_LINE_OPT_DATA_RECEIVE_BURST_SIZE},
        {PARAM_DATA_TX_RING_SIZE,                   required_argument, NULL, CMD_LINE_OPT_DATA_TX_RING_SIZE},
        {PARAM_DATA_TX_RING_SIZE_SHORT,             required_argument, NULL, CMD_LINE_OPT_DATA_TX_RING_SIZE},
        {PARAM_DATA_RX_RING_SIZE,                   required_argument, NULL, CMD_LINE_OPT_DATA_RX_RING_SIZE},
        {PARAM_DATA_RX_RING_SIZE_SHORT,             required_argument, NULL, CMD_LINE_OPT_DATA_RX_RING_SIZE},
        {PARAM_CONTROL_RECEIVE_BURST_SIZE,          required_argument, NULL, CMD_LINE_OPT_CONTROL_RECEIVE_BURST_SIZE},
        {PARAM_CONTROL_RECEIVE_BURST_SIZE_SHORT,    required_argument, NULL, CMD_LINE_OPT_CONTROL_RECEIVE_BURST_SIZE},
//        {PARAM_CONTROL_TX_RING_SIZE,                required_argument, NULL, CMD_LINE_OPT_CONTROL_TX_RING_SIZE},
//        {PARAM_CONTROL_TX_RING_SIZE_SHORT,          required_argument, NULL, CMD_LINE_OPT_CONTROL_TX_RING_SIZE},
//        {PARAM_CONTROL_RX_RING_SIZE,                required_argument, NULL, CMD_LINE_OPT_CONTROL_RX_RING_SIZE},
//        {PARAM_CONTROL_RX_RING_SIZE_SHORT,          required_argument, NULL, CMD_LINE_OPT_CONTROL_RX_RING_SIZE},
        {PARAM_FORWARD_LIST_FILENAME,               required_argument, NULL, CMD_LINE_OPT_FORWARD_LIST_FILENAME},
        {PARAM_FORWARD_LIST_FILENAME_SHORT,         required_argument, NULL, CMD_LINE_OPT_FORWARD_LIST_FILENAME},
        {PARAM_RECEIVER_DATA_MAC,                   required_argument, NULL, CMD_LINE_OPT_RECEIVER_DATA_MAC},
        {PARAM_RECEIVER_DATA_MAC_SHORT,             required_argument, NULL, CMD_LINE_OPT_RECEIVER_DATA_MAC},
        {PARAM_CONTROL_PACKET_COUNT,                required_argument, NULL, CMD_LINE_OPT_CONTROL_PACKET_COUNT},
        {PARAM_CONTROL_PACKET_COUNT_SHORT,          required_argument, NULL, CMD_LINE_OPT_CONTROL_PACKET_COUNT},
        {PARAM_HELP,                                no_argument,       NULL, CMD_LINE_OPT_HELP},
        {NULL,                                      no_argument,       NULL, 0}
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
           "    --" PARAM_CONTROL_RECEIVE_BURST_SIZE "/--" PARAM_CONTROL_RECEIVE_BURST_SIZE_SHORT " CONTROL_RECEIVE_BURST_SIZE: burst size for receiving control packets, must be > 0 and <= %d, and be divisible by 8\n"
//           "    --" PARAM_CONTROL_TX_RING_SIZE "/--" PARAM_CONTROL_TX_RING_SIZE_SHORT " CONTROL_TX_RING_SIZE: tx ring size for control packets, must be > 0 and <= %d\n"
//           "    --" PARAM_CONTROL_RX_RING_SIZE "/--" PARAM_CONTROL_RX_RING_SIZE_SHORT " CONTROL_RX_RING_SIZE: rx ring size for control packets, must be > 0 and <= %d\n"
           "    --" PARAM_FORWARD_LIST_FILENAME "/--" PARAM_FORWARD_LIST_FILENAME_SHORT " FORWARD_LIST_FILENAME: filename that stores the list of node IDs to be populated into the FIB, one ID per line\n"
           "    --" PARAM_RECEIVER_DATA_MAC "/--" PARAM_RECEIVER_DATA_MAC_SHORT " FORWARDER_CONTROL_MAC: the ether address of the control port on the forwarder\n"
           "    --" PARAM_CONTROL_PACKET_COUNT "/--" PARAM_CONTROL_PACKET_COUNT_SHORT " CONTROL_PACKET_COUNT: expected # of control packets, used to save results\n",
            prgname,
            UINT16_MAX,
            UINT16_MAX,
            UINT16_MAX,
            UINT16_MAX,
//            UINT16_MAX,
//            UINT16_MAX,
            UINT16_MAX
            );
}


/* Parse the argument given in the command line of the application */
int
parse_args(int argc, char **argv) {
    int opt, ret;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    bool has_receiver_data_mac = false;
    char addr_str_buf[RTE_ETHER_ADDR_FMT_SIZE];

    data_send_burst_size = DEFAULT_DATA_SEND_BURST_SIZE;
    data_receive_burst_size = DEFAULT_DATA_RECEIVE_BURST_SIZE;
    data_tx_ring_size = DEFAULT_DATA_TX_RING_SIZE;
    data_rx_ring_size = DEFAULT_DATA_RX_RING_SIZE;
    control_receive_burst_size = DEFAULT_CONTROL_RECEIVE_BURST_SIZE;
//    control_tx_ring_size = DEFAULT_CONTROL_TX_RING_SIZE;
//    control_rx_ring_size = DEFAULT_CONTROL_RX_RING_SIZE;
    control_packet_count = 0;
    forward_list_filename = NULL;

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
            case CMD_LINE_OPT_CONTROL_RECEIVE_BURST_SIZE:
                control_receive_burst_size = atoi(optarg);
                break;
//            case CMD_LINE_OPT_CONTROL_TX_RING_SIZE:
//                control_tx_ring_size = atoi(optarg);
//                break;
//            case CMD_LINE_OPT_CONTROL_RX_RING_SIZE:
//                control_rx_ring_size = atoi(optarg);
//                break;
            case CMD_LINE_OPT_FORWARD_LIST_FILENAME:
                forward_list_filename = strdup(optarg);
                break;
            case CMD_LINE_OPT_RECEIVER_DATA_MAC:
                has_receiver_data_mac = true;
                ret = rte_ether_unformat_addr(optarg, &receiver_data_mac);
                if (unlikely(ret))
                    rte_exit(EXIT_FAILURE, "Failed in parsing MAC address of " PARAM_RECEIVER_DATA_MAC "\n");
                break;
            case CMD_LINE_OPT_CONTROL_PACKET_COUNT:
                control_packet_count = atoll(optarg);
                break;
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
           "control_receive_burst_size=%d, "
//           "control_tx_ring_size=%d, "
//           "control_rx_ring_size=%d, "
           "control_packet_count=%lld, "
           "\n",
           data_send_burst_size, data_receive_burst_size, data_tx_ring_size, data_rx_ring_size,
           control_receive_burst_size,
//           control_tx_ring_size, control_rx_ring_size,
           control_packet_count);

    if (unlikely(data_send_burst_size <= 0 || data_send_burst_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_SEND_BURST_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(data_receive_burst_size <= 0 || data_receive_burst_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RECEIVE_BURST_SIZE " should be > 0 and <= %d\n", UINT16_MAX);
    if (unlikely(data_receive_burst_size % 8))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RECEIVE_BURST_SIZE " must be divisible by 8\n");

    if (unlikely(data_tx_ring_size <= 0 || data_tx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_TX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(data_rx_ring_size <= 0 || data_rx_ring_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_DATA_RX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(control_receive_burst_size <= 0 || control_receive_burst_size > UINT16_MAX))
        rte_exit(EXIT_FAILURE, PARAM_CONTROL_RECEIVE_BURST_SIZE " should be > 0 and <= %d\n", UINT16_MAX);
    if (unlikely(control_receive_burst_size % 8))
        rte_exit(EXIT_FAILURE, PARAM_CONTROL_RECEIVE_BURST_SIZE " must be divisible by 8\n");

//    if (unlikely(control_tx_ring_size <= 0 || control_tx_ring_size > UINT16_MAX))
//        rte_exit(EXIT_FAILURE, PARAM_CONTROL_TX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

//    if (unlikely(control_rx_ring_size <= 0 || control_rx_ring_size > UINT16_MAX))
//        rte_exit(EXIT_FAILURE, PARAM_CONTROL_RX_RING_SIZE " should be > 0 and <= %d\n", UINT16_MAX);

    if (unlikely(control_packet_count <= 0))
        rte_exit(EXIT_FAILURE, PARAM_CONTROL_PACKET_COUNT " should be > 0\n");


    if (unlikely(!forward_list_filename))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_FORWARD_LIST_FILENAME "\n");
    printf("forward_list_filename=%s\n", forward_list_filename);

    if (unlikely(!has_receiver_data_mac))
        rte_exit(EXIT_FAILURE, "Must specify " PARAM_RECEIVER_DATA_MAC "\n");

    rte_ether_format_addr(addr_str_buf, sizeof(addr_str_buf), &receiver_data_mac);
    printf("receiver data_mac=%s\n", addr_str_buf);

    if (optind >= 0)
        argv[optind - 1] = prgname;

    ret = optind - 1;
    optind = 1; /* reset getopt lib */
    return ret;
}

