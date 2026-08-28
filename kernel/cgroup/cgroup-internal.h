/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CGROUP_INTERNAL_H
#define __CGROUP_INTERNAL_H

#include <linux/cgroup.h>
#include <linux/kernfs.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include <linux/fs_parser.h>

#define TRACE_CGROUP_PATH_LEN 1024
extern spinlock_t trace_cgroup_path_lock;
extern char trace_cgroup_path[TRACE_CGROUP_PATH_LEN];

/**
 * Function Description: Enables debug mode for cgroup operations. This is called during initialization to set up debugging features. It may enable additional logging or debugging checks.
 */
extern void __init enable_debug_cgroup(void);

/**
 * cgroup_path() takes a spin lock. It is good practice not to take
 * spin locks within trace point handlers, as they are mostly hidden
 * from normal view. As cgroup_path() can take the kernfs_rename_lock
 * spin lock, it is best to not call that function from the trace event
 * handler.
 *
 * Note: trace_cgroup_##type##_enabled() is a static branch that will only
 *       be set when the trace event is enabled.
 */
#define TRACE_CGROUP_PATH(type, cgrp, ...)				\
	do {								\
		if (trace_cgroup_##type##_enabled()) {			\
			unsigned long flags;				\
			spin_lock_irqsave(&trace_cgroup_path_lock,	\
					  flags);			\
			cgroup_path(cgrp, trace_cgroup_path,		\
				    TRACE_CGROUP_PATH_LEN);		\
			trace_cgroup_##type(cgrp, trace_cgroup_path,	\
					    ##__VA_ARGS__);		\
			spin_unlock_irqrestore(&trace_cgroup_path_lock, \
					       flags);			\
		}							\
	} while (0)

/**
 * The cgroup filesystem superblock creation/mount context.
 * 
 * 
 * struct Description: This structure holds the filesystem context for cgroup mounts. It contains the cgroup root, namespace, flags, and cgroup1-specific options like cpuset_clone_children, subsystem mask, name, and release agent path. It is used during cgroup filesystem mounting and remounting.
 */
struct cgroup_fs_context {
	struct kernfs_fs_context kfc;
	struct cgroup_root	*root;
	struct cgroup_namespace	*ns;
	unsigned int	flags;			/** CGRP_ROOT_* flags */

	/** cgroup1 bits */
	bool		cpuset_clone_children;
	bool		none;			/** User explicitly requested empty subsystem */
	bool		all_ss;			/** Seen 'all' option */
	u32		subsys_mask;		/** Selected subsystems */
	char		*name;			/** Hierarchy name */
	char		*release_agent;		/** Path for release notifications */
};

static inline struct cgroup_fs_context *cgroup_fc2context(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;

	return container_of(kfc, struct cgroup_fs_context, kfc);
}

struct cgroup_pidlist;

/**
 * struct Description: This structure holds the context for cgroup file operations. It contains namespace information, PSI trigger data, process iterator state, pidlist for cgroup1, and peak tracking data. It is used to maintain state during reads and writes of cgroup control files. 
 */
struct cgroup_file_ctx {
	struct cgroup_namespace	*ns;

	struct {
		void			*trigger;
	} psi;

	struct {
		bool			started;
		struct css_task_iter	iter;
	} procs;

	struct {
		struct cgroup_pidlist	*pidlist;
	} procs1;

	struct cgroup_of_peak peak;
};

/**
 * A cgroup can be associated with multiple css_sets as different tasks may
 * belong to different cgroups on different hierarchies.  In the other
 * direction, a css_set is naturally associated with multiple cgroups.
 * This M:N relationship is represented by the following link structure
 * which exists for each association and allows traversing the associations
 * from both sides.
 * 
 * 
 * struct Description: This structure represents the association between a cgroup and a css_set. It contains pointers to both the cgroup and css_set, and links into both the cgroup's cset_links list and the css_set's cgrp_links list. This enables traversal of the M:N relationship between cgroups and css_sets.
 */
struct cgrp_cset_link {
	/** the cgroup and css_set this link associates */
	struct cgroup		*cgrp;
	struct css_set		*cset;

	/** list of cgrp_cset_links anchored at cgrp->cset_links */
	struct list_head	cset_link;

	/** list of cgrp_cset_links anchored at css_set->cgrp_links */
	struct list_head	cgrp_link;
};

/**
 * used to track tasks and csets during migration 
 *
 * 
 * struct Description: This structure holds the set of tasks and css_sets involved in a cgroup migration. It contains source and destination cset lists, task count, subsystem ID being processed, and iteration state. It is used during task migration between cgroups.
 */
