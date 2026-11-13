#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
"""Two-way links between the game and the docs (#631), checked.

The game and the docs teach the same mechanisms. This keeps them pointing at
each other as pages and missions are added:

  * every world in web/site/js/worlds.js has a `read:` Field guide page;
  * every Academy mission in web/site/js/missions-data.js links at least one
    docs page;
  * every family results page has `try:` front matter naming a world;
  * every target exists: docs links resolve to a docs page, API links name a
    class declared in core/include, and `try:` worlds are real world ids;
  * the Nest (#635) has a chamber for every public class/struct in
    core/include, and names no class that does not exist;
  * every install-quest step's command (#636) appears in the install docs.

Stdlib only; reads the JS as text (the data files are plain object literals).

Usage:  python3 tools/checks/check-site-links.py            # the repo
        python3 tools/checks/check-site-links.py --warn-nest # Nest findings warn, the rest fail
        python3 tools/checks/check-site-links.py --self-test [--warn-nest]

web/ is not tracked here (#850): run tools/checks/fetch-site-web.sh first.
Without it the script says so in one line and exits 2 (#983).

The Nest lives in swarm-routing.github.io (#850) and follows this repository's
core only through that site's protocols.lock bump, so a strict Nest rule on
both sides would deadlock adding or removing a public core class. This
repository's CI passes --warn-nest; the site runs the rule strictly at bump
time (the site's ADR-0001 item 6).
"""
from __future__ import annotations

import os
import re
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FAMILY_PAGES = [
    "docs/benchmarks/grid.md",
    "docs/benchmarks/static-mesh.md",
    "docs/benchmarks/scenarios/vanet.md",
    "docs/benchmarks/scenarios/fanet.md",
    "docs/benchmarks/satellite/leo-walker.md",
]


SITE_FILES = [
    "web/site/js/worlds.js",
    "web/site/js/missions-data.js",
    "web/site/js/nest-data.js",
    "web/site/js/install-steps.js",
    "web/site/js/replay.js",
]


def missing_site(root: str = REPO) -> str | None:
    """One line for a checkout without the site's files, None when all are there.

    web/ is fetched, not tracked (#850), so a fresh checkout has none of them.
    """
    for rel in SITE_FILES:
        if not os.path.isfile(os.path.join(root, rel)):
            return (f"check-site-links: {rel} is missing: web/ is not tracked here (#850), "
                    "run tools/checks/fetch-site-web.sh first")
    return None


def read(rel: str) -> str:
    with open(os.path.join(REPO, rel), encoding="utf-8") as f:
        return f.read()


def docs_target_exists(href: str, exists=os.path.exists) -> bool:
    """'docs/x/y/' (a site URL) -> docs/x/y.md, docs/x/y/README.md or index.md."""
    path = href.split("#")[0]
    assert path.startswith("docs/"), href
    stem = path[len("docs/"):].rstrip("/")
    if not stem:
        return exists(os.path.join(REPO, "docs", "README.md"))
    cands = [f"docs/{stem}.md", f"docs/{stem}/README.md", f"docs/{stem}/index.md"]
    return any(exists(os.path.join(REPO, c)) for c in cands)


def api_classes() -> set[str]:
    names = set()
    inc = os.path.join(REPO, "core", "include", "anthocnet", "core")
    for f in os.listdir(inc):
        if f.endswith(".h"):
            with open(os.path.join(inc, f), encoding="utf-8") as h:
                names |= set(re.findall(r"^(?:class|struct)\s+(\w+)\b(?!;)", h.read(), re.MULTILINE))
    return names


def worlds(js: str) -> dict[str, dict]:
    out = {}
    for block in re.split(r"\n  \{\n", js)[1:]:
        m = re.search(r"id: '(\w+)'", block)
        if not m:
            continue
        rd = re.search(r"read: \{ href: GUIDE \+ '([^']+)'", block)
        out[m.group(1)] = {"read": ("docs/" + rd.group(1)) if rd else None}
    return out


def missions(js: str) -> list[dict]:
    # resolve the simple `const X = 'lit';` and `const X = API + 'lit';` forms
    consts: dict[str, str] = {}
    for name, base, lit in re.findall(r"^const (\w+) = (?:(\w+) \+ )?'([^']*)';", js, re.MULTILINE):
        consts[name] = (consts.get(base, "") if base else "") + lit
    out = []
    for block in re.split(r"\n  \{\n", js)[1:]:
        m = re.search(r"id: '([\w-]+)', n: \d+, group: '([^']+)'", block)
        if not m:
            continue
        links = []
        lm = re.search(r"links: \[(.*?)\],\n", block, re.DOTALL)
        if lm:
            for base, lit in re.findall(r"(\w+)(?: \+ '([^']*)')?\]", lm.group(1)):
                if base in consts:
                    links.append(consts[base] + (lit or ""))
        out.append({"id": m.group(1), "group": m.group(2), "links": links})
    return out


def front_matter_try(md: str) -> list[str]:
    m = re.match(r"---\n(.*?)\n---\n", md, re.DOTALL)
    if not m:
        return []
    return re.findall(r"^\s*- world: (\w+)", m.group(1), re.MULTILINE)


