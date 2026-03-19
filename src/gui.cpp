#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <math.h>

#define SOCKET_PATH "/tmp/spotpitch.sock"

static bool send_command(const char* cmd) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return false; }
    write(fd, cmd, strlen(cmd));
    close(fd);
    return true;
}

int main() {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(420, 520, "SpotPitch", nullptr, nullptr);
    if (!window) return 1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    float pitch_semitones = 0.0f;
    float speed = 1.0f;
    float reverb_room = 0.5f;
    float reverb_wet = 0.3f;
    bool reverb_on = false;
    bool last_reverb_on = false;

    ImVec4 green = ImVec4(0.11f, 0.73f, 0.33f, 1.0f);
    ImVec4 green_dark = ImVec4(0.08f, 0.55f, 0.25f, 1.0f);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        bool connected = (access(SOCKET_PATH, F_OK) == 0);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(420, 520));
        ImGui::Begin("SpotPitch", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, green);
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Text("SpotPitch");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::SameLine(300);
        if (connected) {
            ImGui::PushStyleColor(ImGuiCol_Text, green);
            ImGui::Text("● Connected");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.3f,0.3f,1));
            ImGui::Text("● Disconnected");
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Pitch
        ImGui::Text("Pitch");
        ImGui::SameLine(300);
        ImGui::Text("%.1f semitones", pitch_semitones);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, green);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, green_dark);
        ImGui::SetNextItemWidth(390);
        if (ImGui::SliderFloat("##pitch", &pitch_semitones, -12.0f, 12.0f)) {
            float scale = powf(2.0f, pitch_semitones / 12.0f);
            char cmd[64]; snprintf(cmd, sizeof(cmd), "pitch:%.4f", scale);
            send_command(cmd);
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();

        // Speed
        ImGui::Text("Speed");
        ImGui::SameLine(300);
        ImGui::Text("%.2fx", speed);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, green);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, green_dark);
        ImGui::SetNextItemWidth(390);
        if (ImGui::SliderFloat("##speed", &speed, 0.5f, 3.5f)) {
            char cmd[64]; snprintf(cmd, sizeof(cmd), "speed:%.4f", speed);
            send_command(cmd);
        }
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Reverb
        ImGui::Text("Reverb");
        ImGui::SameLine(300);
        if (ImGui::Checkbox("##reverb_on", &reverb_on)) {
            char cmd[64]; snprintf(cmd, sizeof(cmd), "reverb_enabled:%d", reverb_on ? 1 : 0);
            send_command(cmd);
        }

        if (reverb_on) {
            ImGui::Text("Room Size");
            ImGui::SameLine(300);
            ImGui::Text("%.2f", reverb_room);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, green);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, green_dark);
            ImGui::SetNextItemWidth(390);
            if (ImGui::SliderFloat("##room", &reverb_room, 0.0f, 1.0f)) {
                char cmd[64]; snprintf(cmd, sizeof(cmd), "reverb_room:%.4f", reverb_room);
                send_command(cmd);
            }
            ImGui::PopStyleColor(2);

            ImGui::Text("Wet Mix");
            ImGui::SameLine(300);
            ImGui::Text("%.2f", reverb_wet);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, green);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, green_dark);
            ImGui::SetNextItemWidth(390);
            if (ImGui::SliderFloat("##wet", &reverb_wet, 0.0f, 1.0f)) {
                char cmd[64]; snprintf(cmd, sizeof(cmd), "reverb_wet:%.4f", reverb_wet);
                send_command(cmd);
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Presets
        ImGui::Text("Presets");
        ImGui::Spacing();

        auto preset = [&](const char* label, float semitones, float spd, bool rev, float room, float wet) {
            ImGui::PushStyleColor(ImGuiCol_Button, green_dark);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, green);
            if (ImGui::Button(label, ImVec2(88, 35))) {
                pitch_semitones = semitones;
                speed = spd;
                reverb_on = rev;
                reverb_room = room;
                reverb_wet = wet;
                float scale = powf(2.0f, semitones / 12.0f);
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "pitch:%.4f", scale); send_command(cmd);
                snprintf(cmd, sizeof(cmd), "speed:%.4f", spd); send_command(cmd);
                snprintf(cmd, sizeof(cmd), "reverb_enabled:%d", rev ? 1 : 0); send_command(cmd);
                snprintf(cmd, sizeof(cmd), "reverb_room:%.4f", room); send_command(cmd);
                snprintf(cmd, sizeof(cmd), "reverb_wet:%.4f", wet); send_command(cmd);
            }
            ImGui::PopStyleColor(2);
        };

        preset("Normal",    0.0f,  1.0f,  false, 0.5f, 0.3f);
        ImGui::SameLine();
        preset("Slowed",   -3.0f,  0.8f,  true,  0.8f, 0.5f);
        ImGui::SameLine();
        preset("Nightcore", 4.0f,  1.25f, false, 0.5f, 0.3f);
        ImGui::SameLine();
        preset("Daycore",  -4.0f,  0.9f,  true,  0.7f, 0.4f);

        ImGui::End();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
