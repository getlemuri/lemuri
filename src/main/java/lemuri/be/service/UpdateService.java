package lemuri.be.service;

import java.io.IOException;
import java.util.Optional;
import java.util.function.Consumer;
import lemuri.be.dto.Release;

/** New versions of the app: the check on GitHub and the update of the copy on disk. */
public interface UpdateService {
    /** The version running now, as in pom.xml (0.1.0). */
    String current();

    /** The latest release, or empty if none was published yet. Network: one HTTPS request. */
    Optional<Release> latest() throws IOException;

    /** Why this copy cannot update itself (in the user's language), or empty if it can. */
    Optional<String> blocker();

    /**
     * Downloads {@code r}, checks its signature, moves the copy on disk to it and builds it. Each step is
     * reported to {@code progress}. If anything fails the copy goes back to the version it was on, and the
     * exception says what went wrong.
     */
    void install(Release r, Consumer<String> progress) throws IOException;

    /** Starts the app again from the copy on disk once this process has ended. */
    void restartAfterExit() throws IOException;

    /** For the interface tests: never any network, never an update. */
    UpdateService NONE = new UpdateService() {
        @Override public String current() { return "0.0.0"; }
        @Override public Optional<Release> latest() { return Optional.empty(); }
        @Override public Optional<String> blocker() { return Optional.of("tests"); }
        @Override public void install(Release r, Consumer<String> progress) throws IOException { throw new IOException("tests"); }
        @Override public void restartAfterExit() {}
    };
}
