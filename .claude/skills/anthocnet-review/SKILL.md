---
name: anthocnet-review
description: Run AntHocNet's AGENTS.md golden-rule checks on the current diff (no NS or emscripten headers in core/, wire-format changes bump kWireVersion, core logic changes carry a core test, new mechanisms ship off, web/ changes run parity, nothing shipped depends on .claude/, new docs pages are in the nav). Use when reviewing or preparing any change to core/ or the adapters, to catch the invariants that are cheap to miss before pushing.
---

# anthocnet-review

`check_invariants.sh` (in this skill dir) runs the mechanical half of a protocol
review in one call, so you spend tokens on judgement, not on rediscovering the
invariants with a dozen greps.

```bash
bash .claude/skills/anthocnet-review/check_invariants.sh [BASE]   # BASE defaults to origin/main
bash .claude/skills/anthocnet-review/check_invariants.sh --self-test   # the wire-diff filter's own cases
```

It reports PASS/WARN/FAIL for the rules below. Two of them **run CI's own
gates** rather than a copy of them, so the skill sees what CI will see (#794);
the rest are the skill's heuristics over the diff.

From CI gates:

- **Rule 1 (FAIL):** `python3 docs/tools/check-headers.py .`, as `lint.yml`
  runs it on every tracked file: a `core/` angle include must be a C++
  standard-library header and a quote include must resolve under `core/` (so no
  ns-3, NS-2, emscripten or `web/` header, `..` paths included), and every
  source file carries its SPDX line (ADR-0028). Hard stop.
- **Rule 10 (FAIL):** `tools/release/check-bundle.sh`, as `lint.yml` and
  `release.yml` run it: nothing shipped references a `.claude/skills/…` script
  (move the script to `tools/`, #604), and the package carries no agent or CI
  files. The rule's regex lives only there. Takes a few seconds (it assembles
  the bundle).

The skill's own heuristics:

- **Rule 4 (WARN):** `ant_message.h` / `ant_message_codec` changed in code
  lines without a `kWireVersion` bump in `core/`; reminds you to also update the
  ns-3 header (`ns3/anthocnet/model/anthocnet-packet`), `test_codec.cpp`, and
  `docs/wire-format.md`. Only code counts: each added or removed line is cut to
  its code (comments and `* `-style doc lines removed), and a removed line whose
  code reappears among the added ones cancels out. So a comment, SPDX or
  trailing-comment edit stays quiet (#783), while code after a leading `/* */`
  or a `*p = …` write still warns. Block-comment lines that do not start with
  `*` are read as code (a false WARN, never a missed one).
- **Rule 7 (WARN):** `core/` logic changed but no `core/tests/` file changed.
- **Docs (WARN):** a core/adapter change with no `docs/` touch — confirm no
  ADR needs updating.
- **Rule 8 (WARN):** a new `bool enable… = true` in the diff — new mechanisms
  ship default-off and are A/B'd in every family first (ADR-0019/0020).
- **Rule 9 (WARN):** the browser adapter lives in swarm-routing.github.io
  (`site/anthocnet/`, #850); after a `core/` change run
  `tools/checks/fetch-site-web.sh && web/test/parity.sh` (CI's `web-parity` does).
- **Docs nav (WARN):** a new `docs/*.md` page that `mkdocs.yml` does not list —
  it would be unreachable on the site.

Exit code is non-zero only on a FAIL. WARN means "look and confirm", not "blocked".

This is a checklist accelerator, **not** a replacement for reading the diff or
for `/code-review`. It does not judge correctness — pair it with the actual
review. Always still run `make test` before committing a core change.
