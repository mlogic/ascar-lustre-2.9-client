/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _OFD_INTERNAL_H
#define _OFD_INTERNAL_H

#include <obd.h>
#include <obd_class.h>
#include <dt_object.h>
#include <md_object.h>
#include <lustre_fid.h>

#define OFD_INIT_OBJID	0
#define OFD_PRECREATE_BATCH_DEFAULT (OBJ_SUBDIR_COUNT * 4)

/* on small filesystems we should not precreate too many objects in
 * a single transaction, otherwise we can overflow transactions */
#define OFD_PRECREATE_SMALL_FS		(1024ULL * 1024 * 1024)
#define OFD_PRECREATE_BATCH_SMALL	8

/* Limit the returned fields marked valid to those that we actually might set */
#define OFD_VALID_FLAGS (LA_TYPE | LA_MODE | LA_SIZE | LA_BLOCKS | \
			 LA_BLKSIZE | LA_ATIME | LA_MTIME | LA_CTIME)

/* per-client-per-object persistent state (LRU) */
struct ofd_mod_data {
	struct list_head fmd_list;	  /* linked to fed_mod_list */
	struct lu_fid	 fmd_fid;	  /* FID being written to */
	__u64		 fmd_mactime_xid; /* xid highest {m,a,c}time setattr */
	cfs_time_t	 fmd_expire;	  /* time when the fmd should expire */
	int		 fmd_refcount;	  /* reference counter - list holds 1 */
};

#define OFD_FMD_MAX_NUM_DEFAULT 128
#define OFD_FMD_MAX_AGE_DEFAULT msecs_to_jiffies((obd_timeout+10)*MSEC_PER_SEC)

#define OFD_SOFT_SYNC_LIMIT_DEFAULT 16

/* request stats */
enum {
	LPROC_OFD_STATS_READ = 0,
	LPROC_OFD_STATS_WRITE,
	LPROC_OFD_STATS_GETATTR,
	LPROC_OFD_STATS_SETATTR,
	LPROC_OFD_STATS_PUNCH,
	LPROC_OFD_STATS_SYNC,
	LPROC_OFD_STATS_DESTROY,
	LPROC_OFD_STATS_CREATE,
	LPROC_OFD_STATS_STATFS,
	LPROC_OFD_STATS_GET_INFO,
	LPROC_OFD_STATS_SET_INFO,
	LPROC_OFD_STATS_QUOTACTL,
	LPROC_OFD_STATS_LAST,
};

static inline void ofd_counter_incr(struct obd_export *exp, int opcode,
				    char *jobid, long amount)
{
	if (exp->exp_obd && exp->exp_obd->obd_stats)
		lprocfs_counter_add(exp->exp_obd->obd_stats, opcode, amount);

	if (exp->exp_obd && exp->exp_obd->u.obt.obt_jobstats.ojs_hash &&
	    (exp_connect_flags(exp) & OBD_CONNECT_JOBSTATS))
		lprocfs_job_stats_log(exp->exp_obd, jobid, opcode, amount);

	if (exp->exp_nid_stats != NULL &&
	    exp->exp_nid_stats->nid_stats != NULL) {
		lprocfs_counter_add(exp->exp_nid_stats->nid_stats, opcode,
				    amount);
	}
}

struct ofd_seq {
	struct list_head	os_list;
	struct ost_id		os_oi;
	spinlock_t		os_last_oid_lock;
	struct mutex		os_create_lock;
	atomic_t		os_refc;
	struct dt_object	*os_lastid_obj;
	unsigned long		os_destroys_in_progress:1;
};

struct ofd_device {
	struct dt_device	 ofd_dt_dev;
	struct dt_device	*ofd_osd;
	struct obd_export	*ofd_osd_exp;
	struct dt_device_param	 ofd_dt_conf;
	/* DLM name-space for meta-data locks maintained by this server */
	struct ldlm_namespace	*ofd_namespace;

