#!/usr/bin/env python3
"""A stand-in for the backend, for the tests of the Qt side: it speaks the same JSON lines (see backend/PROTOCOL.md)
for the hosts and the sessions, without touching any server or the user's data.

- hosts: two hosts, alpha (h1) and beta (h2);
- session.open: a session that reports CONNECTED at once; what is typed comes back as output, and "exit" ends it;
- session.close: ends it (CLOSED);
- fake.log: what was asked so far (op, session, data), for the test to check;
- anything else: answered with null.
"""
import json
import sys

log = []
sessions = {}
count = 0


def send(m):
    sys.stdout.write(json.dumps(m) + "\n")
    sys.stdout.flush()


def state(sid, s):
    if sessions.get(sid) == "CLOSED":
        return
    sessions[sid] = s
    send({"event": "state", "session": sid, "state": s, "message": ""})


HOSTS = [
    {"id": "h1", "name": "alpha", "address": "10.0.0.1", "user": "root", "port": 22, "state": "ONLINE"},
    {"id": "h2", "name": "beta", "address": "10.0.0.2", "user": "root", "port": 22, "state": "ONLINE"},
]

for line in sys.stdin:
    try:
        r = json.loads(line)
    except ValueError:
        continue
    op, rid = r.get("op"), r.get("id")
    log.append({"op": op, "session": r.get("session", r.get("host", "local" if r.get("local") else "")), "data": r.get("data", "")})
    ok = None
    if op == "hello":
        ok = {"version": "test", "protocol": 1, "language": "en", "user": "tester", "vaultExists": False, "vaultOpen": False}
    elif op == "hosts":
        # A line of the activity log whose id is the request's: it is an event, not the reply.
        send({"event": "task", "id": rid, "target": "x", "text": "y", "outcome": "OK"})
        ok = {"hosts": HOSTS, "created": [], "folders": []}
    elif op == "session.open":
        count += 1
        sid = "b%d" % count
        ok = {"session": sid}
        if rid is not None:
            send({"id": rid, "ok": ok})
        state(sid, "CONNECTED")
        continue
    elif op == "session.input":
        sid = r["session"]
        if sessions.get(sid) == "CONNECTED":
            send({"event": "data", "session": sid, "data": r["data"]})
            if "exit" in r["data"]:
                state(sid, "CLOSED")
    elif op == "session.close":
        state(r["session"], "CLOSED")
    elif op == "fake.log":
        ok = log[:-1]
    if rid is not None:
        send({"id": rid, "ok": ok})
