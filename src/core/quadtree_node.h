#pragma once

#include "extent.h"
#include "ellipsoid.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>

namespace globe {

// Quadrant indices (OpenGlobus convention)
enum Quadrant : uint8_t {
    NW = 0,  // North-West (top-left)
    NE = 1,  // North-East (top-right)
    SW = 2,  // South-West (bottom-left)
    SE = 3   // South-East (bottom-right)
};

// Neighbor directions
enum Direction : uint8_t {
    NORTH = 0,
    EAST = 1,
    SOUTH = 2,
    WEST = 3
};

// Node state
enum class NodeState : uint8_t {
    Idle,
    Loading,
    Ready,
    Error
};

/**
 * QuadTree Node (OpenGlobus Node port)
 * Represents a node in the planet quadtree
 */
class QuadTreeNode {
public:
    QuadTreeNode(int x, int y, int zoom, const Extent& extent, QuadTreeNode* parent = nullptr);
    ~QuadTreeNode();
    
    // Tile identification
    int X() const { return x_; }
    int Y() const { return y_; }
    int Zoom() const { return zoom_; }
    uint64_t NodeId() const { return nodeId_; }
    
    // Extent
    const Extent& GetExtent() const { return extent_; }
    LonLat GetCenter() const { return extent_.GetCenter(); }
    
    // Bounding sphere (for frustum/horizon culling)
    const glm::dvec3& GetBoundingSphereCenter() const { return bsCenter_; }
    double GetBoundingSphereRadius() const { return bsRadius_; }
    
    // Parent/children
    QuadTreeNode* Parent() { return parent_; }
    const QuadTreeNode* Parent() const { return parent_; }
    bool HasChildren() const { return children_[0] != nullptr; }
    QuadTreeNode* Child(Quadrant q) { return children_[q].get(); }
    const QuadTreeNode* Child(Quadrant q) const { return children_[q].get(); }
    
    // Create child nodes (subdivision)
    void CreateChildren(const Ellipsoid& ellipsoid);
    void DestroyChildren();
    
    // Neighbors (for edge stitching)
    std::vector<QuadTreeNode*>& Neighbors(Direction dir) { return neighbors_[dir]; }
    const std::vector<QuadTreeNode*>& Neighbors(Direction dir) const { return neighbors_[dir]; }
    
    // State
    NodeState GetState() const { return state_; }
    void SetState(NodeState state) { state_ = state; }
    bool IsReady() const { return state_ == NodeState::Ready; }
    
    // Visibility
    bool InFrustum() const { return inFrustum_; }
    void SetInFrustum(bool v) { inFrustum_ = v; }
    bool CameraInside() const { return cameraInside_; }
    void SetCameraInside(bool v) { cameraInside_ = v; }
    
    // Edge equalization (seam fix)
    uint8_t EdgeCoarserMask() const { return edgeCoarserMask_; }
    void SetEdgeCoarserMask(uint8_t mask) { edgeCoarserMask_ = mask; }
    
    // Compute bounding sphere from ellipsoid
    void ComputeBoundingSphere(const Ellipsoid& ellipsoid);
    
    // Get neighbor node at same or higher level
    static QuadTreeNode* FindNeighbor(QuadTreeNode* node, Direction dir);

private:
    // Tile coordinates
    int x_;
    int y_;
    int zoom_;
    uint64_t nodeId_;
    
    // Geographic extent
    Extent extent_;
    
    // Bounding sphere (world space)
    glm::dvec3 bsCenter_;
    double bsRadius_;
    
    // Tree structure
    QuadTreeNode* parent_;
    std::array<std::unique_ptr<QuadTreeNode>, 4> children_;
    std::array<std::vector<QuadTreeNode*>, 4> neighbors_;  // N, E, S, W
    
    // State
    NodeState state_ = NodeState::Idle;
    bool inFrustum_ = false;
    bool cameraInside_ = false;
    uint8_t edgeCoarserMask_ = 0;
    
    // Helper: compute node ID from tile coordinates
    static uint64_t ComputeNodeId(int x, int y, int z);
};

} // namespace globe
