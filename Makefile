.PHONY: help setup build test clean format lint validate install-hooks

# Colors
BOLD := \033[1m
GREEN := \033[0;32m
YELLOW := \033[1;33m
NC := \033[0m # No Color

help: ## Show this help message
	@echo "$(BOLD)CloudFS Development Commands$(NC)"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  $(GREEN)%-20s$(NC) %s\n", $$1, $$2}'
	@echo ""

setup: ## Install all development tools
	@echo "$(BOLD)Installing development tools...$(NC)"
	@chmod +x scripts/setup_dev_tools.sh
	@scripts/setup_dev_tools.sh

validate: ## Validate development environment setup
	@chmod +x scripts/validate_dev_setup.sh
	@scripts/validate_dev_setup.sh

install-hooks: ## Install Git pre-commit hooks
	@echo "$(BOLD)Installing Git hooks...$(NC)"
	@pre-commit install --hook-type pre-commit
	@pre-commit install --hook-type commit-msg
	@echo "$(GREEN)✓ Hooks installed$(NC)"

build: ## Build the extension
	@echo "$(BOLD)Building CloudFS extension...$(NC)"
	@chmod +x scripts/build_and_test.sh
	@scripts/build_and_test.sh

test: build ## Run tests
	@echo "$(BOLD)Running tests...$(NC)"
	@cd build && ctest --output-on-failure

clean: ## Clean build artifacts
	@echo "$(BOLD)Cleaning build artifacts...$(NC)"
	@rm -rf build/
	@rm -f cloudfs.duckdb_extension
	@rm -f *.log
	@echo "$(GREEN)✓ Clean complete$(NC)"

format: ## Format all code (C++, CMake, Shell)
	@echo "$(BOLD)Formatting code...$(NC)"
	@npm run format:cpp || true
	@npm run format:cmake || true
	@echo "$(GREEN)✓ Formatting complete$(NC)"

lint: ## Run all linters
	@echo "$(BOLD)Running linters...$(NC)"
	@npm run lint || true
	@npm run check:shell || true

lint-fix: ## Run linters and auto-fix issues
	@echo "$(BOLD)Running linters with auto-fix...$(NC)"
	@pre-commit run --all-files || true
	@git diff --name-only

check-format: ## Check code formatting without modifying files
	@echo "$(BOLD)Checking code formatting...$(NC)"
	@find src -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror

commit-lint: ## Validate last commit message
	@echo "$(BOLD)Validating last commit...$(NC)"
	@npx commitlint --from=HEAD~1

deps-update: ## Update dependencies (vcpkg, npm)
	@echo "$(BOLD)Updating dependencies...$(NC)"
	@npm update
	@vcpkg update || true
	@echo "$(GREEN)✓ Dependencies updated$(NC)"

pre-commit-run: ## Run pre-commit hooks manually
	@pre-commit run --all-files

pre-commit-update: ## Update pre-commit hooks to latest versions
	@pre-commit autoupdate

check-secrets: ## Scan for secrets in code
	@echo "$(BOLD)Scanning for secrets...$(NC)"
	@detect-secrets scan

check-all: validate lint check-format check-secrets ## Run all checks
	@echo ""
	@echo "$(GREEN)$(BOLD)✓ All checks complete$(NC)"

# Development workflow targets
dev-setup: setup install-hooks validate ## Complete development environment setup
	@echo ""
	@echo "$(GREEN)$(BOLD)✓ Development environment ready!$(NC)"
	@echo ""
	@echo "Next steps:"
	@echo "  make build       # Build the extension"
	@echo "  make test        # Run tests"
	@echo "  make format      # Format code"
	@echo ""

quick-test: format lint build ## Format, lint, and build (quick validation)
	@echo ""
	@echo "$(GREEN)$(BOLD)✓ Quick test passed!$(NC)"

# Git workflow helpers
git-status: ## Show git status with formatting
	@git status --short --branch

git-log: ## Show formatted git log
	@git log --oneline --graph --decorate --all -10

# Documentation
docs: ## Open development documentation
	@if command -v xdg-open &> /dev/null; then \
		xdg-open docs/DEVELOPMENT.md; \
	elif command -v open &> /dev/null; then \
		open docs/DEVELOPMENT.md; \
	else \
		cat docs/DEVELOPMENT.md; \
	fi

.DEFAULT_GOAL := help
