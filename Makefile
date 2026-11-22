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
#   make ns3-doctor   NS3DIR=/path/to/ns-3-dev   # check the ns-3 tree first
#   make install-ns3  NS3DIR=/path/to/ns-3-dev   # BASELINES=0: AntHocNet only
#   make uninstall-ns3 NS3DIR=/path/to/ns-3-dev
#   make reproduce-list                         # every published figure (#621)
#   make reproduce FIG=grid                     # re-render from committed data, diff the PNG
#   make reproduce FIG=grid MODE=rerun NS3DIR=… # re-measure a cell, validate, render

.PHONY: help build test lint check ci shell core-test bench-core format ns3-doctor install-ns3 uninstall-ns3 clean reproduce reproduce-list

# The org's standard target names (swarm-routing/.github#11); `org-sync check`
# verifies the names, the recipes are this repository's own.
NS3_IMAGE ?= ghcr.io/swarm-routing/ns3-dev:latest-debug

# swarm-core's core/ comes from its vcpkg port (vcpkg.json, vcpkg-configuration.json;
# swarm-routing/.github#489) at the release swarm-core.lock names; SWARM_CORE_REF
# is that release's tag, as every place that names swarm-core must (checked by
# `swarm-core / pin`). Where swarm-core comes from, first match:
#   SWARM_CORE_DIR=<checkout>/core   a local swarm-core (an unreleased change)
#   VCPKG_ROOT=<vcpkg checkout>      the vcpkg port, as CI's core job
#   neither                          the lock's release, unpacked by tools/swarm_core.py
#                                    (the same tarball the port builds; no vcpkg needed)
SWARM_CORE_REF ?= v1.0.0
VCPKG_ROOT ?=
CORE_SWARM_CORE = $(if $(SWARM_CORE_DIR),-DSWARM_CORE_DIR=$(abspath $(SWARM_CORE_DIR)),$(if $(VCPKG_ROOT),-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake -DSWARM_CORE_DIR=,-DSWARM_CORE_DIR=$(shell python3 tools/swarm_core.py path)))
CORE_CMAKE_ARGS = -DCMAKE_BUILD_TYPE=Release $(CORE_SWARM_CORE)
# The port builds in its own directory: CMake reads vcpkg's toolchain only on
# a build directory's first configure, so the port and the sources never share one.
CORE_BUILD = core/build$(if $(SWARM_CORE_DIR),,$(if $(VCPKG_ROOT),-vcpkg))

help: ## list the targets
	@awk 'BEGIN{FS=":.*## "} /^[a-zA-Z0-9_-]+:.*## /{printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

build: ## build the core (the ns-3 module builds on a tree: make install-ns3)
	cmake -S core -B $(CORE_BUILD) $(CORE_CMAKE_ARGS)
	cmake --build $(CORE_BUILD) -j

test: core-test ## the fast tests CI runs on every PR (core, no simulator)

lint: ## clang-format dry run over the project's own C++, as CI
	git ls-files 'core/*.cpp' 'core/*.h' \
	  'ns3/anthocnet/*.cc' 'ns3/anthocnet/*.h' | xargs $(CLANG_FORMAT) --dry-run -Werror

check: lint test ## the local subset of what CI gates on

ci: check ## alias of check (CI itself runs .github/workflows/ci.yml)

shell: ## an interactive shell in the dev image with this repo at /src
	docker run --rm -it -v "$(CURDIR):/src" -w /src $(NS3_IMAGE) bash


core-test:
	cmake -S core -B $(CORE_BUILD) $(CORE_CMAKE_ARGS)
	cmake --build $(CORE_BUILD) -j
	cd $(CORE_BUILD) && ctest --output-on-failure

bench-core:
	cmake -S core -B $(CORE_BUILD) $(CORE_CMAKE_ARGS)
	cmake --build $(CORE_BUILD) -j --target bench_core
	$(CORE_BUILD)/bench_core $(BENCH_ARGS)

