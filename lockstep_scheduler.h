#pragma once

#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <thread>
#include <memory>
#include <ctime>
#include <semaphore.h>

class LockstepScheduler {
public:
    LockstepScheduler();
    ~LockstepScheduler();
    void set_absolute_time(uint64_t time_us);
    uint64_t get_absolute_time() const;
    int sem_timedwait(sem_t *sem, uint64_t timeout_us);
    int usleep(uint64_t usec);

private:
    uint64_t time_us_{0};
    std::condition_variable time_us_changed_{};
    mutable std::mutex time_us_mutex_{};

    struct TimedWait {
        pthread_t thread_id{0};
        uint64_t timeout_time_us{0};
        bool done{false};
        std::mutex mutex{};
    };
    std::vector<std::shared_ptr<TimedWait>> timed_waits_{};
    std::mutex timed_waits_mutex_{};

    uint64_t usleep_time_us_{0};
    std::mutex usleep_time_us_mutex_{};
};
