#include "gtest/gtest.h"

#include "ThreadPool.h"

#include <atomic>

using namespace xthread;

std::atomic<int> g_test_pass{0};

void basic_testing()
{
    ThreadPool pool(4);

    bool f1 = false, f2 = false;
    std::mutex m1, m2;
    std::condition_variable cv1, cv2;

    std::atomic<int> i1{1}, i2{1}, s{0};

    auto dt = [&]() { s++; };

    for (auto i = 0; i < 1024; ++i)
    {
        pool.AddTask(dt);
    }

    std::vector<size_t> num = pool.GetTaskNum();

    ASSERT_EQ(4, num.size());
    ASSERT_EQ(256, num[0]);
    ASSERT_EQ(256, num[1]);
    ASSERT_EQ(256, num[2]);
    ASSERT_EQ(256, num[3]);

    pool.StartWorking();

    pool.AddTask([&]() { i1 = 233; std::unique_lock<std::mutex> lock{m1}; f1 = true; cv1.notify_one();});
    pool.AddTask([&]() { i2 = 234; std::unique_lock<std::mutex> lock{m2}; f2 = true; cv2.notify_one();});

    {
        std::unique_lock<std::mutex> lock{m1};
        while (!f1) cv1.wait(lock);
    }

    ASSERT_EQ(233, i1);

    {
        std::unique_lock<std::mutex> lock{m2};
        while (!f2) cv2.wait(lock);
    }

    ASSERT_EQ(234, i2);

    pool.CloseThread(true);
    pool.Shutdown();

    ASSERT_EQ(1024, s);
}

TEST(test_xthread, test_basic_thread)
{
    for (int i = 0; i < 1264; ++i)
    {
        std::cout << i << "-th basic test." << std::endl;
        basic_testing();
    }

    g_test_pass++;
}

void test_exit()
{
    ThreadPool pool(4);

    int s = 0;
    std::mutex m, m1, m2, m3, m4;
    std::atomic<int> v1{0}, v2{0}, v3{0}, v4{0};
    std::condition_variable cv, cv1, cv2, cv3, cv4;
    bool f1 = false, f2 = false, f3 = false, f4 = false;

    auto bt1 = [&]()
    {
        {
            std::unique_lock<std::mutex> lock{m};
            s++;
        }
        cv.notify_one();
        std::unique_lock<std::mutex> lock{m1};
        while (!f1) cv1.wait(lock);
    };
    auto bt2 = [&]()
    {
        {
            std::unique_lock<std::mutex> lock{m};
            s++;
        }
        cv.notify_one();
        std::unique_lock<std::mutex> lock{m2};
        while (!f2) cv2.wait(lock);
    };
    auto bt3 = [&]()
    {
        {
            std::unique_lock<std::mutex> lock{m};
            s++;
        }
        cv.notify_one();
        std::unique_lock<std::mutex> lock{m3};
        while (!f3) cv3.wait(lock);
    };
    auto bt4 = [&]()
    {
        {
            std::unique_lock<std::mutex> lock{m};
            s++;
        }
        cv.notify_one();
        std::unique_lock<std::mutex> lock{m4};
        while (!f4) cv4.wait(lock);
    };

    auto rt1 = [&]() { v1 = 10; };
    auto rt2 = [&]() { v2 = 20; };
    auto rt3 = [&]() { v3 = 30; };
    auto rt4 = [&]() { v4 = 40; };

    pool.AddTask([&]() { std::cout << "dummy lambda.\n"; });
    pool.AddTask(bt1);
    pool.AddTask(bt2);
    pool.AddTask(bt3);
    pool.AddTask(bt4);

    pool.StartWorking();

    // make sure all the threads are running.
    {
        std::unique_lock<std::mutex> lock{m};
        while (s != 4) cv.wait(lock);
    }

    // following 4 task should not have a chance to run.
    pool.AddTask(rt1);
    pool.AddTask(rt2);
    pool.AddTask(rt3);
    pool.AddTask(rt4);

    // cancel all the threads immediately.
    pool.CloseThread(false);

    {
        std::unique_lock<std::mutex> lock{m1};
        f1 = true;
    }
    cv1.notify_one();

    {
        std::unique_lock<std::mutex> lock{m2};
        f2 = true;
    }
    cv2.notify_one();

    {
        std::unique_lock<std::mutex> lock{m3};
        f3 = true;
    }
    cv3.notify_one();

    {
        std::unique_lock<std::mutex> lock{m4};
        f4 = true;
    }
    cv4.notify_one();

    pool.Shutdown();

    ASSERT_EQ(0, v1);
    ASSERT_EQ(0, v2);
    ASSERT_EQ(0, v3);
    ASSERT_EQ(0, v4);

    // test gracefully shutdown.
    s = 0;
    f1 = f2 = f3 = f4 = false;
    pool.StartWorking();

    pool.AddTask(bt1);
    pool.AddTask(bt2);
    pool.AddTask(bt3);
    pool.AddTask(bt4);

    pool.AddTask(rt1);
    pool.AddTask(rt2);
    pool.AddTask(rt3);
    pool.AddTask(rt4);

    // shutdown all the threads after the pending tasks are run.
    pool.CloseThread(true);

    {
        std::unique_lock<std::mutex> lock{m1};
        f1 = true;
    }
    cv1.notify_one();

    {
        std::unique_lock<std::mutex> lock{m2};
        f2 = true;
    }
    cv2.notify_one();

    {
        std::unique_lock<std::mutex> lock{m3};
        f3 = true;
    }
    cv3.notify_one();

    {
        std::unique_lock<std::mutex> lock{m4};
        f4 = true;
    }
    cv4.notify_one();

    pool.Shutdown();

    ASSERT_EQ(10, v1);
    ASSERT_EQ(20, v2);
    ASSERT_EQ(30, v3);
    ASSERT_EQ(40, v4);
}

