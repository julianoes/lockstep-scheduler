#include "lockstep_scheduler.h"
#include <signal.h>
#include <string.h>
#include <cstring>
#include <unistd.h>

auto constexpr chosen_signal = SIGUSR1;

static void sig_handler(int /*signo*/);
static void register_sig_handler();
static void unregister_sig_handler();


LockstepScheduler::LockstepScheduler()
{
    register_sig_handler();
}

LockstepScheduler::~LockstepScheduler()
{
    unregister_sig_handler();
}

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

            std::unique_lock<std::mutex> lock_timed_wait(timed_wait->get()->mutex);
            // Clean up the ones that are already done from last iteration.
            if (timed_wait->get()->done) {
                // We shouldn't delete a lock in use.
                lock_timed_wait.unlock();
                timed_wait = timed_waits_.erase(timed_wait);
                continue;
            }
            // Keep holding lock while we kill the thread so it doesn't go away
            // when it's done.
            if (timed_wait->get()->time_us <= time_us) {
                timed_wait->get()->timeout = true;
                // We are abusing the semaphore because the signal is sometimes
                // too slow and we get out of sync.
                sem_post(timed_wait->get()->sem);
                //pthread_kill(timed_wait->thread_id, chosen_signal);
                timed_wait->get()->done = true;
            }
            ++timed_wait;
        }
    }
}

int LockstepScheduler::sem_timedwait(sem_t *sem, uint64_t time_us)
{
    if (0 == sem_trywait(sem)) {

        return 0;
    }

    auto new_timed_wait = std::make_shared<TimedWait>();
    {
        std::lock_guard<std::mutex> timed_waits_lock(timed_waits_mutex_);

        {
            std::lock_guard<std::mutex> lock(new_timed_wait->mutex);
            new_timed_wait->thread_id = pthread_self();
            new_timed_wait->time_us = time_us;
            new_timed_wait->sem = sem;

            // The time has already passed.
            if (time_us <= time_us_) {
                errno = ETIMEDOUT;
                return -1;
            }

            timed_waits_.push_back(new_timed_wait);
        }
    }

    int result;

    while (true) {
        result = sem_wait(sem);
        {
            std::lock_guard<std::mutex> lock(new_timed_wait->mutex);

            if (result == -1 && errno == EINTR) {
                continue;

            } else if (new_timed_wait->timeout) {
                result = -1;
                errno = ETIMEDOUT;
                return result;
            } else {
                result = 0;
                new_timed_wait->done = true;
                return result;
            }
        }
        /*
        if (result == 0) {
            break;

        } else if (result == -1 && errno == EINTR) {
            {
                std::lock_guard<std::mutex> lock(new_timed_wait->mutex);
                if (new_timed_wait->timeout) {
                    errno = ETIMEDOUT;
                    break;
                }
            }
        }
        */
    }

}

int LockstepScheduler::usleep_until(uint64_t time_us)
{
    sem_t sem;
    sem_init(&sem, 0, 0);

    int result = sem_timedwait(&sem, time_us);

    if (result == -1 && errno == ETIMEDOUT) {
        // This is expected:
        errno = 0;
        result = 0;
    }

    sem_destroy(&sem);

    return result;
}

static void sig_handler(int /*signo*/) {}

static void register_sig_handler()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = sig_handler;
    sigaction(chosen_signal, &sa, NULL);
}

static void unregister_sig_handler()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, NULL);
}
