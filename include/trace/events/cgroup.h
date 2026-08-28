/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup

#if !defined(_TRACE_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CGROUP_H

#include <linux/cgroup.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(cgroup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	u32,		ss_mask			)
		__string(	name,		root->name		)
	),

	TP_fast_assign(
		__entry->root = root->hierarchy_id;
		__entry->ss_mask = root->subsys_mask;
		__assign_str(name);
	),

	TP_printk("root=%d ss_mask=%#x name=%s",
		  __entry->root, __entry->ss_mask, __get_str(name))
);

/**
 * Event Description: This is a tracepoint that records when a new cgroup root is set up. It logs the root ID, subsystem mask, and root name. This helps in debugging and monitoring cgroup hierarchy creation.
 */
DEFINE_EVENT(cgroup_root, cgroup_setup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup root is destroyed. It logs the root ID, subsystem mask, and root name. This helps track when cgroup hierarchies are removed.
 */
DEFINE_EVENT(cgroup_root, cgroup_destroy_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup root is remounted. It logs the root ID, subsystem mask, and root name. This helps track changes to cgroup mount options.
 */
DEFINE_EVENT(cgroup_root, cgroup_remount,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

DECLARE_EVENT_CLASS(cgroup,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	int,		level			)
		__field(	u64,		id			)
		__string(	path,		path			)
	),

	TP_fast_assign(
		__entry->root = cgrp->root->hierarchy_id;
		__entry->id = cgroup_id(cgrp);
		__entry->level = cgrp->level;
		__assign_str(path);
	),

	TP_printk("root=%d id=%llu level=%d path=%s",
		  __entry->root, __entry->id, __entry->level, __get_str(path))
);

/**
 * Event Description: This is a tracepoint that records when a new cgroup directory is created. It logs the root ID, cgroup ID, level, and path. This helps track cgroup creation events.
 */
DEFINE_EVENT(cgroup, cgroup_mkdir,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup directory is removed. It logs the root ID, cgroup ID, level, and path. This helps track cgroup deletion events.
 */
DEFINE_EVENT(cgroup, cgroup_rmdir,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup is released. It logs the root ID, cgroup ID, level, and path. This helps track when cgroups are freed and no longer in use.
 */
DEFINE_EVENT(cgroup, cgroup_release,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup is renamed. It logs the root ID, cgroup ID, level, and new path. This helps track cgroup name changes.
 */
DEFINE_EVENT(cgroup, cgroup_rename,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup is frozen. It logs the root ID, cgroup ID, level, and path. This helps track when cgroups are paused or frozen.
 */
DEFINE_EVENT(cgroup, cgroup_freeze,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup is unfrozen. It logs the root ID, cgroup ID, level, and path. This helps track when cgroups are resumed from a frozen state.
*/
DEFINE_EVENT(cgroup, cgroup_unfreeze,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);


DECLARE_EVENT_CLASS(cgroup_migrate,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup),

	TP_STRUCT__entry(
		__field(	int,		dst_root		)
		__field(	int,		dst_level		)
		__field(	u64,		dst_id			)
		__field(	int,		pid			)
		__string(	dst_path,	path			)
		__string(	comm,		task->comm		)
	),

	TP_fast_assign(
		__entry->dst_root = dst_cgrp->root->hierarchy_id;
		__entry->dst_id = cgroup_id(dst_cgrp);
		__entry->dst_level = dst_cgrp->level;
		__assign_str(dst_path);
		__entry->pid = task->pid;
		__assign_str(comm);
	),

	TP_printk("dst_root=%d dst_id=%llu dst_level=%d dst_path=%s pid=%d comm=%s",
		  __entry->dst_root, __entry->dst_id, __entry->dst_level,
		  __get_str(dst_path), __entry->pid, __get_str(comm))
);

/**
 * Event Description: This is a tracepoint that records when a task is attached to a cgroup. It logs the destination root, cgroup ID, level, path, process ID, and command name. This helps track process migration between cgroups.
 */
DEFINE_EVENT(cgroup_migrate, cgroup_attach_task,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup)
);

/**
 * Event Description: This is a tracepoint that records when tasks are transferred to a new cgroup. It logs the destination root, cgroup ID, level, path, process ID, and command name. This helps track bulk task migrations.
 */
DEFINE_EVENT(cgroup_migrate, cgroup_transfer_tasks,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup)
);

DECLARE_EVENT_CLASS(cgroup_event,

	TP_PROTO(struct cgroup *cgrp, const char *path, int val),

	TP_ARGS(cgrp, path, val),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	int,		level			)
		__field(	u64,		id			)
		__string(	path,		path			)
		__field(	int,		val			)
	),

	TP_fast_assign(
		__entry->root = cgrp->root->hierarchy_id;
		__entry->id = cgroup_id(cgrp);
		__entry->level = cgrp->level;
		__assign_str(path);
		__entry->val = val;
	),

	TP_printk("root=%d id=%llu level=%d path=%s val=%d",
		  __entry->root, __entry->id, __entry->level, __get_str(path),
		  __entry->val)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup's populated state changes. It logs the root ID, cgroup ID, level, path, and the new populated value. This helps track when cgroups become empty or non-empty.
 */
DEFINE_EVENT(cgroup_event, cgroup_notify_populated,

	TP_PROTO(struct cgroup *cgrp, const char *path, int val),

	TP_ARGS(cgrp, path, val)
);

/**
 * Event Description: This is a tracepoint that records when a cgroup's frozen state changes. It logs the root ID, cgroup ID, level, path, and the new frozen value. This helps track cgroup freeze state transitions.
 */
DEFINE_EVENT(cgroup_event, cgroup_notify_frozen,

	TP_PROTO(struct cgroup *cgrp, const char *path, int val),

	TP_ARGS(cgrp, path, val)
);

DECLARE_EVENT_CLASS(cgroup_rstat,

	TP_PROTO(struct cgroup *cgrp, int cpu, bool contended),

	TP_ARGS(cgrp, cpu, contended),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	int,		level			)
		__field(	u64,		id			)
		__field(	int,		cpu			)
		__field(	bool,		contended		)
	),

	TP_fast_assign(
		__entry->root = cgrp->root->hierarchy_id;
		__entry->id = cgroup_id(cgrp);
		__entry->level = cgrp->level;
		__entry->cpu = cpu;
		__entry->contended = contended;
	),

	TP_printk("root=%d id=%llu level=%d cpu=%d lock contended:%d",
		  __entry->root, __entry->id, __entry->level,
		  __entry->cpu, __entry->contended)
);

