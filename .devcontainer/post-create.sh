#!/usr/bin/env bash
# Devcontainer post-create (#334): make the image's ns-3 tree carry *this*
# checkout's AntHocNet module, then build it.
#
# The base image (ghcr.io/swarm-routing/ns3-dev:<ver>-debug, built by
# swarm-routing/ns3-containers) has stock ns-3 configured and fully built at
# /opt/ns-3, and no AntHocNet in it. This script installs the modules from the
# workspace and builds them. Since every stock object file is already in the
# image's build cache, only the AntHocNet delta compiles, as long as the
# configure keeps the image's profile flags (step 2).
#
# The three steps below mirror .github/workflows/ci-ns3.yml's `ns3-build` job —
# "Install AntHocNet module", "Configure (scope examples/tests to AntHocNet)"
# and "Build" — on purpose: if it builds here it builds in CI, and there is one
# place to change when the build recipe changes. The one addition: the
# configure keeps the image's profile flags (step 2), since this image is a
# -debug one and CI's is the plain `default` profile.
set -euo pipefail

# containerEnv sets this in devcontainer.json; the default keeps the script
# runnable via `bash .devcontainer/post-create.sh` in a plain `docker run`.
NS3DIR=${NS3DIR:-/opt/ns-3}

# --- 1. Install AntHocNet module (ci-ns3.yml: "Install AntHocNet module") --------
# Additive: copies ns3/{model,helper,examples,test} plus the shared core/ into
# $NS3DIR/contrib/anthocnet. No stock ns-3 file is edited (see ns3/Makefile).
echo ">> Installing the AntHocNet module from $PWD into $NS3DIR"
make install-ns3 NS3DIR="$NS3DIR"

# --- 2. Configure (ci-ns3.yml: "Configure (scope examples/tests to AntHocNet)") ---
cd "$NS3DIR"

# The image records its configure line in .swarm-configure.env (ns3-containers
# golden rule 10): NS3_PROFILE_FLAGS is `-d debug` on a -debug image. Passing
# it again keeps every stock object up to date; leaving it out would switch the
# tree to ns-3's default profile and recompile all of ns-3 (~15 min). On an
# image without the record (not an ns3-dev one), configure as CI does, with none.
profile_flags=""
if [ -f .swarm-configure.env ]; then
    # shellcheck source=/dev/null
    profile_flags=$(. ./.swarm-configure.env && printf '%s' "${NS3_PROFILE_FLAGS:-}")
fi

# Build only AntHocNet's own examples/tests, not the stock modules' (their
# cross-module deps drift by release). The filter flag scopes that to our
# module; it was added after ns-3.36, so older trees just build + smoke.
# #435: this list must match ci-ns3.yml's mods= — without aomdv/gpsr/oracle the
# anthocnet-compare and isl-grid examples are SKIPPED silently (their
# CMakeLists lists those libs), so the "run anthocnet-compare" suggestion
# below would point at a binary that was never built. Consistency is now
# enforced by tools/checks/check-allowlists.sh.
mods='anthocnet;aomdv;gpsr;oracle;adaptive;routing-bench;wifi;mobility;applications;csma;aodv;olsr;dsdv;flow-monitor;point-to-point;mesh'
if ./ns3 configure --help 2>/dev/null | grep -q filter-module-examples-and-tests; then
    # shellcheck disable=SC2086  # profile_flags splits into "-d" and its value
    ./ns3 configure $profile_flags --enable-tests --enable-examples --disable-werror \
        --filter-module-examples-and-tests='anthocnet;routing-bench' \
        --enable-modules="$mods"
else
    # shellcheck disable=SC2086
    ./ns3 configure $profile_flags --enable-examples --disable-werror --enable-modules="$mods"
fi

# The -debug image keeps asserts and NS_LOG on, which is what development
# wants; numbers for publication come from the campaign images (optimized), not
# from here.

# --- 3. Build (ci-ns3.yml: "Build") ----------------------------------------------
./ns3 build

cat <<EOF

>> AntHocNet is built. Run your first simulation:

     cd $NS3DIR
     ./ns3 run anthocnet-example

   Compare against AODV / OLSR / DSDV:
     ./ns3 run "anthocnet-compare --nNodes=20 --time=30"

   Module test suite:
     ./test.py -s anthocnet

   Core unit tests (no simulator needed), from the workspace folder:
     make test
EOF
