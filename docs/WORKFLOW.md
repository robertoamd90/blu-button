# Development Workflow

This document captures the working agreement for developing BluButton with coding agents.

## 1. Default development cycle

For normal feature or fix work:

1. implement the change cleanly
2. run target-aware validation for the current scope
3. let the user handle on-device behavioral checks unless explicitly asked to do more
4. only after validation and review proceed with commit / PR / merge work

For non-trivial firmware changes, the default validation target is both currently supported board profiles:

- `source ~/esp/esp-idf/export.sh && scripts/idf-target.sh esp32 build`
- `source ~/esp/esp-idf/export.sh && scripts/idf-target.sh esp32c3 build`

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
  - `source ~/esp/esp-idf/export.sh`
- hardware-level behavioral validation is normally performed by the user after flash
- this repo currently supports:
  - `esp32-devkit-v1`
  - `esp32c3-supermini`

If one of these assumptions is false, say so clearly in the handoff instead of implying full validation happened.

## 2. Multi-agent review flow

Non-trivial work must go through three independent reviews:

- `reviewer`
  - looks for bugs, regressions, hidden risks, and missing validation
- `architect`
  - checks boundaries, ownership, layering, and fit with the intended project structure
- `simplifier`
  - looks for duplication, unnecessary branches, redundant state, and patch-on-patch complexity

When the change significantly affects repository documentation, onboarding flow, workflow guidance, or agent instructions, also run:

- `librarian`
  - reviews documentation clarity, source-of-truth hierarchy, onboarding speed, task discoverability, actionability, and AI-agent friendliness

This section applies to review-phase agents only.
It does not impose the same response format on generic agents doing implementation or exploration work.

### Review-agent output contract

Invoke each review agent with a request for structured output.
Free-form feedback is not sufficient unless it is still clearly organized into the required sections below.

Minimum shared rules for all review agents:

- keep findings practical and actionable
- cite specific files when pointing at an issue
- prefer severity-labelled findings when reporting problems
- say explicitly when there are no material findings
- avoid generic praise or vague approval without a verdict

Required sections for every review agent:

- `VERDICT`
  - one of:
    - `APPROVE`
    - `APPROVE WITH NOTES`
    - `CHANGES REQUESTED`
- `FINDINGS`
  - ordered by severity
  - each finding should use `HIGH`, `MEDIUM`, or `LOW`
  - each finding should explain the concrete risk or cleanup needed
- `OPEN QUESTIONS`
  - optional when there are no open questions

If there are no material findings, require this explicitly:

- `VERDICT`
- `FINDINGS`
  - `NO MATERIAL FINDINGS`

### Review invocation discipline

When invoking review-phase agents, prompt them as single-purpose reviewers, not as coordinators.

Each review agent should be told explicitly:

- it is responsible only for its own role
- it must not coordinate or restate the multi-agent workflow
- it must not spawn or suggest other review agents
- it must not discuss tool instability unless it truly cannot inspect the requested scope
- it should return only the structured review result

Recommended wording:

- `You are ONLY the <role> reviewer for this change.`
- `Review only the requested scope.`
- `Do not coordinate other reviewers.`
- `Do not spawn or suggest sub-agents.`
- `Do not discuss the review process.`
- `Return only the final structured report.`

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
- do this even when the previous agents already reviewed a nearby diff

### Review prompt template

Use this base template and swap in the role-specific focus and requested scope.

```text
You are ONLY the `<role>` reviewer for this change.

Review only this scope:
<explicit scope here>

Do NOT coordinate other reviewers.
Do NOT spawn or suggest sub-agents.
Do NOT discuss the review process.
Do NOT give implementation plans.
Return ONLY the final structured report.

Focus only on:
<role-specific focus here>

Use file-specific evidence from the requested scope.

Required output:

VERDICT
<APPROVE|APPROVE WITH NOTES|CHANGES REQUESTED>

FINDINGS
- <HIGH|MEDIUM|LOW>: <finding with concrete risk and file reference>
- If there are no material findings, write exactly: NO MATERIAL FINDINGS

TOP STRENGTHS
- <optional for most review roles; required for librarian>

OPEN QUESTIONS
- <optional>
```

### Role-specific expectations

`reviewer` should focus on:

- bugs
- regressions
- edge cases
- missing validation

`architect` should focus on:

- module boundaries
- ownership
- layering
- whether the change fits the intended project structure

`simplifier` should focus on:

- duplication
- unnecessary branches or flags
- redundant state
- ways to reduce patch-on-patch complexity

`librarian` should focus on:

- clarity of documentation
- source-of-truth hierarchy
- onboarding speed for a new coding agent
- task-to-doc discoverability
- actionability of instructions
- AI-agent friendliness
- ambiguity or stale-guidance risk

For `librarian`, extend the structured output with:

- `TOP STRENGTHS`
  - 1-3 short points on what the documentation set does especially well

### Completion rule

Review is not complete until:

- all required agents have returned a clear, usable result
- all actionable findings are fixed
- and there are no remaining obvious items to clean up

If one or more agents still find issues, keep iterating and rerun the reviews.
Do not stop at “good enough” if the agents are still pointing at real work to do.
