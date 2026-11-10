package lemuri.be.update;

import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.logging.Logger;
import lemuri.be.dto.Release;
import lemuri.be.service.UpdateService;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * Updates for a copy cloned with git and built with the Maven wrapper, the only way Lemuri is installed today.
 * The check asks the GitHub API for the latest release. The update fetches that tag from GitHub, accepts it only
 * if it is signed by a key in {@code release-signers} (shipped inside the app, so a tag pushed by someone else, or
 * an older signed tag renamed, is refused), moves the copy to it and builds it; on any failure the copy goes back to
 * the commit it was on. It touches only copies with no changes of their own that the release contains (a clone of
 * main, or an earlier release): a development copy, with commits the release does not have, is never moved.
 */
public final class GitUpdater implements UpdateService {
    private static final Logger LOG = Log.of(GitUpdater.class);
    static final String REPO = "getlemuri/lemuri";
    private static final URI LATEST = URI.create("https://api.github.com/repos/" + REPO + "/releases/latest");
    private static final String CLONE = "https://github.com/" + REPO + ".git";
    /** The release notes are a section of CHANGELOG.md: far below this. */
    private static final int MAX_ANSWER = 1 << 20;

    /** The folder with pom.xml, mvnw and bin/lemuri; null if the app does not run from such a folder. */
    private final Path app;
    private final String current;
    /** Where releases are fetched from, and the allowed signers file the tags are checked against. */
    private final String clone, signers;
    private final HttpClient http = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(10))
            .followRedirects(HttpClient.Redirect.NORMAL).build();

    GitUpdater(Path app, String current, String clone, String signers) {
        this.app = app;
        this.current = current;
        this.clone = clone;
        this.signers = signers;
    }

    /** The copy this app runs from: target/classes is two folders below it, with mvn javafx:run and bin/lemuri alike. */
    public static GitUpdater here() {
        Path app = null;
        try {
            Path classes = Path.of(GitUpdater.class.getProtectionDomain().getCodeSource().getLocation().toURI());
            Path name = classes.getFileName(), target = classes.getParent();
            Path candidate = target == null ? null : target.getParent();
            if (candidate != null && name != null && name.toString().equals("classes") && Files.isDirectory(classes)) app = candidate;
        } catch (URISyntaxException | RuntimeException e) {
            LOG.fine("Where the app runs from is not known: " + e);
        }
        String signers = "";
        try (InputStream in = GitUpdater.class.getResourceAsStream("/lemuri/release-signers")) {
            if (in != null) signers = new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            LOG.warning("Cannot read release-signers: " + e.getMessage());
        }
        return new GitUpdater(app, Versions.current(), CLONE, signers);
    }

    @Override
    public String current() {
        return current;
    }

    // ── The check ────────────────────────────────────────────────────────────

    @Override
    public Optional<Release> latest() throws IOException {
        HttpRequest q = HttpRequest.newBuilder(LATEST).timeout(Duration.ofSeconds(20))
                .header("Accept", "application/vnd.github+json")
                .header("X-GitHub-Api-Version", "2022-11-28")
                .header("User-Agent", "Lemuri/" + current).GET().build();
        HttpResponse<InputStream> a;
        try {
            a = http.send(q, HttpResponse.BodyHandlers.ofInputStream());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException(e);
        }
        try (InputStream in = a.body()) {
            // No release published yet.
            if (a.statusCode() == 404) return Optional.empty();
            if (a.statusCode() == 403 || a.statusCode() == 429) throw new IOException(I18n.t("update.error.busy"));
            if (a.statusCode() != 200) throw new IOException("GitHub: HTTP " + a.statusCode());
            return Optional.of(release(new String(in.readNBytes(MAX_ANSWER), StandardCharsets.UTF_8)));
        }
    }

    /** The answer of the API: only plain versions, only pages of this repository. */
    static Release release(String json) throws IOException {
        JsonObject o;
        try {
            o = JsonParser.parseString(json).getAsJsonObject();
        } catch (RuntimeException e) {
            throw new IOException("GitHub: unexpected answer");
        }
        String tag = text(o.get("tag_name"));
        String url = text(o.get("html_url"));
        if (!tag.startsWith("v") || !Versions.valid(tag.substring(1))) throw new IOException("GitHub: unexpected tag " + tag);
        if (!url.startsWith("https://github.com/" + REPO + "/")) url = "https://github.com/" + REPO + "/releases/tag/" + tag;
        return new Release(tag, tag.substring(1), text(o.get("body")), url);
    }

    private static String text(JsonElement e) {
        return e == null || !e.isJsonPrimitive() ? "" : e.getAsString();
    }

    // ── Can this copy update itself? ─────────────────────────────────────────

    @Override
    public Optional<String> blocker() {
        if (app == null || !Files.isRegularFile(app.resolve("pom.xml")) || !Files.isRegularFile(app.resolve("mvnw"))
                || !Files.isRegularFile(app.resolve("bin/lemuri"))) return Optional.of(I18n.t("update.blocker.sources"));
        if (!Files.exists(app.resolve(".git"))) return Optional.of(I18n.t("update.blocker.git"));
        if (!works("git", "--version")) return Optional.of(I18n.t("update.blocker.noGit"));
        // git checks SSH signatures with ssh-keygen.
        if (!works("ssh-keygen", "-?")) return Optional.of(I18n.t("update.blocker.noSshKeygen"));
        try {
            Run status = git(30, "status", "--porcelain", "--untracked-files=no");
            if (status.code() != 0) return Optional.of(I18n.t("update.blocker.git"));
            if (!status.out().isBlank()) return Optional.of(I18n.t("update.blocker.changes"));
        } catch (IOException e) {
            return Optional.of(e.getMessage());
        }
        return Optional.empty();
    }

    /** Whether the program exists: ssh-keygen -? ends with 1, but it ran. */
    private static boolean works(String... command) {
        try {
            Process p = new ProcessBuilder(command).redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
            if (!p.waitFor(10, TimeUnit.SECONDS)) p.destroyForcibly();
            return true;
        } catch (IOException e) {
            return false;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    // ── The update ───────────────────────────────────────────────────────────

    @Override
    public void install(Release r, Consumer<String> progress) throws IOException {
        Optional<String> no = blocker();
        if (no.isPresent()) throw new IOException(no.get());
        String tag = r.tag();
        if (!tag.equals("v" + r.version()) || !Versions.valid(r.version()) || Versions.compare(r.version(), current) <= 0)
            throw new IOException(I18n.t("update.error.notNewer", tag));
        String before = "v" + current;
        Run head = git(30, "rev-parse", "--verify", "HEAD");
        if (head.code() != 0) throw failure(I18n.t("update.blocker.git"), head);
        String start = head.out().strip();
        // On a branch (a clone of main) the branch moves forward and stays; on a tag the copy moves to the new tag.
        Run onBranch = git(30, "symbolic-ref", "--quiet", "--short", "HEAD");
        boolean branch = onBranch.code() == 0;

        progress.accept(I18n.t("update.step.download", tag));
        // Always from GitHub over HTTPS, whatever origin is: no SSH key needed, and the signature decides anyway.
        Run fetch = git(600, "fetch", "--no-tags", "--force", clone, "+refs/tags/" + tag + ":refs/tags/" + tag);
        if (fetch.code() != 0) throw failure(I18n.t("update.error.download"), fetch);

        progress.accept(I18n.t("update.step.verify"));
        try {
            verify(tag);
        } catch (IOException e) {
            // A tag that is not ours does not stay in the copy.
            git(30, "tag", "-d", tag);
            throw e;
        }
        // Only forward: a copy with commits of its own (a development copy) is never moved.
        if (git(30, "merge-base", "--is-ancestor", start, tag).code() != 0) throw new IOException(I18n.t("update.blocker.notRelease"));

        progress.accept(I18n.t("update.step.switch", tag));
        Run move = branch ? git(120, "merge", "--ff-only", "--quiet", tag)
                : git(120, "-c", "advice.detachedHead=false", "checkout", "--quiet", tag);
        if (move.code() != 0) {
            back(start, branch);
            throw failure(I18n.t("update.error.switch"), move);
        }

        progress.accept(I18n.t("update.step.build"));
        Run build = build();
        if (build.code() != 0) {
            progress.accept(I18n.t("update.step.back", before));
            back(start, branch);
            build();
            throw failure(I18n.t("update.error.build"), build);
        }
        LOG.info("Updated from " + before + " to " + tag);
    }

    /** The tag must be signed by a release key, and must really be the tag with that name, not an older one renamed. */
    void verify(String tag) throws IOException {
        if (signers.lines().noneMatch(l -> !l.isBlank() && !l.startsWith("#"))) throw new IOException(I18n.t("update.error.signature", tag));
        Path file = Files.createTempFile("lemuri-signers", ".txt");
        try {
            Files.writeString(file, signers);
            Run v = git(60, "-c", "gpg.format=ssh", "-c", "gpg.ssh.allowedSignersFile=" + file, "verify-tag", tag);
            if (v.code() != 0) throw failure(I18n.t("update.error.signature", tag), v);
            Run object = git(30, "cat-file", "tag", tag);
            if (object.code() != 0 || !object.out().lines().anyMatch(("tag " + tag)::equals))
                throw failure(I18n.t("update.error.signature", tag), object);
        } finally {
            Files.deleteIfExists(file);
        }
    }

    /** Back to the commit the copy was on: it had no changes of its own, so nothing is lost. */
    private void back(String commit, boolean branch) {
        try {
            Run r = branch ? git(120, "reset", "--hard", "--quiet", commit)
                    : git(120, "-c", "advice.detachedHead=false", "checkout", "--quiet", "--force", commit);
            if (r.code() != 0) LOG.warning("Could not go back to " + commit + ":\n" + r.out());
        } catch (IOException e) {
            LOG.warning("Could not go back to " + commit + ": " + e.getMessage());
        }
    }

    /**
     * What bin/lemuri does at startup, done now so that the restart is quick: the classes, the classpath it
     * caches (dependencies are downloaded here, it reads them offline) and its mark of the last build.
     */
    private Run build() throws IOException {
        Path cache = Path.of(System.getProperty("user.home"), ".cache", "lemuri");
        Files.createDirectories(cache);
        Run b = run(app, 1800, List.of(app.resolve("mvnw").toString(), "-q", "-B", "process-classes",
                "dependency:build-classpath", "-Dmdep.outputFile=" + cache.resolve("classpath")));
        if (b.code() == 0) Files.writeString(Files.createDirectories(app.resolve("target")).resolve(".lemuri-built"), "");
        return b;
    }

    @Override
    public void restartAfterExit() throws IOException {
        if (app == null) throw new IOException(I18n.t("update.blocker.sources"));
        // A small shell waits for this process to end (the single-copy lock goes with it), then starts the launcher,
        // which writes to ~/.cache/lemuri/launcher.log when it has no terminal.
        new ProcessBuilder("/bin/sh", "-c", "while kill -0 \"$1\" 2>/dev/null; do sleep 0.3; done; exec \"$2\"",
                "lemuri-restart", String.valueOf(ProcessHandle.current().pid()), app.resolve("bin/lemuri").toString())
                .redirectInput(ProcessBuilder.Redirect.from(new java.io.File("/dev/null")))
                .redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectErrorStream(true).start();
    }

    // ── Processes ────────────────────────────────────────────────────────────

    /** Exit code and the end of the output (the useful part of a Maven error). */
    record Run(int code, String out) {}

    private Run git(int seconds, String... args) throws IOException {
        List<String> c = new ArrayList<>(List.of("git", "-C", app.toString()));
        c.addAll(List.of(args));
        return run(app, seconds, c);
    }

    private static Run run(Path dir, int seconds, List<String> command) throws IOException {
        ProcessBuilder b = new ProcessBuilder(command).directory(dir.toFile()).redirectErrorStream(true);
        Map<String, String> env = b.environment();
        // Never a question for a password or a passphrase: there is no terminal to answer it.
        env.put("GIT_TERMINAL_PROMPT", "0");
        env.put("GIT_ASKPASS", "true");
        env.put("LC_ALL", "C");
        Process p = b.redirectInput(ProcessBuilder.Redirect.from(new java.io.File("/dev/null"))).start();
        Deque<String> tail = new ArrayDeque<>();
        Thread reader = Thread.ofVirtual().start(() -> {
            try (InputStream in = p.getInputStream()) {
                ByteArrayOutputStream line = new ByteArrayOutputStream();
                int ch;
                while ((ch = in.read()) != -1) {
                    if (ch != '\n') {
                        if (line.size() < 4096) line.write(ch);
                        continue;
                    }
                    synchronized (tail) {
                        tail.add(line.toString(StandardCharsets.UTF_8));
                        if (tail.size() > 40) tail.removeFirst();
                    }
                    line.reset();
                }
                if (line.size() > 0) synchronized (tail) { tail.add(line.toString(StandardCharsets.UTF_8)); }
            } catch (IOException e) {
                // The process ended: what was read is enough.
            }
        });
        try {
            if (!p.waitFor(seconds, TimeUnit.SECONDS)) {
                p.destroyForcibly();
                throw new IOException(I18n.t("update.error.timeout", String.join(" ", command.subList(0, Math.min(3, command.size())))));
            }
            reader.join(5000);
        } catch (InterruptedException e) {
            p.destroyForcibly();
            Thread.currentThread().interrupt();
            throw new IOException(e);
        }
        synchronized (tail) {
            return new Run(p.exitValue(), String.join("\n", tail));
        }
    }

    private static IOException failure(String what, Run r) {
        LOG.warning(what + " (exit " + r.code() + ")\n" + r.out());
        String out = r.out().strip();
        return new IOException(out.isEmpty() ? what : what + "\n\n" + out);
    }
}
