// RockMesh Manager Implementation

#include "rockmesh_manager.h"
#include "../io/providers/rocktree_node_data_parser.h"
#include "../core/ellipsoid.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <chrono>
#include <iostream>

namespace globe {

RockMeshManager::RockMeshManager(const Config& config)
    : config_(config),
      requestQueue_(32),
      uploadQueue_(8) {
}

RockMeshManager::~RockMeshManager() {
    Shutdown();
}

bool RockMeshManager::Init() {
    if (!CreateFallbackTexture()) {
        std::cerr << "[RockMesh] Failed to create fallback texture\n";
        return false;
    }
    
    workerThread_ = std::thread(&RockMeshManager::WorkerLoop, this);
    return true;
}

void RockMeshManager::Shutdown() {
    shutdown_ = true;
    requestQueue_.Close();
    uploadQueue_.Close();
    
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    
    // Destroy all GPU meshes
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& [id, entry] : entries_) {
        entry.gpu.Destroy();
    }
    entries_.clear();
    
    // Destroy fallback texture
    if (fallbackTexture_) {
        glDeleteTextures(1, &fallbackTexture_);
        fallbackTexture_ = 0;
    }
}

void RockMeshManager::Request(const std::string& nodeKey) {
    bool shouldQueue = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        
        auto it = entries_.find(nodeKey);
        if (it != entries_.end()) {
            // Already known
            return;
        }
        
        // New entry
        RockMeshEntry entry;
        entry.state = RockMeshState::Queued;
        entries_[nodeKey] = std::move(entry);
        shouldQueue = true;
    }
    
    // Queue for worker (outside lock to avoid blocking)
    if (shouldQueue) {
        if (!requestQueue_.Push(nodeKey)) {
            // Queue full or closed - mark as failed
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "Request queue full or closed";
            }
            std::cerr << "[RockMesh] Failed to queue request for " << nodeKey << "\n";
        }
    }
}

bool RockMeshManager::ProcessUploads(double budgetMs) {
    auto start = std::chrono::steady_clock::now();
    bool didWork = false;
    
    RockMeshCpu cpu;
    while (uploadQueue_.TryPop(cpu)) {
        if (!cpu.valid) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = cpu.error.empty() ? "Build failed" : cpu.error;
            }
            didWork = true;
            continue;
        }
        
        // Create GPU mesh
        RockMeshGpu gpu;
        if (gpu.Create(cpu, fallbackTexture_)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.gpu = std::move(gpu);
                it->second.state = RockMeshState::Uploaded;
            }
        } else {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(cpu.id);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "GPU upload failed";
            }
        }
        
        didWork = true;
        
        // Check budget
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - start).count();
        if (elapsed >= budgetMs) {
            break;
        }
    }
    
    return didWork;
}

void RockMeshManager::Render() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Uploaded && entry.gpu.valid) {
            entry.gpu.Draw();
        }
    }
}

bool RockMeshManager::HasPendingWork() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Queued ||
            entry.state == RockMeshState::Fetching ||
            entry.state == RockMeshState::ReadyToUpload) {
            return true;
        }
    }
    return false;
}

size_t RockMeshManager::GetUploadedCount() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    size_t count = 0;
    for (const auto& [id, entry] : entries_) {
        if (entry.state == RockMeshState::Uploaded) {
            ++count;
        }
    }
    return count;
}

