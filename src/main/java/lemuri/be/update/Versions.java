package lemuri.be.update;

import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/** Version numbers like 0.1.0 or 1.12.3: the one of this build and their order. */
public final class Versions {
    private Versions() {}

    /** The version in pom.xml, put in version.properties by Maven; "0.0.0" if it cannot be read. */
    public static String current() {
        Properties p = new Properties();
        try (InputStream in = Versions.class.getResourceAsStream("/lemuri/version.properties")) {
            if (in != null) p.load(in);
        } catch (IOException e) {
            // Unknown version: every release looks newer, and the update itself checks the copy on disk.
        }
        String v = p.getProperty("version", "");
        return valid(v) ? v : "0.0.0";
    }

    /** Only plain numbers: 0.2.0 yes, 0.2.0-SNAPSHOT or 0.2.0-rc1 no (they are not offered as updates). */
    public static boolean valid(String v) {
        return v != null && v.matches("\\d{1,6}(\\.\\d{1,6}){0,3}");
    }

    /** Negative if {@code a} comes before {@code b}; 0.2 and 0.2.0 are the same version. */
    public static int compare(String a, String b) {
        String[] x = a.split("\\."), y = b.split("\\.");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int c = Integer.compare(i < x.length ? Integer.parseInt(x[i]) : 0, i < y.length ? Integer.parseInt(y[i]) : 0);
            if (c != 0) return c;
        }
        return 0;
    }
}
