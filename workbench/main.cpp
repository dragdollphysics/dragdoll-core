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

#include <imgui.h>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    const Vector3 lift = Vector3Scale(cam.up, 0.004f);
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

    // F9 annotation (record mode): the sim pauses, the user types a note, Enter saves it plus a screenshot.
    bool noteEditing = false;
    std::string noteText;
    bool notePrevPaused = false;
    std::string pendingShot; // Full path of the screenshot to take at the end of this frame
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
    }
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
        DrawText("Space pause | N next frame | Up/Down time scale | R restart | A auto-restart | L joint limits | H hide help | left-drag orbit | middle-drag pan | wheel zoom",
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
    if (finished)
    {
        if (!app.autoRestart)
        {
            return; // Hold the final state (press A to turn auto-restart back on).
        }
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
        DrawText("Space pause | N step | Up/Down time scale | F host frame rate | R restart | A auto-restart | L joint limits | Tab next | F9 note (when recording) | H hide help | left-drag orbit | middle-drag pan | wheel zoom",
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

void drawPanels(App& app, BodyMotion& motion)
{
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
    std::string recordPath, replayPath;
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
        else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
        {
            replayPath = argv[++i];
        }
        else if (argv[i][0] != '-')
        {
            start = argv[i];
        }
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
    ImGui::GetIO().IniFilename = nullptr; // No imgui.ini next to the exe
    SetExitKey(KEY_NULL); // Esc quits unless a note is being typed (see the loop condition)

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
        // --- input ---
        if (keyPressed(app, KEY_SPACE)) app.paused = !app.paused;
        if (keyPressed(app, KEY_H)) app.showHelp = !app.showHelp;
        if (keyPressed(app, KEY_A)) app.autoRestart = !app.autoRestart;
        if (keyPressed(app, KEY_L)) app.showLimits = !app.showLimits;
        if (keyPressed(app, KEY_R) && app.replay != nullptr) openReplay(app, false);
        else if (keyPressed(app, KEY_R)) loadScenario(app, app.scenarioIndex, false); // restart the animation, keep the camera
        if (keyPressed(app, KEY_TAB) && app.replay == nullptr) loadScenario(app, app.scenarioIndex + (IsKeyDown(KEY_LEFT_SHIFT) ? app.names.size() - 1 : 1), false); // keep the camera
        if (keyPressed(app, KEY_F)) app.hostMode = static_cast<HostMode>((static_cast<int>(app.hostMode) + 1) % static_cast<int>(HostMode::Count));
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
        if (!ImGui::GetIO().WantCaptureMouse) app.camera.update();

        // --- simulate ---
        if (app.replay != nullptr)
        {
            updateReplay(app, GetFrameTime());
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

    rlImGuiShutdown();
    CloseWindow();
    dragdoll_replay_close(app.replay);
    app.main = Instance();
    app.shadow = Instance();
    dragdoll_shutdown();
    return 0;
}
