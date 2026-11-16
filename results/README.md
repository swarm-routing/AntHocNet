# Benchmark data

The raw measurements behind every published number (#615). The pages that
explain them live in [`docs/benchmarks/`](../docs/benchmarks/README.md); the
charts are drawn from these files by
[`ns3/tools/family-charts.py`](../ns3/tools/family-charts.py) and committed
next to the pages.

| directory | what | written by |
|---|---|---|
| `cells/` | one harness run per file (`##RUN##` per-seed rows, `##PROV##` commit, `# stddev` lines): the family results pages, the MANET grid, the satellite suites | saved from `paper-benchmark` / `satellite-benchmark` logs (the `benchmark-results` skill) |
| `campaign/` | classified campaign CSVs from `scenario-matrix` and `rescue-artifacts`, named `<actions-run-id>-<label>.csv` (and `-runs.csv` per-seed siblings) | `scenario-matrix.yml` (`commit=true`), `rescue-artifacts.yml` |

Rules:
- **Never edit a file here by hand.** A number that changes is a new run with
  its own run ID, so an old page keeps citing the data it was written from.
- **Validate before you cite:** `python3 tools/bench/scenario_check.py results <file>`.
- **Legacy cells read leniently.** Nine committed cells (`fanet-main*.txt`,
  `grid-*-{nakagami,tworay}.txt`, `static-mesh-jitter-cap1s.txt`) hold a
  `##RSS##` line written into the middle of a `##RUN##` row (stderr and
  stdout interleaved in the old logs). The ns3-tools readers are strict by
  default since v0.2.0 (swarm-routing/ns3-tools#58) and refuse such a row, so
  these files are read with a lenient reader (`--lenient`, or
  `bench_parse.Reader(strict=False)` as `tools/bench/test_bench_parse_query.py`
  does), which drops the broken row and says so. The data is never rewritten:
  a corrected number is a new run, per the first rule.
- **Summarise by script:** `tools/bench/bench_parse.py` for cells,
  `tools/bench/sweep_summary.py` for campaign CSVs,
  `ns3/tools/leo-summary.py` for the constellation cells.
