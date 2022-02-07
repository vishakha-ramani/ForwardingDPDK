import java.io.*;
import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.RoundingMode;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.*;


public class Calculator {
    static final long MONITOR_NODE_ID = 0;
    static final int DEFAULT_ROUNDING_SCALE = 6;
    static final RoundingMode DEFAULT_ROUNDING_MODE = RoundingMode.HALF_EVEN;
    static final BigDecimal TWO = BigDecimal.valueOf(2);

    private static void parseSenderFile(String resultSenderFile,
                                        HashMap<Long, NodeStat> nodes, HashMap<Long, ControlStat> controls,
                                        Times times) {
        try (BufferedReader reader = new BufferedReader(new FileReader(resultSenderFile))) {
            String line;
            long lineCount = 0;
            while ((line = reader.readLine()) != null) {
                lineCount++;
                String[] parts = line.split(" ");
                if (parts.length < 3) {
                    System.err.printf("%s:%d doesn't have at least 3 parts, skip!%n", resultSenderFile, lineCount);
                    continue;
                }
                long nodeId = Long.parseLong(parts[1]);
                long sendTime = Long.parseLong(parts[2]);
                NodeStat node = nodes.compute(nodeId, (k, v) -> v == null ? new NodeStat(nodeId, times) : v);

                times.updateSender(sendTime);

                switch (parts[0]) {
                    case "1": {
                        node.setExpectedControl(sendTime);
                        if (MONITOR_NODE_ID != 0 && nodeId == MONITOR_NODE_ID)
                            System.out.printf("%d: C %d%n", lineCount, sendTime);
                        controls.put(lineCount - 1, new ControlStat(nodeId));
                        break;
                    }
                    case "0": {
                        if (parts.length < 9) {
                            System.err.printf("%s:%d [data] doesn't have 9 parts [0] [node_id] [t3] [t8] [t1] [t2] [t4] [t5] [t7], skip!%n", resultSenderFile, lineCount);
                            continue;
                        }
                        long t8 = Long.parseLong(parts[3]);

                        if (t8 == 0) { // dropped
                            node.addRopped();
                            if (MONITOR_NODE_ID != 0 && nodeId == MONITOR_NODE_ID)
                                System.out.printf("%d: D dropped", lineCount);
                        } else {
                            times.updateSender(t8);
                            long t1 = Long.parseLong(parts[4]);
                            long t2 = Long.parseLong(parts[5]);
                            long t4 = Long.parseLong(parts[6]);
                            long t7 = Long.parseLong(parts[8]);

                            if (t2 != 0) times.updateForwarder(t2);
                            times.updateForwarder(t4);
                            times.updateForwarder(t7);

                            node.updateControlAge(t1, t8);
                            boolean missAddressed = !node.updateDataAge(sendTime, t1, t8);
                            if (missAddressed) {
                                if (Calculator.MONITOR_NODE_ID != 0 && nodeId == Calculator.MONITOR_NODE_ID)
                                    System.out.printf("%d: D expect %d now %d%n", lineCount, node.getExpectedControl(), t1);
                            }
                        }
                        break;
                    }
                    default: {
                        System.err.printf("%s:%d doesn't start with 0 (data) or 1 (control), skip!%n", resultSenderFile, lineCount);
                        break;
                    }
                }
            }
        } catch (IOException e) {
            System.err.printf("Failed to open file. Cause:%n");
            e.printStackTrace(System.err);
            System.exit(-1);
        }
    }

