#ifndef TIMER_H
#define TIMER_H

#include <chrono>
#include <iostream>

// Macro to start the time
#define TIMER_START auto start_time = std::chrono::high_resolution_clock::now();

// Macro to end the timer, calculate the duration, and print the result
#define TIMER_END(text) \
    auto end_time = std::chrono::high_resolution_clock::now(); \
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(); \
    std::cout << text << " - Time elapsed: " << duration << " ms" << std::endl;

#endif // TIMER_Hs