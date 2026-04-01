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
3. Call `list_instances` — note which implementers are active (`role:implementer`)
4. Call `poll_messages` — read and act on any unread messages
5. Call `list_tasks` — check for tasks needing review or follow-up
6. Summarize: your ID, active agents, open work, coordination risks

## Plan and Delegate

- Break work into concrete `implement` or `fix` tasks using `request_task`
- Include clear titles, descriptions, and relevant `files`
- Set `assignee` to a specific implementer when you know who should take it
- Use `kv_set` to store plans or ownership (e.g. `plan/current`)
- Avoid editing code yourself unless necessary

## Review Completed Work

When a task moves to `done`:

- Read the implementer's `result` on the task
- `check_file` for annotations they left
- Inspect the changed files
- If approved: `update_task` the review to `done`
- If changes needed: `update_task` to `failed` and create a `fix` task

## Autonomous Loop

After initial setup and delegation, enter a continuous monitoring loop:

1. Call `wait_for_activity` (30–60 second timeout)
2. Act on what comes back:
   - **new_messages**: Answer implementer questions, unblock them. `[auto]` messages are system notifications.
   - **task_updates**: Review completed tasks. Create fix tasks if needed. Plan next batch when current work is done.
   - **instance_changes**: Assign work to new implementers. Reassign orphaned tasks if someone left.
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
- Reuse completed tasks for follow-up — create new tasks instead
