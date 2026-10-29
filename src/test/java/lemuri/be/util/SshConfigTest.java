package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/** ~/.ssh/config read with the rules of OpenSSH. The home is the fake one of the pom. */
class SshConfigTest {
    private static final Path SSH = SshConfig.sshDir();

    @BeforeEach
    void clear() throws Exception {
        assertTrue(SSH.toString().contains("target"), "the tests must not use the real home: " + SSH);
        Files.createDirectories(SSH.resolve("config.d"));
        try (var l = Files.list(SSH.resolve("config.d"))) {
            for (Path p : l.toList()) Files.delete(p);
        }
    }

    private static Map<String, SshConfig.Entry> read(String text) throws Exception {
        Path f = SSH.resolve("config");
        Files.writeString(f, text);
        return SshConfig.read(f).stream().collect(Collectors.toMap(SshConfig.Entry::alias, Function.identity()));
    }

    @Test
    void theFirstValueWinsAndHostStarGivesTheDefaults() throws Exception {
        Map<String, SshConfig.Entry> m = read(String.join("\n",
                "# my servers",
                "Host web web-alias",
                "    HostName 203.0.113.10",
                "    User deploy",
                "    Port 2222",
                "",
                "Host db",
                "  Hostname=203.0.113.20",
                "",
                "Host *.internal",
                "  User ops",
                "",
                "Host *",
                "  User root",
                "  Port 22",
                "  IdentityFile ~/.ssh/id_ed25519"));
        assertEquals(List.of("db", "web", "web-alias"), m.keySet().stream().sorted().toList());
        SshConfig.Entry web = m.get("web");
        assertEquals("203.0.113.10", web.hostName());
        assertEquals("deploy", web.user());
        assertEquals(2222, web.port());
        assertEquals(SSH.resolve("id_ed25519").toString(), web.identityFile());
        SshConfig.Entry db = m.get("db");
        assertEquals("root", db.user());
        assertEquals(22, db.port());
        assertEquals("web-alias", m.get("web-alias").alias());
    }

    @Test
    void patternsNegationsAndMatchBlocks() throws Exception {
        Map<String, SshConfig.Entry> m = read(String.join("\n",
                "Host app1 app2 !app3 *.example.com",
                "  User app",
                "Host app2",
                "  User other",
                "Match host app1",
                "  User matched",
                "Host * !app2",
                "  Port 2200"));
        assertEquals(List.of("app1", "app2"), m.keySet().stream().sorted().toList());
        assertEquals("app", m.get("app1").user());
        assertEquals("app", m.get("app2").user());
        assertEquals(2200, m.get("app1").port());
        // Negated: "Host * !app2" does not apply to app2.
        assertEquals(22, m.get("app2").port());
        assertEquals("app1", m.get("app1").hostName());
    }

    @Test
    void includeQuotesPercentAndProxyJump() throws Exception {
        Files.writeString(SSH.resolve("config.d/work.conf"), String.join("\n",
                "Host bastion-behind",
                "  HostName %h.corp.example",
                "  ProxyJump jump.example",
                "  IdentityFile \"/opt/keys/my key\""));
        Map<String, SshConfig.Entry> m = read(String.join("\n",
                "Include config.d/*.conf",
                "Host gh",
                "  HostName github.com",
                "  User git",
                "Host direct",
                "  ProxyJump none"));
        SshConfig.Entry b = m.get("bastion-behind");
        assertEquals("bastion-behind.corp.example", b.hostName());
        assertEquals("jump.example", b.proxyJump());
        assertEquals("/opt/keys/my key", b.identityFile());
        assertTrue(m.get("gh").gitService());
        assertFalse(b.gitService());
        assertNull(m.get("direct").proxyJump());
    }

    @Test
    void aMissingFileHasNoHosts() throws Exception {
        assertTrue(SshConfig.read(SSH.resolve("does-not-exist")).isEmpty());
    }

    @Test
    void becomingALemuriHost() {
        Host withKey = new SshConfig.Entry("web", "203.0.113.10", "deploy", 2222, SSH.resolve("id_web").toString(), null).toHost("Prod");
        assertEquals("web", withKey.name);
        assertEquals("203.0.113.10", withKey.address);
        assertEquals(2222, withKey.port);
        assertEquals("deploy", withKey.user);
        assertEquals("Prod", withKey.group);
        assertEquals(Host.AuthMethod.KEY, withKey.authMethod);
        // A key in ~/.ssh is saved by name, one elsewhere with its full path.
        assertEquals("id_web", withKey.key);
        assertEquals("/opt/keys/k", new SshConfig.Entry("a", "h", null, 22, "/opt/keys/k", null).toHost("").key);
        Host agent = new SshConfig.Entry("db", "203.0.113.20", null, 22, null, null).toHost("");
        assertEquals(Host.AuthMethod.AGENT, agent.authMethod);
        assertEquals(System.getProperty("user.name"), agent.user);
        assertEquals(8, agent.id.length());
    }
}
