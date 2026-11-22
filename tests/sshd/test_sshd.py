#!/usr/bin/env python3
"""An SSH server for the tests (paramiko), on loopback and a free port printed on the first line: password "secret" for
any user, the keys of the authorized_keys file given as second argument, a /bin/sh shell on a terminal, commands run by
/bin/sh -c (the monitoring sends pipes, $(...) and several lines), SFTP inside the folder given as first argument (seen
as "/") and forwarding, to be a jump host. The host key is kept beside that folder (sshd-host.key), so a second start
has the same one. Quits when its standard input closes.

    tests/sshd/test_sshd.py ROOT [AUTHORIZED_KEYS]
"""
import errno
import fcntl
import os
import pty
import select
import signal
import socket
import struct
import subprocess
import sys
import termios
import threading

import paramiko
from paramiko import SFTPAttributes, SFTPHandle, SFTPServer, SFTPServerInterface, SFTP_OK


def pump(read, write, done=None):
    """Copies until the end; {@code done} is called after."""
    try:
        while True:
            b = read()
            if not b:
                break
            write(b)
    except (OSError, EOFError, socket.error):
        pass
    finally:
        if done:
            done()


class Sftp(SFTPServerInterface):
    """The files under ROOT, which the client sees as "/"."""

    root = "/"

    def path(self, p):
        p = os.path.normpath("/" + (p or "/")).lstrip("/")
        return os.path.join(self.root, p)

    @staticmethod
    def error(e):
        return SFTPServer.convert_errno(e.errno)

    def canonicalize(self, p):
        return os.path.normpath("/" + (p or "/"))

    def list_folder(self, p):
        try:
            out = []
            for name in os.listdir(self.path(p)):
                a = SFTPAttributes.from_stat(os.lstat(os.path.join(self.path(p), name)))
                a.filename = name
                out.append(a)
            return out
        except OSError as e:
            return self.error(e)

    def stat(self, p):
        try:
            return SFTPAttributes.from_stat(os.stat(self.path(p)))
        except OSError as e:
            return self.error(e)

    def lstat(self, p):
        try:
            return SFTPAttributes.from_stat(os.lstat(self.path(p)))
        except OSError as e:
            return self.error(e)

    def open(self, p, flags, attr):
        try:
            mode = getattr(attr, "st_mode", None) or 0o644
            fd = os.open(self.path(p), flags, mode & 0o777)
        except OSError as e:
            return self.error(e)
        if flags & os.O_WRONLY:
            fm = "ab" if flags & os.O_APPEND else "wb"
        elif flags & os.O_RDWR:
            fm = "a+b" if flags & os.O_APPEND else "r+b"
        else:
            fm = "rb"
        h = SFTPHandle(flags)
        f = os.fdopen(fd, fm)
        h.filename = self.path(p)
        h.readfile = f
        h.writefile = f
        return h

    def remove(self, p):
        try:
            os.remove(self.path(p))
        except OSError as e:
            return self.error(e)
        return SFTP_OK

    def rename(self, old, new):
        try:
            os.rename(self.path(old), self.path(new))
        except OSError as e:
            return self.error(e)
        return SFTP_OK

    def posix_rename(self, old, new):
        return self.rename(old, new)

    def mkdir(self, p, attr):
        try:
            os.mkdir(self.path(p))
        except OSError as e:
            return self.error(e)
        return SFTP_OK

    def rmdir(self, p):
        try:
            os.rmdir(self.path(p))
        except OSError as e:
            return self.error(e)
        return SFTP_OK

    def chattr(self, p, attr):
        try:
            SFTPServer.set_file_attr(self.path(p), attr)
        except OSError as e:
            return self.error(e)
        return SFTP_OK

    def readlink(self, p):
        try:
            return os.readlink(self.path(p))
        except OSError as e:
            return self.error(e)

    def symlink(self, target, p):
        try:
            os.symlink(target, self.path(p))
        except OSError as e:
            return self.error(e)
        return SFTP_OK


