#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <mutex>
#include <vector>

// Use main() as entry point even with /SUBSYSTEM:WINDOWS
#ifdef _MSC_VER
#pragma comment(linker, "/entry:mainCRTStartup")
#endif

#include "Application.h"
#include "core/Settings.h"
#include "core/LogBuffer.h"

// Global queue for files dropped from OS
std::vector<std::string> g_droppedFiles;
std::mutex g_dropMutex;

static void glfw_drop_callback(GLFWwindow*, int count, const char** paths) {
    std::lock_guard<std::mutex> lock(g_dropMutex);
    for (int i = 0; i < count; i++) {
        g_droppedFiles.push_back(paths[i]);
    }
}

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static constexpr float BASE_FONT_SIZE = 18.0f;

/// Pending font scale — applied at the start of the next frame.
/// Font atlas rebuild MUST happen BEFORE ImGui::NewFrame(),
/// because NewFrame() caches font pointers from the atlas.
static float g_pendingFontScale = -1.0f;

/// Build the ImGui-side font atlas (add fonts, Build).
/// Does NOT touch the GPU texture.
/// MUST be called BEFORE ImGui::NewFrame() (no frame active).
static void buildFontAtlas(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    float fontSize = BASE_FONT_SIZE * scale;

    ImFontConfig fontCfg;
    fontCfg.SizePixels = fontSize;

    ImFont* font = nullptr;
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\consola.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    for (const char* path : fontPaths) {
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); font = io.Fonts->AddFontFromFileTTF(path, fontSize, &fontCfg); if (font) break; }
    }
    if (!font) {
        io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
}

int main(int, char**) {
    // --- Setup GLFW ---
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    // Create window
    GLFWwindow* window = glfwCreateWindow(1280, 900, "LogCater - Android Log Viewer", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, glfw_drop_callback);

    // --- Setup Dear ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    // --- Load Settings ---
    Settings settings;
    settings.load(Settings::defaultPath());

    // Build font atlas BEFORE backend init (no destroy/recreate needed)
    buildFontAtlas(settings.uiScale);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Apply style spacing to match font scale
    ImGui::GetStyle().ScaleAllSizes(settings.uiScale);

    // --- Create Application ---
    Application app;
    app.init(settings);

    // --- Main loop ---
    ImVec4 clear_color = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Apply deferred font atlas rebuild BEFORE NewFrame ---
        // Must be before NewFrame because NewFrame caches font pointers.
        if (g_pendingFontScale > 0.0f) {
            buildFontAtlas(g_pendingFontScale);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
            g_pendingFontScale = -1.0f;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Ctrl+MouseWheel / Ctrl+= / Ctrl+- zoom
        bool zoomIn = (io.KeyCtrl && io.MouseWheel > 0.0f) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal));
        bool zoomOut = (io.KeyCtrl && io.MouseWheel < 0.0f) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus));
        if (zoomIn || zoomOut) {
            float prevScale = settings.uiScale;
            settings.uiScale += zoomIn ? 0.1f : -0.1f;
            if (settings.uiScale < 0.5f) settings.uiScale = 0.5f;
            if (settings.uiScale > 2.5f) settings.uiScale = 2.5f;
            if (settings.uiScale != prevScale) {
                // Style sizes — apply immediately (no stale pointers)
                ImGui::GetStyle().ScaleAllSizes(1.0f / prevScale);
                ImGui::GetStyle().ScaleAllSizes(settings.uiScale);

                // Font atlas — defer to next frame (before NewFrame)
                g_pendingFontScale = settings.uiScale;
                settings.save(Settings::defaultPath());
            }
        }

        // Ctrl+0: reset zoom to 1.0x
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
            float prevScale = settings.uiScale;
            if (prevScale != 1.0f) {
                ImGui::GetStyle().ScaleAllSizes(1.0f / prevScale);
                settings.uiScale = 1.0f;
                g_pendingFontScale = 1.0f;
                settings.save(Settings::defaultPath());
            }
        }

        app.render();
        app.postFrame();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                     clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    app.shutdown();
    settings.save(Settings::defaultPath());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