struct cgroup_taskset {
	/** the src and dst cset list running through cset->mg_node */
	struct list_head	src_csets;
	struct list_head	dst_csets;

	/** the number of tasks in the set */
	int			nr_tasks;

	/** the subsys currently being processed */
	int			ssid;

	/**
	 * Fields for cgroup_taskset_*() iteration.
	 *
	 * Before migration is committed, the target migration tasks are on
	 * ->mg_tasks of the csets on ->src_csets.  After, on ->mg_tasks of
	 * the csets on ->dst_csets.  ->csets point to either ->src_csets
	 * or ->dst_csets depending on whether migration is committed.
	 *
	 * ->cur_csets and ->cur_task point to the current task position
	 * during iteration.
	 */
	struct list_head	*csets;
	struct css_set		*cur_cset;
	struct task_struct	*cur_task;
};

/**
 * migration context also tracks preloading 
 * 
 * 
 * struct Description: This structure holds the migration context for cgroup operations. It contains preloaded source and destination csets, the taskset to migrate, and the subsystem mask. It is used to guarantee atomic success or failure of cgroup migrations.
 */
struct cgroup_mgctx {
	/**
	 * Preloaded source and destination csets.  Used to guarantee
	 * atomic success or failure on actual migration.
	 */
	struct list_head	preloaded_src_csets;
	struct list_head	preloaded_dst_csets;

	/** tasks and csets to migrate */
	struct cgroup_taskset	tset;

	/** subsystems affected by migration */
	u32			ss_mask;
};

#define CGROUP_TASKSET_INIT(tset)						\
{										\
	.src_csets		= LIST_HEAD_INIT(tset.src_csets),		\
	.dst_csets		= LIST_HEAD_INIT(tset.dst_csets),		\
	.csets			= &tset.src_csets,				\
}

#define CGROUP_MGCTX_INIT(name)							\
{										\
	LIST_HEAD_INIT(name.preloaded_src_csets),				\
	LIST_HEAD_INIT(name.preloaded_dst_csets),				\
	CGROUP_TASKSET_INIT(name.tset),						\
}

#define DEFINE_CGROUP_MGCTX(name)						\
	struct cgroup_mgctx name = CGROUP_MGCTX_INIT(name)

extern struct cgroup_subsys *cgroup_subsys[];
extern struct list_head cgroup_roots;
extern bool cgrp_dfl_visible;

/** iterate across the hierarchies */
#define for_each_root(root)						\
	list_for_each_entry_rcu((root), &cgroup_roots, root_list,	\
				lockdep_is_held(&cgroup_mutex))

/**
 * for_each_subsys - iterate all enabled cgroup subsystems
 * @ss: the iteration cursor
 * @ssid: the index of @ss, CGROUP_SUBSYS_COUNT after reaching the end
 */
#define for_each_subsys(ss, ssid)					\
	for ((ssid) = 0; (ssid) < CGROUP_SUBSYS_COUNT &&		\
	     (((ss) = cgroup_subsys[ssid]) || true); (ssid)++)


/**
 * Function Description: Checks if a cgroup is dead (offline). Returns true if the CSS_ONLINE flag is not set. This indicates the cgroup has been removed and is no longer active.
 */
static inline bool cgroup_is_dead(const struct cgroup *cgrp)
{
	return !(cgrp->self.flags & CSS_ONLINE);
}

/**
 * Function Description: Checks if a cgroup has the notify-on-release flag set. Returns true if CGRP_NOTIFY_ON_RELEASE is set. This flag indicates that the release agent should be notified when the cgroup becomes empty.
 */
static inline bool notify_on_release(const struct cgroup *cgrp)
{
	return test_bit(CGRP_NOTIFY_ON_RELEASE, &cgrp->flags);
}

/**
 * Function Description: Puts a reference to a css_set while holding the css_set_lock. It decrements the reference count and handles the case where the count reaches zero. This is called internally by put_css_set().
 */
void put_css_set_locked(struct css_set *cset);

/**
 * Function Description: Puts a reference to a css_set. It decrements the reference count and if it reaches zero, it acquires the lock and calls put_css_set_locked(). This ensures safe cleanup of the css_set.
 */
static inline void put_css_set(struct css_set *cset)
{
	unsigned long flags;

	/*
	 * Ensure that the refcount doesn't hit zero while any readers
	 * can see it. Similar to atomic_dec_and_lock(), but for an
	 * rwlock
	 */
	if (refcount_dec_not_one(&cset->refcount))
		return;

	spin_lock_irqsave(&css_set_lock, flags);
	put_css_set_locked(cset);
	spin_unlock_irqrestore(&css_set_lock, flags);
}

