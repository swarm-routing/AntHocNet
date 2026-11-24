#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""swarm-core and its ns-3 module at the releases the two locks name.

Two releases, two locks, every value read from the release by
swarm-routing/.github tools/checks/release_lock.py:

- swarm-core.lock: swarm-core's dual-licensed core/, which core/ builds on
  (swarm-routing/ns3-swarm-core#14, #134). The core's own CMake build takes it
  from its vcpkg port (swarm-routing/.github#489), or, with SWARM_CORE_DIR,
  from the sources this script unpacks (`make test` without VCPKG_ROOT, and
  the coverage, fuzz and analysis jobs). Up to v1.0.0 that release was the
  adapter's (its tarball carried core/); from swarm-core v2.0.0 it is
  swarm-routing/swarm-core's own (swarm-routing/.github#691). Either tarball
  holds a core/, which is all `path` needs.
- swarm-adapter-ns3.lock: the swarm-core ns-3 module (SwarmRoutingBase, S10 of
  #1019) that `make install-ns3` installs as contrib/swarm-core and the
  adapter links. From adapter v2.0.0 on it has no core/ of its own: it
  fetches swarm-core's release at configure time and passes its include
  directory on through ${libswarm-core} (swarm-routing/swarm-adapter-ns3#258).

Each release is downloaded, checked against its lock's SHA256 and unpacked
under .swarm-core/<repository>-<tag>/ (git-ignored): both repositories tag
v2.0.0, so the tag alone does not name a tree.

Usage:
    tools/swarm_core.py path        print swarm-core's core/ directory (fetches first)
    tools/swarm_core.py module      print the swarm-core ns-3 module's directory
    tools/swarm_core.py pin         print swarm-core.lock's repository, tag and commit
    tools/swarm_core.py module-pin  print swarm-adapter-ns3.lock's repository, tag and commit

Environment:
    SWARM_CORE_DIR=<swarm-core checkout>/core  `path` prints that checkout's core/
                                               (offline, or an unreleased change);
                                               `module` its parent, when that is an
                                               adapter tree with a core/ of its own
    SWARM_CORE_MODULE=<adapter checkout>       `module` prints that swarm-core ns-3
                                               module, with or without a core/
                                               (swarm-routing/swarm-adapter-ns3#258);
                                               `path` ignores it
    SWARM_CORE_CACHE=<dir>                     where to unpack (default
                                               <repo>/.swarm-core)

Standard library only, so it runs in the ns-3 images and on a bare runner.
"""

import fcntl
import hashlib
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE_LOCK = (os.path.join(ROOT, "swarm-core.lock"), "SWARM_CORE")
MODULE_LOCK = (os.path.join(ROOT, "swarm-adapter-ns3.lock"), "ADAPTER")
URL = "https://github.com/{repo}/releases/download/{tag}/{asset}"


def lock(which):
    path, prefix = which
    key = re.compile(rf"^({prefix}_[A-Z0-9_]+)=(.*)$")
    vals = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = key.match(line.strip())
            if m:
                vals[m.group(1)] = m.group(2)
    for k in ("REPO", "TAG", "COMMIT", "ASSET", "SHA256"):
        if not vals.get(f"{prefix}_{k}"):
            sys.exit(f"swarm_core: {path} has no {prefix}_{k}")
    return {k[len(prefix) + 1 :]: v for k, v in vals.items()}


def is_core(d):
    return os.path.isfile(os.path.join(d, "swarm-ports.h"))


def is_module(d):
    return os.path.isfile(os.path.join(d, "model", "swarm-routing-base.h"))


def has_core(d):
    return is_core(os.path.join(d, "core"))


def fetch(which, ok, what):
    """The release tree `which` locks, unpacked once; `ok(tree)` must hold."""
    lk = lock(which)
    cache = os.path.abspath(os.environ.get("SWARM_CORE_CACHE") or os.path.join(ROOT, ".swarm-core"))
    dest = os.path.join(cache, f"{lk['REPO'].split('/')[-1]}-{lk['TAG']}")
    if ok(dest):
        return dest
    os.makedirs(cache, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache) as tmp:
        arc = os.path.join(tmp, lk["ASSET"])
        url = URL.format(repo=lk["REPO"], tag=lk["TAG"], asset=lk["ASSET"])
        with urllib.request.urlopen(url, timeout=120) as r, open(arc, "wb") as f:
            shutil.copyfileobj(r, f)
        with open(arc, "rb") as f:
            got = hashlib.sha256(f.read()).hexdigest()
        if got != lk["SHA256"]:
            sys.exit(f"swarm_core: {lk['ASSET']} has SHA256 {got}, the lock says {lk['SHA256']}")
        with tarfile.open(arc, "r:gz") as tar:
            members = []
            for m in tar.getmembers():
                parts = m.name.split("/")
                # <top>/...: only plain files and directories, no absolute or
                # parent-relative names.
                if len(parts) < 2 or ".." in parts or m.name.startswith("/"):
                    continue
                if not (m.isfile() or m.isdir()):
                    continue
                members.append(m)
            if not members:
                sys.exit(f"swarm_core: {lk['ASSET']} is empty")
            tops = {m.name.split("/")[0] for m in members}
            if len(tops) != 1:
                sys.exit(
                    f"swarm_core: {lk['ASSET']} has more than one top directory: {sorted(tops)}"
                )
            safe = {"filter": "data"} if hasattr(tarfile, "data_filter") else {}
            tar.extractall(tmp, members=members, **safe)
            top = tops.pop()
        src = os.path.join(tmp, top)
        if not ok(src):
            sys.exit(f"swarm_core: {lk['REPO']} {lk['TAG']} {lk['ASSET']} is not {what}")
        # Two fetches of one release may race: publish under a lock, keep a
        # tree another process published, replace only an absent or broken one.
        with open(os.path.join(cache, ".lock"), "w") as held:
            fcntl.flock(held, fcntl.LOCK_EX)
            if ok(dest):
                return dest
            shutil.rmtree(dest, ignore_errors=True)
            shutil.move(src, dest)
    return dest


def module():
    """The swarm-core ns-3 module: SWARM_CORE_MODULE, SWARM_CORE_DIR's parent, or the release."""
    adapter = os.environ.get("SWARM_CORE_MODULE")
    if adapter:
        adapter = os.path.abspath(adapter)
        if not is_module(adapter):
            sys.exit(f"swarm_core: SWARM_CORE_MODULE={adapter} is not the swarm-core ns-3 module")
        return adapter
    local = os.environ.get("SWARM_CORE_DIR")
    if local:
        local = os.path.abspath(local)
        if not is_core(local):
            sys.exit(f"swarm_core: SWARM_CORE_DIR={local} is not swarm-core's core/ directory")
        root = os.path.dirname(local)
        if not is_module(root):
            sys.exit(
                f"swarm_core: {root} (SWARM_CORE_DIR's parent) is not the swarm-core ns-3 module;"
                " name an adapter checkout with SWARM_CORE_MODULE"
            )
        return root
    return fetch(MODULE_LOCK, is_module, "the swarm-core ns-3 module")


def path():
    """swarm-core's core/ directory: SWARM_CORE_DIR, or the release swarm-core.lock names."""
    local = os.environ.get("SWARM_CORE_DIR")
    if local:
        local = os.path.abspath(local)
        if not is_core(local):
            sys.exit(f"swarm_core: SWARM_CORE_DIR={local} is not swarm-core's core/ directory")
        return local
    return os.path.join(fetch(CORE_LOCK, has_core, "a tree with swarm-core's core/"), "core")


def main(argv):
    if argv[1:] in (["pin"], ["module-pin"]):
        lk = lock(CORE_LOCK if argv[1] == "pin" else MODULE_LOCK)
        print(f"{lk['REPO']} {lk['TAG']} {lk['COMMIT']}")
    elif argv[1:] == ["path"]:
        print(path())
    elif argv[1:] == ["module"]:
        print(module())
    else:
        usage = [p for p in __doc__.split("\n\n") if p.startswith("Usage:")]
        print(usage[0], file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
