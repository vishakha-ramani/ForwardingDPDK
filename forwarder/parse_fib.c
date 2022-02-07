#include "common.h"

extern char *forward_list_filename;

int
parse_fib(void);

int
parse_fib(void) {
    FILE *fib_file;
    char line[MAX_LINE_WIDTH];
    size_t fib_size = 0;
    fib_entry_t *fib_entry;
    uint16_t node_id, node_id_be;
    int ret;

    fib_file = fopen(forward_list_filename, "r");
    if (unlikely(!fib_file))
        rte_exit(EXIT_FAILURE, "Cannot open forward list file: %s\n", forward_list_filename);

    printf("reading the file to get counts\n");
    while (fgets(line, MAX_LINE_WIDTH, fib_file)) {
        fib_size++;
    }
    printf("fib_size=%zd\n", fib_size);

    printf("creating fib_entry_pool\n");
    fib_entry_pool = rte_mempool_create("FIB_ENTRIES", fib_entry_pool_size(fib_size), sizeof(fib_entry_t),
                                        0, 0,
                                        NULL, NULL,
                                        NULL, NULL,
                                        rte_socket_id(),
                                        MEMPOOL_F_SC_GET | MEMPOOL_F_SP_PUT); // only 1 thread (control) will update the pool
    if (unlikely(!fib_entry_pool))
        rte_exit(EXIT_FAILURE, "Cannot create fib_entry_pool");
    init_hash(fib_size);

    printf("reading the file again to populate the fib\n");
    rewind(fib_file);
    while (fgets(line, MAX_LINE_WIDTH, fib_file)) {
        node_id = (uint16_t) atoi(line);
        fib_entry = get_new_fib_entry();
        fib_entry->control_time = fib_entry->control_arrive_time_f = 0;
        rte_ether_addr_copy(&receiver_data_mac, &fib_entry->receiver_mac);
        node_id_be = rte_cpu_to_be_16(node_id); // store be in the fib, no need to convert when lookup
        ret = rte_hash_add_key_data(fib, &node_id_be, fib_entry);
        if (unlikely(ret))
            rte_exit(EXIT_FAILURE, "Failed in adding entry to FIB, node_id=%"PRIu16"\n", node_id);
//        else
//            printf("added fib_entry: %"PRIu16"\n", node_id);
    }
    printf("sizeof fib: %"PRIi32"\n", rte_hash_count(fib));
    if (unlikely(rte_hash_count(fib) - fib_size)) {
        rte_exit(EXIT_FAILURE, "size of fib not equal to fib_size, having redundant entries in fib?\n");
    }
}
