#!/usr/bin/env bash
# Mechanical AGENTS.md golden-rule checks on a diff, in one call — so a review
# doesn't spend a dozen greps/reads discovering them by hand. Reports PASS/WARN/
# FAIL per rule; exits non-zero if any FAIL. Heuristic, not a substitute for
# reading the diff — it catches the invariants that are cheap to check and easy
# to forget. Rules 1 and 10 run the CI gates themselves (docs/tools/check-headers.py,
# tools/release/check-bundle.sh), so the skill reports what CI will (#794).
#
# Usage: check_invariants.sh [BASE]      (BASE defaults to origin/main)
#        check_invariants.sh --self-test (the wire-diff filter's own cases)
set -u

# The code a unified diff adds or removes. Each +/- line is reduced to its code
# (comments, and doc lines that start with "* ", "*/" or "/*", are cut away),
# and a removed line whose code reappears among the added lines cancels it.
# What is left is printed as +code / -code. Rule 4 counts these, so an edit to
# a comment (an SPDX line, #783), even a trailing one, is not a wire-format
# change, while code after a leading /* */ still is. Block-comment lines that
# do not start with "*" are not recognised and count as code (a false WARN).
code_of() {
  sed -E 's@/\*([^*]|\*+[^*/])*\*+/@@g; s@//.*$@@; s@^[[:space:]]*(\*[[:space:]].*|\*|\*/.*|/\*.*)$@@; s@^[[:space:]]+@@; s@[[:space:]]+$@@' \
    | grep -v '^$' | sort
}
code_changes() {
  local d rem add
  d=$(cat)
  rem=$(printf '%s\n' "$d" | grep -E '^-' | grep -vE '^--- ' | cut -c2- | code_of)
  add=$(printf '%s\n' "$d" | grep -E '^\+' | grep -vE '^\+\+\+ ' | cut -c2- | code_of)
  comm -3 <(printf '%s\n' "$rem" | grep -v '^$') <(printf '%s\n' "$add" | grep -v '^$') \
    | sed -E 's/^\t/+/; t; s/^/-/'
}

if [ "${1:-}" = "--self-test" ]; then
  ok=1
  h=$'--- a/core/include/anthocnet/core/ant_message.h\n+++ b/core/include/anthocnet/core/ant_message.h\n'
  quiet() { [ -z "$(printf '%s\n' "$h$2" | code_changes)" ] || { echo "FAIL self-test: $1 counted as code"; ok=0; }; }
  loud() { [ -n "$(printf '%s\n' "$h$2" | code_changes)" ] || { echo "FAIL self-test: $1 not counted"; ok=0; }; }
  quiet "comment-only diff" $'-// SPDX-License-Identifier: GPL-2.0-only\n+// SPDX-License-Identifier: MIT OR GPL-2.0-only\n+\n- * old doc line\n+ * new doc line\n+/* block */\n+ */'
  quiet "trailing-comment edit" $'-    int x;  // old\n+    int x;  // new'
  loud "field change" $'+    uint32_t hopLimit = 0;  // new field'
  loud "code after a leading block comment" $'+/* units: hops */ uint32_t hopLimit = 0;'
  loud "pointer write" $'-    *p = 1;\n+    *p = 2;'
  [ "$ok" = 1 ] && { echo "ok: self-test"; exit 0; }
  exit 1
fi

cd "$(git rev-parse --show-toplevel)" || exit 2
BASE="${1:-origin/main}"
MB=$(git merge-base "$BASE" HEAD 2>/dev/null || echo "$BASE")
changed=$(git diff --name-only "$MB"...HEAD; git diff --name-only; git diff --cached --name-only)
changed=$(printf '%s\n' "$changed" | sort -u | grep -v '^$')
diff_txt=$(git diff "$MB"...HEAD; git diff; git diff --cached)
fail=0; warn=0
say() { printf '%-5s %s\n' "$1" "$2"; }

# Rule 1 — core/ includes only the C++ standard library and core/ itself (no
# ns-3, NS-2, emscripten or web/ header), and licence headers are in place.
# This is CI's gate (lint.yml), run on every tracked file, not a copy of it.
if out=$(python3 docs/tools/check-headers.py . 2>&1); then
  say PASS "core/ includes and licence headers (docs/tools/check-headers.py)"
else
  say FAIL "docs/tools/check-headers.py (CI gate):"; printf '%s\n' "$out" | sed 's/^/        /'; fail=1
