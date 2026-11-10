#!/usr/bin/env bash
# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
# Release bump gate (#789). release.yml's publish job pushes the bump commit
# to main with a token that may bypass main's ruleset, and that commit was
# made by the release job, which runs repository code. Before the push, this
# checks that the commit is what Commitizen writes and nothing else:
#   - BUMP is BASE (baseline release), or a commit whose only parent is BASE;
#   - it changes only VERSION, .cz.toml, CITATION.cff, core/CMakeLists.txt
#     (each changed line is the version line, or CITATION.cff's
#     date-released line, line for line) and CHANGELOG.md (lines added only;
#     Markdown, never run), all as text: a change git sees as binary fails.
#
# Usage: check-bump.sh BASE BUMP     (in a repository holding both commits)
#        check-bump.sh --self-test
set -euo pipefail

ver='[0-9A-Za-z.+-]+'
# file -> the only line shape it may change
declare -A allowed=(
    [VERSION]="^${ver}\$"
    [.cz.toml]="^version = \"${ver}\"\$"
    [CITATION.cff]="^(version: ${ver}|date-released: \"[0-9]{4}-[0-9]{2}-[0-9]{2}\")\$"
    [core/CMakeLists.txt]="^project\\(anthocnet_core VERSION ${ver} LANGUAGES CXX\\)\$"
)

check() {
    local base=$1 bump=$2 f re removed added
    base=$(git rev-parse --verify "${base}^{commit}")
    bump=$(git rev-parse --verify "${bump}^{commit}")
    if [ "$bump" = "$base" ]; then
        echo "ok: baseline, nothing to push but the tag"
        return 0
    fi
    if [ "$(git rev-list --parents -n1 "$bump")" != "$bump $base" ]; then
        echo "FAIL [bump]: $bump is not a commit whose only parent is $base"
        return 1
    fi
    while IFS= read -r f; do
        # git counts no lines in a binary diff (a NUL byte is enough), so a
        # binary change would slip past the line checks below: reject it.
        if [ "$(git diff --numstat "$base" "$bump" -- "$f" | cut -f1)" = "-" ]; then
            echo "FAIL [bump]: $f changes as binary"
            return 1
        fi
        if [ "$f" = CHANGELOG.md ]; then
            removed=$(git diff --numstat "$base" "$bump" -- CHANGELOG.md | cut -f2)
            [ "$removed" = 0 ] || { echo "FAIL [bump]: CHANGELOG.md removes lines"; return 1; }
            continue
        fi
        re=${allowed[$f]:-}
        [ -n "$re" ] || { echo "FAIL [bump]: changes $f, not a file Commitizen writes"; return 1; }
        # Only regular-file content changes, line for line (no mode or type change).
        [ "$(git ls-tree "$bump" -- "$f" | cut -d' ' -f1)" = 100644 ] \
            || { echo "FAIL [bump]: $f is not a regular file"; return 1; }
        IFS=$'\t' read -r added removed _ < <(git diff --numstat "$base" "$bump" -- "$f")
        [ "$removed" = "$added" ] || { echo "FAIL [bump]: $f adds or removes lines"; return 1; }
        # Every +/- line after the first hunk header is content (one file),
        # whatever it starts with; each must be the file's version line.
        if git diff -U0 "$base" "$bump" -- "$f" | sed -n '/^@@/,$p' | grep -E '^[-+]' | cut -c2- | grep -vqE "$re"; then
            echo "FAIL [bump]: $f changes a line that is not its version line"
            return 1
        fi
    done < <(git diff --name-only --no-renames "$base" "$bump")
    echo "ok: $bump is a version bump of $base"
}

self_test() {
    local d g base fails=0
    d=$(mktemp -d)
    trap 'rm -rf "$d"' RETURN
    g=(git -C "$d" -c user.name=t -c user.email=t@t)
    git init -q "$d"
    mkdir -p "$d/core"
    printf '2.6.0\n' > "$d/VERSION"
    printf '[tool.commitizen]\nversion = "2.6.0"\n' > "$d/.cz.toml"
    printf 'cff-version: 1.2.0\nversion: 2.6.0\ndate-released: "2026-10-01"\n' > "$d/CITATION.cff"
    printf 'cmake_minimum_required(VERSION 3.10)\nproject(anthocnet_core VERSION 2.6.0 LANGUAGES CXX)\n' > "$d/core/CMakeLists.txt"
    printf '## v2.6.0\n' > "$d/CHANGELOG.md"
    printf 'on: push\n' > "$d/ci.yml"
    "${g[@]}" add -A && "${g[@]}" commit -qm base
    base=$("${g[@]}" rev-parse HEAD)
    expect() {   # $1: name, $2: expected 0/1, $3: the commit to check
        local rc=0
        (cd "$d" && check "$base" "$3" >/dev/null) || rc=1
        if [ "$rc" = "$2" ]; then echo "  pass: $1"; else echo "  FAIL: $1 (exit $rc, want $2)"; fails=1; fi
    }
    bump() {   # $1: name, $2: expected 0/1, $3: extra edits
        "${g[@]}" reset -q --hard "$base"
        sed -i 's/2\.6\.0/2.6.1/' "$d/VERSION" "$d/.cz.toml" "$d/CITATION.cff" "$d/core/CMakeLists.txt"
        sed -i 's/2026-10-01/2026-11-10/' "$d/CITATION.cff"
        printf '## v2.6.1\n\n- fix\n%s\n' "$(cat "$d/CHANGELOG.md")" > "$d/CHANGELOG.md"
        (cd "$d" && eval "$3")
        "${g[@]}" add -A && "${g[@]}" commit -qm "bump: version 2.6.0 → 2.6.1"
        expect "$1" "$2" HEAD
    }
    bump "a Commitizen bump" 0 true
    bump "a workflow change" 1 'echo evil >> ci.yml'
    bump "a CMake command" 1 'echo "execute_process(COMMAND sh -c x)" >> core/CMakeLists.txt'
    bump "a CMake line rewritten" 1 'sed -i "s/^cmake_minimum.*/execute_process(COMMAND x)/" core/CMakeLists.txt'
    bump "a new file" 1 'echo x > new.sh'
    bump "a CHANGELOG deletion" 1 'printf "## v2.6.1\n" > CHANGELOG.md'
    bump "VERSION made a symlink" 1 'rm VERSION && ln -s ci.yml VERSION'
    bump "a NUL byte in core/CMakeLists.txt" 1 'printf "#\\000\\nexecute_process(COMMAND x)\\n" >> core/CMakeLists.txt'
    bump "a changed line starting with -" 1 'echo "--x" >> VERSION; sed -i "1d" CITATION.cff'
    bump "a NUL byte in CHANGELOG.md" 1 'printf "\\000" >> CHANGELOG.md'
    "${g[@]}" reset -q --hard "$base"
    expect "baseline" 0 "$base"
    # Two commits, and a merge whose first parent is base.
    "${g[@]}" commit -q --allow-empty -m one && "${g[@]}" commit -q --allow-empty -m two
    expect "two commits" 1 HEAD
    "${g[@]}" checkout -q -b side "$base" && "${g[@]}" commit -q --allow-empty -m side
    "${g[@]}" checkout -q --detach "$base"
    "${g[@]}" merge -q --no-ff --no-edit side
    expect "a merge commit" 1 HEAD
    [ "$fails" = 0 ] && echo "check-bump self-test: all passed"
    return "$fails"
}

case "${1:-}" in
    --self-test) self_test ;;
    *) [ $# -eq 2 ] || { echo "usage: $0 BASE BUMP | --self-test" >&2; exit 2; }; check "$1" "$2" ;;
esac
