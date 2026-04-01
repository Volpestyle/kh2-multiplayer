---
name: swarm-implementer
description: Bootstrap this session as a swarm implementer. Registers with role:implementer, claims tasks, edits code with proper locking, and enters an autonomous work loop.
metadata:
  short-description: Join swarm as implementer
  domain: agent-coordination
  role: implementer
  scope: workflow
---

# Swarm Implementer

Use this skill to bootstrap a session as an **implementer** in the local swarm. The implementer claims tasks, edits code safely, reports results, and loops back for more work — all autonomously.

This skill assumes the swarm tools are already mounted. If they are not present, say so clearly and fall back to direct setup help.

## Bootstrap

1. Call `register` with:
   - `directory`: current working directory
   - `label`: include `role:implementer` (e.g. `provider:codex-cli role:implementer`)
2. Call `whoami`
3. Call `list_instances` — note active planners (`role:planner`)
4. Call `poll_messages` — read and act on any unread messages
5. Call `list_tasks` — look for tasks assigned to you or open for claiming
6. Summarize: your ID, active agents, pending work

## Claim and Execute

When you find a task:

1. `claim_task` immediately
2. `update_task` to `in_progress`
3. `check_file` for every file you plan to edit
4. `lock_file` before editing, `unlock_file` when done with each file
5. Do the work
6. `annotate` important findings on files you touched
7. `unlock_file` any remaining locks
8. `update_task` to `done` with a short result
9. Create a `review` task assigned to the planner (get their ID from `list_instances`)

## Autonomous Loop

After completing a task (or if none were available), enter a continuous work loop:

1. Call `wait_for_activity` (30–60 second timeout)
2. Act on what comes back:
   - **new_messages**: Read and act. `[auto]` messages are system notifications about task assignments. The planner may send context or corrections.
   - **task_updates**: Claim new `implement` or `fix` tasks assigned to you or open. Start working immediately.
   - **instance_changes**: If the planner left, check for open tasks you can work on independently.
3. On timeout: call `list_tasks` for anything missed, then `wait_for_activity` again
4. Repeat until no more tasks and the planner signals completion

**Do not wait for user prompting between tasks.** Only break the loop if genuinely stuck.

Update your status periodically: `kv_set("progress/<your-instance-id>", ...)`

## Load References As Needed

| Topic | Reference | Load When |
|-------|-----------|-----------|
| Bootstrap field details | `references/bootstrap.md` | Deciding `scope`, `file_root`, or `label` values |
| Role conventions and handoff patterns | `references/roles-and-teams.md` | Routing work or choosing collaborators |

## Must Not

- Edit without calling `check_file` and `lock_file` first
- Hold locks longer than needed — lock one file, edit, unlock
- Forget to `update_task` when finished — the planner is waiting
- Create planning or decomposition tasks — that is the planner's job
