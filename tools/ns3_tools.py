#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""The pinned swarm-routing/ns3-tools release (swarm-routing/ns3-tools#29).

The routing-bench harness and the benchmark readers moved to ns3-tools; this
repository builds and reads with the release named in ns3-tools.lock. This
script fetches that release's two assets, checks each against the lock's
sha256, and unpacks them under .ns3-tools/<tag>/ (git-ignored):

    routing-bench/   the ns-3 module (routing-bench-<ver>.tar.gz), which
                     `make install-ns3` copies to contrib/routing-bench
    python/          the ns3_tool package (the wheel, unpacked; pure Python)

Usage:
    tools/ns3_tools.py fetch                     fetch and unpack; print the root
    tools/ns3_tools.py path routing-bench|python print one directory (fetches first)
    tools/ns3_tools.py run MODULE [ARGS...]      run ns3_tool.MODULE as a script,
                                                 e.g. run bench_parse --get pdr ...
    tools/ns3_tools.py pin                       print repo, tag and commit
    tools/ns3_tools.py verify                    the tag still names the locked
                                                 commit, the assets their sha256

From Python (any script in this repository):

    sys.path.insert(0, <repo>/tools); import ns3_tools
    bench_parse = ns3_tools.module("bench_parse")

Environment:
    NS3_TOOLS_DIR=<ns3-tools checkout>  use its ns3/routing-bench and python/
                                        instead of the release (local work on a
                                        bump; never for published numbers)
    NS3_TOOLS_CACHE=<dir>               where to unpack (default <repo>/.ns3-tools)

