#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Copyright (C) 2026 Daniel Henrique Joppi
# Fetch the browser adapter and the learn site into web/ (#850).
#
# The game, its browser adapter (core/ compiled to WebAssembly), the site shell
# and their tests moved to swarm-routing/swarm-routing.github.io, under
# site/anthocnet/ (its ADR-0001). That repository builds the published site
# from this one at a pinned commit; this script does the reverse for this
# repository's own CI and for local work: it lays the site's adapter over this
# checkout as web/ (git-ignored), so a core/ or docs/ change here is still
# built, parity-checked (golden rule 9) and smoke-tested against the adapter
# that ships.
#
# Usage: tools/checks/fetch-site-web.sh [ref]   (default: main; env SITE_REF)
# Env:   SITE_REPO=owner/name (default swarm-routing/swarm-routing.github.io)
#        SITE_DIR=<checkout>  copy from this local checkout instead of fetching
set -euo pipefail
cd "$(dirname "$0")/../.."
ref=${1:-${SITE_REF:-main}}
repo=${SITE_REPO:-swarm-routing/swarm-routing.github.io}
if [ -n "${SITE_DIR:-}" ]; then
  src=$SITE_DIR/site/anthocnet
else
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' EXIT
  git -C "$tmp" init -q
  git -C "$tmp" fetch -q --depth 1 "https://github.com/$repo.git" "$ref"
  git -C "$tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD -- site/anthocnet
  src=$tmp/site/anthocnet
  echo "site adapter: $repo@$(git -C "$tmp" rev-parse FETCH_HEAD)"
fi
[ -f "$src/CMakeLists.txt" ] || { echo "no site/anthocnet in $src" >&2; exit 1; }
if git ls-files --error-unmatch web >/dev/null 2>&1; then
  echo "web/ is tracked here; refusing to overwrite it" >&2; exit 1
fi
rm -rf web
cp -r "$src" web
echo "web/ <- site/anthocnet ($(find web -type f | wc -l) files)"
