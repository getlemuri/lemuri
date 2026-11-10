package lemuri.be.update;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import lemuri.be.dto.Release;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * The update on real git copies: an "upstream" repository plays GitHub, a clone on v0.1.0 plays the user's copy.
 * The Maven wrapper is a script that fails when the version has a file named "broken".
 */
class GitUpdaterTest {
    @TempDir
    Path dir;
    Path upstream, copy, key;
    String signers;

    @BeforeEach
    void setUp() throws Exception {
        assumeTrue(works("git", "--version") && works("ssh-keygen", "-?"), "git and ssh-keygen are needed");
        key = dir.resolve("release");
        sh(dir, "ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "test", "-f", key.toString());
        signers = "releases namespaces=\"git\" " + Files.readString(Path.of(key + ".pub")).strip() + "\n";

        upstream = Files.createDirectories(dir.resolve("upstream"));
        git(upstream, "init", "-q", "-b", "main");
        Files.writeString(upstream.resolve("pom.xml"), "<project/>");
        Files.writeString(upstream.resolve("mvnw"), "#!/bin/sh\n[ -f broken ] && { echo 'BUILD FAILURE'; exit 1; }\nexit 0\n");
        upstream.resolve("mvnw").toFile().setExecutable(true);
        Files.createDirectories(upstream.resolve("bin"));
        Files.writeString(upstream.resolve("bin/lemuri"), "#!/bin/sh\n");
        Files.writeString(upstream.resolve(".gitignore"), "target/\n");
        commit("0.1.0");
        git(upstream, "-c", "tag.gpgSign=false", "tag", "-a", "v0.1.0", "-m", "v0.1.0");
        commit("0.2.0");
        signedTag("v0.2.0", key);

        copy = dir.resolve("copy");
        git(dir, "clone", "-q", upstream.toString(), copy.toString());
        git(copy, "-c", "advice.detachedHead=false", "checkout", "-q", "v0.1.0");
    }

    private GitUpdater updater() {
        return new GitUpdater(copy, "0.1.0", upstream.toString(), signers);
    }

    private static Release release(String tag) {
        return new Release(tag, tag.substring(1), "", "");
    }

    @Test
    void updatesToASignedRelease() throws Exception {
        GitUpdater u = updater();
        assertEquals(Optional.empty(), u.blocker());
        List<String> steps = new ArrayList<>();
        u.install(release("v0.2.0"), steps::add);
        assertEquals("v0.2.0", head());
        assertEquals(4, steps.size());
        assertTrue(Files.exists(copy.resolve("target/.lemuri-built")));
    }

    @Test
    void refusesATagSignedByAnotherKey() throws Exception {
        Path other = dir.resolve("other");
        sh(dir, "ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "other", "-f", other.toString());
        commit("0.3.0");
        signedTag("v0.3.0", other);
        assertThrows(IOException.class, () -> updater().install(release("v0.3.0"), s -> {}));
        assertEquals("v0.1.0", head());
        // The refused tag does not stay in the copy.
        assertFalse(sh(copy, "git", "tag", "--list", "v0.3.0").contains("v0.3.0"));
    }

    @Test
    void refusesAnOlderSignedTagUnderANewName() throws Exception {
        // v0.3.0 here is the tag object of v0.2.0: the signature is good, the name inside is not.
        String object = sh(upstream, "git", "rev-parse", "v0.2.0").strip();
        git(upstream, "update-ref", "refs/tags/v0.3.0", object);
        assertThrows(IOException.class, () -> updater().install(release("v0.3.0"), s -> {}));
        assertEquals("v0.1.0", head());
    }

    @Test
    void goesBackWhenTheBuildFails() throws Exception {
        Files.writeString(upstream.resolve("broken"), "");
        commit("0.4.0");
        signedTag("v0.4.0", key);
        List<String> steps = new ArrayList<>();
        IOException e = assertThrows(IOException.class, () -> updater().install(release("v0.4.0"), steps::add));
        assertTrue(e.getMessage().contains("BUILD FAILURE"), e.getMessage());
        assertEquals("v0.1.0", head());
        assertEquals(5, steps.size());
    }

    @Test
    void neverOlderOrSameVersions() {
        assertThrows(IOException.class, () -> updater().install(release("v0.1.0"), s -> {}));
        assertThrows(IOException.class, () -> updater().install(release("v0.0.9"), s -> {}));
    }

    @Test
    void leavesChangedCopiesAlone() throws Exception {
        Files.writeString(copy.resolve("pom.xml"), "<project><!-- mine --></project>");
        assertTrue(updater().blocker().isPresent());
        assertThrows(IOException.class, () -> updater().install(release("v0.2.0"), s -> {}));
        assertEquals("<project><!-- mine --></project>", Files.readString(copy.resolve("pom.xml")));
    }

    @Test
    void aCloneOfMainMovesForwardAndStaysOnMain() throws Exception {
        commit("0.3.0");
        signedTag("v0.3.0", key);
        git(copy, "checkout", "-q", "main");
        GitUpdater u = updater();
        assertEquals(Optional.empty(), u.blocker());
        u.install(release("v0.3.0"), s -> {});
        assertEquals("v0.3.0", head());
        assertEquals("main", sh(copy, "git", "symbolic-ref", "--short", "HEAD").strip());
    }

    @Test
    void aFailedBuildOnMainPutsTheBranchBack() throws Exception {
        String before = sh(copy, "git", "rev-parse", "main").strip();
        Files.writeString(upstream.resolve("broken"), "");
        commit("0.3.0");
        signedTag("v0.3.0", key);
        git(copy, "checkout", "-q", "main");
        assertThrows(IOException.class, () -> updater().install(release("v0.3.0"), s -> {}));
        assertEquals(before, sh(copy, "git", "rev-parse", "HEAD").strip());
        assertEquals("main", sh(copy, "git", "symbolic-ref", "--short", "HEAD").strip());
    }

    @Test
    void leavesDevelopmentCopiesAlone() throws Exception {
        git(copy, "checkout", "-q", "main");
        Files.writeString(copy.resolve("README"), "work in progress");
        git(copy, "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgSign=false", "commit", "-q", "-am", "mine");
        String mine = sh(copy, "git", "rev-parse", "HEAD").strip();
        commit("0.3.0");
        signedTag("v0.3.0", key);
        assertThrows(IOException.class, () -> updater().install(release("v0.3.0"), s -> {}));
        assertEquals(mine, sh(copy, "git", "rev-parse", "HEAD").strip());
    }

    @Test
    void needsACopyOfTheSources() {
        assertTrue(new GitUpdater(null, "0.1.0", upstream.toString(), signers).blocker().isPresent());
        assertTrue(new GitUpdater(dir, "0.1.0", upstream.toString(), signers).blocker().isPresent());
    }

    // ── Helpers ──

    private String head() throws Exception {
        return sh(copy, "git", "describe", "--tags", "--exact-match", "HEAD").strip();
    }

    private void commit(String message) throws Exception {
        Files.writeString(upstream.resolve("README"), message);
        git(upstream, "add", "-A");
        git(upstream, "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgSign=false", "commit", "-q", "-m", message);
    }

    private void signedTag(String tag, Path signingKey) throws Exception {
        git(upstream, "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "gpg.format=ssh",
                "-c", "user.signingKey=" + signingKey + ".pub", "tag", "-s", tag, "-m", tag);
    }

    private static void git(Path where, String... args) throws Exception {
        List<String> c = new ArrayList<>(List.of("git"));
        c.addAll(List.of(args));
        sh(where, c.toArray(String[]::new));
    }

    private static String sh(Path where, String... command) throws Exception {
        Process p = new ProcessBuilder(command).directory(where.toFile()).redirectErrorStream(true).start();
        String out = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        if (p.waitFor() != 0) throw new IOException(String.join(" ", command) + ": " + out);
        return out;
    }

    private static boolean works(String... command) {
        try {
            new ProcessBuilder(command).redirectErrorStream(true).start().waitFor();
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
