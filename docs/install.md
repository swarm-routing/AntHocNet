# Install AntHocNet

<!--
The org's skeleton for docs/install.md (swarm-routing/.github#881, #884), an
org-sync seed for every C++ repository: written once, then the repository's
own. Fill every section from what actually works today (the commands CI
runs, the release assets, the vcpkg port); never invent a command or a
version. A section with nothing to say for this repository keeps one sentence
saying so (an ns-3 module has no vcpkg port until the registry's `ns3` port
lands; a core-only repository supports no simulator). The "Known simulator
bugs" section is org-managed: generated from swarm-routing/sim-tools
docs/ns3-bugs.md by swarm-routing/.github cmd/ns3-bugs and refreshed by
org-bot's org-sync PRs; never edit it here. Delete this comment.
-->

What a user installs from this repository (the C++ library, the ns-3 module,
the tools) and which of the sections below applies to which part.

## Supported platforms

| Part | Platform | Compiler, C++ standard | CMake | Simulator |
|------|----------|------------------------|-------|-----------|
| `core/` | Linux x64 (Ubuntu 24.04, as CI) | GCC 13 / Clang 18, C++20 | 3.28 | none |
| ns-3 module | Linux x64 | as the ns-3 release | as the ns-3 release | ns-3.48, ns-3.49 |

## Linux through vcpkg (manifest mode)

