#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "lib/assoc.h"
#include "lib/crc32.h"
#include "lib/jstream.h"
#include "lib/utils.h"

/** Is set when the programm is terminated by a signal. */
static volatile sig_atomic_t is_terminated = 0;
/** Is set when propramm receives a check request. */
static volatile sig_atomic_t request_received = 0;
/** Directory to check. */
static char *check_dir = NULL;
/** Sleep interval. */
static long interval = 0;
/** Name of file to save the report. */
static const char *report_file_name = ".integrityspy-report.json";
/** Saved state of directory - all files and their checksums. */
struct assoc saved_state;

/**
 * Termination handler. Sets corresponding flag, does not terminate the process
 * immediately.
 */
static void
terminate_handler(int signal)
{
	(void)signal;
	is_terminated = 1;
}

/**
 * Request handler. Sets corresponding flag.
 */
static void
request_handler(int signal)
{
	(void)signal;
	request_received = 1;
}

/**
 * Sets up signal handlers.
 * SIGTERM terminates process.
 * SIGUSR1 initiates a new intgegrity check.
 * All the other signals are blocked.
 */
static int
signal_setup(void)
{
	static const int terminating_signals[] = {
		SIGTERM,
	};
	static const int request_signals[] = {
		SIGUSR1,
	};

	sigset_t mask;
	sigfillset(&mask);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
		say_error("failed to block all signals: %s",
			  strerror(errno));
		return -1;
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);

	sa.sa_handler = terminate_handler;
	for (size_t i = 0; i < lengthof(terminating_signals); i++) {
		int signal = terminating_signals[i];
		sigemptyset(&mask);
		sigaddset(&mask, signal);
		if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0) {
			say_error("failed to unblock signal-%d: %s",
				  signal, strerror(errno));
			return -1;
		}
		if (sigaction(signal, &sa, NULL) != 0) {
			say_error("failed to setup signal-%d handler: %s",
				  signal, strerror(errno));
			return -1;
		}
	}

	sa.sa_handler = request_handler;
	for (size_t i = 0; i < lengthof(request_signals); i++) {
		int signal = request_signals[i];
		sigemptyset(&mask);
		sigaddset(&mask, signal);
		if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0) {
			say_error("failed to unblock signal-%d: %s",
				  signal, strerror(errno));
			return -1;
		}
		if (sigaction(signal, &sa, NULL) != 0) {
			say_error("failed to setup signal-%d handler: %s",
				  signal, strerror(errno));
			return -1;
		}
	}

	return 0;
}

/**
 * Gets options passed as command line arguments or with environment variables.
 * Command line arguments has more priority. Option -i is often used for
 * interactive launch, so let's use -n as a short version of interval.
 */
static int
get_options(int argc, char **argv)
{
	static struct option longopts[] = {
		{"dir", required_argument, 0, 'd'},
		{"interval", required_argument, 0, 'n'},
		{0, 0, 0, 0}
	};
	static const char *opts = "d:n:";
	int ch;
	bool has_dir = false;
	bool has_interval = false;
	while ((ch = getopt_long(argc, argv, opts, longopts, NULL)) != -1) {
		switch (ch) {
		case 'd':
			assert(optarg != NULL);
			check_dir = strdup(optarg);
			if (check_dir == NULL) {
				say_error("cannot allocate memory to save dir argument");
				return -1;
			}
			has_dir = true;
			break;
		case 'n':
			assert(optarg != NULL);
			interval = strtol(optarg, NULL, 10);
			if (interval <= 0 || interval == LONG_MAX) {
				say_error("invalid interval argument");
				return -1;
			}
			has_interval = true;
			break;
		default:
			say_error("got unexpected argument");
			return -1;
		}
	}
	if (!has_dir) {
		char *dir = getenv("dir");
		if (dir == NULL) {
			say_error("dir argument is required");
			return -1;
		}
		check_dir = strdup(dir);
		if (check_dir == NULL) {
			say_error("cannot allocate memory to save dir argument");
			return -1;
		}	
		has_dir = true;
	}
	if (!has_interval) {
		char *interval_str = getenv("interval");
		if (interval_str == NULL) {
			say_error("interval argument is required");
			return -1;
		}
		interval = strtol(interval_str, NULL, 10);
		if (interval <= 0 || interval == LONG_MAX) {
			say_error("invalid interval argument");
			return -1;
		}
		has_interval = true;
	}
	return 0;
}

/* Simply use popular page size. */
#define BUF_SIZE 4096