	/* last_rcvd file */
	struct lu_target	 ofd_lut;
	struct dt_object	*ofd_health_check_file;
	struct local_oid_storage *ofd_los;

	int			 ofd_subdir_count;
	__u64			 ofd_inconsistency_self_detected;
	__u64			 ofd_inconsistency_self_repaired;

	struct list_head	ofd_seq_list;
	rwlock_t		ofd_seq_list_lock;
	int			ofd_seq_count;
	int			ofd_precreate_batch;
	spinlock_t		ofd_batch_lock;

	/* protect all statfs-related counters */
	spinlock_t		 ofd_osfs_lock;
	/* statfs optimization: we cache a bit  */
	struct obd_statfs	 ofd_osfs;
	__u64			 ofd_osfs_age;
	int			 ofd_blockbits;
	/* writes between prep & commit which might be accounted twice in
	 * ofd_osfs.os_bavail */
	u64			 ofd_osfs_unstable;

	/* counters used during statfs update, protected by ofd_osfs_lock.
	 * record when some statfs refresh are in progress */
	int			 ofd_statfs_inflight;
	/* track writes completed while statfs refresh is underway.
	 * tracking is only effective when ofd_statfs_inflight > 1 */
	u64			 ofd_osfs_inflight;

	/* grants: all values in bytes */
	/* grant lock to protect all grant counters */
	spinlock_t		 ofd_grant_lock;
	/* total amount of dirty data reported by clients in incoming obdo */
	u64			 ofd_tot_dirty;
	/* sum of filesystem space granted to clients for async writes */
	u64			 ofd_tot_granted;
	/* grant used by I/Os in progress (between prepare and commit) */
	u64			 ofd_tot_pending;
	/* number of clients using grants */
	int			 ofd_tot_granted_clients;

	/* preferred BRW size, decided by storage type and capability */
	__u32			 ofd_brw_size;

	/* ofd mod data: ofd_device wide values */
	int			 ofd_fmd_max_num; /* per ofd ofd_mod_data */
	cfs_duration_t		 ofd_fmd_max_age; /* time to fmd expiry */

	spinlock_t		 ofd_flags_lock;
	unsigned long		 ofd_raid_degraded:1,
				 /* sync journal on writes */
				 ofd_syncjournal:1,
				 /* shall we grant space to clients not
				  * supporting OBD_CONNECT_GRANT_PARAM? */
				 ofd_grant_compat_disable:1,
				 /* Protected by ofd_lastid_rwsem. */
				 ofd_lastid_rebuilding:1,
				 ofd_record_fid_accessed:1,
				 ofd_lfsck_verify_pfid:1,
				 ofd_skip_lfsck:1;
	struct seq_server_site	 ofd_seq_site;
	/* the limit of SOFT_SYNC RPCs that will trigger a soft sync */
	unsigned int		 ofd_soft_sync_limit;
	/* Protect ::ofd_lastid_rebuilding */
	struct rw_semaphore	 ofd_lastid_rwsem;
	__u64			 ofd_lastid_gen;
	struct ptlrpc_thread	 ofd_inconsistency_thread;
	struct list_head	 ofd_inconsistency_list;
	spinlock_t		 ofd_inconsistency_lock;
};

static inline struct ofd_device *ofd_dev(struct lu_device *d)
{
	return container_of0(d, struct ofd_device, ofd_dt_dev.dd_lu_dev);
}

static inline struct obd_device *ofd_obd(struct ofd_device *ofd)
{
	return ofd->ofd_dt_dev.dd_lu_dev.ld_obd;
}

static inline struct ofd_device *ofd_exp(struct obd_export *exp)
{
	return ofd_dev(exp->exp_obd->obd_lu_dev);
}

static inline char *ofd_name(struct ofd_device *ofd)
{
	return ofd->ofd_dt_dev.dd_lu_dev.ld_obd->obd_name;
}

