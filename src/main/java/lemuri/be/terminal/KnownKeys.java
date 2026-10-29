package lemuri.be.terminal;

import com.jcraft.jsch.HostKey;
import com.jcraft.jsch.HostKeyRepository;
import com.jcraft.jsch.UserInfo;

/**
 * ~/.ssh/known_hosts as seen by JSch, plus the memory of the last check.
 * This way a CHANGED key is recognised from the result of {@link #check}, not from the
 * text of the JSch message, which can change with every update. And after a changed
 * key the file is not touched, whatever the caller answers.
 */
final class KnownKeys implements HostKeyRepository {
    private final HostKeyRepository file;
    private volatile boolean changed;

    KnownKeys(HostKeyRepository file) {
        this.file = file;
    }

    /** True if the server showed a key different from the one remembered for it. */
    boolean changed() {
        return changed;
    }

    @Override
    public int check(String host, byte[] key) {
        int r = file.check(host, key);
        if (r == CHANGED) changed = true;
        return r;
    }

    @Override
    public void add(HostKey key, UserInfo questions) {
        if (!changed) file.add(key, questions);
    }

    @Override
    public void remove(String host, String type) {
        if (!changed) file.remove(host, type);
    }

    @Override
    public void remove(String host, String type, byte[] key) {
        if (!changed) file.remove(host, type, key);
    }

    @Override public String getKnownHostsRepositoryID() { return file.getKnownHostsRepositoryID(); }

    @Override public HostKey[] getHostKey() { return file.getHostKey(); }

    @Override public HostKey[] getHostKey(String host, String type) { return file.getHostKey(host, type); }
}
