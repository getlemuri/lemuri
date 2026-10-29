package lemuri;

import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import java.util.Properties;
import java.util.prefs.Preferences;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.scene.Scene;
import javafx.stage.Stage;
import lemuri.be.monitoring.Metrics;
import lemuri.be.monitoring.Probe;
import lemuri.be.sharing.Sharing;
import lemuri.be.storage.FileVault;
import lemuri.be.storage.JsonHostRepository;
import lemuri.be.util.I18n;
import lemuri.be.util.LegacyNames;
import lemuri.be.util.Log;
import lemuri.be.util.PrivateFiles;
import lemuri.be.util.SingleInstance;
import lemuri.fe.c.AppContext;
import lemuri.fe.v.AppIcon;
import lemuri.fe.v.Console;
import lemuri.fe.v.Dialogs;
import lemuri.fe.v.GhostMode;
import lemuri.fe.v.SessionViews;
import lemuri.fe.v.SharingDialogs;
import lemuri.fe.v.Splash;
import lemuri.fe.v.Tray;
import lemuri.fe.v.WindowFrame;

/**
 * App startup and the only place where backend and frontend meet: the services
 * ({@code be}) are created here and handed to the controllers ({@code fe}).
 */
public class App extends Application {
    /** The version for the user, from pom.xml: "v0.1" for 0.1.0, "v0.1.2" for 0.1.2. */
    public static final String VERSION = version();

    private static String version() {
        Properties p = new Properties();
        try (InputStream in = App.class.getResourceAsStream("/lemuri/version.properties")) {
            if (in != null) p.load(in);
        } catch (IOException e) {
            // Without the file the version is unknown, nothing else changes.
        }
        String v = p.getProperty("version", "?");
        return "v" + (v.matches("\\d+\\.\\d+\\.0") ? v.substring(0, v.length() - 2) : v);
    }
    // The language: the one chosen in the menu, or the system one if there is a translation, otherwise English.
    // JavaFX standard buttons (Cancel, OK…) follow the Locale too.
    static {
        // Preferences and files saved by the first versions under Italian names move to the English ones first.
        LegacyNames.migrate();
        String choice = Preferences.userRoot().node("lemuri/ui").get("language", "auto");
        Locale l = choice.equals("auto") ? Locale.getDefault() : Locale.forLanguageTag(choice);
        if (!I18n.LANGUAGES.contains(l.getLanguage())) l = Locale.ENGLISH;
        Locale.setDefault(l);
        I18n.language(l);
    }

    /** The single-copy lock (see {@link lemuri.be.util.SingleInstance}); the window to show if the app is started again. */
    private SingleInstance instance;
    private volatile Stage shown;
    /** The main window's ghost mode, once the window exists: F11 from anywhere shows or hides it. */
    private volatile GhostMode ghost;

    @Override
    public void start(Stage stage) {
        // Already open (maybe hidden in the tray): show that one, and this copy quits without opening anything.
        // With --toggle (the F11 system shortcut) the running copy shows or hides the terminal instead.
        String command = getParameters().getRaw().contains("--toggle") ? SingleInstance.TOGGLE
                : SingleInstance.SHOW;
        instance = SingleInstance.acquire(PrivateFiles.DIR, command, c -> Platform.runLater(() -> {
            if (shown == null) return;
            if (c.equals(SingleInstance.TOGGLE) && ghost != null) ghost.toggle(shown);
            else Tray.bringToFront(shown);
        }));
        if (instance == null) {
            Platform.exit();
            return;
        }
        // The log is opened only now: a second copy, which quits right away, must not create another one next to it.
        Log.configure();
        // The splash screen starts at once; the main window is prepared behind it and opens when it ends.
        if (!Preferences.userRoot().node("lemuri/ui").getBoolean("splash", true)) {
            Runnable open = prepare(stage);
            SessionViews.warmUp();
            open.run();
            return;
        }
        // Until the main window is open, closing the splash must not close the app.
        Platform.setImplicitExit(false);
        Runnable[] open = {null};
        boolean[] finished = {false};
        Runnable openWhenReady = () -> {
            if (open[0] == null || !finished[0]) return;
            // The splash still shows its last frame: the pause is not seen there.
            SessionViews.warmUp();
            open[0].run();
            Platform.setImplicitExit(true);
        };
        Splash.show(() -> {
            finished[0] = true;
            openWhenReady.run();
        });
        Platform.runLater(() -> {
            open[0] = prepare(stage);
            openWhenReady.run();
        });
    }

    /** Builds the main window and returns what shows it and starts everything else. */
    private Runnable prepare(Stage stage) {
        // The app data (hosts, vault, settings, logs) is readable by the user only.
        try {
            PrivateFiles.folder(PrivateFiles.DIR);
        } catch (IOException e) {
            Log.of(App.class).warning("Cannot protect " + PrivateFiles.DIR + ": " + e.getMessage());
        }
        Log.of(App.class).info("Lemuri starting, Java " + System.getProperty("java.version") + ", "
                + System.getProperty("os.name") + " " + System.getProperty("os.version"));
        // Backend: the service implementations. The frontend sees them only through the interfaces.
        FileVault vault = new FileVault();
        Metrics metrics = new Metrics(vault);
        AppContext app = new AppContext(new JsonHostRepository(), vault, metrics,
                new Probe(), new Sharing(vault), h -> Dialogs.sshRequests(h, vault));
        SessionViews.attach(app);
        Console console = new Console(app);
        ghost = console.ghost();
        Runnable close = () -> {
            app.stop();
            Tray.remove();
        };
        Runnable quit = () -> {
            close.run();
            stage.close();
            Platform.exit();
        };
        // With the tray icon, the X only hides the window: sessions stay open and you quit from the icon menu.
        Runnable closeWindow = () -> {
            if (Tray.present()) Tray.hide(stage);
            else quit.run();
        };
        Scene scene = WindowFrame.mount(stage, console, closeWindow, 1400, 860);
        scene.getStylesheets().add(App.class.getResource("style.css").toExternalForm());
        stage.setTitle("Lemuri");
        stage.getIcons().setAll(AppIcon.images());
        stage.setScene(scene);
        stage.setOnCloseRequest(e -> {
            e.consume();
            closeWindow.run();
        });
        return () -> {
            stage.show();
            shown = stage;
            Dialogs.main = stage;
            Tray.install(stage, console.ghost(), quit);
            if (app.hosts.loadingNotice != null) Dialogs.warning(I18n.t("hosts.loadWarning.title"), app.hosts.loadingNotice);
            app.monitor.launch();
            // A host sent by a contact over the local network is offered at once: accept, reject or decide later.
            app.sharing.onArrival = i -> SharingDialogs.propose(app, i);
            app.sharing.lan.onRequest = r -> SharingDialogs.pairingRequest(app.sharing.lan, r);
            app.sharing.restart();
        };
    }

    @Override
    public void stop() {
        Tray.remove();
        if (instance != null) instance.release();
    }

    public static void main(String[] args) {
        // lemuri -s HOST, -l: the command line, without opening the window (see Cli).
        if (Cli.handles(args)) Cli.main(args);
        launch(args);
    }
}
