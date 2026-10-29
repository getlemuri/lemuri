package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Comparator;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/** Runs with user.home inside target (see the pom): ~/.ssh here is a test folder. */
class SshFolderTest {
    private static final Path SSH = SshFolder.dir();
    private static final byte[] KEY = "-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n-----END OPENSSH PRIVATE KEY-----\n".getBytes(StandardCharsets.US_ASCII);

    @BeforeEach
    void clear() throws IOException {
        assertTrue(SSH.toString().contains("target"), "the tests must not use the real home: " + SSH);
        if (Files.exists(SSH)) try (var w = Files.walk(SSH)) {
            for (Path p : w.sorted(Comparator.reverseOrder()).toList()) Files.delete(p);
        }
    }

    @Test
    void onlyBareNames() {
        for (String ok : List.of("id_ed25519", "id_rsa.pub", "known_hosts", "config", "work@host-1"))
            assertTrue(SshFolder.safeName(ok), ok);
        for (String bad : List.of("", "..", ".", ".bashrc", "../x", "a/b", "/etc/passwd", "a\\b", "x\ny", "x".repeat(101)))
            assertFalse(SshFolder.safeName(bad), bad);
    }

    @Test
    void privateKeysAreFoundByTheirContent() throws IOException {
        Files.createDirectories(SSH);
        Files.write(SSH.resolve("work"), KEY);
        Files.write(SSH.resolve("work.pub"), "ssh-ed25519 AAAA work".getBytes(StandardCharsets.US_ASCII));
        Files.write(SSH.resolve("known_hosts"), "host ssh-ed25519 AAAA\n".getBytes(StandardCharsets.US_ASCII));
        Files.write(SSH.resolve("notes.txt"), "hello".getBytes(StandardCharsets.US_ASCII));
        assertEquals(List.of("work"), SshFolder.privateKeys());
    }

