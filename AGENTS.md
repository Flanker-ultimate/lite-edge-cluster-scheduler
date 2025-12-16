Repository Guidelines

## Project Structure & Module Organization
- Core scheduler logic lives in `src/`, with cluster orchestration components under `src/cluster/` and scheduling strategies in `src/scheduler/`.
- CLI or service entrypoints typically sit in `cmd/` (Go) or `bin/` (scripts). Shared utilities and types are in `pkg/` or `internal/` depending on visibility needs.
- Tests mirror the source tree under `src/` using `_test.go` files. Fixtures or sample manifests live in `testdata/`.

## Build, Test, and Development Commands
- `make build` – compile binaries and prepare artifacts for release.
- `make test` – run unit tests with race detection and module tidy checks.
- `go test ./...` – quick full-suite run when you don’t need the Makefile wrappers.
- `make lint` – run linters/formatters (gofmt, golangci-lint) to enforce style.
- `make run` or `go run ./cmd/<entry>` – run a service locally; point configs to local `configs/`.

## Coding Style & Naming Conventions
- Go code uses `gofmt` defaults and `golangci-lint` rules; prefer small, focused functions.
- Names: packages are lower_snake; exported identifiers are CamelCase with clear nouns; errors use `Err...`.
- Config/manifests in `configs/` follow kube-style YAML; keep keys lowercase and descriptive.
- Keep files ASCII; document public behavior with short doc comments above exported items.

## Testing Guidelines
- Use Go testing with table-driven cases; place `_test.go` beside implementation.
- Aim for coverage on scheduler decisions and cluster state transitions; include edge cases for resource exhaustion and preemption.
- Name tests `Test<Component>_<Behavior>`; prefer helpers in `testdata/` for fixtures.
- `go test -race ./...` before PR; add benchmarks (`Benchmark...`) for hot paths when relevant.

## Commit & Pull Request Guidelines
- Commits: short imperative subject (max ~72 chars), body explaining rationale and impacts. Example: `fix: guard nil node labels in scorer`.
- PRs: include summary, validation steps (`make test`, `go test -race ./...`), linked issues, and relevant logs/metrics. Add screenshots if UI/CLI output changes.
- Keep PRs scoped; prefer smaller, reviewable changes. Update docs/configs when behavior shifts.

## Security & Configuration Tips
- Do not commit secrets; use env vars and sample placeholders in `configs/`.
- Validate input from manifests and API clients; fail closed on unknown fields.
- Review RBAC and cluster permissions before running against real clusters.