TEST(test_xthread, test_exiting_pool)
{
    for (int i = 0; i < 1024; ++i)
    {
        test_exit();
    }

    g_test_pass++;
}

void test_poly_func_type()
{
    ThreadPool pool;
    pool.StartWorking();

    std::atomic<int> s{0};

    auto f1 = [](int a, std::string s) -> double
    {
        auto r = a + 3.1415;
        std::cout << "lambda with params & return value:" << s << std::endl;
        return r;
    };

    auto f2 = []() -> std::string
    {
        return std::string("hello");
    };

    auto f3 = [&s](int a) { s += a + 2; };

    auto r1 = pool.RunTask(f1, 1, "f1-1");
    auto r2 = pool.RunTask(f1, 2, "f1-2");
    auto r3 = pool.RunTask(f2);
    auto r4 = pool.AddTask(f3, 3);

    ASSERT_DOUBLE_EQ(4.1415, r1.get());
    ASSERT_DOUBLE_EQ(5.1415, r2.get());
    ASSERT_STREQ("hello", r3.get().c_str());
    ASSERT_TRUE(r4);
}

TEST(test_xthread, poly_func_test)
{
    test_poly_func_type();
}

TEST(test_xthread, performance_test)
{
    if (g_test_pass < 2) return;

    ThreadPool pool;
    pool.StartWorking();

    auto t1 = [&]() { int i = 0; while (i < 100000) { ++i; } };
    auto t2 = [&]() { int i = 0; while (i < 100000) { ++i; } };

    std::mutex m1, m2, m3;
    bool f1 = false, f2 = false, f3 = false;

    auto th1 = [&]()
    {
        while (1)
        {
            pool.AddTask(t1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::unique_lock<std::mutex> lock{m1};
            if (f1) break;
        }
    };

    auto th2 = [&]()
    {
        while (1)
        {
            pool.AddTask(t2);
            std::this_thread::yield();
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::unique_lock<std::mutex> lock{m2};
            if (f2) break;
        }
    };

    auto th3 = [&]()
    {
        while (1)
        {
            pool.AddTask(t2);
            std::this_thread::yield();
            // std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::unique_lock<std::mutex> lock{m3};
            if (f3) break;
        }
    };

    std::thread thread1(th1), thread2(th2), thread3(th3);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    {
        std::unique_lock<std::mutex> lock{m1};
        f1 = true;
    }

    {
        std::unique_lock<std::mutex> lock{m2};
        f2 = true;
    }

    {
        std::unique_lock<std::mutex> lock{m3};
        f3 = true;
    }

    thread1.join();
    thread2.join();
    thread3.join();

    std::cout << "\nnow waitting for all the tasks to finish, there are "
              << pool.GetThreadNum() << " workers working on it, "
              << "but it is still going to take long.\n" << std::endl;

    while (1)
    {
        int all = 0;
        auto num = pool.GetTaskNum();
        for (auto i: num)
        {
            all += i;
        }

        if (!all) break;

        std::cout << "(" << all << ") tasks remaining." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    pool.CloseThread(true);
}
