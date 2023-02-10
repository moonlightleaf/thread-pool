#include <string>
#include <sstream>
#include <iostream>

#include "ThreadPool.h"

int main() {
    ThreadPool thread_pool(4); //线程池容量
    for (int i = 0; i < 20; ++i) {
        thread_pool.enqueue([i]{
            //先将要输出的内容都写入同一个stringstream对象中
            std::stringstream ss;
            ss << i << " : Current worker id is " << std::this_thread::get_id() << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << ss.str();//使用1个<<运算符一次性打印全部输出，防止多线程写入输出流时出现交错现象
        });
    }
    return 0;
}