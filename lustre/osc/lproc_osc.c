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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
#define DEBUG_SUBSYSTEM S_CLASS

#include <linux/version.h>
#include <asm/statfs.h>
#include <obd_cksum.h>
#include <obd_class.h>
#include <lprocfs_status.h>
#include <linux/seq_file.h>
#include "osc_internal.h"
#include <ascar.h>

#ifdef CONFIG_PROC_FS
static int osc_active_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;

	LPROCFS_CLIMP_CHECK(dev);
	seq_printf(m, "%d\n", !dev->u.cli.cl_import->imp_deactive);
	LPROCFS_CLIMP_EXIT(dev);
	return 0;
}

static ssize_t osc_active_seq_write(struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	int rc;
	__s64 val;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 0 || val > 1)
		return -ERANGE;

	/* opposite senses */
	if (dev->u.cli.cl_import->imp_deactive == val)
		rc = ptlrpc_set_import_active(dev->u.cli.cl_import, val);
	else
		CDEBUG(D_CONFIG, "activate %d: ignoring repeat request\n",
			(int)val);

	return count;
}
LPROC_SEQ_FOPS(osc_active);

static int osc_max_rpcs_in_flight_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;

	spin_lock(&cli->cl_loi_list_lock);
	seq_printf(m, "%u\n", cli->cl_max_rpcs_in_flight);
	spin_unlock(&cli->cl_loi_list_lock);
	return 0;
}

static ssize_t osc_max_rpcs_in_flight_seq_write(struct file *file,
						const char __user *buffer,
						size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	struct qos_data_t *qos = &cli->qos;
	int rc;
	__s64 val;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 1 || val > OSC_MAX_RIF_MAX)
		return -ERANGE;

	LPROCFS_CLIMP_CHECK(dev);
	set_max_rpcs_in_flight((int)val, cli);
	LPROCFS_CLIMP_EXIT(dev);

	/* Update the value tracked by QoS routines too */
	spin_lock(&qos->lock);
	qos->max_rpc_in_flight100 = val * 100;
	spin_unlock(&qos->lock);

	return count;
}
LPROC_SEQ_FOPS(osc_max_rpcs_in_flight);

static int osc_min_brw_rpc_gap_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	struct qos_data_t *qos = &cli->qos;

	spin_lock(&qos->lock);
	seq_printf(m, "%u\n", qos->min_usec_between_rpcs);
	spin_unlock(&qos->lock);
	return 0;
}

static ssize_t osc_min_brw_rpc_gap_seq_write(struct file *file,
						const char __user *buffer,
						size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;
	__s64 val;
	struct qos_data_t *qos = &cli->qos;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 0)
		return -ERANGE;

	spin_lock(&qos->lock);
	qos->min_usec_between_rpcs = val;
	spin_unlock(&qos->lock);
	return count;
}
LPROC_SEQ_FOPS(osc_min_brw_rpc_gap);

static int osc_max_dirty_mb_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
        struct client_obd *cli = &dev->u.cli;
        long val;
        int mult;

	spin_lock(&cli->cl_loi_list_lock);
	val = cli->cl_dirty_max_pages;
	spin_unlock(&cli->cl_loi_list_lock);

	mult = 1 << (20 - PAGE_SHIFT);
	return lprocfs_seq_read_frac_helper(m, val, mult);
}

static ssize_t osc_max_dirty_mb_seq_write(struct file *file,
					  const char __user *buffer,
					  size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	int rc;
	__s64 pages_number;

	rc = lprocfs_str_with_units_to_s64(buffer, count, &pages_number, 'M');
	if (rc)
		return rc;

	pages_number >>= PAGE_SHIFT;

	if (pages_number <= 0 ||
	    pages_number >= OSC_MAX_DIRTY_MB_MAX << (20 - PAGE_SHIFT) ||
	    pages_number > totalram_pages / 4) /* 1/4 of RAM */
		return -ERANGE;

	spin_lock(&cli->cl_loi_list_lock);
	cli->cl_dirty_max_pages = pages_number;
	osc_wake_cache_waiters(cli);
	spin_unlock(&cli->cl_loi_list_lock);

	return count;
}
LPROC_SEQ_FOPS(osc_max_dirty_mb);

