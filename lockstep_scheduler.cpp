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
        std::lock_guard<std::mutex> lock(timed_waits_mutex_);
        for (auto &timed_wait : timed_waits_) {
            std::lock_guard<std::mutex> lock(timed_wait->mutex);
            if (timed_wait->done) {
                continue;
            }
            // Keep holding lock while we kill the thread so it doesn't go away
            // because it's done.
            pthread_kill(timed_wait->thread_id, chosen_signal);
        }
    }
    time_us_changed_.notify_all();
}

int LockstepScheduler::sem_timedwait(sem_t *sem, uint64_t timeout_us)
{
    if (0 == sem_trywait(sem)) {
        return 0;
    }

    auto new_timed_wait = std::make_shared<TimedWait>();
    new_timed_wait->timeout_time_us = time_us_ + timeout_us;

    // We need to use pthread_t instead of std::thread::id because we need to
    // use pthread_kill on it later which doesn't exist in std::thread.
    new_timed_wait->thread_id = pthread_self();

    {
        std::lock_guard<std::mutex> lock(timed_waits_mutex_);
        timed_waits_.push_back(new_timed_wait);
    }

    int result;

    while (true) {
        result = sem_wait(sem);
        if (result == 0) {
            break;

        } else if (result == -1 && errno == EINTR) {
            {
                std::lock_guard<std::mutex> lock_time_us(time_us_mutex_);
                std::lock_guard<std::mutex> lock_timed_wait(new_timed_wait->mutex);
                if (new_timed_wait->timeout_time_us <= time_us_) {
                    errno = ETIMEDOUT;
                    break;
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(new_timed_wait->mutex);
    new_timed_wait->done = true;
    return result;
}

int LockstepScheduler::usleep(uint64_t usec)
{
    if (usec == 0) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock_time_us(time_us_mutex_);
        std::lock_guard<std::mutex> usleep_time_us(usleep_time_us_mutex_);
        usleep_time_us_ = time_us_ + usec;
    }

    while (true) {
        if (usleep_time_us_ <= time_us_) {
            return 0;
        }

        std::unique_lock<std::mutex> lock(time_us_mutex_);
        time_us_changed_.wait(lock);

    }

    return 0;
}

static void sig_handler(int /*signo*/)
{
}

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
