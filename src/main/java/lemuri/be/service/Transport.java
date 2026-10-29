package lemuri.be.service;

import java.util.function.BiConsumer;
import java.util.function.Consumer;
import lemuri.be.dto.SessionStatus;

/**
 * Channel to a remote shell. Callbacks can arrive from any thread: it is
 * the {@link Terminal} that brings them back to the JavaFX thread.
 */
public interface Transport {
    void onData(Consumer<String> cb);

    void onState(BiConsumer<SessionStatus, String> cb);

    /** Called once, when the terminal knows its size. */
    void launch(int columns, int lines);

    void send(String data);

    void resize(int columns, int lines);

    void close();
}
