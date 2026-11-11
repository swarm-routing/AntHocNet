#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Assemble the release install bundle (#604): the one definition of what ships.
# release.yml calls it to build the zip; check-bundle.sh calls it to verify the
# contents on every PR that could change them, so the two can never drift.
#
# Usage: tools/release/assemble-bundle.sh VERSION OUTDIR
#   -> OUTDIR/anthocnet-VERSION/   (zip it yourself; release.yml does)
#
# What ships: the code (core/, ns3/, tools/), the docs, and the top-level files a
# user or contributor reads. What does not: agent-only material (.claude/ skills,
# AGENTS.md, CLAUDE.md), CI config (.github/, and the service configs at the root:
# .codecov.yml, sonar-project.properties, .coderabbit.yaml; #868), build output.
# The top-level list below is an allow-list, so a root file ships only if named.
set -euo pipefail
VER=${1:?usage: assemble-bundle.sh VERSION OUTDIR}
OUT=${2:?usage: assemble-bundle.sh VERSION OUTDIR}
cd "$(dirname "$0")/../.."
dest="$OUT/anthocnet-$VER"
rm -rf "$dest"
mkdir -p "$dest"
# ns3-tools.lock names the swarm-routing/ns3-tools release (tag, commit,
# sha256) the harness and the readers come from (ns3-tools#29); `make
# install-ns3` fetches it through tools/ns3_tools.py.
for f in Makefile README.md LICENSE LICENSE-MIT LICENSING.md CHANGELOG.md CITATION.cff VERSION \
         CONTRIBUTING.md CODE_OF_CONDUCT.md SECURITY.md SUPPORT.md CONTEXT.md ns3-tools.lock; do
  [ -e "$f" ] && cp "$f" "$dest/"
done
# Tracked files only: never ship build output, caches or local scratch.
git ls-files -z core ns3 tools docs results \
  | xargs -0 -I{} cp --parents {} "$dest/"
echo "$dest"
