#!/usr/bin/env python3
"""The update of the installed package (backend/updates/Updates.cpp, PackageUpdater) through the backend, as the app drives it,
against a fake GitHub on loopback: a release with lemuri_9.9.9_<arch>.deb signed by a key made for the test.
It must be found, checked and handed to the installer (here a script that only notes it, never apt), and refused
when the package was changed, when the signature is a tag's (namespace git), when it is not the release's version,
or when the release has no package for this computer.

The fake GitHub, keys and installer are given through LEMURI_UPDATE_API, _SIGNERS and _INSTALL, which only the tests'
backend reads (build/lemuri-core-testing): the package's backend (build/lemuri-core) must ignore them, and that is
checked too.

    tests/update_test.py [--core path/to/lemuri-core-testing] [--package-core path/to/lemuri-core]
"""
import argparse
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core_test import Core, check, ROOT  # noqa: E402

ARCH = subprocess.run(["dpkg", "--print-architecture"], capture_output=True, text=True).stdout.strip()
DEB = "lemuri_9.9.9_%s.deb" % ARCH
served = {}  # path -> bytes


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = served.get(self.path)
        self.send_response(200 if body is not None else 404)
        self.end_headers()
        if body is not None:
            self.wfile.write(body)

    def log_message(self, *a):
        pass


def make_deb(work, name, version):
    root = os.path.join(work, "pkg-" + version + "-" + name)
    os.makedirs(os.path.join(root, "DEBIAN"))
    with open(os.path.join(root, "DEBIAN", "control"), "w") as f:
        f.write("Package: %s\nVersion: %s\nArchitecture: %s\nMaintainer: test <t@t>\nDescription: test\n" % (name, version, ARCH))
    out = os.path.join(work, "%s_%s.deb" % (name, version))
    subprocess.run(["dpkg-deb", "--build", root, out], check=True, capture_output=True)
    return open(out, "rb").read()


def sign(work, key, data, namespace="file"):
    f = os.path.join(work, "to-sign")
    with open(f, "wb") as o:
        o.write(data)
    subprocess.run(["ssh-keygen", "-Y", "sign", "-f", key, "-n", namespace, f], check=True, capture_output=True)
    sig = open(f + ".sig", "rb").read()
    os.remove(f + ".sig")
    return sig


