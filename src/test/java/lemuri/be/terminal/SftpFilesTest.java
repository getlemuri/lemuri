package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.JSch;
import com.jcraft.jsch.Session;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.sshd.common.file.virtualfs.VirtualFileSystemFactory;
import org.apache.sshd.server.SshServer;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;
import org.apache.sshd.sftp.server.SftpSubsystemFactory;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** Uploads and downloads against a real SFTP server in memory. */
class SftpFilesTest {
    @TempDir Path root;
    @TempDir Path local;
    private SshServer server;
    private Session session;
    private ChannelSftp c;

    /** Records what the transfer reports; cancels after {@code cancelAfter} bytes, if not negative. */
    private static final class Recorder implements SftpFiles.Watch {
        final List<String> files = new ArrayList<>();
        int skipped;
        long bytes;
        long cancelAfter = -1;

        @Override public boolean cancelled() { return cancelAfter >= 0 && bytes > cancelAfter; }
        @Override public void skipped() { skipped++; }
        @Override public void file(String name) { files.add(name); }
        @Override public void init(int op, String src, String dest, long max) {}
        @Override public boolean count(long n) { bytes += n; return !cancelled(); }
        @Override public void end() {}
    }

    @BeforeEach
    void connect() throws Exception {
        server = SshServer.setUpDefaultServer();
        server.setHost("127.0.0.1");
        server.setPort(0);
        server.setKeyPairProvider(new SimpleGeneratorHostKeyProvider(local.resolve("hostkey")));
        server.setPasswordAuthenticator((u, p, s) -> true);
        server.setSubsystemFactories(List.of(new SftpSubsystemFactory()));
        server.setFileSystemFactory(new VirtualFileSystemFactory(root));
        server.start();
        session = new JSch().getSession("u", "127.0.0.1", server.getPort());
        session.setPassword("x");
        session.setConfig("StrictHostKeyChecking", "no");
        session.connect(5000);
        c = (ChannelSftp) session.openChannel("sftp");
        c.connect(5000);
    }

    @AfterEach
    void close() throws Exception {
        if (c != null) c.disconnect();
        if (session != null) session.disconnect();
        server.stop();
    }

    private static Set<String> names(Path dir) throws Exception {
        try (var l = Files.list(dir)) {
            return l.map(p -> p.getFileName().toString()).collect(Collectors.toSet());
        }
    }

    @Test
    void anUploadReplacesTheFileAndLeavesNothingBehind() throws Exception {
        Files.createDirectories(root.resolve("up"));
        Files.writeString(root.resolve("up/app.conf"), "OLD");
        File f = Files.writeString(local.resolve("app.conf"), "NEW").toFile();
        assertEquals(List.of("app.conf"), SftpFiles.existing(c, "/up", List.of(f)));
        SftpFiles.upload(c, f, "/up", new Recorder());
        assertEquals("NEW", Files.readString(root.resolve("up/app.conf")));
        assertEquals(Set.of("app.conf"), names(root.resolve("up")));
    }

    /** More than three files: one listing of the folder, with the same answer as asking file by file. */
    @Test
    void manyFilesAreCheckedWithOneListing() throws Exception {
        Files.createDirectories(root.resolve("up"));
        Files.writeString(root.resolve("up/a.txt"), "A");
        Files.writeString(root.resolve("up/report1.txt"), "R");
        List<File> files = new ArrayList<>();
        for (String n : List.of("a.txt", "b.txt", "c.txt", "report?.txt", "e.txt")) files.add(Files.writeString(local.resolve(n), n).toFile());
        assertEquals(List.of("a.txt"), SftpFiles.existing(c, "/up", files));
        assertEquals(List.of(), SftpFiles.existing(c, "/missing", files), "a folder that is not there has nothing in it");
    }

    /** "report?.txt" must not match report1.txt, nor the leftover of an interrupted upload. */
    @Test
    void wildcardsInNamesTouchOnlyTheirOwnFile() throws Exception {
        Files.createDirectories(root.resolve("up"));
        Files.writeString(root.resolve("up/report1.txt"), "REPORT 1");
        Files.writeString(root.resolve("up/.report1.txt.lemuri-part"), "leftover");
        File f = Files.writeString(local.resolve("report?.txt"), "NEW REPORT").toFile();
        assertTrue(SftpFiles.existing(c, "/up", List.of(f)).isEmpty());
        SftpFiles.upload(c, f, "/up", new Recorder());
        assertEquals("REPORT 1", Files.readString(root.resolve("up/report1.txt")));
        assertEquals("leftover", Files.readString(root.resolve("up/.report1.txt.lemuri-part")));
        assertEquals("NEW REPORT", Files.readString(root.resolve("up/report?.txt")));
    }

