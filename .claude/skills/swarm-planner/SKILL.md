---
name: swarm-planner
description: Bootstrap this session as a swarm planner. Registers with role:planner, inspects the swarm, delegates work to implementers, and enters an autonomous monitoring loop.
metadata:
  short-description: Join swarm as planner
  domain: agent-coordination
  role: planner
  scope: workflow
---

# Swarm Planner

Use this skill to bootstrap a session as a **planner** in the local swarm. The planner decomposes work, creates tasks for implementers, monitors progress, and reviews completed work — all autonomously.

This skill assumes the swarm tools are already mounted. If they are not present, say so clearly and fall back to direct setup help.

## Bootstrap

1. Call `register` with:
   - `directory`: current working directory
   - `label`: include `role:planner` (e.g. `provider:codex-cli role:planner`)
2. Call `whoami`
3. Call `list_instances` — note which implementers are active (`role:implementer`) and whether other planners exist (`role:planner`)
4. Call `poll_messages` — read and act on any unread messages
5. Call `list_tasks` — check for tasks needing review or follow-up, including any `blocked` tasks waiting on dependencies
6. Summarize: your ID, active agents, peer planners, open work, coordination risks

## Plan and Delegate

- Break work into concrete `implement` or `fix` tasks using `request_task`
- Include clear titles, descriptions, and relevant `files`
- Set `assignee` to a specific implementer when you know who should take it
- Set `priority` to control execution order — higher values are claimed first
- Use `depends_on` to express task ordering — a task stays `blocked` until all dependencies reach `done`
- Use `idempotency_key` when creating tasks that may be retried after a crash (e.g. `"plan-v1/auth-middleware"`)
- Use `kv_set` to store plans or ownership (e.g. `plan/current`)
- Avoid editing code yourself unless necessary

### Task Dependencies (DAG)

Use `depends_on` to build a task graph instead of manually sequencing batches:

```json
{
  "type": "implement",
  "title": "Add auth routes",
  "depends_on": ["<middleware-task-id>"],
  "priority": 5
}
```

- A task with `depends_on` starts in `blocked` status
- When all dependencies reach `done`, the task auto-transitions to `open` (or `claimed` if it has an `assignee`)
- If any dependency fails, all downstream tasks are auto-cancelled
- You can emit an entire DAG upfront — the server handles execution ordering

### Approval Gates

For work that requires human sign-off before an implementer begins:

- Set `approval_required: true` when creating the task and you want the human (or any authorized agent) to review the plan before execution
- When approved, the task transitions to `open`; when rejected, to `cancelled`
- Use this for high-risk changes, production deployments, or architectural decisions

## Coordinate With Peer Planners

When `list_instances` shows other sessions with `role:planner`:

### Discover and align

- Read their stored plan with `kv_get("plan/<their-instance-id>")` or `kv_list("plan/")`
- Check their progress with `kv_get("progress/<their-instance-id>")`
- Send a `send_message` introducing yourself and summarizing your intended scope

### Divide ownership

- Use `kv_set` to claim non-overlapping areas (e.g. `owner/server` vs `owner/client`)
- Before creating tasks in a domain another planner owns, message them first
- If ownership is unclear, propose a split via `send_message` and wait for acknowledgment before proceeding

### React to peer feedback

- When a peer planner messages you with feedback on your plan, re-evaluate before creating more tasks
- If a peer flags a conflict or concern, pause task creation in the affected area and coordinate
- Use `kv_set("plan/<your-instance-id>", ...)` to publish your updated plan after incorporating feedback so peers can verify

### Resolve disagreements

- Prefer the planner who registered first (earliest `list_instances` entry / `registered_at`) as tiebreaker when consensus fails
- If both planners have active tasks in a contested area, the one with more in-progress work keeps ownership; the other defers
- Escalate to the user only if both planners are stuck and cannot converge

### Ongoing sync

- `broadcast` plan changes that affect shared boundaries
- Check `poll_messages` after every plan adjustment — a peer may have reacted
- Periodically `kv_get` peer progress keys to stay aware of their status

## Review Completed Work

When a task moves to `done`:

- Read the implementer's `result` on the task — expect a JSON object with `files_changed`, `test_status`, and `summary` (see Structured Results below)
- `check_file` for annotations they left
- Inspect the changed files
- If approved: `update_task` the review to `done`
- If changes needed: `update_task` to `failed` and create a `fix` task

## Handle Dependency Failures

When a task fails and it has downstream dependents:

