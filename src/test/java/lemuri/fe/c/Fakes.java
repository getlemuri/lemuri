package lemuri.fe.c;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import lemuri.be.dto.Host;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.HostRepository;
import lemuri.be.service.Transport;
import lemuri.be.service.Vault;

/** In-memory stand-ins for the backend services, for the controller tests. */
final class Fakes {
    private Fakes() {}

    static Host host(String id, String group) {
        Host h = new Host();
        h.id = id;
        h.name = id;
        h.address = "203.0.113.1";
        h.group = group;
        return h;
    }

    /** Hosts kept in memory; every save is recorded. */
    static final class Repository implements HostRepository {
        final List<Data> saved = new ArrayList<>();
        private final Data initial;

        Repository(List<String> folders, List<Host> hosts) {
            initial = new Data(folders, hosts);
        }

        @Override
        public Data load() {
            return new Data(new ArrayList<>(initial.folders()), new ArrayList<>(initial.hosts()));
        }

        @Override
        public void save(Data data) {
            saved.add(data);
        }

        Data last() {
            return saved.get(saved.size() - 1);
        }
    }

    /** A vault in memory, unlocked or locked at will. */
    static final class MemoryVault implements Vault {
        final Map<String, String> secrets = new HashMap<>();
        boolean open = true;
        private final List<Runnable> onUnlock = new ArrayList<>(), onLock = new ArrayList<>();

        void unlock() {
            open = true;
            onUnlock.forEach(Runnable::run);
        }

        @Override public boolean exists() { return true; }
        @Override public void onUnlock(Runnable r) { onUnlock.add(r); }
        @Override public void onLock(Runnable r) { onLock.add(r); }
        @Override public boolean isOpen() { return open; }
        @Override public void create(char[] main) {}
        @Override public boolean open(char[] main) { unlock(); return true; }
        @Override public void lock() { open = false; onLock.forEach(Runnable::run); }
        @Override public boolean changePassword(char[] current, char[] fresh) { return true; }
        @Override public String password(String key) { return open ? secrets.get(key) : null; }

        @Override
        public void remember(String key, String value) {
            if (!open) throw new IllegalStateException("locked");
            secrets.put(key, value);
        }

        @Override
        public void forget(String key) {
            if (!open) throw new IllegalStateException("locked");
            secrets.remove(key);
        }

        @Override public Map<String, String> entries() { return open ? Map.copyOf(secrets) : Map.of(); }

        @Override
        public void replaceAll(Map<String, String> entries) {
            if (!open) throw new IllegalStateException("locked");
            secrets.clear();
            secrets.putAll(entries);
        }
    }

    /** A transport that records what is sent and reports its state when told to. */
    static final class FakeTransport implements Transport {
        final StringBuilder sent = new StringBuilder();
        boolean closed;
        private BiConsumer<SessionStatus, String> state = (s, m) -> {};

        void report(SessionStatus s) {
            state.accept(s, null);
        }

        @Override public void onData(Consumer<String> cb) {}
        @Override public void onState(BiConsumer<SessionStatus, String> cb) { state = cb; }
        @Override public void launch(int columns, int lines) {}
        @Override public void send(String data) { sent.append(data); }
        @Override public void resize(int columns, int lines) {}

        @Override
        public void close() {
            closed = true;
            report(SessionStatus.CLOSED);
        }
    }
}
