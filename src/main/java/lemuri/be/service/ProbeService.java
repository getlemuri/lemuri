package lemuri.be.service;

import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;

/** Periodically checks whether the hosts answer on the SSH port. */
public interface ProbeService {
    /** Minutes between two rounds: fewer lines in the servers' logs than every minute. */
    int DEFAULT_MINUTES = 5;

    /** Starts the periodic round; the outcomes reach {@code outcomes} from a worker thread. */
    void launch(Consumer<Map<String, HostStatus>> outcomes);

    /** The host list changed: new hosts or hosts with a changed address are tried right away. */
    void list(List<Host> hosts);

    /** Minutes between two rounds; 0: never on its own, only with {@link #all}. */
    void every(int minutes);

    /** Tries all hosts again right away. */
    void all();

    void stop();
}