static int osc_cached_mb_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	int shift = 20 - PAGE_SHIFT;

	seq_printf(m, "used_mb: %ld\n"
		   "busy_cnt: %ld\n"
		   "reclaim: %llu\n",
		   (atomic_long_read(&cli->cl_lru_in_list) +
		    atomic_long_read(&cli->cl_lru_busy)) >> shift,
		    atomic_long_read(&cli->cl_lru_busy),
		   cli->cl_lru_reclaim);

	return 0;
}

/* shrink the number of caching pages to a specific number */
static ssize_t
osc_cached_mb_seq_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	__s64 pages_number;
	long rc;
	char kernbuf[128];

	if (count >= sizeof(kernbuf))
		return -EINVAL;

	if (copy_from_user(kernbuf, buffer, count))
		return -EFAULT;
	kernbuf[count] = 0;

	buffer += lprocfs_find_named_value(kernbuf, "used_mb:", &count) -
		  kernbuf;
	rc = lprocfs_str_with_units_to_s64(buffer, count, &pages_number, 'M');
	if (rc)
		return rc;

	pages_number >>= PAGE_SHIFT;

	if (pages_number < 0)
		return -ERANGE;

	rc = atomic_long_read(&cli->cl_lru_in_list) - pages_number;
	if (rc > 0) {
		struct lu_env *env;
		__u16 refcheck;

		env = cl_env_get(&refcheck);
		if (!IS_ERR(env)) {
			(void)osc_lru_shrink(env, cli, rc, true);
			cl_env_put(env, &refcheck);
		}
	}

	return count;
}
LPROC_SEQ_FOPS(osc_cached_mb);

static int osc_cur_dirty_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;

	spin_lock(&cli->cl_loi_list_lock);
	seq_printf(m, "%lu\n", cli->cl_dirty_pages << PAGE_SHIFT);
	spin_unlock(&cli->cl_loi_list_lock);
	return 0;
}
LPROC_SEQ_FOPS_RO(osc_cur_dirty_bytes);

static int osc_cur_grant_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;

	spin_lock(&cli->cl_loi_list_lock);
	seq_printf(m, "%lu\n", cli->cl_avail_grant);
	spin_unlock(&cli->cl_loi_list_lock);
	return 0;
}

static ssize_t osc_cur_grant_bytes_seq_write(struct file *file,
					     const char __user *buffer,
					     size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &obd->u.cli;
	int                rc;
	__s64              val;

	if (obd == NULL)
		return 0;

	rc = lprocfs_str_with_units_to_s64(buffer, count, &val, '1');
	if (rc)
		return rc;
	if (val < 0)
		return -ERANGE;

	/* this is only for shrinking grant */
	spin_lock(&cli->cl_loi_list_lock);
	if (val >= cli->cl_avail_grant) {
		spin_unlock(&cli->cl_loi_list_lock);
		return 0;
	}

	spin_unlock(&cli->cl_loi_list_lock);

	LPROCFS_CLIMP_CHECK(obd);
	if (cli->cl_import->imp_state == LUSTRE_IMP_FULL)
		rc = osc_shrink_grant_to_target(cli, val);
	LPROCFS_CLIMP_EXIT(obd);
	if (rc)
		return rc;
	return count;
}
LPROC_SEQ_FOPS(osc_cur_grant_bytes);

static int osc_cur_lost_grant_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;

	spin_lock(&cli->cl_loi_list_lock);
	seq_printf(m, "%lu\n", cli->cl_lost_grant);
	spin_unlock(&cli->cl_loi_list_lock);
	return 0;
}
LPROC_SEQ_FOPS_RO(osc_cur_lost_grant_bytes);

static int osc_cur_dirty_grant_bytes_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;

	spin_lock(&cli->cl_loi_list_lock);
	seq_printf(m, "%lu\n", cli->cl_dirty_grant);
	spin_unlock(&cli->cl_loi_list_lock);
	return 0;
}
LPROC_SEQ_FOPS_RO(osc_cur_dirty_grant_bytes);

static int osc_grant_shrink_interval_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	if (obd == NULL)
		return 0;
	seq_printf(m, "%d\n",
		   obd->u.cli.cl_grant_shrink_interval);
	return 0;
}

