# Container images

This repository builds no ns-3 images of its own (#866, org decision
[swarm-routing/.github#107](https://github.com/swarm-routing/.github/issues/107)).
Every image that carries AntHocNet is built `FROM` an image of
[`swarm-routing/ns3-containers`](https://github.com/swarm-routing/ns3-containers)
(`ghcr.io/swarm-routing/ns3-dev`), pinned by digest, by the org's shared
workflow `reusable-ns3-image.yml` in
[`swarm-routing/.github`](https://github.com/swarm-routing/.github): it
resolves `ns3-dev:<version>-<profile>` to its digest once, lays the modules
out with `make install-ns3`, builds them on the image's warm tree with
`ns3-build-contrib`, runs their `test.py` suites on the built image, and only
then pushes, signs (cosign keyless), attests build provenance and a CycloneDX
SBOM, and verifies the signature. Each image records its base digest in the
label `io.github.swarm-routing.ns3-dev.digest`, its commit in
`org.opencontainers.image.revision` and its modules in
`io.github.swarm-routing.modules`.

| Image | Tags | Built by | Use |
|-------|------|----------|-----|
| `ghcr.io/swarm-routing/ns3-dev` | `<ver>-{default,debug,sanitized,optimized}`, each with an immutable `-<sha7>` twin | ns3-containers | plain ns-3 (the comparison protocols included); CI runs in `latest` / `3.48` |
| `ghcr.io/swarm-routing/anthocnet-ns3` | `<release>-optimized`, `<release>-debug` (e.g. `v2.7.0-optimized`) | `release.yml` | ns-3 with every module of a release built and tested; tags never move |
| `ghcr.io/swarm-routing/anthocnet-ns3-campaign` | `<commit7>-<base7>-optimized` | `campaign-image.yml` | one image per campaign; cells pull it by digest |

```bash
docker run --rm ghcr.io/swarm-routing/anthocnet-ns3:vX.Y.Z-optimized \
  ./ns3 run --no-build anthocnet-example
docker buildx imagetools inspect ghcr.io/swarm-routing/anthocnet-ns3:vX.Y.Z-optimized \
  --format '{{ index .Image.Config.Labels "io.github.swarm-routing.ns3-dev.digest" }}'
```

`SECURITY.md` has the `cosign verify` and `gh attestation verify` commands.

**Profiles.** ns3-dev's `optimized` is ns-3's `release` profile (assertions and
`NS_LOG` compiled out, no `-march=native`); CI runs in `default`/`debug`
images, whose assertions catch real bugs. An ns3-dev image records its
profile in `BUILD_PROFILE` and its configure flags in
`/opt/ns-3/.swarm-configure.env`; `benchmark-cell.yml` reads them
([methodology: build profiles](../docs/benchmarks/methodology.md#build-profiles-default-for-ci-release-for-campaigns)).

**Version.** ns-3.49 livelocks stock AODV and the vendored AOMDV (#843), so
release and campaign images use ns3-dev `3.48` until a fixed ns-3 release
reaches `latest`.

**Campaigns.** `campaign-image.yml` builds the image of one commit on one
ns3-dev digest, once (a second call with the same pair reuses it and pushes
nothing). `tools/bench/campaign.py dispatch --build-image` runs it and passes
the digest to every `benchmark-cell.yml` cell, so all cells run one binary
and none builds anything. Published numbers pin an ns3-dev digest and a
campaign-image digest, never a rolling tag; both are in each cell's
`##PROV##` line.

## Historical images (frozen)

Until #866 `docker/Dockerfile.ns3` and `images.yml` built this
repo's own ns-3 (Ubuntu 22.04, gcc-11, 3.36 / 3.41 / 3.42 / 3.47 / 3.48;
`ns3:<ver>-opt` in ns-3's `release` profile for 3.42 only). Both are retired;
the recipe is in the git history (and at every release tag through v2.6.1).
What they published stays pullable and is never rebuilt:

- `ghcr.io/danieljoppi/ns3:<ver>[-opt][-<release>]` and
  `ghcr.io/danieljoppi/anthocnet-ns3:<ver>[-<release>]`, published before the
  org transfer (2026-11-03). The cells and pages published so far name these
  tags (most campaigns ran on `ns3:3.42-opt`). They are frozen
  ([swarm-routing/.github#110](https://github.com/swarm-routing/.github/issues/110))
  and no workflow pulls them.
- `ghcr.io/swarm-routing/ns3:<ver>` (`3.36`, `3.41`, `3.42`, `3.42-opt`, `3.47`,
  `3.48`) and `ghcr.io/swarm-routing/anthocnet-ns3:<ver>`, the same recipe
  built after the transfer. `benchmark-cell.yml` without an `image` (the
  legacy replay path) and `ci-ns3.yml`'s on-demand 3.36 leg pull
  `ns3:<ver>` from here; same Dockerfile and ns-3 release as the
  `danieljoppi` tags, but a later build, so not byte-identical to them.
- NS-2 images (`ns2`, `anthocnet-ns2` at `2.34` / `2.35`), built through
  v1.9.0; see [docs/ns2-support.md](../docs/ns2-support.md).

Numbers measured on the new images (gcc-13, ns-3.48) are compared with
published ones only after the control campaign of
[#867](https://github.com/swarm-routing/AntHocNet/issues/867) has measured
the toolchain move.