def front_matter_replays(md: str) -> list[str]:
    m = re.match(r"---\n(.*?)\n---\n", md, re.DOTALL)
    return re.findall(r"^\s*- replay: (\w+)", m.group(1), re.MULTILINE) if m else []


def replay_cells(js: str) -> list[str]:
    """The recorded ns-3 runs the replay page offers (CELLS in replay.js, #641)."""
    block = js[js.index("const CELLS = [") : js.index("];", js.index("const CELLS = ["))]
    return re.findall(r"id: '(\w+)'", block)


def nest_classes(js: str) -> list[str]:
    return [c for block in re.findall(r"classes: \[([^\]]*)\]", js) for c in re.findall(r"'(\w+)'", block)]


def quest_cmds(js: str) -> list[str]:
    return re.findall(r"cmd: '([^']+)'", js)


INSTALL_DOCS = ("docs/places/workshop.md", "ns3/README.md")


def check(worlds_js: str, missions_js: str, pages: dict[str, str], exists=os.path.exists,
          nest_js: str | None = None, steps_js: str | None = None, install_text: str | None = None,
          replay_js: str | None = None) -> list[str]:
    errs = []
    w = worlds(worlds_js)
    if not w:
        errs.append("worlds.js: no worlds parsed")
    for wid, d in w.items():
        if not d["read"]:
            errs.append(f"world {wid}: no read: Field guide page")
        elif not docs_target_exists(d["read"], exists):
            errs.append(f"world {wid}: read: {d['read']} has no docs page")
    classes = api_classes()
    ms = missions(missions_js)
    if not ms:
        errs.append("missions-data.js: no missions parsed")
    for m in ms:
        docs = [h for h in m["links"] if h.startswith("docs/")]
        if m["group"] == "Academy" and not docs:
            errs.append(f"mission {m['id']}: no docs link")
        for h in m["links"]:
            if h.startswith("docs/") and not docs_target_exists(h, exists):
                errs.append(f"mission {m['id']}: {h} has no docs page")
            am = re.match(r"api/class(?:anthocnet_1_1core_1_1)?(\w+)\.html$", h)
            if h.startswith("api/") and (not am or am.group(1) not in classes):
                errs.append(f"mission {m['id']}: {h} names no class in core/include")
    if nest_js is not None:
        nest = nest_classes(nest_js)
        for c in sorted(classes - set(nest)):
            errs.append(f"Nest: core class {c} has no chamber (web/site/js/nest-data.js)")
        for c in sorted(set(nest) - classes):
            errs.append(f"Nest: chamber names {c}, which core/include does not declare")
        for c in sorted({c for c in nest if nest.count(c) > 1}):
            errs.append(f"Nest: {c} is in more than one chamber")
    if steps_js is not None:
        for cmd in quest_cmds(steps_js):
            if cmd not in (install_text or ""):
                errs.append(f"install quest: '{cmd}' is not in the install docs ({', '.join(INSTALL_DOCS)})")
    for rel, md in pages.items():
        tries = front_matter_try(md)
        if rel in FAMILY_PAGES and not tries:
            errs.append(f"{rel}: no try: front matter (a family results page links its world)")
        for t in tries:
            if t not in w:
                errs.append(f"{rel}: try: world '{t}' is not a world id ({', '.join(sorted(w))})")
        if replay_js is not None:
            cells = replay_cells(replay_js)
            reps = front_matter_replays(md)
            if rel in FAMILY_PAGES and not reps:
                errs.append(f"{rel}: no `- replay:` try entry (each family page links its ns-3 run, #641)")
            for r in reps:
                if r not in cells:
                    errs.append(f"{rel}: replay '{r}' is not a recorded run ({', '.join(cells)})")
    return errs


def repo_inputs():
    pages = {}
    for d, _, names in os.walk(os.path.join(REPO, "docs")):
        for n in names:
            if n.endswith(".md"):
                rel = os.path.relpath(os.path.join(d, n), REPO).replace(os.sep, "/")
                pages[rel] = read(rel)
    return read("web/site/js/worlds.js"), read("web/site/js/missions-data.js"), pages


def extra_inputs() -> dict:
    return {"nest_js": read("web/site/js/nest-data.js"), "steps_js": read("web/site/js/install-steps.js"),
            "install_text": "\n".join(read(p) for p in INSTALL_DOCS),
            "replay_js": read("web/site/js/replay.js")}


