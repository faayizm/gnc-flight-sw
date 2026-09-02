# =============================================================================
#  HYPERSAT flight software -- every workflow in one place.
#  Run `make help` for the list.
# =============================================================================

BUILD_DIR   := build
VENV        := .venv
PYTHON      := $(VENV)/bin/python
SYS_PYTHON  := python3
JOBS        := $(shell (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null) || echo 4)
TTC_PORT    ?= 50001

.DEFAULT_GOAL := help

# -----------------------------------------------------------------------------

.PHONY: help
help:  ## Show this message
	@echo "HYPERSAT flight software"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Typical session:"
	@echo "  make build       compile and unit test"
	@echo "  make run         start the spacecraft"
	@echo "  make demo        (in another terminal) fly a scripted pass"

# -- build --------------------------------------------------------------------

.PHONY: build
build: configure  ## Compile the flight software and the tests
	@cmake --build $(BUILD_DIR) -j$(JOBS)

.PHONY: configure
configure:
	@test -f $(BUILD_DIR)/CMakeCache.txt || \
		cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo

.PHONY: debug
debug:  ## Reconfigure and build with assertions and no optimisation
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR) -j$(JOBS)

.PHONY: clean
clean:  ## Remove build output and scratch storage files
	@rm -rf $(BUILD_DIR) *_nvm.bin
	@echo "cleaned"

.PHONY: distclean
distclean: clean  ## Also remove the Python virtual environment
	@rm -rf $(VENV)

# -- generate -----------------------------------------------------------------

.PHONY: venv
venv:  ## Create the Python virtual environment used by the generator
	@test -d $(VENV) || $(SYS_PYTHON) -m venv $(VENV)
	@$(VENV)/bin/pip -q install --upgrade pip
	@$(VENV)/bin/pip -q install -r requirements.txt
	@echo "virtual environment ready at $(VENV)"

.PHONY: gen
gen: venv  ## Regenerate everything from dictionary/mission.yaml
	@$(PYTHON) tools/gen.py

.PHONY: check-gen
check-gen: gen  ## Fail if the generated files are not up to date with the dictionary
	@if [ -n "$$(git status --porcelain fsw/generated gnd/openc3 gnd/pyground/dictionary.py docs/ICD.md)" ]; then \
		echo "error: generated files are stale. Run 'make gen' and commit the result."; \
		git --no-pager diff --stat fsw/generated gnd/openc3 gnd/pyground/dictionary.py docs/ICD.md; \
		exit 1; \
	fi
	@echo "generated files are up to date"

# -- test ---------------------------------------------------------------------

.PHONY: test
test: build  ## Run the unit tests
	@$(BUILD_DIR)/tests/fsw_tests

.PHONY: sil
sil: build  ## Run the software-in-the-loop tests against the real binary
	@$(SYS_PYTHON) tests/sil/test_endtoend.py

.PHONY: check-layering
check-layering:  ## Enforce the architecture: no OS headers outside platform/
	@echo "checking layering rules..."
	@bad=$$(grep -rlE '#include *<(sys/|netinet/|arpa/|pthread|unistd|fcntl|signal)' \
		fsw/core fsw/hal fsw/apps fsw/generated 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "error: system headers outside fsw/platform/:"; echo "$$bad"; exit 1; fi
	@bad=$$(grep -rl '#include "platform/' fsw/core fsw/hal fsw/apps 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "error: core/hal/apps must not include platform/:"; echo "$$bad"; exit 1; fi
	@bad=$$(grep -rlE '\b(new|delete|malloc|calloc|realloc|free)\b *[({]' \
		fsw/core fsw/apps 2>/dev/null || true); \
	if [ -n "$$bad" ]; then \
		echo "error: dynamic allocation in the flight path:"; echo "$$bad"; exit 1; fi
	@echo "  no system headers outside platform/"
	@echo "  core/hal/apps do not depend on platform/"
	@echo "  no dynamic allocation in core/ or apps/"

.PHONY: check-links
check-links:  ## Verify every relative link in the documentation resolves
	@$(SYS_PYTHON) tools/check_links.py .

.PHONY: check-toolbox
check-toolbox:  ## Run every teaching program, to prove the lessons still work
	@for f in learn/toolbox/*.py; do \
		case "$$f" in */__*) continue;; esac; \
		printf "  %-40s" "$$f"; \
		if $(SYS_PYTHON) "$$f" > /dev/null 2>&1; then echo "ok"; \
		else echo "FAILED"; $(SYS_PYTHON) "$$f"; exit 1; fi; \
	done

.PHONY: learn
learn:  ## Where to start learning
	@echo ""
	@echo "  An 18-lesson course built around this spacecraft."
	@echo ""
	@echo "    open learn/README.md            the curriculum and the three tracks"
	@echo "    open learn/GLOSSARY.md          every acronym, in plain language"
	@echo ""
	@echo "  Nothing to install -- try one now:"
	@echo ""
	@echo "    python3 learn/toolbox/crc_playground.py    catch a cosmic ray"
	@echo "    python3 learn/toolbox/orbit_sandbox.py     why it doesn't fall down"
	@echo "    python3 learn/toolbox/spin_sandbox.py      stop a tumbling satellite"
	@echo ""
	@echo "  Then: make run   (terminal 1)"
	@echo "        make demo  (terminal 2)"
	@echo ""

.PHONY: check
check: check-gen build test sil check-layering check-links check-toolbox  ## Everything CI runs
	@echo ""
	@echo "all checks passed"

# -- run ----------------------------------------------------------------------

.PHONY: run
run: build  ## Start the spacecraft (Ctrl-C to stop)
	@$(BUILD_DIR)/fsw --verbose --ttc-port $(TTC_PORT)

.PHONY: run-fast
run-fast: build  ## Start the spacecraft at ten times real time
	@$(BUILD_DIR)/fsw --verbose --time-scale 10 --ttc-port $(TTC_PORT)

# -- ground -------------------------------------------------------------------

.PHONY: demo
demo:  ## Fly a scripted pass (the spacecraft must be running)
	@cd gnd && $(SYS_PYTHON) -m pyground --port $(TTC_PORT) demo

.PHONY: monitor
monitor:  ## Watch the downlink (the spacecraft must be running)
	@cd gnd && $(SYS_PYTHON) -m pyground --port $(TTC_PORT) monitor

.PHONY: params
params:  ## Read every on-board parameter back from the spacecraft
	@cd gnd && $(SYS_PYTHON) -m pyground --port $(TTC_PORT) params

.PHONY: commands
commands:  ## List the telecommands this spacecraft accepts
	@cd gnd && $(SYS_PYTHON) -m pyground commands

# -- info ---------------------------------------------------------------------

.PHONY: stats
stats:  ## Lines of code by area
	@printf "%-28s %8s %8s\n" AREA FILES LINES
	@for d in fsw/core fsw/hal fsw/platform fsw/apps fsw/generated \
	          gnd/pyground tools tests dictionary docs; do \
		f=$$(find $$d -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.py' \
		     -o -name '*.yaml' -o -name '*.md' \) 2>/dev/null | wc -l | tr -d ' '); \
		l=$$(find $$d -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.py' \
		     -o -name '*.yaml' -o -name '*.md' \) -exec cat {} + 2>/dev/null | wc -l | tr -d ' '); \
		printf "%-28s %8s %8s\n" $$d $$f $$l; \
	done
