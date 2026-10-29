package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.jcraft.jsch.HostKey;
import com.jcraft.jsch.HostKeyRepository;
import com.jcraft.jsch.JSch;
import com.jcraft.jsch.KeyPair;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import org.junit.jupiter.api.Test;

/** The questions from JSch and from the server during the connection. */
class SshPromptsTest {
    private static byte[] newKey(JSch jsch) throws Exception {
        return KeyPair.genKeyPair(jsch, KeyPair.ED25519).getPublicKeyBlob();
    }

    /** The real JSch known_hosts, with a key for web-01: the changed key shows in its check. */
    @Test
    void aChangedKeyIsDetectedAndKnownHostsIsUntouched() throws Exception {
        JSch jsch = new JSch();
        byte[] old = newKey(jsch);
        String line = "web-01 ssh-ed25519 " + Base64.getEncoder().encodeToString(old) + "\n";
        jsch.setKnownHosts(new ByteArrayInputStream(line.getBytes(StandardCharsets.UTF_8)));
        KnownKeys note = new KnownKeys(jsch.getHostKeyRepository());

        assertEquals(HostKeyRepository.OK, note.check("web-01", old));
        assertEquals(HostKeyRepository.NOT_INCLUDED, note.check("web-02", newKey(jsch)));
        assertFalse(note.changed(), "host conosciuto o nuovo: nessun allarme");

        byte[] fake = newKey(jsch);
        assertEquals(HostKeyRepository.CHANGED, note.check("web-01", fake));
        assertTrue(note.changed());

        // What JSch would do if someone answered yes: remove the old key and add the new one.
        note.remove("web-01", "ssh-ed25519");
        note.add(new HostKey("web-01", fake), null);
        assertEquals(HostKeyRepository.OK, note.check("web-01", old), "the old key stays");
        assertEquals(HostKeyRepository.CHANGED, note.check("web-01", fake), "the new one does not get in");
    }

    @Test
    void passwordOnlyForPasswordQuestion() {
        assertTrue(SshTransport.passwordQuestion("Password: "));
        assertTrue(SshTransport.passwordQuestion("root@web-01's password:"));
        assertTrue(SshTransport.passwordQuestion("(root@web-01) Password:"));
        assertTrue(SshTransport.passwordQuestion("root@code-server's password:"));
        assertTrue(SshTransport.passwordQuestion("Passwort: "));
    }

    /** Real prompts of PAM 2FA and password change modules, written as the server sends them. */
    @Test
    void codesAndChangesAreNotPasswords() {
        assertFalse(SshTransport.passwordQuestion("One-time password (OATH) for `root': "));
        assertFalse(SshTransport.passwordQuestion("OTP password: "));
        assertFalse(SshTransport.passwordQuestion("Verification code: "));
        assertFalse(SshTransport.passwordQuestion("Enter PASSCODE:"));
        assertFalse(SshTransport.passwordQuestion("YubiKey for `root': "));
        assertFalse(SshTransport.passwordQuestion("Duo two-factor login for root"));
        assertFalse(SshTransport.passwordQuestion("Token password: "));
        assertFalse(SshTransport.passwordQuestion("PIN: "));
        assertFalse(SshTransport.passwordQuestion("Current password: "));
        assertFalse(SshTransport.passwordQuestion("New password: "));
        assertFalse(SshTransport.passwordQuestion("Retype new password: "));
    }
}
