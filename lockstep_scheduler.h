#pragma once

#include <cstdint>
#include <mutex>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <ctime>
#include <semaphore.h>

class LockstepScheduler {
public:
    LockstepScheduler();
    ~LockstepScheduler();
    void set_absolute_time(uint64_t time_us);
    uint64_t get_absolute_time() const;
    int sem_timedwait(sem_t *sem, uint64_t time_us);
    int usleep_until(uint64_t timed_us);

private:
    uint64_t time_us_{0};
    mutable std::mutex time_us_mutex_{};

    struct TimedWait {
        pthread_t thread_id{0};
        sem_t *sem{nullptr};
        uint64_t time_us{0};
        bool timeout{false};
        bool done{false};
        std::mutex mutex{};
    };
    std::vector<std::shared_ptr<TimedWait>> timed_waits_{};
    std::mutex timed_waits_mutex_{};

    std::atomic<unsigned> num_to_follow_up_{0};
};
