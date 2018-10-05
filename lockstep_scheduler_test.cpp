#include "lockstep_scheduler.h"
#include <cassert>
#include <thread>
#include <atomic>
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

    sem_destroy(&sem);
}

void test_locked_semaphore_timing_out()
{
    // Create locked semaphore.
    sem_t sem;
    sem_init(&sem, 0, 000000000);

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

    sem_destroy(&sem);
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

    sem_destroy(&sem);
}

class TestCase {
public:
    TestCase(unsigned timeout, unsigned unlocked_after, LockstepScheduler &ls) :
        timeout_(timeout),
        unlocked_after_(unlocked_after),
        ls_(ls)
    {
        sem_init(&sem_, 0, 0);
    }

    ~TestCase()
    {
        sem_destroy(&sem_);
    }

    void run()
    {
        thread_ = std::make_shared<std::thread>([this]() {
            result_ = ls_.sem_timedwait(&sem_, timeout_);
        });
    }

    void check(uint64_t time_us)
    {
        if (is_done_) {
            return;
        }

        if (time_us >= unlocked_after_ && unlocked_after_ <= timeout_) {
            sem_post(&sem_);
            is_done_ = true;
            thread_->join();
            assert(result_ == 0);
        }

        else if (time_us >= timeout_) {
            is_done_ = true;
            thread_->join();
            assert(result_ == -1);
        }
    }
private:
    unsigned timeout_;
    unsigned unlocked_after_;
    sem_t sem_;
    LockstepScheduler &ls_;
    std::atomic<bool> is_done_{false};
    std::atomic<int> result_ {42};
    std::shared_ptr<std::thread> thread_{};
};

void test_multiple_semaphores_waiting()
{
    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use different timeouts in random order.
    std::vector<std::shared_ptr<TestCase>> test_cases{};
    test_cases.push_back(std::make_shared<TestCase>(1000,  500,   ls));
    test_cases.push_back(std::make_shared<TestCase>(10000, 5000,  ls));
    test_cases.push_back(std::make_shared<TestCase>(10000, 20000, ls));
    test_cases.push_back(std::make_shared<TestCase>(100,   200,   ls));

    for (auto &test_case : test_cases) {
        test_case->run();
    }

    for (unsigned time_us = 10; time_us < 15000; time_us += 10) {
        ls.set_absolute_time(some_time_us + time_us);
        for (auto &test_case : test_cases) {
            test_case->check(time_us);
        }
    }

    test_cases.clear();
}

void test_usleep_properly()
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

void test_usleep_with_zero()
{
    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use a thread to advance the time later.
    bool usleep_was_too_late = false;
    std::thread thread([&usleep_was_too_late]() {
        usleep(1000);
        usleep_was_too_late = true;
    });

    assert(!usleep_was_too_late);
    assert(ls.usleep(0) == 0);
    assert(!usleep_was_too_late);
    thread.join();
}

int main(int /*argc*/, char** /*argv*/)
{
    test_absolute_time();
    test_unlocked_semaphore();
    test_locked_semaphore_timing_out();
    test_locked_semaphore_getting_unlocked();
    test_usleep_properly();
    test_usleep_with_zero();
    test_multiple_semaphores_waiting();

    return 0;
}
