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
    LockstepScheduler ls;
    sem_t sem;
    sem_init(&sem, 0, 1); // semaphore is not locked
    uint64_t timeout_us = some_time_us;
    assert(ls.sem_timedwait(sem, timeout_us) == 0);
}

void test_locked_semaphore()
{
    LockstepScheduler ls;
    sem_t sem;
    sem_init(&sem, 0, 1);
    sem_wait(&sem); // lock semaphore
    ls.set_absolute_time(some_time_us);
    const uint64_t timeout_us = 1000;
    std::thread t([&ls](){
        ls.set_absolute_time(some_time_us + 1500);
    });
    assert(ls.sem_timedwait(sem, timeout_us) == -1);
    assert(errno == ETIMEDOUT);
    t.join();
}

int main(int /*argc*/, char** /*argv*/)
{
    test_absolute_time();
    test_unlocked_semaphore();
    test_locked_semaphore();

    return 0;
}
