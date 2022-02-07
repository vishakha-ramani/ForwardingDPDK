#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <climits>
#include <cstdio>

using namespace std;

//#define MONITOR_NODE_ID 106

class NodeStat {
public:
    unsigned long long firstT3 = 0, firstT8 = 0, lastT8 = 0;
    unsigned long long oldT3 = 0, totalAge = 0, expectedControl = 0;
    size_t correctReceived = 0, dropped = 0, misDestinated = 0;
};

class ControlStat {
public:
    unsigned long nodeId = 0;
    unsigned long long allocateTime = 0, freeTime = 0;
};

static inline void splitLine(string &line, vector<string> &parts) {
    size_t pos;
    while ((pos = line.find(' ')) != string::npos) {
        parts.push_back(line.substr(0, pos));
        line.erase(0, pos + 1);
    }
    parts.push_back(line);
}

//static inline void printLine(vector<string> &parts) {
//    for (auto &part: parts) {
//        printf("%s ", part);
//    }
//    printf("\n");
//}


static inline void parseSenderFile(
        char **argv,
        unsigned long long &senderStartTime,
        unsigned long long &senderEndTime,
        unsigned long long &forwarderStartTime,
        unsigned long long &forwarderEndTime,
        unordered_map<unsigned long, NodeStat> &nodes,
        unordered_map<unsigned long, ControlStat> &controls) {
    ifstream input(argv[1]);
    if (input.fail()) {
        printf("Failed to open file \"%s\"\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    FILE *output;
    output = fopen(argv[2], "w");
    if (!output) {
        printf("Failed to open file \"%s\"\n", argv[2]);
        exit(EXIT_FAILURE);
    }
    forwarderStartTime = senderStartTime = ULLONG_MAX;
    forwarderEndTime = senderEndTime = 0;
    string line;
    size_t lineCount = 0;

    while (getline(input, line)) {
        lineCount++;
        vector<string> parts;
        splitLine(line, parts);
        if (parts.size() < 3) {
            fprintf(stderr, "%s:%zd doesn't have at least 3 parts, skip!\n", argv[1], lineCount);
            continue;
        }
        auto nodeId = stoul(parts[1]);
        auto sentTime = stoull(parts[2]);
        auto &node = nodes[nodeId];

        senderStartTime = min(sentTime, senderStartTime);
        senderEndTime = max(sentTime, senderEndTime);

        if (parts[0] == "1") { // control
            node.expectedControl = sentTime;
            controls[lineCount - 1].nodeId = nodeId;
#ifdef MONITOR_NODE_ID
            if (nodeId == MONITOR_NODE_ID)
                printf("%zd: C %llu\n", lineCount, sentTime);
#endif
        } else if (parts[0] == "0") { // data
            if (parts.size() < 9) {
                fprintf(stderr, "%s:%zd [data] doesn't have 9 parts [0] [node_id] [t3] [t8] [t1] [t2] [t4] [t5] [t7], skip!\n", argv[1], lineCount);
                continue;
            }
            auto t8 = stoull(parts[3]);
            auto t1 = stoull(parts[4]);
            auto t2 = stoull(parts[5]);
            auto t4 = stoull(parts[6]);
            auto t7 = stoull(parts[8]);


            if (t8 == 0) { // dropped
                node.dropped++;
#ifdef MONITOR_NODE_ID
                if (nodeId == MONITOR_NODE_ID)
                    printf("%zd: D Dropped\n", nodeId);
#endif
            } else {
                senderEndTime = max(t8, senderEndTime);
                if (t2 != 0) forwarderStartTime = min(t2, forwarderStartTime);
                forwarderStartTime = min(t4, forwarderStartTime);
                forwarderStartTime = min(t7, forwarderStartTime);
                forwarderEndTime = max(t2, forwarderEndTime);
                forwarderEndTime = max(t4, forwarderEndTime);
                forwarderEndTime = max(t7, forwarderEndTime);

                if (t1 < node.expectedControl) { // mis-destinated
#ifdef MONITOR_NODE_ID
                    if (nodeId == MONITOR_NODE_ID)
                        printf("%zd: D expect %llu now %llu\n", lineCount, node.expectedControl, t1);
#endif
                    node.misDestinated++;
                } else {
                    node.correctReceived++;
                    node.lastT8 = max(t8, node.lastT8);
                    if (node.firstT3 == 0) { // first packet
                        node.firstT3 = node.oldT3 = sentTime;
                        node.firstT8 = t8;
                    } else {
                        auto v1 = t8 - node.oldT3;
                        auto v2 = t8 - sentTime;
                        node.totalAge += (v1 * v1 - v2 * v2) / 2;
                        node.oldT3 = sentTime;
                    }
                }
            }
        } else {
            fprintf(stderr, "%s:%zd doesn't start with 0 (data) or 1 (control), skip!\n", argv[1], lineCount);
        }
    }
    printf("senderStartTime %llu\n"
           "senderEndTime %llu\n"
           "senderDuration %llu\n"
           "forwarderStartTime %llu\n"
           "forwarderEndTime %llu\n"
           "forwarderDuration %llu\n",
           senderStartTime,
           senderEndTime,
           senderEndTime - senderStartTime,
           forwarderStartTime,
           forwarderEndTime,
           forwarderEndTime - forwarderStartTime);

    fprintf(output, "nodeId "
                    "correctReceived "
                    "dropped "
                    "misAddressed "
                    "firstT3R "
                    "lastT8R "
                    "nodeDuration "
                    "totalAge "
                    "avgAge "
                    "totalAgeNew "
                    "avgAgeNew\n");
    for (auto &node: nodes) {
        fprintf(output, "%lu %zd %zd %zd %llu %llu %llu %llu %.6f ",
                node.first,
                node.second.correctReceived,
                node.second.dropped,
                node.second.misDestinated,
                node.second.firstT3 - senderStartTime,
                node.second.lastT8 - senderStartTime,
                node.second.lastT8 - node.second.firstT3,
                node.second.totalAge,
                (double)node.second.totalAge / (double)(node.second.lastT8 - node.second.firstT3));

        // add first and last "triangles"
        if (node.second.firstT3 == 0) { // if no packet is received, use start time as oldT3
            unsigned long long v = senderEndTime - senderStartTime;
            node.second.totalAge = v * v / 2;
            printf("user %lu received no correct packet, v=%llu, age=%llu, %f!\n", 
                node.first, v, node.second.totalAge, (double)v * v / 2);
        } else {
            // first "triangle"
            auto v1 = node.second.firstT8 - senderStartTime;
            auto v2 = node.second.firstT8 - node.second.firstT3;
            node.second.totalAge += (v1 * v1 - v2 * v2) / 2;

            // last triangle
            auto v3 = senderEndTime - node.second.oldT3;
            node.second.totalAge += v3 * v3 / 2;
        }
        fprintf(output, "%llu %.6f\n",
                node.second.totalAge,
                (double)node.second.totalAge / (double)(senderEndTime - senderStartTime));
    }
    fflush(output);
    fclose(output);
    input.close();
}


static inline void parseMemory(
        char **argv,
        unsigned long long forwarderStartTime,
        unsigned long long forwarderEndTime,
        unordered_map<unsigned long, NodeStat> &nodes,
        unordered_map<unsigned long, ControlStat> &controls

) {
    printf("controlPackets %zd\n"
           "nodes %zd\n",
           controls.size(), nodes.size());

    ifstream inputMem(argv[3]);
    if (inputMem.fail()) {
        fprintf(stderr, "Failed to open file \"%s\"\n", argv[3]);
        exit(EXIT_FAILURE);
    }

    FILE *outputMem;
    outputMem = fopen(argv[4], "w");
    if (!outputMem) {
        fprintf(stderr, "Failed to open file \"%s\"\n", argv[4]);
        exit(EXIT_FAILURE);
    }

    size_t lineCount = 0;
    string line;
    // sequence numbers
    vector<unsigned long> pendingFrees;
    // sequence number
    unsigned long pendingAdd = 0;
    unsigned long entryCount = nodes.size();
    unsigned long long time = forwarderStartTime;
    unsigned long long totalCount = 0;
    unsigned long maxEntryCount = entryCount;
    fprintf(outputMem, "timeR FIBEntries\n"
                       "%llu %lu\n", time - forwarderStartTime, entryCount);
    while (getline(inputMem, line)) {
        lineCount++;
        vector<string> parts;
        splitLine(line, parts);
        if (parts.size() < 2) {
            fprintf(stderr, "%s:%zd doesn't have at least 2 parts, skip!\n", argv[3], lineCount);
            continue;
        }
        if (parts[0] == "A") { // allocate
            if (pendingAdd != 0) {
                fprintf(stderr, "%s:%zd multiple allocations for a control packet!\n", argv[3], lineCount);
                exit(EXIT_FAILURE);
            }
            pendingAdd = stoul(parts[1]);
        } else if (parts[0] == "F") { // free
            pendingFrees.push_back(stoul(parts[1]));
        } else if (parts[0] == "T") { // timestamp
            if (pendingAdd == 0) {
                fprintf(stderr, "%s:%zd no allocation for a control packet!\n", argv[3], lineCount);
                exit(EXIT_FAILURE);
            }
            auto newTime = stoull(parts[1]);
            {
                auto it = controls.find(pendingAdd);
                if (it == controls.end()) {
                    fprintf(stderr, "%s:%zd cannot find control with sequence %lu\n", argv[3], lineCount, pendingAdd);
                    exit(EXIT_FAILURE);
                }
                if (it->second.allocateTime != 0) {
                    fprintf(stderr, "%s:%zd duplicate allocating control with sequence %lu\n", argv[3], lineCount, pendingAdd);
                    exit(EXIT_FAILURE);
                } else {
                    it->second.allocateTime = newTime;
                }
            }
            for (auto pendingFree: pendingFrees) {
                if (pendingFree == 0) continue;
                auto it = controls.find(pendingFree);
                if (it == controls.end()) {
                    fprintf(stderr, "%s:%zd cannot find control with sequence %lu\n", argv[3], lineCount, pendingFree);
                    exit(EXIT_FAILURE);
                }
                if (it->second.allocateTime == 0) {
                    fprintf(stderr, "%s:%zd freeing control before allocating with sequence %lu\n", argv[3], lineCount, pendingFree);
                    exit(EXIT_FAILURE);
                } else if (it->second.freeTime != 0) {
                    fprintf(stderr, "%s:%zd duplicate freeing control with sequence %lu\n", argv[3], lineCount, pendingFree);
                    exit(EXIT_FAILURE);
                } else {
                    it->second.freeTime = newTime;
                }
            }
            totalCount += entryCount * (newTime - time);

            time = newTime;
            entryCount += 1 - pendingFrees.size(); // 1 allocate, n frees
            fprintf(outputMem, "%llu %lu\n", time - forwarderStartTime, entryCount);
            maxEntryCount = max(entryCount, maxEntryCount);

            pendingAdd = 0;
            pendingFrees.clear();
        }
    }
    fprintf(outputMem, "%llu %lu\n", forwarderEndTime, entryCount);
    fflush(outputMem);
    fclose(outputMem);
    totalCount += entryCount * (forwarderEndTime - time);
    printf("totalCount*time %llu\n"
           "avgFibSize %.6f\n"
           "maxEntryCount %lu\n",
           totalCount,
           ((double)totalCount / (double)(forwarderEndTime - forwarderStartTime)),
           maxEntryCount);

    size_t droppedControls = 0, handledControls = 0;
    unsigned long long totalControlDuration = 0;
    for (auto control: controls) {
        //printf("%lu %llu %llu\n", control.first, control.second.allocateTime, control.second.freeTime);
        if (control.second.allocateTime == 0) {
            droppedControls++;
        } else {
            handledControls++;
            auto freeTime = control.second.freeTime;
            freeTime = freeTime == 0 ? forwarderEndTime : freeTime;
            totalControlDuration += freeTime - control.second.allocateTime;
        }
    }
    printf("droppedControl %zd\n"
           "handledControl %zd\n"
           "totalFibEntryDuration %llu\n"
           "avgFibEntryDuration %.6f\n",
           droppedControls,
           handledControls,
           totalControlDuration,
           ((double)totalControlDuration / (double)handledControls));
}


int main(int argc, char **argv) {
    if (argc < 3 || argc == 4) {
        fprintf(stderr, "Usage: %s %s %s [%s %s]\n",
                argv[0], "%result_sender_file%", "%output_file%", "%result_rcu_u_file%", "%output_mem_file%");
        exit(EXIT_FAILURE);
    }

    unordered_map<unsigned long, NodeStat> nodes;
    unordered_map<unsigned long, ControlStat> controls;

    unsigned long long senderStartTime, senderEndTime, forwarderStartTime, forwarderEndTime;

    parseSenderFile(argv, senderStartTime, senderEndTime, forwarderStartTime, forwarderEndTime, nodes, controls);

    if (argc >= 5) {
        parseMemory(argv, forwarderStartTime, forwarderEndTime, nodes, controls);
    }
    return EXIT_SUCCESS;
}
