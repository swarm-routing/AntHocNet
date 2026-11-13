# Security Policy

AntHocNet is a **research routing protocol** for simulation (NS-3; the NS-2 adapter shipped through v1.9.0). This
document records its security posture honestly rather than promising guarantees
the protocol does not provide.

## Trust boundary

`core::codec::deserialize` is the **only** parser of untrusted bytes (an ant
frame arriving from the network). It is:

- **over-read safe** — every length-prefixed section is bounds-checked against
  the remaining buffer before it is read or allocated;
- **bound-enforcing** — declared element counts (`visited`, `history`,
  `helloDests`) are rejected if they exceed the protocol caps
  (`kMaxVisitedOnWire` / `kMaxHistoryOnWire` / `kMaxHelloOnWire`), so a peer
  cannot push 16-bit-max counts past the algorithm's invariants;
- **version- and enum-validating** — a frame whose offset-0 wire-version byte
  does not match `kWireVersion`, or whose `type`/`direction` byte is not a known
  enum value, is rejected in O(1) before any further parsing.

In scope for the implementation to guarantee: a single malformed packet cannot
crash, over-read, over-allocate, or violate the bounded-structure invariants.

## Known, by-design limitations (out of scope)

These are inherent to vanilla AntHocNet, not implementation bugs:

- **Ants are unauthenticated.** A malicious node can inject false pheromone
  (blackhole / greyhole), advertise bogus hello destinations, or mount wormhole
  attacks. Mitigations (signed control messages, trust / reputation systems) are
  research extensions, not protocol fixes.
- **Limited replay protection.** Replays are suppressed only by the bounded
  `(src, seq)` dedup history; once an entry is evicted, a replay is possible.

<!-- org-sync:begin reporting -->
## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: the
repository's **Security** tab, then **Report a vulnerability**. If that is
not available, open an issue that only asks for a private contact, and put
no vulnerability details in it. This is a research project maintained on a
best-effort basis: expect an acknowledgement within a few days, not an SLA.
<!-- org-sync:end reporting -->

## Verifying a release