    private static void parseMemFile(
            String resultRcuUFile,
            String memEventFile,
            Times times,
            HashMap<Long, NodeStat> nodes,
            HashMap<Long, ControlStat> controls) {
        System.out.printf("controlPackets %d%nnodes %d%n", controls.size(), nodes.size());

        try (BufferedReader reader = new BufferedReader(new FileReader(resultRcuUFile));
             PrintStream outputEvent = memEventFile == null ? null : new PrintStream(memEventFile)) {
            String line;
            long lineCount = 0;
            long pendingAdd = 0, entryCount = nodes.size(), maxEntryCount = entryCount, time = times.getForwarderStart();
            BigInteger totalCount = BigInteger.ZERO;
            ArrayList<Long> pendingFrees = new ArrayList<>();

            // mimic the free for the first packet (seq = 0)
            // when a node allocates the first fib entry, the id is taken from pendingInitialFrees and queued at firstAllocated
            // when we see the next seq = 0, we take the first from firstAllocated and assume this is the node for the freed
            HashSet<Long> pendingInitialFrees = new HashSet<>(nodes.keySet());
            LinkedList<Long> firstAllocated = new LinkedList<>();
            // key: nodeId, value: update in each round
            HashMap<Long, Integer> pendingUpdates = new HashMap<>();

            if (outputEvent != null)
                outputEvent.printf("timeR FIBEntries%n%d %d%n", 0, entryCount);
            while ((line = reader.readLine()) != null) {
                lineCount++;
                String[] parts = line.split(" ");
                if (parts.length < 2) {
                    System.err.printf("%s:%d doesn't have at least 2 parts, skip!\n", resultRcuUFile, lineCount);
                    continue;
                }
                switch (parts[0]) {
                    case "A": { // allocate
                        if (pendingAdd != 0) {
                            System.err.printf("%s:%d multiple allocation for a control packet!%n", resultRcuUFile, lineCount);
                            System.exit(-1);
                        }
                        pendingAdd = Long.parseLong(parts[1]);
                        break;
                    }
                    case "F": { // free
                        pendingFrees.add(Long.parseLong(parts[1]));
                        break;
                    }
                    case "T": { // timestamp
                        if (pendingAdd == 0) {
                            System.err.printf("%s:%d no allocation for a control packet!%n", resultRcuUFile, lineCount);
                            System.exit(-1);
                        }
                        long newTime = Long.parseLong(parts[1]);
                        {
                            ControlStat control = controls.get(pendingAdd);
                            if (control == null) {
                                System.err.printf("%s:%d cannot find control packet with sequence %d%n", resultRcuUFile, lineCount, pendingAdd);
                                System.exit(-1);
                            }
                            if (!control.allocate(newTime)) {
                                System.err.printf("%s:%d duplicatie allocating control with sequence %d%n", resultRcuUFile, lineCount, pendingAdd);
                                System.exit(-1);
                            }
                            long pendingAddNodeId = control.getNodeId();
                            pendingUpdates.put(pendingAddNodeId, 1);
                            if (pendingInitialFrees.remove(pendingAddNodeId)) {
//                                System.out.printf("add initialFree %d at %d%n", pendingAddNodeId, newTime);
                                firstAllocated.addLast(pendingAddNodeId);
                            }
                        }
                        for (long pendingFree : pendingFrees) {
                            long pendingFreeNodeId;
                            if (pendingFree == 0) {
                                if (firstAllocated.isEmpty()) {
                                    System.err.printf("%s:%d freeing an extra seq=0, no node allocated yet%n", resultRcuUFile, lineCount);
                                    System.exit(-1);
                                }
                                pendingFreeNodeId = firstAllocated.removeFirst();
//                                System.out.printf("free %d at %d%n", pendingFreeNodeId, newTime);
                            } else {
                                ControlStat control = controls.get(pendingFree);
                                if (control == null) {
                                    System.err.printf("%s:%d cannot find control packet with sequence %d%n", resultRcuUFile, lineCount, pendingFree);
                                    System.exit(-1);
                                }
                                if (!control.free(newTime)) {
                                    System.err.printf("%s:%d freeing control packet before allocating/duplicate freeing control packet with sequence %d%n", resultRcuUFile, lineCount, pendingFree);
                                    System.exit(-1);
                                }
                                pendingFreeNodeId = control.getNodeId();
                            }
                            pendingUpdates.compute(pendingFreeNodeId, (k, v) -> v == null ? -1 : (v - 1));
                        }
                        long tmpLineCount = lineCount;
                        pendingUpdates.forEach((id, update) -> {
                            NodeStat node = nodes.get(id);
                            if (node == null) {
                                System.err.printf("%s:%d cannot find node id %d%n", resultRcuUFile, tmpLineCount, id);
                                System.exit(-1);
                            } else {
                                node.updateFibCount(newTime, update);
                            }
                        });

                        // totalCount += entryCount * (newTime - time);
                        totalCount = totalCount.add(BigInteger.valueOf(entryCount).multiply(BigInteger.valueOf(Math.subtractExact(newTime, time))));

                        time = newTime;
                        entryCount += 1 - pendingFrees.size(); // 1 allocate, n frees
                        if (outputEvent != null)
                            outputEvent.printf("%d %d%n", Math.subtractExact(time, times.getForwarderStart()), entryCount);
                        maxEntryCount = Math.max(entryCount, maxEntryCount);

                        pendingFrees.clear();
                        pendingUpdates.clear();
                        pendingAdd = 0;
                        break;
                    }
                    default: {
                        System.err.printf("%s:%d error event type, skip!%n", resultRcuUFile, lineCount);
                        break;
                    }
                }
            }
            if (outputEvent != null) {
                outputEvent.printf("%d %d%n", Math.subtractExact(times.getForwarderEnd(), times.getForwarderStart()), entryCount);
                outputEvent.flush();
            }

            // totalCount += entryCount * (forwarderEndTime - time);
            totalCount = totalCount.add(
                    BigInteger.valueOf(entryCount)
                            .multiply(BigInteger.valueOf(Math.subtractExact(times.getForwarderEnd(), time)))
            );
            // averageCount = totalCount / (forwarderEndTime - forwarderStartTime)
            BigDecimal averageCount = new BigDecimal(totalCount).divide(times.getForwarderDuration(), DEFAULT_ROUNDING_SCALE, DEFAULT_ROUNDING_MODE);
            System.out.printf("totalCount*time %s%navgFibSize %s%nmaxEntryCount %d%n", totalCount, averageCount, maxEntryCount);

            long[] droppedControls = new long[1], handledControls = new long[1];
            BigInteger[] totalControlDuration = new BigInteger[]{BigInteger.ZERO};

            controls.forEach((seq, control) -> {
                if (control.getAllocateTime() == 0) {
                    droppedControls[0]++;
                } else {
                    handledControls[0]++;
                    // totalControlDuration += freeTime - control.allocateTime
                    totalControlDuration[0] = totalControlDuration[0].add(BigInteger.valueOf(control.getDuration(times.getForwarderEnd())));
                }
            });
            // avgControlDuration = totalControlDuration / handledControls
            BigDecimal avgControlDuration = new BigDecimal(totalControlDuration[0]).divide(new BigDecimal(handledControls[0]), DEFAULT_ROUNDING_SCALE, DEFAULT_ROUNDING_MODE);
            System.out.printf("droppedControl %d%nhandledControl %d%ntotalFibEntryDuration %s%navgFibEntryDuration %s%n",
                    droppedControls[0], handledControls[0], totalControlDuration[0], avgControlDuration);

        } catch (IOException e) {
            System.err.printf("Failed to open file. Cause:%n");
            e.printStackTrace(System.err);
            System.exit(-1);
        }
    }

