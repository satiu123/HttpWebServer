#pragma once
#include <coroutine>
#include <exception>
#include <fmt/format.h>

// 简单的任务类，表示一个无返回值的协程
class Task {
public:
    struct promise_type {
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        // 立即开始执行协程
        std::suspend_never initial_suspend() { return {}; }
        
        // 重要：协程结束时暂停，让持有者决定何时销毁
        std::suspend_always final_suspend() noexcept { 
            // fmt::print("协程结束\n");
            return {}; 
        }
        
        void return_void() {}
        void unhandled_exception() { 
            fmt::print("协程发生未处理异常\n");
            std::terminate(); 
        }
    };
    
    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    
    // 移动构造函数，避免复制
    Task(Task&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }
    
    // 移动赋值，避免复制
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    
    // 禁止复制
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    ~Task() {
        if (handle) {
            fmt::print("销毁协程\n");
            handle.destroy();
        }
    }
    
    std::coroutine_handle<promise_type> handle;
};