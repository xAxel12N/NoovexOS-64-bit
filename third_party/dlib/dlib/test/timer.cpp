// Copyright (C) 2007  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.


#include <sstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <vector>

#include <dlib/timer.h>
#include <dlib/timeout.h>
#include "tester.h"

namespace  
{

    using namespace test;
    using namespace std;
    using namespace dlib;

    logger dlog("test.timer");

    class timer_test_helper
    {
    public:
        dlib::mutex m;
        dlib::signaler count_changed;
        int count;
        int delayed_add_started_count;
        std::vector<dlib::uint64> callback_times;
        dlib::uint64 timestamp;
        dlib::timestamper ts;

        timer_test_helper():
            count_changed(m),
            count(0),
            delayed_add_started_count(0),
            timestamp(0)
        {}

        void add() 
        { 
            auto_mutex lock(m);
            ++count;
            callback_times.push_back(ts.get_timestamp());
            count_changed.broadcast();
        }

        void delayed_add()
        {
            {
                auto_mutex lock(m);
                ++delayed_add_started_count;
                count_changed.broadcast();
            }
            dlib::sleep(1000);
            print_spinner();
            add();
        }

        int get_count()
        {
            auto_mutex lock(m);
            return count;
        }

        void reset_count()
        {
            auto_mutex lock(m);
            count = 0;
            delayed_add_started_count = 0;
            callback_times.clear();
        }

        uint64 get_callback_time (
            size_t index
        )
        {
            auto_mutex lock(m);
            return callback_times[index];
        }

        bool wait_for_count (
            int expected_count,
            unsigned long timeout_ms
        )
        {
            return wait_for_value(count, expected_count, timeout_ms);
        }

        bool wait_for_delayed_add_to_start (
            unsigned long timeout_ms
        )
        {
            return wait_for_value(delayed_add_started_count, 1, timeout_ms);
        }

        void set_timestamp()
        {
            m.lock();
            timestamp = ts.get_timestamp();
            dlog << LTRACE << "in set_timestamp(), time is " << timestamp;
            dlib::sleep(1);
            print_spinner();
            m.unlock();
        }

    private:
        bool wait_for_value (
            const int& value,
            int expected_value,
            unsigned long timeout_ms
        )
        {
            const uint64 timeout_time = ts.get_timestamp() +
                static_cast<uint64>(timeout_ms)*1000;
            auto_mutex lock(m);

            while (value < expected_value)
            {
                const uint64 current_time = ts.get_timestamp();
                if (current_time >= timeout_time)
                    return false;

                const uint64 remaining_microseconds = timeout_time - current_time;
                const unsigned long remaining_milliseconds = static_cast<unsigned long>(
                    (remaining_microseconds + 999)/1000
                );
                count_changed.wait_or_timeout(remaining_milliseconds);
            }
            return true;
        }
    };

    template <
        typename timer_t
        >
    void timer_test2 (
    )
    /*!
        requires
            - timer_t is an implementation of dlib/timer/timer_abstract.h is instantiated 
              timer_test_helper
        ensures
            - runs tests on timer_t for compliance with the specs 
    !*/
    {        
        for (int j = 0; j < 4; ++j)
        {
            print_spinner();
            timer_test_helper h;

            timer_t t1(h,&timer_test_helper::set_timestamp);
            t1.set_delay_time(0);
            dlog << LTRACE << "t1.start()";
            t1.start();

            dlib::sleep(60);
            print_spinner();
            t1.stop_and_wait();

            dlib::uint64 cur_time = h.ts.get_timestamp();
            dlog << LTRACE << "get current time: " << cur_time;

            // make sure the action function has been called recently
            DLIB_TEST_MSG((cur_time-h.timestamp)/1000 < 30, (cur_time-h.timestamp)/1000);

        }
    }