    public static void main(String[] args) {
        if (args.length < 2) {
            System.err.printf("Usage: %s %s %s [%s [%s [%s]]]%n",
                    "java Calculator",
                    "%result_sender_file%", "%output_file%",
                    "%result_rcu_u_file%", "%output_mem_event_file%", "%output_mem_per_user_folder%");
            return;
        }
        String resultSenderFile = args[0];
        String nodeOutputFile = args[1];

        try (PrintStream output = new PrintStream(nodeOutputFile)) {

            // key: nodeId
            HashMap<Long, NodeStat> nodes = new HashMap<>();
            // key: control sequence number
            HashMap<Long, ControlStat> controls = new HashMap<>();
            Times times = new Times();


            parseSenderFile(resultSenderFile, nodes, controls, times);
            times.finish();

            if (args.length == 2) {
                output.println("nodeId correctReceived dropped misAddressed " +
                        "firstT3R firstT8R lastT3R lastT8R nodeDuration totalAge avgAge totalAgeNew avgAgeNew " +
                        "firstT1R firstT8'R lastT1R lastT8'R nodeCtrlDuration totalCtrlAge avgCtrlAge totalCtrlAgeNew avgCtrlAgeNew");
                nodes.values().stream()
                        .sorted(Comparator.comparingLong(NodeStat::getNodeId))
                        .forEach(n -> {
                            n.finishDataAge();
                            n.finishControlAge();
                            n.printOutputWoFib(output);
                        });
            } else {
                String resultRcuUFile = args[2];
                String memEventFile = args.length > 3 ? args[3] : null;
                String memPerUserFolder = args.length > 4 ? args[4] : null;

                if (memPerUserFolder != null) {
                    try {
                        Files.createDirectories(Paths.get(memPerUserFolder));
                    } catch (IOException e) {
                        System.err.printf("Failed to create result mem folder: %s%n", memPerUserFolder);
                        e.printStackTrace();
                    }
                }

                nodes.forEach((id, n) -> n.initFibCount(memPerUserFolder != null));

                parseMemFile(resultRcuUFile, memEventFile, times, nodes, controls);

                output.println("nodeId correctReceived dropped misAddressed " +
                        "firstT3R firstT8R lastT3R lastT8R nodeDuration totalAge avgAge totalAgeNew avgAgeNew " +
                        "firstT1R firstT8'R lastT1R lastT8'R nodeCtrlDuration totalCtrlAge avgCtrlAge totalCtrlAgeNew avgCtrlAgeNew " +
                        "maxFIBSize totalFIBSize*time avgFIBSize");
                nodes.values().stream()
                        .sorted(Comparator.comparingLong(NodeStat::getNodeId))
                        .forEach(n -> {
                            n.finishDataAge();
                            n.finishControlAge();
                            n.finishFibCount(memPerUserFolder);
                            n.printOutputWFib(output);
                        });
            }

            output.flush();
        } catch (IOException e) {
            System.err.printf("Failed to open file. Cause:%n");
            e.printStackTrace(System.err);
            System.exit(-1);
        }
    }

}

