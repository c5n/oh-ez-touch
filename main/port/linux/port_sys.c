/**
 * @file linux/port_sys.c
 *
 * port_sys on the simulator host.
 */

#include "port_sys.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "esp_log.h"

static const char *TAG = "port_sys";

/* clock_gettime(CLOCK_MONOTONIC) rather than esp_timer_get_time(): esp_timer
 * registers headers-only on the linux target, so calling it does not link.
 * This is measured from an arbitrary epoch rather than from boot, but nothing
 * here needs an absolute origin -- only differences and a monotonic direction. */
static uint64_t port_monotonic_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t port_micros(void)
{
    static uint64_t origin = 0;

    if (origin == 0)
        origin = port_monotonic_us();

    return port_monotonic_us() - origin;
}

uint64_t port_millis(void)
{
    return port_micros() / 1000ULL;
}

uint32_t port_tick_ms(void)
{
    return (uint32_t)port_millis();
}

void port_restart(void)
{
    /* A re-exec, because the device reboots and the simulator should be seen
     * to do the same thing: the window comes down and comes back up, and the
     * panel re-reads config.json and the host-file NVS exactly as a real one
     * re-reads its own -- which is what "restart and check the setting
     * stuck" has always meant to test. exit(0) made every reboot a scripted
     * two steps, and whatever shell or harness was watching the process saw
     * it end, which the device never asks anyone to expect.
     *
     * execve() rather than a fork()+exec: the PID stays, so a harness's child
     * handle keeps tracking the process across the reboot and nothing is
     * orphaned behind the shell that started it. The image starts from
     * main() again with zeroed globals, which is as close to a power cycle
     * as a process can give itself.
     *
     * A coverage-instrumented build loses what libgcov was holding in memory,
     * since exec skips the atexit that writes it -- but a reboot ended the
     * counters' session anyway, and the next run merges over the same files.
     *
     * This is also what the ESP-IDF linux port's own esp_restart() will not
     * do: it exits (esp_restart_noos() in esp_system, "restart triggered on
     * Linux, hence exiting"), which is why the firmware routes restarts
     * through here rather than through it. */
    ESP_LOGI(TAG, "restart requested; re-execing");

    /* The command line this image was started with, rebuilt from
     * /proc/self/cmdline because port_restart() has no way to be handed
     * main()'s argv. The file is exactly the array the kernel started the
     * image with: NUL-separated, ending in an empty string. */
    char    cmdline[4096];
    size_t  used  = 0;
    int     fd    = open("/proc/self/cmdline", O_RDONLY);
    ssize_t chunk;

    while (fd >= 0 && used < sizeof(cmdline) - 1
           && (chunk = read(fd, cmdline + used, sizeof(cmdline) - 1 - used)) > 0)
        used += (size_t)chunk;

    if (fd >= 0)
        close(fd);

    /* Unsplittable, or nothing to re-exec as: rather than a half-booted
     * fallback, the old behaviour. The failure is loud, because a test that
     * asked for a restart and got an exit has been lied to. */
    if (used < 2 || cmdline[used - 1] != '\0')
    {
        ESP_LOGE(TAG, "no usable command line; exiting instead");
        exit(1);
    }

    /* One argv slot per NUL-terminated word, plus the terminator. A kernel
     * that ends the file with a second NUL adds an empty word that was never
     * an argument, so the count drops it and the walk below stops at it. */
    size_t argc = 0;

    for (size_t i = 0; i < used; i++)
        if (cmdline[i] == '\0')
            argc++;

    if (used >= 2 && cmdline[used - 2] == '\0')
        argc--;

    char **argv = (char **)malloc((argc + 1) * sizeof(char *));

    if (argv == NULL)
    {
        ESP_LOGE(TAG, "no memory for argv; exiting instead");
        exit(1);
    }

    {
        size_t arg = 0;
        size_t pos = 0;

        while (pos < used && arg < argc && cmdline[pos] != '\0')
        {
            argv[arg++] = &cmdline[pos];
            pos += strlen(&cmdline[pos]) + 1;
        }

        argv[arg] = NULL;
    }

    /* Everything above stdio gets CLOEXEC: the X/Wayland connection, the SDL
     * audio device, the test interface's socket. execve() does not close what
     * does not carry the flag, and a window whose connection survived the
     * reboot would outlive its image on the desktop until the process next
     * exited -- a ghost of the panel beside the real one. Walking the
     * directory rather than counting to a limit: it holds exactly the open
     * descriptors, and a hard limit would either miss some or spend its time
     * closing a million that are not. */
    DIR *dir = opendir("/proc/self/fd");

    if (dir != NULL)
    {
        struct dirent *entry;

        while ((entry = readdir(dir)) != NULL)
        {
            char *end = NULL;
            long  which = strtol(entry->d_name, &end, 10);

            if (end == NULL || *end != '\0' || which <= STDERR_FILENO)
                continue;

            int flags = fcntl((int)which, F_GETFD);

            if (flags >= 0)
                fcntl((int)which, F_SETFD, flags | FD_CLOEXEC);
        }

        closedir(dir);
    }

    /* The log goes to a file when the sim is nohup'd, and a full buffer would
     * be dropped by an exec that never runs the flush at exit. */
    fflush(NULL);

    /* The tick the old image ran on. The FreeRTOS linux port drives its
     * scheduler with setitimer(ITIMER_REAL) (portable/linux/port.c), and an
     * interval timer is one of the things an execve does *not* clear: the
     * tick would carry on firing SIGALRM at the new image, which has had
     * every disposition reset to the default and so terminates on the first
     * one -- the reboot dies before it can log a line. Zeroed here, the new
     * image's own startup is the next thing to arm it. */
    {
        struct itimerval stopped = { 0 };

        setitimer(ITIMER_REAL, &stopped, NULL);
    }

    execve("/proc/self/exe", argv, environ);

    /* Only reached if the exec failed, which leaves the old image running
     * with its CLOEXEC half-applied and its buffers flushed -- it cannot be
     * recovered, only ended. */
    ESP_LOGE(TAG, "re-exec failed: %s; exiting", strerror(errno));
    exit(1);
}

size_t port_free_heap(void)
{
    /* fordblks is the free space *inside* the arena, i.e. memory the allocator
     * holds but has not handed out. It is the closest thing the host has to the
     * device's figure, and it is still not the same thing: the host can always
     * ask the kernel for more. port_sys.h says not to make decisions on this. */
    struct mallinfo2 mi = mallinfo2();

    return (size_t)mi.fordblks;
}

size_t port_largest_free_block(void)
{
    /* Not knowable here, and not worth approximating: mallinfo2() reports how
     * much is free, never how much of it is in one piece, and the host would
     * answer "enough" to any question the device answers "no" to. 0 is the
     * documented "cannot say" -- see port_sys.h. */
    return 0;
}

void port_heap_info(struct port_heap_info_s *out)
{
    /* What the host can say is the same as the two accessors; the rest is 0,
     * the documented "cannot say". */
    memset(out, 0, sizeof(*out));

    out->free = port_free_heap();
    out->largest = 0;
}

bool port_localtime(struct tm *out)
{
    time_t now = time(NULL);

    if (localtime_r(&now, out) == NULL)
        return false;

    /* Always succeeds, unlike the device. The host clock is set by the OS
     * before this process starts, so there is no unsynchronised window to
     * report -- and the year check the device needs would reject nothing here
     * anyway. This is the behaviour hal/sdl2/Arduino.cpp already had. */
    return true;
}
