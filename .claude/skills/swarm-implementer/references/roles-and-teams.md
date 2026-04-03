# Roles And Teams

`swarm-mcp` does not have first-class `role` or `team` fields.

Use label conventions instead.

## Recommended Role Tokens

- `role:planner`
- `role:implementer`
- `role:reviewer`
- `role:researcher`

Treat a session without `role:` as a generalist.

## Recommended Team Tokens

- `team:frontend`
- `team:api`
- `team:platform`

Use `scope` for hard separation and `team:` labels for soft grouping inside one swarm.

## Recommended Meanings

### Planner

- decomposes work into task DAGs with `depends_on` and `priority`
- creates tasks with `idempotency_key` for crash-safe retries
- coordinates specialists
- coordinates with peer planners when multiple planners share a scope
- keeps small plans or ownership notes in `kv_set`
- checkpoints plan state to KV for crash recovery
- handles dependency failure cascades (auto-cancelled downstream tasks)
- escalates to the user after 3 consecutive failures on the same work
- broadcasts `[signal:complete]` when all work is done
- usually avoids being the primary editor

### Implementer

- claims the highest-priority open task first
- skips `blocked` tasks (they auto-unblock when dependencies complete)
- edits files using the check/lock/edit/annotate/unlock cycle
- reports structured results: `{ files_changed, test_status, summary }`
- recognizes `[signal:complete]` as the cue to finish and deregister

### Reviewer

- inspects risks and correctness
- reviews touched files and annotations
- expects structured results from implementers when available
- requests fixes or follow-up tasks when needed

### Researcher

- investigates code, docs, APIs, or runtime facts
- summarizes findings for planners or implementers

### Generalist

- handles overflow or mixed work
- fills gaps when no specialist is available

## Selection Heuristic

When choosing collaborators:

1. Prefer a same-team matching specialist if the swarm uses `team:` labels
2. Otherwise prefer any matching specialist by `role:`
3. Fall back to a generalist when no specialist is active

## Handoff Patterns

Use `request_task` for tracked work. Use `assignee` only when you know the target instance is active in the same scope. Use `priority` to control execution order. Use `depends_on` to express task ordering.

### Three-session: planner + implementer + reviewer

Planner creates `implement` task -> implementer does the work -> planner or reviewer creates `review` task -> reviewer approves or requests fixes.

### Two-session: planner also reviews

The planner handles both planning and review. The implementer sends `review` tasks back to the planner by `assignee` instance ID.

1. Planner creates `implement` task
2. Implementer claims, does the work, marks `done` with structured result
3. Implementer creates `review` task with `assignee` set to the planner's instance ID
4. Planner reviews, approves (`done`) or rejects (`failed` + new `fix` task back to implementer)

### DAG workflow

The planner emits an entire dependency graph upfront:

1. Planner creates multiple tasks with `depends_on` relationships and `priority` values
2. Tasks without dependencies start as `open`; tasks with dependencies start as `blocked`
3. Implementers claim and complete `open` tasks
4. As tasks complete, blocked downstream tasks auto-transition to `open`
5. If a task fails, all downstream dependents are auto-cancelled
6. The planner handles the cascade: retry, skip, or restructure

### Multi-planner: peer coordination

When two or more planners share a scope, they must coordinate to avoid conflicting task creation and plan overwrites.

1. On bootstrap, each planner checks `list_instances` for other `role:planner` sessions
2. Planners divide ownership using `kv_set` (e.g. `owner/server`, `owner/client`) and `send_message` to confirm
3. Each planner publishes their plan to `kv_set("plan/<instance-id>", ...)` so peers can read it
4. Before creating tasks in another planner's domain, message them and wait for acknowledgment
5. When receiving feedback from a peer planner, re-evaluate the plan before creating more tasks
6. Use `broadcast` for plan changes that affect shared boundaries
7. Tiebreaker: the planner who registered first (earliest `list_instances` entry / `registered_at`) wins contested areas if consensus fails

### Cross-team handoff

When teams share one scope, any session can create tasks for sessions on another team.

- Prefer assigning cross-team work to the other team's planner so they can decompose it
- For small cross-team requests, assign directly to an implementer
- Use `send_message` for context that doesn't fit in the task description

### Termination

The planner broadcasts `[signal:complete]` when all work is done. Implementers recognize this signal, finish current work, and deregister.

### Example payloads

Implementation with priority:

```json
{
  "type": "implement",
  "title": "Add retry logic to API client",
  "description": "Handle transient 429 and 503 responses in src/api/client.ts.",
  "files": ["src/api/client.ts", "src/api/client.test.ts"],
  "priority": 10
}
```

Dependent task:

```json
{
  "type": "test",
  "title": "Integration tests for retry logic",
  "depends_on": ["<retry-task-id>"],
  "priority": 5
}
```

Review back to planner:

```json
{
  "type": "review",
  "title": "Review retry logic change",
  "description": "Check for retry loops and missing coverage.",
  "files": ["src/api/client.ts", "src/api/client.test.ts"],
  "assignee": "<planner-instance-id>"
}
```

Structured result:

```json
{
  "files_changed": ["src/api/client.ts", "src/api/client.test.ts"],
  "test_status": "pass",
  "summary": "Added exponential backoff retry for 429/503 with max 3 attempts."
}
```
