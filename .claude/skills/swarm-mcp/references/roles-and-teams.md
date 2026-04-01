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

- decomposes work
- creates tasks
- coordinates specialists
- keeps small plans or ownership notes in `kv_set`
- usually avoids being the primary editor

### Implementer

- claims implementation or fix work
- edits files
- uses locks and annotations carefully
- reports concrete results in `update_task`

### Reviewer

- inspects risks and correctness
- reviews touched files and annotations
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

Use `request_task` for tracked work. Use `assignee` only when you know the target instance is active in the same scope.

### Three-session: planner + implementer + reviewer

Planner creates `implement` task -> implementer does the work -> planner or reviewer creates `review` task -> reviewer approves or requests fixes.

### Two-session: planner also reviews

The planner handles both planning and review. The implementer sends `review` tasks back to the planner by `assignee` instance ID.

1. Planner creates `implement` task
2. Implementer claims, does the work, marks `done`
3. Implementer creates `review` task with `assignee` set to the planner's instance ID
4. Planner reviews, approves (`done`) or rejects (`failed` + new `fix` task back to implementer)

### Cross-team handoff

When teams share one scope, any session can create tasks for sessions on another team.

- Prefer assigning cross-team work to the other team's planner so they can decompose it
- For small cross-team requests, assign directly to an implementer
- Use `send_message` for context that doesn't fit in the task description

### Example payloads

Implementation:

```json
{
  "type": "implement",
  "title": "Add retry logic to API client",
  "description": "Handle transient 429 and 503 responses in src/api/client.ts.",
  "files": ["src/api/client.ts", "src/api/client.test.ts"]
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
