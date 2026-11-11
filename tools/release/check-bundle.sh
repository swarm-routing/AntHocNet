#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Release-package gate (#604). Assembles the bundle exactly as release.yml does
# and fails if it ships something it must not, or misses something it must:
#   - agent-only material: .claude/, AGENTS.md, CLAUDE.md
#   - CI configuration: .github/, .codecov.yml, sonar-project.properties, .coderabbit.yaml (#868)
#   - caches and build output: __pycache__, *.pyc, build/ dirs
#   - a shipped script that reaches into .claude/ (it would break in the package)
#   - the tools the shipped harness imports (tools/bench/*.py), and the
#     ns3-tools pin the harness and the readers come from (ns3-tools.lock,
#     tools/ns3_tools.py; swarm-routing/ns3-tools#29)
#   - the licence texts (LICENSE, LICENSE-MIT, LICENSING.md, core/LICENSE; ADR-0028)
# It also checks that `git archive` (GitHub's "Source code" downloads, and what
# Zenodo archives) honours the same exclusions via .gitattributes export-ignore.
set -uo pipefail
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
bundle=$(tools/release/assemble-bundle.sh 0.0.0-check "$tmp") || { echo "FAIL: assemble-bundle.sh"; exit 1; }
fail=0
bad() { echo "FAIL: $*"; fail=1; }
cd "$bundle"
for p in .claude AGENTS.md CLAUDE.md .github .codecov.yml sonar-project.properties .coderabbit.yaml; do
  [ -e "$p" ] && bad "bundle ships $p"
done
junk=$(find . \( -name __pycache__ -o -name '*.pyc' -o -type d -name build \) -print | head -5)
[ -n "$junk" ] && bad "bundle ships caches/build output: $junk"
# A dependency, not a mention: a path to a script under .claude/skills/, or the
# path built from parts (".claude", "skills").
refs=$(grep -rIlE '\.claude/skills/[^/"'"'"' ]+/[^/"'"'"' ]+\.(py|sh)|"\.claude", *"skills"' \
       --include='*.py' --include='*.sh' --include='*.cc' --include='*.h' \
       --include='CMakeLists.txt' --include='Makefile' --exclude-dir=release . 2>/dev/null | head -5)
[ -n "$refs" ] && bad "shipped code references .claude/ (breaks in the package): $refs"
for f in tools/bench/scenario_check.py tools/ns3_tools.py ns3-tools.lock \
         ns3/tools/anchors.yml core/CMakeLists.txt Makefile \
         LICENSE LICENSE-MIT LICENSING.md core/LICENSE; do
  [ -e "$f" ] || bad "bundle is missing $f"
done
# The readers and the harness are ns3-tools' now: the bundle must not carry
# copies that could drift from the pin.
for f in tools/bench/bench_parse.py tools/bench/stats_util.py tools/bench/pool_runs.py ns3/routing-bench; do
  [ -e "$f" ] && bad "bundle ships $f, which ns3-tools provides (ns3-tools.lock)"
done
# The lock names a full commit, so the bundle records what it was built with.
grep -Eq '^NS3_TOOLS_COMMIT=[0-9a-f]{40}$' ns3-tools.lock || bad "ns3-tools.lock names no 40-hex commit"
cd - >/dev/null
# The standalone package (#770; routing-bench's is ns3-tools' release asset
# now): it holds exactly its own contrib directory,
# with its README, the licence and an install note. check-packages.sh builds
# them on ns-3 in CI; this is the fast static half.
mod=$(tools/release/module-tarball.sh 0.0.0-check "$tmp/mod" | tail -n 1) || bad "module-tarball.sh failed"
p=adaptive
list=$(tar -tzf "$tmp/mod/$p-0.0.0-check.tar.gz")
top=$(printf '%s\n' "$list" | cut -d/ -f2 | sort -u | tr -d '\n')
[ "$top" = "$p" ] || bad "$p package holds ($top), not only $p/"
for f in CMakeLists.txt README.md LICENSE INSTALL.txt; do
  printf '%s\n' "$list" | grep -qx "\./$p/$f" || bad "$p package is missing $f"
done
[ -s "$mod" ] || bad "module tarball not written"
# The index tree with the working tree's .gitattributes: in CI that is HEAD;
# locally it includes staged changes.
arch=$(git archive --worktree-attributes --format=tar "$(git write-tree)" | tar -t)
for p in .claude/ AGENTS.md CLAUDE.md .github/ .codecov.yml sonar-project.properties .coderabbit.yaml; do
  # A here-string, not a pipe: with pipefail, grep -q exiting on an early match
  # can SIGPIPE the writer and turn a hit into a miss.
  grep -q "^$p" <<<"$arch" && bad "git archive ships $p (add export-ignore)"
done
if [ "$fail" = 0 ]; then
  echo "ok: bundle $(find "$bundle" -type f | wc -l) files, no agent or CI config files, no caches, tools/bench present; git archive clean"
fi
exit "$fail"
