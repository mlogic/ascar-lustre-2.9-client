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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Whamcloud, Inc.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _MOUNT_UTILS_H_
#define _MOUNT_UTILS_H_

#include <lustre_disk.h>
#include <lustre_param.h>

extern char *progname;
extern int verbose;
extern int failover;

#define vprint(fmt, arg...) if (verbose > 0) printf(fmt, ##arg)
#define verrprint(fmt, arg...) if (verbose >= 0) fprintf(stderr, fmt, ##arg)

/* mo_flags */
#define MO_IS_LOOP		0x01
#define MO_FORCEFORMAT		0x02
#define MO_FAILOVER		0x04
#define MO_DRYRUN		0x08

#define MAX_LOOP_DEVICES	16
#define INDEX_UNASSIGNED	0xFFFF

/* used to describe the options to format the lustre disk, not persistent */
struct mkfs_opts {
	struct lustre_disk_data	mo_ldd; /* to be written in MOUNT_DATA_FILE */
	char	mo_device[128];   /* disk device name */
	char	**mo_pool_vdevs;  /* list of pool vdevs */
	char	mo_loopdev[128];  /* in case a loop dev is needed */
	char	mo_mkfsopts[512]; /* options to the backing-store mkfs */
	__u64	mo_device_sz;     /* in KB */
	int	mo_stripe_count;
	int	mo_flags;
	int	mo_mgs_failnodes;
};

/* used to describe the options to mount the lustre disk */
struct mount_opts {
	struct lustre_disk_data	 mo_ldd;
	char	*mo_orig_options;
	char	*mo_usource;		/* user-specified mount device */
	char	*mo_source;		/* our mount device name */
	char	 mo_target[PATH_MAX];	/* mount directory */
	int	 mo_nomtab;
	int	 mo_fake;
	int	 mo_force;
	int	 mo_retry;
	int	 mo_have_mgsnid;
	int	 mo_md_stripe_cache_size;
	int	 mo_nosvc;
};

int get_mountdata(char *, struct lustre_disk_data *);

/* mkfs/mount helper functions */
void fatal(void);
int run_command_err(char *cmd, int cmdsz, char *error_msg);
int run_command(char *cmd, int cmdsz);
int add_param(char *buf, char *key, char *val);
int get_param(char *buf, char *key, char **val);
char *strscat(char *dst, char *src, int buflen);
char *strscpy(char *dst, char *src, int buflen);
int check_mtab_entry(char *spec1, char *spec2, char *mntpt, char *type);
int update_mtab_entry(char *spec, char *mtpt, char *type, char *opts,
		      int flags, int freq, int pass);
int check_mountfsoptions(char *mountopts, char *wanted_mountopts, int justwarn);
void trim_mountfsoptions(char *s);
__u64 get_device_size(char* device);

int is_block(char *devname);
void disp_old_e2fsprogs_msg(const char *feature, int make_backfs);
int make_lustre_backfs(struct mkfs_opts *mop);
int write_local_files(struct mkfs_opts *mop);
int read_local_files(struct mkfs_opts *mop);
int is_lustre_target(struct mkfs_opts *mop);

/* loopback helper functions */
int file_create(char *path, int size);
int loop_format(struct mkfs_opts *mop);
int loop_setup(struct mkfs_opts *mop);
int loop_cleanup(struct mkfs_opts *mop);

/* generic target support */
int osd_write_ldd(struct mkfs_opts *mop);
int osd_read_ldd(char *dev, struct lustre_disk_data *ldd);
int osd_is_lustre(char *dev, unsigned *mount_type);
int osd_make_lustre(struct mkfs_opts *mop);
int osd_prepare_lustre(struct mkfs_opts *mop,
		       char *default_mountopts, int default_len,
		       char *always_mountopts, int always_len);
int osd_tune_lustre(char *dev, struct mount_opts *mop);
int osd_label_lustre(struct mount_opts *mop);
int osd_init(void);
void osd_fini(void);

int ldiskfs_write_ldd(struct mkfs_opts *mop);
int ldiskfs_read_ldd(char *dev, struct lustre_disk_data *ldd);
int ldiskfs_is_lustre(char *dev, unsigned *mount_type);
int ldiskfs_make_lustre(struct mkfs_opts *mop);
int ldiskfs_prepare_lustre(struct mkfs_opts *mop,
			   char *default_mountopts, int default_len,
			   char *always_mountopts, int always_len);
int ldiskfs_tune_lustre(char *dev, struct mount_opts *mop);
int ldiskfs_label_lustre(struct mount_opts *mop);
int ldiskfs_init(void);
void ldiskfs_fini(void);

#ifdef HAVE_ZFS_OSD
int zfs_write_ldd(struct mkfs_opts *mop);
int zfs_read_ldd(char *ds,  struct lustre_disk_data *ldd);
int zfs_is_lustre(char *dev, unsigned *mount_type);
int zfs_make_lustre(struct mkfs_opts *mop);
int zfs_prepare_lustre(struct mkfs_opts *mop,
		       char *default_mountopts, int default_len,
		       char *always_mountopts, int always_len);
int zfs_tune_lustre(char *dev, struct mount_opts *mop);
int zfs_label_lustre(struct mount_opts *mop);
int zfs_init(void);
void zfs_fini(void);
#endif

#endif
