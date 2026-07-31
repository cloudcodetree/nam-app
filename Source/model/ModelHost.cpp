#include "model/ModelHost.h"
#include <exception>

namespace nam {

ModelHost::ModelHost() {
    // Start the worker only after all other members (mtx_, cv_, jobs_,
    // stop_) are fully constructed, since worker() touches them immediately.
    th_ = std::thread([this]{ worker(); });
}

ModelHost::~ModelHost() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void ModelHost::configure(int sampleRate, int maxBlock) {
    sampleRate_ = sampleRate; maxBlock_ = maxBlock;
}

void ModelHost::loadNow(const std::string& path, Callback onDone) {
    std::shared_ptr<NamModel> model;
    try {
        model = std::shared_ptr<NamModel>(NamModel::load(path, sampleRate_, maxBlock_));
    } catch (const std::exception&) {
        model = nullptr;
    } catch (...) {
        model = nullptr;
    }
    try {
        onDone(model);
    } catch (...) {
        // Swallow: a throwing caller callback must not propagate.
    }
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
            // Pending jobs are cancelled on destruction: once stop_ is set,
            // drop the queue immediately rather than draining it, so
            // callbacks never fire during owner teardown.
            if (stop_) return;
            job = std::move(jobs_.front()); jobs_.pop();
        }
        std::shared_ptr<NamModel> model;
        try {
            model = std::shared_ptr<NamModel>(NamModel::load(job.path, sampleRate_, maxBlock_));
        } catch (const std::exception&) {
            model = nullptr;
        } catch (...) {
            model = nullptr;
        }
        try {
            job.cb(model);
        } catch (...) {
            // Swallow: a throwing user callback must not terminate the worker thread.
        }
    }
}

} // namespace nam