static ssize_t osc_grant_shrink_interval_seq_write(struct file *file,
						   const char __user *buffer,
						   size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int rc;
	__s64 val;

	if (obd == NULL)
		return 0;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;

	if (val <= 0 || val > INT_MAX)
		return -ERANGE;

	obd->u.cli.cl_grant_shrink_interval = val;

	return count;
}
LPROC_SEQ_FOPS(osc_grant_shrink_interval);

static int osc_checksum_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	if (obd == NULL)
		return 0;

	seq_printf(m, "%d\n", obd->u.cli.cl_checksum ? 1 : 0);
	return 0;
}

static ssize_t osc_checksum_seq_write(struct file *file,
				      const char __user *buffer,
				      size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int rc;
	__s64 val;

	if (obd == NULL)
		return 0;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;

	obd->u.cli.cl_checksum = !!val;

	return count;
}
LPROC_SEQ_FOPS(osc_checksum);

static int osc_checksum_type_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	int i;
	DECLARE_CKSUM_NAME;

        if (obd == NULL)
                return 0;

	for (i = 0; i < ARRAY_SIZE(cksum_name); i++) {
		if (((1 << i) & obd->u.cli.cl_supp_cksum_types) == 0)
			continue;
		if (obd->u.cli.cl_cksum_type == (1 << i))
			seq_printf(m, "[%s] ", cksum_name[i]);
		else
			seq_printf(m, "%s ", cksum_name[i]);
	}
	seq_printf(m, "\n");
	return 0;
}

static ssize_t osc_checksum_type_seq_write(struct file *file,
					   const char __user *buffer,
					   size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int i;
	DECLARE_CKSUM_NAME;
	char kernbuf[10];

        if (obd == NULL)
                return 0;

        if (count > sizeof(kernbuf) - 1)
                return -EINVAL;
	if (copy_from_user(kernbuf, buffer, count))
                return -EFAULT;
        if (count > 0 && kernbuf[count - 1] == '\n')
                kernbuf[count - 1] = '\0';
        else
                kernbuf[count] = '\0';

        for (i = 0; i < ARRAY_SIZE(cksum_name); i++) {
                if (((1 << i) & obd->u.cli.cl_supp_cksum_types) == 0)
                        continue;
                if (!strcmp(kernbuf, cksum_name[i])) {
                       obd->u.cli.cl_cksum_type = 1 << i;
                       return count;
                }
        }
        return -EINVAL;
}
LPROC_SEQ_FOPS(osc_checksum_type);

static int osc_resend_count_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;

	seq_printf(m, "%u\n", atomic_read(&obd->u.cli.cl_resends));
	return 0;
}

static ssize_t osc_resend_count_seq_write(struct file *file,
					  const char __user *buffer,
					  size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	int rc;
	__s64 val;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;

	if (val < 0 || val > INT_MAX)
		return -EINVAL;

	atomic_set(&obd->u.cli.cl_resends, val);

	return count;
}
LPROC_SEQ_FOPS(osc_resend_count);

static int osc_contention_seconds_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	struct osc_device *od  = obd2osc_dev(obd);

	seq_printf(m, "%u\n", od->od_contention_time);
	return 0;
}

static ssize_t osc_contention_seconds_seq_write(struct file *file,
						const char __user *buffer,
						size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
	struct osc_device *od  = obd2osc_dev(obd);
	int rc;
	__s64 val;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 0 || val > INT_MAX)
		return -ERANGE;

	od->od_contention_time = val;

	return count;
}
LPROC_SEQ_FOPS(osc_contention_seconds);

static int osc_lockless_truncate_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	struct osc_device *od  = obd2osc_dev(obd);

	seq_printf(m, "%u\n", od->od_lockless_truncate);
	return 0;
}

static ssize_t osc_lockless_truncate_seq_write(struct file *file,
					       const char __user *buffer,
				    size_t count, loff_t *off)
{
	struct obd_device *obd = ((struct seq_file *)file->private_data)->private;
        struct osc_device *od  = obd2osc_dev(obd);
	int rc;
	__s64 val;

	rc = lprocfs_str_to_s64(buffer, count, &val);
	if (rc)
		return rc;
	if (val < 0)
		return -ERANGE;

	od->od_lockless_truncate = !!val;

	return count;
}
LPROC_SEQ_FOPS(osc_lockless_truncate);

