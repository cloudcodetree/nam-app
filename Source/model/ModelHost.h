#pragma once
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "model/NamModel.h"

namespace nam {
class ModelHost {
public:
    using Callback = std::function<void(std::shared_ptr<NamModel>)>;
    ModelHost();
    ~ModelHost();
    void configure(int sampleRate, int maxBlock);
    void requestLoad(const std::string& path, Callback onDone);
    void loadNow(const std::string& path, Callback onDone);
private:
    struct Job { std::string path; Callback cb; };
    void worker();
    int sampleRate_ = 48000, maxBlock_ = 128;
    std::thread th_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    bool stop_ = false;
};
} // namespace nam