Standard library only, so it runs in the ns-3 images and on a bare runner.
"""

import hashlib
import importlib
import os
import runpy
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOCK = os.path.join(ROOT, "ns3-tools.lock")
KEYS = ("NS3_TOOLS_REPO", "NS3_TOOLS_TAG", "NS3_TOOLS_COMMIT", "NS3_TOOLS_VERSION",
        "ROUTING_BENCH_SHA256", "NS3_TOOL_WHEEL_SHA256")


def lock():
    """The lock file as a dict; every key must be present."""
    pin = {}
    with open(LOCK, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                key, _, value = line.partition("=")
                pin[key.strip()] = value.strip()
    missing = [k for k in KEYS if not pin.get(k)]
    if missing:
        sys.exit(f"ns3-tools.lock: missing {', '.join(missing)}")
    return pin


def assets(pin):
    """(asset name, sha256) for the two release assets this repo uses."""
    ver = pin["NS3_TOOLS_VERSION"]
    return [(f"routing-bench-{ver}.tar.gz", pin["ROUTING_BENCH_SHA256"]),
            (f"ns3_tool-{ver}-py3-none-any.whl", pin["NS3_TOOL_WHEEL_SHA256"])]


def _download(pin, name, sha, dest):
    url = (f"https://github.com/{pin['NS3_TOOLS_REPO']}/releases/download/"
           f"{pin['NS3_TOOLS_TAG']}/{name}")
    path = os.path.join(dest, name)
    with urllib.request.urlopen(url, timeout=120) as r, open(path, "wb") as f:
        shutil.copyfileobj(r, f)
    with open(path, "rb") as f:
        got = hashlib.sha256(f.read()).hexdigest()
    if got != sha:
        sys.exit(f"ns3-tools: {name} has sha256 {got}, ns3-tools.lock says {sha}")
    return path


def _safe_members(names, root):
    for n in names:
        target = os.path.realpath(os.path.join(root, n))
        if not (target == root or target.startswith(root + os.sep)):
            sys.exit(f"ns3-tools: refusing archive member outside the tree: {n}")


def _unpack(tar_path, whl_path, dest):
    """routing-bench/ from the tarball, python/ from the wheel."""
    root = os.path.realpath(dest)
    with tarfile.open(tar_path) as t:
        members = t.getmembers()
        _safe_members([m.name for m in members], root)
        if any(m.issym() or m.islnk() or m.isdev() for m in members):
            sys.exit("ns3-tools: the routing-bench tarball holds links or devices")
        if hasattr(tarfile, "data_filter"):  # Python 3.12, and 3.8-3.11 security releases
            t.extractall(dest, members=members, filter="data")
        else:
            t.extractall(dest, members=members)  # noqa: S202 (members checked above)
    if not os.path.isfile(os.path.join(dest, "routing-bench", "CMakeLists.txt")):
        sys.exit("ns3-tools: the tarball has no routing-bench/CMakeLists.txt")
    py = os.path.join(dest, "python")
    with zipfile.ZipFile(whl_path) as z:
        _safe_members(z.namelist(), os.path.realpath(py))
        z.extractall(py)
    if not os.path.isfile(os.path.join(py, "ns3_tool", "__init__.py")):
        sys.exit("ns3-tools: the wheel has no ns3_tool package")


def fetch():
    """The directory holding routing-bench/ and python/, fetched if needed."""
    local = os.environ.get("NS3_TOOLS_DIR")
    if local:
        return None
    pin = lock()
    cache = os.environ.get("NS3_TOOLS_CACHE") or os.path.join(ROOT, ".ns3-tools")
    dest = os.path.join(cache, pin["NS3_TOOLS_TAG"])
    stamp = os.path.join(dest, ".complete")
    want = "".join(f"{sha}  {name}\n" for name, sha in assets(pin))
    if os.path.isfile(stamp):
        with open(stamp, encoding="utf-8") as f:
            if f.read() == want:
                return dest
    os.makedirs(cache, exist_ok=True)
    work = tempfile.mkdtemp(prefix=".fetch-", dir=cache)
    try:
        dl = os.path.join(work, "dl")
        out = os.path.join(work, "out")
        os.makedirs(dl)
        os.makedirs(out)
        tar_path, whl_path = (_download(pin, n, s, dl) for n, s in assets(pin))
        _unpack(tar_path, whl_path, out)
        with open(os.path.join(out, ".complete"), "w", encoding="utf-8") as f:
            f.write(want)
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(out, dest)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"ns3-tools: {pin['NS3_TOOLS_REPO']}@{pin['NS3_TOOLS_TAG']} "
          f"({pin['NS3_TOOLS_COMMIT'][:12]}) -> {dest}", file=sys.stderr)
    return dest


def path(part):
    """routing-bench or python: the directory of that part of the pin."""
    if part not in ("routing-bench", "python"):
        sys.exit(f"ns3-tools: unknown part '{part}' (routing-bench or python)")
    local = os.environ.get("NS3_TOOLS_DIR")
    if local:
        return os.path.join(local, "ns3", "routing-bench") if part == "routing-bench" \
            else os.path.join(local, "python")
    return os.path.join(fetch(), part)


def use():
    """Put the pinned ns3_tool package first on sys.path; return its directory."""
    py = path("python")
    if py not in sys.path:
        sys.path.insert(0, py)
    return py


def module(name):
    """Import ns3_tool.<name> from the pin."""
    use()
    return importlib.import_module(f"ns3_tool.{name}")


def verify():
    """The tag still names the locked commit, and the assets their sha256."""
    pin = lock()
    url = f"https://github.com/{pin['NS3_TOOLS_REPO']}.git"
    tag = pin["NS3_TOOLS_TAG"]
    out = subprocess.run(["git", "ls-remote", url, f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}"],
                         capture_output=True, text=True, check=True).stdout
    refs = dict(reversed(line.split("\t")) for line in out.splitlines() if "\t" in line)
    commit = refs.get(f"refs/tags/{tag}^{{}}") or refs.get(f"refs/tags/{tag}")
    if commit != pin["NS3_TOOLS_COMMIT"]:
        sys.exit(f"ns3-tools.lock: {tag} names {commit}, the lock says {pin['NS3_TOOLS_COMMIT']}")
    with tempfile.TemporaryDirectory() as d:
        for name, sha in assets(pin):
            _download(pin, name, sha, d)
    print(f"ok: {pin['NS3_TOOLS_REPO']} {tag} = {commit}; "
          + ", ".join(n for n, _ in assets(pin)) + " match ns3-tools.lock")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0 if argv else 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "fetch":
        print(fetch() or os.environ["NS3_TOOLS_DIR"])
    elif cmd == "path" and len(rest) == 1:
        print(path(rest[0]))
    elif cmd == "pin":
        pin = lock()
        print(f"{pin['NS3_TOOLS_REPO']} {pin['NS3_TOOLS_TAG']} {pin['NS3_TOOLS_COMMIT']}")
    elif cmd == "verify":
        verify()
    elif cmd == "run" and rest:
        use()
        sys.argv = [f"ns3_tool.{rest[0]}"] + rest[1:]
        runpy.run_module(f"ns3_tool.{rest[0]}", run_name="__main__", alter_sys=True)
    else:
        print(__doc__, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
