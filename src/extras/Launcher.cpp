#include "common.h"
#include "Launcher.h"

#include <GLFW/glfw3.h>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>

#include "../../vendor/librw/src/gl/glad/glad.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include "FolderDialog.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

unsigned int Launcher::ms_backgroundTexture = 0;
int Launcher::ms_backgroundWidth = 0;
int Launcher::ms_backgroundHeight = 0;
bool Launcher::ms_backgroundLoaded = false;

std::string Launcher::ms_gamePath = Launcher::GetDefaultPath();
bool Launcher::ms_showDisclaimer = true;
bool Launcher::ms_disclaimerAccepted = false;

bool showFolderDialog = false;

// Хелпер для ссылок
static void TextLink(const char* label, const char* url)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 1.0f, 1.0f)); // синий
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();
    
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        // Подчёркивание
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(min.x, max.y), ImVec2(max.x, max.y),
            ImGui::GetColorU32(ImVec4(0.4f, 0.6f, 1.0f, 1.0f))
        );
    }
    
    if (ImGui::IsItemClicked()) {
        // Открыть URL на Aurora OS / Linux FIXME: use RuntimeManager API for link opening
        std::string cmd = "xdg-open \"" + std::string(url) + "\" &";
        system(cmd.c_str());
    }
}

float Launcher::CalculateDpiScale(GLFWwindow *window)
{
	// Try glfwGetWindowContentScale first
	float xscale = 1.0f, yscale = 1.0f;
	glfwGetWindowContentScale(window, &xscale, &yscale);

	// If valid result, use it
	if (xscale > 1.0f || yscale > 1.0f) {
		return (xscale > yscale) ? xscale : yscale;
	}

	// Fallback: calculate from physical size and resolution
	GLFWmonitor *monitor = glfwGetWindowMonitor(window);
	if (!monitor) {
		monitor = glfwGetPrimaryMonitor();
	}
	if (!monitor) {
		return 1.0f;
	}

	int widthMM, heightMM;
	glfwGetMonitorPhysicalSize(monitor, &widthMM, &heightMM);
	if (widthMM <= 0 || heightMM <= 0) {
		return 1.0f;
	}

	const GLFWvidmode *mode = glfwGetVideoMode(monitor);
	if (!mode || mode->width <= 0 || mode->height <= 0) {
		return 1.0f;
	}

	// Calculate DPI (use larger dimension for better accuracy)
	float dpiX = (float)mode->width / ((float)widthMM / 25.4f);
	float dpiY = (float)mode->height / ((float)heightMM / 25.4f);
	float dpi = (dpiX > dpiY) ? dpiX : dpiY;

	// Scale relative to 96 DPI (desktop standard)
	// For mobile you might want 160 DPI as base
	const float baseDpi = 96.0f;
	float scale = dpi / baseDpi;

	// Clamp to reasonable range
	if (scale < 1.0f) scale = 1.0f;
	if (scale > 3.25f) scale = 3.5f;

	fprintf(stderr, "Launcher: DPI=%.1f, scale=%.2f\n", dpi, scale);

	return scale;
}