class Times {
    private long senderStart, senderEnd;
    private long forwarderStart, forwarderEnd;
    private BigDecimal senderDuration, senderMaximumAge, forwarderDuration;

    Times() {
        senderStart = forwarderStart = Long.MAX_VALUE;
        senderEnd = forwarderEnd = 0;
    }

    void updateSender(long time) {
        senderStart = Math.min(time, senderStart);
        senderEnd = Math.max(time, senderEnd);
    }

    void updateForwarder(long time) {
        forwarderStart = Math.min(time, forwarderStart);
        forwarderEnd = Math.max(time, forwarderEnd);
    }

    long getSenderStart() {
        return senderStart;
    }

    long getSenderEnd() {
        return senderEnd;
    }

    long getForwarderStart() {
        return forwarderStart;
    }

    long getForwarderEnd() {
        return forwarderEnd;
    }

    BigDecimal getSenderDuration() {
        return senderDuration;
    }

    BigDecimal getSenderMaximumAge() {
        return senderMaximumAge;
    }

    BigDecimal getForwarderDuration() {
        return forwarderDuration;
    }

    void finish() {
        senderDuration = BigDecimal.valueOf(Math.subtractExact(senderEnd, senderStart), 0);
        senderMaximumAge = senderDuration.multiply(senderDuration).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY);
        forwarderDuration = BigDecimal.valueOf(Math.subtractExact(forwarderEnd, forwarderStart), 0);

        System.out.printf("senderStartTime %d%nsenderEndTime %d%nsenderDuration %d%n" +
                        "forwarderStartTime %d%nforwarderEndTime %d%nforwarderDuration %d%n",
                senderStart, senderEnd, Math.subtractExact(senderEnd, senderStart),
                forwarderStart, forwarderEnd, Math.subtractExact(forwarderEnd, forwarderStart));
    }
}


class ControlStat {
    private final long nodeId;
    private long allocateTime, freeTime;

    ControlStat(long nodeId) {
        this.nodeId = nodeId;
    }

