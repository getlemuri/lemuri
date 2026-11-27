#!/usr/bin/env python3
"""The real backend end to end, as the app drives it: hosts and folders, the vault, an SSH session to a test
server (new host key, password remembered and then given by the vault), SFTP both ways, the double click's file
check, a local shell, preferences and an encrypted backup taken and restored.

Everything happens in a temporary home (HOME): the user's ~/.lemuri, ~/.ssh and preferences are
never read or written; the script checks that ~/.lemuri did not change. No network: the SSH server (tests/sshd) is
on loopback.

    tests/core_test.py [--core path/to/lemuri-core]   (default: build/lemuri-core; the package's bin/lemuri-core too)
"""
import argparse
import hashlib
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TIMEOUT = 30


class Core:
    """The backend on a pipe: requests with replies, events in a queue, questions answered by {@code answers}."""

    def __init__(self, command, env):
        self.p = subprocess.Popen([command], stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env, text=True, bufsize=1)
        self.next = 0
        self.replies = {}
        self.events = queue.Queue()
        self.asked = []
        self.answers = {}
        self.lock = threading.Condition()
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.p.stdout:
            m = json.loads(line)
            # A reply has an id and no "event" (the "task" events have an id of their own: the log line's).
            if "id" in m and "event" not in m:
                with self.lock:
                    self.replies[m["id"]] = m
                    self.lock.notify_all()
            elif m.get("event") == "ask":
                self.asked.append(m["kind"])
                answer = self.answers.get(m["kind"])
                self.send({"op": "answer", "ask": m["ask"], "value": answer(m) if callable(answer) else answer})
            else:
                self.events.put(m)

    def send(self, m):
        self.p.stdin.write(json.dumps(m) + "\n")
        self.p.stdin.flush()

    def call(self, op, **params):
        """The reply's value; an error reply raises."""
        self.next += 1
        rid = self.next
        self.send(dict(params, op=op, id=rid))
        with self.lock:
            if not self.lock.wait_for(lambda: rid in self.replies, TIMEOUT):
                raise AssertionError("no reply to " + op)
            r = self.replies.pop(rid)
        if "error" in r:
            raise RuntimeError("%s: %s" % (op, r["error"]))
        return r.get("ok")

    def fails(self, op, **params):
        try:
            self.call(op, **params)
        except RuntimeError as e:
            return str(e)
        raise AssertionError(op + " should have failed")

    def wait(self, test, what):
        """The first event for which test(event) is true; the others are dropped."""
        end = time.time() + TIMEOUT
        while time.time() < end:
            try:
                e = self.events.get(timeout=end - time.time())
            except queue.Empty:
                break
            if test(e):
                return e
        raise AssertionError("waited in vain for " + what)

    def output(self, session, text):
        """Waits until the session printed {@code text}."""
        seen = []

        def has(e):
            if e.get("event") == "data" and e.get("session") == session:
                seen.append(e["data"])
            return text in "".join(seen)

        self.wait(has, repr(text) + " from " + session)

    def state(self, session, state):
        def got(e):
            if e.get("event") == "state" and e.get("session") == session and e["state"] == "ERROR":
                raise AssertionError("session error: " + str(e.get("message")))
            return e.get("event") == "state" and e.get("session") == session and e["state"] == state
        self.wait(got, session + " " + state)

    def stop(self):
        self.p.stdin.close()
        try:
            return self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
            return None


def fingerprint(folder):
    """What ~/.lemuri holds: names, sizes and times, to see that the test did not touch it."""
    if not os.path.isdir(folder):
        return None
    h = hashlib.sha256()
    for base, _, files in sorted(os.walk(folder)):
        for f in sorted(files):
            p = os.path.join(base, f)
            try:
                st = os.stat(p)
            except FileNotFoundError:
                continue
            h.update(("%s %d %d\n" % (p, st.st_size, st.st_mtime_ns)).encode())
    return h.hexdigest()


