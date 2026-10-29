package lemuri.be.dto;

import java.util.Comparator;
import java.util.List;

/** CPU, memory and disks of a VPS, read from /proc and df. */
public record Resources(int cpu, Double cpuUsage, long memTotalKb, long memFreeKb, List<Disk> disks, long onForS) {
    public Disk root() {
        return disks.stream().filter(d -> d.dot().equals("/")).findFirst().orElse(null);
    }

    /** The fullest disk: it is the one that matters in a list. */
    public Disk fullest() {
        return disks.stream().max(Comparator.comparingDouble(Disk::quota)).orElse(null);
    }
}
