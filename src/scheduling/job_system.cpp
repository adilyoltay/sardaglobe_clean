#include "job_system.h"

namespace globe {

void JobSystem::Submit(Job job) {
    int idx = static_cast<int>(job.priority);
    if (idx >= 0 && idx < 3) {
        queues_[idx].push(std::move(job));
    }
}

void JobSystem::Submit(std::function<void()> func, Priority priority, const std::string& tag) {
    Job job;
    job.execute = std::move(func);
    job.priority = priority;
    job.tag = tag;
    Submit(std::move(job));
}

int JobSystem::ProcessBudget(double budgetMs) {
    auto startTime = std::chrono::high_resolution_clock::now();
    int processed = 0;
    
    // Process in priority order: High -> Normal -> Low
    for (int p = 0; p < 3; ++p) {
        while (!queues_[p].empty()) {
            // Check budget
            auto now = std::chrono::high_resolution_clock::now();
            double elapsedMs = std::chrono::duration<double, std::milli>(now - startTime).count();
            if (elapsedMs >= budgetMs) {
                lastProcessTimeMs_ = elapsedMs;
                lastProcessedCount_ = processed;
                return processed;
            }
            
            // Execute job
            Job job = std::move(queues_[p].front());
            queues_[p].pop();
            
            if (job.execute) {
                job.execute();
            }
            ++processed;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    lastProcessTimeMs_ = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    lastProcessedCount_ = processed;
    return processed;
}

int JobSystem::ProcessCount(int maxJobs) {
    auto startTime = std::chrono::high_resolution_clock::now();
    int processed = 0;
    
    // Process in priority order: High -> Normal -> Low
    for (int p = 0; p < 3 && processed < maxJobs; ++p) {
        while (!queues_[p].empty() && processed < maxJobs) {
            Job job = std::move(queues_[p].front());
            queues_[p].pop();
            
            if (job.execute) {
                job.execute();
            }
            ++processed;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    lastProcessTimeMs_ = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    lastProcessedCount_ = processed;
    return processed;
}

void JobSystem::Clear() {
    for (int p = 0; p < 3; ++p) {
        while (!queues_[p].empty()) {
            queues_[p].pop();
        }
    }
}

int JobSystem::GetPendingCount() const {
    int total = 0;
    for (int p = 0; p < 3; ++p) {
        total += static_cast<int>(queues_[p].size());
    }
    return total;
}

int JobSystem::GetPendingCount(Priority priority) const {
    int idx = static_cast<int>(priority);
    if (idx >= 0 && idx < 3) {
        return static_cast<int>(queues_[idx].size());
    }
    return 0;
}

} // namespace globe
