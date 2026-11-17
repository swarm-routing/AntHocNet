#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""swarm-core at the release swarm-core.lock names.

core/ builds on swarm-routing/ns3-swarm-core's dual-licensed core/
(swarm-routing/ns3-swarm-core#14), at the release swarm-core.lock locks
(swarm-routing/ns3-swarm-core#134: tag, commit, the release tarball and its
SHA256, every value read from the release by swarm-routing/.github
tools/checks/release_lock.py). The core's own CMake build takes it from its
vcpkg port (swarm-routing/.github#489), or, with SWARM_CORE_DIR, from the
sources this script unpacks (`make test` without VCPKG_ROOT, and the coverage,
fuzz and analysis jobs). The ns-3 builds get the same release from this
script: it downloads the tarball, checks its SHA256 against the lock and
unpacks it under .swarm-core/<tag>/swarm-core/ (git-ignored). That tree is the
swarm-core ns-3 module (`make install-ns3` installs it as contrib/swarm-core,
which the adapter links: SwarmRoutingBase, S10 of #1019), and its core/ is what
the coverage job compiles core/'s sources against.

Usage:
    tools/swarm_core.py path      print swarm-core's core/ directory (fetches first)
    tools/swarm_core.py module    print the swarm-core ns-3 module's directory
                                  (the release tree, core/ inside it)
    tools/swarm_core.py pin       print the locked repository, tag and commit

Environment:
    SWARM_CORE_DIR=<swarm-core checkout>/core  use that checkout instead
                                               (offline, or an unreleased
                                               change); `module` is its parent
    SWARM_CORE_CACHE=<dir>                     where to unpack (default
                                               <repo>/.swarm-core)

Standard library only, so it runs in the ns-3 images and on a bare runner.
"""

import hashlib
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "swarm-core.lock")
URL = "https://github.com/{repo}/releases/download/{tag}/{asset}"
KEY = re.compile(r"^(SWARM_CORE_[A-Z0-9_]+)=(.*)$")


def lock():
    vals = {}
    with open(LOCK, encoding="utf-8") as f:
        for line in f:
            m = KEY.match(line.strip())
            if m:
                vals[m.group(1)] = m.group(2)
    for k in ("REPO", "TAG", "COMMIT", "ASSET", "SHA256"):
        if not vals.get("SWARM_CORE_" + k):
            sys.exit(f"swarm_core: {LOCK} has no SWARM_CORE_{k}")
    return {k[len("SWARM_CORE_") :]: v for k, v in vals.items()}


def is_core(d):
    return os.path.isfile(os.path.join(d, "swarm-ports.h"))


def is_module(d):
    return is_core(os.path.join(d, "core")) and os.path.isfile(
        os.path.join(d, "model", "swarm-routing-base.h")
    )


def module():
    """The swarm-core ns-3 module: the release tree (or SWARM_CORE_DIR's parent)."""
    local = os.environ.get("SWARM_CORE_DIR")
    if local:
        local = os.path.abspath(local)
        if not is_core(local):
            sys.exit(f"swarm_core: SWARM_CORE_DIR={local} is not swarm-core's core/ directory")
        root = os.path.dirname(local)
        if not is_module(root):
            sys.exit(
                f"swarm_core: {root} (SWARM_CORE_DIR's parent) is not the swarm-core ns-3 module"
            )
        return root
    lk = lock()
    cache = os.path.abspath(os.environ.get("SWARM_CORE_CACHE") or os.path.join(ROOT, ".swarm-core"))
    dest = os.path.join(cache, lk["TAG"], "swarm-core")
    if is_module(dest):
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with tempfile.TemporaryDirectory(dir=os.path.dirname(dest)) as tmp:
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
        if not is_module(src):
            sys.exit(f"swarm_core: {lk['ASSET']} is not the swarm-core ns-3 module")
        shutil.move(src, dest)
    return dest


def path():
    """swarm-core's core/ directory."""
    local = os.environ.get("SWARM_CORE_DIR")
    if local:
        local = os.path.abspath(local)
        if not is_core(local):
            sys.exit(f"swarm_core: SWARM_CORE_DIR={local} is not swarm-core's core/ directory")
        return local
    return os.path.join(module(), "core")


def main(argv):
    if argv[1:] == ["pin"]:
        lk = lock()
        print(f"{lk['REPO']} {lk['TAG']} {lk['COMMIT']}")
    elif argv[1:] == ["path"]:
        print(path())
    elif argv[1:] == ["module"]:
        print(module())
    else:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