/**
 * Related to locks:
 * global rstat_base_lock for base stats
 * cgroup_subsys::rstat_ss_lock for subsystem stats
 * 
 * 
 * Event Description: This is a tracepoint that records when the cgroup rstat lock is contended. It logs the root ID, cgroup ID, level, CPU number, and whether the lock was contended. This helps debug performance issues with cgroup statistics.
 */
DEFINE_EVENT(cgroup_rstat, cgroup_rstat_lock_contended,

	TP_PROTO(struct cgroup *cgrp, int cpu, bool contended),

	TP_ARGS(cgrp, cpu, contended)
);

/**
 * Event Description: This is a tracepoint that records when the cgroup rstat lock is acquired. It logs the root ID, cgroup ID, level, CPU number, and contention status. This helps track lock acquisition for cgroup statistics.
 */
DEFINE_EVENT(cgroup_rstat, cgroup_rstat_locked,

	TP_PROTO(struct cgroup *cgrp, int cpu, bool contended),

	TP_ARGS(cgrp, cpu, contended)
);

/**
 * Event Description: This is a tracepoint that records when the cgroup rstat lock is released. It logs the root ID, cgroup ID, level, CPU number, and contention status. This helps track lock release for cgroup statistics.
 */
DEFINE_EVENT(cgroup_rstat, cgroup_rstat_unlock,

	TP_PROTO(struct cgroup *cgrp, int cpu, bool contended),

	TP_ARGS(cgrp, cpu, contended)
);

#endif /* _TRACE_CGROUP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
