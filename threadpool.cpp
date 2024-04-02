// Created by ZHL on 2024/3/21.

#include "threadpool.h"

#include <utility>

ThreadPool::ThreadPool()
    : poolMode_(PoolMode::MODE_FIXED), isRunning_(false), initSize_(0),
      freeThread_(0), threadCount_(0),
      threadMaxThreshold_(THREAD_MAX_THRESHOLD), taskCount_(0),
      taskMaxThreshold_(TASK_MAX_THRESHOLD) {}

ThreadPool::~ThreadPool()
{
  isRunning_ = false;

  // 退出时，线程状态：正在阻塞 & 正在执行任务 & [抢锁后，阻塞前)
  std::unique_lock<std::mutex> lock(taskQueueMtx_);
  // 先抢锁，再唤醒等待任务的线程，如果先唤醒，唤醒之后可能又有线程进入条件等待，就没机会再次唤醒了
  notEmpty_.notify_all();
  exitCond_.wait(lock, [&]() -> bool
                 { return threads_.empty(); });
}

bool ThreadPool::checkRunningState() const { return isRunning_; }

// 不能inline，否则PoolMode访问不到
void ThreadPool::setMode(PoolMode mode)
{
  if (checkRunningState())
  {
    return;
  }
  poolMode_ = mode;
}

void ThreadPool::setThreadMaxThreshold(size_t threshold)
{
  if (checkRunningState())
  {
    return;
  }
  if (poolMode_ == PoolMode::MODE_CACHED)
  {
    threadMaxThreshold_ = threshold;
  }
}

void ThreadPool::setTaskMaxThreshold(size_t threshold)
{
  if (checkRunningState())
  {
    return;
  }
  taskMaxThreshold_ = threshold;
}

void ThreadPool::startPool(size_t initSize)
{
  isRunning_ = true;
  initSize_ = initSize;
  threadCount_ = initSize;

  // 创建线程并绑定线程函数
  for (size_t i = 0; i < initSize_; ++i)
  {
    auto threadPtr =
        // std::bind(&ThreadPool::threadFunc, this)绑定器生成一个临时的函数对象
        std::make_unique<Thread>(
            std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
    size_t id = threadPtr->getThreadId();
    threads_.emplace(id, std::move(threadPtr));
  }

  // 启动线程
  for (size_t i = 0; i < initSize_; ++i)
  {
    threads_[i]->startThread();
    freeThread_++;
  }
}

// 用户给线程池提交任务
Result ThreadPool::submitTask(const std::shared_ptr<Task> &sp)
{
  std::unique_lock<std::mutex> lock(taskQueueMtx_);
  // 判断是否到达任务队列上限
  // wait_for三个参数的重载版本
  if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]() -> bool
                         { return taskCount_ < TASK_MAX_THRESHOLD; }))
  {
    std::cerr << "task queue is full, submit failed" << std::endl;
    // 不能task->getResult，因为线程执行完task，task对象就析构了
    // 因此Result要拿到对应的task，延长task的生命周期
    return Result(sp, false);
  }

  tasksQueue_.emplace(sp);
  taskCount_++;
  // 唤醒等待任务的线程
  notEmpty_.notify_all();

  if (poolMode_ == PoolMode::MODE_CACHED && taskCount_ > freeThread_ &&
      threadCount_ < threadMaxThreshold_)
  {
    // 创建新的线程并启动
    std::cout << ">>>create thread" << std::endl;
    auto ptr = std::make_unique<Thread>(
        std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
    size_t id = ptr->getThreadId();
    threads_.emplace(id, std::move(ptr));
    threads_[id]->startThread();
    freeThread_++;
    threadCount_++;
  }
  return Result(sp);
}

