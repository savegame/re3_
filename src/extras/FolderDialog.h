#pragma once

#include <string>
#include <vector>

class FolderDialog
{
public:
    // Returns true if user selected a folder, false if cancelled
    static bool Show(const std::string &startPath, std::string &outSelectedPath, float dpiScale = 1.0f);

private:
    struct Entry {
        std::string name;
        bool isDir;
    };

    static std::string ms_currentPath;
    static std::vector<Entry> ms_entries;
    static int ms_selectedIndex;
    static bool ms_isOpen;
    static std::string ms_resultPath;
    static bool ms_confirmed;

    static void RefreshEntries();
    static bool IsRootPath(const std::string &path);
    static std::string GetParentPath(const std::string &path);
    static std::string GetStartPath(const std::string &requestedPath);
};