static int osc_destroys_in_flight_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *obd = m->private;
	seq_printf(m, "%u\n",
		   atomic_read(&obd->u.cli.cl_destroy_in_flight));
	return 0;
}
LPROC_SEQ_FOPS_RO(osc_destroys_in_flight);

static int osc_obd_max_pages_per_rpc_seq_show(struct seq_file *m, void *v)
{
	return lprocfs_obd_max_pages_per_rpc_seq_show(m, m->private);
}

static ssize_t osc_obd_max_pages_per_rpc_seq_write(struct file *file,
						   const char __user *buffer,
						   size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	struct obd_connect_data *ocd = &cli->cl_import->imp_connect_data;
	int chunk_mask, rc;
	__s64 val;

	rc = lprocfs_str_with_units_to_s64(buffer, count, &val, '1');
	if (rc)
		return rc;
	if (val < 0)
		return -ERANGE;

	/* if the max_pages is specified in bytes, convert to pages */
	if (val >= ONE_MB_BRW_SIZE)
		val >>= PAGE_SHIFT;

	LPROCFS_CLIMP_CHECK(dev);

	chunk_mask = ~((1 << (cli->cl_chunkbits - PAGE_SHIFT)) - 1);
	/* max_pages_per_rpc must be chunk aligned */
	val = (val + ~chunk_mask) & chunk_mask;
	if (val == 0 || (ocd->ocd_brw_size != 0 &&
			 val > ocd->ocd_brw_size >> PAGE_SHIFT)) {
		LPROCFS_CLIMP_EXIT(dev);
		return -ERANGE;
	}
	spin_lock(&cli->cl_loi_list_lock);
	cli->cl_max_pages_per_rpc = val;
	client_adjust_max_dirty(cli);
	spin_unlock(&cli->cl_loi_list_lock);

	LPROCFS_CLIMP_EXIT(dev);
	return count;
}
LPROC_SEQ_FOPS(osc_obd_max_pages_per_rpc);

static int osc_unstable_stats_seq_show(struct seq_file *m, void *v)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	long pages;
	int mb;

	pages = atomic_long_read(&cli->cl_unstable_count);
	mb    = (pages * PAGE_SIZE) >> 20;

	seq_printf(m, "unstable_pages: %20ld\n"
		   "unstable_mb:              %10d\n",
		   pages, mb);
	return 0;
}
LPROC_SEQ_FOPS_RO(osc_unstable_stats);

static int osc_qos_rules_seq_show(struct seq_file *m, void *data)
{
	struct obd_device *dev = m->private;
	struct client_obd *cli = &dev->u.cli;
	struct qos_data_t *qos = &cli->qos;
	int i;
	struct qos_rule_t *r;

	spin_lock(&qos->lock);
	if (0 == qos->rule_no || NULL == qos->rules || 0 == qos->min_gap_between_updating_mrif) {
		seq_printf(m, "0\n");
		/* Make sure the upcoming for loop doesn't run */
		qos->rule_no = 0;
	} else {
		seq_printf(m, "%d,%d\n", qos->rule_no, 1000000 / qos->min_gap_between_updating_mrif);
	}
	for (i = 0; i < qos->rule_no; ++i) {
		r = &qos->rules[i];
		seq_printf(m, "%llu,%llu,%llu,%llu,%u,%u,%d,%d,%u,%d,%llu,%llu,%u\n",
			      r->ack_ewma_lower,  r->ack_ewma_upper,
			      r->send_ewma_lower, r->send_ewma_upper,
			      r->rtt_ratio100_lower, r->rtt_ratio100_upper,
			      r->m100, r->b100, r->tau,
			      r->used_times,
			      r->ack_ewma_avg, r->send_ewma_avg, r->rtt_ratio100_avg);
	}
	spin_unlock(&qos->lock);
	return 0;
}

