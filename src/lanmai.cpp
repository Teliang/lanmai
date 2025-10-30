#include "args.h"
#include "common.h"
#include "config.h"
#include "file_watch.h"
#include "log.h"
#include "mapper.h"
#include <atomic>
#include <fcntl.h>
#include <functional>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

void send(const libevdev_uinput* uinput_dev, unsigned int type, unsigned int code, int value) {
    LLOG(LL_DEBUG, "send: type:%d, code:%d, value:%d", type, code, value);
    libevdev_uinput_write_event(uinput_dev, type, code, value);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

void send(const libevdev_uinput* uinput_dev, input_event e) { send(uinput_dev, e.type, e.code, e.value); }

void handle_input(const std::string path, SingleMapper& sm, DoubleMapper& dm, MetaMapper& mm) {
    LLOG(LL_INFO, "thread begin");
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        LLOG(LL_ERROR, "open file:%s failed.", path.c_str());
        return;
    }
    Defer fd_defer{[&]() { close(fd); }};

    libevdev* dev = nullptr;
    Defer dev_defer{[&]() { libevdev_free(dev); }};
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        LLOG(LL_ERROR, "create dev failed");
        return;
    }
    sleep(1);

    Defer grab_defer{[&]() { libevdev_grab(dev, LIBEVDEV_UNGRAB); }};
    if (libevdev_grab(dev, LIBEVDEV_GRAB) < 0) {
        LLOG(LL_ERROR, "grab dev failed");
        return;
    }

    int uifd = open("/dev/uinput", O_RDWR);
    if (uifd < 0) {
        LLOG(LL_ERROR, "open uinput file failed");
        return;
    }
    Defer uifd_defer{[&]() { close(uifd); }};

    struct libevdev_uinput* uidev = nullptr;
    Defer uidev_defer{[&]() { libevdev_uinput_destroy(uidev); }};
    if (libevdev_uinput_create_from_device(dev, uifd, &uidev) != 0) {
        return;
    }

    while (true) {
        struct input_event input;
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, &input);
        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &input);
        }
        if (rc == -EAGAIN) {
            continue;
        }
        if (rc != LIBEVDEV_READ_STATUS_SUCCESS) {
            break;
        }

        if (input.type != EV_KEY) {
            send(uidev, input);
            continue;
        }
        LLOG(LL_DEBUG, "accept key: type:%d, code:%d, value:%d", input.type, input.code, input.value);
        auto si = sm.map(input);
        for (auto di : dm.map(si)) {
            for (auto mi : mm.map(di)) {
                send(uidev, mi);
            }
        }
    }
    LLOG(LL_INFO, "thread ending");
}

std::vector<std::string> get_grab_kbds(std::string conf_kbd) {
    std::vector<std::string> grab_kbds = get_kbd_devices();
    if (grab_kbds.size() == 0) {
        LLOG(LL_ERROR, "can't find out any key board device");
    }

    if (!conf_kbd.empty()) {
        if (std::find(grab_kbds.begin(), grab_kbds.end(), conf_kbd) == grab_kbds.end()) {
            /* grab_kbds does not contain conf_kbd */
            grab_kbds.push_back(conf_kbd);
        }
    }

    LLOG(LL_INFO, "get_grab_kbds size: %ld", grab_kbds.size());
    for (auto& kbd : grab_kbds) {
        LLOG(LL_INFO, "kbd: %s", kbd.c_str());
    }
    return grab_kbds;
}

void worker(std::atomic<bool>* is_finished, const std::string path, SingleMapper sm, DoubleMapper dm, MetaMapper mm) {
    try {
        handle_input(path, sm, dm, mm);
    } catch (const std::runtime_error& e) {
        LLOG(LL_ERROR, "Caught std::runtime_error: %s", e.what());
    } catch (...) { // Catch-all handler
        LLOG(LL_ERROR, "Caught an unknown exception type.");
    }

    is_finished->store(true);
    LLOG(LL_INFO, "worker finished");
}

int main(int argc, char* argv[]) {
    Args args(argc, argv);
    GLOBAL_LOG_LEVEL  = args.log_level;
    json cfg          = readConfig(args.config_path);
    auto [sm, dm, mm] = get_mappers(cfg);
    // Map of threads
    std::map<std::string, std::tuple<std::thread, std::atomic<bool>*>> thread_map;

    std::vector<std::string> grab_kbds = get_grab_kbds(args.device);

    for (auto& device : grab_kbds) {
        LLOG(LL_INFO, "new_thread_to_handle for %s", device.c_str());
        std::atomic<bool>* is_finished = new std::atomic<bool>(false);
        thread_map.insert({device, std::pair{std::thread(worker, is_finished, device, sm, dm, mm), is_finished}});
    }

    while (1) {
        if (have_new_device()) {
            // can't get new device if thread don't sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            LLOG(LL_INFO, "have a new input device!");

            // remove terminated thread
            for (auto it = thread_map.begin(); it != thread_map.end();) {
                auto& value      = it->second;
                auto is_finished = std::get<1>(value);
                LLOG(LL_INFO, "%s's is_finished: %d", it->first.c_str(), is_finished->load());
                if (is_finished->load()) {
                    LLOG(LL_INFO, "%s's thread is terminated", it->first.c_str());
                    delete is_finished;
                    std::get<0>(value).join();
                    it = thread_map.erase(it);
                } else {
                    it++;
                }
            }

            // reget kbds
            grab_kbds = get_grab_kbds(args.device);

            // only handle new device
            for (auto& device : grab_kbds) {
                if (thread_map.count(device)) {
                    LLOG(LL_INFO, "%s's thread is still running", device.c_str());
                } else {
                    LLOG(LL_INFO, "new_thread_to_handle for %s", device.c_str());
                    std::atomic<bool>* is_finished = new std::atomic<bool>(false);
                    thread_map.insert(
                        {device, std::pair{std::thread(worker, is_finished, device, sm, dm, mm), is_finished}});
                }
            }
        }
    }

    return 0;
}