def start_sshd(work):
    """The test SSH server (tests/sshd/test_sshd.py); its port."""
    root = os.path.join(work, "server")
    os.makedirs(root)
    with open(os.path.join(root, "hello.txt"), "w") as f:
        f.write("hello from the server\n")
    # A key protected by a passphrase, the only one the server accepts.
    key = os.path.join(work, "id_test")
    subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "key pass", "-C", "test", "-f", key], check=True)
    p = subprocess.Popen([sys.executable, os.path.join(HERE, "sshd", "test_sshd.py"), root, key + ".pub"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return p, int(p.stdout.readline()), root


checks = 0
ONLY = None


def want(section):
    return ONLY is None or section in ONLY


def check(cond, what):
    global checks
    if not cond:
        raise AssertionError(what)
    checks += 1
    print("  ok  " + what)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=os.path.join(ROOT, "build", "lemuri-core"))
    ap.add_argument("--only", help="the sections to run, separated by commas (each needs those before it that it uses)")
    args = ap.parse_args()
    global ONLY
    ONLY = set(args.only.split(",")) if args.only else None

    real = os.path.expanduser("~/.lemuri")
    before = fingerprint(real)
    work = tempfile.mkdtemp(prefix="lemuri-core-test-")
    home = os.path.join(work, "home")
    os.makedirs(home)
    sshd, port, served = start_sshd(work)
    env = dict(os.environ, HOME=home, SHELL="/bin/bash", LANG="C.UTF-8", SSH_AUTH_SOCK="")
    core = Core(args.core, env)
    try:
        run(core, home, port, served, work)
    finally:
        code = core.stop()
        sshd.stdin.close()
        sshd.wait(10)
        shutil.rmtree(work, ignore_errors=True)
    check(code == 0, "the backend quits when its input closes (exit code %s)" % code)
    check(fingerprint(real) == before, "~/.lemuri untouched")
    print("%d checks passed" % checks)


def run(core, home, port, served, work):
    if want("application"):
        print("application")
        hello = core.call("hello")
        check(hello["version"] and not hello["vaultExists"] and not hello["vaultOpen"], "hello: version, no vault yet")
        texts = core.call("texts")
        check(len(texts) > 300 and texts.get("common.cancel") == "Cancel", "texts: %d keys" % len(texts))
        core.call("prefs.put", node="ui", values={"coreTest": "42"})
        check(core.call("prefs.get", node="ui").get("coreTest") == "42", "preferences written and read back")
        check(os.path.isfile(os.path.join(home, ".java", ".userPrefs", "lemuri", "ui", "prefs.xml")),
              "preferences in the temporary home, in the file shared with the first Lemuri")

        # Every request in PROTOCOL.md exists, and every one that exists is in PROTOCOL.md.
        with open(os.path.join(ROOT, "backend", "PROTOCOL.md")) as f:
            # The request tables come before "Activity log" (then the kinds of question, which are not requests).
            text = f.read().split("## Activity log")[0]
        documented = set()
        for first in re.findall(r"^\| ([^|]*`[a-z][^|]*) \|", text, re.M):
            documented |= set(re.findall(r"`([a-z]+(?:\.[A-Za-z]+)?)`", first))
        known = set(core.call("protocol.ops"))
        check(documented == known, "PROTOCOL.md and the backend list the same %d requests%s" % (len(known),
              "" if documented == known else ": only documented %s, only in the code %s" % (sorted(documented - known), sorted(known - documented))))

    if want("hosts and folders"):
        print("hosts and folders")
        check(core.call("hosts")["hosts"] == [], "no hosts at first")
        core.call("folder.create", path="lab/inner")
        h = core.call("host.save", host={"name": "mina", "address": "127.0.0.1", "port": port, "user": "tester",
                                         "authMethod": "PASSWORD", "group": "lab"})
        hid = h["id"]
        check(hid and h["group"] == "lab", "host saved in its folder")
        core.call("folder.move", **{"from": "lab", "to": "test"})
        hosts = core.call("hosts")
        check(hosts["hosts"][0]["group"] == "test" and "test/inner" in hosts["folders"], "renaming a folder moves its host and subfolder")
        check(os.path.exists(os.path.join(home, ".lemuri", "hosts.json")), "hosts.json in the temporary home")

    if want("vault"):
        print("vault")
        core.call("vault.create", password="master-pass-1234")
        check(core.call("vault.state") == {"exists": True, "open": True}, "vault created and open")
        core.call("vault.lock")
        check(not core.call("vault.open", password="wrong")["ok"], "wrong master password refused")
        check(core.call("vault.open", password="master-pass-1234")["ok"], "right master password opens it")

    if want("ssh"):
        print("ssh")
        core.answers = {"confirm": True, "password": {"text": "secret", "remember": True}}
        s = core.call("session.open", host=hid, cols=100, rows=30)["session"]
        # Resized while still signing in (a terminal not laid out yet): the shell must get the last size.
        core.call("session.resize", session=s, cols=3, rows=2)
        core.call("session.resize", session=s, cols=120, rows=40)
        core.state(s, "CONNECTED")
        check(core.asked == ["confirm", "password"], "new host key confirmed, password asked")
        mode = os.stat(os.path.join(home, ".ssh")).st_mode & 0o777
        check(mode == 0o700, "a new ~/.ssh is the user's only: %o" % mode)
        core.call("session.input", session=s, data="stty size\n")
        core.output(s, "40 120")
        check(True, "the shell has the last size given while connecting")
        core.call("session.input", session=s, data="echo lemuri-$((40+2))\n")
        core.output(s, "lemuri-42")
        check(True, "the remote shell runs commands")
        deadline = time.time() + 10
        while not core.call("vault.has", host=hid) and time.time() < deadline:
            time.sleep(0.2)
        check(core.call("vault.has", host=hid), "the accepted password is in the vault")

    if want("sftp"):
        print("sftp")
        home_dir = core.call("sftp.home", session=s)
        check(home_dir == "/", "sftp home: " + str(home_dir))
        check(core.call("sftp.existing", session=s, folder="/", files=["/x/hello.txt", "/x/new.txt"]) == ["hello.txt"],
              "names already on the server")
        local = os.path.join(work, "up.txt")
        with open(local, "w") as f:
            f.write("uploaded\n" * 1000)
        t = core.call("sftp.upload", session=s, folder="/", files=[local], known=[])["transfer"]
        done = core.wait(lambda e: e.get("event") == "transfer" and e["transfer"] == t and e["finished"], "upload")
        check(done["outcome"] == "DONE", "upload: " + str(done.get("message")))
        with open(os.path.join(served, "up.txt")) as f:
            check(f.read() == "uploaded\n" * 1000, "the uploaded file is on the server, whole")
        into = os.path.join(work, "downloads")
        os.makedirs(into)
        t = core.call("sftp.download", session=s, path="/hello.txt", into=into, replace=False)["transfer"]
        done = core.wait(lambda e: e.get("event") == "transfer" and e["transfer"] == t and e["finished"], "download")
        check(done["outcome"] == "DONE", "download: " + str(done.get("message")))
        with open(os.path.join(into, "hello.txt")) as f:
            check(f.read() == "hello from the server\n", "the downloaded file is here, whole")
        check(core.call("sftp.locate", session=s, shell="/", listed=None, name="hello.txt")["path"] == "/hello.txt",
              "a name listed by ls is found")
        listed = core.call("sftp.list", session=s, path="")
        names = {e["name"]: e for e in listed["entries"]}
        check(listed["path"] == "/" and "hello.txt" in names and not names["hello.txt"]["dir"]
              and names["hello.txt"]["size"] == len("hello from the server\n") and names["hello.txt"]["mtime"] > 0,
              "the file view lists the home folder, with sizes and dates")
        core.call("sftp.mkdir", session=s, path="/box")
        core.call("sftp.mkdir", session=s, path="/box/inner")
        with open(os.path.join(served, "box", "inner", "deep.txt"), "w") as f:
            f.write("deep\n")
        os.symlink(os.path.join(served, "hello.txt"), os.path.join(served, "box", "link"))
        check(core.call("sftp.list", session=s, path="/box/inner")["entries"][0]["name"] == "deep.txt", "a new folder, listed")
        core.call("sftp.rename", session=s, **{"from": "/box/inner", "to": "/box/moved"})
        check(os.path.isdir(os.path.join(served, "box", "moved")), "a folder renamed")
        refused = core.fails("sftp.rename", session=s, **{"from": "/box/moved", "to": "/box/link"})
        check(refused is not None and os.path.islink(os.path.join(served, "box", "link")), "renaming onto a name already there is refused")
        check(core.fails("sftp.remove", session=s, paths=["/"]) is not None and os.path.isdir(served), "the root is never deleted")
        check(core.fails("sftp.remove", session=s, paths=["box"]) is not None, "a path that is not absolute is refused")
        core.call("sftp.remove", session=s, paths=["/box"])
        check(not os.path.exists(os.path.join(served, "box")) and os.path.isfile(os.path.join(served, "hello.txt")),
              "a folder deleted with what is inside, the file a link pointed to kept")
        core.call("session.close", session=s)
        core.state(s, "CLOSED")
        check(True, "session closed")

    if want("ssh again: the vault answers"):
        print("ssh again: the vault answers")
        core.asked.clear()
        core.answers = {}
        s = core.call("session.open", host=hid, cols=80, rows=24)["session"]
        core.state(s, "CONNECTED")
        check(core.asked == [], "known host key and saved password: no questions")
        core.call("session.disconnectHost", host=hid)
        core.state(s, "CLOSED")
        check(True, "disconnecting the host closes its sessions")

    if want("host keys"):
        print("host keys")
        known = os.path.join(home, ".ssh", "known_hosts")
        with open(known) as f:
            remembered = f.read()
        item = "[127.0.0.1]:%d" % port

        def refused(what, line):
            with open(known, "w") as f:
                f.write(line)
            core.asked.clear()
            core.answers = {"confirm": True}
            s = core.call("session.open", host=hid, cols=80, rows=24)["session"]
            e = core.wait(lambda e: e.get("event") == "state" and e.get("session") == s and e["state"] in ("ERROR", "CONNECTED"), what)
            check(e["state"] == "ERROR" and "confirm" not in core.asked, what + ": refused without asking")
            with open(known) as f:
                check(f.read() == line, what + ": known_hosts left as it was")

        other = os.path.join(work, "other-host-key")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", other], check=True)
        refused("another key of the same kind", item + " " + " ".join(open(other + ".pub").read().split()[:2]) + "\n")
        subprocess.run(["ssh-keygen", "-q", "-t", "ecdsa", "-N", "", "-f", other + "-ecdsa"], check=True)
        refused("a key of another kind than the remembered one",
                item + " " + " ".join(open(other + "-ecdsa.pub").read().split()[:2]) + "\n")
        with open(known, "w") as f:
            f.write(remembered)
        core.answers = {}

    if want("sftp, the odd cases"):
        print("sftp, the odd cases")
        core.answers = {}
        s = core.call("session.open", host=hid, cols=80, rows=24)["session"]
        core.state(s, "CONNECTED")

        def transfer(op, **params):
            t = core.call(op, session=s, **params)["transfer"]
            return core.wait(lambda e: e.get("event") == "transfer" and e["transfer"] == t and e["finished"], op)

        def write(path, text):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(text)

        def read(path):
            with open(path) as f:
                return f.read()

        up = os.path.join(served, "up")
        write(os.path.join(up, "report1.txt"), "REPORT 1")
        write(os.path.join(up, ".report1.txt.lemuri-part"), "leftover")
        write(os.path.join(up, "app.conf"), "OLD")
        mine = os.path.join(work, "mine")
        write(os.path.join(mine, "report?.txt"), "NEW REPORT")
        write(os.path.join(mine, "app.conf"), "NEW")
        check(core.call("sftp.existing", session=s, folder="/up", files=[os.path.join(mine, "report?.txt"), os.path.join(mine, "app.conf")])
              == ["app.conf"], "\"report?.txt\" is not report1.txt: only app.conf is already there")
        transfer("sftp.upload", folder="/up", files=[os.path.join(mine, "report?.txt"), os.path.join(mine, "app.conf")], known=["app.conf"])
        check(read(os.path.join(up, "report?.txt")) == "NEW REPORT" and read(os.path.join(up, "report1.txt")) == "REPORT 1"
              and read(os.path.join(up, ".report1.txt.lemuri-part")) == "leftover", "a name with \"?\" touches only its own file")
        check(read(os.path.join(up, "app.conf")) == "NEW" and sorted(os.listdir(up)) == [".report1.txt.lemuri-part", "app.conf", "report1.txt", "report?.txt"],
              "an upload replaces the file and leaves nothing behind")

        site = os.path.join(mine, "site")
        write(os.path.join(site, "index.html"), "<h1>")
        write(os.path.join(site, "css", "a.css"), "body{}")
        os.symlink("/etc", os.path.join(site, "outside"))
        os.makedirs(os.path.join(served, "www"))
        transfer("sftp.upload", folder="/www", files=[site], known=[])
        check(read(os.path.join(served, "www", "site", "index.html")) == "<h1>" and read(os.path.join(served, "www", "site", "css", "a.css")) == "body{}"
              and not os.path.lexists(os.path.join(served, "www", "site", "outside")), "a folder goes up without its links")

        there = os.path.join(served, "srv", "site")
        write(os.path.join(there, "index.html"), "<h1>")
        write(os.path.join(there, "css", "a.css"), "a{}")
        os.symlink("/etc", os.path.join(there, "etc"))
        down = os.path.join(work, "down")
        os.makedirs(down)
        transfer("sftp.download", path="/srv/site", into=down, replace=False)
        check(read(os.path.join(down, "site", "index.html")) == "<h1>" and read(os.path.join(down, "site", "css", "a.css")) == "a{}"
              and not os.path.lexists(os.path.join(down, "site", "etc")), "a folder comes down without its links")
        # Again, into the folder now here: merged, and the files it would replace counted and asked about once.
        write(os.path.join(there, "index.html"), "<h2>")
        write(os.path.join(down, "site", "mine.txt"), "MINE")
        core.asked.clear()
        messages = []
        core.answers = {"confirm": lambda m: messages.append(m.get("message", "")) or False}
        done = transfer("sftp.download", path="/srv/site", into=down, replace=False)
        check(done["outcome"] == "CANCELLED" and read(os.path.join(down, "site", "index.html")) == "<h1>"
              and len(messages) == 1 and " 2 " in messages[0], "a merge that would replace 2 files is asked about: " + str(messages))
        core.answers = {"confirm": True}
        done = transfer("sftp.download", path="/srv/site", into=down, replace=False)
        check(done["outcome"] == "DONE" and read(os.path.join(down, "site", "index.html")) == "<h2>"
              and read(os.path.join(down, "site", "mine.txt")) == "MINE", "merged: replaced the server's files, kept the others")
        core.answers = {}
        write(os.path.join(served, "logs", "app.log"), "NEW LOG")
        write(os.path.join(down, "app.log"), "OLD")
        transfer("sftp.download", path="/logs/app.log", into=down, replace=True)
        check(read(os.path.join(down, "app.log")) == "NEW LOG" and sorted(os.listdir(down)) == ["app.log", "site"],
              "a file comes down, replaces the local one, nothing left behind")
        write(os.path.join(served, "star", "*"), "STAR")
        transfer("sftp.download", path="/star/*", into=down, replace=False)
        check(read(os.path.join(down, "*")) == "STAR", "a file called \"*\" comes down as itself")

        # Cancelled halfway: the old file stays, no partial file is left.
        big = os.path.join(mine, "big.bin")
        with open(big, "wb") as f:
            f.write(os.urandom(48 << 20))
        write(os.path.join(up, "big.bin"), "OLD")
        t = core.call("sftp.upload", session=s, folder="/up", files=[big], known=["big.bin"])["transfer"]
        core.call("transfer.cancel", transfer=t)
        done = core.wait(lambda e: e.get("event") == "transfer" and e["transfer"] == t and e["finished"], "cancelled upload")
        check(done["outcome"] != "DONE" and read(os.path.join(up, "big.bin")) == "OLD"
              and sorted(os.listdir(up)) == [".report1.txt.lemuri-part", "app.conf", "big.bin", "report1.txt", "report?.txt"],
              "a cancelled upload keeps the old file: " + str(done["outcome"]))
        with open(os.path.join(served, "big.bin"), "wb") as f:
            f.write(os.urandom(48 << 20))
        write(os.path.join(down, "big.bin"), "OLD")
        t = core.call("sftp.download", session=s, path="/big.bin", into=down, replace=True)["transfer"]
        core.call("transfer.cancel", transfer=t)
        done = core.wait(lambda e: e.get("event") == "transfer" and e["transfer"] == t and e["finished"], "cancelled download")
        check(done["outcome"] != "DONE" and read(os.path.join(down, "big.bin")) == "OLD"
              and sorted(os.listdir(down)) == ["*", "app.log", "big.bin", "site"], "a cancelled download keeps the old file: " + str(done["outcome"]))
        core.call("session.close", session=s)
        core.state(s, "CLOSED")

    if want("jump host"):
        print("jump host")
        # The same test server, reached through itself: the hop is the saved host "mina", whose password is in the vault.
        core.asked.clear()
        core.answers = {"password": {"text": "secret", "remember": False}}
        behind = core.call("host.save", host={"name": "behind", "address": "127.0.0.1", "port": port, "user": "other",
                                              "authMethod": "PASSWORD", "jump": "mina"})["id"]
        s = core.call("session.open", host=behind, cols=80, rows=24)["session"]
        core.state(s, "CONNECTED")
        check(core.asked == ["password"], "through the jump host: only the target's password asked (%s)" % core.asked)
        core.call("session.input", session=s, data="echo jumped-$((6*7))\n")
        core.output(s, "jumped-42")
        check(True, "the shell behind the jump host answers")
        check(core.call("sftp.home", session=s) == "/", "SFTP through the jump host")
        core.call("session.close", session=s)
        core.state(s, "CLOSED")
        core.call("host.save", host={"id": behind, "name": "behind", "address": "127.0.0.1", "port": port, "user": "other",
                                     "authMethod": "PASSWORD", "jump": "nobody-by-this-name:99999"})
        s = core.call("session.open", host=behind, cols=80, rows=24)["session"]
        core.wait(lambda e: e.get("event") == "state" and e.get("session") == s and e["state"] == "ERROR", "a bad jump host")
        check(True, "an impossible jump host ends in an error, not a hang")
        core.call("host.remove", host=behind)

    if want("key with passphrase"):
        print("key with passphrase")
        core.asked.clear()
        core.answers = {"secret": lambda m: {"text": "wrong"} if core.asked.count("secret") == 1 else {"text": "key pass"}}
        keyed = core.call("host.save", host={"name": "keyed", "address": "127.0.0.1", "port": port, "user": "tester",
                                             "authMethod": "KEY", "key": os.path.join(work, "id_test")})["id"]
        s = core.call("session.open", host=keyed, cols=80, rows=24)["session"]
        core.state(s, "CONNECTED")
        check(core.asked == ["secret", "secret"], "the passphrase asked again after a wrong one (%s)" % core.asked)
        core.call("session.input", session=s, data="echo keyed-$((40+2))\n")
        core.output(s, "keyed-42")
        check(True, "signed in with the key")
        core.call("session.close", session=s)
        core.state(s, "CLOSED")
        core.call("host.remove", host=keyed)

    if want("local shell"):
        print("local shell")
        s = core.call("session.open", local=True, cols=80, rows=24)
        check(s["host"] == "local" and s["title"] == "bash", "local session: " + s["title"])
        s = s["session"]
        core.state(s, "CONNECTED")
        core.call("session.resize", session=s, cols=120, rows=40)
        core.call("session.input", session=s, data="printf 'size %s\\n' \"$(stty size)\"\r")
        core.output(s, "size 40 120")
        check(True, "a real pseudo-terminal, resized")
        os.makedirs(os.path.join(home, "somedir"))
        found = core.call("fs.check", session=s, shell=home, listed=None, name="somedir")
        check(found["kind"] == "FOLDER" and found["typed"] == "somedir", "double click on a local folder: cd")
        with open(os.path.join(home, "my notes.txt"), "w") as f:
            f.write("text\n")
        found = core.call("fs.check", session=s, shell=home, listed=None, name="my notes.txt")
        check(found["kind"] == "TEXT" and found["typed"] == "'my notes.txt'", "double click on a text file: nano " + found["typed"])
        with open(os.path.join(home, "hey!.txt"), "w") as f:
            f.write("text\n")
        found = core.call("fs.check", session=s, shell=home, listed=None, name="hey!.txt")
        check(found["kind"] == "UNTYPABLE" and found["typed"] == "", "a ! in the name (csh expands it even in quotes): never typed")
        core.call("session.input", session=s, data="printf 'env %s %s\\n' \"$TERM\" \"$COLORTERM\"\r")
        core.output(s, "env xterm-256color truecolor")
        check(True, "the shell's terminal type, set before it starts")
        # A shell that does not read: what is typed waits, the backend goes on answering.
        core.call("session.input", session=s, data="sleep 4\r")
        time.sleep(0.5)
        started = time.time()
        core.call("session.input", session=s, data=("#" * 70 + "\r") * 1000)  # 71 KB: the terminal takes about 20
        core.call("hello")
        check(time.time() - started < 2, "a shell that does not read holds nothing else: %.2f s" % (time.time() - started))
        core.call("session.input", session=s, data="echo read-$((40+2))\r")
        core.output(s, "read-42")
        check(True, "what was typed meanwhile reaches the shell, in order")
        core.call("session.input", session=s, data="exit\r")
        core.state(s, "CLOSED")
        check(True, "exit ends the local shell")

    if want("monitoring"):
        print("monitoring")
        core.asked.clear()
        core.answers = {}

        def reading(host, good):
            def got(e):
                return (e.get("event") == "metrics" and e["host"] == host
                        and (e["result"]["resources"] is not None) == good and (e["result"]["error"] is None) == good)
            return core.wait(got, "a reading of " + host)["result"]

        core.call("monitor.start")
        r = reading(hid, True)
        check(r["resources"]["cpu"] > 0 and r["resources"]["memTotalKb"] > 0 and r["resources"]["disks"],
              "resources read over SSH, with the password from the vault")
        check(hid in core.call("metrics.last"), "the last reading is kept")
        check(len(core.call("metrics.history", host=hid)["cpu"]) >= 1, "the CPU history has a sample")
        u = core.call("host.updates", host=hid, force=True)
        check(u["error"] is None and "packages" in u and "reboot" in u, "pending updates read: %s" % u)
        sec = core.call("host.security", host=hid, force=True)
        ids = {c["id"]: c for c in sec["checks"]}
        check(sec["error"] is None and ids["client.auth"]["level"] == "BAD" and ids["conn.kex"]["value"]
              and "server.user" in ids and 0 <= sec["score"] <= 1, "security checks: score %.2f" % sec["score"])
        check(core.call("host.security", host=hid)["when"] == sec["when"], "the security report is kept for a while")
        # Through a jump host, with no password saved for the target: no question, an error.
        behind = core.call("host.save", host={"name": "behind", "address": "127.0.0.1", "port": port, "user": "other",
                                              "authMethod": "PASSWORD", "jump": "mina"})["id"]
        r = reading(behind, False)
        check("sign-in failed" in r["error"] and core.asked == [], "without a saved password: an error, never a question")
        core.call("vault.remember", host=behind, password="secret")
        core.call("metrics.refresh", host=behind, force=True)
        r = reading(behind, True)
        check(r["resources"]["cpu"] > 0, "resources read through the jump host")
        core.call("host.remove", host=behind)
        # The probe: a host whose port answers nothing.
        closed = core.call("host.save", host={"name": "nowhere", "address": "127.0.0.1", "port": 1, "user": "x",
                                              "authMethod": "PASSWORD"})["id"]
        core.wait(lambda e: e.get("event") == "hosts" and any(h["id"] == closed and h.get("state") == "OFFLINE" for h in e["hosts"]),
                  "the closed port seen offline")
        check(True, "a new host is probed at once: offline")
        core.call("monitor.tryAll")
        deadline = time.time() + 10
        while time.time() < deadline and not any(h["id"] == hid and h.get("state") == "ONLINE" for h in core.call("hosts")["hosts"]):
            time.sleep(0.2)
        check(any(h["id"] == hid and h.get("state") == "ONLINE" for h in core.call("hosts")["hosts"]), "the test server is online")
        core.call("host.remove", host=closed)
        check(core.call("monitor.probeMinutes", minutes=0) == 0 and core.call("monitor.probeMinutes") == 0, "probe set to never")
        check(core.call("monitor.probeMinutes", minutes=1) == 5, "every minute becomes every 5 minutes")
        core.call("monitor.probeMinutes", minutes=5)

    if want("backup"):
        print("backup")
        path = os.path.join(work, "all.lemuri")
        short = core.fails("backup.export", path=path, passphrase="short", files=[])
        check("short" in short.lower() or "character" in short.lower(), "a short passphrase is refused")
        core.call("backup.export", path=path, passphrase="a long passphrase 42", files=[])
        check(os.path.getsize(path) > 0 and b"mina" not in open(path, "rb").read(), "backup written, encrypted")
        check("backup.open" in core.fails("backup.open", path=path, passphrase="not this one 42"), "wrong passphrase refused")
        opened = core.call("backup.open", path=path, passphrase="a long passphrase 42")
        check(opened["hosts"] == 1 and opened["secrets"] >= 1, "backup holds the host and its password")
        core.call("host.remove", host=hid)
        check(core.call("hosts")["hosts"] == [], "host removed")
        core.call("backup.import", token=opened["token"], replace=True, files=[], source="test")
        hosts = core.call("hosts")["hosts"]
        check([x["name"] for x in hosts] == ["mina"], "host restored from the backup")
        check(core.call("vault.has", host=hosts[0]["id"]), "its password restored too")

    if want("saved formats"):
        print("saved formats")
        # Each in a home of its own, with a backend of its own.
        def fresh(name, hosts_json):
            h = os.path.join(work, name)
            os.makedirs(os.path.join(h, ".lemuri"))
            with open(os.path.join(h, ".lemuri", "hosts.json"), "w") as f:
                f.write(hosts_json)
            env = dict(os.environ, HOME=h, LANG="C.UTF-8")
            return h, Core(core.p.args[0], env)
        h, other = fresh("old-format", '[{"id":"h1","nome":"old","indirizzo":"192.0.2.1","porta":2222,"autenticazione":"CHIAVE",'
                                       '"gruppo":"Prod","daCollega":"a@b.c"}]')
        got = other.call("hosts")
        x = got["hosts"][0]
        check(x["name"] == "old" and x["address"] == "192.0.2.1" and x["port"] == 2222 and x["authMethod"] == "KEY"
              and x["group"] == "Prod" and x["fromContact"] == "a@b.c", "hosts.json of the first versions read")
        other.call("host.save", host=dict(x, note="touched"))
        other.stop()
        text = open(os.path.join(h, ".lemuri", "hosts.json")).read()
        check('"name"' in text and "nome" not in text and "indirizzo" not in text, "rewritten with today's names")
        h, other = fresh("corrupt", "{ this is not json")
        hello = other.call("hello")
        check(other.call("hosts")["hosts"] == [] and hello["loadingNotice"], "a corrupt hosts.json: empty list, and the user is told")
        aside = [f for f in os.listdir(os.path.join(h, ".lemuri")) if f.startswith("hosts.json.corrupt-")]
        check(len(aside) == 1 and open(os.path.join(h, ".lemuri", aside[0])).read() == "{ this is not json",
              "its content set aside, not deleted")
        other.stop()

    if want("vault password change"):
        print("vault password change")
        check(core.call("vault.change", current="master-pass-1234", password="new-master-5678")["ok"], "master password changed")
        core.call("vault.lock")
        check(core.call("vault.open", password="new-master-5678")["ok"], "the new one opens it")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print("FAILED: %s" % e)
        sys.exit(1)