struct ofd_object {
	struct lu_object_header	ofo_header;
	struct dt_object	ofo_obj;
	struct lu_fid		ofo_pfid;
	unsigned int		ofo_pfid_checking:1,
				ofo_pfid_verified:1;
};

static inline struct ofd_object *ofd_obj(struct lu_object *o)
{
	return container_of0(o, struct ofd_object, ofo_obj.do_lu);
}

static inline int ofd_object_exists(struct ofd_object *obj)
{
	LASSERT(obj != NULL);
	if (lu_object_is_dying(obj->ofo_obj.do_lu.lo_header))
		return 0;
	return lu_object_exists(&obj->ofo_obj.do_lu);
}

static inline struct dt_object *fo2dt(struct ofd_object *obj)
{
	return &obj->ofo_obj;
}

static inline struct dt_object *ofd_object_child(struct ofd_object *_obj)
{
	struct lu_object *lu = &(_obj)->ofo_obj.do_lu;

	return container_of0(lu_object_next(lu), struct dt_object, do_lu);
}

static inline struct ofd_device *ofd_obj2dev(const struct ofd_object *fo)
{
	return ofd_dev(fo->ofo_obj.do_lu.lo_dev);
}

static inline void ofd_read_lock(const struct lu_env *env,
				 struct ofd_object *fo)
{
	struct dt_object  *next = ofd_object_child(fo);

	next->do_ops->do_read_lock(env, next, 0);
}

static inline void ofd_read_unlock(const struct lu_env *env,
				   struct ofd_object *fo)
{
	struct dt_object  *next = ofd_object_child(fo);

	next->do_ops->do_read_unlock(env, next);
}

static inline void ofd_write_lock(const struct lu_env *env,
				  struct ofd_object *fo)
{
	struct dt_object *next = ofd_object_child(fo);

	next->do_ops->do_write_lock(env, next, 0);
}

static inline void ofd_write_unlock(const struct lu_env *env,
				    struct ofd_object *fo)
{
	struct dt_object  *next = ofd_object_child(fo);

	next->do_ops->do_write_unlock(env, next);
}

/*
 * Common data shared by obdofd-level handlers. This is allocated per-thread
 * to reduce stack consumption.
 */
struct ofd_thread_info {
	const struct lu_env		*fti_env;

	struct obd_export		*fti_exp;
	__u64				 fti_xid;
	__u64				 fti_pre_version;

	struct lu_fid			 fti_fid;
	struct lu_attr			 fti_attr;
	struct lu_attr			 fti_attr2;
	struct ldlm_res_id		 fti_resid;
	union {
		struct filter_fid	 fti_mds_fid;
		struct filter_fid_old	 fti_mds_fid_old;
	};
	struct ost_id			 fti_ostid;
	struct ofd_object		*fti_obj;
	union {
		char			 name[64]; /* for ofd_init0() */
		struct obd_statfs	 osfs;    /* for obdofd_statfs() */
	} fti_u;

	/* Ops object filename */
	struct lu_name			 fti_name;
	struct dt_object_format		 fti_dof;
	struct lu_buf			 fti_buf;
	loff_t				 fti_off;

	/* Space used by the I/O, used by grant code */
	unsigned long			 fti_used;
	struct ost_lvb			 fti_lvb;
	union {
		struct lfsck_request	 fti_lr;
		struct obd_connect_data	 fti_ocd;
	};
};

extern void target_recovery_fini(struct obd_device *obd);
extern void target_recovery_init(struct lu_target *lut, svc_handler_t handler);

/* ofd_dev.c */
extern struct lu_context_key ofd_thread_key;
int ofd_postrecov(const struct lu_env *env, struct ofd_device *ofd);
int ofd_fiemap_get(const struct lu_env *env, struct ofd_device *ofd,
		   struct lu_fid *fid, struct fiemap *fiemap);

/* ofd_obd.c */
extern struct obd_ops ofd_obd_ops;
int ofd_statfs_internal(const struct lu_env *env, struct ofd_device *ofd,
			struct obd_statfs *osfs, __u64 max_age,
			int *from_cache);