def self_test() -> int:
    """Every rule must fire on a broken input and stay quiet on the repo."""
    wjs, mjs, pages = repo_inputs()
    ex = extra_inputs()
    no_docs = mjs.replace(
        "links: [['doc.howItWorks', DOC + 'ant-colony-routing/'], ['AntRouterLogic::onMaintenanceTick', ARL]]",
        "links: [['AntRouterLogic::onMaintenanceTick', ARL]]", 1)
    no_try = dict(pages)
    no_try["docs/benchmarks/static-mesh.md"] = re.sub(r"^---\n.*?\n---\n", "", no_try["docs/benchmarks/static-mesh.md"], flags=re.DOTALL)
    no_replay = dict(pages)
    no_replay["docs/benchmarks/static-mesh.md"] = re.sub(r"  - replay: \w+\n    title: .*\n", "",
                                                         no_replay["docs/benchmarks/static-mesh.md"])
    bad_world = dict(pages)
    bad_world["docs/ant-types.md"] = bad_world["docs/ant-types.md"].replace("- world: line", "- world: atlantis", 1)
    # The Nest probes run on a fixture Nest (one chamber, every core class once),
    # not the fetched one, so drift in the site's Nest cannot turn a probe into a
    # no-op or crash it; they are compared against that fixture's own baseline.
    core = sorted(api_classes())
    nest_fixture = "classes: [" + ", ".join(f"'{c}'" for c in core) + "]"
    ghost = "NoSuchCoreClass"
    while ghost in core:
        ghost += "_"
    cases = [
        ("the repo as committed", (wjs, mjs, pages), {}, False),
        ("a dangling world read:", (wjs.replace("GUIDE + 'ant-types/'", "GUIDE + 'no-such-page/'", 1), mjs, pages), {}, True),
        ("a world without read:", (re.sub(r"    read: \{[^}]*\},\n", "", wjs, count=1), mjs, pages), {}, True),
        ("an Academy mission without a docs link", (wjs, no_docs, pages), {}, True),
        ("a family page without try:", (wjs, mjs, no_try), {}, True),
        ("a try: world that does not exist", (wjs, mjs, bad_world), {}, True),
        ("a family page without its ns-3 replay", (wjs, mjs, no_replay), {}, True),
        ("a replay id the page does not offer", (wjs, mjs, pages),
         {"replay_js": ex["replay_js"].replace("{ id: 'mesh'", "{ id: 'meshy'", 1)}, True),
        ("an API link to a class that does not exist",
         (wjs, mjs.replace("PheromoneEngine.html", "PheromoneEngin.html", 1), pages), {}, True),
        ("a core class missing from the Nest", (wjs, mjs, pages), {"nest_js": nest_fixture.replace(f"'{core[0]}'", "", 1)}, True),
        ("a Nest chamber naming a class that does not exist", (wjs, mjs, pages),
         {"nest_js": nest_fixture.replace(f"'{core[0]}'", f"'{core[0]}', '{ghost}'", 1)}, True),
        ("an install-quest command not in the docs", (wjs, mjs, pages),
         {"steps_js": ex["steps_js"].replace("cmd: './test.py -s anthocnet'", "cmd: './test.py -s nothing'", 1)}, True),
    ]
    # Each broken input must add a finding of its own on top of the repo as
    # committed; with --warn-nest the committed repo may carry Nest findings
    # (a core class not yet in the site's Nest), which must not fail or mask a case.
    warn_nest = "--warn-nest" in sys.argv
    base = set(check(*cases[0][1], **ex))
    nest_base = set(check(*cases[0][1], **{**ex, "nest_js": nest_fixture}))
    bad = 0
    for name, args, override, should_fire in cases:
        got = set(check(*args, **{**ex, **override}))
        ref = nest_base if "nest_js" in override else base
        if should_fire:
            ok, n = bool(got - ref), len(got - ref)
        else:
            n = len([e for e in got if not (warn_nest and e.startswith("Nest: "))])
            ok = n == 0
        print(f"{'ok  ' if ok else 'FAIL'} {name}: {n} finding(s)")
        bad += not ok
    # A checkout without web/ gets one line naming the missing file and the
    # fetch script (#983); this checkout, with web/ fetched, gets none.
    with tempfile.TemporaryDirectory() as empty:
        msg = missing_site(empty)
    ok = (msg is not None and "\n" not in msg and SITE_FILES[0] in msg
          and "tools/checks/fetch-site-web.sh" in msg and missing_site() is None)
    print(f"{'ok  ' if ok else 'FAIL'} a checkout without web/: {msg}")
    bad += not ok
    print(f"{len(cases) + 1 - bad}/{len(cases) + 1} cases passed")
    return 1 if bad else 0


def main() -> int:
    msg = missing_site()
    if msg:
        print(msg, file=sys.stderr)
        return 2
    if "--self-test" in sys.argv:
        return self_test()
    errs = check(*repo_inputs(), **extra_inputs())
    if "--warn-nest" in sys.argv:
        for e in errs:
            if e.startswith("Nest: "):
                print(f"WARN {e} (warn-only here; fix the Nest in the site's protocols.lock bump)")
        errs = [e for e in errs if not e.startswith("Nest: ")]
    w = worlds(read("web/site/js/worlds.js"))
    ms = missions(read("web/site/js/missions-data.js"))
    for e in errs:
        print(f"FAIL {e}")
    if errs:
        return 1
    nest = "the Nest is warn-only here" if "--warn-nest" in sys.argv else "the Nest covers every core class"
    print(f"ok: {len(w)} worlds and {len(ms)} missions link the docs; the {len(FAMILY_PAGES)} family "
          f"pages link the game; every target exists; {nest}; "
          "every install-quest command is in the docs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
