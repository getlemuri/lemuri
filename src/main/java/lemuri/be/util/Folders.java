package lemuri.be.util;

/**
 * Paths of the folders that organise the hosts: segments separated by "/",
 * like "Production/EU/web". The empty string is the root (the datacenter).
 */
public final class Folders {
    private Folders() {}

    /** Removes extra spaces and slashes: " /Prod//EU/ " becomes "Prod/EU". */
    public static String normalize(String p) {
        if (p == null) return "";
        StringBuilder out = new StringBuilder();
        for (String s : p.split("/")) {
            String t = s.trim();
            if (t.isEmpty()) continue;
            if (!out.isEmpty()) out.append('/');
            out.append(t);
        }
        return out.toString();
    }

    public static String parent(String p) {
        int i = p.lastIndexOf('/');
        return i < 0 ? "" : p.substring(0, i);
    }

    public static String name(String p) {
        return p.substring(p.lastIndexOf('/') + 1);
    }

    public static String merge(String parent, String name) {
        return parent.isEmpty() ? name : parent + "/" + name;
    }

    public static int depth(String p) {
        return p.isEmpty() ? 0 : p.split("/").length;
    }

    /** True if {@code p} is {@code folder} or lies inside it, at any depth. */
    public static boolean inside(String p, String folder) {
        return folder.isEmpty() || p.equals(folder) || p.startsWith(folder + "/");
    }

    /** Moves {@code p} from under {@code old} to under {@code created}; other paths stay as they were. */
    public static String rename(String p, String old, String created) {
        if (p.equals(old)) return created;
        if (p.startsWith(old + "/")) return merge(created, p.substring(old.length() + 1));
        return p;
    }

    public static String label(String p) {
        return p.isEmpty() ? "Datacenter" : p;
    }
}