    long getNodeId() {
        return nodeId;
    }

    long getAllocateTime() {
        return allocateTime;
    }

    long getFreeTime() {
        return freeTime;
    }

    boolean allocate(long time) {
        if (allocateTime != 0) return false;
        allocateTime = time;
        return true;
    }

    boolean free(long time) {
        if (allocateTime == 0 || freeTime != 0) return false;
        freeTime = time;
        return true;
    }

    long getDuration(long forwarderEnd) {
        return Math.subtractExact(freeTime == 0 ? forwarderEnd : freeTime, allocateTime);
    }
}


class NodeStat {
    private final long nodeId;
    private final Times times;

    NodeStat(long nodeId, Times times) {
        this.nodeId = nodeId;
        this.times = times;
    }

    long getNodeId() {
        return nodeId;
    }

    //region Control Age
    private long firstT1, firstT8P, lastT8P; // T8P is the T8 without counting misaddressed ones, can be different from T8
    private long oldT1;
    private BigInteger totalCtrlAge = BigInteger.ZERO;
    String controlAgeOutput = "";

    void updateControlAge(long t1, long t8) {
        // calculate control age, we do not consider mis-addressed ones when counting control age
        if (t1 == 0) return; // do not consider t1 = 0 situation (first triangle, add it at the end)

        if (firstT1 == 0) { // first packet
            firstT1 = oldT1 = t1;
            firstT8P = lastT8P = t8;
        } else if (t1 != oldT1) { // only consider when t1 != oldT1, since when t1 == oldT1, v1 == v2, result == 0
            // v1 = t8 - oldT1
            BigInteger v1 = BigInteger.valueOf(Math.subtractExact(t8, oldT1));
            // v2 = t8 - t1
            BigInteger v2 = BigInteger.valueOf(Math.subtractExact(t8, t1));
            // totalCtrlAge = v1^2 - v2^2
            // didn't do "/ 2" here, did it at the end
            totalCtrlAge = totalCtrlAge.add(v1.multiply(v1).subtract(v2.multiply(v2)));
            oldT1 = t1;
            lastT8P = Math.max(lastT8P, t8);
        }
    }

    void finishControlAge() {
        BigDecimal totalCtrlAge = new BigDecimal(this.totalCtrlAge).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY);
        BigDecimal averageCtrlAge = firstT1 == 0
                ? null // if no packet is forwarded
                : totalCtrlAge.divide(BigDecimal.valueOf(Math.subtractExact(lastT8P, firstT1)), Calculator.DEFAULT_ROUNDING_SCALE, Calculator.DEFAULT_ROUNDING_MODE);

        BigDecimal totalCtrlAgeNew;
        if (firstT1 == 0) { // if no packet is forwarded
            totalCtrlAgeNew = times.getSenderMaximumAge();
            System.out.printf("User %d didn't receive any packet, totalCtrlAge=%s%n", nodeId, totalCtrlAgeNew);
        } else { // triangles for ctrl age
            totalCtrlAgeNew = totalCtrlAge;
            // v1 = node.firstT8P - senderStartTime
            BigDecimal firstTriangle = BigDecimal.valueOf(Math.subtractExact(firstT8P, times.getSenderStart()));
            // v2 = node.firstT8P - node.firstT1
            BigDecimal firstTriangleMinus = BigDecimal.valueOf(Math.subtractExact(firstT8P, firstT1));

            // node.totalAge += (v1 * v1 - v2 * v2) / 2;
            totalCtrlAgeNew = totalCtrlAgeNew.add(
                    firstTriangle.multiply(firstTriangle).subtract(
                            firstTriangleMinus.multiply(firstTriangleMinus)
                    ).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY)
            );