/**
 * refcounted get/put for css_set objects
 * 
 * 
 * Function Description: Gets a reference to a css_set. It increments the reference count. This is called when a task or cgroup needs to hold a reference to a css_set.
 */
static inline void get_css_set(struct css_set *cset)
{
	refcount_inc(&cset->refcount);
}

bool cgroup_ssid_enabled(int ssid);
bool cgroup_on_dfl(const struct cgroup *cgrp);

struct cgroup_root *cgroup_root_from_kf(struct kernfs_root *kf_root);
struct cgroup *task_cgroup_from_root(struct task_struct *task,
				     struct cgroup_root *root);
struct cgroup *cgroup_kn_lock_live(struct kernfs_node *kn, bool drain_offline);
void cgroup_kn_unlock(struct kernfs_node *kn);
int cgroup_path_ns_locked(struct cgroup *cgrp, char *buf, size_t buflen,
			  struct cgroup_namespace *ns);

void cgroup_favor_dynmods(struct cgroup_root *root, bool favor);
void cgroup_free_root(struct cgroup_root *root);
void init_cgroup_root(struct cgroup_fs_context *ctx);
int cgroup_setup_root(struct cgroup_root *root, u32 ss_mask);
int rebind_subsystems(struct cgroup_root *dst_root, u32 ss_mask);
int cgroup_do_get_tree(struct fs_context *fc);

int cgroup_migrate_vet_dst(struct cgroup *dst_cgrp);
void cgroup_migrate_finish(struct cgroup_mgctx *mgctx);
void cgroup_migrate_add_src(struct css_set *src_cset, struct cgroup *dst_cgrp,
			    struct cgroup_mgctx *mgctx);
int cgroup_migrate_prepare_dst(struct cgroup_mgctx *mgctx);
int cgroup_migrate(struct task_struct *leader, bool threadgroup,
		   struct cgroup_mgctx *mgctx);

int cgroup_attach_task(struct cgroup *dst_cgrp, struct task_struct *leader,
		       bool threadgroup);
void cgroup_attach_lock(enum cgroup_attach_lock_mode lock_mode,
			struct task_struct *tsk);
void cgroup_attach_unlock(enum cgroup_attach_lock_mode lock_mode,
			  struct task_struct *tsk);
struct task_struct *cgroup_procs_write_start(char *buf, bool threadgroup,
					     enum cgroup_attach_lock_mode *lock_mode)
	__acquires(&cgroup_threadgroup_rwsem);
void cgroup_procs_write_finish(struct task_struct *task,
			       enum cgroup_attach_lock_mode lock_mode)
	__releases(&cgroup_threadgroup_rwsem);

void cgroup_lock_and_drain_offline(struct cgroup *cgrp);

int cgroup_mkdir(struct kernfs_node *parent_kn, const char *name, umode_t mode);
int cgroup_rmdir(struct kernfs_node *kn);
int cgroup_show_path(struct seq_file *sf, struct kernfs_node *kf_node,
		     struct kernfs_root *kf_root);

int __cgroup_task_count(const struct cgroup *cgrp);
int cgroup_task_count(const struct cgroup *cgrp);

/*
 * rstat.c
 */
int css_rstat_init(struct cgroup_subsys_state *css);
void css_rstat_exit(struct cgroup_subsys_state *css);
int ss_rstat_init(struct cgroup_subsys *ss);
void cgroup_base_stat_cputime_show(struct seq_file *seq);

/*
 * namespace.c
 */
extern const struct proc_ns_operations cgroupns_operations;

/*
 * cgroup-v1.c
 */
extern struct cftype cgroup1_base_files[];
extern struct kernfs_syscall_ops cgroup1_kf_syscall_ops;
extern const struct fs_parameter_spec cgroup1_fs_parameters[];

int proc_cgroupstats_show(struct seq_file *m, void *v);
bool cgroup1_ssid_disabled(int ssid);
void cgroup1_pidlist_destroy_all(struct cgroup *cgrp);
void cgroup1_release_agent(struct work_struct *work);
void cgroup1_check_for_release(struct cgroup *cgrp);
int cgroup1_parse_param(struct fs_context *fc, struct fs_parameter *param);
int cgroup1_get_tree(struct fs_context *fc);
int cgroup1_reconfigure(struct fs_context *ctx);

#endif /* __CGROUP_INTERNAL_H */
