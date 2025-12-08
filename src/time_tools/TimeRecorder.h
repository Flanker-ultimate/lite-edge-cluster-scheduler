#pragma once
#include <chrono>
#include <spdlog/spdlog.h>
#include <typeinfo>
#include <string>
#include <utility>

using namespace std;

template<class TimeUnit>
class TimeRecord {
private:
    chrono::time_point<chrono::steady_clock, chrono::nanoseconds> startTime;
    long duration{};
    bool isStart = false;
    string recordName = "TimeRecord";
public:
    TimeRecord() {}

    // explicit TimeRecord(string name) : recordName(std::move(name)) {

    // };
    explicit TimeRecord(string name){
        this->recordName = name;
    }
    bool _isStart(){
        return isStart;
    }
    void startRecord() {
        isStart = true;
        startTime = chrono::steady_clock::now();
    }

    void endRecord() {
        if (isStart) {
            duration += chrono::duration_cast<TimeUnit>(chrono::steady_clock::now() - startTime).count();
            isStart = false;
        } else {
            duration = 0;
            //printf(" No start record! this reocrd %s is 0! \n",recordName);
            spdlog::error("{} did not start", recordName);
        }

    }

    void print() {
        if (typeid(TimeUnit) == typeid(chrono::milliseconds)) {
            spdlog::info("{} time is {} ms", recordName, duration);
        } else if (typeid(TimeUnit) == typeid(chrono::microseconds)) {
            spdlog::info("{} time is {} micro++s", recordName, duration);
        } else if (typeid(TimeUnit) == typeid(chrono::nanoseconds)) {
            spdlog::info("{} time is {} ns", recordName, duration);
        }
    }

    void clearRecord() {
        isStart = false;
        duration = 0;
    }
    long getDuration(){
        return duration;
    }

};