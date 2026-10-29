package lemuri.fe.m;

import java.util.ArrayList;
import java.util.List;

/**
 * How a tab is divided: a single session, or two parts side by side (or one above the other), each of
 * them divided again in the same way. Changed by the session controller, drawn by the console.
 */
public final class Pane {
    /** The session shown here: set only when the pane is not divided. */
    private Session session;
    private boolean sideBySide;
    private Pane first, second;
    /** Where the separator is, from 0 to 1: kept while the console is rebuilt. */
    private double divider = 0.5;

    public Pane(Session session) {
        this.session = session;
    }

    public double divider() {
        return divider;
    }

    /** Moved by the user: remembered for when the console is rebuilt. */
    public void divider(double at) {
        divider = at;
    }

    public Session session() {
        return session;
    }

    public boolean divided() {
        return session == null;
    }

    public boolean sideBySide() {
        return sideBySide;
    }

    public Pane first() {
        return first;
    }

    public Pane second() {
        return second;
    }

    /** The sessions in the pane, from the top left. */
    public List<Session> sessions() {
        List<Session> all = new ArrayList<>();
        collect(all);
        return all;
    }

    private void collect(List<Session> all) {
        if (!divided()) all.add(session);
        else {
            first.collect(all);
            second.collect(all);
        }
    }

    /** Puts {@code added} next to {@code target}: on its right or below it. False if {@code target} is not here. */
    public boolean split(Session target, Session added, boolean sideBySide) {
        if (!divided()) {
            if (session != target) return false;
            first = new Pane(target);
            second = new Pane(added);
            this.sideBySide = sideBySide;
            divider = 0.5;
            session = null;
            return true;
        }
        return first.split(target, added, sideBySide) || second.split(target, added, sideBySide);
    }

    /** Takes {@code s} away: the part next to it takes all the space. False if {@code s} is not here or is the only one. */
    public boolean remove(Session s) {
        if (!divided()) return false;
        for (Pane gone : new Pane[] {first, second}) {
            if (gone.divided() || gone.session != s) continue;
            Pane kept = gone == first ? second : first;
            session = kept.session;
            sideBySide = kept.sideBySide;
            first = kept.first;
            second = kept.second;
            divider = kept.divider;
            return true;
        }
        return first.remove(s) || second.remove(s);
    }
}
