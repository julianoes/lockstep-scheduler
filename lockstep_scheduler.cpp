#include "lockstep_scheduler.h"
#include <signal.h>
#include <string.h>
#include <cstring>

static void sig_handler(int /*signo*/);
static void register_sig_handler();
static void unregister_sig_handler();

LockstepScheduler::LockstepScheduler()
{
    {
        std::lock_guard<std::mutex> lock(waiting_thread_mutex_);
        waiting_thread_ = pthread_self();
        register_sig_handler();
    }
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
    std::lock_guard<std::mutex> lock_time_us(time_us_mutex_);
    time_us_ = time_us;
    {
        std::lock_guard<std::mutex> lock_waiting_thread(waiting_thread_mutex_);
        pthread_kill(waiting_thread_, SIGUSR1);
    }
}

int LockstepScheduler::sem_timedwait(sem_t sem, uint64_t timeout_us)
{
    if (0 == sem_trywait(&sem)) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock_time_us(time_us_mutex_);
        std::lock_guard<std::mutex> lock_timeout_time_us(timeout_time_us_mutex_);
        timeout_time_us_ = time_us_ + timeout_us;
    }

    while (true) {
        {
            std::lock_guard<std::mutex> lock(waiting_thread_mutex_);
            waiting_thread_ = pthread_self();
            register_sig_handler();
        }

        int ret = sem_wait(&sem);
        if (ret == 0) {
            return 0;

        } else if (ret == -1 && errno == EINTR) {
            {
                std::lock_guard<std::mutex> lock_time_us(time_us_mutex_);
                std::lock_guard<std::mutex> lock_timeout_time_us(timeout_time_us_mutex_);
                if (timeout_time_us_ <= time_us_) {
                    errno = ETIMEDOUT;
                    return -1;
                }
            }
        }
    }
}

static void sig_handler(int /*signo*/) {}

static void register_sig_handler()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = sig_handler;
    sigaction(SIGUSR1, &sa, NULL);
}

static void unregister_sig_handler()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGUSR1, &sa, NULL);
}
