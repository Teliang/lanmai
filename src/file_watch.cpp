#include "file_watch.h"
#include "inotify.h"
#include "log.h"
#include <cstdlib>
#include <iostream>
#include <linux/limits.h>
#include <unistd.h>

volatile time_t time_of_create_event = {0};

int inotify_init1(const char* path) {
    int inotfd = inotify_init();

    int watch_desc = inotify_add_watch(inotfd, path, IN_CREATE | IN_DELETE);
    return inotfd;
}

void watch_directory(const char* path) {
    int inotfd = inotify_init1(path);

    size_t bufsiz               = sizeof(struct inotify_event) + PATH_MAX + 1;
    struct inotify_event* event = (inotify_event*)malloc(bufsiz);

    while (1) {
        /* wait for an event to occur */
        read(inotfd, event, bufsiz);

        /* process event struct here */
        if (event->mask == IN_CREATE) {
            LLOG(LL_INFO, "file create:%s", event->name);
            time_of_create_event = time(nullptr);
        } else if (event->mask == IN_DELETE) {
            LLOG(LL_INFO, "file delete:%s", event->name);
        }
    }
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

void watch_dev_input() {
    LLOG(LL_INFO, "watch dev input begin.");
    const char* path = "/dev/input/";
    watch_directory(path);
}