    @Test
    void installNeverOverwrites() throws IOException {
        SshFolder.Installed a = SshFolder.install("id_x", KEY);
        assertEquals(SshFolder.Outcome.ADDED, a.outcome());
        assertEquals("rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(SSH.resolve("id_x"))));
        assertEquals("rwx------", PosixFilePermissions.toString(Files.getPosixFilePermissions(SSH)));
        assertEquals(SshFolder.Outcome.SAME, SshFolder.install("id_x", KEY).outcome());

        byte[] other = "-----BEGIN OPENSSH PRIVATE KEY-----\nzzz\n".getBytes(StandardCharsets.US_ASCII);
        SshFolder.Installed b = SshFolder.install("id_x", other);
        assertEquals(SshFolder.Outcome.BESIDE, b.outcome());
        assertEquals("id_x.imported", b.savedAs());
        assertArrayEquals(KEY, Files.readAllBytes(SSH.resolve("id_x")), "the key that was here stays");
        assertArrayEquals(other, Files.readAllBytes(SSH.resolve("id_x.imported")));
        assertEquals("id_x.imported-2", SshFolder.install("id_x", "-----BEGIN OPENSSH PRIVATE KEY-----\nthird\n".getBytes(StandardCharsets.US_ASCII)).savedAs());

        SshFolder.install("id_x.pub", "ssh-ed25519 AAAA".getBytes(StandardCharsets.US_ASCII));
        assertEquals("rw-r--r--", PosixFilePermissions.toString(Files.getPosixFilePermissions(SSH.resolve("id_x.pub"))));
    }

    @Test
    void knownHostsGetsOnlyTheMissingLines() throws IOException {
        SshFolder.install("known_hosts", "a key1\nb key2".getBytes(StandardCharsets.UTF_8));
        SshFolder.Installed i = SshFolder.install("known_hosts", "b key2\nc key3\n".getBytes(StandardCharsets.UTF_8));
        assertEquals(SshFolder.Outcome.MERGED, i.outcome());
        assertEquals("a key1\nb key2\nc key3\n", Files.readString(SSH.resolve("known_hosts")));
        assertEquals(SshFolder.Outcome.SAME, SshFolder.install("known_hosts", "c key3\n".getBytes(StandardCharsets.UTF_8)).outcome());
    }

    /** A link already in ~/.ssh is not followed: the file it points to is not touched. */
    @Test
    void linksAreNotFollowed() throws IOException {
        Files.createDirectories(SSH);
        Path outside = SSH.resolveSibling("outside.txt");
        Files.writeString(outside, "keep me");
        Files.createSymbolicLink(SSH.resolve("config"), outside);
        SshFolder.Installed i = SshFolder.install("config", "Host x".getBytes(StandardCharsets.UTF_8));
        assertEquals(SshFolder.Outcome.BESIDE, i.outcome());
        assertEquals("keep me", Files.readString(outside));
        assertThrows(IOException.class, () -> SshFolder.read("config"), "a link is not read either");
        Files.delete(outside);
    }

    /** The table shows what an import will do: the preview must say what install then does, and write nothing. */
    @Test
    void previewSaysWhatInstallWillDo() throws IOException {
        assertEquals(SshFolder.Outcome.ADDED, SshFolder.preview("id_x", KEY));
        assertFalse(Files.exists(SSH.resolve("id_x")), "a preview writes nothing");
        SshFolder.install("id_x", KEY);
        assertEquals(SshFolder.Outcome.SAME, SshFolder.preview("id_x", KEY));
        assertEquals(SshFolder.Outcome.BESIDE, SshFolder.preview("id_x", "other".getBytes(StandardCharsets.US_ASCII)));
        SshFolder.install("known_hosts", "a k\n".getBytes(StandardCharsets.UTF_8));
        assertEquals(SshFolder.Outcome.MERGED, SshFolder.preview("known_hosts", "b k\n".getBytes(StandardCharsets.UTF_8)));
        assertEquals(SshFolder.Outcome.SAME, SshFolder.preview("known_hosts", "a k\n".getBytes(StandardCharsets.UTF_8)));
    }

    @Test
    void installRefusesPaths() {
        assertThrows(IOException.class, () -> SshFolder.install("../evil", KEY));
    }

    /** Analysis #12: known_hosts from a backup never gets a CA for every host or a revoked marker, new or merged. */
    @Test
    void knownHostsNeverGetsCertAuthorityLines() throws IOException {
        byte[] arrived = "@cert-authority * ssh-ed25519 AAAA ca\n@revoked * ssh-ed25519 BBBB\nhost1 ssh-ed25519 CCCC\n".getBytes(StandardCharsets.UTF_8);
        assertEquals(List.of("host1 ssh-ed25519 CCCC"), SshFolder.newKnownHosts(arrived));
        assertEquals(2, SshFolder.markerLines(arrived));
        SshFolder.install("known_hosts", arrived);
        assertEquals("host1 ssh-ed25519 CCCC\n", Files.readString(SSH.resolve("known_hosts")));
        SshFolder.install("known_hosts", "@cert-authority *.example.org ssh-ed25519 DDDD\nhost2 ssh-ed25519 EEEE\n".getBytes(StandardCharsets.UTF_8));
        assertEquals("host1 ssh-ed25519 CCCC\nhost2 ssh-ed25519 EEEE\n", Files.readString(SSH.resolve("known_hosts")));
    }

    @Test
    void onlyKeysKnownHostsAndConfig() {
        byte[] priv = KEY, pub = "ssh-ed25519 AAAA me@pc\n".getBytes(StandardCharsets.US_ASCII);
        assertEquals(SshFolder.Kind.PRIVATE_KEY, SshFolder.kind("id_x", priv));
        assertEquals(SshFolder.Kind.PUBLIC_KEY, SshFolder.kind("id_x.pub", pub));
        assertEquals(SshFolder.Kind.KNOWN_HOSTS, SshFolder.kind("known_hosts", pub));
        assertEquals(SshFolder.Kind.CONFIG, SshFolder.kind("config", "Host x".getBytes(StandardCharsets.US_ASCII)));
        for (String n : List.of("authorized_keys", "authorized_keys2", "rc", "environment", "known_hosts2"))
            assertEquals(null, SshFolder.kind(n, priv), n);
        assertEquals(null, SshFolder.kind("notes", "hello".getBytes(StandardCharsets.US_ASCII)));
        assertEquals(null, SshFolder.kind("x.pub", "two\nlines".getBytes(StandardCharsets.US_ASCII)));
        assertThrows(IOException.class, () -> SshFolder.install("authorized_keys", pub));
    }

    @Test
    void configDirectivesThatRunCommandsAreFound() {
        String config = "Host a\n  ProxyCommand nc %h %p\nHost b\n  LocalCommand echo hi\nMatch host c exec \"true\"\n  User x\n";
        assertEquals(List.of("ProxyCommand", "LocalCommand", "Match exec"), SshFolder.commandDirectives(config.getBytes(StandardCharsets.UTF_8)));
        assertEquals(List.of(), SshFolder.commandDirectives("Host a\n  User x\n".getBytes(StandardCharsets.UTF_8)));
    }
}