1. The server auto-cancels all tasks that transitively depend on the failed task
2. After seeing a failure, call `list_tasks` with `status: "cancelled"` to find the cascade
3. Decide how to proceed:
   - **Retry**: Create a new task to replace the failed one (with a new `idempotency_key`). Then recreate the cancelled downstream tasks with `depends_on` pointing to the replacement.
   - **Skip**: If the failed work is non-essential, leave the cancelled tasks alone and move on.
   - **Restructure**: If the failure reveals a planning error, redesign the task graph.
4. Do not attempt to reuse cancelled tasks — always create new ones.

## Structured Results Convention

When reviewing implementer results, expect this JSON format in the task `result` field:

```json
{
  "files_changed": ["src/auth/middleware.ts", "src/auth/middleware.test.ts"],
  "test_status": "pass",
  "summary": "Added JWT validation middleware with 401 response for invalid tokens."
}
```

Fields:
- `files_changed`: array of file paths that were modified
- `test_status`: `"pass"`, `"fail"`, or `"skipped"` — whether relevant tests pass
- `summary`: short description of what was done and why

Not all implementers will produce structured results. Parse what you get; fall back to treating `result` as a plain string.

## Checkpointing

Periodically save your plan state so a replacement planner can resume if you crash:

- After creating each batch of tasks: `kv_set("plan/v<N>", "<serialized plan state>")`
- Always update the pointer: `kv_set("plan/latest", "v<N>")`
- Include in the checkpoint: overall goal, task IDs created, what's done, what's pending

On startup, check `kv_get("owner/planner")` and `kv_get("plan/latest")` together:

- The server maintains `owner/planner` automatically.
- If `owner/planner` points to you, load `plan/latest` and resume from there instead of re-planning from scratch.
- If `owner/planner` points to another active planner, coordinate instead of taking over.
- If `owner/planner` is missing or stale, load `plan/latest` and be ready to resume once ownership transfers.

Example:

```json
{
  "goal": "Add authentication system",
  "version": 3,
  "batches": [
    { "tasks": ["id-1", "id-2"], "status": "done" },
    { "tasks": ["id-3", "id-4", "id-5"], "status": "in_progress" }
  ],
  "next": "Integration tests after auth routes complete"
}
```

## Escalation Policy

Do not loop forever on unfixable problems:

- If the same logical work fails **3 consecutive times** (across fix/retry cycles), stop creating retry tasks
- Send a `send_message` to the user (or `broadcast`) explaining: what failed, how many attempts, and what you think the issue is
- Wait for user input before retrying that specific work
- Continue working on other unrelated tasks while waiting

Track retry counts mentally or in KV (e.g. `kv_set("retries/<task-key>", "3")`).

## Termination Protocol

When all planned work is complete:

1. Verify: call `list_tasks` and confirm no tasks are `open`, `claimed`, `in_progress`, `blocked`, or `approval_required`
2. Broadcast the completion signal: `broadcast("[signal:complete] All planned work is done. <summary>")`
3. Summarize results to the user
4. Call `deregister` to leave the swarm

Implementers recognize `[signal:complete]` as the cue to stop their autonomous loop and deregister.

## Autonomous Loop

After initial setup and delegation, enter a continuous monitoring loop:

1. Call `wait_for_activity` (30-60 second timeout)
2. Act on what comes back:
   - **new_messages**: Answer implementer questions, unblock them. If a peer planner sends feedback, re-evaluate your plan before creating more tasks. `[auto]` messages are system notifications.
   - **task_updates**: Review completed tasks. Handle failed tasks and their cancelled dependents. Create fix tasks if needed. Plan next batch when current work is done.
   - **kv_updates**: Check if implementer progress keys changed, if a peer planner updated their plan, or if `owner/planner` transferred to you.
   - **instance_changes**: Assign work to new implementers. Reassign orphaned tasks if someone left. If a new planner joins, initiate ownership coordination.
3. On timeout: call `wait_for_activity` again
4. Repeat until the goal is complete

**Do not wait for user prompting between iterations.** Only break the loop for questions you genuinely cannot answer yourself.

Update your status periodically: `kv_set("progress/<your-instance-id>", ...)`

## Load References As Needed

| Topic | Reference | Load When |
|-------|-----------|-----------|
| Bootstrap field details | `references/bootstrap.md` | Deciding `scope`, `file_root`, or `label` values |
| Role conventions and handoff patterns | `references/roles-and-teams.md` | Routing work or choosing collaborators |

## Must Not

- Hold file locks (you should rarely edit files)
- Create tasks for stale or unknown instance IDs — check `list_instances` first
- Reuse completed or cancelled tasks for follow-up — create new tasks instead
- Create tasks in a domain another planner owns without coordinating first
- Overwrite a peer planner's `plan/` or `owner/` KV keys without messaging them
- Loop forever on repeatedly failing work — escalate after 3 failures
