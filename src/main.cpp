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
#include <shellapi.h>
#include <CommCtrl.h>
#include "resource.h"

// Use main() as entry point even with /SUBSYSTEM:WINDOWS
#ifdef _MSC_VER
#pragma comment(linker, "/entry:mainCRTStartup")
#endif

#include "Application.h"
#include "core/Settings.h"
#include "core/LogBuffer.h"
#include "resource.h"

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

// ─── System tray ─────────────────────────────────────────────────
static HWND g_hwnd = nullptr;
static WNDPROC g_origWndProc = nullptr;
static NOTIFYICONDATAW g_nid = {};
static bool g_exitRequested = false;
static constexpr UINT WM_TRAYICON = WM_APP + 1;

static void showTrayMenu() {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Show LogCater");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"Exit");
    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                              pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == 1) {
        ShowWindow(g_hwnd, SW_SHOW);
        SetForegroundWindow(g_hwnd);
    } else if (cmd == 2) {
        g_exitRequested = true;
    }
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (lp == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        } else if (lp == WM_RBUTTONUP) {
            showTrayMenu();
        }
        return 0;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

static void initTray(HWND hwnd) {
    g_hwnd = hwnd;
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TrayWndProc)));

    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wcscpy_s(g_nid.szTip, L"LogCater");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static constexpr float BASE_FONT_SIZE = 18.0f;

/// Pending font scale — applied at the start of the next frame.
/// Font atlas rebuild MUST happen BEFORE ImGui::NewFrame(),
/// because NewFrame() caches font pointers from the atlas.
static float g_pendingFontScale = -1.0f;
static int g_pendingTheme = -1;
static ImGuiStyle g_baseStyle;   // snapshot of the active theme at 1.0x

static void applyDarkTheme();     // defined below (custom dark palette)

/// Apply colors/padding for the given theme and refresh the base-style snapshot.
static void applyTheme(int theme) {
    ImGuiStyle& style = ImGui::GetStyle();
    if (theme == 1) {
        ImGui::StyleColorsLight(&style);
    } else {
        applyDarkTheme();
    }
    // Custom padding (shared across themes)
    style.FramePadding     = ImVec2(7.0f, 5.0f);
    style.ItemSpacing      = ImVec2(10.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding      = ImVec2(6.0f, 4.0f);
    style.WindowPadding    = ImVec2(10.0f, 10.0f);
    style.TabRounding      = 4.0f;
    style.FrameRounding    = 3.0f;
    g_baseStyle = style;
}

/// Called by MainWindow when the user toggles theme. Applied before the next frame.
extern "C" void LogCaterRequestTheme(int theme) {
    g_pendingTheme = theme;
}

/// Apply a professional dark theme (Android-green accent).
static void applyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    const ImVec4 bg         = ImVec4(0.105f, 0.115f, 0.130f, 1.00f); // window
    const ImVec4 panel      = ImVec4(0.135f, 0.148f, 0.168f, 1.00f); // child/table bg
    const ImVec4 panelAlt   = ImVec4(0.118f, 0.130f, 0.148f, 1.00f); // alt rows
    const ImVec4 header     = ImVec4(0.165f, 0.185f, 0.215f, 1.00f);
    const ImVec4 border     = ImVec4(0.235f, 0.265f, 0.305f, 1.00f);
    const ImVec4 text       = ImVec4(0.910f, 0.925f, 0.940f, 1.00f);
    const ImVec4 textDim    = ImVec4(0.570f, 0.600f, 0.635f, 1.00f);
    const ImVec4 accent     = ImVec4(0.240f, 0.860f, 0.520f, 1.00f); // Android green
    const ImVec4 accentDim  = ImVec4(0.180f, 0.620f, 0.390f, 1.00f);

    c[ImGuiCol_Text]                 = text;
    c[ImGuiCol_TextDisabled]         = textDim;
    c[ImGuiCol_WindowBg]             = bg;
    c[ImGuiCol_ChildBg]              = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.145f, 0.160f, 0.182f, 0.98f);
    c[ImGuiCol_Border]               = border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.000f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.160f, 0.180f, 0.205f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.200f, 0.225f, 0.255f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.235f, 0.265f, 0.300f, 1.00f);
    c[ImGuiCol_TitleBg]              = header;
    c[ImGuiCol_TitleBgActive]        = header;
    c[ImGuiCol_TitleBgCollapsed]     = header;
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.125f, 0.138f, 0.158f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.120f, 0.132f, 0.150f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.280f, 0.310f, 0.350f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.360f, 0.400f, 0.450f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.440f, 0.480f, 0.530f, 1.00f);
    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentDim;
    c[ImGuiCol_Button]               = ImVec4(0.185f, 0.210f, 0.240f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.240f, 0.270f, 0.310f, 1.00f);
    c[ImGuiCol_ButtonActive]         = accentDim;
    c[ImGuiCol_Header]               = ImVec4(0.200f, 0.350f, 0.260f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.230f, 0.420f, 0.310f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.160f, 0.300f, 0.220f, 1.00f);
    c[ImGuiCol_Separator]            = border;
    c[ImGuiCol_SeparatorHovered]     = accent;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.300f, 0.340f, 0.380f, 0.50f);
    c[ImGuiCol_ResizeGripHovered]    = accent;
    c[ImGuiCol_ResizeGripActive]     = accentDim;
    c[ImGuiCol_Tab]                  = ImVec4(0.150f, 0.170f, 0.195f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.220f, 0.250f, 0.285f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.200f, 0.370f, 0.270f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = ImVec4(0.120f, 0.135f, 0.155f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.170f, 0.280f, 0.215f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(0.240f, 0.860f, 0.520f, 0.25f);
    c[ImGuiCol_NavHighlight]         = accent;
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.000f, 0.000f, 0.000f, 0.55f);
    c[ImGuiCol_TableHeaderBg]        = header;
    c[ImGuiCol_TableBorderStrong]    = border;
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.190f, 0.215f, 0.245f, 1.00f);
    c[ImGuiCol_TableRowBg]           = panel;
    c[ImGuiCol_TableRowBgAlt]        = panelAlt;
}

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
    // Closing the window hides it to the tray instead of quitting;
    // use the tray menu "Exit" to actually quit.
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        glfwHideWindow(w);
        glfwSetWindowShouldClose(w, GLFW_FALSE);
    });

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
        initTray(hwnd);
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

    // Apply theme + custom padding (before ScaleAllSizes so they scale with zoom)
    applyTheme(settings.uiTheme);

    // Apply style spacing to match font scale
    ImGui::GetStyle().ScaleAllSizes(settings.uiScale);


    // --- Create Application ---
    Application app;
    app.init(settings);

    // --- Main loop ---
    ImVec4 clear_color = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);

    while (!glfwWindowShouldClose(window) && !g_exitRequested) {
        glfwPollEvents();

        // --- Apply deferred font atlas rebuild BEFORE NewFrame ---
        // Must be before NewFrame because NewFrame caches font pointers.
        if (g_pendingFontScale > 0.0f) {
            buildFontAtlas(g_pendingFontScale);
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ImGui_ImplOpenGL3_CreateFontsTexture();
            g_pendingFontScale = -1.0f;
        }
        // --- Apply deferred theme switch BEFORE NewFrame ---
        if (g_pendingTheme >= 0) {
            applyTheme(g_pendingTheme);
            ImGui::GetStyle() = g_baseStyle;
            ImGui::GetStyle().ScaleAllSizes(settings.uiScale);
            g_pendingTheme = -1;
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
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    app.shutdown();
    settings.save(Settings::defaultPath());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
