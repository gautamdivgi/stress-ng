/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__) && defined(__NR_membarrier)

#define MAX_MEMBARRIER_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

enum membarrier_cmd {
	MEMBARRIER_CMD_QUERY = 0,
	MEMBARRIER_CMD_SHARED = (1 << 0),
};

static void *stress_membarrier_thread(void *arg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	stack_t ss;
	const args_t *args = (args_t *)arg;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	ss.ss_sp = (void *)align_address(stack, STACK_ALIGNMENT);
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err("sigaltstack");
		return &nowt;
	}
	while (keep_running && opt_do_run) {
		int ret;

		ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0);
		if (ret < 0) {
			pr_fail_err("membarrier CMD QUERY");
			break;
		}
		/* CMD SHARED not availble; skip it */
		if (!(ret & MEMBARRIER_CMD_SHARED))
			continue;
		if (shim_membarrier(MEMBARRIER_CMD_SHARED, 0) < 0) {
			pr_fail_err("membarrier CMD SHARED");
			break;
		}
	}

	return &nowt;
}

/*
 *  stress on membarrier()
 *	stress system by IO sync calls
 */
int stress_membarrier(const args_t *args)
{
	int ret;
	pthread_t pthreads[MAX_MEMBARRIER_THREADS];
	size_t i;
	int pthread_ret[MAX_MEMBARRIER_THREADS];

	ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0);
	if (ret < 0) {
		pr_err("%s: membarrier failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (!(ret & MEMBARRIER_CMD_SHARED)) {
		pr_inf("%s: membarrier MEMBARRIER_CMD_SHARED "
			"not supported\n", args->name);
		return EXIT_FAILURE;
	}

	sigfillset(&set);
	memset(pthread_ret, 0, sizeof(pthread_ret));
	keep_running = true;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		pthread_ret[i] =
			pthread_create(&pthreads[i], NULL,
				stress_membarrier_thread, (void *)args);
	}

	do {
		ret = shim_membarrier(MEMBARRIER_CMD_SHARED, 0);
		if (ret < 0) {
			pr_err("%s: membarrier failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
		}
		inc_counter(args);
	} while (keep_stressing());

	keep_running = false;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		if (pthread_ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}
	return EXIT_SUCCESS;
}
#else
int stress_membarrier(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
