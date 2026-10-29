package lemuri.fe.v;

import java.util.Map;
import javafx.scene.Group;
import javafx.scene.Node;
import javafx.scene.shape.SVGPath;

/** Stroke icons on a 24×24 grid, in the lucide style (the one of the web version). */
public final class Icons {
    private Icons() {}

    private static final Map<String, String> TRACES = Map.ofEntries(
            Map.entry("datacenter", "M4 21V5a1 1 0 0 1 1-1h8a1 1 0 0 1 1 1v16M14 9h5a1 1 0 0 1 1 1v11M2 21h20M8 8h2M8 12h2M8 16h2M17 13h0M17 17h0"),
            Map.entry("server", "M4 4h16v7H4zM4 13h16v7H4zM8 7.5h.01M8 16.5h.01"),
            Map.entry("folder", "M3 6a1 1 0 0 1 1-1h5l2 2h9a1 1 0 0 1 1 1v10a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1z"),
            Map.entry("folder-open", "M3 18V6a1 1 0 0 1 1-1h5l2 2h8a1 1 0 0 1 1 1v2M3 18l2.4-7.2a1 1 0 0 1 1-.8H21a1 1 0 0 1 1 1.3L19.7 18.3a1 1 0 0 1-1 .7H4a1 1 0 0 1-1-1z"),
            Map.entry("gear", "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"),
            // The screen with a thick band on one side: where the ghost sticks.
            Map.entry("dock-left", "M3 4h18v16H3zM6 4v16M9 4v16"),
            Map.entry("dock-top", "M3 4h18v16H3zM3 7h18M3 10h18"),
            Map.entry("dock-bottom", "M3 4h18v16H3zM3 14h18M3 17h18"),
            Map.entry("dock-right", "M3 4h18v16H3zM15 4v16M18 4v16"),
            Map.entry("split-right", "M3 4h18v16H3zM12 4v16"),
            // A plug in and a plug pulled out: PCs connected on the local network, or not.
            Map.entry("plug", "M9 2v5M15 2v5M6 7h12v4a6 6 0 0 1-12 0zM12 17v5"),
            Map.entry("plug-off", "M9 2v5M15 2v5M18 7v4a6 6 0 0 1-1.3 3.7M14.6 16.5A6 6 0 0 1 6 11V7h6M12 17v5M3 3l18 18"),
            Map.entry("copy", "M9 9h11v11H9zM5 15H4V4h11v1"),
            Map.entry("paste", "M8 4H6a1 1 0 0 0-1 1v15a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V5a1 1 0 0 0-1-1h-2M9 3h6v3H9z"),
            Map.entry("split-down", "M3 4h18v16H3zM3 12h18"),
            Map.entry("upload", "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M17 8l-5-5-5 5M12 3v12"),
            Map.entry("download", "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4M7 10l5 5 5-5M12 15V3"),
            Map.entry("eye", "M1 12s4-8 11-8 11 8 11 8-4 8-11 8S1 12 1 12zM12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"),
            // A panel with its left side and an arrow towards it: the side column closes.
            Map.entry("sidebar-close", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM9 3v18M16 15l-3-3 3-3"),
            Map.entry("chevrons-left", "M11 17l-5-5 5-5M18 17l-5-5 5-5"),
            Map.entry("chevrons-right", "M13 17l5-5-5-5M6 17l5-5-5-5"),
            Map.entry("collapse", "M7 20l5-5 5 5M7 4l5 5 5-5"),
            Map.entry("expand", "M7 15l5 5 5-5M7 9l5-5 5 5"),
            Map.entry("terminal", "M4 5h16v14H4zM7 9l3 3-3 3M12 15h5"),
            Map.entry("plus", "M12 5v14M5 12h14"),
            Map.entry("power", "M12 3v8M6.3 6.3a8 8 0 1 0 11.4 0"),
            Map.entry("edit", "M4 20h4L19 9l-4-4L4 16zM14 6l4 4"),
            Map.entry("trash", "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"),
            Map.entry("summary", "M12 14l4-4M3.3 19a10 10 0 1 1 17.4 0"),
            Map.entry("list", "M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01"),
            Map.entry("note", "M6 3h9l4 4v14H6zM9 12h7M9 16h7M9 8h3"),
            Map.entry("grid", "M3 3h7v7H3zM14 3h7v7h-7zM14 14h7v7h-7zM3 14h7v7H3z"),
            Map.entry("close", "M6 6l12 12M18 6 6 18"),
            Map.entry("search", "M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM20 20l-4-4"),
            Map.entry("lock", "M6 11h12v10H6zM8 11V7a4 4 0 0 1 8 0v4"),
            Map.entry("lock-open", "M6 11h12v10H6zM8 11V7a4 4 0 0 1 7.6-1.7"),
            // Three connected dots: the share icon of phones (WhatsApp, Android).
            Map.entry("share", "M18 2a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM6 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM18 16a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM8.6 13.5l6.8 4M15.4 6.5l-6.8 4"),
            // A friendly ghost: round head, arms open to the sides, wavy hem, eyes and a smile.
            Map.entry("ghost", "M7 10a5 5 0 0 1 10 0C18.6 9.4 21 9.6 21 11.3C21 12.7 19.3 13.1 18 13.5C18.2 15.6 18.9 17.4 18.3 19.2Q16.8 21 15.1 20.1Q13.5 19.4 12 20.5Q10.5 21.6 8.9 20.3Q7.2 19.2 5.7 19.2C5.1 17.4 5.8 15.6 6 13.5C4.7 13.1 3 12.7 3 11.3C3 9.6 5.4 9.4 7 10ZM9.6 10.6a0.4 0.7 0 1 0 0.8 0a0.4 0.7 0 1 0-0.8 0zM13.6 10.6a0.4 0.7 0 1 0 0.8 0a0.4 0.7 0 1 0-0.8 0zM10.7 13.4h2.6a1.3 1.3 0 0 1-2.6 0z"),
            Map.entry("sun", "M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8zM12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"),
            Map.entry("user", "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM12 7a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM6.2 18.6a7 7 0 0 1 11.6 0"),
            Map.entry("down", "M6 9l6 6 6-6"),
            Map.entry("up", "M6 15l6-6 6 6"),
            Map.entry("right", "M9 6l6 6-6 6"),
            Map.entry("save", "M5 3h11l5 5v13H5zM8 3v5h7M8 21v-7h8v7"),
            Map.entry("ok", "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM8 12l3 3 5-6"),
            Map.entry("warn", "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM12 8v4M12 16h.01"),
            Map.entry("minimize", "M6 12h12"),
            Map.entry("maximize", "M6 6h12v12H6z"),
            Map.entry("restore", "M9 9h10v10H9zM6 15V6a1 1 0 0 1 1-1h9"),
            Map.entry("disk", "M4 5h16v14H4zM4 13h16M8 16h.01"),
            // The mouse arrow, for interactive mode; crossed out when it is off.
            Map.entry("sync", "M3 12h5M8 12l5-6h6M8 12l5 6h6M17 3l3 3-3 3M17 15l3 3-3 3"),
            Map.entry("refresh", "M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"),
            Map.entry("cloud", "M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9z"),
            Map.entry("moon", "M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"),
            Map.entry("inbox", "M22 12h-6l-2 3h-4l-2-3H2M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z"),
            Map.entry("network", "M9 2h6v6H9zM3 16h6v6H3zM15 16h6v6h-6zM12 8v4M6 16v-2a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v2"),
            Map.entry("group", "M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2M9 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8zM22 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75"),
            Map.entry("send", "M22 2L11 13M22 2l-7 20-4-9-9-4z"),
            Map.entry("shield", "M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"));

    /** Icons drawn larger than the others at the same size: their trace leaves more room around it. */
    private static final Map<String, Double> LARGER = Map.of("ghost", 1.2);

    public static Node icon(String name, double side, String... classes) {
        SVGPath p = new SVGPath();
        p.setContent(TRACES.getOrDefault(name, TRACES.get("server")));
        p.getStyleClass().add("icon");
        p.getStyleClass().addAll(classes);
        double s = side / 24.0 * LARGER.getOrDefault(name, 1.0);
        p.setScaleX(s);
        p.setScaleY(s);
        // The Group makes the scale count in the sizes used by the layout.
        Group g = new Group(p);
        g.getStyleClass().add("icon-box");
        return g;
    }

    public static Node icon(String name, String... classes) {
        return icon(name, 14, classes);
    }
}