static ssize_t osc_qos_rules_seq_write(struct file *file,
				       const char __user *buffer,
				       size_t count, loff_t *off)
{
	struct obd_device *dev = ((struct seq_file *)file->private_data)->private;
	struct client_obd *cli = &dev->u.cli;
	struct qos_data_t *qos = &cli->qos;
	int rc;
	char *kernbuf = NULL;

	OBD_ALLOC(kernbuf, count + 1);
	if (NULL == kernbuf) {
		return -ENOMEM;
	}
	if (copy_from_user(kernbuf, buffer, count)) {
		rc = -EFAULT;
		goto out_free_kernbuf;
	}
	/* Make sure the buf ends with a null so that sscanf won't overread */
	kernbuf[count] = '\0';

	spin_lock(&qos->lock);
	/* parse_qos_rules() will free existing rules in qos before starting parsing */
	rc = parse_qos_rules(kernbuf, qos);
	if (0 == rc) {
		/* return the number of chars processed on a success parsing */
		rc = count;
	}
	qos->ack_ewma.ea = 0;
	qos->ack_ewma.last_time.tv_sec = 0;
	qos->ack_ewma.last_time.tv_usec = 0;
	qos->sent_ewma.ea = 0;
	qos->sent_ewma.last_time.tv_sec = 0;
	qos->sent_ewma.last_time.tv_usec = 0;
	qos->rtt_ratio100 = 0;
	qos->smallest_rtt = 0;
	spin_unlock(&qos->lock);
out_free_kernbuf:
	OBD_FREE(kernbuf, count + 1);
	return rc;

}
LPROC_SEQ_FOPS(osc_qos_rules);

LPROC_SEQ_FOPS_RO_TYPE(osc, uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, connect_flags);
LPROC_SEQ_FOPS_RO_TYPE(osc, blksize);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytestotal);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytesfree);
LPROC_SEQ_FOPS_RO_TYPE(osc, kbytesavail);
LPROC_SEQ_FOPS_RO_TYPE(osc, filestotal);
LPROC_SEQ_FOPS_RO_TYPE(osc, filesfree);
LPROC_SEQ_FOPS_RO_TYPE(osc, server_uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, conn_uuid);
LPROC_SEQ_FOPS_RO_TYPE(osc, timeouts);
LPROC_SEQ_FOPS_RO_TYPE(osc, state);

LPROC_SEQ_FOPS_WO_TYPE(osc, ping);

LPROC_SEQ_FOPS_RW_TYPE(osc, import);
LPROC_SEQ_FOPS_RW_TYPE(osc, pinger_recov);

