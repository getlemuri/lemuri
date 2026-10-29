package lemuri.be.terminal;

import com.jcraft.jsch.Channel;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.Proxy;
import com.jcraft.jsch.Session;
import com.jcraft.jsch.SocketFactory;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

/**
 * The connection to the next server goes through a direct-tcpip channel of the already connected jump
 * session, as {@code ssh -J} does: no local port is opened. The next session keeps its real address, so
 * its host key is checked against known_hosts like for a direct connection. Closing it closes the jump
 * session too (and, through its own proxy, the hops before it).
 */
final class JumpProxy implements Proxy {
    private final Session jump;
    private Channel channel;
    private InputStream in;
    private OutputStream out;

    JumpProxy(Session jump) {
        this.jump = jump;
    }

    @Override
    public void connect(SocketFactory factory, String host, int port, int timeout) throws JSchException, IOException {
        channel = jump.getStreamForwarder(host, port);
        in = channel.getInputStream();
        out = channel.getOutputStream();
        channel.connect(timeout);
    }

    @Override
    public InputStream getInputStream() {
        return in;
    }

    @Override
    public OutputStream getOutputStream() {
        return out;
    }

    @Override
    public Socket getSocket() {
        return null;
    }

    @Override
    public void close() {
        if (channel != null) channel.disconnect();
        jump.disconnect();
    }
}
