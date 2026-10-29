package lemuri.be.dto;

/**
 * Pending package updates and whether the VPS asks to be rebooted. {@code packages} is null when no known
 * package manager answered; {@code reboot} is null when the system has no way to tell.
 */
public record Updates(Integer packages, Integer security, Boolean reboot, String rebootBecause, String error, long when) {}
