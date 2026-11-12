# agents/

Agent-facing context for this repository. **Read every file in this folder before
starting any work here.**

| file | what it is |
|---|---|
| `next-agent-prompt.md` | the current handoff: what to do next, the state, and which plan to read |
| `working-notes.md` | standing build/test commands, constraints, and gotchas for every task |

`AGENTS.md` at the repository root points here, so this folder is the first thing an
agent should read. It is maintained by whoever hands off the work: update
`next-agent-prompt.md` when the current task changes, and `working-notes.md` when a
rule or command changes.

This folder is separate from `docs/superpowers/` (the plans/specs) — keep
agent-facing handoff material here so it is easy to find.
