#!/usr/bin/env python3
"""Two backends sharing hosts as two colleagues do: key cards exchanged, a host sent as a text envelope and opened on
the other side, a copy refused, then the local network on loopback (a PC added by address, a host delivered directly
with its receipt, a pairing with the 6-digit code).

    tests/team_test.py [--core build/lemuri-core]

In temporary homes, with the local network on free ports and no announcements (LEMURI_LAN_PORTS=0,0): the user's
data, preferences and network are never touched.
"""
import argparse
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core_test import Core, check, ROOT  # noqa: E402


def start(command, work, name):
    home = os.path.join(work, name)
    os.makedirs(home)
    env = dict(os.environ, HOME=home, LANG="C.UTF-8", SSH_AUTH_SOCK="", USER=name, LEMURI_LAN_PORTS="0,0")
    core = Core(command, env)
    core.call("vault.create", password="master-pass-" + name)
    core.call("team.start")
    deadline = time.time() + 15
    while core.call("team.state")["state"] != "READY" and time.time() < deadline:
        time.sleep(0.1)
    check(core.call("team.state")["state"] == "READY", name + ": sharing ready, keys made in the vault")
    return core


def lan_port(core):
    """Turns the local network on and returns its TCP port, read from the state message."""
    core.call("lan.on", on=True)
    deadline = time.time() + 15
    while time.time() < deadline:
        s = core.call("team.state")
        if s["lanState"] == "ACTIVE":
            return int(re.sub(r"\D", "", re.search(r"TCP ([\d,. ]+)", s["lanMessage"]).group(1)))
        time.sleep(0.1)
    raise AssertionError("the local network did not start: " + str(core.call("team.state")))


