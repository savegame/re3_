#pragma once

#include <string>
#include <vector>

class CSearchPaths
{
    static std::vector<std::string> ms_paths;

public:
    // Add path (first added = highest priority, used for writes)
    static void Add(const std::string &path);
    
    // Clear all paths
    static void Clear();
    
    // Get path by index
    static size_t GetCount() { return ms_paths.size(); }
    static const std::string& Get(size_t index);
    
    // Get first path (for writes)
    static const std::string& GetWritePath() { return Get(0); }
    
    // Find file across all search paths (case-insensitive)
    // Returns full resolved path or empty string if not found
    static std::string FindFile(const std::string &relativePath);

    // Build path for writing (always in first search path, no existence check)
    static std::string MakeWritePath(const std::string &relativePath);
};