// SPDX-License-Identifier: MIT
// dragdoll_view — live scenario viewer (raylib). The seed of the workbench: it plays the very same scenarios
// the unit tests run, in real time, so a human can judge how they look. Built ONLY on the public C ABI.
//
// Keys: Space pause | N step one fixed step | Up/Down time scale | F host frame rate | R restart
//       A toggle auto-restart | L joint-limit overlay | Tab next scenario | H help | mouse: left-drag orbit, middle-drag pan, wheel zoom | Esc quit

#include <dragdoll_scenarios/scenario.hpp>

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include "command_server.hpp"
#include "hot_reload.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <random>
#include <string>
#include <vector>

using dragdoll::scenarios::Instance;

namespace
{
// ------------------------------------------------------------------------------------------------
// Debug geometry collected from the C ABI each frame
// ------------------------------------------------------------------------------------------------
struct Tri
{
    Vector3 a, b, c;
    Color color;
};
struct Line
{
    Vector3 a, b;
    Color color;
};

struct Geometry
{
    std::vector<Tri> tris;
    std::vector<Line> lines;
};

Vector3 toRay(DdVec3 v)
{
    return Vector3{v.x, v.y, v.z};
}

Color toColor(uint32_t abgr)
{
    return Color{static_cast<unsigned char>(abgr & 255), static_cast<unsigned char>((abgr >> 8) & 255),
                 static_cast<unsigned char>((abgr >> 16) & 255), static_cast<unsigned char>((abgr >> 24) & 255)};
}

void DD_CALL onTriangle(void* user, DdVec3 a, DdVec3 b, DdVec3 c, uint32_t color)
{
    static_cast<Geometry*>(user)->tris.push_back(Tri{toRay(a), toRay(b), toRay(c), toColor(color)});
}

void DD_CALL onLine(void* user, DdVec3 a, DdVec3 b, uint32_t color)
{
    static_cast<Geometry*>(user)->lines.push_back(Line{toRay(a), toRay(b), toColor(color)});
}

// ------------------------------------------------------------------------------------------------
// Host frame-rate simulation
// ------------------------------------------------------------------------------------------------
enum class HostMode
{
    Real,
    Fps30,
    Fps60,
    Fps144,
    Jitter,
    Count
};

const char* hostModeName(HostMode m)
{
    switch (m)
    {
    case HostMode::Real: return "real frame time (monitor rate)";
    case HostMode::Fps30: return "SA vanilla: 30 fps";
    case HostMode::Fps60: return "60 fps mod";
    case HostMode::Fps144: return "144 Hz";
    case HostMode::Jitter: return "unlocked: random 30-200 fps";
    default: return "?";
    }
}

// ------------------------------------------------------------------------------------------------
// Orbit camera around a target, with an arbitrary world up axis
// ------------------------------------------------------------------------------------------------
// Horizontal basis for a given up axis. World +X is always horizontal-right on screen at azimuth 0, so a scenario
// looks the same whether its world is Y-up or Z-up.
void horizontalBasis(Vector3 up, Vector3& e1, Vector3& e2)
{
    Vector3 ref = std::fabs(up.x) < 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    e1 = Vector3Normalize(Vector3Subtract(ref, Vector3Scale(up, Vector3DotProduct(ref, up))));
    e2 = Vector3CrossProduct(up, e1);
}

struct OrbitCamera
{
    // Switch the world's up axis while keeping the view where it is (target position relative to the ground).
    void setUp(Vector3 newUp)
    {
        Vector3 o1, o2, n1, n2;
        horizontalBasis(up, o1, o2);
        horizontalBasis(newUp, n1, n2);
        const float h1 = Vector3DotProduct(target, o1), h2 = Vector3DotProduct(target, o2), v = Vector3DotProduct(target, up);
        up = newUp;
        target = Vector3Add(Vector3Add(Vector3Scale(n1, h1), Vector3Scale(n2, h2)), Vector3Scale(newUp, v));
    }

    Vector3 up{0, 1, 0};
    Vector3 target{0, 0, 0};
    float distance = 12.0f;
    float azimuth = 0.9f;
    float elevation = 0.45f;

    Camera3D build() const
    {
        Vector3 e1, e2;
        horizontalBasis(up, e1, e2);
        const Vector3 horizontal = Vector3Add(Vector3Scale(e1, std::cos(azimuth)), Vector3Scale(e2, std::sin(azimuth)));
        const Vector3 dir = Vector3Add(Vector3Scale(horizontal, std::cos(elevation)), Vector3Scale(up, std::sin(elevation)));
        Camera3D camera{};
        camera.position = Vector3Add(target, Vector3Scale(dir, distance));
        camera.target = target;
        camera.up = up;
        camera.fovy = 55.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        return camera;
    }

    void update()
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            const Vector2 d = GetMouseDelta();
            azimuth -= d.x * 0.006f;
            elevation = Clamp(elevation + d.y * 0.006f, -1.4f, 1.5f);
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
        {
            // Pan in the screen plane (like design tools): the scene follows the cursor 1:1 at the target's depth.
            const Vector2 d = GetMouseDelta();
            const Camera3D camera = build();
            const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
            const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));
            const Vector3 screenUp = Vector3CrossProduct(right, forward);
            const float worldPerPixel = 2.0f * distance * std::tan(camera.fovy * 0.5f * DEG2RAD) / static_cast<float>(GetScreenHeight());
            target = Vector3Add(target, Vector3Add(Vector3Scale(right, -d.x * worldPerPixel), Vector3Scale(screenUp, d.y * worldPerPixel)));
        }
        distance = Clamp(distance * (1.0f - GetMouseWheelMove() * 0.1f), 1.5f, 200.0f);
    }
};

void drawGrid(const OrbitCamera& cam)
{
    Vector3 e1, e2;
    horizontalBasis(cam.up, e1, e2);
    const float snap = 5.0f;
    const Vector3 centre = Vector3Add(Vector3Scale(e1, std::round(Vector3DotProduct(cam.target, e1) / snap) * snap),
                                      Vector3Scale(e2, std::round(Vector3DotProduct(cam.target, e2) / snap) * snap));
    const Vector3 lift = Vector3Add(centre, Vector3Scale(cam.up, 0.004f));
    rlBegin(RL_LINES);
    for (int i = -30; i <= 30; ++i)
    {
        const bool major = (i % 5) == 0;
        rlColor4ub(255, 255, 255, major ? 46 : 22);
        const Vector3 a1 = Vector3Add(Vector3Add(Vector3Scale(e1, static_cast<float>(i)), Vector3Scale(e2, -30.0f)), lift);
        const Vector3 b1 = Vector3Add(Vector3Add(Vector3Scale(e1, static_cast<float>(i)), Vector3Scale(e2, 30.0f)), lift);
        const Vector3 a2 = Vector3Add(Vector3Add(Vector3Scale(e2, static_cast<float>(i)), Vector3Scale(e1, -30.0f)), lift);
        const Vector3 b2 = Vector3Add(Vector3Add(Vector3Scale(e2, static_cast<float>(i)), Vector3Scale(e1, 30.0f)), lift);
        rlVertex3f(a1.x, a1.y, a1.z);
        rlVertex3f(b1.x, b1.y, b1.z);
        rlVertex3f(a2.x, a2.y, a2.z);
        rlVertex3f(b2.x, b2.y, b2.z);
    }
    rlEnd();
}

