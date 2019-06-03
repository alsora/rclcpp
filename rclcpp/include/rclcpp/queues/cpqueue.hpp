#ifndef __CONSUMERPRODUCERQUEUE_HPP__
#define __CONSUMERPRODUCERQUEUE_HPP__

#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class ConsumerProducerQueue
{
    std::condition_variable cond;
    std::mutex mutex;
    size_t maxSize;
    std::queue<T> cpq;

    // if true, this queue is storing shared_ptr<MessageT>
    // if false, this queue is storing unique_ptr<MessageT>
    bool is_shared_ptr_queue;

public:
    ConsumerProducerQueue(size_t mxsz, bool is_shared)
    : maxSize(mxsz), is_shared_ptr_queue(is_shared)
    { }

    void add(T request)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]()
            { return !isFull(); });
        cpq.push(request);
        lock.unlock();
        cond.notify_all();
    }

    void consume(T &request)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]()
            { return !isEmpty(); });
        request = cpq.front();
        cpq.pop();
        lock.unlock();
        cond.notify_all();

    }

    bool isFull() const
    {
        return cpq.size() >= maxSize;
    }

    bool isEmpty() const
    {
        return cpq.size() == 0;
    }

    int length() const
    {
        return cpq.size();
    }

    bool is_shared_ptr() const
    {
        return is_shared_ptr_queue;
    }

    void clear()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!isEmpty())
        {
            cpq.pop();
        }
        lock.unlock();
        cond.notify_all();
    }
};

#endif