            // v3 = senderEndTime - node.oldT1;
            BigDecimal lastTriangle = BigDecimal.valueOf(Math.subtractExact(times.getSenderEnd(), oldT1));
            // node.totalCtrlAge += v3 * v3 / 2;
            totalCtrlAgeNew = totalCtrlAgeNew.add(
                    lastTriangle.multiply(lastTriangle).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY)
            );
        }
        BigDecimal averageCtrlAgeNew = totalCtrlAgeNew.divide(times.getSenderDuration(), Calculator.DEFAULT_ROUNDING_SCALE, Calculator.DEFAULT_ROUNDING_MODE);

        controlAgeOutput = String.format("%d %d %d %d %d %s %s %s %s",
                firstT1 == 0 ? 0 : Math.subtractExact(firstT1, times.getSenderStart()),
                firstT8P == 0 ? 0 : Math.subtractExact(firstT8P, times.getSenderStart()),
                oldT1 == 0 ? 0 : Math.subtractExact(oldT1, times.getSenderStart()),
                lastT8P == 0 ? 0 : Math.subtractExact(lastT8P, times.getSenderStart()),
                Math.subtractExact(lastT8P, firstT1),
                totalCtrlAge,
                averageCtrlAge,
                totalCtrlAgeNew,
                averageCtrlAgeNew);
    }
    //endregion

    //region Data Age
    private long firstT3, firstT8, lastT8;
    private long oldT3, expectedControl;
    private long correctReceived, dropped, misAddressed;
    private BigInteger totalAge = BigInteger.ZERO;
    private String dataAgeOutput = "";


    void addRopped() {
        dropped++;
    }

    void setExpectedControl(long expectedControl) {
        this.expectedControl = expectedControl;
    }

    long getExpectedControl() {
        return expectedControl;
    }

    boolean updateDataAge(long sendTime, long t1, long t8) {
        // calculate data age
        if (t1 < expectedControl) { // mis-addressed
            misAddressed++;
            return false;
        }
        correctReceived++;
        lastT8 = Math.max(t8, lastT8);
        if (firstT3 == 0) { // first packet
            firstT3 = oldT3 = sendTime;
            firstT8 = t8;
        } else {
            // v1 = t8 - oldT3
            BigInteger v1 = BigInteger.valueOf(Math.subtractExact(t8, oldT3));
            // v2 = t8 - t3
            BigInteger v2 = BigInteger.valueOf(Math.subtractExact(t8, sendTime));
            // totalAge += v1^2 - v2^2
            // didn't do "/ 2" here, did it at the end
            totalAge = totalAge.add(v1.multiply(v1).subtract(v2.multiply(v2)));
            oldT3 = sendTime;
        }
        return true;
    }

    void finishDataAge() {
        BigDecimal totalAge = new BigDecimal(this.totalAge).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY);
        BigDecimal averageAge = firstT3 == 0
                ? null // if no packet is received
                : totalAge.divide(BigDecimal.valueOf(Math.subtractExact(lastT8, firstT3)), Calculator.DEFAULT_ROUNDING_SCALE, Calculator.DEFAULT_ROUNDING_MODE);
        if (Calculator.MONITOR_NODE_ID != 0 && nodeId == Calculator.MONITOR_NODE_ID)
            System.out.printf("totalAge %s%n", totalAge);

        BigDecimal totalAgeNew;
        // add first and last "triangles" for data age
        if (firstT3 == 0) { // if no packet is received
            totalAgeNew = times.getSenderMaximumAge();
            System.out.printf("User %d didn't receive any correct packet, totalAge=%s%n", nodeId, totalAgeNew);
        } else {
            totalAgeNew = totalAge;
            // v1 = node.firstT8 - senderStartTime
            BigDecimal firstTriangle = BigDecimal.valueOf(Math.subtractExact(firstT8, times.getSenderStart()));
            // v2 = node.firstT8 - node.firstT3
            BigDecimal firstTriangleMinus = BigDecimal.valueOf(Math.subtractExact(firstT8, firstT3));
            // node.totalAge += (v1 * v1 - v2 * v2) / 2;
            totalAgeNew = totalAgeNew.add(
                    firstTriangle.multiply(firstTriangle).subtract(
                            firstTriangleMinus.multiply(firstTriangleMinus)
                    ).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY)
            );

            // v3 = senderEndTime - node.oldT3;
            BigDecimal lastTriangle = BigDecimal.valueOf(Math.subtractExact(times.getSenderEnd(), oldT3));
            // node.totalAge += v3 * v3 / 2;
            totalAgeNew = totalAgeNew.add(
                    lastTriangle.multiply(lastTriangle).divide(Calculator.TWO, 1, RoundingMode.UNNECESSARY)
            );
        }
        BigDecimal averageAgeNew = totalAgeNew.divide(times.getSenderDuration(), Calculator.DEFAULT_ROUNDING_SCALE, Calculator.DEFAULT_ROUNDING_MODE);

        dataAgeOutput = String.format("%d %d %d %d %d %s %s %s %s",
                firstT3 == 0 ? 0 : Math.subtractExact(firstT3, times.getSenderStart()),
                firstT8 == 0 ? 0 : Math.subtractExact(firstT8, times.getSenderStart()),
                oldT3 == 0 ? 0 : Math.subtractExact(oldT3, times.getSenderStart()),
                lastT8 == 0 ? 0 : Math.subtractExact(lastT8, times.getSenderStart()),
                Math.subtractExact(lastT8, firstT3),
                totalAge,
                averageAge,
                totalAgeNew,
                averageAgeNew);
    }
    //endregion

    //region FIB
    private int maxFibEntries, currFibEntries;
    private long lastFibUpdate;
    private LinkedList<String> fibChanges;
    private BigInteger totalFibCount;
    private String fibOutput = "";

    void initFibCount(boolean needOutputFibChanges) {
        maxFibEntries = currFibEntries = 1;
        lastFibUpdate = times.getForwarderStart();
        totalFibCount = BigInteger.ZERO;
        if (needOutputFibChanges) {
            fibChanges = new LinkedList<>();
            fibChanges.addLast("timeR FIBEntries");
            fibChanges.addLast(String.format("%d %d", 0, 1));
        } else {
            fibChanges = null;
        }
    }

    void updateFibCount(long time, int add) {
        // count = count + entries * (newTime - lastUpdateTime)
        totalFibCount = totalFibCount.add(
                BigInteger.valueOf(currFibEntries).multiply(
                        BigInteger.valueOf(Math.subtractExact(time, lastFibUpdate))
                )
        );
        currFibEntries += add;
        maxFibEntries = Math.max(maxFibEntries, currFibEntries);
        lastFibUpdate = time;
        if (fibChanges != null)
            fibChanges.addLast(String.format("%d %d", Math.subtractExact(time, times.getForwarderStart()), currFibEntries));
    }

    void finishFibCount(String folder) {
        if (lastFibUpdate != times.getForwarderEnd()) {
            // count = count + entries * (end - lastUpdateTime)
            totalFibCount = totalFibCount.add(
                    BigInteger.valueOf(currFibEntries).multiply(
                            BigInteger.valueOf(Math.subtractExact(times.getForwarderEnd(), lastFibUpdate))
                    )
            );
            if (fibChanges != null)
                fibChanges.addLast(String.format("%s %d", times.getForwarderDuration(), currFibEntries));
        }
        BigDecimal averageFibCount = new BigDecimal(totalFibCount).divide(times.getForwarderDuration(), Calculator.DEFAULT_ROUNDING_SCALE, Calculator.DEFAULT_ROUNDING_MODE);
        fibOutput = String.format("%d %s %s", maxFibEntries, totalFibCount, averageFibCount);
        if (fibChanges != null) {
            try {
                Files.write(Paths.get(folder, nodeId + ".txt"), fibChanges);
            } catch (IOException e) {
                System.err.printf("Failed in writing mem events for node %d, cause: %n", nodeId);
                e.printStackTrace(System.err);
            }
        }
    }

    void printOutputWoFib(PrintStream output) {
        output.printf("%d %d %d %d %s %s%n",
                nodeId,
                correctReceived, dropped, misAddressed,
                dataAgeOutput,
                controlAgeOutput);
    }

    void printOutputWFib(PrintStream output) {
        output.printf("%d %d %d %d %s %s %s%n",
                nodeId,
                correctReceived, dropped, misAddressed,
                dataAgeOutput,
                controlAgeOutput,
                fibOutput);
    }
    //endregion
}