// When the camera dips under the floor, everything that lies entirely at or below the floor plane (the ground slab)
// is hidden so the scene stays visible from below; the grid is still drawn.
bool belowFloor(const Vector3& v, const Vector3& up)
{
    return Vector3DotProduct(v, up) <= 0.001f;
}

void drawGeometry(const Geometry& geometry, unsigned char alpha = 255, const Vector3* hideBelowUp = nullptr)
{
    const Vector3 light = Vector3Normalize(Vector3{0.4f, 0.8f, 0.5f});
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (const Tri& t : geometry.tris)
    {
        if (hideBelowUp != nullptr && belowFloor(t.a, *hideBelowUp) && belowFloor(t.b, *hideBelowUp) &&
            belowFloor(t.c, *hideBelowUp))
        {
            continue;
        }
        const Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(t.b, t.a), Vector3Subtract(t.c, t.a)));
        // Two-sided shading against two lights so every orientation reads clearly.
        const float shade = 0.45f + 0.30f * std::fabs(Vector3DotProduct(n, light)) +
                            0.25f * std::fabs(Vector3DotProduct(n, Vector3{-0.6f, 0.3f, -0.5f}));
        rlColor4ub(static_cast<unsigned char>(t.color.r * shade), static_cast<unsigned char>(t.color.g * shade),
                   static_cast<unsigned char>(t.color.b * shade), static_cast<unsigned char>(t.color.a * alpha / 255));
        rlVertex3f(t.a.x, t.a.y, t.a.z);
        rlVertex3f(t.b.x, t.b.y, t.b.z);
        rlVertex3f(t.c.x, t.c.y, t.c.z);
    }
    rlEnd();
    rlBegin(RL_LINES);
    for (const Line& l : geometry.lines)
    {
        rlColor4ub(l.color.r, l.color.g, l.color.b, static_cast<unsigned char>(l.color.a * alpha / 255));
        rlVertex3f(l.a.x, l.a.y, l.a.z);
        rlVertex3f(l.b.x, l.b.y, l.b.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
}

// ------------------------------------------------------------------------------------------------
// App state
// ------------------------------------------------------------------------------------------------
struct App
{
    std::vector<std::string> names;
    size_t scenarioIndex = 0;
    Instance main;
    Instance shadow; // Reference run at a steady 120 Hz (only for rate-independent scenarios)
    bool hasShadow = false;
    OrbitCamera camera;

    HostMode hostMode = HostMode::Real;
    float timeScale = 1.0f;
    bool paused = false;
    bool showHelp = true;
    bool autoRestart = true;
    bool showLimits = false;
    double hostAccumulator = 0.0;
    std::mt19937 rng{7};

    float lastHostDt = 0.0f;
    uint32_t lastSteps = 0;
    uint64_t hash = 0;
    uint64_t shadowHash = 0;
    std::string error;

    // Recording (--record) and replay (--replay) modes
    std::string recordPath;
    std::string replayPath;
    DdReplay* replay = nullptr;
    DdReplayInfo replayInfo{};
    DdReplayFrame replayFrame{};
    double replayWait = 0.0; // host time the last replayed frame covered
    double replayAccumulator = 0.0;
    std::string replayNote;
    uint64_t replayNoteFrame = 0;
    double replayNoteTimer = 0.0;
    std::string replayBuild;
    bool replayFocusPending = false; // Centre the camera on the ragdoll once it exists

    // F9 annotation (record mode): the sim pauses, the user types a note, Enter saves it plus a screenshot.
    bool noteEditing = false;
    std::string noteText;
    bool notePrevPaused = false;
    std::string pendingShot; // Full path of the screenshot to take at the end of this frame

    // Right-click push (task 2.3): an impulse along the view ray at the clicked body. Goes through the C API, so it is
    // recorded and replays like any other input.
    float pushStrength = 40.0f; // N*s
    std::string lastPush;
    struct Push
    {
        uint64_t step;
        uint32_t body;
        DdVec3 impulse;
        DdVec3 point;
    };
    std::vector<Push> pushes; // This run's pushes, re-applied when a hot reload restores the session

    // Hot reload of dragdoll.dll (task 2.4)
    bool hotEnabled = false;
    std::string commandStatus; // Command channel (task 2.7)
    double hotNextCheck = 0.0;
    std::string hotStatus;

    // Live rig editing (task 2.4): --rig <file> is watched; the Rig panel edits joint friction in memory.
    std::string rigPath;
    std::filesystem::file_time_type rigFileTime{};
    double rigNextCheck = 0.0;
    nlohmann::json rigDoc; // What the ragdoll currently uses (file + unsaved edits)
    bool rigEdited = false;
    std::string rigStatus;
};

// While a note is being typed, hotkeys are off.
bool keyPressed(const App& app, int key)
{
    return !app.noteEditing && !ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(key);
}

std::string shotPathFor(const std::string& recordPath, uint64_t step)
{
    std::string base = recordPath;
    const size_t dot = base.find_last_of('.');
    const size_t slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    {
        base.erase(dot);
    }
    return base + "_step" + std::to_string(step) + ".png";
}

std::string fileNameOf(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void beginNote(App& app)
{
    app.noteEditing = true;
    app.noteText.clear();
    app.notePrevPaused = app.paused;
    app.paused = true; // Freeze the moment being annotated
}

void editNote(App& app)
{
    for (int c = GetCharPressed(); c != 0; c = GetCharPressed())
    {
        if (c >= 32 && app.noteText.size() < 160)
        {
            int size = 0;
            app.noteText += CodepointToUTF8(c, &size);
        }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !app.noteText.empty())
    {
        // Remove one UTF-8 character
        size_t n = app.noteText.size() - 1;
        while (n > 0 && (static_cast<unsigned char>(app.noteText[n]) & 0xC0) == 0x80) --n;
        app.noteText.erase(n);
    }
    if (IsKeyPressed(KEY_ESCAPE))
    {
        app.noteEditing = false;
        app.paused = app.notePrevPaused;
    }
    else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
    {
        const uint64_t step = dragdoll_world_get_step_count(app.main.world);
        app.pendingShot = shotPathFor(app.recordPath, step);
        std::string text = app.noteText.empty() ? "F9 mark at step " + std::to_string(step) : app.noteText;
        text += " | shot=" + fileNameOf(app.pendingShot);
        dragdoll_world_record_note(app.main.world, text.c_str());
        app.noteEditing = false; // The sim stays paused until the screenshot has been taken
    }
}

void drawNoteBox(const App& app)
{
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    DrawRectangle(0, 0, w, h, Color{0, 0, 0, 110});
    const int boxW = std::min(w - 40, 760);
    const int x = (w - boxW) / 2;
    const int y = h / 2 - 50;
    DrawRectangle(x, y, boxW, 100, Color{30, 34, 44, 245});
    DrawRectangleLines(x, y, boxW, 100, Color{255, 220, 120, 255});
    DrawText("Note for this moment  (Enter = save + screenshot, Esc = cancel, empty = plain mark)", x + 12, y + 10, 16, Color{255, 220, 120, 255});
    const std::string shown = app.noteText + (static_cast<int>(GetTime() * 2.0) % 2 == 0 ? "_" : " ");
    DrawText(shown.c_str(), x + 12, y + 50, 22, RAYWHITE);
}

DdWorld* activeWorld(App& app)
{
    return app.replay != nullptr ? dragdoll_replay_get_world(app.replay) : app.main.world;
}

bool loadScenario(App& app, size_t index, bool resetCamera)
{
    app.scenarioIndex = index % app.names.size();
    app.error.clear();
    app.pushes.clear();
    app.lastPush.clear();
    app.hostAccumulator = 0.0;
    app.lastHostDt = 0.0f;
    app.lastSteps = 0;
    if (!Instance::make(app.names[app.scenarioIndex], app.main, app.error, app.recordPath.empty() ? nullptr : app.recordPath.c_str()))
    {
        return false;
    }
    app.hasShadow = app.main.scenario->rateIndependent();
    if (app.hasShadow && !Instance::make(app.names[app.scenarioIndex], app.shadow, app.error))
    {
        return false;
    }
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    app.main.scenario->configure(desc);
    if (resetCamera)                                  // The view only moves when explicitly asked (first load)...
    {
        app.camera.up = Vector3Normalize(toRay(desc.up));
        app.camera.target = toRay(app.main.scenario->cameraTarget());
        app.camera.distance = app.main.scenario->cameraDistance();
    }
    else
    {
        app.camera.setUp(Vector3Normalize(toRay(desc.up))); // ...the world's up axis follows the scenario, the view stays.
    }
    return true;
}

bool openReplay(App& app, bool resetCamera)
{
    dragdoll_replay_close(app.replay);
    app.replay = nullptr;
    app.error.clear();
    if (dragdoll_replay_open(app.replayPath.c_str(), &app.replay) != DD_OK)
    {
        app.error = std::string("replay: ") + dragdoll_last_error_message();
        return false;
    }
    app.replayInfo = DdReplayInfo{};
    app.replayInfo.struct_size = sizeof(DdReplayInfo);
    dragdoll_replay_get_info(app.replay, &app.replayInfo);
    app.replayBuild = app.replayInfo.recorded_build;
    app.replayFrame = DdReplayFrame{};
    app.replayFrame.struct_size = sizeof(DdReplayFrame);
    app.replayWait = 0.0;
    app.replayAccumulator = 0.0;
    app.replayNote.clear();
    app.replayNoteTimer = 0.0;
    if (resetCamera)
    {
        app.camera.up = Vector3Normalize(toRay(app.replayInfo.up));
        app.camera.target = Vector3Scale(app.camera.up, 0.8f);
        app.camera.distance = 8.0f;
        app.replayFocusPending = true;
    }
    return true;
}

// Centres the camera on the first character's root body (e.g. SA recordings sit at map coordinates ~km from 0).
bool focusCharacter(App& app)
{
    DdWorld* world = app.replay != nullptr ? dragdoll_replay_get_world(app.replay) : app.main.world;
    DdCharacter* c = world != nullptr ? dragdoll_world_get_character(world, 0) : nullptr;
    if (c == nullptr)
    {
        return false;
    }
    std::vector<DdTransform> bodies(dragdoll_character_get_body_count(c)); // The call fills all bodies or none
    if (bodies.empty() || dragdoll_character_get_body_transforms(c, 1, bodies.data(), static_cast<uint32_t>(bodies.size())) != DD_OK)
    {
        return false;
    }
    app.camera.target = toRay(bodies[0].position);
    app.camera.distance = 6.0f;
    return true;
}

void advanceReplay(App& app)
{
    if (app.replay == nullptr || app.replayFrame.at_end != 0)
    {
        return;
    }
    if (dragdoll_replay_advance(app.replay, &app.replayFrame) != DD_OK)
    {
        app.error = std::string("replay: ") + dragdoll_last_error_message();
        app.replayFrame.at_end = 1;
        return;
    }
    app.replayWait = app.replayFrame.host_dt;
    if (app.replayFrame.note != nullptr)
    {
        app.replayNote = app.replayFrame.note;
        app.replayNoteFrame = app.replayFrame.frame;
        app.replayNoteTimer = 4.0;
    }
}

void updateReplay(App& app, float realDt)
{
    realDt = std::min(realDt, 0.1f);
    app.replayNoteTimer = std::max(0.0, app.replayNoteTimer - static_cast<double>(realDt));
    if (app.paused)
    {
        return;
    }
    if (app.replayFrame.at_end != 0)
    {
        if (app.autoRestart)
        {
            openReplay(app, false);
        }
        return;
    }
    // Play the recorded host frames back at the pace they were recorded (scaled by the time scale).
    app.replayAccumulator += static_cast<double>(realDt) * app.timeScale;
    int guard = 0;
    while (app.replayAccumulator >= app.replayWait && app.replayFrame.at_end == 0 && guard++ < 64)
    {
        app.replayAccumulator -= app.replayWait;
        advanceReplay(app);
    }
}

void drawReplayHud(App& app)
{
    int y = 12;
    const int x = 14;
    auto text = [&](const std::string& s, Color color, int size = 18) {
        DrawText(s.c_str(), x, y, size, color);
        y += size + 5;
    };
    char buffer[256];
    text("REPLAY: " + app.replayPath, RAYWHITE, 22);
    text("saved by: " + app.replayBuild + (app.replayInfo.same_build ? "  (same program: exact copy expected)" : "  (another program build)"),
         Color{200, 205, 215, 255}, 16);
    std::snprintf(buffer, sizeof(buffer), "host frame %llu / %llu | last frame %.2f ms -> %u fixed step(s) | %s | time scale %.2fx",
                  static_cast<unsigned long long>(app.replayFrame.frame), static_cast<unsigned long long>(app.replayInfo.frame_count),
                  static_cast<double>(app.replayFrame.host_dt) * 1000.0, app.replayFrame.steps,
                  app.paused ? "PAUSED" : (app.replayFrame.at_end ? "END" : "playing"), static_cast<double>(app.timeScale));
    text(buffer, Color{160, 220, 255, 255});
    if (app.replayFrame.frame > 0)
    {
        if (app.replayFrame.first_divergence == 0)
        {
            text("identical to the original run at every frame so far", Color{110, 230, 130, 255}, 16);
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "DIVERGED from the recording first at frame %llu%s",
                          static_cast<unsigned long long>(app.replayFrame.first_divergence),
                          app.replayInfo.same_build ? "" : " (expected: recorded by another build)");
            text(buffer, Color{255, 120, 90, 255}, 16);
        }
    }
    if (app.replayNoteTimer > 0.0)
    {
        std::snprintf(buffer, sizeof(buffer), "NOTE (frame %llu): %s", static_cast<unsigned long long>(app.replayNoteFrame), app.replayNote.c_str());
        text(buffer, Color{255, 220, 120, 255}, 20);
    }
    if (!app.error.empty())
    {
        text("ERROR: " + app.error, Color{255, 90, 90, 255});
    }
    if (app.showHelp)
    {
        DrawText("Space pause | N next frame | Up/Down time scale | R restart | F focus ragdoll | A auto-restart | L joint limits | H hide help | left-drag orbit | middle-drag pan | wheel zoom",
                 x, GetScreenHeight() - 28, 16, Color{170, 175, 185, 255});
    }
    DrawFPS(GetScreenWidth() - 90, 10);
}

void advanceHostFrame(App& app, float dt)
{
    app.lastHostDt = dt;
    app.lastSteps = app.main.hostFrame(dt);
    if (app.hasShadow)
    {
        // Bring the reference world to the same simulated step count with a perfectly steady clock.
        const uint64_t target = dragdoll_world_get_step_count(app.main.world);
        const uint64_t have = dragdoll_world_get_step_count(app.shadow.world);
        if (target > have)
        {
            dragdoll_world_step_fixed(app.shadow.world, static_cast<uint32_t>(target - have));
        }
    }
}

void update(App& app, float realDt)
{
    realDt = std::min(realDt, 0.1f);
    if (app.paused)
    {
        return;
    }
    // A scenario that has played out restarts at once, in the same frame (no freeze), exactly like pressing R.
    const double duration = app.main.scenario->duration();
    const bool finished = duration > 0.0 && static_cast<double>(dragdoll_world_get_step_count(app.main.world)) / 120.0 >= duration;
    // Without auto-restart the world keeps running past the scenario's end, so it can still be pushed and inspected
    // (resting bodies sleep, so this costs almost nothing).
    if (finished && app.autoRestart)
    {
        loadScenario(app, app.scenarioIndex, false);
    }
    const double scaled = static_cast<double>(realDt) * app.timeScale;
    if (app.hostMode == HostMode::Real)
    {
        advanceHostFrame(app, static_cast<float>(scaled));
        return;
    }
    app.hostAccumulator += scaled;
    int guard = 0;
    while (guard++ < 16)
    {
        float hostDt = 1.0f / 30.0f;
        switch (app.hostMode)
        {
        case HostMode::Fps30: hostDt = 1.0f / 30.0f; break;
        case HostMode::Fps60: hostDt = 1.0f / 60.0f; break;
        case HostMode::Fps144: hostDt = 1.0f / 144.0f; break;
        case HostMode::Jitter: {
            std::uniform_real_distribution<float> dist(1.0f / 200.0f, 1.0f / 30.0f);
            hostDt = dist(app.rng);
            break;
        }
        default: break;
        }
        if (app.hostAccumulator < hostDt)
        {
            break;
        }
        app.hostAccumulator -= hostDt;
        advanceHostFrame(app, hostDt);
    }
}

std::vector<std::string> wrap(const std::string& text, size_t width)
{
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find(' ', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        const std::string word = text.substr(start, end - start);
        if (!line.empty() && line.size() + 1 + word.size() > width)
        {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        start = end + 1;
    }
    if (!line.empty())
    {
        lines.push_back(line);
    }
    return lines;
}

void drawHud(App& app)
{
    int y = 12;
    const int x = 14;
    auto text = [&](const std::string& s, Color color, int size = 18) {
        DrawText(s.c_str(), x, y, size, color);
        y += size + 5;
    };

    char buffer[256];
    text(std::string("scenario: ") + app.names[app.scenarioIndex] + "   (" + std::to_string(app.scenarioIndex + 1) + "/" +
             std::to_string(app.names.size()) + ", Tab = next)",
         RAYWHITE, 22);
    for (const std::string& line : wrap(app.main.scenario->description(), 105))
    {
        text(line, Color{200, 205, 215, 255}, 17);
    }
    y += 6;

    const uint64_t step = dragdoll_world_get_step_count(app.main.world);
    std::snprintf(buffer, sizeof(buffer), "physics: step %llu | sim time %.3f s | fixed 120 Hz | %s",
                  static_cast<unsigned long long>(step), static_cast<double>(step) / 120.0, app.paused ? "PAUSED" : "running");
    text(buffer, Color{160, 220, 255, 255});
    std::snprintf(buffer, sizeof(buffer), "host rate: %s | time scale %.2fx | auto-restart: %s", hostModeName(app.hostMode),
                  static_cast<double>(app.timeScale), app.autoRestart ? "on" : "off");
    text(buffer, Color{160, 220, 255, 255});
    if (app.lastHostDt > 0.0f)
    {
        std::snprintf(buffer, sizeof(buffer), "last host frame: %.2f ms (%.0f fps) -> %u fixed step(s) | interpolation alpha %.2f",
                      static_cast<double>(app.lastHostDt) * 1000.0, 1.0 / static_cast<double>(app.lastHostDt), app.lastSteps,
                      static_cast<double>(dragdoll_world_get_interpolation_alpha(app.main.world)));
        text(buffer, Color{160, 220, 255, 255});
    }

    dragdoll_world_compute_state_hash(app.main.world, &app.hash);
    std::snprintf(buffer, sizeof(buffer), "state hash: %016llx", static_cast<unsigned long long>(app.hash));
    text(buffer, Color{190, 190, 190, 255}, 16);
    if (app.hasShadow)
    {
        dragdoll_world_compute_state_hash(app.shadow.world, &app.shadowHash);
        const bool match = app.hash == app.shadowHash;
        std::snprintf(buffer, sizeof(buffer), "steady-120Hz reference run: %016llx  ->  %s",
                      static_cast<unsigned long long>(app.shadowHash), match ? "IDENTICAL" : "DIFFERENT!");
        text(buffer, match ? Color{110, 230, 130, 255} : Color{255, 90, 90, 255}, 16);
    }

    std::vector<std::string> lines;
    app.main.scenario->hud(app.main.world, lines);
    for (const std::string& line : lines)
    {
        text(line, Color{255, 220, 120, 255});
    }

    if (!app.error.empty())
    {
        text("ERROR: " + app.error, Color{255, 90, 90, 255});
    }

    if (app.showHelp)
    {
        const int h = GetScreenHeight();
        DrawText("Space pause | N step | Up/Down time scale | F host frame rate | R restart | A auto-restart | L joint limits | Tab next | right-click push | F9 note (when recording) | H hide help | left-drag orbit | middle-drag pan | wheel zoom",
                 x, h - 28, 16, Color{170, 175, 185, 255});
    }
    DrawFPS(GetScreenWidth() - 90, 10);
}
// ------------------------------------------------------------------------------------------------
// ImGui panels (task 2.3)
// ------------------------------------------------------------------------------------------------
struct BodyMotion
{
    uint64_t step = 0;
    std::vector<DdTransform> previous;
    std::vector<float> speed; // m/s
    std::vector<float> spin;  // deg/s
};

void measureMotion(DdWorld* world, BodyMotion& m)
{
    DdCharacter* c = world != nullptr ? dragdoll_world_get_character(world, 0) : nullptr;
    if (c == nullptr)
    {
        m.previous.clear();
        return;
    }
    const uint64_t step = dragdoll_world_get_step_count(world);
    std::vector<DdTransform> now(dragdoll_character_get_body_count(c));
    dragdoll_character_get_body_transforms(c, 0, now.data(), static_cast<uint32_t>(now.size()));
    if (now.size() != m.previous.size() || step < m.step)
    {
        m.speed.assign(now.size(), 0.0f); // New character or restart
        m.spin.assign(now.size(), 0.0f);
    }
    else if (step > m.step)
    {
        const float dt = static_cast<float>(step - m.step) / 120.0f;
        for (size_t i = 0; i < now.size(); ++i)
        {
            const DdVec3 &a = m.previous[i].position, &b = now[i].position;
            m.speed[i] = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)) / dt;
            const DdQuat &qa = m.previous[i].rotation, &qb = now[i].rotation;
            const float d = std::fabs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w);
            m.spin[i] = 2.0f * std::acos(std::min(1.0f, d)) * RAD2DEG / dt;
        }
    }
    else
    {
        return; // Paused: keep the last measurement
    }
    m.previous = std::move(now);
    m.step = step;
}

