#include "SearchPaths.h"

#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <strings.h>  // strcasecmp, strncasecmp

std::vector<std::string> CSearchPaths::ms_paths;
static std::string sEmptyString;

static std::string NormalizePath(const std::string &path)
{
    std::string result = path;
    
    // Convert backslashes to forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');
    
    // Trim leading whitespace
    auto start = std::find_if_not(result.begin(), result.end(), ::isspace);
    
    // Trim trailing whitespace and slashes
    auto end = result.end();
    while (end > start && (std::isspace(*(end-1)) || *(end-1) == '/')) {
        --end;
    }
    
    return std::string(start, end);
}

void CSearchPaths::Add(const std::string &path)
{
    if (path.empty()) return;
    
    std::string normalized = NormalizePath(path);
    if (normalized.empty()) return;
    
    // Check duplicates
    for (const auto &p : ms_paths) {
        if (p == normalized) return;
    }
    
    printf("SearchPaths: [%zu] %s\n", ms_paths.size(), normalized.c_str());
    ms_paths.push_back(normalized);
}

void CSearchPaths::Clear()
{
    ms_paths.clear();
}

const std::string& CSearchPaths::Get(size_t index)
{
    if (index >= ms_paths.size()) return sEmptyString;
    return ms_paths[index];
}

// Case-insensitive search for single component in directory
static bool FindInDir(const std::string &dir, const std::string &name, std::string &outFound)
{
    DIR *d = opendir(dir.c_str());
    if (!d) return false;
    
    struct dirent *entry;
    while ((entry = readdir(d))) {
        if (strcasecmp(entry->d_name, name.c_str()) == 0) {
            outFound = entry->d_name;
            closedir(d);
            return true;
        }
    }
    
    closedir(d);
    return false;
}

// Resolve path case-insensitively from base directory
static std::string ResolvePath(const std::string &baseDir, const std::string &relativePath)
{
    std::string current = baseDir;
    std::string normalized = NormalizePath(relativePath);
    
    size_t start = 0;
    while (start < normalized.size()) {
        size_t end = normalized.find('/', start);
        if (end == std::string::npos) end = normalized.size();
        
        std::string component = normalized.substr(start, end - start);
        start = end + 1;
        
        if (component.empty()) continue;
        
        std::string found;
        if (!FindInDir(current, component, found)) {
            return "";
        }
        
        current += "/" + found;
    }
    
    // Verify exists
    struct stat st;
    if (stat(current.c_str(), &st) != 0) {
        return "";
    }
    
    return current;
}

std::string CSearchPaths::FindFile(const std::string &relativePath)
{
    if (relativePath.empty()) return "";
    
    // Absolute path - check directly
    if (relativePath[0] == '/') {
        struct stat st;
        if (stat(relativePath.c_str(), &st) == 0) {
            return relativePath;
        }
        return "";
    }
    
    // Try each search path
    for (const auto &basePath : ms_paths) {
        std::string resolved = ResolvePath(basePath, relativePath);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    return "";
}

std::string CSearchPaths::MakeWritePath(const std::string &relativePath)
{
    if (ms_paths.empty()) return NormalizePath(relativePath);
    
    return ms_paths.back() + "/" + NormalizePath(relativePath);
}