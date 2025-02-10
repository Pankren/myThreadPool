#include "threadPool.hpp"

#include <iostream>
#include <functional>
#include <thread>
#include <chrono>
#include <future>
#include <string>

using namespace std;

int sum(int a, int b) {
    this_thread::sleep_for(chrono::seconds(2));
    return a + b;
}

void print() {
    this_thread::sleep_for(chrono::seconds(2));
    cout << "hello world!" << endl;
}

void printStr(const string& str) {
    this_thread::sleep_for(chrono::seconds(2));
    cout << str << endl;
}
int main() {
    ThreadPool pool;
    pool.setMode(PoolMode::CACHED_MODE);
    pool.start(1);

    future<int> r1 = pool.submitTask(sum, 1, 2);
    future<void> r2 = pool.submitTask(print);
    future<int> r3 = pool.submitTask([](int begin, int end)-> int {
        int sum = 0;
        for(int i = begin; i <= end; ++i) {
            sum += i;
        }
        this_thread::sleep_for(chrono::seconds(2));
        return sum;
    }, 1, 1000);
    future<void> r4 = pool.submitTask(printStr, "this is a string");

    cout << r1.get() << endl;
    r2.get();
    cout<< "print() complete!" << endl;
    cout << r3.get() << endl;
    r4.get();
    cout << "printStr() complete!" << endl;

    return 0;
}