package lemuri.be.dto;

/** State of a session; the text to show is decided by the view (see Ui.text). */
public enum SessionStatus {
    CONNECTING, CONNECTED, CLOSED, ERROR;

    public boolean alive() {
        return this == CONNECTING || this == CONNECTED;
    }
}
