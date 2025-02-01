#ifndef JOB_H
#define JOB_H

#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <exception>

struct Job {
    int jobId;
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<int32_t> generatedTokens;
    std::string generatedText;
    bool isFinished = false;
    bool hasError = false;
    std::string errorMessage;
    float tps = 0;
};

#endif // JOB_H