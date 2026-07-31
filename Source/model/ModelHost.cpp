#include "model/ModelHost.h"

namespace nam {

ModelHost::ModelHost() : th_([this]{ worker(); }) {}

ModelHost::~ModelHost() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void ModelHost::configure(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate; maxBlock_ = maxBlock;
}

void ModelHost::loadNow(const std::string& path, Callback onDone) {
    onDone(std::shared_ptr<NamModel>(NamModel::load(path, sampleRate_, maxBlock_)));
}

void ModelHost::requestLoad(const std::string& path, Callback onDone) {
    { std::lock_guard<std::mutex> lk(mtx_); jobs_.push({path, std::move(onDone)}); }
    cv_.notify_one();
}

void ModelHost::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]{ return stop_ || !jobs_.empty(); });
            if (stop_ && jobs_.empty()) return;
            job = std::move(jobs_.front()); jobs_.pop();
        }
        job.cb(std::shared_ptr<NamModel>(NamModel::load(job.path, sampleRate_, maxBlock_)));
    }
}

} // namespace nam
