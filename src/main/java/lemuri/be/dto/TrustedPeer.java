package lemuri.be.dto;

import com.google.gson.annotations.SerializedName;

/** A PC paired with the code: its key and the name it had when the pairing was made. */
public record TrustedPeer(@SerializedName(value = "key", alternate = "chiave") String key, @SerializedName(value = "name", alternate = "nome") String name) {}