Every release from v2.3.0 on can be checked three ways (#614). The release
workflow runs each check itself before it publishes, so a release whose
signatures do not verify never goes out.

**1. Integrity: the checksum.** It is published twice, in the release body
and as the `.sha256` asset.

```bash
sha256sum -c anthocnet-X.Y.Z.zip.sha256
```

**2. Origin: signed build provenance.** The install bundle and the drop-in
ns-3 module tarball carry SLSA build provenance. It is signed keylessly
(Sigstore) with the identity of `release.yml` in this repository, and it is
stored in GitHub's attestation store. With the [GitHub CLI](https://cli.github.com/)
(`--signer-workflow` accepts only an attestation signed by `release.yml`, not
one from another workflow, such as the image builds):

```bash
gh attestation verify anthocnet-X.Y.Z.zip --repo swarm-routing/AntHocNet \
  --signer-workflow swarm-routing/AntHocNet/.github/workflows/release.yml
gh attestation verify anthocnet-ns3-modules-X.Y.Z.tar.gz --repo swarm-routing/AntHocNet \
  --signer-workflow swarm-routing/AntHocNet/.github/workflows/release.yml
# from v2.7.0, the standalone adaptive-arms package (#770):
gh attestation verify adaptive-X.Y.Z.tar.gz --repo swarm-routing/AntHocNet \
  --signer-workflow swarm-routing/AntHocNet/.github/workflows/release.yml
```

The comparison harness (`routing-bench-<ver>.tar.gz`) and the `ns3_tool`
readers are swarm-routing/ns3-tools release assets, verified against that
repository (its SECURITY.md). This repository uses the release named in
`ns3-tools.lock`. `tools/ns3_tools.py` (which `make install-ns3` calls) checks
both assets against the sha256 recorded there when it downloads them, and
then reuses that unpacked copy, under `.ns3-tools/<tag>/`, without checking it
again; delete the directory to force a fresh, checked download. Setting
`NS3_TOOLS_DIR` replaces the release with a local checkout and checks nothing
(ns3-tools#29).

Releases published before the repository moved to the `swarm-routing`
organisation (2026-11-03) were signed as
`danieljoppi/AntHocNet`: verify those with `--repo danieljoppi/AntHocNet`
and `--signer-workflow danieljoppi/AntHocNet/.github/workflows/release.yml`.

The same Sigstore bundle is attached to the release as
`anthocnet-X.Y.Z.provenance.sigstore.json`, for offline checking or for
tools other than `gh`, such as `cosign verify-blob-attestation`. From
v2.7.0 its signed in-toto envelope is also attached on its own, as
`anthocnet-X.Y.Z.intoto.jsonl` (one DSSE envelope per line, the SLSA
convention).

**3. Contents: the SBOM.** `anthocnet-X.Y.Z.spdx.json` is an SPDX software
bill of materials of the unpacked bundle. It is attested against the zip, so
you can check that the SBOM belongs to the file you downloaded:

```bash
gh attestation verify anthocnet-X.Y.Z.zip --repo swarm-routing/AntHocNet \
  --signer-workflow swarm-routing/AntHocNet/.github/workflows/release.yml \
  --predicate-type https://spdx.dev/Document/v2.3
```

**Container images.** From #866 a release publishes
`ghcr.io/swarm-routing/anthocnet:<tag>-optimized` and `:<tag>-debug`, and
each campaign `ghcr.io/swarm-routing/anthocnet-ns3-campaign:<commit7>-<base7>-optimized`.
They are built by swarm-routing/.github's shared `reusable-ns3-image.yml`
`FROM` a pinned `ghcr.io/swarm-routing/ns3-dev` digest, and signed with cosign
(keyless) by digest, with build provenance and a CycloneDX SBOM in the
registry. The signing identity is that shared workflow; this repository is in
the certificate's workflow-repository extension (log in first with
`docker login ghcr.io` if the package is not public; `gh attestation verify`
on an `oci://` image needs registry credentials):

```bash
cosign verify ghcr.io/swarm-routing/anthocnet:vX.Y.Z-optimized \
  --certificate-identity-regexp '^https://github\.com/swarm-routing/\.github/\.github/workflows/reusable-ns3-image\.yml@refs/heads/main$' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-github-workflow-repository swarm-routing/AntHocNet
gh attestation verify oci://ghcr.io/swarm-routing/anthocnet:vX.Y.Z-optimized --owner swarm-routing \
  --signer-workflow swarm-routing/.github/.github/workflows/reusable-ns3-image.yml
```

The images this repository built itself until #866 (`images.yml`, retired)
stay pullable and are never rebuilt: `ghcr.io/danieljoppi/anthocnet-ns3` and
`ghcr.io/danieljoppi/ns3`, and their post-transfer copies under
`ghcr.io/swarm-routing/`. They are signed with cosign (keyless) by digest, so
every tag of an image is covered. The AntHocNet image also carries build
provenance in the registry.

```bash
cosign verify ghcr.io/danieljoppi/anthocnet-ns3:3.48 \
  --certificate-identity-regexp '^https://github.com/(danieljoppi|swarm-routing)/AntHocNet/\.github/workflows/(images|release)\.yml@' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com
gh attestation verify oci://ghcr.io/danieljoppi/anthocnet-ns3:3.48 --repo danieljoppi/AntHocNet --bundle-from-oci
```

These images were built and signed before the repository moved to
`swarm-routing` (2026-11-03), so their signatures and provenance name
`danieljoppi/AntHocNet`; the identity pattern also accepts `swarm-routing`
for images signed after the move (verify those with
`--repo swarm-routing/AntHocNet`).

Releases before v2.3.0 have only the checksum.
