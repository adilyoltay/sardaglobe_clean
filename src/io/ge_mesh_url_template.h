// GE Mesh URL Template
// URL template placeholder replacement for RockTree/NodeData requests
// Sprint 1: Supports {quadkey}, {z}, {x}, {y} placeholders

#pragma once

#include <string>

namespace globe {

// Validates that the endpoint template contains required {quadkey} placeholder
// and no unknown placeholders.
// Returns true if valid, false with error message in outErr if invalid.
bool ValidateGeMeshEndpointTemplate(const std::string& tmpl, std::string& outErr);

// Builds a URL from template by replacing placeholders.
// Supported placeholders: {quadkey} (required), {z}, {x}, {y} (optional)
// 
// Parameters:
//   tmpl     - URL template with placeholders
//   nodeKey  - NodeData key string (direct string, NOT TileKey.ToQuadKey())
//   z, x, y  - Optional tile coordinates. If z < 0, {z}{x}{y} are left unreplaced.
//
// Returns: URL with all valid placeholders replaced
std::string BuildGeMeshUrl(const std::string& tmpl, const std::string& nodeKey,
                           int z = -1, int x = -1, int y = -1);

} // namespace globe
