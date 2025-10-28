#include "file_watch.h"
#include "inotify.h"
#include "log.h"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <linux/limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

volatile time_t time_of_create_event = {0};

void watch_directory(int inotfd, const char* path) {
    int watch_desc = inotify_add_watch(inotfd, path, IN_CREATE | IN_DELETE);

    size_t bufsiz               = sizeof(struct inotify_event) + PATH_MAX + 1;
    struct inotify_event* event = (inotify_event*)malloc(bufsiz);

    // ignore first_create_event that will trigger by libudev when it grap kbd
    bool first_create_event = true;

    while (1) {
        /* wait for an event to occur */
        read(inotfd, event, bufsiz);

        /* process event struct here */
        if (event->mask == IN_CREATE) {
            LLOG(LL_INFO, "file create:%s", event->name);
            if (first_create_event) {
                first_create_event = false;
            } else {
                time_of_create_event = time(nullptr);
                break;
            }
        } else if (event->mask == IN_DELETE) {
            LLOG(LL_INFO, "file delete:%s", event->name);
        }
    }

    free(event);
    inotify_rm_watch(inotfd, watch_desc);
    LLOG(LL_INFO, "thread ending");
}

bool have_new_device() {
    if (time_of_create_event) {
        time_t now;
        time(&now);

        double duration = difftime(now, time_of_create_event);
        LLOG(LL_INFO, "Operation took %f seconds.", duration);
        if (duration > 0.5) {
            time_of_create_event = {0};
            LLOG(LL_INFO, "Do something!");
            return true;
        }
    }
    return false;
}

int inotfd = inotify_init();

void watch_dev_input() {
    LLOG(LL_INFO, "watch dev input begin.");
    const char* path = "/dev/input/";
    watch_directory(inotfd, path);
}