void RockMeshManager::WorkerLoop() {
    // Create client for this thread
    GoogleEarthNodeDataClient client(config_);
    
    if (!client.IsEnabled()) {
        std::cerr << "[RockMesh] Worker: client not enabled\n";
        return;
    }
    
    std::string nodeKey;
    while (requestQueue_.Pop(nodeKey)) {
        if (shutdown_) break;
        
        // Update state
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Fetching;
            }
        }
        
        // Fetch
        NodeDataResult fetchResult = client.FetchNodeData(nodeKey);
        if (!fetchResult.success) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = fetchResult.errorMessage;
            }
            continue;
        }
        
        // Parse
        ParsedNodeData parsed = RockTreeNodeDataParser::Parse(fetchResult.data);
        if (!parsed.success) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = parsed.error;
            }
            continue;
        }
        
        // Build CPU mesh
        RockMeshCpu cpu = BuildMesh(nodeKey, parsed);
        
        if (!cpu.valid) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = cpu.error.empty() ? "BuildMesh failed" : cpu.error;
            }
            continue;
        }
        
        // Queue for upload
        if (!uploadQueue_.Push(std::move(cpu))) {
            // Upload queue full or closed
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto it = entries_.find(nodeKey);
            if (it != entries_.end()) {
                it->second.state = RockMeshState::Failed;
                it->second.error = "Upload queue full or closed";
            }
            std::cerr << "[RockMesh] Failed to queue upload for " << nodeKey << "\n";
        }
    }
}

