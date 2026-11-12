# AntHocNet — top-level entry point.
#
# A simulator-agnostic algorithm core (core/) with a thin NS-3 adapter,
# installed as an additive contrib module onto your ns-3 tree. (The NS-2
# adapter shipped through v1.9.0 and was removed in v2.0.0, #307; check out
# the v1.2.0 tag for its last actively supported form.)
#
# Usage:
#   make test                                   # build + run core unit tests
#   make bench-core                             # core speed benchmarks (ns/op)
#   make format                                 # clang-format the C++ (#35)
#   make doctor       NS3DIR=/path/to/ns-3-dev   # check the ns-3 tree first
#   make install-ns3  NS3DIR=/path/to/ns-3-dev   # BASELINES=0: AntHocNet only
#   make uninstall-ns3 NS3DIR=/path/to/ns-3-dev
#   make reproduce-list                         # every published figure (#621)
#   make reproduce FIG=grid                     # re-render from committed data, diff the PNG
#   make reproduce FIG=grid MODE=rerun NS3DIR=… # re-measure a cell, validate, render

.PHONY: help build test lint check ci shell core-test bench-core format doctor install-ns3 uninstall-ns3 clean reproduce reproduce-list

# The org's standard target names (swarm-routing/.github#11); `org-sync check`
# verifies the names, the recipes are this repository's own.
NS3_IMAGE ?= ghcr.io/swarm-routing/ns3-dev:latest-debug

help: ## list the targets
	@awk 'BEGIN{FS=":.*## "} /^[a-zA-Z0-9_-]+:.*## /{printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## build the core (the ns-3 module builds on a tree: make install-ns3)
	cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
	cmake --build core/build -j

test: core-test ## the fast tests CI runs on every PR (core, no simulator)

lint: ## clang-format dry run over the project's own C++, as CI
	git ls-files 'core/*.cpp' 'core/*.h' \
	  'ns3/anthocnet/*.cc' 'ns3/anthocnet/*.h' | xargs $(CLANG_FORMAT) --dry-run -Werror

check: lint test ## the local subset of what CI gates on

ci: check ## alias of check (CI itself runs .github/workflows/ci.yml)

shell: ## an interactive shell in the dev image with this repo at /src
	docker run --rm -it -v "$(CURDIR):/src" -w /src $(NS3_IMAGE) bash


core-test:
	cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
	cmake --build core/build -j
	cd core/build && ctest --output-on-failure

bench-core:
	cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
	cmake --build core/build -j --target bench_core
	core/build/bench_core $(BENCH_ARGS)

# The project's own C++ (.clang-format; ns3/baselines/ is excluded by
# .clang-format-ignore). CI runs the same file list with --dry-run -Werror.
CLANG_FORMAT ?= clang-format-18
format:
	git ls-files 'core/*.cpp' 'core/*.h' \
	  'ns3/anthocnet/*.cc' 'ns3/anthocnet/*.h' | xargs $(CLANG_FORMAT) -i

doctor:
	@$(MAKE) --no-print-directory -C ns3 doctor NS3DIR=$(NS3DIR)

install-ns3:
	$(MAKE) -C ns3 install NS3DIR=$(NS3DIR) BASELINES=$(or $(BASELINES),1)

uninstall-ns3:
	$(MAKE) -C ns3 uninstall NS3DIR=$(NS3DIR)

clean:
	rm -rf core/build

# One command per published figure (#621): results/figures.yml maps each PNG
# under docs/benchmarks/ to its data, renderer and re-run recipe.
# REPRODUCE_ARGS passes through, e.g. --cell sat-failcell --runs 2 --time 500.
reproduce:
	@test -n "$(FIG)" || { echo "usage: make reproduce FIG=<name> [MODE=rerun NS3DIR=...]; make reproduce-list"; exit 2; }
	python3 tools/reproduce.py $(FIG) $(if $(filter rerun,$(MODE)),--rerun $(if $(NS3DIR),--ns3dir $(NS3DIR)),--from-data) $(REPRODUCE_ARGS)

reproduce-list:
	@python3 tools/reproduce.py list
