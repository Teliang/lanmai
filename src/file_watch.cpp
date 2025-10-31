#include "common.h"
#include "inotify.h"
#include "log.h"
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/limits.h>
#include <string>
#include <unistd.h>

bool is_phys_not_null(const char* path) {
    int fd = open(path, O_RDWR | IN_CLOEXEC);
    if (fd == -1) {
        return false;
    }

    Defer fd_defer{[&]() { close(fd); }};

    struct libevdev* dev = nullptr;
    Defer dev_defer{[&]() { libevdev_free(dev); }};
    if (libevdev_new_from_fd(fd, &dev) == 0) {
        const char* phys = libevdev_get_phys(dev);
        LLOG(LL_INFO, "device: %s , phys: %s", path, phys);
        if (phys) {
            return true;
        }
    }
    return false;
}

void watch_directory(int inotfd, const char* path) {
    int watch_desc              = inotify_add_watch(inotfd, path, IN_CREATE);
    size_t bufsiz               = sizeof(struct inotify_event) + PATH_MAX + 1;
    struct inotify_event* event = (inotify_event*)malloc(bufsiz);

    Defer free_defer{[&]() {
        free(event);
        inotify_rm_watch(inotfd, watch_desc);
    }};

    while (1) {
        /* wait for an event to occur */
        read(inotfd, event, bufsiz);
        LLOG(LL_INFO, "File %s be created", event->name);

        std::string full_path = std::string(path) + event->name;

        // ignore device that phys is null, which maybe create by libudev
        if (is_phys_not_null(full_path.c_str())) {
            break;
        }
    }
}

int inotfd = inotify_init();

bool have_new_device() {
    LLOG(LL_INFO, "Watching /dev/input/");
    const char* path = "/dev/input/";
    watch_directory(inotfd, path);
    return true;
}
