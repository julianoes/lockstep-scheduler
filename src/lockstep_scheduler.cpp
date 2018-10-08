#include "lockstep_scheduler/lockstep_scheduler.h"


uint64_t LockstepScheduler::get_absolute_time() const
{
    std::lock_guard<std::mutex> lock(time_us_mutex_);
    return time_us_;
}

void LockstepScheduler::set_absolute_time(uint64_t time_us)
{
    {
        std::lock_guard<std::mutex> lock(time_us_mutex_);
        time_us_ = time_us;
    }

    {
        std::lock_guard<std::mutex> lock_timed_waits(timed_waits_mutex_);

        auto timed_wait = std::begin(timed_waits_);
        while (timed_wait != std::end(timed_waits_)) {

            std::unique_lock<std::mutex> lock(timed_wait->get()->mutex);
            // Clean up the ones that are already done from last iteration.
            if (timed_wait->get()->done) {
                // We shouldn't delete a lock in use.
                lock.unlock();
                timed_wait = timed_waits_.erase(timed_wait);
                continue;
            }
            // Keep holding lock while we do the sem_post so it doesn't go away
            // when it's done.
            if (timed_wait->get()->time_us <= time_us) {
                timed_wait->get()->timeout = true;
                // We are abusing the condition here to signal that the time
                // has passed.
                sem_post(timed_wait->get()->cond);
                timed_wait->get()->done = true;
            }
            ++timed_wait;
        }
    }
}

int LockstepScheduler::cond_timedwait(sem_t *cond, sem_t *lock, uint64_t time_us)
{
    if (0 == sem_trywait(cond)) {
        return 0;
    }

    // TODO: check here if new_timed_wait already exists in vector
    // or maybe use a map to check quicker, otherwise this is not
    // atomically as it should be.

    std::shared_ptr<TimedWait> new_timed_wait;
    {
        std::lock_guard<std::mutex> timed_waits_lock(timed_waits_mutex_);

        // The time has already passed.
        if (time_us <= time_us_) {
            errno = ETIMEDOUT;
            return -1;
        }

        new_timed_wait = std::make_shared<TimedWait>();
        {
            std::lock_guard<std::mutex> new_timed_wait_lock(new_timed_wait->mutex);
            new_timed_wait->time_us = time_us;
            new_timed_wait->cond = cond;
            new_timed_wait->lock = lock;
        }
        timed_waits_.push_back(new_timed_wait);

        // We need to unlock before we wait on the condition.
        sem_post(new_timed_wait->lock);
    }

    while (true) {
        int result = sem_wait(cond);
        {
            std::lock_guard<std::mutex> new_timed_wait_lock(new_timed_wait->mutex);

            if (result == -1 && errno == EINTR) {
                continue;

            } else if (new_timed_wait->timeout) {
                result = -1;
                errno = ETIMEDOUT;
                // We need to lock again before returning.
                sem_wait(new_timed_wait->lock);
                return result;

            } else {
                result = 0;
                new_timed_wait->done = true;
                // We need to lock again before returning.
                sem_wait(new_timed_wait->lock);
                return result;
            }
        }
    }
}

int LockstepScheduler::usleep_until(uint64_t time_us)
{
    sem_t cond;
    sem_init(&cond, 0, 0);

    sem_t lock;
    sem_init(&lock, 0, 0);

    int result = cond_timedwait(&cond, &lock, time_us);

    if (result == -1 && errno == ETIMEDOUT) {
        // This is expected because we never posted to the
        // semaphore.
        errno = 0;
        result = 0;
    }

    sem_destroy(&lock);
    sem_destroy(&cond);

    return result;
}
