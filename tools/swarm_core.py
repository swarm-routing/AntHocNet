#!/usr/bin/env python3
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
"""swarm-core's core/ at the commit core/CMakeLists.txt pins (SWARM_CORE_REF).

core/ builds on swarm-routing/ns3-swarm-core's dual-licensed core/
(swarm-routing/ns3-swarm-core#14). The core's own CMake build fetches it with
FetchContent; the builds that compile core/'s sources by name (`make
install-ns3`, which copies them into contrib/anthocnet, and the coverage job)
get the same files from this script: it downloads swarm-core's source archive
at the pinned commit and unpacks its core/ under .swarm-core/<sha>/
(git-ignored).

Usage:
    tools/swarm_core.py path      print swarm-core's core/ directory (fetches first)
    tools/swarm_core.py pin       print the pinned commit

Environment:
    SWARM_CORE_DIR=<swarm-core checkout>/core  use that directory instead
                                               (offline, or a local change)
    SWARM_CORE_CACHE=<dir>                     where to unpack (default
                                               <repo>/.swarm-core)

Standard library only, so it runs in the ns-3 images and on a bare runner.
"""

import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CMAKE = os.path.join(ROOT, "core", "CMakeLists.txt")
ARCHIVE = "https://codeload.github.com/swarm-routing/ns3-swarm-core/tar.gz/{sha}"
PIN = re.compile(r'^\s*set\(\s*SWARM_CORE_REF\s+"([0-9a-f]{40})"', re.MULTILINE)


def pin():
    with open(CMAKE, encoding="utf-8") as f:
        m = PIN.search(f.read())
    if not m:
        sys.exit(f"swarm_core: no SWARM_CORE_REF in {CMAKE}")
    return m.group(1)


def is_core(d):
    return os.path.isfile(os.path.join(d, "swarm-ports.h"))


def path():
    local = os.environ.get("SWARM_CORE_DIR")
    if local:
        local = os.path.abspath(local)
        if not is_core(local):
            sys.exit(f"swarm_core: SWARM_CORE_DIR={local} is not swarm-core's core/ directory")
        return local
    sha = pin()
    cache = os.environ.get("SWARM_CORE_CACHE") or os.path.join(ROOT, ".swarm-core")
    dest = os.path.join(cache, sha, "core")
    if is_core(dest):
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with tempfile.TemporaryDirectory(dir=os.path.dirname(dest)) as tmp:
        arc = os.path.join(tmp, "src.tar.gz")
        with urllib.request.urlopen(ARCHIVE.format(sha=sha), timeout=120) as r, open(arc, "wb") as f:
            shutil.copyfileobj(r, f)
        with tarfile.open(arc, "r:gz") as tar:
            members = []
            for m in tar.getmembers():
                parts = m.name.split("/")
                # <top>/core/...: only core/, only plain files and directories,
                # no absolute or parent-relative names.
                if len(parts) < 2 or parts[1] != "core" or ".." in parts or m.name.startswith("/"):
                    continue
                if not (m.isfile() or m.isdir()):
                    continue
                members.append(m)
            if not members:
                sys.exit(f"swarm_core: the archive of {sha} has no core/")
            safe = {"filter": "data"} if hasattr(tarfile, "data_filter") else {}
            tar.extractall(tmp, members=members, **safe)
            top = members[0].name.split("/")[0]
        got = os.path.join(tmp, top, "core")
        if not is_core(got):
            sys.exit(f"swarm_core: the archive of {sha} has no core/swarm-ports.h")
        shutil.move(got, dest)
    return dest


def main(argv):
    if argv[1:] == ["pin"]:
        print(pin())
    elif argv[1:] == ["path"]:
        print(path())
    else:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