    @Test
    void aCancelledUploadKeepsTheOldFile() throws Exception {
        Files.createDirectories(root.resolve("up"));
        Files.writeString(root.resolve("up/big.bin"), "OLD");
        File f = Files.write(local.resolve("big.bin"), new byte[512 * 1024]).toFile();
        Recorder r = new Recorder();
        r.cancelAfter = 1;
        SftpFiles.upload(c, f, "/up", r);
        assertEquals("OLD", Files.readString(root.resolve("up/big.bin")));
        assertEquals(Set.of("big.bin"), names(root.resolve("up")), "no partial file left");
    }

    @Test
    void aFolderGoesUpWithoutItsLinks() throws Exception {
        Path dir = Files.createDirectories(local.resolve("site/css"));
        Files.writeString(local.resolve("site/index.html"), "<h1>");
        Files.writeString(dir.resolve("a.css"), "body{}");
        Files.createSymbolicLink(local.resolve("site/outside"), Path.of("/etc"));
        Files.createDirectories(root.resolve("www"));
        Recorder r = new Recorder();
        SftpFiles.upload(c, local.resolve("site").toFile(), "/www", r);
        assertEquals("<h1>", Files.readString(root.resolve("www/site/index.html")));
        assertEquals("body{}", Files.readString(root.resolve("www/site/css/a.css")));
        assertFalse(Files.exists(root.resolve("www/site/outside")));
        assertEquals(1, r.skipped);
        assertEquals(10, SftpFiles.localSize(local.resolve("site").toFile()));
    }

    @Test
    void aFileComesDownAndReplacesTheLocalOneWithNothingLeftBehind() throws Exception {
        Files.createDirectories(root.resolve("logs"));
        Files.writeString(root.resolve("logs/app.log"), "NEW LOG");
        Path into = Files.createDirectories(local.resolve("down"));
        Files.writeString(into.resolve("app.log"), "OLD");
        Recorder r = new Recorder();
        assertEquals(7, SftpFiles.remoteSize(c, "/logs/app.log"));
        SftpFiles.download(c, "/logs/app.log", into.toFile(), r);
        assertEquals("NEW LOG", Files.readString(into.resolve("app.log")));
        assertEquals(Set.of("app.log"), names(into));
        assertEquals(List.of("app.log"), r.files);
    }

    @Test
    void aFolderComesDownWithoutItsLinks() throws Exception {
        Path site = Files.createDirectories(root.resolve("srv/site"));
        Files.writeString(site.resolve("index.html"), "<h1>");
        Files.createDirectories(site.resolve("css"));
        Files.writeString(site.resolve("css/a.css"), "a{}");
        Files.createSymbolicLink(site.resolve("etc"), Path.of("/etc"));
        Path into = Files.createDirectories(local.resolve("down"));
        Recorder r = new Recorder();
        assertEquals(7, SftpFiles.remoteSize(c, "/srv/site"), "the link counts nothing");
        SftpFiles.download(c, "/srv/site", into.toFile(), r);
        assertEquals("<h1>", Files.readString(into.resolve("site/index.html")));
        assertEquals("a{}", Files.readString(into.resolve("site/css/a.css")));
        assertFalse(Files.exists(into.resolve("site/etc"), LinkOption.NOFOLLOW_LINKS), "the link is not followed");
        assertEquals(1, r.skipped);
    }

    @Test
    void aCancelledDownloadKeepsTheOldFile() throws Exception {
        Files.write(root.resolve("big.bin"), new byte[512 * 1024]);
        Path into = Files.createDirectories(local.resolve("down"));
        Files.writeString(into.resolve("big.bin"), "OLD");
        Recorder r = new Recorder();
        r.cancelAfter = 1;
        SftpFiles.download(c, "/big.bin", into.toFile(), r);
        assertEquals("OLD", Files.readString(into.resolve("big.bin")));
        assertEquals(Set.of("big.bin"), names(into), "no partial file left");
    }

    @Test
    void namesThatCannotBeFilesHereAreRefused() {
        assertTrue(SftpFiles.safeLocalName("notes.txt"));
        for (String bad : new String[] {"", ".", "..", "a/b", "a\\b", "a\u0007b"}) assertFalse(SftpFiles.safeLocalName(bad), bad);
        assertEquals("site", SftpFiles.lastName("/srv/site/"));
    }
}
