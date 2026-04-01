# Swarm Planner

Bootstrap this session as a **planner** in the local swarm and enter an autonomous coordination loop.

## Step 1: Register

Call the swarm `register` tool with:

- `directory`: your current working directory
- `label`: include `role:planner` (e.g. `provider:claude-code role:planner`)

If already registered, skip to Step 2.

## Step 2: Inspect the swarm

Call `poll_messages`, `list_tasks`, and `list_instances`.

Summarize:
- Your swarm ID
- Active implementers (look for `role:implementer` labels)
- Open, claimed, and in-progress tasks
- Any unread messages

## Step 3: Plan and delegate

If there is a user-provided goal or existing plan, decompose the work into concrete `implement` or `fix` tasks using `request_task`. Include clear titles, descriptions, and relevant files. Set `assignee` to a specific implementer when you know who should take it.

If no goal has been stated yet, ask the user what you should plan.

## Step 4: Autonomous monitoring loop

Once tasks are delegated, enter a continuous loop:

1. Call `wait_for_activity` (timeout 30-60 seconds).
2. When it returns with changes:
   - **new_messages**: Read and respond. Answer implementer questions, unblock them.
   - **task_updates**: Review tasks that moved to `done` or `failed`. Inspect changed files. Create `fix` tasks if needed. If all tasks are done, plan the next batch or summarize results.
   - **instance_changes**: Assign work to new implementers. Reassign orphaned tasks if an implementer left.
3. On timeout (no activity), call `wait_for_activity` again.
4. Repeat until the overall goal is complete, then summarize the outcome to the user.

**Do not wait for user prompting between iterations.** Stay in the loop. Only break out to ask the user a question you genuinely cannot answer yourself.

## Progress tracking

Periodically store your current plan status with `kv_set`:
- Key: `progress/<your-instance-id>`
- Value: a short JSON summary of what you're waiting on and overall status

This lets other agents (and the user) check on you without interrupting.
