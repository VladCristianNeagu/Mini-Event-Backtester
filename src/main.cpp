#include <iostream>
#include <string>
#include <chrono>
#include "Backtest.hpp"
#include "RollingMid.hpp"

int main() {
    std::string path = "data/AMZN_2012-06-21_34200000_57600000_message_10.csv";
    Book book;
    RollingMid strategy;
    Backtest backtest(path, book, strategy, 34200017459617LL, 57599959359650LL, 1000000LL);

    chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

    backtest.run();
    backtest.printResults();
    
    chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Execution time: " << duration.count() << " ms" << std::endl;
    return 0;
}