/**
 * At first glance, it may seem that reading with mmap will be faster since
 * we don't need to copy read data. However, messing with virtual pages (which
 * mmap does) can be very costly - some benchmarks shows that if the file is not
 * huge, simple read can be about 50 percent faster. Also, mmap has a more
 * difficult interface. So let's simply read file block by block and use fadvise
 * if it is possible.
 */
static int
file_calc_crc32(int fd, uint32_t *crc32)
{
#ifdef POSIX_FADV_SEQUENTIAL
	posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif // POSIX_FADV_SEQUENTIAL
	unsigned char buffer[BUF_SIZE];
	ssize_t read_count = 0;
	*crc32 = 0;
	while ((read_count = read(fd, buffer, BUF_SIZE)) > 0 ||
	       read_count < 0 && errno == EINTR) {
		*crc32 = calculate_crc32c(*crc32, buffer, read_count);
	}
	if (read_count < 0) {
		assert(errno != EINTR);
		say_error("failed to read from file: %s", strerror(errno));
		return -1;
	}
	return 0;
}

#undef BUF_SIZE

/**
 * Calculates state (files and their checksums) of check_dir and writes it
 * to assoc.
 */
static int
calc_state(struct assoc *assoc)
{
	int rc = 0;
	DIR *dir = NULL;
	int fd = open(check_dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		say_error("failed to open directory: %s", strerror(errno));
		rc = -1;
		goto out;
	}
	dir = fdopendir(fd);
	if (dir == NULL) {
		say_error("failed to open directory stream: %s", strerror(errno));
		rc = -1;
		goto out;
	}
	struct dirent *entry_file;
	while (rc == 0 && (entry_file = readdir(dir)) != NULL) {
		/* Handle only regular files. */
		if (entry_file->d_type != DT_REG)
			continue;
		/* Skip hidden files. */
		if (entry_file->d_name[0] == '.')
			continue;
		int file_fd = -1;
		while (file_fd < 0) {
			file_fd = openat(fd, entry_file->d_name, O_RDONLY);
			/* Exit if the file was opened successfully. */
			if (file_fd >= 0)
				break;
			/* Try again if we were interrupted by a signal. */
			if (errno == EINTR)
				continue;
			/* The file was deleted - it is OK, just skip it. */
			if (errno == ENOENT || errno == EPIPE)
				rc = 0;
				break;
			rc = -1;
			say_error("failed to open file %s: %s",
				  entry_file->d_name, strerror(errno));
			break;
		}
		if (file_fd >= 0) {
			uint32_t crc32 = 0;
			/*
			 * It's OK to fail while calculating checksum since
			 * the file can be deleted.
			 */
			int file_calc_rc = file_calc_crc32(file_fd, &crc32);
			close(file_fd);
			if (file_calc_rc == 0 &&
			    assoc_put(assoc, entry_file->d_name, crc32) != 0) {
				say_error("cannot put crc to assoc");
				rc = -1;
				break;
			}

		}
		if (rc != 0)
			return rc;
	}
out:
	if (dir != NULL)
		closedir(dir);
	if (fd >= 0)
		close(fd);
	return rc;
}

/**
 * Cacluates current state of check_dir (files and checksums) and compares
 * it to the saved stated. Write report to the report file and prints check
 * result to syslog (OK with level INFO, FAIL with level WARNING).
 */
