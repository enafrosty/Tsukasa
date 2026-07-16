/*
 * exec.h - Minimal executable resolver for Phase 2 process lifecycle.
 */

#ifndef TSUKASA_EXEC_H
#define TSUKASA_EXEC_H

typedef void (*exec_entry_t)(void);

int exec_register_builtin(const char *path, exec_entry_t entry);
int exec_resolve_builtin(const char *path, exec_entry_t *entry_out);

#endif /* TSUKASA_EXEC_H */