bool Launcher::LoadBackgroundTexture(const char *path)
{
    if (ms_backgroundLoaded) return true;

    int width, height, channels;
    unsigned char *data = stbi_load(path, &width, &height, &channels, 4);
    if (!data) {
        fprintf(stderr, "Launcher: Failed to load background: %s\n", path);
        return false;
    }

    glGenTextures(1, &ms_backgroundTexture);
    glBindTexture(GL_TEXTURE_2D, ms_backgroundTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    ms_backgroundWidth = width;
    ms_backgroundHeight = height;
    ms_backgroundLoaded = true;

    fprintf(stderr, "Launcher: Background loaded %dx%d\n", width, height);
    return true;
}

void Launcher::DrawBackground(int width, int height)
{
    ImDrawList *draw = ImGui::GetBackgroundDrawList();

    if (!ms_backgroundLoaded) {
        // Fallback gradient
        ImU32 colTop = IM_COL32(20, 30, 48, 255);
        ImU32 colBottom = IM_COL32(40, 50, 80, 255);
        draw->AddRectFilledMultiColor(
            ImVec2(0, 0), 
            ImVec2((float)width, (float)height),
            colTop, colTop, colBottom, colBottom
        );
        return;
    }

    // Calculate "cover" mode - fill screen, crop overflow, center image
    float screenAspect = (float)width / (float)height;
    float imageAspect = (float)ms_backgroundWidth / (float)ms_backgroundHeight;

    float drawWidth, drawHeight;
    float offsetX = 0, offsetY = 0;

    if (screenAspect > imageAspect) {
        // Screen wider than image - fit width, crop height
        drawWidth = (float)width;
        drawHeight = drawWidth / imageAspect;
        offsetY = (height - drawHeight) * 0.5f;
    } else {
        // Screen taller than image - fit height, crop width
        drawHeight = (float)height;
        drawWidth = drawHeight * imageAspect;
        offsetX = (width - drawWidth) * 0.5f;
    }

    ImVec2 p0(offsetX, offsetY);
    ImVec2 p1(offsetX + drawWidth, offsetY + drawHeight);

    draw->AddImage(
        (ImTextureID)(intptr_t)ms_backgroundTexture,
        p0, p1,
        ImVec2(0, 0), ImVec2(1, 1),
        IM_COL32(255, 255, 255, 255)
    );
}

void Launcher::DrawDisclaimer(float dpiScale)
{
	if (!ms_showDisclaimer) return;

	ImGuiIO& io = ImGui::GetIO();

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar 
	                       | ImGuiWindowFlags_NoResize 
	                       | ImGuiWindowFlags_NoMove 
	                       | ImGuiWindowFlags_NoCollapse
	                       | ImGuiWindowFlags_NoBringToFrontOnFocus;

	ImGui::Begin("DisclaimerWindow", nullptr, flags);

	// Center content vertically
	float startY = 20 * dpiScale;
	if (startY < 20) startY = 20;
	ImGui::SetCursorPosY(startY);

	// Title
	ImGui::SetWindowFontScale(1.5f);
	float titleWidth = ImGui::CalcTextSize("Дисклеймер").x;
	ImGui::SetCursorPosX((io.DisplaySize.x - titleWidth) * 0.5f);
	ImGui::Text("Дисклеймер");
	ImGui::SetWindowFontScale(1.0f);

	ImGui::Spacing();
	ImGui::Spacing();

	// Text with padding
	float padding = 15 * dpiScale;
	ImGui::SetCursorPosX(padding);
	ImGui::PushTextWrapPos(io.DisplaySize.x - padding);
	ImGui::TextWrapped(
		"RE3 - это неофициальный проект обратной разработки GTA III.\n\n"
		"Для работы требуются оригинальные файлы игры GTA III. "
		"Этот проект не содержит и не распространяет игровые ресурсы.\n\n"
		"Используйте только легально приобретённые копии игры.\n\n"
		"Разработчики не несут ответственности за использование данного ПО."
	);

	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::SetCursorPosX(padding);
	ImGui::TextWrapped("Поддержать портирование игр на ОС Аврора:");
	ImGui::SetCursorPosX(padding);
	TextLink("Подписывайтесь на Boosty", "https://boosty.to/sashikknox");
	
	ImGui::Spacing();
	ImGui::Spacing();

	ImGui::SetCursorPosX(padding);
	ImGui::TextWrapped("Подписывайтесь на канал в телеграм:");
	ImGui::SetCursorPosX(padding);
	TextLink("@auroraosgames", "https://t.me/auroraosgames");
	ImGui::PopTextWrapPos();

	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::Spacing();

	// Centered button
	float buttonWidth = 200 * dpiScale;
	float buttonHeight = 50 * dpiScale;
	ImGui::SetCursorPosX((io.DisplaySize.x - buttonWidth) * 0.5f);

	if (ImGui::Button("Понятно", ImVec2(buttonWidth, buttonHeight))) {
		ms_showDisclaimer = false;
		ms_disclaimerAccepted = true;
	}

	ImGui::End();
}

// Required game files - if any missing, game cannot start
const std::vector<std::string>& 
Launcher::GetRequiredFiles()
{
	static const std::vector<std::string> files = {
		"models/gta3.img",
		"models/gta3.dir", 
		"data/gta_vc.dat",
		"data/default.dat",
		"anim/ped.ifp"
	};
	return files;
}

std::string
Launcher::GetDefaultPath()
{
	const char *home = getenv("HOME");
	if(home) {
		return std::string(home) + "/Documents/GTA3"; 
	}
	return "./";
}

std::string
Launcher::GetConfigPath()
{
	const char *home = getenv("HOME");
	if (home) {
		return std::string(home) + "/.config/ru.sashikknox/miami/launcher.conf";
	}
	return "./launcher.conf";
}

std::string
Launcher::LoadStoredPath()
{
	std::ifstream file(GetConfigPath());
	if (file.is_open()) {
		std::string path;
		std::getline(file, path);
		if (!path.empty())
			return path;
	}
	return GetDefaultPath();
}

void
Launcher::SaveStoredPath(const std::string &path)
{
	std::string configPath = GetConfigPath();
	size_t lastSlash = configPath.rfind('/');
	if (lastSlash != std::string::npos) {
		std::string dir = configPath.substr(0, lastSlash);
		mkdir(dir.c_str(), 0755);
	}

	std::ofstream file(configPath);
	if (file.is_open())
		file << path << std::endl; 
}

bool
Launcher::CheckResources(const std::string &basePath)
{
	const auto &files = GetRequiredFiles();

	for (const auto &file : files) {
		std::string fullPath = basePath + "/" + file;
		struct stat st;
		if (stat(fullPath.c_str(), &st) != 0) {
			fprintf(stderr, "Launcher: Missing file: %s\n", fullPath.c_str());
			return false;
		}
	}

	fprintf(stderr, "Launcher: All required files found at %s\n", basePath.c_str());
	return true;
}

// Helper: get list of missing files
static std::vector<std::string> 
GetMissingFiles(const std::string &basePath, const std::vector<std::string> &files)
{
    std::vector<std::string> missing;
    for (const auto &file : files) {
        std::string fullPath = basePath + "/" + file;
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) {
            missing.push_back(file);
        }
    }
    return missing;
}

