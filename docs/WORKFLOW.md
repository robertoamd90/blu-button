# Development Workflow

This document captures the working agreement for developing BluButton with coding agents.

## 1. Default development cycle

For normal feature or fix work:

1. implement the change cleanly
2. run target-aware validation for the current scope
3. let the user handle on-device behavioral checks unless explicitly asked to do more
4. only after validation and any user-requested review proceed with commit / PR / merge work

For non-trivial firmware changes, the default validation target is all currently supported board profiles:

- `source ~/esp/esp-idf-v6.0/export.sh && scripts/idf-target.sh esp32 build`
- `source ~/esp/esp-idf-v6.0/export.sh && scripts/idf-target.sh esp32c3-supermini build`
- `source ~/esp/esp-idf-v6.0/export.sh && scripts/idf-target.sh xiao-esp32-c3 build`

If browser installer files change, also validate:

- `node --check site/app.js`
- `bash -n scripts/package-release.sh`
- release asset names still match `config/boards.json`

Behavior changes include:

- BLE advertising logic
- identity / key / counter persistence
- button timing or gesture handling
- LED feedback behavior
- board wiring or runtime init flow
- installer metadata / Pages contract behavior

## Environment assumptions

Default local assumptions for this repo:

- ESP-IDF is installed locally
- the IDF environment can be loaded with:
  - `source ~/esp/esp-idf-v6.0/export.sh`
- hardware-level behavioral validation is normally performed by the user after flash
- this repo currently supports:
  - `esp32-devkit-v1`
  - `esp32c3-supermini`
  - `xiao-esp32-c3`

If one of these assumptions is false, say so clearly in the handoff instead of implying full validation happened.

## 2. User-directed review flow

Review-phase agents are opt-in for this repository.
Do not invoke review agents unless the user explicitly asks for them in the
current task.

Default rule:

- do not automatically start review agents after implementation
- do not automatically enter a `fix -> review -> fix -> review` loop
- if review agents were not requested, say that explicitly in the handoff

When the user explicitly asks for a review round on non-trivial work, the
default review set is three independent reviews:

- `reviewer`
  - looks for bugs, regressions, hidden risks, and missing validation
- `architect`
  - checks boundaries, ownership, layering, and fit with the intended project structure
- `simplifier`
  - looks for duplication, unnecessary branches, redundant state, and patch-on-patch complexity

When the user explicitly asks for a review round and the change significantly affects repository documentation, onboarding flow, workflow guidance, or agent instructions, also run:

- `librarian`
  - reviews documentation clarity, source-of-truth hierarchy, onboarding speed, task discoverability, actionability, and AI-agent friendliness

This section applies to review-phase agents only.
It does not impose the same response format on generic agents doing implementation or exploration work.

Stable reviewer-role definitions live in:

- `.codex/agents/reviewer.toml`
- `.codex/agents/architect.toml`
- `.codex/agents/simplifier.toml`
- `.codex/agents/librarian.toml`

Treat those files as the source of truth for reviewer identity, mandate,
forbidden actions, and output format.
When the host supports project-scoped custom subagent discovery, the review
runner should load the named subagents from those files as entry points.
The invocation prompt should add only the current review scope, objective, and
any truly local emphasis.

### Review-agent output contract

Each reviewer's exact output contract is defined in the matching
`.codex/agents/*.toml` file.
`docs/WORKFLOW.md` should be treated only as invocation and runner guidance.

### Review invocation discipline

When invoking review-phase agents, spawn the named custom subagent for the role
from `.codex/agents/` and prompt it as a single-purpose reviewer, not as a
coordinator.

The named custom subagent already carries the reviewer contract.
Do not restate the full prohibition set in the task prompt unless tooling
limitations prevent the subagent definition from being loaded.

Expected runtime support:
- the review runner must support project-scoped custom subagent discovery from `.codex/agents/`
- it must also support closing and respawning those named subagents between rounds

Fallback when that support is unavailable:
- treat the matching `.codex/agents/<role>.toml` file as the reviewer contract
- emulate the role with a generic read-only agent
- keep the task prompt limited to scope, objective, and any truly local emphasis
- say explicitly in the handoff that named custom subagent loading was unavailable for that run

Concrete fallback template for a generic read-only agent:

```text
Use `.codex/agents/<role>.toml` in this repository as the full reviewer contract.
Do not edit files, run builds, spawn agents, coordinate other reviewers, or add process commentary.

Review only this scope:
<explicit scope here>

Objective of this review round:
<explicit objective here>

Local emphasis for this round:
<optional, only when truly needed>

Return ONLY the final structured report required by `.codex/agents/<role>.toml`.
```

### Review scope selection

Always state the exact review scope in the prompt.
Do not assume the agent will infer whether you want a staged diff, branch diff, or full-repo audit.

Supported review scopes include:

- staged diff
  - `review the currently staged diff in <repo-path>`
- working-tree diff
  - `review the current uncommitted diff in <repo-path>`
- branch diff
  - `review the diff between <base-ref> and <head-ref>`
- file-scoped diff
  - `review only changes in <file-paths>`
- full repo
  - `review the current repository state in <repo-path>, not just the diff`

When asking for a full-repo review, say whether you want:

- a general code-health audit
- a contract-focused audit against docs
- an architecture audit of current boundaries

### Fresh-agent rule

For review rounds, do not reuse old review agents.

- close any previous `reviewer`, `architect`, `simplifier`, and `librarian` agents first
- spawn fresh agents for the new review round
- when the project-scoped custom subagent exists, use it instead of emulating the role with a generic agent
- do this even when the previous agents already reviewed a nearby diff

### Review prompt template

Use this base template with the relevant named custom subagent.

```text
Review only this scope:
<explicit scope here>

Objective of this review round:
<explicit objective here>

Local emphasis for this round:
<optional, only when truly needed>

Return ONLY the final structured report.
```

Host-facing reviewer selection is runtime-specific.
For example, hosts that support subagent mentions may select a reviewer with a
handle such as `[@reviewer](subagent://reviewer)`.

### Completion rule

When the user has explicitly asked for a review phase, review is not complete until:

- all user-requested agents have returned a clear, usable result
- all actionable findings that the user wants addressed in that round are fixed
- and there are no remaining obvious items to clean up within the requested scope

If one or more agents still find issues and the user asked for a review loop,
keep iterating and rerun the requested reviews.
Do not start a new review round unless the user asked for that loop or asks for
another explicit review pass.
