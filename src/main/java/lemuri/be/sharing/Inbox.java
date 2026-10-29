package lemuri.be.sharing;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Consumer;
import lemuri.be.dto.Delivery;

/**
 * The incoming deliveries, from the local network and pasted as text, newest first. They live only
 * in memory: one not accepted before closing the app is gone (the text can be pasted again).
 * Every change is passed on whole to {@code onChange}.
 */
final class Inbox {
    private final Consumer<List<Delivery>> onChange;
    private final List<Delivery> all = new ArrayList<>();

    Inbox(Consumer<List<Delivery>> onChange) {
        this.onChange = onChange;
    }

    /** False if a delivery with the same id is already there. */
    synchronized boolean add(Delivery d) {
        if (all.stream().anyMatch(x -> x.id().equals(d.id()))) return false;
        all.add(d);
        publish();
        return true;
    }

    synchronized void remove(String id) {
        if (all.removeIf(d -> d.id().equals(id))) publish();
    }

    /** With the vault locked: the deliveries hold decrypted passwords. */
    synchronized void clear() {
        all.clear();
        publish();
    }

    private void publish() {
        List<Delivery> l = new ArrayList<>(all);
        l.sort((x, y) -> Long.compare(y.when(), x.when()));
        onChange.accept(l);
    }
}
