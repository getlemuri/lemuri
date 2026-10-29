package lemuri.be.storage;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.service.HostRepository;

/** Runs with user.home inside target (see the pom): it never touches the real hosts.json. */
class JsonHostRepositoryTest {
    private static final Path FILE = Path.of(System.getProperty("user.home"), ".lemuri", "hosts.json");

    @Test
    void savesAndRereads() throws Exception {
        assertTrue(FILE.toString().contains("target"), "the tests must not use the real home: " + FILE);
        Host h = new Host();
        h.id = "h1";
        h.name = "web-01";
        h.address = "203.0.113.10";
        h.group = "Prod/EU";
        JsonHostRepository r = new JsonHostRepository();
        r.save(new HostRepository.Data(List.of("Vuota"), List.of(h)));
        HostRepository.Data d = r.load();
        assertEquals(List.of("Vuota"), d.folders());
        assertEquals("web-01", d.hosts().get(0).name);
        assertEquals("Prod/EU", d.hosts().get(0).group);
        assertEquals("rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(FILE)));
    }

    @Test
    void readsTheFormatOfEarlyVersions() throws Exception {
        Files.createDirectories(FILE.getParent());
        Files.writeString(FILE, "[{\"id\":\"h1\",\"nome\":\"old\",\"indirizzo\":\"192.0.2.1\"}]");
        HostRepository.Data d = new JsonHostRepository().load();
        assertEquals("old", d.hosts().get(0).name);
        assertTrue(d.folders().isEmpty());
        // Rewritten in today's format right away; reading it again changes nothing.
        String now = Files.readString(FILE);
        assertTrue(now.contains("\"name\": \"old\"") && !now.contains("nome"), now);
        assertEquals("old", new JsonHostRepository().load().hosts().get(0).name);
        assertEquals(now, Files.readString(FILE));
    }

    @Test
    void aCorruptFileIsNotLost() throws Exception {
        Files.createDirectories(FILE.getParent());
        // Copies left by an earlier run that failed would be found instead of the one made here.
        try (var old = Files.list(FILE.getParent())) {
            for (Path p : old.filter(p -> p.getFileName().toString().startsWith("hosts.json.corrupt-")).toList()) Files.delete(p);
        }
        Files.writeString(FILE, "{ this is not json");
        JsonHostRepository r = new JsonHostRepository();
        HostRepository.Data d = r.load();
        assertTrue(d.hosts().isEmpty());
        assertTrue(d.warning() != null && d.warning().contains("corrupt"), d.warning());
        // The corrupted content was set aside, not deleted.
        try (var beside = Files.list(FILE.getParent())) {
            Path part = beside.filter(p -> p.getFileName().toString().startsWith("hosts.json.corrupt-")).findFirst().orElseThrow();
            assertEquals("{ this is not json", Files.readString(part));
            Files.delete(part);
        }
    }
}
