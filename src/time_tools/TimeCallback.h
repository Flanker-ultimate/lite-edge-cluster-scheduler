#include <iostream>
#include <thread>
#include <chrono>
#include <functional>
#include <atomic>
#include <string>

class TimerCallback {
public:
    // Constructor to initialize the timer with interval and callback
    // TimerCallback(): interval(5), callback([]() -> void {}), stop_flag(true),
    //       elapsed_time(0),timer_thread(),once_flag(once_flag) {
    //
    // };

    // Constructor to initialize the timer with interval and callback
    TimerCallback(){
        interval = 30;
        once_flag = true;
    }

    // Constructor to initialize the timer with interval and callback
    TimerCallback(int interval_seconds, std::function<void()> callback, bool once_flag = false)
        : interval(interval_seconds), callback(callback), stop_flag(true),
          elapsed_time(0), once_flag(once_flag) {
    }

    void set_callback(std::function<void()> callback1) {
        this->callback = callback1;
    }

    void set_once_flag(bool once_flag) {
        this->once_flag = once_flag;
    }

    void set_interval(int interval) {
        this->interval = interval;
    }

    // Start the timer by creating a new thread
    void start() {
        if (timer_thread.joinable()) {
            stop_flag = true;
            timer_thread.join(); // ensure previous thread teminate
        }
        stop_flag = false; // Ensure stop_flag is false before starting the timer
        elapsed_time = 0; // Reset the elapsed time
        timer_thread = std::thread(&TimerCallback::run, this); // Start the timer thread
    }


    // Reset the elapsed time
    void refresh() {
        elapsed_time = 0;
    }

    // Get the elapsed time
    int getElapsedTime() const {
        return elapsed_time;
    }

    // Get the internal time
    int getIntervalTime() const {
        return interval;
    }

private:
    int interval; // Interval in seconds to trigger the callback
    std::function<void()> callback; // The callback function to be triggered
    std::atomic<bool> stop_flag; // Flag to stop the timer
    std::thread timer_thread; // Timer thread that runs the timer
    int elapsed_time; // Time elapsed since the last callback
    bool once_flag; // Flag to trigger the callback only once

    // Timer thread function
    void run() {
        while (!stop_flag) {
            // Continue running as long as stop_flag is false
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Wait for 1 second
            elapsed_time++; // Increment elapsed time

            if (elapsed_time >= interval) {
                // If the interval has passed
                callback(); // Trigger the callback
                if (once_flag) {
                    stop_flag = true;
                    // If it's a one-time time thread over
                    break; // Exit the loop
                }
                elapsed_time = 0; // Reset elapsed time for repeated callbacks
            }
        }
    }


};