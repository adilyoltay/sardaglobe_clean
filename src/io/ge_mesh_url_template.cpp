// GE Mesh URL Template Implementation

#include "ge_mesh_url_template.h"
#include <vector>

namespace globe {

namespace {
    // Known placeholders for GE mesh URLs
    const std::vector<std::string> kKnownPlaceholders = {
        "{quadkey}", "{z}", "{x}", "{y}", "{epoch}", "{path}"
    };
    
    // Check if a substring is a known placeholder
    bool IsKnownPlaceholder(const std::string& tmpl, size_t pos) {
        for (const auto& ph : kKnownPlaceholders) {
            if (tmpl.substr(pos, ph.length()) == ph) {
                return true;
            }
        }
        return false;
    }
    
    // Find unknown placeholders (pattern: {word} where word is not known)
    // Returns empty string if valid, error message if invalid
    std::string FindPlaceholderError(const std::string& tmpl) {
        size_t start = 0;
        while ((start = tmpl.find('{', start)) != std::string::npos) {
            size_t end = tmpl.find('}', start);
            if (end == std::string::npos) {
                // Unclosed brace - specific error
                return "Unclosed '{' brace in template";
            }
            if (!IsKnownPlaceholder(tmpl, start)) {
                return "Unknown placeholder: " + tmpl.substr(start, end - start + 1);
            }
            start = end + 1;
        }
        return "";
    }
    
    // Replace all occurrences of placeholder with value
    void ReplaceAll(std::string& str, const std::string& placeholder, const std::string& value) {
        size_t pos = 0;
        while ((pos = str.find(placeholder, pos)) != std::string::npos) {
            str.replace(pos, placeholder.length(), value);
            pos += value.length(); // Continue after replacement to avoid infinite loop
        }
    }
}

bool ValidateGeMeshEndpointTemplate(const std::string& tmpl, std::string& outErr) {
    // Check for required {quadkey} placeholder
    if (tmpl.find("{quadkey}") == std::string::npos) {
        outErr = "Template missing required {quadkey} placeholder";
        return false;
    }
    
    // Check for unknown placeholders or syntax errors (typo detection)
    std::string err = FindPlaceholderError(tmpl);
    if (!err.empty()) {
        outErr = err;
        return false;
    }
    
    return true;
}

std::string BuildGeMeshUrl(const std::string& tmpl, const std::string& nodeKey,
                           int z, int x, int y) {
    std::string result = tmpl;
    
    // Always replace {quadkey} (required)
    ReplaceAll(result, "{quadkey}", nodeKey);
    
    // Optionally replace {z}, {x}, {y} only if z >= 0
    if (z >= 0) {
        ReplaceAll(result, "{z}", std::to_string(z));
        ReplaceAll(result, "{x}", std::to_string(x));
        ReplaceAll(result, "{y}", std::to_string(y));
    }
    
    return result;
}

} // namespace globe