# The project's own C++ (.clang-format; ns3/baselines/ is excluded by
# .clang-format-ignore). CI runs the same file list with --dry-run -Werror.
CLANG_FORMAT ?= clang-format-18
format:
	git ls-files 'core/*.cpp' 'core/*.h' \
	  'ns3/anthocnet/*.cc' 'ns3/anthocnet/*.h' | xargs $(CLANG_FORMAT) -i

# The ns-3 tree check (#605); `make doctor` is the org's (swarm-routing/.github#647).
ns3-doctor:
	@$(MAKE) --no-print-directory -C ns3 doctor NS3DIR=$(NS3DIR)

install-ns3:
	$(MAKE) -C ns3 install NS3DIR=$(NS3DIR) BASELINES=$(or $(BASELINES),1)

uninstall-ns3:
	$(MAKE) -C ns3 uninstall NS3DIR=$(NS3DIR)

clean:
	rm -rf core/build core/build-vcpkg

# One command per published figure (#621): results/figures.yml maps each PNG
# under docs/benchmarks/ to its data, renderer and re-run recipe.
# REPRODUCE_ARGS passes through, e.g. --cell sat-failcell --runs 2 --time 500.
reproduce:
	@test -n "$(FIG)" || { echo "usage: make reproduce FIG=<name> [MODE=rerun NS3DIR=...]; make reproduce-list"; exit 2; }
	python3 tools/reproduce.py $(FIG) $(if $(filter rerun,$(MODE)),--rerun $(if $(NS3DIR),--ns3dir $(NS3DIR)),--from-data) $(REPRODUCE_ARGS)

reproduce-list:
	@python3 tools/reproduce.py list

# org-sync:begin org-tools
# The org tooling (check-headers, org-sync): a shallow clone, refreshed to ORG_REF,
# by default the template commit .github/org-sync.lock pins, as CI checks (#586).
ORG_DIR   ?= $(HOME)/.cache/swarm-routing/dotgithub
ORG_REF   ?= $(or $(shell sed -nE 's/^commit = "([0-9a-f]{40})"[[:space:]]*$$/\1/p' .github/org-sync.lock 2>/dev/null),main)
# make pre-push TITLE='type(scope): subject': the PR title rule of reusable-pr-title.yml
export TITLE
TITLE_RE  := ^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-z0-9._/-]+\))?!?: [^A-Z ](.*[^ ])?$$

.PHONY: org-tools org-sync-check pre-push

org-tools:  # the same templates CI checks against: refreshed to ORG_REF every time
	@[ -d "$(ORG_DIR)/.git" ] || { git init -q "$(ORG_DIR)" && git -C "$(ORG_DIR)" remote add origin https://github.com/swarm-routing/.github; }
	@git -C "$(ORG_DIR)" fetch -q --depth 1 origin $(ORG_REF) && git -C "$(ORG_DIR)" checkout -q --detach FETCH_HEAD

org-sync-check: org-tools ## org-managed files match the org templates
	python3 -I "$(ORG_DIR)/tools/org-sync/org_sync.py" check .

pre-push: check ## before every push: make check, then TITLE='type(scope): subject' (the PR title)
	@[ -n "$$TITLE" ] || { echo "make pre-push: pass TITLE='<the PR title>'" >&2; exit 2; }
	@printf '%s\n' "$$TITLE" | grep -Eq '$(TITLE_RE)' || { echo "PR title is not a lowercase Conventional Commit: $$TITLE" >&2; exit 1; }
	@echo "pre-push: ok ($$TITLE)"

.PHONY: doctor
doctor: ## this machine's tools against the versions CI runs (the org's tools/versions.env, go.mod), and the hosts the network steps need
	@$(MAKE) -s org-tools || echo "doctor: could not refresh $(ORG_DIR) (offline?): using it as it is" >&2
	@"$(ORG_DIR)/tools/doctor/doctor.sh" .
# org-sync:end org-tools