int ofd_destroy_by_fid(const struct lu_env *env, struct ofd_device *ofd,
		       const struct lu_fid *fid, int orphan);
int ofd_statfs(const struct lu_env *env,  struct obd_export *exp,
	       struct obd_statfs *osfs, __u64 max_age, __u32 flags);
int ofd_obd_disconnect(struct obd_export *exp);

/* ofd_fs.c */
u64 ofd_seq_last_oid(struct ofd_seq *oseq);
void ofd_seq_last_oid_set(struct ofd_seq *oseq, u64 id);
int ofd_seq_last_oid_write(const struct lu_env *env, struct ofd_device *ofd,
			   struct ofd_seq *oseq);
int ofd_seqs_init(const struct lu_env *env, struct ofd_device *ofd);
struct ofd_seq *ofd_seq_get(struct ofd_device *ofd, u64 seq);
void ofd_seq_put(const struct lu_env *env, struct ofd_seq *oseq);

int ofd_fs_setup(const struct lu_env *env, struct ofd_device *ofd,
		 struct obd_device *obd);
void ofd_fs_cleanup(const struct lu_env *env, struct ofd_device *ofd);
int ofd_precreate_batch(struct ofd_device *ofd, int batch);
struct ofd_seq *ofd_seq_load(const struct lu_env *env, struct ofd_device *ofd,
			     u64 seq);
void ofd_seqs_fini(const struct lu_env *env, struct ofd_device *ofd);
void ofd_seqs_free(const struct lu_env *env, struct ofd_device *ofd);

/* ofd_io.c */
int ofd_start_inconsistency_verification_thread(struct ofd_device *ofd);
int ofd_stop_inconsistency_verification_thread(struct ofd_device *ofd);
int ofd_verify_ff(const struct lu_env *env, struct ofd_object *fo,
		  struct obdo *oa);
int ofd_preprw(const struct lu_env *env,int cmd, struct obd_export *exp,
	       struct obdo *oa, int objcount, struct obd_ioobj *obj,
	       struct niobuf_remote *rnb, int *nr_local,
	       struct niobuf_local *lnb);
int ofd_commitrw(const struct lu_env *env, int cmd, struct obd_export *exp,
		 struct obdo *oa, int objcount, struct obd_ioobj *obj,
		 struct niobuf_remote *rnb, int npages,
		 struct niobuf_local *lnb, int old_rc);

/* ofd_trans.c */
struct thandle *ofd_trans_create(const struct lu_env *env,
				 struct ofd_device *ofd);
int ofd_trans_start(const struct lu_env *env,
		    struct ofd_device *ofd, struct ofd_object *fo,
		    struct thandle *th);
void ofd_trans_stop(const struct lu_env *env, struct ofd_device *ofd,
		    struct thandle *th, int rc);
int ofd_txn_stop_cb(const struct lu_env *env, struct thandle *txn,
		    void *cookie);

/* lproc_ofd.c */
#ifdef CONFIG_PROC_FS
extern struct lprocfs_vars lprocfs_ofd_obd_vars[];
void ofd_stats_counter_init(struct lprocfs_stats *stats);
#else
static inline void ofd_stats_counter_init(struct lprocfs_stats *stats) {}
#endif

/* ofd_objects.c */
struct ofd_object *ofd_object_find(const struct lu_env *env,
				   struct ofd_device *ofd,
				   const struct lu_fid *fid);
int ofd_object_ff_load(const struct lu_env *env, struct ofd_object *fo);
int ofd_precreate_objects(const struct lu_env *env, struct ofd_device *ofd,
			  u64 id, struct ofd_seq *oseq, int nr, int sync);

void ofd_object_put(const struct lu_env *env, struct ofd_object *fo);
int ofd_attr_set(const struct lu_env *env, struct ofd_object *fo,
		 struct lu_attr *la, struct filter_fid *ff);