struct lprocfs_vars lprocfs_osc_obd_vars[] = {
	{ .name	=	"uuid",
	  .fops	=	&osc_uuid_fops			},
	{ .name	=	"ping",
	  .fops	=	&osc_ping_fops,
	  .proc_mode =	0222				},
	{ .name	=	"connect_flags",
	  .fops	=	&osc_connect_flags_fops		},
	{ .name	=	"blocksize",
	  .fops	=	&osc_blksize_fops		},
	{ .name	=	"kbytestotal",
	  .fops	=	&osc_kbytestotal_fops		},
	{ .name	=	"kbytesfree",
	  .fops	=	&osc_kbytesfree_fops		},
	{ .name	=	"kbytesavail",
	  .fops	=	&osc_kbytesavail_fops		},
	{ .name	=	"filestotal",
	  .fops	=	&osc_filestotal_fops		},
	{ .name	=	"filesfree",
	  .fops	=	&osc_filesfree_fops		},
	{ .name	=	"ost_server_uuid",
	  .fops	=	&osc_server_uuid_fops		},
	{ .name	=	"ost_conn_uuid",
	  .fops	=	&osc_conn_uuid_fops		},
	{ .name	=	"active",
	  .fops	=	&osc_active_fops		},
	{ .name	=	"max_pages_per_rpc",
	  .fops	=	&osc_obd_max_pages_per_rpc_fops	},
	{ .name	=	"max_rpcs_in_flight",
	  .fops	=	&osc_max_rpcs_in_flight_fops	},
	{ .name	=	"min_brw_rpc_gap",
	  .fops	=	&osc_min_brw_rpc_gap_fops	},
	{ .name	=	"destroys_in_flight",
	  .fops	=	&osc_destroys_in_flight_fops	},
	{ .name	=	"max_dirty_mb",
	  .fops	=	&osc_max_dirty_mb_fops		},
	{ .name	=	"osc_cached_mb",
	  .fops	=	&osc_cached_mb_fops		},
	{ .name	=	"cur_dirty_bytes",
	  .fops	=	&osc_cur_dirty_bytes_fops	},
	{ .name	=	"cur_grant_bytes",
	  .fops	=	&osc_cur_grant_bytes_fops	},
	{ .name	=	"cur_lost_grant_bytes",
	  .fops	=	&osc_cur_lost_grant_bytes_fops	},
	{ .name	=	"cur_dirty_grant_bytes",
	  .fops	=	&osc_cur_dirty_grant_bytes_fops	},
	{ .name	=	"grant_shrink_interval",
	  .fops	=	&osc_grant_shrink_interval_fops	},
	{ .name	=	"checksums",
	  .fops	=	&osc_checksum_fops		},
	{ .name	=	"checksum_type",
	  .fops	=	&osc_checksum_type_fops		},
	{ .name	=	"resend_count",
	  .fops	=	&osc_resend_count_fops		},
	{ .name	=	"timeouts",
	  .fops	=	&osc_timeouts_fops		},
	{ .name	=	"contention_seconds",
	  .fops	=	&osc_contention_seconds_fops	},
	{ .name	=	"lockless_truncate",
	  .fops	=	&osc_lockless_truncate_fops	},
	{ .name	=	"import",
	  .fops	=	&osc_import_fops		},
	{ .name	=	"state",
	  .fops	=	&osc_state_fops			},
	{ .name	=	"pinger_recov",
	  .fops	=	&osc_pinger_recov_fops		},
	{ .name	=	"unstable_stats",
	  .fops	=	&osc_unstable_stats_fops	},
	{ .name	=	"qos_rules",
	  .fops	=	&osc_qos_rules_fops		},
	{ NULL }
};

#define pct(a,b) (b ? a * 100 / b : 0)

static int osc_rpc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct obd_device *dev = seq->private;
	struct client_obd *cli = &dev->u.cli;
	unsigned long read_tot = 0, write_tot = 0, read_cum, write_cum;
	int i;

	do_gettimeofday(&now);

	spin_lock(&cli->cl_loi_list_lock);

	seq_printf(seq, "snapshot_time:         %lu.%lu (secs.usecs)\n",
		   now.tv_sec, now.tv_usec);
	seq_printf(seq, "read RPCs in flight:  %d\n",
		   cli->cl_r_in_flight);
	seq_printf(seq, "write RPCs in flight: %d\n",
		   cli->cl_w_in_flight);
	seq_printf(seq, "pending write pages:  %d\n",
		   atomic_read(&cli->cl_pending_w_pages));
	seq_printf(seq, "pending read pages:   %d\n",
		   atomic_read(&cli->cl_pending_r_pages));

	seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
	seq_printf(seq, "pages per rpc         rpcs   %% cum %% |");
	seq_printf(seq, "       rpcs   %% cum %%\n");

	read_tot = lprocfs_oh_sum(&cli->cl_read_page_hist);
	write_tot = lprocfs_oh_sum(&cli->cl_write_page_hist);

	read_cum = 0;
	write_cum = 0;
	for (i = 0; i < OBD_HIST_MAX; i++) {
		unsigned long r = cli->cl_read_page_hist.oh_buckets[i];
		unsigned long w = cli->cl_write_page_hist.oh_buckets[i];

		read_cum += r;
		write_cum += w;
		seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
			   1 << i, r, pct(r, read_tot),
			   pct(read_cum, read_tot), w,
			   pct(w, write_tot),
			   pct(write_cum, write_tot));
		if (read_cum == read_tot && write_cum == write_tot)
			break;
	}

	seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
	seq_printf(seq, "rpcs in flight        rpcs   %% cum %% |");
	seq_printf(seq, "       rpcs   %% cum %%\n");

	read_tot = lprocfs_oh_sum(&cli->cl_read_rpc_hist);
	write_tot = lprocfs_oh_sum(&cli->cl_write_rpc_hist);

        read_cum = 0;
        write_cum = 0;
        for (i = 0; i < OBD_HIST_MAX; i++) {
                unsigned long r = cli->cl_read_rpc_hist.oh_buckets[i];
                unsigned long w = cli->cl_write_rpc_hist.oh_buckets[i];
                read_cum += r;
                write_cum += w;
		seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
			   i, r, pct(r, read_tot),
			   pct(read_cum, read_tot), w,
			   pct(w, write_tot),
			   pct(write_cum, write_tot));
                if (read_cum == read_tot && write_cum == write_tot)
                        break;
        }

        seq_printf(seq, "\n\t\t\tread\t\t\twrite\n");
        seq_printf(seq, "offset                rpcs   %% cum %% |");
        seq_printf(seq, "       rpcs   %% cum %%\n");

        read_tot = lprocfs_oh_sum(&cli->cl_read_offset_hist);
        write_tot = lprocfs_oh_sum(&cli->cl_write_offset_hist);

        read_cum = 0;
        write_cum = 0;
        for (i = 0; i < OBD_HIST_MAX; i++) {
                unsigned long r = cli->cl_read_offset_hist.oh_buckets[i];
                unsigned long w = cli->cl_write_offset_hist.oh_buckets[i];
                read_cum += r;
                write_cum += w;
                seq_printf(seq, "%d:\t\t%10lu %3lu %3lu   | %10lu %3lu %3lu\n",
                           (i == 0) ? 0 : 1 << (i - 1),
                           r, pct(r, read_tot), pct(read_cum, read_tot),
                           w, pct(w, write_tot), pct(write_cum, write_tot));
                if (read_cum == read_tot && write_cum == write_tot)
                        break;
        }

	spin_unlock(&cli->cl_loi_list_lock);

        return 0;
}
#undef pct