def release(port, deb, sig, assets=True):
    served["/latest"] = json.dumps({
        "tag_name": "v9.9.9", "html_url": "https://github.com/getlemuri/lemuri/releases/tag/v9.9.9", "body": "## News\n- test",
        "assets": [{"name": DEB, "browser_download_url": "http://127.0.0.1:%d/%s" % (port, DEB)},
                   {"name": DEB + ".sig", "browser_download_url": "http://127.0.0.1:%d/%s.sig" % (port, DEB)}] if assets else []}).encode()
    served["/" + DEB] = deb
    served["/" + DEB + ".sig"] = sig


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default=os.path.join(ROOT, "build", "lemuri-core-testing"))
    ap.add_argument("--package-core", default=os.path.join(ROOT, "build", "lemuri-core"))
    args = ap.parse_args()
    work = tempfile.mkdtemp(prefix="lemuri-update-test-")
    home = os.path.join(work, "home")
    os.makedirs(home)
    key = os.path.join(work, "release-key")
    subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "test", "-f", key], check=True)
    signers = os.path.join(work, "signers")
    with open(signers, "w") as f:
        f.write('# test\nreleases@test namespaces="file" ' + open(key + ".pub").read().split(" ", 2)[0] + " "
                + open(key + ".pub").read().split(" ", 2)[1] + "\n")
    installed = os.path.join(work, "installed")
    installer = os.path.join(work, "install.sh")
    with open(installer, "w") as f:
        f.write('#!/bin/sh\n# The package as apt would get it: readable by others (apt reads it as _apt).\n'
                '[ -r "$1" ] && [ "$(stat -c %%a "$1")" = 644 ] && [ "$(stat -c %%a "$(dirname "$1")")" = 755 ] || exit 3\n'
                'dpkg-deb --field "$1" Version > "%s"\n' % installed)
    os.chmod(installer, 0o755)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    good = make_deb(work, "lemuri", "9.9.9")

    env = dict(os.environ, HOME=home, LANG="C.UTF-8",
               LEMURI_UPDATE_API="http://127.0.0.1:%d/latest" % port, LEMURI_UPDATE_SIGNERS=signers, LEMURI_UPDATE_INSTALL=installer)
    core = Core(args.core, env)
    steps, done, lines = [], [], []
    try:
        print("no release yet")
        check(core.call("update.check") is None, "GitHub has no release: nothing to offer")

        print("a release with only the sources")
        release(port, good, sign(work, key, good), assets=False)
        check(core.call("update.check") is None, "no package for this computer: not an update")

        print("a signed release")
        release(port, good, sign(work, key, good))
        r = core.call("update.check")
        check(r and r["version"] == "9.9.9", "release 9.9.9 found")
        check(core.call("update.blocker") is None, "this copy can update itself")
        core.call("update.install")
        check(open(installed).read().strip() == "9.9.9", "the verified package went to the installer")
        while not core.events.empty():
            e = core.events.get()
            if e.get("event") == "updateStep":
                steps.append(e["step"])
            elif e.get("event") == "updateStepDone":
                done.append(e["index"])
            elif e.get("event") == "updateLog":
                lines.append(e["line"])
        check(len(steps) == 3 and DEB in steps[0], "steps shown: " + " / ".join(steps))
        check(done == [0, 1, 2], "each step ended: %s" % done)
        check(any(l.startswith("$ ") for l in lines) and any("packages" in l or "Good" in l for l in lines),
              "the details shown: %d lines" % len(lines))
        os.remove(installed)

        def refused(what, deb, sig, words):
            release(port, deb, sig)
            core.call("update.check")
            error = core.fails("update.install")
            check(not os.path.exists(installed) and any(w in error for w in words), what + ": " + error.split(": ", 1)[1].splitlines()[0])

        print("refused")
        refused("changed after signing", good + b"x", sign(work, key, good), ["not valid"])
        refused("signed as a tag (namespace git)", good, sign(work, key, good, "git"), ["not signed", "not valid"])
        other = os.path.join(work, "other-key")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", other], check=True)
        refused("signed by another key", good, sign(work, other, good), ["not signed"])
        old = make_deb(work, "lemuri", "0.0.1")
        refused("an older package under the new name", old, sign(work, key, old), ["is not Lemuri 9.9.9"])
        alien = make_deb(work, "something-else", "9.9.9")
        refused("another package", alien, sign(work, key, alien), ["is not Lemuri 9.9.9"])
        print("the installer that runs as root (packaging/linux/lemuri-install)")
        layout = os.path.join(work, "installed-layout")
        os.makedirs(os.path.join(layout, "bin"))
        os.makedirs(os.path.join(layout, "share"))
        shutil.copy(os.path.join(ROOT, "packaging", "linux", "lemuri-install"), os.path.join(layout, "bin"))
        shutil.copy(signers, os.path.join(layout, "share", "package-signers"))
        fakes = os.path.join(work, "fake-bin")
        os.makedirs(fakes)
        took = os.path.join(work, "apt-took")
        with open(os.path.join(fakes, "apt-get"), "w") as f:
            f.write('#!/bin/sh\n# What apt would install, and whether only root could have changed it.\n'
                    'dpkg-deb --field "$3" Version > "%s"; stat -c "%%a" "$(dirname "$3")" >> "%s"\n' % (took, took))
        os.chmod(os.path.join(fakes, "apt-get"), 0o755)
        installed = os.path.join(work, "installed-version")
        with open(os.path.join(fakes, "dpkg-query"), "w") as f:
            f.write('#!/bin/sh\n# The version installed, as dpkg would say it (none if the file is missing).\n'
                    '[ -f "%s" ] && cat "%s" || exit 1\n' % (installed, installed))
        os.chmod(os.path.join(fakes, "dpkg-query"), 0o755)
        open(installed, "w").write("0.1.0")

        def helper(deb, sig, version):
            d, g = os.path.join(work, "h.deb"), os.path.join(work, "h.deb.sig")
            open(d, "wb").write(deb)
            open(g, "wb").write(sig)
            if os.path.exists(took):
                os.remove(took)
            return subprocess.run([os.path.join(layout, "bin", "lemuri-install"), d, g, version], capture_output=True,
                                  env=dict(os.environ, PATH=fakes + ":" + os.environ["PATH"])).returncode

        check(helper(good, sign(work, key, good), "9.9.9") == 0 and open(took).read().split() == ["9.9.9", "755"],
              "a signed package: apt gets its copy, in a folder only root writes")
        check(helper(good + b"x", sign(work, key, good), "9.9.9") == 3 and not os.path.exists(took), "changed after signing: refused (3)")
        check(helper(good, sign(work, other, good), "9.9.9") == 3, "signed by another key: refused (3)")
        check(helper(old, sign(work, key, old), "9.9.9") == 4, "another version under the name: refused (4)")
        check(helper(alien, sign(work, key, alien), "9.9.9") == 4, "another package: refused (4)")
        open(installed, "w").write("9.9.9")
        check(helper(good, sign(work, key, good), "9.9.9") == 6 and not os.path.exists(took), "the version already installed: refused (6)")
        open(installed, "w").write("10.0")
        check(helper(good, sign(work, key, good), "9.9.9") == 6 and not os.path.exists(took),
              "older than the installed one, though signed: refused (6)")
        os.remove(installed)
        check(helper(good, sign(work, key, good), "9.9.9") == 0, "nothing installed yet: installed")

        code = core.stop()
        check(code == 0, "the backend quits")

        print("the package's backend ignores the test settings")
        if os.path.exists(args.package_core):
            package = Core(args.package_core, env)
            # Not installed from the .deb and no other install command: it refuses to update itself.
            blocker = package.call("update.blocker")
            check(blocker is not None and "installed from the .deb" in blocker, "LEMURI_UPDATE_INSTALL ignored: " + str(blocker))
            check(package.stop() == 0, "the package's backend quits")
        else:
            print("  (skipped: no " + args.package_core + ")")
    finally:
        core.stop()
        server.shutdown()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    try:
        main()
        print("update test passed")
    except AssertionError as e:
        print("FAILED: %s" % e)
        sys.exit(1)
