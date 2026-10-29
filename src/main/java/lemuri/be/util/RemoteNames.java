package lemuri.be.util;

/** Paths on a server, as JSch must receive them. */
public final class RemoteNames {
    private RemoteNames() {}

    /**
     * A server path for JSch: get, put, rm, rename, ls, stat and lstat read "*" and "?" as wildcards
     * and drop every "\\". Without this a file "report?.txt" would also match "report1.txt".
     * Not for mkdir and realpath, which take the path as it is.
     */
    public static String literal(String path) {
        StringBuilder b = new StringBuilder(path.length() + 8);
        for (char c : path.toCharArray()) {
            if (c == '\\' || c == '*' || c == '?') b.append('\\');
            b.append(c);
        }
        return b.toString();
    }
}
