---
name: swarm-mcp
description: Use when swarm-mcp tools are available and the task involves joining a local swarm, discovering other agent sessions, coordinating specialists through `role:` labels, handing off work with swarm tasks or messages, or avoiding file collisions with shared locks and annotations.
metadata:
  short-description: Coordinate work through swarm-mcp
  domain: agent-coordination
  role: specialist
  scope: workflow
---

# Swarm MCP

Use this skill when the `swarm` MCP server is available in the current session and the task benefits from multi-agent coordination.

This skill assumes the swarm tools are already mounted. If they are not present, say so clearly and fall back to local work or direct setup help.

## Core Workflow

1. Bootstrap into the swarm with `register`
2. Inspect the current swarm with `whoami`, `list_instances`, `poll_messages`, and `list_tasks`
3. Check coordination risk before editing with `check_file`
4. Lock files while editing with `lock_file`
5. Delegate or coordinate with `request_task`, `send_message`, or `broadcast`
6. Leave durable context with `annotate` and small shared state with `kv_set`
7. Release locks and complete tasks with `unlock_file` and `update_task`

For planner sessions, the server maintains `owner/planner` automatically. Check it with `kv_get` to see whether you currently own planner duties.

## Task Features

- **Priority**: Tasks have an integer `priority` field (higher = more urgent). `list_tasks` returns tasks sorted by priority. Claim the highest-priority open task first.
- **Dependencies**: Tasks can have a `depends_on` field (array of task IDs). A task with unmet dependencies starts as `blocked` and auto-transitions to `open` when all deps complete. If a dependency fails, downstream tasks are auto-cancelled.
- **Approval gates**: Tasks can be set to `approval_required` status. They remain gated until approved (transitions to `open`) or rejected (transitions to `cancelled`).
- **Idempotency**: Tasks can have an `idempotency_key` field that prevents duplicate creation on retry.

## Load References As Needed

| Topic | Reference | Load When |
|-------|-----------|-----------|
| Bootstrap and registration fields | `references/bootstrap.md` | You need to decide `directory`, `scope`, `file_root`, or `label` |
| Specialists, generalists, and team conventions | `references/roles-and-teams.md` | You need to route work by `role:` or `team:` labels |

## Constraints

### Must Do

- Call `register` before using other swarm tools
- Use `whoami`, `list_instances`, `poll_messages`, and `list_tasks` early in the session
- Call `check_file` before editing and `lock_file` while editing
- Use `update_task` when you start and finish claimed work
- Treat `role:` labels as conventions, not hard schema
- Treat sessions without a `role:` label token as generalists
- Prefer the highest-priority open task when claiming work
- Include structured results (JSON with `files_changed`, `test_status`, `summary`) when completing tasks

### Must Not Do

- Assume other sessions share your exact working directory unless `scope` and `file_root` make that true
- Invent role-routing behavior that is not visible from labels, messages, tasks, or instructions
- Hold file locks longer than needed
- Use `assignee` for a stale or unknown instance
- Confuse direct messages with task handoff; use `request_task` for structured delegated work
- Try to claim `blocked` tasks — they will become `open` automatically

## Default Behavior

When the skill triggers, prefer this sequence unless the task clearly requires something else:

1. Verify the swarm tools exist
2. `register`
3. `whoami`
4. `list_instances`
5. `poll_messages`
6. `list_tasks`
7. Summarize active specialists, open work, and collision risks before taking action
8. Act on any pending work (claim tasks, respond to messages)
9. Enter an autonomous loop using `wait_for_activity` — react to messages, task changes, KV updates, and instance changes as they arrive. Do not wait for user prompting between tasks.

## Collaboration Heuristics

- Prefer `request_task` when the work should be tracked and completed
- Prefer `send_message` for targeted coordination that does not need task state
- Prefer `broadcast` for short status updates that help everyone
- Prefer `annotate` for file-specific findings another agent may need later
- Prefer a matching `role:` token when choosing a specialist
- Prefer a matching `team:` token when the swarm uses soft teams
- Fall back to any matching specialist, then to a generalist, when the ideal collaborator is unavailable
- Use `wait_for_activity` as your idle loop — it blocks until new messages, task changes, KV updates, or instance changes arrive, then returns the updates so you can act immediately
- If you are acting as a planner, watch `owner/planner` on `kv_updates` so you can resume from `plan/latest` after failover
- Update your progress with `kv_set("progress/<your-instance-id>", ...)` while working on tasks so others can check on you without interrupting
- Messages prefixed with `[auto]` are system notifications (task assignments, completions, stale-agent recovery) — treat them like any other actionable message
- When you receive a `[signal:complete]` broadcast, the planner is signaling all work is done — finish current work, deregister, and stop

## Structured Results Convention

When completing a task, prefer a JSON `result`:

```json
{
  "files_changed": ["src/foo.ts"],
  "test_status": "pass",
  "summary": "What was done and why."
}
```

Fall back to a plain string if you cannot produce structured output.
