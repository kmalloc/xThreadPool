#ifndef XTHREADPOOL_XPACKAGEDTASK_H
#define XTHREADPOOL_XPACKAGEDTASK_H

#include <future>
#include <functional>

template<class> class xPackagedTask;

template<class R, class ...ARGS>
class xPackagedTask<R(ARGS...)>
{
public:
    xPackagedTask() = default;

    template<class F>
    explicit xPackagedTask(F&& f)
        : func_(std::forward<F>(f))
    {
        // type check for F
    }

    xPackagedTask(xPackagedTask<R(ARGS...)>&& other)
    {
        func_ = std::move(other.func_);
        promise_ = std::move(other.promise_);
    }

    xPackagedTask<R(ARGS...)>& operator=(xPackagedTask<R(ARGS...)>&& other)
    {
        if (this == &other) return *this;

        func_ = std::move(other.func_);
        promise_ = std::move(other.promise_);

        return *this;
    }

    std::future<R> get_future()
    {
        return promise_.get_future();
    }

    template<class ...A>
    void operator()(A&&... args)
    {
        // TODO: check the argument types, make sure they match the class signature.
        auto r = func_(std::forward<A>(args)...);
        promise_.set_value(std::move(r));
    }

private:
    // darnit, promise is not copyable.
    std::promise<R> promise_;
    std::function<R(ARGS...)> func_;
};

#endif //XTHREADPOOL_XPACKAGEDTASK_H