    template <
        typename timer_t
        >
    void timer_test (
    )
    /*!
        requires
            - timer_t is an implementation of dlib/timer/timer_abstract.h is instantiated 
              timer_test_helper
        ensures
            - runs tests on timer_t for compliance with the specs 
    !*/
    {        

        print_spinner();
        for (int j = 0; j < 3; ++j)
        {
            timer_test_helper h;
            timer_test_helper h2;
            timer_test_helper h3;

            timer_t t1(h,&timer_test_helper::add);
            timer_t t2(h2,&timer_test_helper::add);
            timer_t t3(h3,&timer_test_helper::add);

            DLIB_TEST(t1.delay_time() == 1000);
            DLIB_TEST(t2.delay_time() == 1000);
            DLIB_TEST(t3.delay_time() == 1000);
            DLIB_TEST(t1.is_running() == false);
            DLIB_TEST(t2.is_running() == false);
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t1.action_function() == &timer_test_helper::add);
            DLIB_TEST(t2.action_function() == &timer_test_helper::add);
            DLIB_TEST(t3.action_function() == &timer_test_helper::add);
            DLIB_TEST(&t1.action_object() == &h);
            DLIB_TEST(&t2.action_object() == &h2);
            DLIB_TEST(&t3.action_object() == &h3);

            t1.set_delay_time(1000);
            t2.set_delay_time(500);
            t3.set_delay_time(1500);

            DLIB_TEST(t1.delay_time() == 1000);
            DLIB_TEST(t2.delay_time() == 500);
            DLIB_TEST(t3.delay_time() == 1500);
            DLIB_TEST(t1.is_running() == false);
            DLIB_TEST(t2.is_running() == false);
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t1.action_function() == &timer_test_helper::add);
            DLIB_TEST(t2.action_function() == &timer_test_helper::add);
            DLIB_TEST(t3.action_function() == &timer_test_helper::add);
            DLIB_TEST(&t1.action_object() == &h);
            DLIB_TEST(&t2.action_object() == &h2);
            DLIB_TEST(&t3.action_object() == &h3);
            dlib::sleep(1100);
            print_spinner();
            DLIB_TEST(h.get_count() == 0);
            DLIB_TEST(h2.get_count() == 0);
            DLIB_TEST(h3.get_count() == 0);

            t1.stop_and_wait();
            t2.stop_and_wait();
            t3.stop_and_wait();

