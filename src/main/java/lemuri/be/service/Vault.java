package lemuri.be.service;

import java.io.IOException;
import java.util.Map;

/**
 * Secrets encrypted with a master password: host passwords, sharing keys,
 * saved sign-ins. Until it is unlocked, it gives nothing.
 */
public interface Vault {
    boolean exists();

    /** Notifies every time the vault is unlocked or created, from the thread that unlocked it. */
    void onUnlock(Runnable r);

    /** Notifies when the vault is locked: whoever keeps secrets taken from it in memory must forget them. */
    void onLock(Runnable r);

    boolean isOpen();

    void create(char[] main) throws IOException;

    /** False if the master password is wrong. */
    boolean open(char[] main) throws IOException;

    void lock();

    /** Changes the master password; false if the current one is wrong. The vault must be unlocked. */
    boolean changePassword(char[] current, char[] fresh) throws IOException;

    /** null if the vault is locked or the entry does not exist. */
    String password(String key);

    void remember(String key, String value) throws IOException;

    void forget(String key) throws IOException;

    /** A copy of every entry, for a backup; empty if the vault is locked. */
    Map<String, String> entries();

    /** Replaces every entry with these, in one save (an import). The vault must be unlocked. */
    void replaceAll(Map<String, String> entries) throws IOException;
}