fi

# Rule 4 — a wire-format change must bump kWireVersion (+ both adapters, test_codec, doc).
wire_touched=$(printf '%s\n' "$changed" | grep -E 'ant_message(\.h|_codec)' || true)
if [ -n "$wire_touched" ]; then
  mapfile -t wf <<<"$wire_touched"
  wire_code=$( { git diff "$MB"...HEAD -- "${wf[@]}"; git diff -- "${wf[@]}"; git diff --cached -- "${wf[@]}"; } | code_changes)
  if [ -z "$wire_code" ]; then
    say PASS "wire files changed in comments only"
  elif { git diff "$MB"...HEAD -- core/; git diff -- core/; git diff --cached -- core/; } | code_changes | grep -qE '^\+.*kWireVersion'; then
    say PASS "wire files changed and kWireVersion is bumped"
  else
    say WARN "wire files changed but no kWireVersion bump in the diff:"; echo "$wire_touched" | sed 's/^/        /'
    say WARN "  also update the ns-3 header (ns3/anthocnet/model/anthocnet-packet), test_codec.cpp, docs/wire-format.md"; warn=1
  fi
fi

# Rule 4 — docs/wire-format.md names the version the codec emits (CI gate, #981).
if out=$(python3 tools/checks/check-wire-doc.py 2>&1); then
  say PASS "docs/wire-format.md is at the codec's kWireVersion (tools/checks/check-wire-doc.py)"
else
  say FAIL "tools/checks/check-wire-doc.py (CI gate):"; printf '%s\n' "$out" | sed 's/^/        /'; fail=1
fi

# Rule 7 — core logic changes are covered by a core unit test.
core_src=$(printf '%s\n' "$changed" | grep -E '^core/(src|include)/' | grep -v '/tests/' || true)
core_test=$(printf '%s\n' "$changed" | grep -E '^core/tests/' || true)
if [ -n "$core_src" ] && [ -z "$core_test" ]; then
  say WARN "core/ logic changed but no core/tests/ file changed (golden rule #7)"; warn=1
elif [ -n "$core_src" ]; then
  say PASS "core/ change has an accompanying core/tests/ change"
fi

# Rule 8 — a new mechanism ships default-off (ADR-0019/0020).
on=$(printf '%s\n' "$diff_txt" | grep -E '^\+\s*bool\s+enable[A-Za-z0-9_]*\s*=\s*true' || true)
if [ -n "$on" ]; then
  say WARN "new default-ON switch in the diff — new mechanisms ship off (golden rule #8):"; echo "$on" | sed 's/^/        /'; warn=1
fi

# Rule 9 — browser adapter changes must keep native/WASM parity.
if printf '%s\n' "$changed" | grep -qE '^web/(src|include)/'; then
  say WARN "web/ adapter changed — run web/test/parity.sh and web/test/smoke.mjs (learn-site skill)"; warn=1
fi

# Rule 10 — nothing shipped depends on .claude/ (ADR-0014 amendment, #604).
# CI's release-package gate holds the rule; run it rather than a second regex.
if out=$(tools/release/check-bundle.sh 2>&1); then
  say PASS "release package clean (tools/release/check-bundle.sh)"
else
  say FAIL "tools/release/check-bundle.sh (CI gate):"; printf '%s\n' "$out" | grep -v '^ns3-tools:' | sed 's/^/        /'; fail=1
fi

# New docs page must be in the mkdocs nav (else it is unreachable on the site).
for f in $(printf '%s\n' "$changed" | grep -E '^docs/.*\.md$'); do
  [ -f "$f" ] || continue
  git cat-file -e "$MB:$f" 2>/dev/null && continue
  grep -qF "${f#docs/}" mkdocs.yml || { say WARN "new page $f is not in the mkdocs.yml nav"; warn=1; }
done

# Behavioural change to a documented decision should touch a doc/ADR.
if [ -n "$core_src$wire_touched" ] && ! printf '%s\n' "$changed" | grep -qE '^docs/'; then
  say WARN "core/adapter change with no docs/ update — confirm no ADR/improvement doc needs it"; warn=1
fi

echo "----"
if [ "$fail" = 1 ]; then say FAIL "invariant violation — must fix before merge"; exit 1
elif [ "$warn" = 1 ]; then say WARN "review the warnings above"; exit 0
else say PASS "mechanical invariants clean"; exit 0; fi