            dlib::sleep(1100);
            print_spinner();
            DLIB_TEST(h.get_count() == 0);
            DLIB_TEST(h2.get_count() == 0);
            DLIB_TEST(h3.get_count() == 0);
            DLIB_TEST(t1.delay_time() == 1000);
            DLIB_TEST(t2.delay_time() == 500);
            DLIB_TEST(t3.delay_time() == 1500);
            DLIB_TEST(t1.is_running() == false);
            DLIB_TEST(t2.is_running() == false);
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t1.action_function() == &timer_test_helper::add);
            DLIB_TEST(t2.action_function() == &timer_test_helper::add);
            DLIB_TEST(t3.action_function() == &timer_test_helper::add);
            DLIB_TEST(&t1.action_object() == &h);
            DLIB_TEST(&t2.action_object() == &h2);
            DLIB_TEST(&t3.action_object() == &h3);

            t1.start();
            t2.start();
            t3.start();

            DLIB_TEST(t1.delay_time() == 1000);
            DLIB_TEST(t2.delay_time() == 500);
            DLIB_TEST(t3.delay_time() == 1500);
            DLIB_TEST(t1.is_running() == true);
            DLIB_TEST(t2.is_running() == true);
            DLIB_TEST(t3.is_running() == true);
            DLIB_TEST(t1.action_function() == &timer_test_helper::add);
            DLIB_TEST(t2.action_function() == &timer_test_helper::add);
            DLIB_TEST(t3.action_function() == &timer_test_helper::add);
            DLIB_TEST(&t1.action_object() == &h);
            DLIB_TEST(&t2.action_object() == &h2);
            DLIB_TEST(&t3.action_object() == &h3);

            t1.stop();
            t2.stop();
            t3.stop();

            DLIB_TEST(t1.delay_time() == 1000);
            DLIB_TEST(t2.delay_time() == 500);
            DLIB_TEST(t3.delay_time() == 1500);
            DLIB_TEST(t1.is_running() == false);
            DLIB_TEST(t2.is_running() == false);
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t1.action_function() == &timer_test_helper::add);
            DLIB_TEST(t2.action_function() == &timer_test_helper::add);
            DLIB_TEST(t3.action_function() == &timer_test_helper::add);
            DLIB_TEST(&t1.action_object() == &h);
            DLIB_TEST(&t2.action_object() == &h2);
            DLIB_TEST(&t3.action_object() == &h3);

            DLIB_TEST(h.get_count() == 0);
            DLIB_TEST(h2.get_count() == 0);
            DLIB_TEST(h3.get_count() == 0);
            dlib::sleep(1100);
            print_spinner();
            DLIB_TEST(h.get_count() == 0);
            DLIB_TEST(h2.get_count() == 0);
            DLIB_TEST(h3.get_count() == 0);

            for (int i = 1; i <= 3; ++i)
            {
                const uint64 t1_start_time = h.ts.get_timestamp();
                t1.start();
                const uint64 t2_start_time = h2.ts.get_timestamp();
                t2.start();
                const uint64 t3_start_time = h3.ts.get_timestamp();
                t3.start();

                DLIB_TEST(t1.is_running() == true);
                DLIB_TEST(t2.is_running() == true);
                DLIB_TEST(t3.is_running() == true);

                // Wait for each timer to complete the expected number of callbacks.
                // This tests repeated firing without assuming the runner schedules each
                // callback within a narrow wall-clock window.
                DLIB_TEST_MSG(h.wait_for_count(i, 10000),
                    "t1 count: " << h.get_count() << " i: " << i);
                t1.stop_and_wait();
                DLIB_TEST_MSG(h2.wait_for_count(3*i, 10000),
                    "t2 count: " << h2.get_count() << " i: " << i);
                t2.stop_and_wait();
                DLIB_TEST_MSG(h3.wait_for_count(i, 10000),
                    "t3 count: " << h3.get_count() << " i: " << i);
                t3.stop_and_wait();

                DLIB_TEST_MSG(h.get_count() == i,
                    "t1 count: " << h.get_count() << " i: " << i);
                DLIB_TEST_MSG(h2.get_count() == 3*i,
                    "t2 count: " << h2.get_count() << " i: " << i);
                DLIB_TEST_MSG(h3.get_count() == i,
                    "t3 count: " << h3.get_count() << " i: " << i);

                // Check the callback timestamps rather than when this test thread
                // happens to wake up.  A heavily loaded runner may observe them late,
                // but the timers must not fire early or ignore their configured delay.
                const uint64 early_tolerance = 10*1000;
                const uint64 callback_timeout = 10*1000*1000;
                const uint64 t1_callback_time = h.get_callback_time(i-1);
                const uint64 t2_callback_time1 = h2.get_callback_time(3*(i-1));
                const uint64 t2_callback_time2 = h2.get_callback_time(3*(i-1) + 1);
                const uint64 t2_callback_time3 = h2.get_callback_time(3*(i-1) + 2);
                const uint64 t3_callback_time = h3.get_callback_time(i-1);

                DLIB_TEST(t1_callback_time + early_tolerance >= t1_start_time + 1000*1000);
                DLIB_TEST(t1_callback_time <= t1_start_time + callback_timeout);
                DLIB_TEST(t2_callback_time1 + early_tolerance >= t2_start_time + 500*1000);
                DLIB_TEST(t2_callback_time2 + early_tolerance >= t2_callback_time1 + 500*1000);
                DLIB_TEST(t2_callback_time3 + early_tolerance >= t2_callback_time2 + 500*1000);
                DLIB_TEST(t2_callback_time3 <= t2_start_time + callback_timeout);
                DLIB_TEST(t3_callback_time + early_tolerance >= t3_start_time + 1500*1000);
                DLIB_TEST(t3_callback_time <= t3_start_time + callback_timeout);
            }


            t1.stop_and_wait();

            h.reset_count();
            const uint64 adjusted_timer_start_time = h.ts.get_timestamp();
            t1.start();
            t1.set_delay_time(400);
            DLIB_TEST_MSG(h.wait_for_count(1, 10000), h.get_count());
            DLIB_TEST_MSG(h.wait_for_count(2, 10000), h.get_count());

            const uint64 adjusted_callback_time1 = h.get_callback_time(0);
            const uint64 adjusted_callback_time2 = h.get_callback_time(1);
            DLIB_TEST(adjusted_callback_time1 + 10*1000 >=
                adjusted_timer_start_time + 400*1000);
            DLIB_TEST(adjusted_callback_time1 <=
                adjusted_timer_start_time + 10*1000*1000);
            DLIB_TEST(adjusted_callback_time2 + 10*1000 >=
                adjusted_callback_time1 + 400*1000);
            DLIB_TEST(adjusted_callback_time2 <=
                adjusted_callback_time1 + 10*1000*1000);
            t1.stop_and_wait();
            t1.clear();

            h.reset_count();
            const uint64 increased_timer_start_time = h.ts.get_timestamp();
            t1.start();
            t1.set_delay_time(2000);
            DLIB_TEST_MSG(h.wait_for_count(1, 10000), h.get_count());
            const uint64 increased_callback_time = h.get_callback_time(0);
            DLIB_TEST(increased_callback_time + 10*1000 >=
                increased_timer_start_time + 2000*1000);
            DLIB_TEST(increased_callback_time <=
                increased_timer_start_time + 10*1000*1000);
            t1.stop_and_wait();
            t1.clear();

            h3.reset_count();
            t3.start();
            DLIB_TEST(t3.is_running() == true);
            DLIB_TEST(t3.delay_time() == 1500);
            DLIB_TEST_MSG(h3.get_count() == 0,h3.get_count());
            t3.clear();
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t3.delay_time() == 1000);
            DLIB_TEST_MSG(h3.get_count() == 0,h3.get_count());
            dlib::sleep(200);
            print_spinner();
            DLIB_TEST(t3.is_running() == false);
            DLIB_TEST(t3.delay_time() == 1000);
            DLIB_TEST_MSG(h3.get_count() == 0,h3.get_count());


            {
                h.reset_count();
                timer_t t4(h,&timer_test_helper::delayed_add);
                t4.set_delay_time(100);
                t4.start();
                DLIB_TEST_MSG(h.wait_for_delayed_add_to_start(10000),
                    "count: " << h.get_count());
                DLIB_TEST_MSG(h.get_count() == 0,h.get_count());
                t4.stop_and_wait();
                DLIB_TEST_MSG(h.get_count() == 1,h.get_count());
                DLIB_TEST(t4.is_running() == false);
            }

            {
                h.reset_count();
                timer_t t4(h,&timer_test_helper::delayed_add);
                t4.set_delay_time(100);
                t4.start();
                DLIB_TEST_MSG(h.wait_for_delayed_add_to_start(10000),
                    "count: " << h.get_count());
                DLIB_TEST_MSG(h.get_count() == 0,h.get_count());
                t4.clear();
                DLIB_TEST(t4.is_running() == false);
                DLIB_TEST_MSG(h.get_count() == 0,h.get_count());
                t4.stop_and_wait();
                DLIB_TEST_MSG(h.get_count() == 1,h.get_count());
                DLIB_TEST(t4.is_running() == false);
            }

            {
                h.reset_count();
                timer_t t5(h,&timer_test_helper::delayed_add);
                t5.set_delay_time(100);
                t5.start();
                DLIB_TEST_MSG(h.wait_for_delayed_add_to_start(10000),
                    "count: " << h.get_count());
                DLIB_TEST_MSG(h.get_count() == 0,h.get_count());
            }
            DLIB_TEST_MSG(h.get_count() == 1,h.get_count());

        }

    }




    class timer_tester : public tester
    {
    public:
        timer_tester (
        ) :
            tester ("test_timer",
                    "Runs tests on the timer component.")
        {}

        void perform_test (
        )
        {
            dlog << LINFO << "testing timer_heavy with test_timer";
            timer_test<timer_heavy<timer_test_helper> >  ();
            dlog << LINFO << "testing timer_heavy with test_timer2";
            timer_test2<timer_heavy<timer_test_helper> >  ();

            dlog << LINFO << "testing timer with test_timer";
            timer_test<timer<timer_test_helper> >  ();
            dlog << LINFO << "testing timer with test_timer2";
            timer_test2<timer<timer_test_helper> >  ();
        }
    } a;

}
