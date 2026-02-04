#include "quadtree_node.h"
#include <algorithm>

namespace globe {

QuadTreeNode::QuadTreeNode(int x, int y, int zoom, const Extent& extent, QuadTreeNode* parent)
    : x_(x)
    , y_(y)
    , zoom_(zoom)
    , nodeId_(ComputeNodeId(x, y, zoom))
    , extent_(extent)
    , bsCenter_(0.0)
    , bsRadius_(0.0)
    , parent_(parent)
{
    // unique_ptr already default-initialized to nullptr
}

QuadTreeNode::~QuadTreeNode() {
    DestroyChildren();
}

uint64_t QuadTreeNode::ComputeNodeId(int x, int y, int z) {
    // Morton code / Z-order curve for unique ID
    uint64_t id = 0;
    for (int i = 0; i < z; ++i) {
        id |= ((uint64_t)((x >> i) & 1) << (2 * i));
        id |= ((uint64_t)((y >> i) & 1) << (2 * i + 1));
    }
    // Prepend zoom level
    return ((uint64_t)z << 56) | id;
}

void QuadTreeNode::ComputeBoundingSphere(const Ellipsoid& ellipsoid) {
    // Get corner points of the extent
    LonLat sw = extent_.southWest;
    LonLat ne = extent_.northEast;
    LonLat center = extent_.GetCenter();
    
    // Convert to cartesian
    glm::dvec3 centerCart = ellipsoid.GeodeticToCartesian(center.lon, center.lat, 0.0);
    glm::dvec3 swCart = ellipsoid.GeodeticToCartesian(sw.lon, sw.lat, 0.0);
    glm::dvec3 neCart = ellipsoid.GeodeticToCartesian(ne.lon, ne.lat, 0.0);
    glm::dvec3 nwCart = ellipsoid.GeodeticToCartesian(sw.lon, ne.lat, 0.0);
    glm::dvec3 seCart = ellipsoid.GeodeticToCartesian(ne.lon, sw.lat, 0.0);
    
    // Bounding sphere center
    bsCenter_ = centerCart;
    
    // Radius is max distance from center to any corner
    double r1 = glm::length(swCart - centerCart);
    double r2 = glm::length(neCart - centerCart);
    double r3 = glm::length(nwCart - centerCart);
    double r4 = glm::length(seCart - centerCart);
    
    bsRadius_ = std::max({r1, r2, r3, r4});
}

void QuadTreeNode::CreateChildren(const Ellipsoid& ellipsoid) {
    if (HasChildren()) return;
    
    int childZoom = zoom_ + 1;
    int childX = x_ * 2;
    int childY = y_ * 2;
    
    Extent nw, ne, sw, se;
    extent_.GetChildren(nw, ne, sw, se);
    
    children_[NW] = std::make_unique<QuadTreeNode>(childX, childY, childZoom, nw, this);
    children_[NE] = std::make_unique<QuadTreeNode>(childX + 1, childY, childZoom, ne, this);
    children_[SW] = std::make_unique<QuadTreeNode>(childX, childY + 1, childZoom, sw, this);
    children_[SE] = std::make_unique<QuadTreeNode>(childX + 1, childY + 1, childZoom, se, this);
    
    // Compute bounding spheres for children
    for (auto& child : children_) {
        if (child) {
            child->ComputeBoundingSphere(ellipsoid);
        }
    }
}

void QuadTreeNode::DestroyChildren() {
    for (auto& child : children_) {
        child.reset();
    }
}

QuadTreeNode* QuadTreeNode::FindNeighbor(QuadTreeNode* node, Direction dir) {
    if (!node || !node->parent_) {
        return nullptr;
    }
    
    QuadTreeNode* parent = node->parent_;
    
    // Determine which quadrant this node is in
    Quadrant quadrant = NW;
    for (int q = 0; q < 4; ++q) {
        if (parent->children_[q].get() == node) {
            quadrant = static_cast<Quadrant>(q);
            break;
        }
    }
    
    // Find sibling or parent's neighbor
    switch (dir) {
        case NORTH:
            if (quadrant == SW) return parent->Child(NW);
            if (quadrant == SE) return parent->Child(NE);
            break;
        case SOUTH:
            if (quadrant == NW) return parent->Child(SW);
            if (quadrant == NE) return parent->Child(SE);
            break;
        case WEST:
            if (quadrant == NE) return parent->Child(NW);
            if (quadrant == SE) return parent->Child(SW);
            break;
        case EAST:
            if (quadrant == NW) return parent->Child(NE);
            if (quadrant == SW) return parent->Child(SE);
            break;
    }
    
    // Need to go up to parent's neighbor
    QuadTreeNode* parentNeighbor = FindNeighbor(parent, dir);
    if (!parentNeighbor || !parentNeighbor->HasChildren()) {
        return parentNeighbor;
    }
    
    // Get corresponding child of parent's neighbor
    switch (dir) {
        case NORTH:
            if (quadrant == NW) return parentNeighbor->Child(SW);
            if (quadrant == NE) return parentNeighbor->Child(SE);
            break;
        case SOUTH:
            if (quadrant == SW) return parentNeighbor->Child(NW);
            if (quadrant == SE) return parentNeighbor->Child(NE);
            break;
        case WEST:
            if (quadrant == NW) return parentNeighbor->Child(NE);
            if (quadrant == SW) return parentNeighbor->Child(SE);
            break;
        case EAST:
            if (quadrant == NE) return parentNeighbor->Child(NW);
            if (quadrant == SE) return parentNeighbor->Child(SW);
            break;
    }
    
    return nullptr;
}

} // namespace globe
