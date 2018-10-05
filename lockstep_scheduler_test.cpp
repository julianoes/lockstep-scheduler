#include "lockstep_scheduler.h"
#include <cassert>
#include <thread>
#include <unistd.h>

constexpr uint64_t some_time_us = 12345678;

void test_absolute_time()
{
    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);
    assert(ls.get_absolute_time() == some_time_us);
}

void test_unlocked_semaphore()
{
    // Create unlocked semaphore.
    sem_t sem;
    sem_init(&sem, 0, 1);

    LockstepScheduler ls;
    uint64_t timeout_us = some_time_us;

    assert(ls.sem_timedwait(&sem, timeout_us) == 0);
}

void test_locked_semaphore_timing_out()
{
    // Create unlocked semaphore.
    sem_t sem;
    sem_init(&sem, 0, 1);
    // And lock it.
    sem_wait(&sem);

    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use a thread to advance the time later.
    bool should_trigger_timeout = false;
    std::thread thread([&ls, &should_trigger_timeout]() {
        usleep(1000);
        ls.set_absolute_time(some_time_us + 500);

        usleep(1000);
        should_trigger_timeout = true;
        ls.set_absolute_time(some_time_us + 1500);
    });

    assert(!should_trigger_timeout);
    assert(ls.sem_timedwait(&sem, 1000) == -1);
    assert(should_trigger_timeout);
    assert(errno == ETIMEDOUT);
    thread.join();
}

void test_locked_semaphore_getting_unlocked()
{
    // Create unlocked semaphore.
    sem_t sem;
    sem_init(&sem, 0, 0);

    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use a thread to unlock semaphore.
    std::thread thread([&ls, &sem]() {
        usleep(1000);
        ls.set_absolute_time(some_time_us + 500);

        // Unlock semaphore.
        sem_post(&sem);

        usleep(1000);
        ls.set_absolute_time(some_time_us + 1500);
    });

    assert(ls.sem_timedwait(&sem, 1000) == 0);
    thread.join();
}

void test_usleep()
{
    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use a thread to advance the time later.
    bool usleep_should_be_done = false;
    std::thread thread([&ls, &usleep_should_be_done]() {
        usleep(1000);
        ls.set_absolute_time(some_time_us + 500);

        usleep(1000);
        usleep_should_be_done = true;
        ls.set_absolute_time(some_time_us + 1500);
    });

    assert(!usleep_should_be_done);
    assert(ls.usleep(1000) == 0);
    assert(usleep_should_be_done);
    thread.join();
}

int main(int /*argc*/, char** /*argv*/)
{
    test_absolute_time();
    test_unlocked_semaphore();
    test_locked_semaphore_timing_out();
    test_locked_semaphore_getting_unlocked();
    test_usleep();

    return 0;
}