Launcher::Result
Launcher::Run()
{
	// Initialize GLFW early - RE3 will reuse this initialization
	if(!glfwInit()) {
		fprintf(stderr, "Launcher: Failed to initialize GLFW\n");
		return Result::Exit;
	}

	glfwSetErrorCallback([](int error, const char *description) { fprintf(stderr, "Launcher: GLFW Error %d: %s\n", error, description); });

	// Try stored path first
	// UI state
	std::string inputPath = LoadStoredPath();
	inputPath.reserve(4096); // Reserve space for InputText
	std::vector<std::string> missingFiles;

	bool resourcesFound = false;

	missingFiles = GetMissingFiles(inputPath, GetRequiredFiles());
	resourcesFound = missingFiles.empty();

	if (resourcesFound) {
		ms_gamePath = inputPath;
	} else {
		inputPath = GetDefaultPath();
		// Try default path
		if (CheckResources(inputPath)) {
			SaveStoredPath(inputPath);
			ms_gamePath = inputPath;
			resourcesFound = true;
		}
	}

	// No resources found - show UI
	fprintf(stderr, "Launcher: Resources not found, showing UI...\n");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

	GLFWwindow *window = glfwCreateWindow(720, 480, "RE3 Launcher", nullptr, nullptr);

	// Fallback to OpenGL if GLES failed
	if (!window) {
		fprintf(stderr, "Launcher: GLES2 failed, trying OpenGL...\n");
		glfwDefaultWindowHints();
		window = glfwCreateWindow(720, 480, "RE3 Launcher", nullptr, nullptr);
	}

	if (!window) {
		fprintf(stderr, "Launcher: Failed to create window\n");
		return Result::Exit;
	}

	// Maximize window
	glfwMaximizeWindow(window);

	glfwMakeContextCurrent(window);

	if (!gladLoadGLES2Loader((GLADloadproc)glfwGetProcAddress, 20)) {
		fprintf(stderr, "Launcher: Failed to load GLES2\n");
		glfwDestroyWindow(window);
		return Result::Exit;
	}

	glfwSwapInterval(1);

	// Get DPI scale
	// float xscale = 1.0f, yscale = 1.0f;
	// glfwGetWindowContentScale(window, &xscale, &yscale);
	// float dpiScale = (xscale > yscale) ? xscale : yscale;
	// if (dpiScale < 1.0f) dpiScale = 1.0f;

	// xscale = yscale = dpiScale = 2.5f;
	float dpiScale = CalculateDpiScale(window);

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// Style
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(dpiScale);

	// Load font with Cyrillic glyphs
	float fontSize = 18.0f * dpiScale;
	const char* fontPath = "/usr/share/fonts/als-hauss-variable/ALSHaussVariable-Medium.ttf"; // TODO: change to your font
	ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, fontSize, nullptr, io.Fonts->GetGlyphRangesCyrillic());
	if (!font) {
		fprintf(stderr, "Launcher: Failed to load font %s, using default\n", fontPath);
		io.Fonts->AddFontDefault();
	}

	// Initialize ImGui backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 100"); // GLES2

	Result result = Result::Exit;

	LoadBackgroundTexture("/usr/share/ru.sashikknox.miami/poster.png");

	// Main loop
	while (!glfwWindowShouldClose(window)) {
		// Check iconified state
		int iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
		
		if (iconified) {
			// Don't render when minimized — just wait for events
			glfwWaitEvents();
			continue;
		}
		
		glfwPollEvents();

		// TODO: ImGui frame here
		// For now: placeholder - check resources on any key press

		// Clear screen with dark color
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// TODO: ImGui render here
		// =================== BEGIN IMGUI
		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		int winWidth, winHeight;
		glfwGetWindowSize(window, &winWidth, &winHeight);

		DrawBackground(winWidth, winHeight);

		if (ms_showDisclaimer) {
			// Show disclaimer on first run
			DrawDisclaimer(dpiScale);
		} else {
			// Main UI window (fullscreen)
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2((float)winWidth, (float)winHeight));
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar 
											| ImGuiWindowFlags_NoResize 
											| ImGuiWindowFlags_NoMove 
											| ImGuiWindowFlags_NoCollapse;

			ImGui::Begin("Launcher", nullptr, windowFlags);

			// Title
			ImGui::SetCursorPosY(20.0f * dpiScale);
			ImGui::SetWindowFontScale(1.5f);
			ImGui::Text("RE3 Launcher");
			ImGui::SetWindowFontScale(1.0f);
			
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			// Instructions
			if (!resourcesFound)
				ImGui::TextWrapped("Файлы игры GTA III не найдены. Укажите путь к папке с игрой:");
			else
				ImGui::TextWrapped("Файлы игры GTA III найдены. Но вы можете изменить путь до папки:");

			ImGui::Spacing();

			// Path input
			float browseWidth = 100 * dpiScale;
			ImGui::Text("Путь к игре:");
			ImGui::SetNextItemWidth(-1);
			// InputTextString("##path", &inputPath);

			ImGui::SameLine();
			if (ImGui::Button("Обзор", ImVec2(browseWidth, 0))) {
				showFolderDialog = true;
			}

			// Folder dialog
			if (showFolderDialog) {
				std::string selected;
				if (FolderDialog::Show(GetDefaultPath(), selected, dpiScale)) {
					if (!selected.empty()) {
						inputPath = selected;
						// Check resources immediately after selection
						missingFiles = GetMissingFiles(inputPath, GetRequiredFiles());
						resourcesFound = missingFiles.empty();
						if (resourcesFound) {
							SaveStoredPath(inputPath);
						}
					}
					showFolderDialog = false;
				}
			}
			ImGui::Spacing();

			// Check button
			if (ImGui::Button("Проверить", ImVec2(150 * dpiScale, 40 * dpiScale))) {
				missingFiles = GetMissingFiles(inputPath, GetRequiredFiles());
				resourcesFound = missingFiles.empty();
				if (resourcesFound) {
					SaveStoredPath(inputPath);
				}
			}

			ImGui::Spacing();

			// Status
			if (resourcesFound) {
				ImGui::TextWrapped("Проверка: %s", inputPath.c_str());
				ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[/] Все файлы найдены!");
				
				ImGui::Spacing();
				
				if (ImGui::Button("Запустить игру", ImVec2(200 * dpiScale, 50 * dpiScale))) {
					ms_gamePath = inputPath;
					result = Result::Continue;
					glfwSetWindowShouldClose(window, GLFW_TRUE);
				}
			} else if (!missingFiles.empty()) {
				ImGui::TextWrapped("Проверка: %s", inputPath.c_str());
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[X] Отсутствующие файлы:");
				for (const auto& file : missingFiles) {
					ImGui::BulletText("%s", file.c_str());
				}
			}

			// Footer with exit button
			ImGui::SetCursorPosY((float)winHeight - 60.0f * dpiScale);
			ImGui::Separator();
			ImGui::Spacing();

			float availWidth = ImGui::GetContentRegionAvail().x;
			const float spacing = ImGui::GetStyle().ItemSpacing.x;
			float buttonWidth = std::min(200 * dpiScale, (availWidth - spacing) * 0.5f);
			
			if (ImGui::Button("Выход", ImVec2(buttonWidth, 35 * dpiScale))) {
				result = Result::Exit;
				glfwSetWindowShouldClose(window, GLFW_TRUE);
			}

			ImGui::SameLine();
			if (ImGui::Button("Дисклеймер", ImVec2(buttonWidth, 35 * dpiScale))) {
				ms_showDisclaimer = true;
			}
			
			ImGui::End();
		}

		// Render
		ImGui::Render();
		glViewport(0, 0, winWidth, winHeight);
		glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		// ====================END IMGUI 

		glfwSwapBuffers(window);

		// Temporary: recheck resources each frame (for testing)
		// User copies files while window is open, then they are detected
		// path = LoadStoredPath();
		// if(CheckResources(path)) {
		// 	ms_gamePath = path;
		// 	result = Result::Continue;
		// 	break;
		// }
	}

	// Cleanup ImGui
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	// Destroy launcher window, but keep GLFW initialized for RE3
	glfwDestroyWindow(window);

	// Reset window hints to defaults for RE3
	glfwDefaultWindowHints();

	return result;
}