// Picks the body under the mouse (each body approximated by a 15 cm sphere at its centre) and pushes it.
void pushBodyUnderMouse(App& app)
{
    DdCharacter* c = app.main.world != nullptr ? dragdoll_world_get_character(app.main.world, 0) : nullptr;
    if (c == nullptr)
    {
        return;
    }
    const Ray ray = GetScreenToWorldRay(GetMousePosition(), app.camera.build());
    std::vector<DdTransform> bodies(dragdoll_character_get_body_count(c));
    dragdoll_character_get_body_transforms(c, 1, bodies.data(), static_cast<uint32_t>(bodies.size()));
    int best = -1;
    RayCollision bestHit{};
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        const DdVec3& p = bodies[i].position;
        const RayCollision hit = GetRayCollisionSphere(ray, Vector3{p.x, p.y, p.z}, 0.15f);
        if (hit.hit && (best < 0 || hit.distance < bestHit.distance))
        {
            best = static_cast<int>(i);
            bestHit = hit;
        }
    }
    if (best < 0)
    {
        return;
    }
    const DdVec3 impulse{ray.direction.x * app.pushStrength, ray.direction.y * app.pushStrength, ray.direction.z * app.pushStrength};
    const DdVec3 point{bestHit.point.x, bestHit.point.y, bestHit.point.z};
    dragdoll_character_apply_impulse(c, static_cast<uint32_t>(best), impulse, &point);
    app.pushes.push_back({dragdoll_world_get_step_count(app.main.world), static_cast<uint32_t>(best), impulse, point});
    char text[128];
    std::snprintf(text, sizeof(text), "pushed %s with %.0f N*s at step %llu", dragdoll_character_get_body_name(c, static_cast<uint32_t>(best)),
                  app.pushStrength, static_cast<unsigned long long>(dragdoll_world_get_step_count(app.main.world)));
    app.lastPush = text;
}

