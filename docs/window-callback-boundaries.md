# Window / UI callback boundaries

- Date: 2026-07-13
- Branch: `refactor/window-callback-boundaries`
- Baseline commit: `1f0795e`

## External conflict flow

External file monitor callbacks no longer open modal dialogs directly.

- `GFileMonitor` callback decides disk state, updates fingerprints, and calls `lmme_external_conflict_request()`.
- UI presentation is deferred through `g_idle_add_full` in `src/ui/external_conflict.c`.
- State machine: `LmmeExternalConflictState` (`IDLE`, `SCHEDULED`, `PRESENTING`) plus `external_change_generation` and `external_change_pending`.
- The pending idle source is owned by its `LmmeDocument`; `SCHEDULED` means that `external_conflict_source_id` identifies an active source.
- After resolving the document by ID, the idle handler clears `external_conflict_source_id` before checking whether the conflict is still current.
- If the conflict disappeared before dispatch, the handler returns the document to `IDLE` and clears `external_change_pending`; otherwise it sets `PRESENTING` before showing the dialog.
- `lmme_document_apply_external_conflict_choice()` is the test seam for reload/overwrite/keep-local without GTK.
- `Ctrl+S` routes through the conflict request path only when `disk_state != LMME_DISK_STATE_NORMAL`.

## Partial workspace delete

Recursive delete returns `LmmeWorkspaceDeleteOutcome`:

- `COMPLETE` — target removed; open tabs in the deleted subtree are forgotten.
- `PARTIAL` — some entries were removed; affected open tabs move to `EXTERNAL_DELETED`, autosave is cancelled, user sees a partial-delete dialog, tree refreshes.
- `UNCHANGED` — operation failed before durable progress.

Fault injection for tests: `src/workspace/workspace_delete_test.h`.

## Document reads and word count

- `LMME_DOCUMENT_MAX_OPEN_BYTES` = 10 MiB for normal open, reload, and session restore.
- Recovery payload reads use a separate path without the open limit.
- Oversized session-restore paths log a warning and restore continues with remaining tabs.
- `LMME_DOCUMENT_WORD_COUNT_MAX_BYTES` = 2 MiB; inactive tabs do not schedule word-count work.
- Status bar shows `— words` when `word_count_valid` is false.

## Image insertion lifecycle

All insertion entry points (dialog, drop, clipboard) share one request model in `src/features/image_insert.c`.

State machine (`LmmeImageInsertState`):

1. `PREPARING`
2. `FILE_CREATED`
3. `COMMITTED`
4. `FINISHED`

Rollback deletes `destination_path` only when:

- state is `FILE_CREATED` (not yet `COMMITTED`);
- `destination_created_by_request == TRUE`;
- the file did not exist before this operation.

After `COMMITTED`, cancel or stale requests do not delete the image file.

Document field: `image_insert_cancellable` (replaces `clipboard_cancellable`).

Test seam: `src/features/image_insert_test.h`.

## Command enabled state

- Predicate context: `LmmeCommandContext` in `src/command/command_enabled.h`.
- `lmme_command_enabled_for_handler()` is the pure test entry point.
- Every catalog entry sets `is_enabled`.
- `lmme_command_actions_refresh()` walks the catalog and calls `g_simple_action_set_enabled`.
- Refresh points: workspace open, tree selection, notebook switch, tab add/remove/close, external disk-state changes, buffer change and cursor move on the active tab, tree/tab context menus.

## Performance decisions (measured on fresh baseline)

| Area | Gate | Decision |
|------|------|----------|
| Recursive delete 10k | p95 50 ms | Stay synchronous (44.2 ms measured) |
| Word count | 2 MiB cap | Defer inactive tabs |
| Document open | 10 MiB cap | Sync read with explicit error |
| Image copy / large open | not measured post-change | No async commit in this pass |
