# AGENTS.md

Repository-wide instructions for coding agents working on BluButton.

## Read this first

Before doing non-trivial work, read:

- `README.md`
- `docs/WORKFLOW.md`
- `docs/COMPATIBILITY_NOTES.md`
- `docs/PAGES_INSTALLER_CONTRACT.md`
- `docs/BOOTSTRAP_PLAN.md`

For project-scoped Codex runtime settings, also read:

- `.codex/config.toml`

For review-phase Codex subagent work, also read:

- `.codex/agents/*.toml`

## Core intent

BluButton is a sibling project of BluButtonBridge.
The device must look indistinguishable from a Shelly BLU Button to BluButtonBridge.

## Document roles

Use the docs with these responsibilities in mind:

- `AGENTS.md`
  - repo policy and agent guardrails
- `README.md`
  - operator-facing overview, setup, and terminology
- `docs/WORKFLOW.md`
  - development cycle, review flow, and review-agent invocation contract
- `.codex/config.toml`
  - project-scoped subagent runtime settings
- `.codex/agents/*.toml`
  - source of truth for reviewer identity, mandate, forbidden actions, and output contract
- `docs/COMPATIBILITY_NOTES.md`
  - compatibility-sensitive BLE contract and identity assumptions
- `docs/PAGES_INSTALLER_CONTRACT.md`
  - source of truth for GitHub Pages installer payload, metadata, and browser install behavior
- `docs/BOOTSTRAP_PLAN.md`
  - current v0 scope, implemented slices, and remaining follow-up work

If documents overlap:

- `docs/WORKFLOW.md` wins for development/review process and review-agent expectations
- `.codex/config.toml` is runtime configuration only and does not participate in prose-doc conflict resolution
- `.codex/agents/*.toml` win for reviewer-role definition and reviewer output contract
- `docs/COMPATIBILITY_NOTES.md` wins for BLE payload and identity expectations
- `docs/PAGES_INSTALLER_CONTRACT.md` wins for release asset naming, mirrored metadata, and browser install behavior
- `README.md` wins for user-facing setup instructions and terminology
- `AGENTS.md` wins for agent process and repository policy

## Core rules

- Keep the normal runtime battery-friendly even during the USB-powered prototype phase.
- Treat BLE compatibility with the existing bridge as a hard contract.
- Prefer serial-based maintenance over adding on-device web or Wi-Fi features early.
- External browser installers hosted on GitHub Pages are allowed when they stay separate from the device runtime.
- Keep `app_main()` linear and lightweight.
- Put board-specific wiring in `components/board_config`.
- Put BLE payload and crypto logic in a dedicated transmitter module, not in bootstrap code.

## Early-phase scope guardrails

- Do not add web UI in the initial bootstrap phase.
- Do not add Wi-Fi or AP mode unless a later design decision explicitly calls for it.
- Do not regenerate the AES key as part of normal button interaction.
- A 10-second hold may be used for maintenance behavior such as reprinting device credentials.

## Validation

For non-trivial work, prefer target-aware builds for both currently supported board profiles before calling work complete.

When browser installer files change, also validate:

- `node --check site/app.js`
- `bash -n scripts/package-release.sh`
- the generated release asset names still match `config/boards.json`

## Mandatory multi-agent review

For non-trivial work, run all three code reviews:

- `reviewer`
- `architect`
- `simplifier`

When the change significantly affects repository documentation, onboarding flow,
workflow guidance, or agent instructions, also run:

- `librarian`

This requirement is specifically for the review phase.
It does not redefine how generic work agents should be prompted for normal
implementation, exploration, or research tasks.

The operational custom subagent definitions for review roles live in:

- `.codex/agents/reviewer.toml`
- `.codex/agents/architect.toml`
- `.codex/agents/simplifier.toml`
- `.codex/agents/librarian.toml`

Treat `.codex/agents/*.toml` as the source of truth for reviewer identity,
mandate, forbidden actions, and output format.

When invoking these review agents, follow the review invocation contract in
`docs/WORKFLOW.md` and keep `AGENTS.md` at the policy level only.

Review is not complete until:

- all required agents have produced a clear, usable outcome
- all actionable findings are fixed
- and no obvious cleanup remains

If an agent still finds issues, keep iterating and rerun the reviews.
If tooling is unstable, say that explicitly instead of claiming review is complete.
