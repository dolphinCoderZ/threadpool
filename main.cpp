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

    // û��Result���գ�35����������ִ�����������result������sem������post��������
    pool.submitTask(std::make_shared<MyTask>(10, 20));
    pool.submitTask(std::make_shared<MyTask>(10, 20));

    int sum1 = res1.get().cast_<int>();
    std::cout << sum1 << std::endl;

    // std::cout << res1.get().cast_<int>() << std::endl;
    // sem�Ѿ�Ϊ0������wait״̬��ͨ�����isDone����Լ��޸�Result.get()���ص�AnyΪ����Դת�ƽ����unique_ptr->shared_ptr��
    /*
    sem��wait��post��Ҫ�ж�
    if (!task_->isDone) {
      // ���task��û��ִ���꣬�ͻ������û�
      sem_.wait();
    }
    if (result_) {
      // ���߳�ִ�����˽����task�첽����Result
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