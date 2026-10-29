package lemuri.be.dto;

/** As in df: the figures exclude the space reserved for root, so used + free can be less than the total. */
public record Disk(String dot, long totalKb, long usedKb, long freeKb) {
    public double quota() {
        return usedKb + freeKb == 0 ? 0 : (double) usedKb / (usedKb + freeKb);
    }
}
