#ifndef TASK_H
#define TASK_H

#include <pthread.h>
#include "util/list.h"
#include "emu/cpu.h"
#include "kernel/fs.h"
#include "kernel/signal.h"
#include "kernel/resource.h"
#include "util/timer.h"

// everything here is private to the thread executing this task and needs no
// locking, unless otherwise specified
struct task {
    struct cpu_state cpu;
    pthread_t thread;

    struct tgroup *group; // immutable
    struct list group_links;
    pid_t_ pid, tgid; // immutable
    uid_t_ uid, gid;
    uid_t_ euid, egid;

    // TODO move into mem
    addr_t vdso;
    addr_t start_brk;
    addr_t brk;
    addr_t clear_tid;

    struct fdtable *files;
    struct fs_info *fs;

    struct sighand *sighand;
    sigset_t_ blocked;
    sigset_t_ queued; // where blocked signals go when they're sent
    sigset_t_ pending;

    // locked by pids_lock
    struct task *parent;
    struct list children;
    struct list siblings;
    pid_t_ sid, pgid;
    struct list session;
    struct list pgroup;

    // locked by parent's thread group
    dword_t exit_code;
    bool zombie;

    // I wish conditions variables were as reliable as wait queues. alas, they are not
    bool vfork_done;
    pthread_cond_t vfork_cond;
    lock_t vfork_lock;
};

// current will always give the process that is currently executing
// if I have to stop using __thread, current will become a macro
extern __thread struct task *current;
#define curmem current->cpu.mem

// Creates a new process, initializes most fields from the parent. Specify
// parent as NULL to create the init process. Returns NULL if out of memory.
// Ends with an underscore because there's a mach function by the same name
struct task *task_create_(struct task *parent);
// Removes the process from the process table and frees it. Must be called with pids_lock.
void task_destroy(struct task *task);

void vfork_notify(struct task *task);

// struct thread_group is way too long to type comfortably
struct tgroup {
    struct list threads;
    struct task *leader; // immutable
    struct rusage_ rusage;

    struct tty *tty;

    bool has_timer;
    struct timer *timer;

    struct rlimit_ limits[RLIMIT_NLIMITS_];

    // https://twitter.com/tblodt/status/957706819236904960
    // TODO locking
    bool doing_group_exit;
    dword_t group_exit_code;

    struct rusage_ children_rusage;
    pthread_cond_t child_exit;

    lock_t lock;
};

static inline bool task_is_leader(struct task *task) {
    return task->group->leader == task;
}

struct pid {
    dword_t id;
    struct task *task;
    struct list session;
    struct list pgroup;
};

// synchronizes obtaining a pointer to a task and freeing that task
extern lock_t pids_lock;
// these functions must be called with pids_lock
struct pid *pid_get(dword_t pid);
struct task *pid_get_task(dword_t pid);
struct task *pid_get_task_zombie(dword_t id); // don't return null if the task exists as a zombie

#define MAX_PID (1 << 10) // oughta be enough

// When a thread is created to run a new process, this function is used.
extern void (*task_run_hook)(void);
// TODO document
void task_start(struct task *task);

extern void (*exit_hook)(int code);

#endif
