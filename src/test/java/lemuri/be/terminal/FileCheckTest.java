package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.JSch;
import com.jcraft.jsch.Session;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import org.apache.sshd.common.file.virtualfs.VirtualFileSystemFactory;
import org.apache.sshd.server.SshServer;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;
import org.apache.sshd.sftp.server.SftpSubsystemFactory;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** What a double click may open: folders, text files up to 1 MB; not binary files nor large ones. */
class FileCheckTest {
    @TempDir Path root;
    @TempDir Path keys;

    private void files() throws Exception {
        Files.createDirectories(root.resolve("logs"));
        Files.writeString(root.resolve("notes.txt"), "hello\nworld\n");
        Files.write(root.resolve("empty.conf"), new byte[0]);
        Files.write(root.resolve("app.bin"), new byte[] {0x7f, 'E', 'L', 'F', 0, 1, 2});
        Files.write(root.resolve("huge.log"), new byte[(int) FileCheck.MAX_TEXT + 1]);
        Files.writeString(root.resolve("utf8 è.txt"), "città\n");
    }

    @Test
    void onThisComputer() throws Exception {
        files();
        assertEquals(FileCheck.Kind.FOLDER, FileCheck.local(root.resolve("logs")));
        assertEquals(FileCheck.Kind.TEXT, FileCheck.local(root.resolve("notes.txt")));
        assertEquals(FileCheck.Kind.TEXT, FileCheck.local(root.resolve("empty.conf")), "an empty file is opened");
        assertEquals(FileCheck.Kind.TEXT, FileCheck.local(root.resolve("utf8 è.txt")));
        assertEquals(FileCheck.Kind.BINARY, FileCheck.local(root.resolve("app.bin")));
        assertEquals(FileCheck.Kind.LARGE, FileCheck.local(root.resolve("huge.log")));
        assertEquals(FileCheck.Kind.MISSING, FileCheck.local(root.resolve("nothing")));
    }

    @Test
    void onTheServerOverSftp() throws Exception {
        files();
        SshServer server = SshServer.setUpDefaultServer();
        server.setHost("127.0.0.1");
        server.setPort(0);
        server.setKeyPairProvider(new SimpleGeneratorHostKeyProvider(keys.resolve("hostkey")));
        server.setPasswordAuthenticator((u, p, s) -> true);
        server.setSubsystemFactories(List.of(new SftpSubsystemFactory()));
        server.setFileSystemFactory(new VirtualFileSystemFactory(root));
        server.start();
        Session session = new JSch().getSession("u", "127.0.0.1", server.getPort());
        try {
            session.setPassword("x");
            session.setConfig("StrictHostKeyChecking", "no");
            session.connect(5000);
            ChannelSftp c = (ChannelSftp) session.openChannel("sftp");
            c.connect(5000);
            assertEquals(FileCheck.Kind.FOLDER, FileCheck.remote(c, "/logs"));
            assertEquals(FileCheck.Kind.TEXT, FileCheck.remote(c, "/notes.txt"));
            assertEquals(FileCheck.Kind.TEXT, FileCheck.remote(c, "/empty.conf"));
            assertEquals(FileCheck.Kind.BINARY, FileCheck.remote(c, "/app.bin"));
            assertEquals(FileCheck.Kind.LARGE, FileCheck.remote(c, "/huge.log"));
            assertEquals(FileCheck.Kind.MISSING, FileCheck.remote(c, "/nothing"));
            c.disconnect();
        } finally {
            session.disconnect();
            server.stop();
        }
    }
}
