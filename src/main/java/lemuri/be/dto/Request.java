package lemuri.be.dto;

/** Pairing request from {@code peer}: accept it if the code is the same on both screens. */
public record Request(Peer peer, String code) {}
