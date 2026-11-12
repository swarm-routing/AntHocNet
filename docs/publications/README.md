# Publications — source-of-truth digests

Structured technical digests of the AntHocNet publications this repository
implements. Each digest restates, in reference form, everything the fidelity
work needs — algorithm mechanics, every parameter value, the equations, the
experimental setup and headline results — with section pointers back to the
original document, and a mapping table from each paper claim to where (and
whether) this codebase implements it.

**Why digests and not full-text conversions:** the originals are published,
copyright-protected papers (Springer LNCS / IDSIA). We do not reproduce or
vendor their full text or PDFs here; the digests carry the technical content in
reference form with citations. Each document is an entry of
[`docs/references.bib`](../references.bib), keyed as the table below says; obtain
the originals through its DOI link or record page.

## Contents

| Path | Document | Status |
|------|----------|--------|
| [`papers/2004-ppsn-anthocnet.md`](papers/2004-ppsn-anthocnet.md) | Di Caro, Ducatelle & Gambardella, *AntHocNet: an Ant-Based Hybrid Routing Algorithm for Mobile Ad Hoc Networks*, PPSN VIII, LNCS 3242, pp. 461–470, 2004 (= IDSIA tech report IDSIA-25-04-2004; conference best paper). This is the repo's canonical **[1]**; `DDG04`, <https://doi.org/10.1007/978-3-540-30217-9_47>. The journal version, `DDG05` (<https://doi.org/10.1002/ett.1062>), restates the algorithm. | ✅ digested (2026-07-19, from the maintainer-supplied PDFs; the two versions' bodies are 99.96 % identical) |
| [`thesis/`](thesis/README.md) | Ducatelle, *Adaptive Routing in Ad Hoc Wireless Multi-hop Networks*, PhD thesis, USI/IDSIA, 2007 — the designated **primary source** for parameter verification (decision of record on #58/#70); `ducatelle2007`, <https://sonar.ch/global/documents/318075> | 🟡 obtained 2026-07-25, partially digested; see `thesis/README.md` for which open questions it answered and which remain |

## How these digests are used

- Parameter/formula verification tickets check the implementation against the
  digest and record the verdict on the issue (ADR-0013): #58 (parameter table),
  #70 (A2 formula/α — **confirmed** by the 2004 paper), #88 (T_hop value —
  **not stated** in the 2004 paper, thesis needed), #89 (jitter estimator —
  the 2004 paper uses the 99th-percentile delay, no explicit jitter formula;
  thesis needed).
- The v1.0 compliance ledger (#91) links its "paper claim ↔ evidence" rows to
  the digests' mapping tables.
- Known, deliberate deviations from the papers are listed in each digest's
  mapping table and cross-referenced from `docs/porting-notes.md` /
  `CONTEXT.md` §8.
