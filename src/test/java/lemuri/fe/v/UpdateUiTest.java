package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import javafx.application.Platform;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Release;
import lemuri.be.service.UpdateService;
import lemuri.be.util.I18n;
import lemuri.fe.c.UpdateController;

/** The update from its dialogs, with an update service that installs at once. */
class UpdateUiTest extends UiTest {
    @Test
    void restartNowClosesTheDialogAndRestarts() {
        AtomicBoolean prepared = new AtomicBoolean(), quit = new AtomicBoolean();
        UpdateController c = new UpdateController(new UpdateService() {
            @Override public String current() { return "0.1.0"; }
            @Override public Optional<Release> latest() { return Optional.empty(); }
            @Override public Optional<String> blocker() { return Optional.empty(); }
            @Override public void install(Release r, Consumer<String> progress) { progress.accept("step"); }
            @Override public void restartAfterExit() { prepared.set(true); }
        });
        c.quit = () -> quit.set(true);
        Release r = new Release("v0.1.1", "0.1.1", "- notes", "");
        // The dialogs wait for an answer: opened without waiting for them.
        Platform.runLater(() -> UpdateDialogs.offer(c, r));

        press(I18n.t("update.install"));
        press(I18n.t("update.restart"));

        until(quit::get, "the app to quit");
        assertTrue(prepared.get());
        assertFalse(dialogOpen());
    }

    @Test
    void theTopBarFlagsANewerVersion() {
        assertTrue(all(".brand-update").stream().noneMatch(javafx.scene.Node::isVisible));
        on(() -> app.updates.available.set(new Release("v9.9.9", "9.9.9", "- notes", "")));
        settle();
        assertTrue(all(".brand-update").stream().anyMatch(javafx.scene.Node::isVisible));
        on(() -> app.updates.available.set(null));
        settle();
        assertTrue(all(".brand-update").stream().noneMatch(javafx.scene.Node::isVisible));
    }
}
