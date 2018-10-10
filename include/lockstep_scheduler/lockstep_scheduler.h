#pragma once

#include <cstdint>
#include <mutex>
#include <vector>
#include <memory>
#include <pthread.h>

class LockstepScheduler {
public:
    void set_absolute_time(uint64_t time_us);
    uint64_t get_absolute_time() const;
    int cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *lock, uint64_t time_us);
    int usleep_until(uint64_t timed_us);

private:
    uint64_t time_us_{0};
    mutable std::mutex time_us_mutex_{};

    struct TimedWait {
        pthread_cond_t *passed_cond{nullptr};
        pthread_mutex_t *passed_lock{nullptr};
        uint64_t time_us{0};
        bool timeout{false};
        bool done{false};
    };
    std::vector<std::shared_ptr<TimedWait>> timed_waits_{};
    std::mutex timed_waits_mutex_{};
};
