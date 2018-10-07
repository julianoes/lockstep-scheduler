#include "lockstep_scheduler.h"
#include <cassert>
#include <thread>
#include <atomic>
#include <random>
#include <iostream>
#include <chrono>

constexpr uint64_t some_time_us = 12345678;

#define WAIT_FOR(condition_) \
    while (!(condition_)) { \
        std::this_thread::yield(); \
    }

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

    enum class Step {
        Init,
        ThreadStarted,
        BeforeTimedWait,
        TimeoutNotTriggeredYet,
        TimeoutTriggered
    };

    std::atomic<Step> step{Step::Init};

    // Use a thread to advance the time later.
    std::thread thread([&ls, &step]() {
        step = Step::ThreadStarted;

        WAIT_FOR(step == Step::BeforeTimedWait);

        step = Step::TimeoutNotTriggeredYet;
        ls.set_absolute_time(some_time_us + 500);

        step = Step::TimeoutTriggered;
        ls.set_absolute_time(some_time_us + 1500);
    });

    WAIT_FOR(step == Step::ThreadStarted);

    step = Step::BeforeTimedWait;

    assert(ls.sem_timedwait(&sem, some_time_us + 1000) == -1);
    assert(errno == ETIMEDOUT);
    assert(step == Step::TimeoutTriggered);

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

    enum class Step {
        Init,
        ThreadStarted,
        BeforeTimedWait,
        TimeoutNotTriggeredYet,
        SemaphoreTriggered
    };

    std::atomic<Step> step{Step::Init};

    // Use a thread to unlock semaphore.
    std::thread thread([&ls, &sem, &step]() {
        step = Step::ThreadStarted;

        WAIT_FOR(step == Step::BeforeTimedWait);

        step = Step::TimeoutNotTriggeredYet;
        ls.set_absolute_time(some_time_us + 500);


        step = Step::SemaphoreTriggered;
        // Unlock semaphore.
        sem_post(&sem);
    });

    WAIT_FOR(step == Step::ThreadStarted);

    step = Step::BeforeTimedWait;
    assert(ls.sem_timedwait(&sem, some_time_us + 1000) == 0);
    assert(step == Step::SemaphoreTriggered);

    thread.join();

    sem_destroy(&sem);
}

class TestCase {
public:
    TestCase(unsigned timeout, unsigned unlocked_after, LockstepScheduler &ls) :
        timeout_(timeout + some_time_us),
        unlocked_after_(unlocked_after + some_time_us),
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

    void check()
    {
        if (is_done_) {
            return;
        }

        uint64_t time_us = ls_.get_absolute_time();

        const bool unlock_reached = (time_us >= unlocked_after_);
        const bool unlock_is_before_timeout = (unlocked_after_ <= timeout_);
        const bool timeout_reached = (time_us >= timeout_);

        if (unlock_reached && unlock_is_before_timeout && !(timeout_reached)) {
            sem_post(&sem_);
            is_done_ = true;
            // We can be sure that this triggers.
            thread_->join();
            assert(result_ == 0);
        }

        else if (timeout_reached) {
            is_done_ = true;
            thread_->join();
            assert(result_ == -1);
        }
    }
private:
    static constexpr int INITIAL_RESULT = 42;

    unsigned timeout_;
    unsigned unlocked_after_;
    sem_t sem_;
    LockstepScheduler &ls_;
    std::atomic<bool> is_done_{false};
    std::atomic<int> result_ {INITIAL_RESULT};
    std::shared_ptr<std::thread> thread_{};
};

int random_number(int min, int max)
{
    // We want predictable test results, so we always
    // start with the seed 0.
    static int iteration = 0;

    std::seed_seq seed{iteration++};
    std::default_random_engine engine{seed};
    std::uniform_int_distribution<> distribution(min, max);

    const int random_number = distribution(engine);
    return random_number;
}

void test_multiple_semaphores_waiting()
{

    const int num_threads = random_number(1, 20);

    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    // Use different timeouts in random order.
    std::vector<std::shared_ptr<TestCase>> test_cases{};
    for (int i = 0; i < num_threads; ++i) {

        test_cases.push_back(
            std::make_shared<TestCase>(
                random_number(1, 20000), random_number(1, 20000), ls));
    }

    for (auto &test_case : test_cases) {
        test_case->run();
    }

    for (unsigned time_us = 1; time_us < 20000; time_us += random_number(1, 100)) {
        ls.set_absolute_time(some_time_us + time_us);
        for (auto &test_case : test_cases) {
            test_case->check();
        }
    }

    test_cases.clear();
}

void test_usleep()
{
    LockstepScheduler ls;
    ls.set_absolute_time(some_time_us);

    enum class Step {
        Init,
        ThreadStarted,
        BeforeUsleep,
        UsleepNotTriggeredYet,
        UsleepTriggered
    };

    std::atomic<Step> step{Step::Init};

    std::thread thread([&step, &ls]() {
        step = Step::ThreadStarted;

        WAIT_FOR(step == Step::BeforeUsleep);

        step = Step::UsleepNotTriggeredYet;
        ls.set_absolute_time(some_time_us + 500);

        step = Step::UsleepTriggered;
        ls.set_absolute_time(some_time_us + 1500);
    });

    WAIT_FOR(step == Step::ThreadStarted);

    step = Step::BeforeUsleep;

    assert(ls.usleep_until(some_time_us + 1000) == 0);
    assert(step == Step::UsleepTriggered);
    thread.join();
}

int main(int /*argc*/, char** /*argv*/)
{
    for (int iteration = 1; iteration <= 10000; ++iteration) {
        std::cout << "Test iteration: " << iteration << std::endl;
        test_absolute_time();
        test_unlocked_semaphore();
        test_locked_semaphore_timing_out();
        test_locked_semaphore_getting_unlocked();
        test_usleep();
        test_multiple_semaphores_waiting();
    }

    return 0;
}
