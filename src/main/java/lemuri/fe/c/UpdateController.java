package lemuri.fe.c;

import java.io.IOException;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.prefs.Preferences;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleObjectProperty;
import lemuri.be.dto.Release;
import lemuri.be.service.UpdateService;
import lemuri.be.update.Versions;
import lemuri.be.util.Log;

/**
 * New versions: a check on GitHub once a day while the app is open (it can be turned off), one on request,
 * and the update of the copy on disk. The properties are changed on the JavaFX thread.
 */
public final class UpdateController {
    private static final long DAY = TimeUnit.HOURS.toMillis(24);
    private final Preferences pref = Preferences.userRoot().node("lemuri/updates");
    private final UpdateService service;
    private ScheduledExecutorService timer;

    /** A release newer than this one, found by the last check; null if none. */
    public final ObjectProperty<Release> available = new SimpleObjectProperty<>();
    /** The automatic check found it and the user did not skip it: the status bar shows it. */
    public final ObjectProperty<Release> offered = new SimpleObjectProperty<>();
    public final BooleanProperty checking = new SimpleBooleanProperty();
    /** Called on the JavaFX thread to end the app, after the restart has been prepared. */
    public Runnable quit = () -> {};

    public UpdateController(UpdateService service) {
        this.service = service;
    }

    public String current() {
        return service.current();
    }

    public boolean automatic() {
        return pref.getBoolean("automatic", true);
    }

    public void automatic(boolean on) {
        pref.putBoolean("automatic", on);
    }

    /** When the last check answered, in milliseconds; 0 if never. */
    public long lastCheck() {
        return pref.getLong("lastCheck", 0);
    }

    /** Starts the daily check: the first one a little after startup, so that it does not slow it down. */
    public void launch() {
        if (timer != null) return;
        // Found by an earlier check: shown again at once, without waiting for the next one.
        String tag = pref.get("releaseTag", "");
        if (tag.startsWith("v") && Versions.valid(tag.substring(1)) && Versions.compare(tag.substring(1), service.current()) > 0) {
            Release r = new Release(tag, tag.substring(1), pref.get("releaseNotes", ""), pref.get("releaseUrl", ""));
            available.set(r);
            if (automatic() && !r.version().equals(pref.get("skipped", ""))) offered.set(r);
        }
        timer = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "updates");
            t.setDaemon(true);
            return t;
        });
        timer.scheduleWithFixedDelay(() -> {
            if (!automatic() || System.currentTimeMillis() - lastCheck() < DAY) return;
            fetch().ifPresent(r -> Platform.runLater(() -> {
                if (!r.version().equals(pref.get("skipped", ""))) offered.set(r);
            }));
        }, 20, 3600, TimeUnit.SECONDS);
    }

    /** The check asked by the user: the newer release, or empty if this is the latest. */
    public CompletableFuture<Optional<Release>> checkNow() {
        checking.set(true);
        return CompletableFuture.supplyAsync(() -> {
            try {
                return fetchOrFail();
            } catch (IOException e) {
                throw new java.util.concurrent.CompletionException(e);
            }
        }).whenComplete((r, e) -> Platform.runLater(() -> checking.set(false)));
    }

    /** The automatic check: an error is only logged, the next try is in an hour. */
    private Optional<Release> fetch() {
        try {
            return fetchOrFail();
        } catch (IOException e) {
            Log.of(UpdateController.class).info("Update check failed: " + e.getMessage());
            return Optional.empty();
        }
    }

    private Optional<Release> fetchOrFail() throws IOException {
        Optional<Release> r = service.latest().filter(x -> Versions.compare(x.version(), service.current()) > 0);
        pref.putLong("lastCheck", System.currentTimeMillis());
        pref.put("releaseTag", r.map(Release::tag).orElse(""));
        pref.put("releaseUrl", r.map(Release::url).orElse(""));
        // A value in the preferences holds at most 8192 characters.
        String notes = r.map(Release::notes).orElse("");
        pref.put("releaseNotes", notes.length() > 6000 ? notes.substring(0, 6000) + "\n…" : notes);
        Platform.runLater(() -> {
            available.set(r.orElse(null));
            if (r.isEmpty()) offered.set(null);
        });
        return r;
    }

    /** Not offered again by the automatic check; a newer one will be. */
    public void skip(Release r) {
        pref.put("skipped", r.version());
        offered.set(null);
    }

    public void later() {
        offered.set(null);
    }

    public Optional<String> blocker() {
        return service.blocker();
    }

    /** Updates on a worker thread; the steps reach {@code progress} on the JavaFX thread. */
    public CompletableFuture<Void> install(Release r, Consumer<String> progress) {
        return CompletableFuture.runAsync(() -> {
            try {
                service.install(r, step -> Platform.runLater(() -> progress.accept(step)));
            } catch (IOException e) {
                throw new java.util.concurrent.CompletionException(e);
            }
        });
    }

    /** The new copy starts once this one has ended. */
    public void restart() throws IOException {
        service.restartAfterExit();
        quit.run();
    }

    public void stop() {
        if (timer != null) timer.shutdownNow();
    }
}
