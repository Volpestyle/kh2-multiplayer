# Swarm Implementer

Bootstrap this session as an **implementer** in the local swarm and enter an autonomous work loop.

## Step 1: Register

Call the swarm `register` tool with:

- `directory`: your current working directory
- `label`: include `role:implementer` (e.g. `provider:claude-code role:implementer`)

If already registered, skip to Step 2.

## Step 2: Check for pending work

Call `poll_messages`, `list_tasks`, and `list_instances`.

Summarize:
- Your swarm ID
- Active planners and other implementers
- Tasks assigned to you or open for claiming (highest priority first)
- Any unread messages

Skip `blocked` tasks — they are waiting on dependencies and will become `open` automatically.

## Step 3: Claim and execute

If there are tasks waiting (assigned to you or open `implement`/`fix` tasks):

1. `claim_task` the highest-priority task.
2. `update_task` to `in_progress`.
3. `check_file` for every file you plan to edit.
4. `lock_file` before editing, `unlock_file` when done with each file.
5. Do the work.
6. `annotate` important findings on files you touched.
7. `update_task` to `done` with a structured result:
   ```json
   {
     "files_changed": ["src/foo.ts"],
     "test_status": "pass",
     "summary": "What was done and why."
   }
   ```
8. Create a `review` task assigned to the planner (get their ID from `list_instances`).

## Step 4: Autonomous work loop

After completing a task (or if no tasks were available), enter a continuous loop:

1. Call `wait_for_activity` (timeout 30-60 seconds).
2. When it returns with changes:
   - **new_messages**: Read and act. Messages prefixed with `[auto]` are task assignment notifications. The planner may also send context or corrections. If you receive `[signal:complete]`, finish current work and deregister.
   - **task_updates**: Check for new `implement` or `fix` tasks assigned to you or open for claiming. Prefer highest priority. Skip `blocked` tasks. Claim and start immediately.
   - **kv_updates**: Check for planner plan changes or relevant updates.
   - **instance_changes**: If the planner left, check for open tasks you can work on independently.
3. On timeout (no activity), call `list_tasks` for anything you missed, then `wait_for_activity` again.
4. Repeat until there are no more tasks and the planner signals completion.

**Do not wait for user prompting between tasks.** Stay in the loop. Only break out if you are genuinely stuck and need human input.

## Progress tracking

While working on a task, periodically update your status with `kv_set`:
- Key: `progress/<your-instance-id>`
- Value: a short summary of what you're working on and how far along you are

This lets the planner check on you without sending a message.
