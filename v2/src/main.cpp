#include <iostream>
#include <vector>
#include <sstream>
#include "ThreadPool.h"

int square(int i) {
    std::stringstream ss;
    ss << "index i = " << i << " starts\n";
    std::cout << ss.str();//和v1同样的手法，防止多线程环境下cout的<<链式调用出现交错现象

    //假装这是一项耗时的工作，sleep期间依然占据cpu资源
    std::this_thread::sleep_for(std::chrono::seconds(1));

    ss.str("");
    ss.clear();//重置以复用ss
    ss << "index i = " << i << " completed\n";
    std::cout << ss.str();

    return i * i;
}

int main() {
    ThreadPool pool(4);

    std::vector<std::future<int>> results;

    for (int i = 0; i < 20; ++i) {
        results.emplace_back(pool.enqueue(square, i));
    }

    for (auto& result : results) {
        std::stringstream ss;
        ss << result.get() << "\n";
        std::cout << ss.str();
    }

    return 0;
}