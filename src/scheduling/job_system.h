#pragma once

#include <functional>
#include <queue>
#include <chrono>
#include <string>

namespace globe {

// JobSystem - Frame-budgeted task execution (GE-style)
// Manages per-frame time budgets for expensive operations like:
// - Texture uploads
// - Mesh rebuilds
// - DEM processing
class JobSystem {
public:
    // Job priority levels
    enum class Priority {
        High = 0,    // Visible tiles, critical updates
        Normal = 1,  // Standard operations
        Low = 2      // Background prefetch, cleanup
    };
    
    // Job definition
    struct Job {
        std::function<void()> execute;
        Priority priority = Priority::Normal;
        std::string tag;  // For debugging/metrics
    };
    
    JobSystem() = default;
    
    // Queue a job for execution
    void Submit(Job job);
    void Submit(std::function<void()> func, Priority priority = Priority::Normal, const std::string& tag = "");
    
    // Execute jobs within time budget (milliseconds)
    // Returns number of jobs executed
    int ProcessBudget(double budgetMs);
    
    // Execute up to N jobs (count-based budget)
    // Returns number of jobs executed
    int ProcessCount(int maxJobs);
    
    // Clear all pending jobs
    void Clear();
    
    // Stats
    int GetPendingCount() const;
    int GetPendingCount(Priority priority) const;
    double GetLastProcessTimeMs() const { return lastProcessTimeMs_; }
    int GetLastProcessedCount() const { return lastProcessedCount_; }

private:
    // Priority queues (High, Normal, Low)
    std::queue<Job> queues_[3];
    
    // Metrics
    double lastProcessTimeMs_ = 0.0;
    int lastProcessedCount_ = 0;
};

} // namespace globe
