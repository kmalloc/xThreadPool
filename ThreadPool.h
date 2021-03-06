#ifndef _XTHREADPOOL_H_
#define _XTHREADPOOL_H_

// #include "xPackagedTask.h"

#include <deque>
#include <vector>
#include <memory>
#include <future>
#include <functional>

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace xthread {

    typedef std::function<void()> task_t;

    template <typename T, typename F>
    struct IsFunctor
    {
        typedef char small;
        struct big { char d[2]; };

        static T GetDummyT();
        static big  test(...);
        static small test(F&&);

        enum { value = sizeof(test(GetDummyT())) == sizeof(small) };
    };

    class TaskQueue
    {
    public:
        void Clear()
        {
            std::unique_lock<std::mutex> lock(m_);
            exit_ = false;
            tasks_.clear();
        }

        size_t GetTaskNum() const
        {
            std::unique_lock<std::mutex> lock(m_);
            return tasks_.size();
        }

        bool Pop(task_t& f)
        {
            std::unique_lock<std::mutex> lock(m_);
            while (!exit_ && tasks_.empty()) cv_.wait(lock);

            if (exit_ && tasks_.empty()) return false;

            f = std::move(tasks_.front());
            tasks_.pop_front();

            return true;
        }

        bool TryPop(task_t &f)
        {
            std::unique_lock<std::mutex> lock{m_, std::try_to_lock};
            if (!lock || tasks_.empty()) return false;

            f = std::move(tasks_.front());
            tasks_.pop_front();
            return true;
        }

        template<typename T>
        bool Push(T&& f)
        {
            {
                std::unique_lock<std::mutex> lock{m_};
                tasks_.emplace_back(std::forward<T>(f));
            }

            cv_.notify_one();
            return true;
        }

        template<typename T>
        bool PushExitTask(T&& f, bool linger)
        {
            {
                std::unique_lock<std::mutex> lock{m_};
                if (!linger) tasks_.clear();

                exit_ = true;
                tasks_.emplace_back(std::move(f));
            }

            cv_.notify_one();
            return true;
        }

        template<typename T>
        bool TryPush(T&& f)
        {
            {
                std::unique_lock<std::mutex> lock{m_, std::try_to_lock};
                if (!lock) return false;

                tasks_.emplace_back(std::forward<T>(f));
            }

            cv_.notify_one();
            return true;
        }

    private:
        bool exit_ = false;
        mutable std::mutex m_;
        std::condition_variable cv_;

        std::deque<task_t> tasks_;
    };

    class ThreadPool
    {
    public:
        ThreadPool()
            : ThreadPool(std::thread::hardware_concurrency())
        {
        }

        explicit ThreadPool(int threadnum)
            : thread_num_(threadnum), run_(threadnum), queue_(threadnum)
        {
            threads_.reserve(threadnum);
        }

        ~ThreadPool()
        {
            Shutdown();
        }

        // may raise out-of-memory exception.
        template <typename T>
        bool AddTask(T&& f)
        {
            static_assert(IsFunctor<T, task_t>::value, "invalid function type for the thread pool.");

            const int sel = (sel_++) % thread_num_;
            for (auto i = 0; i < thread_num_; ++i)
            {
                auto& q = queue_[(i + sel) % thread_num_];
                if (q.TryPush(std::forward<T>(f))) return true;
            }

            return queue_[sel].Push(std::forward<T>(f));
        }

        template <class F, class ...ARG>
        bool AddTask(F&& f, ARG&& ...args)
        {
            static_assert(sizeof...(ARG), "false function type, argument required.");
            auto fun = std::bind(std::forward<F>(f), std::forward<ARG>(args)...);
            return AddTask(std::move(fun));
        };

        template <class F, class ...ARG>
        auto RunTask(F&& f, ARG&& ...args) -> std::future<typename std::result_of<F(ARG...)>::type>
        {
            using ret_type = typename std::result_of<F(ARG...)>::type;

            // task queue requires the task to be copyable, but packaged_task is only movable.
            // have to wrap it with a smart pointer.
            auto task = std::make_shared<std::packaged_task<ret_type()>>(
                    (std::bind(std::forward<F>(f), std::forward<ARG>(args)...)));

            auto res = task->get_future();
            AddTask([t = std::move(task)]() { (*t)(); });

            /*
            auto task = xPackagedTask<ret_type()>(std::bind(std::forward<F>(f), std::forward<ARG>(args)...));
            auto res = task.get_future();
            AddTask([t = std::move(task)]() mutable { t(); });
            */

            return res;
        }

        bool CloseThread(bool gracefully)
        {
            if (done_) return false;

            done_ = true;
            for (auto i = 0; i < thread_num_; ++i)
            {
                queue_[i].PushExitTask([this, i]() { run_[i]  = false; }, gracefully);
            }

            return true;
        }

        void Shutdown()
        {
            CloseThread(false);

            for (auto i = 0u; i < threads_.size(); ++i)
            {
                threads_[i].join();
            }

            for (auto i = 0u; i < queue_.size(); ++i)
            {
                queue_[i].Clear();
            }

            threads_.clear();
        }

        void StartWorking()
        {
            sel_ = 0; done_ = false;
            for (auto i = 0; i < thread_num_; ++i)
            {
                run_[i] = true;
                threads_.emplace_back([this, i]() { Entry(i); });
            }
        }

        std::vector<size_t> GetTaskNum() const
        {
            std::vector<size_t> num;

            num.reserve(thread_num_);
            for (const auto& q: queue_)
            {
                num.push_back(q.GetTaskNum());
            }

            return num;
        }

        int GetThreadNum() const { return thread_num_; }

    private:
        void Entry(int id)
        {
            // the i-th thread.
            while (run_[id])
            {
                task_t fun;

                // steal work if necessary & possible.
                for (auto i = 0; i < thread_num_; ++i)
                {
                    if (queue_[(id + i)%thread_num_].TryPop(fun)) break;
                }

                if (!fun && !queue_[id].Pop(fun)) break;

                fun();
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

    private:
        const int thread_num_;
        std::atomic<int> sel_{0};
        std::atomic<bool> done_{false};
        std::vector<std::atomic<bool>> run_;
        std::vector<TaskQueue> queue_;
        std::vector<std::thread> threads_;
    };

} // end xthread namespace

#endif // _XTHREADPOOL_H_

