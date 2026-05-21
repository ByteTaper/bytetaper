FORMAT_FILES := $(shell find src include tests -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cc' -o -name '*.cpp' \))
DOCKER_COMPOSE ?= docker compose
LOCAL_UID ?= $(shell id -u)
LOCAL_GID ?= $(shell id -g)
HOST_DOCKER_SOCK ?= $(shell python3 -c "import os; print(os.path.realpath('/var/run/docker.sock'))" 2>/dev/null || echo "/var/run/docker.sock")
COMPOSE_ENV := LOCAL_UID=$(LOCAL_UID) LOCAL_GID=$(LOCAL_GID) HOST_DOCKER_SOCK=$(HOST_DOCKER_SOCK)

.PHONY: format test smoke-test test-control-plane-unit test-control-plane-integration test-control-plane-compose
format:
	@if [ -z "$(FORMAT_FILES)" ]; then \
		echo "No C++ files found to format."; \
	else \
		clang-format -i $(FORMAT_FILES); \
		echo "Formatted $(words $(FORMAT_FILES)) file(s)."; \
	fi

test:
	@rm -rf build
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-unit-test

smoke-test:
	@rm -rf build
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-smoke-test

test-control-plane-unit:
	@./scripts/test/control-plane-unit.sh

test-control-plane-integration:
	@./scripts/test/control-plane-integration.sh

test-control-plane-compose:
	@DOCKER_COMPOSE="$(DOCKER_COMPOSE)" ./scripts/test/control-plane-compose.sh

benchmark:
	@rm -rf build
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) build bytetaper-dev
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-benchmark

policy-allowlist-test:
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm -e BYTETAPER_E2E_PHASE=1 bytetaper-policy-allowlist-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) restart bytetaper-extproc
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm -e BYTETAPER_E2E_PHASE=2 bytetaper-policy-allowlist-test

all-tests:
	@rm -rf build
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) build bytetaper-dev
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-unit-test
	@$(MAKE) policy-allowlist-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-cache-hit-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-cache-e2e-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-smoke-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-integration-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-pagination-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-max-limit-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-compression-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-coalescing-burst-test
	@$(COMPOSE_ENV) $(DOCKER_COMPOSE) run --rm bytetaper-coalescing-e2e-test
