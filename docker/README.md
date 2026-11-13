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
| `ghcr.io/swarm-routing/anthocnet` | `<release>-optimized`, `<release>-debug` (e.g. `v2.7.0-optimized`) | `release.yml` | ns-3 with every module of a release built and tested; tags never move |
| `ghcr.io/swarm-routing/anthocnet-dev` | `<commit7>-<base7>-optimized` | `campaign-image.yml` | one image per campaign; cells pull it by digest. Deleted after 2 days unless a committed file pins it (`ci.yml`'s `dev-images` job, #987); `anthocnet-ns3-campaign` holds the images before #987, frozen |

```bash
docker run --rm ghcr.io/swarm-routing/anthocnet:vX.Y.Z-optimized \
  ./ns3 run --no-build anthocnet-example
docker buildx imagetools inspect ghcr.io/swarm-routing/anthocnet:vX.Y.Z-optimized \
  --format '{{ index .Image.Config.Labels "io.github.swarm-routing.ns3-dev.digest" }}'
```

The release images are named after the repository, `anthocnet`, from the first
release built by `reusable-ns3-image.yml` on (swarm-routing/.github#370, #987);
`anthocnet-ns3` holds only the frozen pre-#866 rebuilds below and receives
nothing new.

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
nothing). A campaign plan pins its digest and commit (`image`, `source_sha`),
and `ns3-campaign` passes both to every `benchmark-cell.yml` cell, so all cells
run one binary on one commit and none builds anything (#905). Published numbers pin an ns3-dev digest and a
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

### Images of record

Frozen means: never pushed again, never deleted, still pullable
([swarm-routing/.github#110](https://github.com/swarm-routing/.github/issues/110)).
Digests read from the registry on 2026-11-11. The `##PROV##` line of a
published cell names the image as `image=<repo>:<tag>` (cells before the
control campaign) or by digest (the control campaign, #867).

| Image | Digest | Cells that name it | Published numbers |
|---|---|---|---|
| `ghcr.io/danieljoppi/ns3:3.42-opt` | `sha256:af6b50155528164de33764f059a2736d31bf4cca1c4778009c93fb6620ddc95e` | 519 `##PROV##` lines in `results/cells/` | the MANET, mesh, FANET, VANET, disaster and ISL-grid pages through v2.7.0 ([benchmark index](../docs/benchmarks.md)) |
| `ghcr.io/danieljoppi/ns3:3.48` | `sha256:8b5a0e308dd4bbb55b3124cfff18f24746ba598646f90f24d38a6e4bfdf06c65` | 127 lines (LEO, SAGIN, satellite cells) | the satellite pages; the control arm of [v2.7.x image control](../docs/benchmarks/v2.7.x-image-control.md) |
| `ghcr.io/danieljoppi/ns3:3.36` | `sha256:8d5ec48981810473613cbec96a0ee855a85827c57151ae89f08d96985d524bac` | none | the NS-3 3.36 compatibility leg, until this change |
| `ghcr.io/danieljoppi/anthocnet-ns3:3.48` (`:latest`) | `sha256:404485ca9a37c1e49afab41bde6dd85ced3a236ef2dcdc28a3f3eeb79db626cb` | none | the release image of v2.x through v2.6.1 |
| `ghcr.io/danieljoppi/anthocnet-ns3:3.36` | `sha256:0e29faac52f7aa30024d33a8a1465f307ab2691df8a814978ddc03d385dcc366` | none | the 3.36 release image |
| `ghcr.io/swarm-routing/ns3:3.42-opt` | `sha256:735c52a7605d36f5a92fde95e17e29cbd77bc2529b31806be898749b41b81127` | none | the post-transfer rebuild (legacy replay path); not byte-identical to the `danieljoppi` tag |
| `ghcr.io/swarm-routing/ns3:3.48` | `sha256:b1ff6fab12600beee57e453f468d72b49c7a967ff868e6aac4a143697760e690` | none | as above |
| `ghcr.io/swarm-routing/ns3:3.36` | `sha256:938d9bb6ba74dba0af3e2dfc1af748621887ffc0f81a74bf859f1b2bd28c43e9` | none | the on-demand 3.36 leg of `ci-ns3.yml` |
| `ghcr.io/swarm-routing/anthocnet-ns3:3.48` (`:latest`) | `sha256:0671d73f2092181a25f39cc5ae7f4f985a43bdfa2b5d09319513c9c3b2795edc` | none | the post-transfer rebuild |
| `ghcr.io/swarm-routing/anthocnet-ns3:3.36` | `sha256:a6290ff0586fa35d91995e304094b6bd33e54664718f0791c03ef96ad144ca6e` | none | as above |

The other tags (`3.41`, `3.47`, the `-<release>` pins) are no published cell's
image; they stay for the same reason. A `:latest` tag on these repositories is
not a pin: use the digest. The campaign images of new numbers are a separate
set (`ghcr.io/swarm-routing/anthocnet-dev@sha256:...`, `anthocnet-ns3-campaign` before #987, built by
`campaign-image.yml`), recorded in each run's `##PROV##` line.

No workflow on `main` pushes to `ghcr.io/danieljoppi/`, and none pulls from it
for new work. The one reader is the control campaign: a plan's
`legacy_images` names a `danieljoppi` digest so a published cell can be re-run
in the image its `##PROV##` names (`tools/bench/campaigns/v2.7.x-image-control*.yaml`,
`benchmark-cell.yml` `legacy_image`). For the v2.4.0 cells, the control-plan
digests were pushed after those runs, so they are not proven to be the image
bytes the original runs used. Only the account owner can change those
packages (keep them public, delete nothing); the checklist is on #110.

Numbers measured on the new images (gcc-13, ns-3.48) are compared with
published ones only after the control campaign of
[#867](https://github.com/swarm-routing/AntHocNet/issues/867) has measured
the toolchain move.
