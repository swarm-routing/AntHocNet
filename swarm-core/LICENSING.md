# Licensing

`swarm-routing/swarm-adapter-ns3` is licensed per directory, under the same rules as
[AntHocNet](https://github.com/swarm-routing/AntHocNet/blob/main/LICENSING.md), the source repository
([ADR-0028](https://github.com/swarm-routing/AntHocNet/blob/main/docs/adr/0028-core-is-dual-licensed.md)):

| Path | What | Licence |
|------|------|---------|
| `tools/` | standalone scripts (checks, release, data handling) | `MIT OR GPL-2.0-only` |
| `tools/checks/tests/` | the `tools/checks` scripts' self-tests run under coverage (`ci.yml` `python-quality`) | `MIT OR GPL-2.0-only` |
| `model/`, `helper/`, `test/`, `examples/`, `CMakeLists.txt`, `swarm-core.lock` | the `swarm-core` ns-3 contrib module, and the swarm-routing/swarm-core release it builds on (#231) | `GPL-2.0-only` |
| `docs`, `README.md`, `AGENTS.md`, `CLAUDE.md` | docs | `GPL-2.0-only` |
| everything else | | `GPL-2.0-only` |

<!-- org-sync:begin licence-texts -->
Texts: [`LICENSE`](LICENSE) (GPL-2.0) and [`LICENSE-MIT`](LICENSE-MIT) (the
MIT option). Each source file states its licence in its
`SPDX-License-Identifier` line; CI checks it
(`swarm-routing/.github` `cmd/check-headers`; locally `make headers`).
<!-- org-sync:end licence-texts -->

## What this means in practice

- **swarm-core's simulator-free primitives** (`core/`, `MIT OR
  GPL-2.0-only`) are no longer in this repository (#231): they live in
  [swarm-routing/swarm-core](https://github.com/swarm-routing/swarm-core),
  under its own licence, and this module builds on the release
  `swarm-core.lock` names. The GPL module uses them; nothing here flows back.
- **The dual-licensed paths** (the standalone tooling) may be used under MIT
  on their own — in another simulator, a routing daemon, firmware, or a closed
  product — keeping the copyright notice and the MIT text, or under
  GPL-2.0-only.
- **The ns-3 modules, the site, docs and data are GPL-2.0-only.** An ns-3
  module builds only inside a GPL-2.0 ns-3 tree, so an MIT option would change
  nothing for it.
- **The dependency only goes one way.** GPL code may use the dual-licensed
  parts; they never pull GPL-only code in. A dual script imports no GPL-only
  code.
- **Combined artifacts** (an ns-3 build, a published image, the site) are
  distributed under GPL-2.0; each dual-licensed file inside keeps its notice.
- **Contributions** to the dual-licensed paths are accepted under both
  licences; anywhere else under GPL-2.0-only. Code arriving from another
  project keeps its own licence and may only land where that licence fits
  (GPL-2.0-only code never in a dual path; third-party code, such as AntHocNet's
  vendored AOMDV/GPSR, only with its provenance recorded).