RockMeshCpu RockMeshManager::BuildMesh(const std::string& nodeKey, const ParsedNodeData& parsed) {
    RockMeshCpu cpu;
    cpu.id = nodeKey;
    
    // Validate
    if (!parsed.hasTransform) {
        cpu.error = "No transform matrix";
        return cpu;
    }
    if (parsed.vertexCount <= 0) {
        cpu.error = "No vertices";
        return cpu;
    }
    if (parsed.indices.empty() || parsed.indices.size() % 3 != 0) {
        cpu.error = "Invalid indices";
        return cpu;
    }
    
    const int V = parsed.vertexCount;
    const int T = parsed.triangleCount;
    
    // Build transform matrix (column-major to glm)
    glm::dmat4 M;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            M[col][row] = parsed.transform[col * 4 + row];
        }
    }
    
    // Extract translation and compute scale
    glm::dvec3 t(M[3][0], M[3][1], M[3][2]);
    double tLen = glm::length(t);
    if (tLen < 1e-10) {
        cpu.error = "Invalid transform: translation vector too small";
        return cpu;
    }
    double kmPerRockUnit = Ellipsoid::WGS84_A_KM / tLen;
    
    // Resize output
    cpu.vertices.resize(V * 9);  // stride 9
    cpu.indices = parsed.indices;
    cpu.triangleCount = T;
    
    // Build positions
    for (int i = 0; i < V; ++i) {
        // Local position (int16 / 32768)
        double lx = parsed.positions[i * 3 + 0] / 32768.0;
        double ly = parsed.positions[i * 3 + 1] / 32768.0;
        double lz = parsed.positions[i * 3 + 2] / 32768.0;
        
        // Transform to world (km)
        glm::dvec4 local(lx, ly, lz, 1.0);
        glm::dvec3 world = glm::dvec3(M * local) * kmPerRockUnit;
        
        cpu.vertices[i * 9 + 0] = static_cast<float>(world.x);
        cpu.vertices[i * 9 + 1] = static_cast<float>(world.y);
        cpu.vertices[i * 9 + 2] = static_cast<float>(world.z);
    }
    
    // Build UVs
    if (parsed.hasUvQuant && !parsed.uv.empty()) {
        for (int i = 0; i < V; ++i) {
            uint16_t u16 = parsed.uv[i * 2 + 0];
            uint16_t v16 = parsed.uv[i * 2 + 1];
            float u, v;
            parsed.uvQuant.Decode(u16, v16, u, v, config_.geMeshFlipV);
            cpu.vertices[i * 9 + 6] = u;
            cpu.vertices[i * 9 + 7] = v;
        }
    } else {
        // Default UVs
        for (int i = 0; i < V; ++i) {
            cpu.vertices[i * 9 + 6] = 0.0f;
            cpu.vertices[i * 9 + 7] = 0.0f;
        }
    }
    
    // Build normals (from triangle list)
    std::vector<glm::vec3> normals(V, glm::vec3(0.0f));
    std::vector<int> normalCounts(V, 0);
    
    for (int t = 0; t < T; ++t) {
        uint32_t i0 = parsed.indices[t * 3 + 0];
        uint32_t i1 = parsed.indices[t * 3 + 1];
        uint32_t i2 = parsed.indices[t * 3 + 2];
        
        if (i0 >= V || i1 >= V || i2 >= V) continue;  // Safety
        
        glm::vec3 p0(cpu.vertices[i0 * 9 + 0], cpu.vertices[i0 * 9 + 1], cpu.vertices[i0 * 9 + 2]);
        glm::vec3 p1(cpu.vertices[i1 * 9 + 0], cpu.vertices[i1 * 9 + 1], cpu.vertices[i1 * 9 + 2]);
        glm::vec3 p2(cpu.vertices[i2 * 9 + 0], cpu.vertices[i2 * 9 + 1], cpu.vertices[i2 * 9 + 2]);
        
        glm::vec3 e0 = p1 - p0;
        glm::vec3 e1 = p2 - p0;
        glm::vec3 cross = glm::cross(e0, e1);
        float crossLen2 = glm::dot(cross, cross);
        
        // Skip degenerate triangles
        if (crossLen2 < 1e-10f) continue;
        
        glm::vec3 n = glm::normalize(cross);
        
        // Outward enforce
        glm::vec3 centerPos = (p0 + p1 + p2) / 3.0f;
        if (glm::dot(n, centerPos) < 0.0f) {
            n = -n;
        }
        
        normals[i0] += n;
        normals[i1] += n;
        normals[i2] += n;
        normalCounts[i0]++;
        normalCounts[i1]++;
        normalCounts[i2]++;
    }
    
    for (int i = 0; i < V; ++i) {
        if (normalCounts[i] > 0) {
            glm::vec3 n = glm::normalize(normals[i] / static_cast<float>(normalCounts[i]));
            cpu.vertices[i * 9 + 3] = n.x;
            cpu.vertices[i * 9 + 4] = n.y;
            cpu.vertices[i * 9 + 5] = n.z;
        } else {
            cpu.vertices[i * 9 + 3] = 0.0f;
            cpu.vertices[i * 9 + 4] = 0.0f;
            cpu.vertices[i * 9 + 5] = 1.0f;
        }
    }
    
    // HeightKm (always 0 for rockmesh)
    for (int i = 0; i < V; ++i) {
        cpu.vertices[i * 9 + 8] = 0.0f;
    }
    
    // Decode texture
    if (parsed.texture.valid && !parsed.texture.jpegBytes.empty()) {
        stbi_set_flip_vertically_on_load_thread(0);  // We handle flip in UV
        
        int w, h, channels;
        uint8_t* pixels = stbi_load_from_memory(
            parsed.texture.jpegBytes.data(),
            static_cast<int>(parsed.texture.jpegBytes.size()),
            &w, &h, &channels, 4  // Force RGBA
        );
        
        if (pixels && w > 0 && h > 0) {
            // Size validation: max 8192x8192, prevent overflow
            constexpr int MAX_TEX_SIZE = 8192;
            if (w <= MAX_TEX_SIZE && h <= MAX_TEX_SIZE) {
                size_t numPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
                size_t bytesNeeded = numPixels * 4;
                // Check for overflow (simple: if w*h would overflow, bytesNeeded would be wrong)
                if (bytesNeeded / 4 == numPixels) {
                    cpu.rgba.resize(bytesNeeded);
                    memcpy(cpu.rgba.data(), pixels, bytesNeeded);
                    cpu.texWidth = w;
                    cpu.texHeight = h;
                    cpu.hasTexture = true;
                }
            }
            stbi_image_free(pixels);
        }
    }
    
    cpu.valid = true;
    return cpu;
}

bool RockMeshManager::CreateFallbackTexture() {
    glGenTextures(1, &fallbackTexture_);
    if (!fallbackTexture_) return false;
    
    glBindTexture(GL_TEXTURE_2D, fallbackTexture_);
    
    // 1x1 gray pixel
    uint8_t gray[4] = {128, 128, 128, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

} // namespace globe
