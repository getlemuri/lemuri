package lemuri.be.dto;

/** Last reading of a host's resources: the resources, or the reason why they are missing. */
public record MetricsResult(Resources resources, String error, long when) {}
