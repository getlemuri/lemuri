package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static lemuri.be.util.RemoteNames.literal;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.JSch;
import com.jcraft.jsch.Session;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
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

/** JSch reads "*" and "?" in server paths as wildcards: with {@link RemoteNames#literal} they are plain names. On a real SFTP server. */
class RemoteNamesSftpTest {
    @TempDir Path root;
    @TempDir Path local;
    private SshServer server;
    private Session session;
    private ChannelSftp c;

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

    @Test
    void escapesOnlyTheWildcardsAndTheBackslash() {
        assertEquals("/up/a\\*b\\?c\\\\d.txt", literal("/up/a*b?c\\d.txt"));
        assertEquals("/plain/name", literal("/plain/name"));
    }

    /** The upload sequence of Transfers: a leftover partial file and a name with "?" must not cost "report1.txt". */
    @Test
    void anUploadWithAQuestionMarkTouchesOnlyItsOwnFile() throws Exception {
        Files.createDirectories(root.resolve("up"));
        Files.writeString(root.resolve("up/report1.txt"), "REPORT 1");
        Files.writeString(root.resolve("up/.report1.txt.lemuri-part"), "leftover");
        Path f = Files.writeString(local.resolve("report?.txt"), "NEW REPORT");
        String dest = "/up/report?.txt", partial = "/up/.report?.txt.lemuri-part";
        try (InputStream in = Files.newInputStream(f)) {
            c.put(in, literal(partial), ChannelSftp.OVERWRITE);
        }
        try {
            if (!c.lstat(literal(dest)).isDir()) c.rm(literal(dest));
        } catch (Exception missing) {
            // it was not there
        }
        c.rename(literal(partial), literal(dest));
        assertEquals("REPORT 1", Files.readString(root.resolve("up/report1.txt")));
        assertEquals("leftover", Files.readString(root.resolve("up/.report1.txt.lemuri-part")));
        assertEquals("NEW REPORT", Files.readString(root.resolve("up/report?.txt")));
    }

    @Test
    void aDownloadOfAStarGetsThatFile() throws Exception {
        Files.createDirectories(root.resolve("dl"));
        Files.writeString(root.resolve("dl/a*"), "A STAR");
        Files.writeString(root.resolve("dl/ab"), "AB");
        Path to = local.resolve("a*");
        assertEquals(6, c.stat(literal("/dl/a*")).getSize());
        try (OutputStream out = Files.newOutputStream(to)) {
            c.get(literal("/dl/a*"), out);
        }
        assertEquals("A STAR", Files.readString(to));
    }

    @Test
    void aFolderWithWildcardsInItsNameIsListed() throws Exception {
        Files.createDirectories(root.resolve("odd*dir?"));
        Files.writeString(root.resolve("odd*dir?/x"), "X");
        Files.createDirectories(root.resolve("oddXdirY"));
        Files.writeString(root.resolve("oddXdirY/y"), "Y");
        Set<String> names = c.ls(literal("/odd*dir?")).stream().map(ChannelSftp.LsEntry::getFilename)
                .filter(n -> !n.equals(".") && !n.equals("..")).collect(Collectors.toSet());
        assertEquals(Set.of("x"), names);
    }
}