// 线程池提供线程函数处理任务
void ThreadPool::threadFunc(size_t id)
{
  auto lastTime = std::chrono::high_resolution_clock::now();
  // 等线程执行完任务，才能退出线程池
  for (;;)
  {
    std::shared_ptr<Task> task;
    {
      std::unique_lock<std::mutex> lock(taskQueueMtx_);
      std::cout << "tid: " << std::this_thread::get_id() << " want to get task"
                << std::endl;

      while (tasksQueue_.empty())
      {
        // 没有任务可执行，1.线程池已关闭 || 2.线程开多了（可以回收一部分）
        // 第一种情况：线程池已关闭
        if (!isRunning_)
        {
          // 线程池关闭，当前线程需要退出
          threads_.erase(id);
          std::cout << std::this_thread::get_id() << " final exit!"
                    << std::endl;
          // 通知线程池当前线程已经执行完毕
          exitCond_.notify_all();
          return;
        }

        // 第二种情况：线程开多了，检查线程是否可以回收
        if (poolMode_ == PoolMode::MODE_CACHED)
        {
          // wait_for两个参数的重载版本，超时返回说明队列无任务，可能需要回收线程
          if (std::cv_status::timeout ==
              notEmpty_.wait_for(lock, std::chrono::seconds(1)))
          {
            auto nowTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                nowTime - lastTime);
            // 进一步根据线程的空闲时间判断是否回收
            if (duration.count() >= THREAD_MAX_FREE_TIME &&
                freeThread_ > initSize_)
            {
              threads_.erase(id);
              freeThread_--;
              threadCount_--;
              std::cout << std::this_thread::get_id() << "middle exit!"
                        << std::endl;
              return; // 线程函数返回，线程就结束了
            }
          }
        }
        else
        {
          // 释放锁，并阻塞等待唤醒，被唤醒后抢锁
          notEmpty_.wait(lock);
        }
      }

      // 取出任务
      freeThread_--;
      std::cout << "tid: " << std::this_thread::get_id() << " get task success"
                << std::endl;
      task = tasksQueue_.front();
      tasksQueue_.pop();
      taskCount_--;
      // 唤醒提交任务的线程
      notFull_.notify_all();
    } // 操作完任务队列就应该释放锁

    // 执行任务，并设置任务的返回值
    if (task)
    {
      task->exec();
    }
    freeThread_++;
    lastTime = std::chrono::high_resolution_clock::now();
  }
}

///////////////////////线程方法
size_t Thread::generateId = 0;

// 线程会执行线程函数，由线程池指定
Thread::Thread(ThreadFunc func)
    : func_(std::move(func)), threadId_(generateId++) {}
Thread::~Thread() = default;

void Thread::startThread()
{
  // 开真实线程执行线程函数
  std::thread t(func_, threadId_);
  t.detach();
}

size_t Thread::getThreadId() const { return threadId_; }

/////////////////////Semaphore
Semaphore::Semaphore(size_t limit) : resLimit_(limit), isExit_(false) {}
Semaphore::~Semaphore() { isExit_ = true; }

void Semaphore::wait()
{
  if (isExit_)
    return;
  std::unique_lock<std::mutex> lock(mtx_);
  cond_.wait(lock, [&]() -> bool
             { return resLimit_ > 0; });
  resLimit_--;
}

void Semaphore::post()
{
  // Result对象已经析构了，不会再有get或者setValue了
  if (isExit_)
    return;
  std::unique_lock<std::mutex> lock(mtx_);
  resLimit_++;
  // Result析构，sem也会析构，condition_variable也会析构
  cond_.notify_all();
}

// 任务和结果的异步调用设计
/////////////////////Result
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(std::move(task)), isValid_(isValid)
{
  // 将任务和结果对应起来
  task_->setResult(this);
}
Result::~Result() = default;

Any Result::get()
{
  if (!isValid_)
  {
    return "";
  }
  // 如果task还没有执行完，就会阻塞用户
  sem_.wait();

  return std::move(any_);
}

void Result::setValue(Any any)
{
  any_ = std::move(any);
  sem_.post();
}

/////////////////////Task
Task::Task() : result_(nullptr) {}
Task::~Task() = default;

void Task::setResult(Result *res) { result_ = res; }

void Task::exec()
{
  if (result_)
  {
    // 当线程执行有了结果，task异步设置Result
    result_->setValue(run());
  }
}