The C++ core is published as a port in the org's registry,
[swarm-routing/vcpkg-registry](https://github.com/swarm-routing/vcpkg-registry);
every release opens its port PR (`vcpkg-ports` of the release workflow).
Install vcpkg once:

```bash
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
```

Then, in your project, declare the dependency. `vcpkg.json`:

```json
{
  "name": "my-simulation",
  "version": "0.1.0",
  "dependencies": ["<port>"]
}
```

`vcpkg-configuration.json` takes the port from the org's registry and
vcpkg's helper ports from the builtin one; each `baseline` is a commit of
that registry (`git -C "$VCPKG_ROOT" rev-parse HEAD` for the builtin one,
the registry's `main` for ours):

```json
{
  "default-registry": {
    "kind": "builtin",
    "baseline": "<vcpkg commit>"
  },
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/swarm-routing/vcpkg-registry",
      "baseline": "<vcpkg-registry commit>",
      "packages": ["swarm-core", "<port>"]
    }
  ]
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my-simulation CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(<port> CONFIG REQUIRED)
add_executable(my-simulation main.cc)
target_link_libraries(my-simulation PRIVATE <imported target>)
```

Configure with the vcpkg toolchain, which installs the manifest's ports
first:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build -j
```

A minimal `main.cc` that uses the library, and what it prints.

## ns-3

One subsection per ns-3 release this repository supports (the releases its
CI builds on), newest first. Each is a complete example install.

### ns-3.49

- **Required tools**: the compiler and CMake above, Python 3, Git, Ninja or
  Make; what ns-3 itself requires for the modules this one depends on.
- **Optional tools**: the `ns3-dev` container image (everything preinstalled),
  `ccache`, `gdb`, NetAnim, Python bindings; what each adds.
- **Install**: the commands, from a clean machine to a run of the module's
  example, with the ns-3 release tag and this repository's release tag:

```bash
git clone --branch ns-3.49 --depth 1 https://gitlab.com/nsnam/ns-3-dev.git ns-3
git clone --branch v<version> --depth 1 https://github.com/swarm-routing/AntHocNet.git ns-3/contrib/anthocnet
cd ns-3
./ns3 configure --enable-examples --enable-tests
./ns3 build
./test.py --suite=anthocnet
```

- **Or the container**: `docker run --rm -it ghcr.io/swarm-routing/ns3-dev:3.49`
  and the commands that build this module in it.
- **Known bugs on this release**: the IDs from the table below that affect it,
  and what this repository does about each.

## OMNeT++

Not supported yet: swarm-adapter-omnet adds it. This section gets one
subsection per OMNeT++ release once a release of this repository supports it.

## Known simulator bugs

<!-- org-sync:begin known-bugs -->
<!-- Generated from swarm-routing/sim-tools docs/ns3-bugs.md by swarm-routing/.github
     cmd/ns3-bugs (swarm-routing/.github#884); never edit by hand: a change goes to the register. -->
Upstream ns-3 defects our repositories meet, from the org's [ns-3 bug register](https://github.com/swarm-routing/sim-tools/blob/main/docs/ns3-bugs.md).
Each ID links its RCAPA (the reproduction, the root cause, the upstream report).

| ID | What breaks | ns-3 versions | Status | Our fix or workaround |
|----|-------------|---------------|--------|-----------------------|
| [NS3-001](https://github.com/swarm-routing/sim-tools/blob/main/docs/ns3-rcapa/ns3-rcapa-aodv-icmp-livelock.md) | AODV livelocks in a zero-time ICMP "net unreachable" storm | 3.49 and ns-3-dev `master` `8ec9b4a1` (run); not 3.48 | open upstream; reported, fix proposed (not ours); worked around | AODV-family arms on `ns3-dev:3.48` (AntHocNet CI) until the harness guard reaches them: `routing-bench` wraps the `aodv` arm in a guard ([swarm-routing/sim-tools#322](https://github.com/swarm-routing/sim-tools/pull/322)) applying [nsnam/ns-3-dev!3101](https://gitlab.com/nsnam/ns-3-dev/-/merge_requests/3101)'s rules (maintainer decision 2026-11-23, swarm-routing/sim-tools#238); strict XFAIL `3.49 sparse-aodv` in swarm-routing/ns3-containers `test/known-failures.txt` |
| [NS3-002](https://github.com/swarm-routing/sim-tools/blob/main/docs/ns3-rcapa/ns3-rcapa-socket-map-heap-order.md) | AODV and DSDV order per-interface sockets by heap address | 3.42, 3.48, 3.49 and ns-3-dev `master` `8ec9b4a1`, `a150de8f` (run); 3.42, 3.48, 3.49 (read) | open upstream; reported; worked around | `--isolateArms` (default since v1.1.0); AntHocNet: its own maps ordered, stock `aodv` in its own process, the order gate on `anthocnet` rows only |
| [NS3-003](https://github.com/swarm-routing/sim-tools/blob/main/docs/ns3-rcapa/ns3-rcapa-dsdv-null-route.md) | DSDV sends on a route with a null output device (assert, or a silent SIGSEGV when optimized) | 3.36–3.48 and ns-3-dev 2026-08 (run); 3.49 (read) | open upstream; reported, fix proposed; worked around | AntHocNet's `isl-grid` refuses `dsdv` on multi-interface topologies, checked by its satellite arm smoke |

Fixed upstream, but present in an older ns-3 release you may still pin:

| What breaks | ns-3 versions | Fixed in | Our fix or workaround |
|-------------|---------------|----------|-----------------------|
| 802.11b HR/DSSS (5.5, 11 Mb/s) frames dropped as an unsupported modulation: `GetModulationClassForStandard(WIFI_STANDARD_80211b)` returns `WIFI_MOD_CLASS_DSSS`, and since 3.41 `WifiPhy::StartReceivePreamble` refuses any class above it | 3.42 (run); 3.41, 3.42 (read); not 3.40 or earlier (no gate, read) | 3.43 (`da9fcebe`) | strict XFAIL `3.41`/`3.42` `dsdv-manet`, `dense-{aodv,olsr,dsdv}` (`no-packets-delivered`) in swarm-routing/sim-containers `test/known-failures.txt` ([swarm-routing/sim-containers#250](https://github.com/swarm-routing/sim-containers/pull/250)); [swarm-routing/AntHocNet#923](https://github.com/swarm-routing/AntHocNet/issues/923) to check for 802.11b above 2 Mb/s on 3.42 |
| Four wifi unit suites (`wifi-txop`, `wifi-mlo`, `wifi-gcr`, `wifi-emlsr-basic-exchanges`) fail when `test-runner` is built without the `test` module: the tests match frames by packet UID, which needs packet metadata on, and 3.48 turns it on only through `src/test`'s `ns3tcp` suites (a test bug; `wifi-txop` then reads past its frame list) | 3.48 (run) | 3.49 (the tests use the MPDU's own UID) | strict XFAIL `3.48 unit:<suite>` in swarm-routing/sim-containers `test/known-failures.txt` |
<!-- org-sync:end known-bugs -->

## Troubleshooting

The failures people meet installing this repository (a missing port, a
baseline without the version, an ns-3 configure error) and their fixes.