static ssize_t osc_rpc_stats_seq_write(struct file *file,
				       const char __user *buf,
                                       size_t len, loff_t *off)
{
        struct seq_file *seq = file->private_data;
        struct obd_device *dev = seq->private;
        struct client_obd *cli = &dev->u.cli;

        lprocfs_oh_clear(&cli->cl_read_rpc_hist);
        lprocfs_oh_clear(&cli->cl_write_rpc_hist);
        lprocfs_oh_clear(&cli->cl_read_page_hist);
        lprocfs_oh_clear(&cli->cl_write_page_hist);
        lprocfs_oh_clear(&cli->cl_read_offset_hist);
        lprocfs_oh_clear(&cli->cl_write_offset_hist);

        return len;
}
LPROC_SEQ_FOPS(osc_rpc_stats);

static int osc_stats_seq_show(struct seq_file *seq, void *v)
{
	struct timeval now;
	struct obd_device *dev = seq->private;
	struct osc_stats *stats = &obd2osc_dev(dev)->od_stats;

	do_gettimeofday(&now);

	seq_printf(seq, "snapshot_time:         %lu.%lu (secs.usecs)\n",
		   now.tv_sec, now.tv_usec);
	seq_printf(seq, "lockless_write_bytes\t\t%llu\n",
		   stats->os_lockless_writes);
	seq_printf(seq, "lockless_read_bytes\t\t%llu\n",
		   stats->os_lockless_reads);
	seq_printf(seq, "lockless_truncate\t\t%llu\n",
		   stats->os_lockless_truncates);
	return 0;
}

static ssize_t osc_stats_seq_write(struct file *file,
				   const char __user *buf,
                                   size_t len, loff_t *off)
{
        struct seq_file *seq = file->private_data;
        struct obd_device *dev = seq->private;
        struct osc_stats *stats = &obd2osc_dev(dev)->od_stats;

        memset(stats, 0, sizeof(*stats));
        return len;
}

LPROC_SEQ_FOPS(osc_stats);

int lproc_osc_attach_seqstat(struct obd_device *dev)
{
	int rc;

	rc = lprocfs_seq_create(dev->obd_proc_entry, "osc_stats", 0644,
				&osc_stats_fops, dev);
	if (rc == 0)
		rc = lprocfs_obd_seq_create(dev, "rpc_stats", 0644,
					    &osc_rpc_stats_fops, dev);

	return rc;
}
#endif /* CONFIG_PROC_FS */