class Server(paramiko.ServerInterface):
    def __init__(self, authorized):
        self.authorized = authorized
        self.pty = {}  # channel id -> (term, cols, rows)
        self.shells = set()
        self.commands = {}
        self.forwards = {}  # channel id -> (host, port)
        self.ready = threading.Condition()

    def get_allowed_auths(self, username):
        return "password,publickey" if self.authorized else "password"

    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if password == "secret" else paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_SUCCESSFUL if (key.get_name(), key.get_base64()) in self.authorized else paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_direct_tcpip_request(self, chanid, origin, destination):
        with self.ready:
            self.forwards[chanid] = destination
            self.ready.notify_all()
        return paramiko.OPEN_SUCCEEDED

    def check_channel_pty_request(self, channel, term, width, height, pw, ph, modes):
        self.pty[channel.get_id()] = [width, height]
        return True

    def check_channel_window_change_request(self, channel, width, height, pw, ph):
        size = self.pty.get(channel.get_id())
        if size is not None:
            size[:] = [width, height]
            fd = getattr(channel, "lemuri_pty", None)
            if fd is not None:
                fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", height, width, 0, 0))
        return True

    def check_channel_env_request(self, channel, name, value):
        return True

    def check_channel_shell_request(self, channel):
        threading.Thread(target=shell, args=(channel, self.pty.get(channel.get_id(), [80, 24])), daemon=True).start()
        return True

    def check_channel_exec_request(self, channel, command):
        threading.Thread(target=run, args=(channel, command.decode("utf-8", "replace")), daemon=True).start()
        return True

    def check_channel_subsystem_request(self, channel, name):
        return super().check_channel_subsystem_request(channel, name)


def shell(channel, size):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.chdir(Sftp.root)
        os.execv("/bin/sh", ["sh", "-i"])
    channel.lemuri_pty = fd
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", size[1], size[0], 0, 0))
    threading.Thread(target=pump, args=(lambda: channel.recv(32768), lambda b: os.write(fd, b)), daemon=True).start()

    def read():
        try:
            return os.read(fd, 32768)
        except OSError:
            return b""
    pump(read, channel.sendall)
    _, status = os.waitpid(pid, 0)
    os.close(fd)
    finish(channel, os.waitstatus_to_exitcode(status))


def run(channel, command):
    p = subprocess.Popen(["/bin/sh", "-c", command], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         cwd=Sftp.root)

    def to_stdin():
        pump(lambda: channel.recv(32768), lambda b: (p.stdin.write(b), p.stdin.flush()))
        try:
            p.stdin.close()
        except OSError:
            pass
    threading.Thread(target=to_stdin, daemon=True).start()
    err = threading.Thread(target=pump, args=(lambda: p.stderr.read1(32768), channel.sendall_stderr), daemon=True)
    err.start()
    pump(lambda: p.stdout.read1(32768), channel.sendall)
    err.join()
    finish(channel, p.wait())


def finish(channel, code):
    try:
        channel.send_exit_status(code if code >= 0 else 128 - code)
        channel.shutdown_write()
        channel.close()
    except (OSError, EOFError):
        pass


def forward(channel, destination):
    try:
        s = socket.create_connection(destination, timeout=10)
    except OSError:
        channel.close()
        return
    s.settimeout(None)

    def closed():
        try:
            s.shutdown(socket.SHUT_WR)
        except OSError:
            pass
    threading.Thread(target=pump, args=(lambda: channel.recv(32768), s.sendall, closed), daemon=True).start()
    pump(lambda: s.recv(32768), channel.sendall, channel.close)


def client(sock, host_key, authorized):
    t = paramiko.Transport(sock)
    t.add_server_key(host_key)
    t.set_subsystem_handler("sftp", SFTPServer, Sftp)
    server = Server(authorized)
    try:
        t.start_server(server=server)
    except (paramiko.SSHException, EOFError, OSError):
        return
    while t.is_active():
        channel = t.accept(1)
        if channel is None:
            continue
        with server.ready:
            destination = server.forwards.pop(channel.get_id(), None)
        if destination:
            threading.Thread(target=forward, args=(channel, destination), daemon=True).start()


def main():
    root = os.path.abspath(sys.argv[1])
    Sftp.root = root
    authorized = set()
    if len(sys.argv) > 2:
        for line in open(sys.argv[2]):
            p = line.split()
            if len(p) >= 2:
                authorized.add((p[0], p[1]))
    key_file = os.path.join(os.path.dirname(root), "sshd-host.key")
    if not os.path.exists(key_file):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "test-sshd", "-f", key_file], check=True)
    host_key = paramiko.Ed25519Key.from_private_key_file(key_file)
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", 0))
    listener.listen(50)
    print(listener.getsockname()[1], flush=True)

    def accept():
        while True:
            try:
                s, _ = listener.accept()
            except OSError:
                return
            threading.Thread(target=client, args=(s, host_key, authorized), daemon=True).start()
    threading.Thread(target=accept, daemon=True).start()
    signal.signal(signal.SIGCHLD, signal.SIG_DFL)
    while sys.stdin.buffer.read(4096):
        pass
    os._exit(0)


if __name__ == "__main__":
    main()
