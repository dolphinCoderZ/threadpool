//
// Created by ZHL on 2024/3/21.
//

#ifndef THREADPOOL_THREADPOOL_H
#define THREADPOOL_THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

// Any是一个类，如何通过构造函数让Any可以接收任意类型
// 1.构造函数是模板 2.数据存到派生类，派生类是一个类模板，可以接收任意类型
// 3.Any构造函数直接new派生类，用基类指针接收
class Any {
public:
  Any() = default;
  ~Any() = default;
  Any(const Any &) = delete;
  Any &operator=(const Any &) = delete;
  Any(Any &&) = default;
  Any &operator=(Any &&) = default;

  // 构造函数是函数模板，返回结果时会匹配Any的构造函数，数据就放入了派生类
  template <typename T>
  Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

  template <typename T> T cast_() {
    auto *p = dynamic_cast<Derive<T> *>((base_).get());
    if (p == nullptr) {
      throw "type is not match";
    }
    return p->data_;
  }

private:
  class Base {
  public:
    virtual ~Base() = default;
  };

  template <class T> class Derive : public Base {
  public:
    explicit Derive(T data) : data_(data) {}
    T data_;
  };

private:
  // 基类类型可以接收派生类类型
  std::unique_ptr<Base> base_;
};

class Semaphore {
public:
  explicit Semaphore(size_t limit = 0);
  ~Semaphore();
  void wait();
  void post();

private:
  size_t resLimit_;
  std::mutex mtx_;
  std::condition_variable cond_;
  std::atomic_bool isExit_;
};

class Task;
class Result {
  friend class Task;

public:
  explicit Result(std::shared_ptr<Task> task, bool isValid = true);
  ~Result();
  Any get();

private:
  Any any_;                  // 存储对应任务的返回值
  std::atomic_bool isValid_; // 返回值是否有效
  Semaphore sem_; // 线程通信信号量，用户get返回值可能被阻塞
  std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
  void setValue(Any any);
};

// 任务抽象基类，模板和虚函数不能写到一起，因为模板没有实例化之前没有虚函数的地址
// 因为不知道会接受什么样的任务，所以提供统一的接口
class Task {
  friend class Result;

public:
  Task();
  ~Task();
  // 用户可以自定义任务类型，从Task继承，并重写run，实现自定义任务处理
  virtual Any run() = 0;
  void exec();

private:
  // 设置任务的结果
  void setResult(Result *res);
  // 不能用强智能指针，否则会和task出现交叉引用问题
  Result *result_;
  // std::atomic_bool isDone;
};

class Thread {
public:
  using ThreadFunc = std::function<void(size_t)>;
  explicit Thread(ThreadFunc func);
  ~Thread();

  void startThread();
  [[nodiscard]] size_t getThreadId() const;

private:
  // 保存线程池提供的处理函数
  ThreadFunc func_;
  static size_t generateId;
  size_t threadId_;
};

enum class PoolMode {
  MODE_FIXED,  // 固定数量的线程
  MODE_CACHED, // 线程数量可动态增长
};

const size_t INIT_THREAD_SIZE = std::thread::hardware_concurrency();
const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_MAX_THRESHOLD = 10;
const int THREAD_MAX_FREE_TIME = 60;

class ThreadPool {
public:
  ThreadPool();
  ~ThreadPool();
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  void setMode(PoolMode mode);
  void setThreadMaxThreshold(size_t threshold);
  void setTaskMaxThreshold(size_t threshold);

  void startPool(size_t initSize = INIT_THREAD_SIZE);
  Result submitTask(const std::shared_ptr<Task> &sp);

private:
  // 线程池提供线程的执行逻辑
  void threadFunc(size_t id);
  [[nodiscard]] bool checkRunningState() const;

private:
  PoolMode poolMode_;
  std::atomic_bool isRunning_; // 记录线程池是否已经在工作

  size_t initSize_; // 初始化线程数量
  // std::vector<std::unique_ptr<Thread>> threads_; // fixed模式下缓存线程
  std::unordered_map<size_t, std::unique_ptr<Thread>> threads_;
  std::atomic_uint freeThread_;  // 线程池中的空闲线程数量
  std::atomic_uint threadCount_; // 当前线程池中创建的线程数量
  size_t threadMaxThreshold_;    // 线程池线程数量上限
  std::condition_variable exitCond_; // 线程完成任务通知线程池退出的条件变量

  std::condition_variable notFull_;
  std::condition_variable notEmpty_;

  // 指针接收实现多态，shared_ptr延长传入对象的生命周期
  std::queue<std::shared_ptr<Task>> tasksQueue_;
  std::mutex taskQueueMtx_;
  // 简单的内存和寄存器交换变量，使用CAS，不会改变线程的状态
  std::atomic_uint taskCount_; // 当前任务数量
  size_t taskMaxThreshold_;    // 任务数量上限
};

#endif // THREADPOOL_THREADPOOL_H
