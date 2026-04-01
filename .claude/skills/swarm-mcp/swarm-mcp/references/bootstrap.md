# Bootstrap

Use this reference when a session needs to join the swarm correctly.

## Register First

Call `register` before any other swarm tool.

Minimum payload:

```json
{
  "directory": "C:\\repo"
}
```

Useful full payload:

```json
{
  "directory": "C:\\repo",
  "scope": "C:\\repo",
  "file_root": "C:\\repo",
  "label": "provider:codex-cli role:implementer"
}
```

## Field Meanings

### `directory`

The live working directory for the current session.

### `scope`

The shared swarm boundary.

- Sessions in the same scope can see each other, message each other, and share tasks, locks, annotations, and KV.
- Sessions in different scopes are different swarms.

If omitted, the server defaults to the detected git root, or the provided directory when no git root exists.

### `file_root`

The canonical base path used for resolving relative file paths.

Use it when:

- the session is running in a disposable worktree
- multiple worktrees should share one logical file tree
- locks and annotations should point at a stable checkout instead of a temp path

If you are working directly in the shared checkout, `file_root` can usually match `directory`.

### `label`

Free-form identity text for the session.

Recommended convention:

- `provider:codex-cli role:planner`
- `provider:codex-cli role:implementer`
- `provider:claude-code role:reviewer`

The `role:` token is optional. No `role:` token means the session is a generalist.

## First Read After Register

After `register`, call:

- `whoami`
- `list_instances`
- `poll_messages`
- `list_tasks`

Before editing, also call:

- `check_file`

That gives you current identity, other active sessions, unread coordination requests, open work, and file-level risks.
