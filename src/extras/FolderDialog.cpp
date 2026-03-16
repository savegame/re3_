#include "common.h"
#include "FolderDialog.h"

#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

#include "imgui.h"

std::string FolderDialog::ms_currentPath;
std::vector<FolderDialog::Entry> FolderDialog::ms_entries;
int FolderDialog::ms_selectedIndex = -1;
bool FolderDialog::ms_isOpen = false;
std::string FolderDialog::ms_resultPath;
bool FolderDialog::ms_confirmed = false;

bool FolderDialog::IsRootPath(const std::string &path)
{
    return path == "/" || path.empty();
}

std::string FolderDialog::GetParentPath(const std::string &path)
{
    if (IsRootPath(path)) return "/";
    
    size_t pos = path.rfind('/');
    if (pos == 0) return "/";
    if (pos == std::string::npos) return "/";
    return path.substr(0, pos);
}

void FolderDialog::RefreshEntries()
{
    ms_entries.clear();
    ms_selectedIndex = -1;

    DIR *dir = opendir(ms_currentPath.c_str());
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (strcmp(ent->d_name, "..") == 0) continue;

        std::string fullPath = ms_currentPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += ent->d_name;

        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) continue;

        Entry entry;
        entry.name = ent->d_name;
        entry.isDir = S_ISDIR(st.st_mode);
        
        // Only show directories
        if (entry.isDir) {
            ms_entries.push_back(entry);
        }
    }
    closedir(dir);

    // Sort alphabetically
    std::sort(ms_entries.begin(), ms_entries.end(), 
        [](const Entry &a, const Entry &b) {
            return a.name < b.name;
        });
}

std::string FolderDialog::GetStartPath(const std::string &requestedPath)
{
    // Try requested path first
    if (!requestedPath.empty()) {
        struct stat st;
        if (stat(requestedPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return requestedPath;
        }
    }

    // Fallback to HOME
    const char *home = getenv("HOME");
    if (home) {
        return std::string(home);
    }

    return "/";
}

bool FolderDialog::Show(const std::string &startPath, std::string &outSelectedPath, float dpiScale)
{
    ImGuiIO& io = ImGui::GetIO();

    // Initialize on first call
    if (!ms_isOpen) {
        ms_isOpen = true;
        ms_confirmed = false;
        ms_resultPath.clear();
        ms_currentPath = GetStartPath(startPath);
        RefreshEntries();
    }

    // Fullscreen window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar 
                           | ImGuiWindowFlags_NoResize 
                           | ImGuiWindowFlags_NoMove 
                           | ImGuiWindowFlags_NoCollapse;

    ImGui::Begin("FolderDialogWindow", nullptr, flags);

    // Title
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("Выбор папки");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::Separator();

    // Current path
    ImGui::Text("Путь:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", ms_currentPath.c_str());

    ImGui::Spacing();

    // Navigation buttons
    float buttonHeight = 40 * dpiScale;
    
    if (!IsRootPath(ms_currentPath)) {
        if (ImGui::Button("Наверх", ImVec2(150 * dpiScale, buttonHeight))) {
            ms_currentPath = GetParentPath(ms_currentPath);
            RefreshEntries();
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Обновить", ImVec2(150 * dpiScale, buttonHeight))) {
        RefreshEntries();
    }

    ImGui::Separator();

    // Folder list
    float bottomHeight = 80 * dpiScale;
    ImVec2 listSize = ImVec2(-1, ImGui::GetContentRegionAvail().y - bottomHeight);

    float itemHeight = 25 * dpiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 12 * dpiScale));
    
    if (ImGui::BeginChild("FolderList", listSize, true)) {
        if (!IsRootPath(ms_currentPath)) {
            if (ImGui::Selectable("[ ] ..", false, 0, ImVec2(0, itemHeight))) {
                ms_currentPath = GetParentPath(ms_currentPath);
                RefreshEntries();
            }
        }
        for (int i = 0; i < (int)ms_entries.size(); i++) {
            const Entry &entry = ms_entries[i];
            
            std::string label = "[ ] " + entry.name;
            
            if (ImGui::Selectable(label.c_str(), ms_selectedIndex == i, 0, ImVec2(0, itemHeight))) {
                ms_selectedIndex = i;
                
                std::string newPath = ms_currentPath;
                if (newPath.back() != '/') newPath += '/';
                newPath += entry.name;
                ms_currentPath = newPath;
                RefreshEntries();
            }
        }
        
        if (ms_entries.empty()) {
            ImGui::TextDisabled("(пусто)");
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();

    ImGui::Separator();

    // Bottom buttons
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    bool closeDialog = false;
    float bigButtonHeight = 50 * dpiScale;
    float buttonWidth = std::min(200 * dpiScale, (availWidth - spacing) * 0.5f);

    if (ImGui::Button("Выбрать эту папку", ImVec2(buttonWidth, bigButtonHeight))) {
        outSelectedPath = ms_currentPath;
        closeDialog = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("Отмена", ImVec2(buttonWidth, bigButtonHeight))) {
        outSelectedPath.clear();
        closeDialog = true;
    }

    ImGui::End();

    if (closeDialog) {
        ms_isOpen = false;
        return true;
    }

    return false;
}