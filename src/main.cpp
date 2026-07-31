#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <mutex>
#include <vector>
#include "resource.h"

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

    // Build glyph ranges: Latin + Chinese Simplified common (~2500 chars)
    ImFontGlyphRangesBuilder rangesBuilder;
    rangesBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    rangesBuilder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    ImVector<ImWchar> ranges;
    rangesBuilder.BuildRanges(&ranges);

    ImFontConfig fontCfg;
    fontCfg.SizePixels = fontSize;
    fontCfg.GlyphRanges = ranges.Data;

    // Try CJK-capable fonts first, fall back to basic fonts
    ImFont* font = nullptr;
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",       // Microsoft YaHei (CJK + Latin)
        "C:\\Windows\\Fonts\\msyhbd.ttc",     // Microsoft YaHei Bold
        "C:\\Windows\\Fonts\\simsun.ttc",     // SimSun
        "C:\\Windows\\Fonts\\segoeui.ttf",    // Segoe UI
        "C:\\Windows\\Fonts\\consola.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    for (const char* path : fontPaths) {
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); font = io.Fonts->AddFontFromFileTTF(path, fontSize, &fontCfg, ranges.Data); if (font) break; }
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

    // Set window icon from embedded resource (GLFW defaults to IDI_APPLICATION)
    {
        HWND hwnd = glfwGetWin32Window(window);
        HINSTANCE hInst = GetModuleHandle(nullptr);
        // ICON_BIG = taskbar / alt-tab, ICON_SMALL = title bar
        HICON hIconBig = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1),
            IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        HICON hIconSmall = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        if (hIconBig)  SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
        if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    }

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

    // Apply style tweaks (before ScaleAllSizes so they scale with zoom)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FramePadding     = ImVec2(7.0f, 5.0f);    // buttons, tabs
        style.ItemSpacing      = ImVec2(10.0f, 6.0f);   // widget spacing
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);    // inner spacing
        style.CellPadding      = ImVec2(6.0f, 4.0f);    // table cells
        style.WindowPadding    = ImVec2(10.0f, 10.0f);  // window edges
        style.TabRounding      = 4.0f;
        style.FrameRounding    = 3.0f;
    }

    // Apply style spacing to match font scale
    ImGui::GetStyle().ScaleAllSizes(settings.uiScale);

    // Snapshot the base style (custom values at 1.0x).
    // Used to cleanly restore style on every zoom change,
    // avoiding float drift from repeated ScaleAllSizes(1/prev) + ScaleAllSizes(new).
    static ImGuiStyle g_baseStyle = ImGui::GetStyle();

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
                // Restore base style + re-scale (avoids float drift)
                ImGui::GetStyle() = g_baseStyle;
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
                settings.uiScale = 1.0f;
                ImGui::GetStyle() = g_baseStyle;
                ImGui::GetStyle().ScaleAllSizes(1.0f);
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