int ofd_object_punch(const struct lu_env *env, struct ofd_object *fo,
		     __u64 start, __u64 end, struct lu_attr *la,
		     struct filter_fid *ff, struct obdo *oa);
int ofd_object_destroy(const struct lu_env *, struct ofd_object *, int);
int ofd_attr_get(const struct lu_env *env, struct ofd_object *fo,
		 struct lu_attr *la);
int ofd_attr_handle_ugid(const struct lu_env *env, struct ofd_object *fo,
			 struct lu_attr *la, int is_setattr);

static inline
struct ofd_object *ofd_object_find_exists(const struct lu_env *env,
					  struct ofd_device *ofd,
					  const struct lu_fid *fid)
{
	struct ofd_object *fo;

	fo = ofd_object_find(env, ofd, fid);
	if (!IS_ERR(fo) && !ofd_object_exists(fo)) {
		ofd_object_put(env, fo);
		fo = ERR_PTR(-ENOENT);
	}
	return fo;
}

/* ofd_grants.c */
static inline int ofd_grant_param_supp(struct obd_export *exp)
{
	return !!(exp_connect_flags(exp) & OBD_CONNECT_GRANT_PARAM);
}

/* Blocksize used for client not supporting OBD_CONNECT_GRANT_PARAM.
 * That's 4KB=2^12 which is the biggest block size known to work whatever
 * the client's page size is. */
#define COMPAT_BSIZE_SHIFT 12

static inline int ofd_grant_prohibit(struct obd_export *exp,
				     struct ofd_device *ofd)
{
	/* When ofd_grant_compat_disable is set, we don't grant any space to
	 * clients not supporting OBD_CONNECT_GRANT_PARAM.
	 * Otherwise, space granted to such a client is inflated since it
	 * consumes PAGE_SIZE of grant space per block */
	return !!(ofd_obd(ofd)->obd_self_export != exp &&
		  !ofd_grant_param_supp(exp) && ofd->ofd_grant_compat_disable);
}

void ofd_grant_sanity_check(struct obd_device *obd, const char *func);
void ofd_grant_connect(const struct lu_env *env, struct obd_export *exp,
		       struct obd_connect_data *data, bool new_conn);
void ofd_grant_discard(struct obd_export *exp);
void ofd_grant_prepare_read(const struct lu_env *env, struct obd_export *exp,
			    struct obdo *oa);
void ofd_grant_prepare_write(const struct lu_env *env, struct obd_export *exp,
			     struct obdo *oa, struct niobuf_remote *rnb,
			     int niocount);
void ofd_grant_commit(struct obd_export *exp, unsigned long grant_used, int rc);
int ofd_grant_commit_cb_add(struct thandle *th, struct obd_export *exp,
			    unsigned long grant);
long ofd_grant_create(const struct lu_env *env, struct obd_export *exp,
		      int *nr);

/* ofd_fmd.c */
int ofd_fmd_init(void);
void ofd_fmd_exit(void);
struct ofd_mod_data *ofd_fmd_find(struct obd_export *exp,
				  const struct lu_fid *fid);
struct ofd_mod_data *ofd_fmd_get(struct obd_export *exp,
				 const struct lu_fid *fid);
void ofd_fmd_put(struct obd_export *exp, struct ofd_mod_data *fmd);
void ofd_fmd_expire(struct obd_export *exp);
void ofd_fmd_cleanup(struct obd_export *exp);
#ifdef DO_FMD_DROP
void ofd_fmd_drop(struct obd_export *exp, const struct lu_fid *fid);
#else
#define ofd_fmd_drop(exp, fid) do {} while (0)
#endif

/* ofd_dev.c */
int ofd_fid_set_index(const struct lu_env *env, struct ofd_device *ofd,
		      int index);
int ofd_fid_init(const struct lu_env *env, struct ofd_device *ofd);
int ofd_fid_fini(const struct lu_env *env, struct ofd_device *ofd);

