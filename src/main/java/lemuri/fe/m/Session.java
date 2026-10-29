package lemuri.fe.m;

import java.util.List;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ObjectProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleObjectProperty;
import lemuri.be.dto.Host;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.Transport;

/**
 * An open SSH session: its channel lives until the session is closed. The
 * terminal that shows it belongs to the view (see {@code SessionViews}).
 */
public class Session {
    public final String id;
    public final Host host;
    public final String title;
    public final Transport transport;
    public final long startup = System.currentTimeMillis();
    public final ObjectProperty<SessionStatus> state = new SimpleObjectProperty<>(SessionStatus.CONNECTING);
    /**
     * For a tab: what is typed in one of its parts goes to all of them, like tmux synchronize-panes.
     * Off at the start, and off again when the tab is back to one part.
     */
    public final BooleanProperty synced = new SimpleBooleanProperty();
    /** The tab this session is a part of, after a split; null if it is a tab itself. */
    private Session owner;
    /** For a tab that has been split: how its sessions are arranged. Null while it shows only itself. */
    private Pane layout;

    public Session(String id, Host host, String title, Transport transport) {
        this.id = id;
        this.host = host;
        this.title = title;
        this.transport = transport;
    }

    public Session owner() {
        return owner;
    }

    /** Changed only by the session controller, when a tab is split or a part is closed. */
    public void owner(Session tab) {
        owner = tab;
    }

    public Pane layout() {
        return layout;
    }

    /** Changed only by the session controller, like {@link #owner(Session)}. */
    public void layout(Pane p) {
        layout = p;
    }

    /** The tab that shows this session: itself, or the tab it was split from. */
    public Session tab() {
        return owner != null ? owner : this;
    }

    /** The sessions shown in this tab, from the top left; only itself if it has not been split. */
    public List<Session> panes() {
        return layout != null ? layout.sessions() : List.of(this);
    }
}
