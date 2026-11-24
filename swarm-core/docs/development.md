# Developer guide

swarm-adapter-ns3 is the shared ns-3 contrib module `swarm-core` that every
swarm-routing protocol builds on: the ns-3 glue in `model/`, `helper/` and
`test/`, which builds inside an ns-3 tree, on the plain C++23 primitives of
[swarm-routing/swarm-core](https://github.com/swarm-routing/swarm-core) (its
`core/`, which builds and tests without any simulator, lives there since
#231). This page shows how to set up, build, test and lint the module, and
what CI checks on a pull request. The rules a change must keep are in [AGENTS.md](../AGENTS.md)
("Golden rules"); how issues and pull requests work is in the org's
[CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md).

## Prerequisites

| Tool | Version | Needed for |
| --- | --- | --- |
| Linux or macOS, `git`, `make` | any recent | everything |
| Docker | any recent; about 4 GB free for the debug image, 6 GB more for the sanitized one | the ns-3 module, `make test` |
| Python | 3.11 or newer | the checks under `tools/`, licence headers, org-sync |
| clang-format | 18 (`clang-format-18`) | formatting |

You do not install ns-3 yourself: the module builds inside the org's
prebuilt image `ghcr.io/swarm-routing/ns3-dev` (ns-3 3.49 today, GCC 13.3,
CMake 3.28, Python 3.12), which `make` pulls on first use.

## Get the code

```bash
git clone https://github.com/swarm-routing/swarm-adapter-ns3.git
cd swarm-adapter-ns3
```

There are no submodules. To propose a change from outside the org, fork the
repository first and clone your fork (CONTRIBUTING "Outside contributions").

## Dependencies

The module depends on ns-3 and on swarm-routing/swarm-core's primitives.
ns-3 comes with the `ns3-dev` image; nothing is vendored. swarm-core comes
from the release [`swarm-core.lock`](../swarm-core.lock) names: the ns-3
configure step downloads its tarball and checks it against the lock's SHA256
(CMake `FetchContent`), so the first build in a tree needs network access.
The lock is written from the release by `swarm-routing/.github`
`tools/checks/release_lock.py`, never by hand, and the `core` check verifies
it (`python3 -I ../.github/tools/checks/release_lock.py verify swarm-core.lock`
with that repository checked out beside this one); swarm-routing/.github
CONTRIBUTING "Dependencies" has the convention. To build against an
unreleased swarm-core, mount its checkout and pass its `core/` to the
configure step, `-DSWARM_CORE_DIR=<checkout>/core` (in the image:
`./ns3 configure ... -- -DSWARM_CORE_DIR=/src/swarm-core-checkout/core`).

## Build

The ns-3 module, inside the `ns3-dev` image (your checkout is mounted
read-only; only this module compiles, against ns-3's prebuilt tree):

```bash
make build
# the same command written out:
docker run --rm -v "$PWD:/src/swarm-core:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib swarm-core=/src/swarm-core
```

## Test

| What | Command | CI |
| --- | --- | --- |
| `swarm-core.lock` names the release, verified | `python3 -I ../.github/tools/checks/release_lock.py verify swarm-core.lock` | `core` (core.yml) |
| the ns-3 suite `swarm-core`, strict warnings as errors | `make test` | `ci / ns-latest debug` |
| the same on the sanitized image | `make test NS3_IMAGE=ghcr.io/swarm-routing/ns3-dev:latest-sanitized` | `ci / ns-latest sanitized` |
| the locked swarm-core release's C++ standard matches the image's ns-3 | `python3 -I tools/checks/check-cxx-standard.py --ns3 /opt/ns-3 <unpacked release>/core/CMakeLists.txt` (inside the image: `make shell`) | `cxx-standard` |
| the scripts' own tests | `for t in tools/checks/tests/test_*.py; do python3 -I "$t"; done` | `python-quality` |

`make test` prints `1 of 1 tests passed` when the suite passes; the first run
pulls the image, later runs take under a minute.

CI also runs, without a local equivalent you need: the determinism gate
(`swarm-core-seed --seed=1` twice must print the same bytes and `--seed=2`
must differ), and coverage for Codecov and SonarQube Cloud (report-only).
The primitives' unit tests, sanitizer runs and microbenchmarks run in
swarm-routing/swarm-core.

## Run

The module ships one small program, `swarm-core-seed`, the determinism
gate's: it prints seeded random draws and a roulette sequence.

```bash
docker run --rm -v "$PWD:/src/swarm-core:ro" ghcr.io/swarm-routing/ns3-dev:latest-debug \
    ns3-build-contrib --run "swarm-core-seed --seed=1" swarm-core=/src/swarm-core
```

A protocol that uses swarm-core runs through its own repository's examples
and through the comparison harness in swarm-routing/ns3-tools.

## Docker and dev containers

`make shell` opens an interactive shell in the `ns3-dev` debug image with
this checkout at `/src`; ns-3 is at `/opt/ns-3` and `ns3-build-contrib
--help` lists the build options. Set `NS3_IMAGE` to use another profile
(`latest-sanitized`, or a pinned `3.48-debug`). The images are documented
in swarm-routing/ns3-containers ("Which image").

There is no `.devcontainer/` yet; a release also publishes this module
prebuilt as the image `ghcr.io/swarm-routing/swarm-adapter-ns3:<tag>` (releases
up to v1.0.0: `ns3-swarm-core:<tag>`, deprecated and deleted once the
swarm-core migration completes, #255).

## Lint and format

```bash
# CI uses the org's style file when the repository has none
curl -fsSL -o .clang-format https://raw.githubusercontent.com/swarm-routing/.github/main/.clang-format
git ls-files '*.h' '*.cc' | xargs clang-format-18 --dry-run -Werror   # what `ci / format` runs
git ls-files '*.h' '*.cc' | xargs clang-format-18 -i                  # fix it
rm .clang-format                                                      # it is not committed
make headers          # licence headers (fetches the org tools)
make org-sync-check   # the org-managed files match the org templates
ruff check tools && ruff format --check tools   # Python, as python-quality (ruff 0.17.1)
```

`make lint` and `make format` run `clang-format` with whatever version is
first on your `PATH`; use version 18 so the result matches CI. `make check`
runs lint, the ns-3 test, the headers and the org-sync check together.

## CI map

| Workflow | Job (check name) | What it checks | Required |
| --- | --- | --- | --- |
| `ci.yml` | `ci / ns-latest debug`, `ci / ns-latest sanitized` | the module builds with strict warnings and `test.py -s swarm-core` passes in each image; determinism; coverage | yes |
| `ci.yml` | `ci / format` | clang-format-18 on every `.h`/`.cc` | yes |
| `ci.yml` | `ci / headers / licence` | licence headers | yes |
| `ci.yml` | `cxx-standard` | the locked swarm-core release's C++ standard is the image's ns-3 minimum | yes |
| `ci.yml` | `python-quality / python` | ruff and the tests of `tools/` | no |
| `ci.yml` | `codecov` | coverage upload | no (report-only) |
| `core.yml` | `core` | `swarm-core.lock` verified against the swarm-core release | yes |
| `api-surface.yml` | `api-surface` | `docs/api-surface.txt` still matches the headers and TypeIds ([api-policy.md](api-policy.md)) | yes |
| `org-sync.yml` | `check / org-sync` | org-managed files match the templates | yes |
| `pr-title.yml` | `title / title` | the PR title is a lowercase Conventional Commit | yes |
| `review-gate.yml` | `review / coderabbit` | CodeRabbit has reviewed the PR | yes |
| `dco.yml`, `scorecard.yml`, `sync-labels.yml` | | sign-off on outside contributions; OpenSSF Scorecard; labels | no |

The required checks are the ones the `main` ruleset lists: `gh api
repos/swarm-routing/swarm-adapter-ns3/rules/branches/main`. `main` merges
through a merge queue, so a pull request is merged by enabling auto-merge.

## Release

Releases follow the org procedure: [CONTRIBUTING "Cutting a release"](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#cutting-a-release).
Here, `release.yml` is dispatched with `dry-run`, then `bump`; merging the
bot's `chore(release): bump version A to B` PR tags the version and
publishes the asset `swarm-core-<ver>.tar.gz` (built by `make dist`; `DIST_CHECK=0`
skips its checks) and the image `swarm-adapter-ns3:<tag>`. The `swarm-core`
vcpkg port follows swarm-routing/swarm-core's releases.
A dry run also builds the consumers in `.github/downstream.txt` against the
candidate. What counts as a breaking change is in
[docs/api-policy.md](api-policy.md).

## Troubleshooting

- **`make test` is slow on Apple silicon or fails with an exec format
  error.** The `ns3-dev` images are `linux/amd64` only; Docker runs them under
  emulation. `make shell PLATFORM=linux/amd64` selects the platform
  explicitly.
- **A warning fails the ns-3 build.** `--strict-warnings` turns warnings in
  this module's sources into errors (ns-3's and swarm-core's headers are
  exempt). Fix the warning.
- **`ci / format` fails but `make lint` passed.** Your `clang-format` is not
  version 18, or the org's `.clang-format` was not in the checkout; see
  "Lint and format".
- **The configure step fails to download swarm-core, or reports a hash
  mismatch.** The tree has no network access, or the asset is not the one
  `swarm-core.lock` names: build offline with `-DSWARM_CORE_DIR=<checkout>/core`,
  and never edit the lock by hand (`release_lock.py verify swarm-core.lock`).
- **A protocol's ns-3 module fails with `contrib/swarm-core/core ... not
  found`.** It still looks for the `core/` this repository no longer has
  (#231): it drops that path and takes swarm-core's include directory from
  linking `${libswarm-core}` (README "swarm-core's primitives: a PUBLIC
  dependency").
- **The determinism gate fails.** Something reaching the output depends on
  wall-clock time, libc `rand()` or unordered-container order (golden rule 5).
