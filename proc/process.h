/*
 * process.h - x86_64 process model and preemptive scheduler core.
 */

#ifndef TSUKASA_PROCESS_H
#define TSUKASA_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#include "../mm/vm_space.h"

#define PROCESS_MAX_COUNT      256
#define PROCESS_MAX            PROCESS_MAX_COUNT
#define PROCESS_NAME_MAX       48
#define PROCESS_CMDLINE_MAX    256
#define PROCESS_STACK_SIZE     (32 * 1024)
#define PROCESS_USER_STACK_SIZE (8 * 1024 * 1024)
#define PROCESS_MAX_SIGNALS    64
#define PROCESS_MAX_CHILDREN   32
#define PROCESS_CWD_MAX        256
#define PROCESS_MAX_OPEN_FILES 32
#define PROCESS_PRIORITY_LEVELS 256
#define PROCESS_DEFAULT_PRIORITY 128
#define PROCESS_DEFAULT_TIMESLICE 4

#define PROCESS_WAIT_WNOHANG   0x1

#define PROCESS_SIG_DFL        ((uintptr_t)0)
#define PROCESS_SIG_IGN        ((uintptr_t)1)

#define PROCESS_SIGINT         2
#define PROCESS_SIGKILL        9
#define PROCESS_SIGUSR1        10

typedef enum process_state {
    PROCESS_CREATED = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_SLEEPING = 4,
    PROCESS_STOPPED = 5,
    PROCESS_ZOMBIE = 6,
    PROCESS_DEAD = 7,
} process_state_t;

typedef void (*process_entry_t)(void);
typedef void (*process_signal_handler_t)(int sig);
typedef process_signal_handler_t signal_handler_t;

typedef enum signal_action {
    SIGNAL_ACTION_DEFAULT = 0,
    SIGNAL_ACTION_IGNORE = 1,
    SIGNAL_ACTION_HANDLER = 2,
} signal_action_t;

typedef struct signal_action_slot {
    signal_handler_t handler;
    uint64_t mask;
    int flags;
    signal_action_t action;
} signal_action_s;

typedef enum thread_state {
    THREAD_CREATED = 0,
    THREAD_READY = 1,
    THREAD_RUNNING = 2,
    THREAD_BLOCKED = 3,
    THREAD_SLEEPING = 4,
    THREAD_STOPPED = 5,
    THREAD_ZOMBIE = 6,
    THREAD_DEAD = 7,
} thread_state_t;

typedef struct process process_t;
typedef struct thread thread_t;

typedef struct fd_entry {
    int fd;
    int flags;
    uint64_t offset;
    int file_desc;
} fd_entry_t;

struct thread {
    uint32_t tid;
    uint32_t cpu_id;
    thread_state_t state;
    uint32_t priority;
    uint32_t time_slice;
    uint64_t sched_ticks;
    uint64_t kernel_rsp;
    void *kernel_stack;
    uint64_t user_rsp;
    process_entry_t entry;
    process_t *process;
    thread_t *next_queue;
    thread_t *prev_queue;
};

typedef struct process_snapshot {
    uint32_t pid;
    uint32_t ppid;
    uint32_t pgid;
    uint32_t cpu_id;
    process_state_t state;
    uint32_t priority;
    uint32_t time_slice;
    uint64_t ticks;
    uint64_t sched_ticks;
    uint32_t shm_attachments;
    int tty_id;
    int is_idle;
    char name[PROCESS_NAME_MAX];
    char cwd[PROCESS_CWD_MAX];
    char cmdline[PROCESS_CMDLINE_MAX];
} process_snapshot_t;

struct process {
    uint32_t pid;
    uint32_t ppid;
    uint32_t pgid;
    uint32_t sid;
    uint32_t uid;
    uint32_t gid;
    uint32_t cpu_id;

    process_state_t state;
    int is_idle;
    int kill_pending;

    uint64_t kernel_rsp;
    void *kernel_stack;
    uintptr_t kernel_stack_phys;
    int kernel_stack_from_pmm;
    process_entry_t entry;
    thread_t main_thread;
    vm_space_t *va_space;
    void *user_stack;
    uint64_t user_entry;

    int wait_status;
    int exit_code;
    int exit_signal;
    uint64_t ticks;
    uint64_t created_time;
    uint64_t exit_time;

    uint64_t signal_mask;
    uint64_t signal_pending;
    uintptr_t signal_handlers[PROCESS_MAX_SIGNALS];
    signal_action_s sig_actions[PROCESS_MAX_SIGNALS];

    vm_space_t vm_space;
    uint32_t shm_attachment_count;

    int tty_id;
    char cwd[PROCESS_CWD_MAX];
    char cmdline[PROCESS_CMDLINE_MAX];
    void *open_files[PROCESS_MAX_OPEN_FILES];
    fd_entry_t fds[PROCESS_MAX_OPEN_FILES];
    int next_fd;

    int used;
    char name[PROCESS_NAME_MAX];

    uint32_t priority;
    uint32_t time_slice;
    uint64_t sched_ticks;
    process_t *next_queue;
    process_t *prev_queue;
    process_t *parent;
    process_t *children[PROCESS_MAX_CHILDREN];
    uint32_t child_count;

    process_t *rq_next;
    process_t *parent_next_child;
    process_t *children_head;
};

#define PROC_CREATED PROCESS_CREATED
#define PROC_READY PROCESS_READY
#define PROC_RUNNING PROCESS_RUNNING
#define PROC_BLOCKED PROCESS_BLOCKED
#define PROC_SLEEPING PROCESS_SLEEPING
#define PROC_STOPPED PROCESS_STOPPED
#define PROC_ZOMBIE PROCESS_ZOMBIE
#define PROC_DEAD PROCESS_DEAD

void process_init(void);

process_t *process_current(void);
int process_current_pid(void);
int process_get_pgid(int pid);

process_t *process_spawn_kernel(const char *name, process_entry_t entry);
int process_exec(int pid, process_entry_t entry, const char *name);
int process_exit_current(int code);
void process_exit(int code) __attribute__((noreturn));

int process_waitpid(int caller_pid, int target_pid, int options, int *status_out);
int process_kill(int pid, int sig);
int process_kill_pgid(int pgid, int sig);

int process_set_tty(int pid, int tty_id);
int process_set_priority(int pid, uint32_t priority);
int process_set_cmdline(int pid, const char *cmdline);
int process_get_cmdline(int pid, char *buf, size_t size);
const char *process_current_cmdline(void);
int process_clone_fd(int dst_pid, int dst_fd, int src_pid, int src_fd);

int process_signal_register(int pid, int sig, process_signal_handler_t handler);
int process_signal_mask(int pid, int how, uint64_t set, uint64_t *old_set);
int process_signal_pending(int pid, uint64_t *pending_out);
int process_signal_send(int pid, int sig);

uint64_t process_schedule_tick(uint64_t current_rsp);
void process_yield(void);
uint64_t process_ticks(void);

void process_start_scheduler(void) __attribute__((noreturn));

void process_run_phase2_selftests(void);
void process_run_phase3_selftests(void);
void process_run_phase4_selftests(void);
void process_run_phase5_selftests(void);
void process_run_phase7_selftests(void);
void process_run_phase8_selftests(void);
void process_dump_memory_state(void);
int process_snapshot(process_snapshot_t *out, int max);
int process_get_info(int pid, process_snapshot_t *out);
void process_get_memory_totals(size_t *proc_count_out,
                               size_t *mapped_pages_out,
                               size_t *shm_pages_out,
                               size_t *shm_attachments_out);

#endif /* TSUKASA_PROCESS_H */