def pair(a_cmd, b_cmd):
    print("anna and bruno")
    work = tempfile.mkdtemp(prefix="lemuri-team-test-")
    anna = bruno = None
    try:
        anna = start(a_cmd, work, "anna")
        bruno = start(b_cmd, work, "bruno")
        card_a, card_b = anna.call("team.state")["keyCard"], bruno.call("team.state")["keyCard"]
        check(card_a.startswith("LEMURI-KEY 1 ") and card_a.endswith(" anna"), "key card with the user's name")
        parsed = bruno.call("team.parseKey", card="here:\n" + card_a + "\n")
        check(parsed["name"] == "anna" and parsed["fingerprint"] == anna.call("team.state")["fingerprint"],
              "the card read on the other side: same fingerprint")
        check("own key" in anna.fails("team.addContact", keyCard=card_a, name="me"), "your own key refused")
        anna.call("team.addContact", keyCard=card_b, name="bruno")
        bruno.call("team.addContact", keyCard=card_a, name="anna")
        check("already" in anna.fails("team.addContact", keyCard=card_b, name="bruno 2"), "a key already there refused")
        check(anna.call("lan.nameCheck", name="Brun0", key="other")["problem"], "a look-alike name flagged")
        contacts = anna.call("team.state")["contacts"]
        check([c["name"] for c in contacts] == ["bruno"] and not contacts[0]["nearby"], "bruno is a contact, not nearby")

        # As text: the local network is off.
        host = anna.call("host.save", host={"name": "db-prod", "address": "203.0.113.5", "port": 2222, "user": "admin",
                                            "authMethod": "PASSWORD", "os": "Debian 12"})
        anna.call("vault.remember", host=host["id"], password="s3cret")
        out = anna.call("team.send", host=host["id"], to=[contacts[0]["key"]], withPassword=True, message="the new db")
        text = out[0]["text"]
        check(len(out) == 1 and text.startswith("-----BEGIN LEMURI HOST-----") and "s3cret" not in text and "203.0.113.5" not in text,
              "sent as a text envelope, nothing readable in it")
        quoted = "Hi, here it is:\n> " + text.replace("\n", "\n> ")
        d = bruno.call("team.open", text=quoted)
        c = d["content"]
        check(d["from"] == "anna" and c["name"] == "db-prod" and c["port"] == 2222 and c["hasPassword"] and c["message"] == "the new db"
              and "password" not in c, "opened on the other side: from anna, the password stays in the backend")
        check("already" in bruno.fails("team.open", text=text), "the same text again: already among the incoming ones")
        r = bruno.call("team.accept", delivery=d["id"], mode="folder", folder="from anna")
        check(r["remembered"], "accepted, its id remembered")
        hosts = bruno.call("hosts")["hosts"]
        got = [h for h in hosts if h["id"] == r["host"]][0]
        check(got["group"] == "from anna" and got["fromContact"] == "anna" and got["address"] == "203.0.113.5"
              and not got.get("key"), "the host in the chosen folder, marked as from anna")
        check(bruno.call("vault.has", host=r["host"]), "its password in bruno's vault")
        check("already accepted or declined" in bruno.fails("team.open", text=text), "a copy pasted again is refused")
        mangled = text.replace(text.split("\n")[2][10], "A" if text.split("\n")[2][10] != "A" else "B", 1)
        bruno.fails("team.open", text=mangled)
        check(True, "a changed envelope does not open")

        # Over the local network, on loopback.
        port_b = lan_port(bruno)
        lan_port(anna)
        peer = anna.call("lan.addAddress", address="127.0.0.1:%d" % port_b)
        check(peer["key"] == contacts[0]["key"] and peer["trusted"] and peer["trustedName"] == "bruno", "bruno's PC found by address")
        deadline = time.time() + 10
        while not anna.call("team.state")["contacts"][0]["nearby"] and time.time() < deadline:
            time.sleep(0.2)
        check(anna.call("team.state")["contacts"][0]["nearby"], "bruno now nearby")
        out = anna.call("team.send", host=host["id"], to=[contacts[0]["key"]], withPassword=False)
        check(out[0]["text"] is None, "delivered directly, with bruno's receipt")
        e = bruno.wait(lambda e: e.get("event") == "arrival", "the arrival over the local network")
        c = e["delivery"]["content"]
        check(e["delivery"]["from"] == "anna" and c["name"] == "db-prod" and not c["hasPassword"] and c["twin"] == r["host"],
              "arrived at bruno, without password, seen as the host he already has")
        bruno.call("team.reject", delivery=e["delivery"]["id"])
        check(bruno.call("team.state")["deliveries"] == [], "rejected: gone from the incoming ones")

        # Pairing with the code: bruno forgets anna, anna asks again.
        bruno.call("lan.forget", key=parsed and anna.call("team.state")["contacts"][0]["key"] and
                   [x for x in bruno.call("team.state")["contacts"] if x["name"] == "anna"][0]["key"])
        check(bruno.call("team.state")["contacts"] == [], "bruno forgot anna")
        codes = {}
        bruno.answers = {"pair": lambda m: codes.setdefault("bruno", m["code"]) and "anna"}
        # Only a PC whose user waits for a link answers.
        check("not waiting" in anna.fails("lan.pair", key=contacts[0]["key"]), "bruno not waiting for a link: refused")
        check(not codes, "nothing asked on bruno's screen")
        check(bruno.call("lan.waitForLink", on=True), "bruno waits for a link")
        ok = anna.call("lan.pair", key=contacts[0]["key"])
        e = anna.wait(lambda e: e.get("event") == "pairCode", "the code on anna's screen")
        check(ok and e["code"] == codes.get("bruno") and re.match(r"^\d{3} \d{3}$", e["code"]), "the same code on both screens: " + e["code"])
        check([x["name"] for x in bruno.call("team.state")["contacts"]] == ["anna"], "bruno trusts anna again")
        check("too many" in anna.fails("lan.pair", key=contacts[0]["key"]), "a second request within a minute refused")
    finally:
        for c in (anna, bruno):
            if c:
                c.stop()
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=os.path.join(ROOT, "build", "lemuri-core"))
    args = ap.parse_args()
    pair(args.core, args.core)
    import core_test
    print("%d checks passed" % core_test.checks)


if __name__ == "__main__":
    try:
        main()
    except AssertionError as e:
        print("FAILED: %s" % e)
        sys.exit(1)
