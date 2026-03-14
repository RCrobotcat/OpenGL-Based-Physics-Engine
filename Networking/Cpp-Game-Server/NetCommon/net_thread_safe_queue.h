#pragma once

#include "net_common.h"

// Thread-safe queue
namespace RCNet
{
    namespace net
    {
        template<typename T>
        class tsqueue
        {
        public:
            tsqueue() = default;

            // not allow copy construction
            // to prevent issues with multiple queues trying to manage the same data
            tsqueue(const tsqueue<T> &) = delete;

            virtual ~tsqueue()
            {
                clear();
            }

        public:
            // scoped_lock: 在作用域内自动加锁，离开作用域时自动解锁（异常安全）=> RAII
            // 并且可以一次性锁多个 mutex，避免死锁

            const T &front()
            {
                std::scoped_lock lock(muxQueue);
                return deqQueue.front();
            }

            const T &back()
            {
                std::scoped_lock lock(muxQueue);
                return deqQueue.back();
            }

            void push_back(const T &item)
            {
                std::scoped_lock lock(muxQueue);
                deqQueue.emplace_back(std::move(item));

                // 给条件变量配套的 mutex 上锁
                std::unique_lock<std::mutex> ul(muxBlocking);
                // 唤醒一个正在 cvBlocking.wait(...) 上睡眠的线程
                cvBlocking.notify_one();
                // 之后离开作用域时，ul 会自动解锁 => RAII
            }

            void push_front(const T &item)
            {
                std::scoped_lock lock(muxQueue);
                deqQueue.emplace_front(std::move(item));

                std::unique_lock<std::mutex> ul(muxBlocking);
                cvBlocking.notify_one();
            }

            bool empty()
            {
                std::scoped_lock lock(muxQueue);
                return deqQueue.empty();
            }

            size_t count()
            {
                std::scoped_lock lock(muxQueue);
                return deqQueue.size();
            }

            T pop_front()
            {
                std::scoped_lock lock(muxQueue);
                auto t = std::move(deqQueue.front());
                deqQueue.pop_front();
                return t;
            }

            T pop_back()
            {
                std::scoped_lock lock(muxQueue);
                auto t = std::move(deqQueue.back());
                deqQueue.pop_back();
                return t;
            }

            void clear()
            {
                std::scoped_lock lock(muxQueue);
                deqQueue.clear();
            }

            void wait()
            {
                // 当队列为空时，消费者线程不要傻转圈，而是睡眠
                // (这里的消费者指调用 tsqueue 的线程，即这里的服务端)
                // 当生产者 push 进来时把它叫醒
                 while (empty())
                 {
                     std::unique_lock<std::mutex> ul(muxBlocking);
                     cvBlocking.wait(ul);
                 }
            }

        protected:
            // muxQueue：保护队列本体 deqQueue 的读写安全
            std::mutex muxQueue;
            std::deque<T> deqQueue;

            // 条件变量 = 线程在“条件不满足时睡觉，条件满足时被唤醒”的同步机制
            // 队列空 => wait() 睡眠，阻塞消费者线程 (这里的消费者指调用 tsqueue 的线程，即这里的服务端)
            // push 新元素 => notify_one() 唤醒一个正在睡的消费者
            std::condition_variable cvBlocking;
            // muxBlocking：给条件变量 cvBlocking 配的锁
            // i.e. protect the condition variable
            std::mutex muxBlocking;
        };
    }
}
