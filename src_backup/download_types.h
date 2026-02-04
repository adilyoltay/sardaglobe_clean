#pragma once

#include <string>
#include <vector>
#include <functional>

// ============================================================================
// DOWNLOAD TYPES
// Shared types for tile download system.
// Extracted from globe_engine.cpp for better modularity.
// ============================================================================

// Priority levels for download queue (lower = higher priority)
enum DownloadPriority {
    PRIORITY_VISIBLE_LEAF = 0,   // Currently visible leaf tiles
    PRIORITY_VISIBLE_PARENT = 1, // Parent tiles for fallback
    PRIORITY_PREFETCH = 2,       // Prefetch nearby tiles
    PRIORITY_LOW = 3             // Background loading
};

// Support URL mode for fallback downloads
enum class SupportMode {
    NONE = 0,
    PARENT = 1,
    SIBLING = 2
};

// Download job for the worker queue
struct DownloadJob {
    std::string urlTemplate;
    std::string supportUrl;      // Alternative URL for retries
    std::string layerId;         // Layer ID for multi-layer support (empty = base layer)
    int z = 0;
    int x = 0;
    int y = 0;
    bool isVector = false;
    int priority = PRIORITY_LOW;
    float priorityScore = 0.0f;  // Google Earth style: Distance + Importance + Viewport Overlap
    int retryCount = 0;
    double queueTime = 0.0;      // Time when job was queued
    bool cancelled = false;      // Set to true to skip this job
    bool isSupportRequest = false;
    SupportMode supportMode = SupportMode::NONE;
    std::function<void(std::vector<unsigned char>, bool)> callback; // Scheduler callback
};

// Result from download worker
struct DownloadResult {
    int z = 0;
    int x = 0;
    int y = 0;
    std::string layerId;         // Layer ID for multi-layer support
    bool isVector = false;
    bool ok = false;
    bool isEmpty = false;        // True if image is empty/transparent
    bool usedSupportUrl = false; // True if support URL was used
    bool isSupport = false;
    SupportMode supportMode = SupportMode::NONE;
    std::vector<unsigned char> data;
    
    // Pre-decoded pixel data (worker thread decode optimization)
    std::vector<unsigned char> decodedPixels;
    int decodedWidth = 0;
    int decodedHeight = 0;
    bool decodeSuccess = false;
};

// Priority queue comparator for download jobs
// Google Earth style: Priority level first, then priorityScore (higher = more important)
struct DownloadJobComparator {
    bool operator()(const DownloadJob& a, const DownloadJob& b) const {
        // Lower priority value = higher priority (URGENT=0 > LOW=3)
        if (a.priority != b.priority) return a.priority > b.priority;
        
        // Same priority level: use priorityScore (higher score = higher priority)
        if (a.priorityScore != b.priorityScore) {
            return a.priorityScore < b.priorityScore;  // Higher score wins
        }
        
        // Tie-break: Prefer lower Z (parents/ancestors load first for fallback)
        if (a.z != b.z) return a.z > b.z;
        
        // Same zoom: FIFO order
        return a.queueTime > b.queueTime;
    }
};
