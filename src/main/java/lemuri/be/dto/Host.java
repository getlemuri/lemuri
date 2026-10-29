package lemuri.be.dto;

import com.google.gson.annotations.SerializedName;
import java.util.List;

/** A VPS reachable over SSH. Plain public fields: it is saved as it is in JSON (the Italian field names of the first versions are still read). */
public class Host {
    public String id;
    @SerializedName(value = "name", alternate = "nome") public String name;
    /** Tree folder that contains the host, like "Production/EU"; empty = directly in the datacenter. */
    @SerializedName(value = "group", alternate = "gruppo") public String group = "";
    @SerializedName(value = "address", alternate = "indirizzo") public String address;
    @SerializedName(value = "port", alternate = "porta") public int port = 22;
    @SerializedName(value = "user", alternate = "utente") public String user = "root";
    @SerializedName(value = "authMethod", alternate = "autenticazione") public AuthMethod authMethod = AuthMethod.KEY;
    /** Name of the file in ~/.ssh, for key authentication. */
    @SerializedName(value = "key", alternate = "chiave") public String key;
    public String note = "";
    @SerializedName(value = "os", alternate = "sistema") public String os;
    @SerializedName(value = "state", alternate = "stato") public HostStatus state = HostStatus.UNKNOWN;
    /** Name of the contact it came from, if it arrived through sharing. */
    @SerializedName(value = "fromContact", alternate = {"fromColleague", "daCollega"}) public String fromContact;
    /** Still in the "Received from team" section, outside your own folders: moving it to a folder takes it out of there. */
    @SerializedName(value = "received", alternate = "ricevuto") public boolean received;
    /**
     * Jump hosts, as in ssh's ProxyJump: comma-separated hops, each the name of another host in the list or
     * {@code [user@]host[:port]}. Null or empty: direct connection.
     */
    public String jump;
    /** The hops resolved just before connecting (see Jumps): never saved. Null if the host is reached directly. */
    public transient List<Host> route;
    /** Why {@link #jump} could not be resolved, or null: never saved. */
    public transient String routeError;

    public enum AuthMethod {
        // The first versions saved this value with its Italian name: it is still read.
        @SerializedName(value = "KEY", alternate = "CHIAVE") KEY,
        PASSWORD,
        AGENT
    }

    public Host copy() {
        Host h = new Host();
        h.id = id;
        h.name = name;
        h.group = group;
        h.address = address;
        h.port = port;
        h.user = user;
        h.authMethod = authMethod;
        h.key = key;
        h.note = note;
        h.os = os;
        h.state = state;
        h.fromContact = fromContact;
        h.received = received;
        h.jump = jump;
        h.route = route;
        h.routeError = routeError;
        return h;
    }

    public String destination() {
        return user + "@" + address + ":" + port;
    }
}