// ------------------------------------------------------------------------------------------------
// Live rig editing
// ------------------------------------------------------------------------------------------------
// Builds a character from `json` in a scratch world: "" if the rig is valid, otherwise the library's error.
std::string validateRig(const std::string& json)
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    DdWorld* world = nullptr;
    if (dragdoll_world_create(&desc, &world) != DD_OK)
    {
        return dragdoll_last_error_message();
    }
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = json.c_str();
    DdCharacter* c = nullptr;
    std::string error;
    if (dragdoll_character_create(world, &cd, &c) != DD_OK)
    {
        error = dragdoll_last_error_message();
    }
    dragdoll_world_destroy(world);
    return error;
}

// Makes `json` the rig of the ragdoll scenarios and restarts the current one (camera kept). A broken rig is refused
// and the previous one stays in use.
bool useRig(App& app, const std::string& json)
{
    const std::string error = validateRig(json);
    if (!error.empty())
    {
        app.rigStatus = "NOT applied: " + error;
        return false;
    }
    dragdoll::scenarios::setRigOverride(json);
    if (app.replay == nullptr)
    {
        loadScenario(app, app.scenarioIndex, false);
    }
    return true;
}

void loadRigFile(App& app)
{
    std::error_code ec;
    app.rigFileTime = std::filesystem::last_write_time(app.rigPath, ec);
    std::ifstream in(std::filesystem::path(app.rigPath), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    if (!in)
    {
        app.rigStatus = "cannot read " + app.rigPath;
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(text.str(), nullptr, false);
    if (doc.is_discarded())
    {
        app.rigStatus = "NOT applied: the file is not valid JSON";
        return;
    }
    if (useRig(app, text.str()))
    {
        app.rigDoc = std::move(doc);
        app.rigEdited = false;
        app.rigStatus = "loaded from file";
    }
}

void watchRigFile(App& app)
{
    if (app.rigPath.empty() || GetTime() < app.rigNextCheck)
    {
        return;
    }
    app.rigNextCheck = GetTime() + 0.5;
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(app.rigPath, ec);
    if (!ec && time != app.rigFileTime)
    {
        loadRigFile(app); // Saved in an editor: reload (drops unsaved slider edits)
    }
}

std::string formatNumber(float v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", v);
    std::string text = buffer;
    while (text.size() > 3 && text.back() == '0' && text[text.size() - 2] != '.') text.pop_back();
    return text;
}

// Writes the edited friction values into the rig file, touching only those numbers (keeps the hand formatting).
void saveFriction(App& app)
{
    std::ifstream in(std::filesystem::path(app.rigPath), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    int written = 0;
    for (const nlohmann::json& body : app.rigDoc["bodies"])
    {
        if (!body.contains("joint") || !body["joint"].contains("friction"))
        {
            continue;
        }
        const size_t name = text.find("\"name\": \"" + body["name"].get<std::string>() + "\"");
        const size_t next = name == std::string::npos ? std::string::npos : text.find("\"name\":", name + 8);
        size_t key = name == std::string::npos ? std::string::npos : text.find("\"friction\":", name);
        if (key == std::string::npos || (next != std::string::npos && key > next))
        {
            continue;
        }
        size_t from = key + 11;
        while (from < text.size() && text[from] == ' ') ++from;
        size_t to = from;
        while (to < text.size() && std::strchr("0123456789.-+eE", text[to]) != nullptr) ++to;
        text.replace(from, to - from, formatNumber(body["joint"]["friction"].get<float>()));
        ++written;
    }
    std::ofstream out(std::filesystem::path(app.rigPath), std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out)
    {
        app.rigStatus = "could not write " + app.rigPath;
        return;
    }
    std::error_code ec;
    app.rigFileTime = std::filesystem::last_write_time(app.rigPath, ec); // Already in use: no reload
    app.rigEdited = false;
    app.rigStatus = "saved " + std::to_string(written) + " friction values to the file";
}

void drawRigPanel(App& app)
{
    if (app.rigPath.empty() || app.replay != nullptr || !app.rigDoc.contains("bodies"))
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10.0f, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 470.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Rig (live)"))
    {
        ImGui::TextWrapped("%s%s", fileNameOf(app.rigPath).c_str(), app.rigEdited ? "  (edited, not saved)" : "");
        ImGui::TextDisabled("Saving the file in an editor reloads it.");
        if (!app.rigStatus.empty()) ImGui::TextWrapped("%s", app.rigStatus.c_str());
        ImGui::BeginDisabled(!app.rigEdited);
        if (ImGui::Button("Save to file")) saveFriction(app);
        ImGui::SameLine();
        if (ImGui::Button("Revert")) loadRigFile(app);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Joint friction (N*m)");
        for (nlohmann::json& body : app.rigDoc["bodies"])
        {
            if (!body.contains("joint"))
            {
                continue;
            }
            nlohmann::json& joint = body["joint"];
            float friction = joint.value("friction", 0.0f);
            const std::string name = body["name"].get<std::string>();
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat(name.c_str(), &friction, 0.0f, 10.0f, "%.2f"))
            {
                joint["friction"] = friction;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && useRig(app, app.rigDoc.dump()))
            {
                app.rigEdited = true; // Applied on release: the scenario restarts with the new value
                app.rigStatus = name + " friction " + formatNumber(friction) + " applied";
            }
        }
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------
// Hot reload: swap in a rebuilt dragdoll.dll and restore the session from its inputs
// ------------------------------------------------------------------------------------------------
void hotReload(App& app)
{
    const double started = GetTime();
    const bool wasReplay = app.replay != nullptr;
    const uint64_t target = wasReplay ? app.replayFrame.frame : (app.main.world != nullptr ? dragdoll_world_get_step_count(app.main.world) : 0);
    const std::vector<App::Push> pushes = app.pushes;
    const bool paused = app.paused;

    // Release everything the old library owns, then swap it.
    dragdoll_replay_close(app.replay);
    app.replay = nullptr;
    app.main = Instance();
    app.shadow = Instance();
    dragdoll_shutdown();
    std::string error;
    const bool swapped = hot::swap(error);
    if (dragdoll_init(DD_API_VERSION) != DD_OK)
    {
        app.hotStatus = std::string("library failed to start: ") + dragdoll_last_error_message();
        return;
    }

    // Restore: the same inputs reach the same moment, now simulated by the new code.
    if (wasReplay)
    {
        openReplay(app, false);
        while (app.replay != nullptr && app.replayFrame.frame < target && app.replayFrame.at_end == 0)
        {
            advanceReplay(app);
        }
    }
    else if (loadScenario(app, app.scenarioIndex, false))
    {
        size_t next = 0;
        auto applyDue = [&](uint64_t now) {
            DdCharacter* c = dragdoll_world_get_character(app.main.world, 0);
            for (; next < pushes.size() && pushes[next].step <= now; ++next)
            {
                if (c != nullptr) dragdoll_character_apply_impulse(c, pushes[next].body, pushes[next].impulse, &pushes[next].point);
            }
        };
        for (uint64_t guard = 0; dragdoll_world_get_step_count(app.main.world) < target && guard < target * 2 + 16; ++guard)
        {
            applyDue(dragdoll_world_get_step_count(app.main.world));
            advanceHostFrame(app, 1.0f / 120.0f);
        }
        applyDue(dragdoll_world_get_step_count(app.main.world));
        app.pushes = pushes;
    }
    app.paused = paused;

    char text[200];
    std::snprintf(text, sizeof(text), "%s (library #%d), session restored to %s %llu in %.0f ms%s",
                  swapped ? "reloaded" : "RELOAD FAILED, kept the old library", hot::generation(), wasReplay ? "frame" : "step",
                  static_cast<unsigned long long>(target), (GetTime() - started) * 1000.0,
                  !wasReplay && !app.recordPath.empty() ? " (recording restarted)" : "");
    app.hotStatus = swapped ? text : text + std::string(": ") + error;
}

void watchLibrary(App& app)
{
    if (!app.hotEnabled || GetTime() < app.hotNextCheck)
    {
        return;
    }
    app.hotNextCheck = GetTime() + 0.5;
    if (hot::changed())
    {
        hotReload(app);
    }
}

// ------------------------------------------------------------------------------------------------
// Command channel (task 2.7): one text command per line, one JSON reply per command.
// ------------------------------------------------------------------------------------------------
std::string handleCommand(App& app, BodyMotion& motion, const std::string& line)
{
    using nlohmann::json;
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    auto ok = [](json extra = json::object()) { extra["ok"] = true; return extra.dump(); };
    auto bad = [](const std::string& why) { return json{{"ok", false}, {"error", why}}.dump(); };
    const bool replaying = app.replay != nullptr;

    if (cmd == "help" || cmd.empty())
    {
        return ok({{"commands", "list | load <scenario> | restart | pause | resume | step [n] | timescale <x> | "
                                "autorestart on|off | state | push <body> <x> <y> <z> (N*s) | note <text> | shot <file.png>"}});
    }
    if (cmd == "list")
    {
        return ok({{"scenarios", app.names}});
    }
    if (cmd == "load")
    {
        std::string name;
        in >> name;
        const auto it = std::find(app.names.begin(), app.names.end(), name);
        if (replaying) return bad("viewer is showing a replay");
        if (it == app.names.end()) return bad("unknown scenario: " + name);
        if (!loadScenario(app, static_cast<size_t>(it - app.names.begin()), false)) return bad(app.error);
        return ok({{"scenario", name}});
    }
    if (cmd == "restart")
    {
        if (replaying) openReplay(app, false);
        else loadScenario(app, app.scenarioIndex, false);
        return ok();
    }
    if (cmd == "pause" || cmd == "resume")
    {
        app.paused = cmd == "pause";
        return ok({{"paused", app.paused}});
    }
    if (cmd == "step")
    {
        int n = 1;
        in >> n;
        n = std::clamp(n, 1, 120 * 60);
        app.paused = true;
        for (int i = 0; i < n; ++i)
        {
            if (replaying) advanceReplay(app);
            else if (app.main.world != nullptr) advanceHostFrame(app, 1.0f / 120.0f);
        }
        measureMotion(activeWorld(app), motion);
        return ok({{"steps", n}});
    }
    if (cmd == "timescale")
    {
        float x = 1.0f;
        in >> x;
        app.timeScale = std::clamp(x, 0.0625f, 4.0f);
        return ok({{"timescale", app.timeScale}});
    }
    if (cmd == "autorestart")
    {
        std::string v;
        in >> v;
        app.autoRestart = v == "on";
        return ok({{"autorestart", app.autoRestart}});
    }
    if (cmd == "state")
    {
        DdWorld* world = activeWorld(app);
        if (world == nullptr) return bad("no world");
        uint64_t hash = 0;
        dragdoll_world_compute_state_hash(world, &hash);
        const uint64_t step = dragdoll_world_get_step_count(world);
        json state = {{"mode", replaying ? "replay" : "scenario"},
                      {"name", replaying ? app.replayPath : app.names[app.scenarioIndex]},
                      {"step", step}, {"time", static_cast<double>(step) / 120.0}, {"paused", app.paused},
                      {"hash", [&] { char b[20]; std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(hash)); return std::string(b); }()}};
        if (DdCharacter* c = dragdoll_world_get_character(world, 0))
        {
            std::vector<DdTransform> bodies(dragdoll_character_get_body_count(c));
            dragdoll_character_get_body_transforms(c, 0, bodies.data(), static_cast<uint32_t>(bodies.size()));
            json list = json::array();
            for (uint32_t i = 0; i < bodies.size(); ++i)
            {
                auto r = [](float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }; // mm: compact and enough
                json b = {{"name", dragdoll_character_get_body_name(c, i)},
                          {"p", {r(bodies[i].position.x), r(bodies[i].position.y), r(bodies[i].position.z)}}};
                if (i < motion.speed.size())
                {
                    b["v"] = r(motion.speed[i]);
                    b["w"] = std::round(static_cast<double>(motion.spin[i]) * 10.0) / 10.0;
                }
                list.push_back(b);
            }
            state["bodies"] = list;
        }
        if (!replaying && app.main.scenario != nullptr)
        {
            std::vector<std::string> lines;
            app.main.scenario->hud(world, lines);
            state["hud"] = lines;
        }
        return ok(state);
    }
    if (cmd == "push")
    {
        std::string body;
        DdVec3 impulse{};
        in >> body >> impulse.x >> impulse.y >> impulse.z;
        DdCharacter* c = app.main.world != nullptr ? dragdoll_world_get_character(app.main.world, 0) : nullptr;
        if (replaying || c == nullptr) return bad("no live character");
        const int32_t index = dragdoll_character_find_body(c, body.c_str());
        if (index < 0 || in.fail()) return bad("usage: push <body> <x> <y> <z>");
        std::vector<DdTransform> bodies(dragdoll_character_get_body_count(c));
        dragdoll_character_get_body_transforms(c, 0, bodies.data(), static_cast<uint32_t>(bodies.size()));
        const DdVec3 point = bodies[static_cast<size_t>(index)].position;
        dragdoll_character_apply_impulse(c, static_cast<uint32_t>(index), impulse, &point);
        app.pushes.push_back({dragdoll_world_get_step_count(app.main.world), static_cast<uint32_t>(index), impulse, point});
        return ok({{"body", body}});
    }
    if (cmd == "note")
    {
        std::string text;
        std::getline(in >> std::ws, text);
        if (app.main.world == nullptr || !dragdoll_world_is_recording(app.main.world)) return bad("not recording (start the viewer with --record)");
        dragdoll_world_record_note(app.main.world, text.c_str());
        return ok();
    }
    if (cmd == "shot")
    {
        std::string path;
        std::getline(in >> std::ws, path);
        if (path.empty()) return bad("usage: shot <file.png>");
        app.notePrevPaused = app.paused; // The screenshot code restores this pause state
        app.pendingShot = path;
        return ok({{"shot", path}, {"note", "saved at the end of this frame"}});
    }
    return bad("unknown command: " + cmd + " (try help)");
}

void drawPanels(App& app, BodyMotion& motion)
{
    drawRigPanel(app);
    const float w = static_cast<float>(GetScreenWidth());
    const bool replaying = app.replay != nullptr;

    ImGui::SetNextWindowPos(ImVec2(w - 290.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Playback"))
    {
        ImGui::Checkbox("Paused (Space)", &app.paused);
        ImGui::SameLine();
        if (ImGui::Button("Step (N)"))
        {
            app.paused = true;
            if (replaying) advanceReplay(app);
            else if (app.main.world != nullptr) advanceHostFrame(app, 1.0f / 120.0f);
        }
        if (ImGui::Button("Restart (R)"))
        {
            if (replaying) openReplay(app, false);
            else loadScenario(app, app.scenarioIndex, false);
        }
        ImGui::SliderFloat("Time scale", &app.timeScale, 0.0625f, 4.0f, "%.3fx", ImGuiSliderFlags_Logarithmic);
        if (!replaying)
        {
            int mode = static_cast<int>(app.hostMode);
            const char* preview = hostModeName(app.hostMode);
            if (ImGui::BeginCombo("Host rate", preview))
            {
                for (int i = 0; i < static_cast<int>(HostMode::Count); ++i)
                {
                    if (ImGui::Selectable(hostModeName(static_cast<HostMode>(i)), i == mode)) app.hostMode = static_cast<HostMode>(i);
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox("Auto-restart (A)", &app.autoRestart);
        ImGui::Checkbox("Joint limits (L)", &app.showLimits);
        ImGui::Checkbox("Help line (H)", &app.showHelp);
        ImGui::SeparatorText("Library");
        if (app.hotEnabled)
        {
            ImGui::TextDisabled("hot reload on: rebuild to swap (#%d loaded)", hot::generation());
            if (!app.hotStatus.empty()) ImGui::TextWrapped("%s", app.hotStatus.c_str());
        }
        else
        {
            ImGui::TextWrapped("hot reload off: %s", app.hotStatus.c_str());
        }
        if (!app.commandStatus.empty()) ImGui::TextDisabled("%s", app.commandStatus.c_str());
        if (!replaying)
        {
            ImGui::SeparatorText("Push (right-click a body)");
            ImGui::SliderFloat("Impulse", &app.pushStrength, 5.0f, 300.0f, "%.0f N*s");
            ImGui::TextDisabled("70 N*s = the shove scenario");
            if (!app.lastPush.empty()) ImGui::TextWrapped("%s", app.lastPush.c_str());
        }
    }
    ImGui::End();

    if (!replaying)
    {
        ImGui::SetNextWindowPos(ImVec2(w - 290.0f, 300.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280.0f, 220.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Scenarios (Tab)"))
        {
            for (size_t i = 0; i < app.names.size(); ++i)
            {
                if (ImGui::Selectable(app.names[i].c_str(), i == app.scenarioIndex)) loadScenario(app, i, false);
            }
        }
        ImGui::End();
    }

    measureMotion(activeWorld(app), motion);
    DdWorld* world = activeWorld(app);
    DdCharacter* c = world != nullptr ? dragdoll_world_get_character(world, 0) : nullptr;
    if (c != nullptr && motion.previous.size() == motion.speed.size())
    {
        ImGui::SetNextWindowPos(ImVec2(w - 290.0f, 530.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Bodies"))
        {
            ImGui::TextDisabled("speed between rendered steps");
            if (ImGui::BeginTable("bodies", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("body");
                ImGui::TableSetupColumn("m/s");
                ImGui::TableSetupColumn("deg/s");
                ImGui::TableHeadersRow();
                for (uint32_t i = 0; i < motion.speed.size(); ++i)
                {
                    // Highlight the "moving while the rest is still" pattern the founder spotted.
                    const bool spinning = motion.spin[i] > 30.0f && motion.speed[i] < 0.02f;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(dragdoll_character_get_body_name(c, i));
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", motion.speed[i]);
                    ImGui::TableNextColumn();
                    if (spinning) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.35f, 1.0f), "%.1f", motion.spin[i]);
                    else ImGui::Text("%.1f", motion.spin[i]);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}
} // namespace

int main(int argc, char** argv)
{
    std::string start = "car_vs_pole_120kmh";
    std::string recordPath, replayPath, rigPath, dllPath;
    int port = commands::kDefaultPort;
    HostMode initialMode = HostMode::Real;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--list") == 0)
        {
            for (const std::string& n : dragdoll::scenarios::list())
            {
                std::printf("%s\n", n.c_str());
            }
            return 0;
        }
        if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
        {
            const int fps = std::atoi(argv[++i]);
            initialMode = fps == 30 ? HostMode::Fps30 : fps == 60 ? HostMode::Fps60 : fps == 144 ? HostMode::Fps144 : HostMode::Real;
        }
        else if (std::strcmp(argv[i], "--record") == 0 && i + 1 < argc)
        {
            recordPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--dll") == 0 && i + 1 < argc)
        {
            dllPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--rig") == 0 && i + 1 < argc)
        {
            rigPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
        {
            replayPath = argv[++i];
        }
        else if (argv[i][0] != '-')
        {
            start = argv[i];
        }
    }

    std::string hotError;
    const bool hotEnabled = hot::start(dllPath, hotError); // Must precede the first dragdoll_* call
    if (!dllPath.empty() && !hotEnabled)
    {
        std::fprintf(stderr, "cannot use --dll %s: %s\n", dllPath.c_str(), hotError.c_str());
        return 1; // Never silently replay with a different library
    }
    if (dragdoll_init(DD_API_VERSION) != DD_OK)
    {
        std::fprintf(stderr, "dragdoll_init failed: %s\n", dragdoll_last_error_message());
        return 1;
    }

    App app;
    app.names = dragdoll::scenarios::list();
    app.hostMode = initialMode;
    app.recordPath = recordPath;
    // Recording: a restart would start a new recording. Replay: stop on the last frame so it can be inspected.
    app.autoRestart = recordPath.empty() && replayPath.empty();
    app.replayPath = replayPath;
    app.rigPath = rigPath;
    app.hotEnabled = hotEnabled;
    app.hotStatus = hotError;
    size_t index = 0;
    for (size_t i = 0; i < app.names.size(); ++i)
    {
        if (app.names[i] == start)
        {
            index = i;
        }
    }

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_UNFOCUSED);
    InitWindow(1280, 720, "DragDoll live viewer");
    rlImGuiSetup(true);
    {
        std::string error;
        app.commandStatus = port <= 0 ? "commands: off"
                            : commands::start(port, error) ? "commands: 127.0.0.1:" + std::to_string(port) + " (dragdoll_ctl)"
                                                             : "commands: off, " + error;
    }
    ImGui::GetIO().IniFilename = nullptr; // No imgui.ini next to the exe
    SetExitKey(KEY_NULL); // Esc quits unless a note is being typed (see the loop condition)

    if (!app.rigPath.empty())
    {
        loadRigFile(app); // Installs the file's rig for the scenarios (the scenario is (re)loaded below)
    }
    if (!app.replayPath.empty())
    {
        openReplay(app, true);
    }
    else if (!loadScenario(app, index, true))
    {
        std::fprintf(stderr, "failed to load scenario: %s\n", app.error.c_str());
    }

    const DdDebugDrawCallbacks callbacks{sizeof(DdDebugDrawCallbacks), nullptr, &onLine, &onTriangle};
    Geometry geometry;
    Geometry overlay; // Joint limits, drawn translucent on top
    BodyMotion motion;
    while (!WindowShouldClose() && !(IsKeyPressed(KEY_ESCAPE) && !app.noteEditing))
    {
        watchRigFile(app);
        watchLibrary(app);
        commands::poll([&](const std::string& line) { return handleCommand(app, motion, line); });
        // --- input ---
        if (keyPressed(app, KEY_SPACE)) app.paused = !app.paused;
        if (keyPressed(app, KEY_H)) app.showHelp = !app.showHelp;
        if (keyPressed(app, KEY_A)) app.autoRestart = !app.autoRestart;
        if (keyPressed(app, KEY_L)) app.showLimits = !app.showLimits;
        if (keyPressed(app, KEY_R) && app.replay != nullptr) openReplay(app, false);
        else if (keyPressed(app, KEY_R)) loadScenario(app, app.scenarioIndex, false); // restart the animation, keep the camera
        if (keyPressed(app, KEY_TAB) && app.replay == nullptr) loadScenario(app, app.scenarioIndex + (IsKeyDown(KEY_LEFT_SHIFT) ? app.names.size() - 1 : 1), false); // keep the camera
        if (keyPressed(app, KEY_F) && app.replay != nullptr) focusCharacter(app);
        else if (keyPressed(app, KEY_F)) app.hostMode = static_cast<HostMode>((static_cast<int>(app.hostMode) + 1) % static_cast<int>(HostMode::Count));
        if (keyPressed(app, KEY_UP)) app.timeScale = std::min(app.timeScale * 2.0f, 4.0f);
        if (keyPressed(app, KEY_DOWN)) app.timeScale = std::max(app.timeScale * 0.5f, 0.0625f);
        if (keyPressed(app, KEY_N) && app.replay != nullptr)
        {
            app.paused = true;
            advanceReplay(app); // Exactly one recorded host frame.
        }
        else if (keyPressed(app, KEY_N) && app.main.world != nullptr)
        {
            app.paused = true;
            advanceHostFrame(app, 1.0f / 120.0f); // Exactly one fixed step.
        }
        if (keyPressed(app, KEY_F9) && app.main.world != nullptr && dragdoll_world_is_recording(app.main.world))
        {
            beginNote(app);
        }
        if (app.noteEditing)
        {
            editNote(app);
        }
        if (!ImGui::GetIO().WantCaptureMouse)
        {
            app.camera.update();
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && app.replay == nullptr && !app.noteEditing)
            {
                pushBodyUnderMouse(app);
            }
        }

        // --- simulate ---
        if (app.replay != nullptr)
        {
            updateReplay(app, GetFrameTime());
            if (app.replayFocusPending && focusCharacter(app))
            {
                app.replayFocusPending = false;
            }
        }
        else if (app.main.world != nullptr)
        {
            update(app, GetFrameTime());
        }

        // --- collect + draw ---
        geometry.tris.clear();
        geometry.lines.clear();
        overlay.tris.clear();
        overlay.lines.clear();
        DdResult drawResult = DD_ERROR_NOT_INITIALIZED;
        if (DdWorld* world = activeWorld(app))
        {
            DdDebugDrawCallbacks cb = callbacks;
            cb.user = &geometry;
            cb.flags = DD_DRAW_SHAPES;
            drawResult = dragdoll_world_debug_draw(world, &cb);
            if (app.showLimits && drawResult == DD_OK)
            {
                cb.user = &overlay;
                cb.flags = DD_DRAW_CONSTRAINT_LIMITS;
                dragdoll_world_debug_draw(world, &cb);
            }
        }

        BeginDrawing();
        ClearBackground(Color{22, 26, 34, 255});
        BeginMode3D(app.camera.build());
        drawGrid(app.camera);
        const Camera3D view = app.camera.build();
        const bool cameraBelowFloor = Vector3DotProduct(view.position, app.camera.up) < 0.0f;
        drawGeometry(geometry, 255, cameraBelowFloor ? &app.camera.up : nullptr);
        if (!overlay.tris.empty() || !overlay.lines.empty())
        {
            rlDrawRenderBatchActive();
            rlDisableDepthMask(); // Translucent: don't hide what is behind
            drawGeometry(overlay, 128);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
        }
        EndMode3D();
        if (drawResult == DD_ERROR_UNSUPPORTED)
        {
            DrawText("This DragDoll build has no debug renderer (use a dev preset).", 14, 300, 20, RED);
        }
        if (app.replay != nullptr)
        {
            drawReplayHud(app);
        }
        else if (app.main.world != nullptr)
        {
            drawHud(app);
        }
        else if (!app.error.empty())
        {
            DrawText(app.error.c_str(), 14, 14, 20, RED);
        }
        if (!app.pendingShot.empty())
        {
            // Taken before the note box is drawn, so the picture shows the scene exactly as annotated.
            Image shot = LoadImageFromScreen();
            if (!ExportImage(shot, app.pendingShot.c_str()))
            {
                app.error = "could not save screenshot " + app.pendingShot;
            }
            UnloadImage(shot);
            app.pendingShot.clear();
            app.paused = app.notePrevPaused;
        }
        rlImGuiBegin();
        drawPanels(app, motion);
        rlImGuiEnd();
        if (app.noteEditing)
        {
            drawNoteBox(app);
        }
        EndDrawing();
    }

    commands::stop();
    rlImGuiShutdown();
    CloseWindow();
    dragdoll_replay_close(app.replay);
    app.main = Instance();
    app.shadow = Instance();
    dragdoll_shutdown();
    return 0;
}
