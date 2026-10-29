package lemuri.be.dto;

import com.google.gson.annotations.SerializedName;

/** Reachability of a host; the text to show is decided by the view (see Ui.text). */
public enum HostStatus {
    ONLINE,
    OFFLINE,
    // The first versions saved this value with its Italian name: it is still read.
    @SerializedName(value = "UNKNOWN", alternate = "SCONOSCIUTO") UNKNOWN
}