/* ofd_lvb.c */
extern struct ldlm_valblock_ops ofd_lvbo;

/* ofd_dlm.c */
int ofd_intent_policy(struct ldlm_namespace *ns, struct ldlm_lock **lockp,
		      void *req_cookie, enum ldlm_mode mode, __u64 flags,
		      void *data);

static inline struct ofd_thread_info *ofd_info(const struct lu_env *env)
{
	struct ofd_thread_info *info;

	lu_env_refill((void *)env);
	info = lu_context_key_get(&env->le_ctx, &ofd_thread_key);
	LASSERT(info);
	return info;
}

static inline struct ofd_thread_info *ofd_info_init(const struct lu_env *env,
						    struct obd_export *exp)
{
	struct ofd_thread_info *info;

	info = ofd_info(env);
	LASSERT(info->fti_exp == NULL);
	LASSERT(info->fti_env == NULL);
	LASSERT(info->fti_attr.la_valid == 0);

	info->fti_env = env;
	info->fti_exp = exp;
	info->fti_pre_version = 0;
	return info;
}

static inline struct ofd_thread_info *tsi2ofd_info(struct tgt_session_info *tsi)
{
	struct ptlrpc_request	*req = tgt_ses_req(tsi);
	struct ofd_thread_info	*info;

	info = ofd_info(tsi->tsi_env);
	LASSERT(info->fti_exp == NULL);
	LASSERT(info->fti_env == NULL);
	LASSERT(info->fti_attr.la_valid == 0);

	info->fti_env = tsi->tsi_env;
	info->fti_exp = tsi->tsi_exp;

	info->fti_xid = req->rq_xid;
	/** VBR: take versions from request */
	if (req->rq_reqmsg != NULL &&
	    lustre_msg_get_flags(req->rq_reqmsg) & MSG_REPLAY) {
		__u64 *pre_version = lustre_msg_get_versions(req->rq_reqmsg);

		info->fti_pre_version = pre_version ? pre_version[0] : 0;
	}
	return info;
}

/* sync on lock cancel is useless when we force a journal flush,
 * and if we enable async journal commit, we should also turn on
 * sync on lock cancel if it is not enabled already. */
static inline void ofd_slc_set(struct ofd_device *ofd)
{
	if (ofd->ofd_syncjournal == 1)
		ofd->ofd_lut.lut_sync_lock_cancel = NEVER_SYNC_ON_CANCEL;
	else if (ofd->ofd_lut.lut_sync_lock_cancel == NEVER_SYNC_ON_CANCEL)
		ofd->ofd_lut.lut_sync_lock_cancel = ALWAYS_SYNC_ON_CANCEL;
}

static inline void ofd_prepare_fidea(struct filter_fid *ff,
				     const struct obdo *oa)
{
	/* packing fid and converting it to LE for storing into EA.
	 * Here ->o_stripe_idx should be filled by LOV and rest of
	 * fields - by client. */
	ff->ff_parent.f_seq = cpu_to_le64(oa->o_parent_seq);
	ff->ff_parent.f_oid = cpu_to_le32(oa->o_parent_oid);
	/* XXX: we are ignoring o_parent_ver here, since this should
	 *      be the same for all objects in this fileset. */
	ff->ff_parent.f_ver = cpu_to_le32(oa->o_stripe_idx);
}

static inline int ofd_validate_seq(struct obd_export *exp, __u64 seq)
{
	struct filter_export_data *fed = &exp->exp_filter_data;

	if (unlikely(seq == FID_SEQ_OST_MDT0 && fed->fed_group != 0)) {
		/* IDIF request only operates on MDT0 group */
		CERROR("%s: Invalid sequence %#llx for group %u\n",
		       exp->exp_obd->obd_name, seq, fed->fed_group);
		RETURN(-EINVAL);
	}

	return 0;
}
#endif /* _OFD_INTERNAL_H */