static int
demon_check_integrity(void)
{
	static char crc_buf[30];
	static const char *map_k_buf[4];
	static const char *map_v_buf[4];
	struct assoc state;
	assoc_create(&state);
	int rc = calc_state(&state);
	if (rc != 0)
		return -1;
	bool passed = true;
	struct assoc_iterator it;
	struct jstream stream;
	if (jstream_open(&stream, report_file_name) != 0) {
		say_error("Cannot open jstream");
		return -1;
	}
	const char *file;
	uint32_t saved_crc;
	for (bool has_elems = assoc_iterator_start(&saved_state, &it, &file, &saved_crc);
	     has_elems; has_elems = assoc_iterator_next(&it, &file, &saved_crc)) {
		uint32_t crc;
		bool has_file = assoc_pop(&state, file, &crc);
		if (!has_file) {
			passed = false;
			map_k_buf[0] = "path";
			map_k_buf[1] = "status";
			map_v_buf[0] = file;
			map_v_buf[1] = "ABSENT";
			if (jstream_write_map(&stream, map_k_buf, map_v_buf, 2) != 0) {
				say_error("Cannot write to jstream");
				goto fail;
			}
		} else {
			const char *status = "OK";
			if (crc != saved_crc) {
				passed = false;
				status = "FAIL";
			}
			char *etalon_crc_str = crc_buf;
			int written = sprintf(etalon_crc_str, "%X", saved_crc);
			/* Plus one for terminating null. */
			char *result_crc_str = crc_buf + written + 1;
			sprintf(result_crc_str, "%X", crc);
			map_k_buf[0] = "path";
			map_k_buf[1] = "etalon_crc32";
			map_k_buf[2] = "result_crc32";
			map_k_buf[3] = "status";
			map_v_buf[0] = file;
			map_v_buf[1] = etalon_crc_str;
			map_v_buf[2] = result_crc_str;
			map_v_buf[3] = status;
			if (jstream_write_map(&stream, map_k_buf, map_v_buf, 4) != 0) {
				say_error("Cannot write to jstream");
				goto fail;
			}
		}
	}

	/* Now state contains only new files. */
	uint32_t crc;
	for (bool has_elems = assoc_iterator_start(&state, &it, &file, &crc);
	     has_elems; has_elems = assoc_iterator_next(&it, &file, &crc)) {
		map_k_buf[0] = "path";
		map_k_buf[1] = "status";
		map_v_buf[0] = file;
		map_v_buf[1] = "NEW";
		if (jstream_write_map(&stream, map_k_buf, map_v_buf, 2) != 0) {
			say_error("Cannot write to jstream");
			goto fail;
		}
	}
	jstream_close(&stream);
	if (passed)
		syslog(LOG_INFO, "Integrity check: OK");
	else
		syslog(LOG_WARNING, "Integrity check: FAIL");
	return 0;
fail:
	jstream_close(&stream);
	return -1;
}

#ifdef __linux__

#include <poll.h>
#include <sys/inotify.h>

static int inotify_fd = 0;
static int inotify_wd = 0;

static int
demon_sleep_init()
{
	uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_DELETE_SELF;
	inotify_fd = inotify_init();
	if (inotify_fd < 0) {
		say_error("cannot init inotify");
		return -1;
	}
	inotify_wd = inotify_add_watch(inotify_fd, check_dir, mask);
	if (inotify_wd < 0) {
		say_error("cannot add inotify watcher");
		return -1;
	}
	return 0;
}

static void
demon_sleep_free()
{
	if (inotify_rm_watch(inotify_fd, inotify_wd) != 0)
		say_error("cannot remove inotify watch");
	if (close(inotify_fd) != 0)
		say_error("cannot close inotify");
}

static int
demon_sleep(long interval)
{
	struct pollfd pfd = {inotify_fd, POLLIN, 0};
	int ret = poll(&pfd, 1, interval * 1000);
	int rc = 0;
	if (ret < 0 && errno != EINTR) {
		say_error("poll failed: %s", strerror(errno));
		return -1;
	}
	return 0;
}

#else // __linux__


static int
demon_sleep_init()
{
	return 0;
}

static void
demon_sleep_free()
{
	return;
}

static int
demon_sleep(long interval)
{
	sleep(interval);
	return 0;
}

#endif // __linux__

/**
 * The main function of demon.
 * Wakes up every interval seconds and checks integrity of all files in
 * a directory.
 */
static void
demon_main(void)
{
	int rc = 0;
	while (rc == 0 && !is_terminated) {
		/* Do not sleep if we have pending request. */
		if (request_received == 0 && demon_sleep(interval) != 0) {
			rc = -1;
			break;
		}
		if (is_terminated)
			break;
		request_received = 0;
		rc = demon_check_integrity();
	}
	if (rc != 0)
		say_error("Demon has failed");
}

/**
 * Launches demon and returns its pid.
 * Argument demon_pid must not be NULL.
 */
static int
launch_demon(pid_t *demon_pid)
{
	assert(demon_pid != NULL);

	pid_t pid = fork();
	if (pid < 0) {
		say_error("fork has failed");
		return -1;
	}
	/* Exit from the parent process. */
	if (pid > 0) {
		*demon_pid = pid;
		return 0;
	}
	if (demon_sleep_init() != 0)
		exit(EXIT_FAILURE);
	demon_main();
	closelog();
	demon_sleep_free();
	exit(EXIT_SUCCESS);
}

int
main(int argc, char **argv)
{	
	setlogmask(LOG_UPTO(LOG_INFO));
	openlog("integrityspy", LOG_PID, LOG_USER);
	if (get_options(argc, argv) != 0)
		return -1;
	if (signal_setup() != 0)
		return -1;
	/* Save dir checksums. */
	assoc_create(&saved_state);
	if (calc_state(&saved_state) != 0)
		return -1;
	pid_t demon_pid = 0;
	if (launch_demon(&demon_pid) != 0)
		return -1;
	printf("Integrity spy is started with pid %d\n", demon_pid);
	closelog();
}
