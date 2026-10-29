package lemuri.be.util;

import java.util.Locale;

/** The operating system the app runs on. */
public final class Os {
    private Os() {}

    /** On Mac the way to open the browser and the window frame change, and there is no tray icon. */
    public static final boolean MAC = System.getProperty("os.name", "").toLowerCase(Locale.ROOT).contains("mac");
}
