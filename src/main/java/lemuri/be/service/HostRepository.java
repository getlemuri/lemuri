package lemuri.be.service;

import java.util.List;
import lemuri.be.dto.Host;

/** Where the hosts and the folders created by hand (even empty ones) are saved. */
public interface HostRepository {
    /** {@code warning}: what went wrong reading the saved file, to tell the user; null if nothing. */
    record Data(List<String> folders, List<Host> hosts, String warning) {
        public Data(List<String> folders, List<Host> hosts) {
            this(folders, hosts, null);
        }
    }

    /**
     * Never null: if nothing is saved it returns two empty lists. If the file exists but cannot be read, it is
     * not lost: it is set aside (or, if that fails, saving stops) and this is reported in {@code warning}.
     */
    Data load();

    void save(Data data);
}
