//
// Created by ZHL on 2024/3/21.
//
#include "threadpool.h"
#include <iostream>

#if 1
class MyTask : public Task {
public:
  MyTask(int begin, int end) : begin_(begin), end_(end) {}
  Any run() override {
    int sum = 0;
    for (int i = begin_; i <= end_; ++i) {
      sum += i;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return sum;
  }

private:
  int begin_;
  int end_;
};

int main() {

  {
    ThreadPool pool;
    pool.setMode(PoolMode::MODE_CACHED);
    pool.startPool(2);

    Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10));
    Result res2 = pool.submitTask(std::make_shared<MyTask>(10, 20));

    // 没用Result接收，35行生命周期执行完就死亡，result析构、sem析构，post发生阻塞
    pool.submitTask(std::make_shared<MyTask>(10, 20));
    pool.submitTask(std::make_shared<MyTask>(10, 20));

    int sum1 = res1.get().cast_<int>();
    std::cout << sum1 << std::endl;

    // std::cout << res1.get().cast_<int>() << std::endl;
    // sem已经为0，进入wait状态，通过添加isDone解决以及修改Result.get()返回的Any为非资源转移解决（unique_ptr->shared_ptr）
    /*
    sem的wait和post都要判断
    if (!task_->isDone) {
      // 如果task还没有执行完，就会阻塞用户
      sem_.wait();
    }
    if (result_) {
      // 当线程执行有了结果，task异步设置Result
      result_->setValue(run());
      isDone = true;
    }
     */
    getchar();
  }

  std::cout << "main over" << std::endl;
  return 0;
}
#endif