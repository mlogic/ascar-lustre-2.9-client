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
 * Please contact Storage Systems Research Center, Computer Science Department,
 * University of California, Santa Cruz (www.ssrc.ucsc.edu) if you need
 * additional information or have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2013, 2014, 2015, University of California, Santa Cruz, CA, USA.
 * All rights reserved.
 * Developers:
 *   Yan Li <yanli@cs.ucsc.edu>
 */
/*
 * This file is NOT part of Lustre.
 * Lustre is a trademark of Sun Microsystems, Inc.
 */
/*
 * Header file for Ascar routines
 *
 *  Created on: Nov 14, 2013
 *      Author: yanli
 */

#ifndef SMART_QOS_H_
#define SMART_QOS_H_

/* We work with kernel only */
#ifdef __KERNEL__
# include <linux/types.h>
# include <linux/time.h>
# include <asm/param.h>
# include <libcfs/libcfs.h>
# include <linux/delay.h>
#else /* __KERNEL__ */
# define HZ 100
# define ONE_MILLION 1000000
# include <liblustre.h>
#endif

#define EWMA_ALPHA_INV (8)

/**
 * For tracking the exponentially-weighted moving average of a timeval. Note
 * that we can't do float point div in kernel, so actually we are tracking
 * ea = ewma * alpha. You should divide ea with alpha to get the real ewma.
 */
struct time_ewma {
	__u64          alpha_inv;
	__u64          ea;
	struct timeval last_time;
};
/* We can't do float point div, so we are tracking
 * ea = ewma * alpha = ewma / alpha_inv
 */

struct qos_rule_t {
	__u64 ack_ewma_lower;
	__u64 ack_ewma_upper;
	__u64 send_ewma_lower;
	__u64 send_ewma_upper;
	unsigned int rtt_ratio100_lower;
	unsigned int rtt_ratio100_upper;
	int m100;
	int b100;
	unsigned int tau;
	int used_times;

	__u64 ack_ewma_avg;
	__u64 send_ewma_avg;
	unsigned int rtt_ratio100_avg;
};

struct qos_data_t {
	spinlock_t       lock;
        struct time_ewma ack_ewma;
        struct time_ewma sent_ewma;
        int              rtt_ratio100;
        long             smallest_rtt;
        int              max_rpc_in_flight100;
        struct timeval   last_mrif_update_time;
        int              min_gap_between_updating_mrif;
        int              rule_no;
        /* Following fields are for calculating I/O bandwidth,
         * 0 for read, 1 for write */
        long             last_req_sec[2];       /* second of last request we received */
        __u64            tp_last_sec[2];        /* bw of last sec */
        __u64            sum_bytes_this_sec[2]; /* cumulative bytes read within this sec */
        /* For throttling support */
        unsigned int     min_usec_between_rpcs;
        struct timeval   last_rpc_time;
        struct qos_rule_t *rules;
};

static inline __u64 qos_get_ewma_usec(const struct time_ewma *ewma) {
	return ewma->ea / ewma->alpha_inv;
}

int parse_qos_rules(const char *buf, struct qos_data_t *qos);

/* TODO: need to write test cases for the following functions */
/* Lock of qos must be held. op == 0 for read, 1 for write */
static inline void calc_throughput(struct qos_data_t *qos, int op, int bytes_transferred)
{
	struct timeval now;

	if (op != 0 && op != 1)
		return;

	do_gettimeofday(&now);
	if (likely(now.tv_sec == qos->last_req_sec[op])) {
		qos->sum_bytes_this_sec[op] += bytes_transferred;
	} else if (likely(now.tv_sec == qos->last_req_sec[op] + 1)) {
		qos->tp_last_sec[op] = qos->sum_bytes_this_sec[op];
		qos->last_req_sec[op] = now.tv_sec;
		qos->sum_bytes_this_sec[op] = bytes_transferred;
	} else if (likely(now.tv_sec > qos->last_req_sec[op] + 1)) {
		qos->tp_last_sec[op] = 0;
		qos->last_req_sec[op] = now.tv_sec;
		qos->sum_bytes_this_sec[op] = bytes_transferred;
	}
	/* Ignore cases when now.tv_sec < qos->last_req_sec */
}

#endif /* ASCAR_H_ */
