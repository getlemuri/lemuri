package lemuri.be.dto;

/**
 * An incoming delivery: {@code content} is the opened envelope (or null, with {@code error}).
 * {@code from} is the name of the contact who sealed it, {@code fromKey} their public key.
 */
public record Delivery(String id, String from, String fromKey, long when, Parcel content, String label, String error) {}
