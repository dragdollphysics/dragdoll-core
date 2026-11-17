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
#include "live_client.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <rlImGui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <ctime>
#include <deque>
#include <map>
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

    // Live "Game camera" mode: the view comes from the host instead of the orbit.
    bool hasOverride = false;
    Camera3D overrideCam{};

    Camera3D build() const
    {
        if (hasOverride) return overrideCam;
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

void drawGrid(const OrbitCamera& cam, float height)
{
    Vector3 e1, e2;
    horizontalBasis(cam.up, e1, e2);
    const float snap = 5.0f;
    const Vector3 centre = Vector3Add(Vector3Scale(e1, std::round(Vector3DotProduct(cam.target, e1) / snap) * snap),
                                      Vector3Scale(e2, std::round(Vector3DotProduct(cam.target, e2) / snap) * snap));
    const Vector3 lift = Vector3Add(centre, Vector3Scale(cam.up, height + 0.004f));
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
float gFloorHeight = 0.0f; // Height of the floor plane along up (game sessions sit far above 0)

bool belowFloor(const Vector3& v, const Vector3& up)
{
    return Vector3DotProduct(v, up) <= gFloorHeight + 0.001f;
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
    rlDrawRenderBatchActive(); // Draw the batch now: culling must still be off when it is flushed (both sides visible)
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
    struct TensionChange
    {
        uint64_t step;
        DdTension tension;
        float blend;
    };
    std::vector<TensionChange> tensionChanges; // Same, for tension set from the viewer or dragdoll_ctl
    float tensionBlend = 0.3f;
    int tensionOverride = -1; // Picked in the viewer: re-applied after every (re)load; -1 = the scenario's own
    std::vector<DdTransform> restPose; // Character 0 as created (reference for the pose error)

    // Hot reload of dragdoll.dll (task 2.4)
    bool hotEnabled = false;
    std::string commandStatus; // Command channel (task 2.7)

    // Live tap (bridge task 3.7): --attach <port> mirrors a world streamed by a running host (e.g. the SA game).
    int attachPort = 0;
    DdWorld* live = nullptr;                       // Mirror world: rebuilt from "scene", posed from "frame"
    std::map<uint32_t, DdColliderId> liveColliders; // Host collider id -> mirror id
    std::vector<DdCharacter*> liveCharacters;      // In host id order
    std::map<uint32_t, DdCharacter*> liveById;     // Host character id -> mirror character
    struct LiveCharacter
    {
        uint32_t id;
        std::string name;
        bool visible;
        int tension = 0;
        bool balance = false;
    };
    std::vector<LiveCharacter> liveList;           // For the panel (from scene + frames)
    // Smooth live motion: frames are buffered with the host's simulation time and shown ~50 ms in the past,
    // interpolated, so uneven host frames (e.g. 33/47 ms) do not look choppy.
    struct LivePose
    {
        DdTransform root;
        std::vector<DdQuat> joints;
    };
    struct LiveFrame
    {
        double time = 0.0; // Host simulation seconds (step / 120)
        std::map<uint32_t, LivePose> characters;
        std::map<uint32_t, DdTransform> colliders; // Moving proxies (cars), by host id
        bool hasCamera = false;
        Vector3 cameraPosition{}, cameraForward{}, cameraUp{};
        float cameraFov = 60.0f;
    };
    std::deque<LiveFrame> liveBuffer;

    // Capture (founder -> Claude): everything the host streams, with arrival times, plus a note on what to look at.
    nlohmann::json sceneWorld;                         // The host's scene, kept current by "scene" + "delta"
    std::map<uint32_t, nlohmann::json> sceneColliders;  // messages (host id -> description). A capture starts with
    std::map<uint32_t, nlohmann::json> sceneCharacters; // it, so it stands on its own
    std::ofstream capture;
    std::string capturePath, lastCapture;
    double captureStart = 0.0;
    uint64_t captureLines = 0;
    bool askCaptureNote = false;
    char captureNote[2048] = {};
    double liveClock = -1.0;
    int cameraMode = 0;                            // Live view: 0 free, 1 game camera, 2 follow CJ
    bool controlling = false;                      // Game camera mode: keyboard + mouse go to the game (Esc releases)
    bool controlSkipLeft = false;                  // The click that took control is not a shot
    float mouseSensitivity = 0.3f;                 // Remote control: viewer pixels -> game mouse units
    bool mouseInvertY = true;                      // The game's vertical mouse axis is the opposite of the screen's
    bool hasHostCamera = false;
    Camera3D hostCamera{};
    float liveFloor = 0.0f;                        // Floor height for the grid (top of the floor proxy)
    bool liveHasFloor = false;
    float lowestSeen = 1e9f;                       // Replays: lowest body height seen so far
    uint64_t liveWorldId = 0;
    bool liveConnected = false;
    double liveLostAt = -1.0;                     // When the link dropped (-1: connected)
    bool liveFocusPending = false;
    uint64_t liveStep = 0, liveFrames = 0;
    std::vector<std::string> liveStatus;          // Per character, for the HUD
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

// While a note is being typed, or the game is being controlled, hotkeys are off.
bool keyPressed(const App& app, int key)
{
    return !app.noteEditing && !app.controlling && !ImGui::GetIO().WantCaptureKeyboard && IsKeyPressed(key);
}

// Remote control (game camera mode): the viewer's keyboard and mouse, sent to the host as
// "input <dx> <dy> <buttons> <virtual-key codes...>" every frame. Key codes are Windows virtual keys, so the host needs
// no knowledge of the viewer's window library.
void sendRemoteInput(App& app, bool release)
{
    std::string data = "input 0 0 0";
    if (!release)
    {
        static const std::pair<int, int> kKeys[] = {
            {KEY_SPACE, 0x20}, {KEY_ENTER, 0x0D}, {KEY_TAB, 0x09}, {KEY_BACKSPACE, 0x08}, {KEY_LEFT_SHIFT, 0xA0},
            {KEY_RIGHT_SHIFT, 0xA1}, {KEY_LEFT_CONTROL, 0xA2}, {KEY_RIGHT_CONTROL, 0xA3}, {KEY_LEFT_ALT, 0xA4},
            {KEY_CAPS_LOCK, 0x14}, {KEY_LEFT, 0x25}, {KEY_UP, 0x26}, {KEY_RIGHT, 0x27}, {KEY_DOWN, 0x28}};
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) app.controlSkipLeft = false;
        const Vector2 d = GetMouseDelta();
        const int buttons = (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !app.controlSkipLeft ? 1 : 0) |
                            (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 2 : 0) | (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ? 4 : 0);
        char head[64];
        const float dy = app.mouseInvertY ? -d.y : d.y;
        std::snprintf(head, sizeof(head), "input %.2f %.2f %d", d.x * app.mouseSensitivity, dy * app.mouseSensitivity, buttons);
        data = head;
        for (int k = KEY_A; k <= KEY_Z; ++k) // Letters and digits share their codes with Windows virtual keys
            if (IsKeyDown(k)) data += " " + std::to_string(k);
        for (int k = KEY_ZERO; k <= KEY_NINE; ++k)
            if (IsKeyDown(k)) data += " " + std::to_string(k);
        for (int f = 0; f < 12; ++f)
            if (IsKeyDown(KEY_F1 + f)) data += " " + std::to_string(0x70 + f);
        for (const auto& [key, vk] : kKeys)
            if (IsKeyDown(key)) data += " " + std::to_string(vk);
    }
    live::send(nlohmann::json{{"t", "host"}, {"data", data}}.dump());
}

void setControlling(App& app, bool on)
{
    if (app.controlling == on) return;
    app.controlling = on;
    if (on)
    {
        app.controlSkipLeft = true;
        DisableCursor();
    }
    else
    {
        sendRemoteInput(app, true);
        EnableCursor();
    }
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

// Perception (ADR-0024): for every character with a threat, the probed front silhouette of that object (drawn on the
// vehicle) and one line of text: time to collision, expected trajectory, measured front.
void collectThreats(DdWorld* world, std::vector<Line>& lines, std::vector<std::string>& text)
{
    static const char* const kClass[] = {"?", "wrap", "forward projection", "fender vault", "roof vault", "somersault"};
    const uint32_t count = dragdoll_world_get_character_count(world);
    // Senses of the first simulated character (ADR-0024 §3): what it touches, airborne and landing, how hard it was hit.
    for (uint32_t i = 0; i < count; ++i)
    {
        DdCharacter* c = dragdoll_world_get_character(world, i);
        DdSenses s{};
        s.struct_size = sizeof(DdSenses);
        if (dragdoll_character_get_kinematic(c) != 0 || dragdoll_character_get_senses(c, &s) != DD_OK) continue;
        char where[96];
        if (s.airborne != 0 && s.landing_seconds >= 0.0f && s.landing_body >= 0)
        {
            std::snprintf(where, sizeof(where), "airborne %.2f s, lands in %.2f s on %s", s.airborne_seconds, s.landing_seconds,
                          dragdoll_character_get_body_name(c, static_cast<uint32_t>(s.landing_body)));
        }
        else if (s.airborne != 0)
        {
            std::snprintf(where, sizeof(where), "airborne %.2f s", s.airborne_seconds);
        }
        else
        {
            std::snprintf(where, sizeof(where), "%u bodies on the ground, %u on a vehicle", s.ground_bodies, s.object_bodies);
        }
        char line[220];
        std::snprintf(line, sizeof(line), "senses: %s | ground %.2f m below | upright %.2f | hardest hit: head %.1f m/s, body %.1f m/s", where,
                      s.ground_height, s.upright, s.peak_head_delta_v, s.peak_impact_delta_v);
        text.emplace_back(line);
        DdImpactReport r{};
        r.struct_size = sizeof(DdImpactReport);
        if (dragdoll_character_get_impact_report(c, &r) == DD_OK && r.valid != 0)
        {
            static const char* const kZone[] = {"-", "front", "leading edge", "bonnet", "windscreen", "roof"};
            static const char* const kClass[] = {"...", "wrap", "forward projection", "fender vault", "roof vault", "somersault"};
            char head[96] = "no head strike";
            if (r.head_strike_time >= 0.0f)
            {
                std::snprintf(head, sizeof(head), "head on the %s at %.0f ms, wrap-around %.2f m", kZone[std::clamp(static_cast<int>(r.head_strike_zone), 0, 5)],
                              r.head_strike_time * 1000.0f, r.wrap_around_distance);
            }
            char report[260];
            if (r.finished != 0)
            {
                std::snprintf(report, sizeof(report), "impact: %s | %s | launch %.0f km/h | throw %.1f m (carry %.1f + flight/slide %.1f; expected %.1f-%.1f %s)",
                              kClass[std::clamp(static_cast<int>(r.measured), 0, 5)], head, r.launch_speed * 3.6f, r.throw_distance, r.carry_distance,
                              r.flight_slide_distance, r.corridor_min, r.corridor_max, r.in_corridor != 0 ? "- inside" : "- OUTSIDE");
            }
            else
            {
                std::snprintf(report, sizeof(report), "impact: %s | %s | first contact on the %s", kClass[std::clamp(static_cast<int>(r.measured), 0, 5)], head,
                              kZone[std::clamp(static_cast<int>(r.first_zone), 0, 5)]);
            }
            text.emplace_back(report);
        }
        break;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        DdCharacter* c = dragdoll_world_get_character(world, i);
        DdThreat threat{};
        threat.struct_size = sizeof(DdThreat);
        if (dragdoll_character_get_threat(c, &threat) != DD_OK || threat.collider == DD_INVALID_COLLIDER) continue;
        DdVec3 points[160];
        const uint32_t n = std::min<uint32_t>(dragdoll_character_get_threat_silhouette(c, points, 160), 160);
        for (uint32_t k = 0; k + 1 < n; ++k) lines.push_back(Line{toRay(points[k]), toRay(points[k + 1]), Color{255, 210, 90, 255}});
        if (text.size() >= 4) continue; // A crowd: the silhouettes are enough
        const char* name = dragdoll_character_get_name(c);
        char line[220];
        std::snprintf(line, sizeof(line), "%s%sthreat in %.2f s at %.0f km/h | expects %s | front: bumper %.2f m, leading edge %.2f m, bonnet %.1f m",
                      name[0] != 0 ? name : "", name[0] != 0 ? ": " : "", threat.time_to_collision, threat.closing_speed * 3.6f,
                      kClass[std::min(static_cast<int>(threat.predicted), 5)], threat.profile.front_top_height,
                      threat.profile.leading_edge_height, threat.profile.bonnet_length);
        std::string shown = line;
        if (dragdoll_character_get_behaviour(c)[0] != 0) // What the behaviour makes of it (its stage)
        {
            shown += std::string(" | ") + dragdoll_character_get_behaviour(c) + ": " + dragdoll_character_get_behaviour_status(c);
        }
        text.emplace_back(shown);
    }
}

DdWorld* activeWorld(App& app)
{
    if (app.attachPort > 0) return app.live;
    return app.replay != nullptr ? dragdoll_replay_get_world(app.replay) : app.main.world;
}

bool loadScenario(App& app, size_t index, bool resetCamera)
{
    app.scenarioIndex = index % app.names.size();
    app.error.clear();
    app.pushes.clear();
    app.lastPush.clear();
    app.tensionChanges.clear();
    app.restPose.clear();
    app.hostAccumulator = 0.0;
    app.lastHostDt = 0.0f;
    app.lastSteps = 0;
    if (!Instance::make(app.names[app.scenarioIndex], app.main, app.error, app.recordPath.empty() ? nullptr : app.recordPath.c_str()))
    {
        return false;
    }
    if (DdCharacter* c = app.main.scenario->character())
    {
        app.restPose.resize(dragdoll_character_get_body_count(c));
        dragdoll_character_get_body_transforms(c, 0, app.restPose.data(), static_cast<uint32_t>(app.restPose.size()));
    }
    DdCharacter* mainCharacter = app.main.scenario->character();
    if (mainCharacter == nullptr) mainCharacter = dragdoll_world_get_character(app.main.world, 0);
    if (app.tensionOverride >= 0 && mainCharacter != nullptr &&
        dragdoll_character_set_tension(mainCharacter, static_cast<DdTension>(app.tensionOverride), 0.0f) == DD_OK)
    {
        app.tensionChanges.push_back({0, static_cast<DdTension>(app.tensionOverride), 0.0f}); // Restored after a hot reload too
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
    app.lowestSeen = 1e9f;
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
    DdWorld* world = activeWorld(app);
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
    const std::vector<App::TensionChange> tensions = app.tensionChanges;
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
        size_t next = 0, nextTension = 0;
        auto applyDue = [&](uint64_t now) {
            DdCharacter* c = dragdoll_world_get_character(app.main.world, 0);
            for (; next < pushes.size() && pushes[next].step <= now; ++next)
            {
                if (c != nullptr) dragdoll_character_apply_impulse(c, pushes[next].body, pushes[next].impulse, &pushes[next].point);
            }
            for (; nextTension < tensions.size() && tensions[nextTension].step <= now; ++nextTension)
            {
                if (c != nullptr) dragdoll_character_set_tension(c, tensions[nextTension].tension, tensions[nextTension].blend);
            }
        };
        for (uint64_t guard = 0; dragdoll_world_get_step_count(app.main.world) < target && guard < target * 2 + 16; ++guard)
        {
            applyDue(dragdoll_world_get_step_count(app.main.world));
            advanceHostFrame(app, 1.0f / 120.0f);
        }
        applyDue(dragdoll_world_get_step_count(app.main.world));
        app.pushes = pushes;
        app.tensionChanges = tensions;
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
const char* const kTensionNames[] = {"limp", "relaxed", "tense", "stunned"};

// Sets the tension of the live character (logged for hot-reload restore). false if there is none.
bool setLiveTension(App& app, DdTension tension, float blend)
{
    DdCharacter* c = nullptr;
    if (app.main.world != nullptr && app.replay == nullptr)
    {
        c = app.main.scenario->character() != nullptr ? app.main.scenario->character() : dragdoll_world_get_character(app.main.world, 0);
    }
    if (c == nullptr || dragdoll_character_set_tension(c, tension, blend) != DD_OK)
    {
        return false;
    }
    app.tensionChanges.push_back({dragdoll_world_get_step_count(app.main.world), tension, blend});
    app.tensionOverride = static_cast<int>(tension); // Sticky: survives restarts until "scenario" is picked
    return true;
}

std::string handleCommand(App& app, BodyMotion& motion, const std::string& line)
{
    using nlohmann::json;
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    auto ok = [](json extra = json::object()) { extra["ok"] = true; return extra.dump(); };
    auto bad = [](const std::string& why) { return json{{"ok", false}, {"error", why}}.dump(); };
    const bool replaying = app.replay != nullptr || app.attachPort > 0;

    if (cmd == "help" || cmd.empty())
    {
        return ok({{"commands", "list | load <scenario> | restart | pause | resume | step [n] | timescale <x> | "
                                "autorestart on|off | state | push <body> <x> <y> <z> (N*s) | tension <limp|relaxed|tense|stunned|default> [blend s] (sticky across restarts) | balance on|off | note <text> | shot <file.png>"}});
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
        json state = {{"mode", app.attachPort > 0 ? "live" : replaying ? "replay" : "scenario"},
                      {"name", app.attachPort > 0 ? "127.0.0.1:" + std::to_string(app.attachPort) : replaying ? app.replayPath : app.names[app.scenarioIndex]},
                      {"step", step}, {"time", static_cast<double>(step) / 120.0}, {"paused", app.paused},
                      {"library", hot::generation()}, // Live-reload copy in use: scripts wait for it to change
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
            state["tension"] = kTensionNames[dragdoll_character_get_tension(c)];
            if (!replaying && app.restPose.size() == bodies.size())
            {
                const dragdoll::scenarios::metrics::PoseError e = dragdoll::scenarios::metrics::poseError(c, app.restPose, bodies);
                state["pose_error"] = {{"rms", std::round(e.rms * 10.0) / 10.0}, {"max", std::round(e.max * 10.0) / 10.0},
                                       {"worst", e.worst >= 0 ? dragdoll_character_get_body_name(c, static_cast<uint32_t>(e.worst)) : ""}};
            }
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
    if (cmd == "tension")
    {
        std::string name;
        float blend = app.tensionBlend;
        in >> name;
        if (!(in >> blend)) blend = app.tensionBlend;
        for (int t = 0; t < DD_TENSION_COUNT; ++t)
        {
            if (name == kTensionNames[t])
            {
                if (!setLiveTension(app, static_cast<DdTension>(t), blend)) return bad("no live character");
                return ok({{"tension", name}, {"blend", blend}});
            }
        }
        if (name == "default")
        {
            app.tensionOverride = -1;
            return ok({{"tension", "scenario default from the next restart"}});
        }
        return bad("usage: tension limp|relaxed|tense|stunned|default [blend seconds]");
    }
    if (cmd == "balance")
    {
        std::string v;
        in >> v;
        DdCharacter* c = app.main.world != nullptr && app.replay == nullptr
                             ? (app.main.scenario->character() != nullptr ? app.main.scenario->character() : dragdoll_world_get_character(app.main.world, 0))
                             : nullptr;
        if (c == nullptr) return bad("no live character");
        if (dragdoll_character_set_balance(c, v == "on" ? 1 : 0) != DD_OK) return bad(dragdoll_last_error_message());
        return ok({{"balance", v == "on"}});
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

// ------------------------------------------------------------------------------------------------
// Live tap mirror (bridge task 3.7)
// ------------------------------------------------------------------------------------------------
DdVec3 jsonVec(const nlohmann::json& j)
{
    return DdVec3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
DdQuat jsonQuat(const nlohmann::json& j)
{
    return DdQuat{j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

DdColliderDesc liveColliderDesc(const nlohmann::json& c, std::vector<DdVec3>& vertices, std::vector<uint32_t>& indices)
{
    DdColliderDesc d;
    dragdoll_collider_desc_default(&d);
    d.shape = static_cast<DdShapeType>(c["shape"].get<int>());
    d.motion = c["motion"].get<int>() == DD_MOTION_STATIC ? DD_MOTION_STATIC : DD_MOTION_KINEMATIC; // Moved by frames
    d.transform.position = jsonVec(c["p"]);
    d.transform.rotation = jsonQuat(c["q"]);
    d.radius = c["radius"].get<float>();
    d.half_height = c["half_height"].get<float>();
    d.half_extents = jsonVec(c["half"]);
    d.group = c.value("group", 0u); // One object per group: the mirror perceives a car as the host does
    if (c.contains("vertices"))
    {
        const std::vector<float> flat = c["vertices"].get<std::vector<float>>();
        for (size_t k = 0; k + 2 < flat.size(); k += 3) vertices.push_back(DdVec3{flat[k], flat[k + 1], flat[k + 2]});
        d.vertices = vertices.data();
        d.vertex_count = static_cast<uint32_t>(vertices.size());
        if (c.contains("indices"))
        {
            indices = c["indices"].get<std::vector<uint32_t>>();
            d.indices = indices.data();
            d.index_count = static_cast<uint32_t>(indices.size());
        }
    }
    return d;
}

// The host's scene as last received ("scene", then "delta" messages). Captures start with it.
nlohmann::json composeLiveScene(const App& app)
{
    nlohmann::json colliders = nlohmann::json::array(), characters = nlohmann::json::array();
    for (const auto& [id, c] : app.sceneColliders) colliders.push_back(c);
    for (const auto& [id, c] : app.sceneCharacters) characters.push_back(c);
    return nlohmann::json{{"t", "scene"}, {"world", app.sceneWorld}, {"colliders", colliders}, {"characters", characters}};
}

void addLiveCollider(App& app, const nlohmann::json& c)
{
    const uint32_t hostId = c["id"].get<uint32_t>();
    if (app.liveColliders.count(hostId) != 0) return;
    std::vector<DdVec3> vertices;
    std::vector<uint32_t> indices;
    const DdColliderDesc d = liveColliderDesc(c, vertices, indices);
    DdColliderId id = DD_INVALID_COLLIDER;
    if (dragdoll_world_add_colliders(app.live, &d, 1, &id) == DD_OK) app.liveColliders[hostId] = id;
}

void removeLiveCollider(App& app, uint32_t hostId)
{
    auto it = app.liveColliders.find(hostId);
    if (it == app.liveColliders.end()) return;
    dragdoll_world_remove_colliders(app.live, &it->second, 1);
    app.liveColliders.erase(it);
}

void addLiveCharacter(App& app, const nlohmann::json& c)
{
    const uint32_t hostId = c["id"].get<uint32_t>();
    if (app.liveById.count(hostId) != 0) return;
    const std::string rig = c["rig"].get<std::string>();
    DdCharacterDesc d;
    dragdoll_character_desc_default(&d);
    d.rig_json = rig.c_str();
    d.transform.position = jsonVec(c["p"]);
    d.transform.rotation = jsonQuat(c["q"]);
    d.mass_scale = c["mass_scale"].get<float>();
    d.size_scale = c.value("size_scale", 1.0f); // The host's character proportions
    DdCharacter* ch = nullptr;
    if (dragdoll_character_create(app.live, &d, &ch) != DD_OK) return;
    // The mirror only shows the host's poses, which jump (new world, catching up after a hitch, a car at speed): a
    // simulated copy would read a jump as an explosion and fail (vanish). Kinematic copies accept any jump.
    dragdoll_character_set_kinematic(ch, 1);
    app.liveById[hostId] = ch;
}

void removeLiveCharacter(App& app, uint32_t hostId)
{
    auto it = app.liveById.find(hostId);
    if (it == app.liveById.end()) return;
    dragdoll_character_destroy(it->second);
    app.liveById.erase(it);
}

// After any scene change: the grid height and the characters panel (each entry keeps its "visible" choice).
void refreshLiveDerived(App& app)
{
    // The grid lies on the floor: the top of the largest static box (e.g. the SA mod's floor proxy).
    const Vector3 up = Vector3Normalize(toRay(jsonVec(app.sceneWorld["up"])));
    float bestArea = 0.0f;
    app.liveHasFloor = false;
    for (const auto& [id, c] : app.sceneColliders)
    {
        if (c["motion"].get<int>() != DD_MOTION_STATIC || c["shape"].get<int>() != DD_SHAPE_BOX) continue;
        const Vector3 half = toRay(jsonVec(c["half"]));
        const float alongUp = std::fabs(half.x * up.x) + std::fabs(half.y * up.y) + std::fabs(half.z * up.z);
        const float area = (half.x * half.y * half.z) / std::max(alongUp, 1e-3f);
        if (area > bestArea)
        {
            bestArea = area;
            app.liveFloor = Vector3DotProduct(toRay(jsonVec(c["p"])), up) + alongUp;
            app.liveHasFloor = true;
        }
    }
    std::map<uint32_t, bool> visible;
    for (const App::LiveCharacter& entry : app.liveList) visible[entry.id] = entry.visible;
    app.liveCharacters.clear();
    app.liveList.clear();
    for (const auto& [hostId, c] : app.sceneCharacters)
    {
        auto it = app.liveById.find(hostId);
        if (it == app.liveById.end()) continue;
        app.liveCharacters.push_back(it->second);
        const std::string name = c.value("name", std::string());
        app.liveList.push_back({hostId, name.empty() ? "character " + std::to_string(hostId) : name,
                                visible.count(hostId) != 0 ? visible[hostId] : true});
    }
}

// Applies a whole scene (sent when the viewer connects): a new host world (new id) is rebuilt; the same world is
// updated in place (only what changed), so reconnecting does not reset the view.
void rebuildLive(App& app, const nlohmann::json& scene)
{
    const uint64_t worldId = scene["world"].value("id", uint64_t{0});
    const bool sameWorld = app.live != nullptr && worldId == app.liveWorldId;
    if (!sameWorld)
    {
        const DdVec3 previousFocus{app.camera.target.x, app.camera.target.y, app.camera.target.z};
        const bool hadScene = app.live != nullptr;
        dragdoll_world_destroy(app.live);
        app.live = nullptr;
        app.liveColliders.clear();
        app.liveById.clear();
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = jsonVec(scene["world"]["up"]);
        desc.gravity = 0.0f; // The mirror only shows poses; it never simulates on its own
        if (dragdoll_world_create(&desc, &app.live) != DD_OK)
        {
            app.error = std::string("live mirror: ") + dragdoll_last_error_message();
            return;
        }
        app.liveWorldId = worldId;
        app.liveStep = 0;
        app.liveBuffer.clear();
        app.liveClock = -1.0;
        app.lowestSeen = 1e9f;
        app.camera.setUp(Vector3Normalize(toRay(desc.up))); // e.g. San Andreas is Z-up
        // Look at the new scene only if it is somewhere else (a new F7 drop next to the old one keeps the camera).
        app.liveFocusPending = !hadScene;
        if (hadScene && !scene["characters"].empty())
        {
            const DdVec3 p = jsonVec(scene["characters"][0]["p"]);
            const float dx = p.x - previousFocus.x, dy = p.y - previousFocus.y, dz = p.z - previousFocus.z;
            app.liveFocusPending = dx * dx + dy * dy + dz * dz > 15.0f * 15.0f;
        }
    }
    app.sceneWorld = scene["world"];
    app.sceneColliders.clear();
    app.sceneCharacters.clear();
    for (const nlohmann::json& c : scene["colliders"]) app.sceneColliders[c["id"].get<uint32_t>()] = c;
    for (const nlohmann::json& c : scene["characters"]) app.sceneCharacters[c["id"].get<uint32_t>()] = c;
    // Remove the gone, add the new.
    std::vector<uint32_t> gone;
    for (const auto& [hostId, id] : app.liveColliders)
        if (app.sceneColliders.count(hostId) == 0) gone.push_back(hostId);
    for (uint32_t hostId : gone) removeLiveCollider(app, hostId);
    for (const auto& [hostId, c] : app.sceneColliders) addLiveCollider(app, c);
    gone.clear();
    for (const auto& [hostId, ch] : app.liveById)
        if (app.sceneCharacters.count(hostId) == 0) gone.push_back(hostId);
    for (uint32_t hostId : gone) removeLiveCharacter(app, hostId);
    for (const auto& [hostId, c] : app.sceneCharacters) addLiveCharacter(app, c);
    refreshLiveDerived(app);
}

// Applies what changed since the last scene or delta: removals first, then additions (adding a known id updates its
// description, e.g. a renamed character).
void applyLiveDelta(App& app, const nlohmann::json& delta)
{
    if (app.live == nullptr || delta.value("world", uint64_t{0}) != app.liveWorldId) return; // Wait for the scene
    const nlohmann::json none = nlohmann::json::array();
    for (const nlohmann::json& id : delta.value("colliders_remove", none))
    {
        app.sceneColliders.erase(id.get<uint32_t>());
        removeLiveCollider(app, id.get<uint32_t>());
    }
    for (const nlohmann::json& id : delta.value("characters_remove", none))
    {
        app.sceneCharacters.erase(id.get<uint32_t>());
        removeLiveCharacter(app, id.get<uint32_t>());
    }
    for (const nlohmann::json& c : delta.value("colliders_add", none))
    {
        app.sceneColliders[c["id"].get<uint32_t>()] = c;
        addLiveCollider(app, c);
    }
    for (const nlohmann::json& c : delta.value("characters_add", none))
    {
        app.sceneCharacters[c["id"].get<uint32_t>()] = c;
        addLiveCharacter(app, c);
    }
    refreshLiveDerived(app);
}

void applyLiveFrame(App& app, const nlohmann::json& frame)
{
    if (app.live == nullptr) return;
    // Characters, moving proxies (cars) and the host camera: buffered and blended every redraw from one clock (see
    // updateLivePose), so a rider stays on his bike (cars used to move on arrival, ahead of the delayed characters).
    App::LiveFrame buffered;
    buffered.time = static_cast<double>(frame["step"].get<uint64_t>()) / 120.0;
    for (const nlohmann::json& c : frame["colliders"])
    {
        buffered.colliders[c["id"].get<uint32_t>()] = DdTransform{jsonVec(c["p"]), jsonQuat(c["q"])};
    }
    if (frame.contains("camera"))
    {
        const nlohmann::json& c = frame["camera"];
        buffered.hasCamera = true;
        buffered.cameraPosition = toRay(jsonVec(c["p"]));
        buffered.cameraForward = Vector3Normalize(toRay(jsonVec(c["f"])));
        buffered.cameraUp = Vector3Normalize(toRay(jsonVec(c["u"])));
        buffered.cameraFov = c["fov"].get<float>();
    }
    app.liveStep = frame["step"].get<uint64_t>();
    ++app.liveFrames;
    static const char* const kStates[] = {"active", "recovering", "settled", "dead", "FAILED"};
    static const char* const kTension[] = {"limp", "relaxed", "tense", "stunned"};
    app.liveStatus.clear();
    for (const nlohmann::json& c : frame["characters"])
    {
        const uint32_t hostId = c.value("id", 0u);
        auto found = app.liveById.find(hostId);
        if (found == app.liveById.end()) continue;
        for (App::LiveCharacter& entry : app.liveList)
        {
            if (entry.id != hostId) continue;
            entry.visible = c.value("visible", 1) != 0;
            entry.tension = c.value("tension", 0);
            entry.balance = c.value("balance", 0) != 0;
        }
        dragdoll_character_set_riding(found->second, c.value("riding", 0u)); // Its own vehicle is no threat to it here either
        dragdoll_character_set_label(found->second, c.value("label", std::string()).c_str());
        App::LivePose pose;
        for (const nlohmann::json& q : c["joints"]) pose.joints.push_back(jsonQuat(q));
        pose.root = DdTransform{jsonVec(c["p"]), jsonQuat(c["q"])};
        buffered.characters[hostId] = std::move(pose);
        std::string label = "character " + std::to_string(hostId);
        for (const App::LiveCharacter& entry : app.liveList)
        {
            if (entry.id == hostId) label = entry.name;
        }
        char line[200];
        std::snprintf(line, sizeof(line), "%s: %s | %s%s | capture point %.1f cm", label.c_str(), kStates[std::min(4, c["state"].get<int>())],
                      kTension[std::min(3, c["tension"].get<int>())], c["balance"].get<int>() != 0 ? " + balance" : "",
                      c["balance_error"].get<float>() * 100.0f);
        std::string status = line;
        if (c.value("ttc", -1.0f) >= 0.0f) // The host's own perception and behaviour stage
        {
            static const char* const kClass[] = {"?", "wrap", "forward projection", "fender vault", "roof vault", "somersault"};
            char more[160];
            std::snprintf(more, sizeof(more), " | threat in %.2f s, expects %s", c.value("ttc", 0.0f), kClass[std::clamp(c.value("expects", 0), 0, 5)]);
            status += more;
        }
        if (!c.value("behaviour", std::string()).empty())
        {
            status += " | " + c.value("behaviour", std::string()) + ": " + c.value("stage", std::string());
        }
        app.liveStatus.push_back(status);
    }
    if (!app.liveBuffer.empty() && buffered.time < app.liveBuffer.back().time) app.liveBuffer.clear(); // Host restarted
    app.liveBuffer.push_back(std::move(buffered));
    while (app.liveBuffer.size() > 32) app.liveBuffer.pop_front();
}

DdQuat nlerp(DdQuat a, DdQuat b, float t)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = DdQuat{-b.x, -b.y, -b.z, -b.w}; // Shortest way
    DdQuat q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return DdQuat{q.x / n, q.y / n, q.z / n, q.w / n};
}
DdTransform blend(const DdTransform& a, const DdTransform& b, float t)
{
    return DdTransform{DdVec3{a.position.x + (b.position.x - a.position.x) * t, a.position.y + (b.position.y - a.position.y) * t,
                              a.position.z + (b.position.z - a.position.z) * t},
                       nlerp(a.rotation, b.rotation, t)};
}

// Every redraw: a smooth clock ~50 ms behind the newest frame picks the two frames around it and blends the
// characters' poses (host frames arrive unevenly, e.g. 33/47 ms; jumping to each one looks choppy).
void updateLivePose(App& app, float realDt)
{
    if (app.live == nullptr || app.liveBuffer.empty()) return;
    constexpr double kDelay = 0.12; // ~3 game frames of margin: a late frame must not freeze, then jump
    const double newest = app.liveBuffer.back().time;
    if (app.liveClock < 0.0 || std::fabs(app.liveClock - (newest - kDelay)) > 0.5) app.liveClock = newest - kDelay; // (Re)sync
    // Track the host's pace by running the clock a little faster or slower, never by stepping it: frames arrive in
    // ~40 ms steps, and pulling the clock towards each new one made playback speed swing ~20% (stutter at driving
    // speed). Close to the target: at most 5% (invisible); far off (after a game hitch): up to 30% to catch up.
    const double lag = newest - kDelay - app.liveClock;
    const double gain = std::fabs(lag) < 0.05 ? 0.5 : 3.0;
    app.liveClock += static_cast<double>(realDt) * (1.0 + std::clamp(lag * gain, -0.3, 0.3));
    size_t b = 0;
    while (b < app.liveBuffer.size() && app.liveBuffer[b].time < app.liveClock) ++b;
    const App::LiveFrame& next = app.liveBuffer[b < app.liveBuffer.size() ? b : app.liveBuffer.size() - 1];
    const App::LiveFrame& prev = app.liveBuffer[b == 0 ? 0 : b - 1];
    const double span = next.time - prev.time;
    const float t = span > 1e-6 ? static_cast<float>(std::clamp((app.liveClock - prev.time) / span, 0.0, 1.0)) : 1.0f;
    for (const auto& [hostId, pose] : next.characters)
    {
        auto mirror = app.liveById.find(hostId);
        if (mirror == app.liveById.end()) continue;
        auto before = prev.characters.find(hostId);
        DdTransform root = pose.root;
        std::vector<DdQuat> joints = pose.joints;
        if (before != prev.characters.end() && before->second.joints.size() == joints.size())
        {
            root = blend(before->second.root, pose.root, t);
            for (size_t n = 0; n < joints.size(); ++n) joints[n] = nlerp(before->second.joints[n], pose.joints[n], t);
        }
        dragdoll_character_activate(mirror->second, nullptr, nullptr, &root, joints.data(), static_cast<uint32_t>(joints.size()), 0.0f);
    }
    {
        // Moving proxies: kinematic targets at the same blend, reached exactly by one mirror step (the mirror never
        // simulates anything else: no gravity, kinematic characters).
        std::vector<DdColliderId> ids;
        std::vector<DdTransform> targets;
        for (const auto& [hostId, pose] : next.colliders)
        {
            auto it = app.liveColliders.find(hostId);
            if (it == app.liveColliders.end()) continue;
            auto before = prev.colliders.find(hostId);
            ids.push_back(it->second);
            targets.push_back(before != prev.colliders.end() ? blend(before->second, pose, t) : pose);
        }
        if (!ids.empty()) dragdoll_world_set_kinematic_targets(app.live, ids.data(), targets.data(), static_cast<uint32_t>(ids.size()));
        dragdoll_world_step_fixed(app.live, 1);
    }
    if (next.hasCamera)
    {
        const bool both = prev.hasCamera;
        const Vector3 position = both ? Vector3Lerp(prev.cameraPosition, next.cameraPosition, t) : next.cameraPosition;
        const Vector3 forward = Vector3Normalize(both ? Vector3Lerp(prev.cameraForward, next.cameraForward, t) : next.cameraForward);
        app.hostCamera.position = position;
        app.hostCamera.target = Vector3Add(position, Vector3Scale(forward, 8.0f));
        app.hostCamera.up = Vector3Normalize(both ? Vector3Lerp(prev.cameraUp, next.cameraUp, t) : next.cameraUp);
        app.hostCamera.fovy = both ? prev.cameraFov + (next.cameraFov - prev.cameraFov) * t : next.cameraFov;
        app.hostCamera.projection = CAMERA_PERSPECTIVE;
        app.hasHostCamera = true;
    }
    if (app.capture.is_open())
    {
        // What the viewer draws, every redraw: lets a capture show whether a stutter comes from the game or from here.
        nlohmann::json view = {{"t", "view"}, {"clock", app.liveClock}, {"blend", t}, {"newest", newest}, {"mode", app.cameraMode}};
        for (const App::LiveCharacter& entry : app.liveList)
        {
            if (entry.name.find("CJ") == std::string::npos) continue;
            auto it = app.liveById.find(entry.id);
            if (it == app.liveById.end()) break;
            DdTransform r{};
            dragdoll_character_get_pose(it->second, 0, nullptr, 0, &r);
            view["cj"] = {r.position.x, r.position.y, r.position.z};
        }
        const Camera3D cam = app.camera.build();
        view["cam"] = {cam.position.x, cam.position.y, cam.position.z};
        app.capture << nlohmann::json{{"rx", GetTime() - app.captureStart}, {"m", view}}.dump() << "\n";
    }
}

// Live camera modes: the host's own camera, or the orbit centred on CJ's puppet (orbit and zoom still work).
void updateLiveCamera(App& app)
{
    app.camera.hasOverride = app.cameraMode == 1 && app.hasHostCamera;
    if (app.camera.hasOverride)
    {
        app.camera.overrideCam = app.hostCamera;
        app.camera.target = app.hostCamera.target; // The grid centres on what the game camera looks at
        return;
    }
    if (app.cameraMode == 2)
    {
        for (const App::LiveCharacter& entry : app.liveList)
        {
            if (entry.name.find("CJ") == std::string::npos) continue;
            auto it = app.liveById.find(entry.id);
            if (it == app.liveById.end()) break;
            DdTransform root{};
            dragdoll_character_get_pose(it->second, 1, nullptr, 0, &root);
            app.camera.target = toRay(root.position); // His drawn pose is already smoothed: follow it exactly
            break;
        }
    }
}

void pollLive(App& app, float realDt)
{
    bool connected = false;
    for (const std::string& line : live::poll(connected))
    {
        const nlohmann::json m = nlohmann::json::parse(line, nullptr, false);
        if (m.is_discarded() || !m.contains("t")) continue;
        if (app.capture.is_open())
        {
            app.capture << nlohmann::json{{"rx", GetTime() - app.captureStart}, {"m", m}}.dump() << "\n";
            ++app.captureLines;
        }
        if (m["t"] == "scene") rebuildLive(app, m);
        else if (m["t"] == "delta") applyLiveDelta(app, m);
        else if (m["t"] == "frame") applyLiveFrame(app, m);
    }
    if (connected) app.liveLostAt = -1.0;
    else if (app.liveConnected || app.liveLostAt < 0.0) app.liveLostAt = GetTime();
    app.liveConnected = connected;
    updateLivePose(app, realDt);
    if (app.liveFocusPending && focusCharacter(app)) app.liveFocusPending = false;
    updateLiveCamera(app);
}

void drawLiveHud(const App& app)
{
    int y = 12;
    auto text = [&](const std::string& s, Color color, int size = 18) {
        DrawText(s.c_str(), 14, y, size, color);
        y += size + 6;
    };
    text("LIVE from 127.0.0.1:" + std::to_string(app.attachPort) + (app.liveConnected ? "  - connected" : "  - waiting for a world (start the game / press F7)..."),
         app.liveConnected ? Color{110, 230, 130, 255} : Color{255, 200, 90, 255}, 20);
    if (app.live != nullptr)
    {
        text("what DragDoll sees: " + std::to_string(app.liveColliders.size()) + " colliders, " + std::to_string(app.liveCharacters.size()) +
                 " characters | host step " + std::to_string(app.liveStep) + " | frames " + std::to_string(app.liveFrames),
             Color{160, 220, 255, 255}, 16);
    }
    for (const std::string& line : app.liveStatus) text(line, Color{255, 220, 120, 255}, 16);
    if (app.showHelp)
    {
        DrawText("C camera: free / game / follow CJ | F focus ragdoll | left-drag orbit | middle-drag pan | wheel zoom | H hide help", 14, GetScreenHeight() - 28, 16, Color{170, 175, 185, 255});
    }
    DrawFPS(GetScreenWidth() - 90, 10);
}

void startCapture(App& app)
{
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(GetApplicationDirectory()) / ".." / ".." / "captures"; // build/captures
    std::filesystem::create_directories(dir, ec);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char name[64];
    std::strftime(name, sizeof(name), "live_%Y%m%d_%H%M%S.jsonl", &local);
    app.capturePath = std::filesystem::weakly_canonical(dir / name, ec).string();
    app.capture.open(app.capturePath, std::ios::binary);
    app.captureStart = GetTime();
    app.captureLines = 0;
    if (app.capture.is_open() && !app.sceneWorld.is_null())
    {
        app.capture << nlohmann::json{{"rx", 0.0}, {"m", composeLiveScene(app)}}.dump() << "\n";
    }
}

void finishCapture(App& app)
{
    if (!app.capture.is_open()) return;
    const std::string note = app.captureNote;
    app.capture << nlohmann::json{{"rx", GetTime() - app.captureStart}, {"m", {{"t", "note"}, {"text", note}}}}.dump() << "\n";
    app.capture.close();
    std::ofstream txt(std::filesystem::path(app.capturePath).replace_extension(".txt"), std::ios::binary);
    txt << note << "\n";
    app.lastCapture = app.capturePath;
    app.askCaptureNote = false;
    app.captureNote[0] = '\0';
}

void drawCaptureControls(App& app)
{
    ImGui::SeparatorText("capture for Claude");
    if (!app.capture.is_open())
    {
        if (ImGui::Button("Start capture")) startCapture(app);
        if (!app.lastCapture.empty()) ImGui::TextWrapped("saved: %s", std::filesystem::path(app.lastCapture).filename().string().c_str());
    }
    else if (!app.askCaptureNote)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f), "REC %.0f s, %llu messages", GetTime() - app.captureStart,
                           static_cast<unsigned long long>(app.captureLines));
        if (ImGui::Button("Stop capture")) app.askCaptureNote = true; // Keeps recording while the note is typed
    }
    if (app.askCaptureNote)
    {
        ImGui::OpenPopup("What should Claude look at?");
    }
    if (ImGui::BeginPopupModal("What should Claude look at?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("Describe what you saw (e.g. \"CJ's legs jitter when he turns left\").");
        ImGui::InputTextMultiline("##note", app.captureNote, sizeof(app.captureNote), ImVec2(520.0f, 140.0f));
        if (ImGui::Button("Save capture"))
        {
            finishCapture(app);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Viewer settings that survive restarts (next to the executable).
std::filesystem::path settingsPath()
{
    return std::filesystem::path(GetApplicationDirectory()) / "dragdoll_view_settings.json";
}

void loadSettings(App& app)
{
    std::ifstream in(settingsPath());
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return;
    app.cameraMode = std::clamp(j.value("camera_mode", 0), 0, 2);
    app.mouseSensitivity = std::clamp(j.value("mouse_sensitivity", app.mouseSensitivity), 0.02f, 2.0f);
    app.mouseInvertY = j.value("mouse_invert_y", app.mouseInvertY);
}

void saveSettings(const App& app)
{
    std::ofstream out(settingsPath());
    out << nlohmann::json{{"camera_mode", app.cameraMode}, {"mouse_sensitivity", app.mouseSensitivity},
                          {"mouse_invert_y", app.mouseInvertY}}.dump(2)
        << "\n";
}

void drawLivePanel(App& app)
{
    if (app.attachPort <= 0 || app.liveList.empty())
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(10.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Live characters"))
    {
        drawCaptureControls(app);
        const char* const modes[] = {"free", "game camera", "follow CJ"};
        ImGui::TextDisabled("camera (C cycles)");
        for (int m = 0; m < 3; ++m)
        {
            if (m > 0) ImGui::SameLine();
            if (ImGui::RadioButton(modes[m], app.cameraMode == m))
            {
                app.cameraMode = m;
                saveSettings(app);
            }
        }
        if (app.cameraMode == 1)
        {
            ImGui::TextDisabled(app.controlling ? "controlling the game - Esc releases" : "click the view to control the game");
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::SliderFloat("mouse", &app.mouseSensitivity, 0.02f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                saveSettings(app);
            ImGui::SameLine();
            if (ImGui::Checkbox("invert Y", &app.mouseInvertY)) saveSettings(app);
        }
        static const char* const kTensions[] = {"limp", "relaxed", "tense", "stunned"};
        for (App::LiveCharacter& entry : app.liveList)
        {
            ImGui::PushID(static_cast<int>(entry.id));
            ImGui::SeparatorText(entry.name.c_str());
            bool visible = entry.visible;
            if (ImGui::Checkbox("show in game", &visible))
            {
                entry.visible = visible; // Confirmed by the next frame
                live::send(nlohmann::json{{"t", "show"}, {"id", entry.id}, {"visible", visible ? 1 : 0}}.dump());
            }
            // Muscles and balance of the game's character (applied and recorded by the game's DragDoll).
            for (int t = 0; t < 4; ++t)
            {
                if (t > 0) ImGui::SameLine();
                if (ImGui::RadioButton(kTensions[t], entry.tension == t))
                {
                    entry.tension = t;
                    live::send(nlohmann::json{{"t", "tension"}, {"id", entry.id}, {"tension", t}, {"blend", 0.3}}.dump());
                }
            }
            bool balance = entry.balance;
            if (ImGui::Checkbox("balance (needs muscles)", &balance))
            {
                entry.balance = balance;
                live::send(nlohmann::json{{"t", "balance"}, {"id", entry.id}, {"on", balance ? 1 : 0}}.dump());
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void drawPanels(App& app, BodyMotion& motion)
{
    drawLivePanel(app);
    drawRigPanel(app);
    const float w = static_cast<float>(GetScreenWidth());
    const bool replaying = app.replay != nullptr || app.attachPort > 0;

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
            DdCharacter* c = app.main.world == nullptr ? nullptr
                             : app.main.scenario->character() != nullptr ? app.main.scenario->character()
                                                                         : dragdoll_world_get_character(app.main.world, 0);
            if (c != nullptr)
            {
                ImGui::SeparatorText("Muscles");
                if (ImGui::RadioButton("scenario", app.tensionOverride < 0)) app.tensionOverride = -1; // From the next restart
                ImGui::SameLine();
                ImGui::TextDisabled("(now: %s)", kTensionNames[dragdoll_character_get_tension(c)]);
                for (int t = 0; t < DD_TENSION_COUNT; ++t)
                {
                    if (t > 0) ImGui::SameLine();
                    if (ImGui::RadioButton(kTensionNames[t], app.tensionOverride == t)) setLiveTension(app, static_cast<DdTension>(t), app.tensionBlend);
                }
                ImGui::SliderFloat("Blend", &app.tensionBlend, 0.0f, 2.0f, "%.2f s");
                bool balance = dragdoll_character_get_balance(c) != 0;
                if (ImGui::Checkbox("Balance (needs muscles)", &balance)) dragdoll_character_set_balance(c, balance ? 1 : 0);
            }
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
    int attachPort = 0;
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
        else if (std::strcmp(argv[i], "--attach") == 0)
        {
            attachPort = (i + 1 < argc && argv[i + 1][0] != '-') ? std::atoi(argv[++i]) : 47830;
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
    app.attachPort = attachPort;
    loadSettings(app);
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
    if (app.attachPort > 0)
    {
        live::start(app.attachPort);
        app.autoRestart = false;
    }
    else if (!app.replayPath.empty())
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
    while (!WindowShouldClose() && !(IsKeyPressed(KEY_ESCAPE) && !app.noteEditing && !app.controlling))
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
        if (keyPressed(app, KEY_F) && (app.replay != nullptr || app.attachPort > 0)) focusCharacter(app);
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
        if (keyPressed(app, KEY_C) && app.attachPort > 0)
        {
            app.cameraMode = (app.cameraMode + 1) % 3;
            saveSettings(app);
        }
        // Game camera mode: a click in the view hands keyboard + mouse to the game until Esc (or another mode).
        // A new host world (F7 in SA) reconnects within a moment: keep control through it, release if the game is gone.
        const bool gameGone = !app.liveConnected && app.liveLostAt >= 0.0 && GetTime() - app.liveLostAt > 2.0;
        if (app.controlling && (IsKeyPressed(KEY_ESCAPE) || app.cameraMode != 1 || gameGone))
        {
            setControlling(app, false);
        }
        else if (app.controlling)
        {
            sendRemoteInput(app, false);
        }
        else if (app.attachPort > 0 && app.cameraMode == 1 && app.liveConnected && !app.noteEditing &&
                 !ImGui::GetIO().WantCaptureMouse && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            setControlling(app, true);
        }
        if (!ImGui::GetIO().WantCaptureMouse && !app.camera.hasOverride && !app.controlling)
        {
            app.camera.update();
            if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && app.replay == nullptr && !app.noteEditing)
            {
                pushBodyUnderMouse(app);
            }
        }

        // --- simulate ---
        if (app.attachPort > 0)
        {
            pollLive(app, GetFrameTime());
        }
        else if (app.replay != nullptr)
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
        std::vector<Line> overlayLines;
        std::vector<std::string> threatText;
        if (DdWorld* world = activeWorld(app))
        {
            DdDebugDrawCallbacks cb = callbacks;
            cb.user = &geometry;
            cb.flags = DD_DRAW_SHAPES | DD_DRAW_CAMERA_LOD;
            const Camera3D eye = app.camera.build();
            cb.camera_position = DdVec3{eye.position.x, eye.position.y, eye.position.z};
            drawResult = dragdoll_world_debug_draw(world, &cb);
            if (app.showLimits && drawResult == DD_OK)
            {
                cb.user = &overlay;
                cb.flags = DD_DRAW_CONSTRAINT_LIMITS;
                dragdoll_world_debug_draw(world, &cb);
            }
            collectThreats(world, overlayLines, threatText);
        }

        BeginDrawing();
        ClearBackground(Color{22, 26, 34, 255});
        BeginMode3D(app.camera.build());
        // The grid lies on the ground the character stands on (game sessions are far above height 0): the floor
        // proxy's top in live view, otherwise the lowest body height seen so far (so it does not slide with a fall).
        float gridHeight = 0.0f;
        if (app.attachPort > 0 && app.liveHasFloor)
        {
            gridHeight = app.liveFloor;
        }
        else if (app.replay != nullptr || app.attachPort > 0)
        {
            if (DdCharacter* c = activeWorld(app) != nullptr ? dragdoll_world_get_character(activeWorld(app), 0) : nullptr)
            {
                std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
                dragdoll_character_get_body_transforms(c, 1, t.data(), static_cast<uint32_t>(t.size()));
                float lowest = 1e9f;
                for (const DdTransform& b : t) lowest = std::min(lowest, Vector3DotProduct(toRay(b.position), app.camera.up));
                app.lowestSeen = std::min(app.lowestSeen, lowest - 0.04f); // Body centres sit a few cm above the floor
                gridHeight = app.lowestSeen;
            }
        }
        // A live game streaming its own level collision has a real floor: no grid, and nothing is hidden below it
        // (CJ walks downhill, through tunnels, ...). The grid stays for a flat floor proxy and for replays.
        const bool levelIsFloor = app.attachPort > 0 && !app.liveHasFloor;
        if (!levelIsFloor) drawGrid(app.camera, gridHeight);
        gFloorHeight = (app.replay != nullptr || app.attachPort > 0) ? gridHeight + 0.04f : 0.0f;
        const Camera3D view = app.camera.build();
        const bool cameraBelowFloor = !levelIsFloor && Vector3DotProduct(view.position, app.camera.up) < gFloorHeight;
        drawGeometry(geometry, 255, cameraBelowFloor ? &app.camera.up : nullptr);
        if (!overlay.tris.empty() || !overlay.lines.empty())
        {
            rlDrawRenderBatchActive();
            rlDisableDepthMask(); // Translucent: don't hide what is behind
            drawGeometry(overlay, 128);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
        }
        rlDisableDepthTest(); // What a character perceives is drawn on top of the vehicle
        for (const Line& l : overlayLines) DrawLine3D(l.a, l.b, l.color);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        EndMode3D();
        // What each character is doing in its host (dragdoll_character_set_label), above its head.
        if (DdWorld* labelled = activeWorld(app))
        {
            const Camera3D eye = app.camera.build();
            const Vector3 up = app.camera.up;
            for (uint32_t i = 0; i < dragdoll_world_get_character_count(labelled); ++i)
            {
                DdCharacter* c = dragdoll_world_get_character(labelled, i);
                const char* label = dragdoll_character_get_label(c);
                const int head = dragdoll_character_find_body(c, "head");
                if (label[0] == 0 || head < 0) continue;
                std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
                if (dragdoll_character_get_body_transforms(c, 1, t.data(), static_cast<uint32_t>(t.size())) != DD_OK) continue;
                const Vector3 above = Vector3Add(toRay(t[static_cast<size_t>(head)].position), Vector3Scale(up, 0.35f));
                const Vector3 toLabel = Vector3Subtract(above, eye.position);
                if (Vector3DotProduct(toLabel, Vector3Subtract(eye.target, eye.position)) <= 0.0f || Vector3Length(toLabel) > 40.0f) continue;
                const Vector2 at = GetWorldToScreen(above, eye);
                const int width = MeasureText(label, 14);
                DrawRectangle(static_cast<int>(at.x) - width / 2 - 3, static_cast<int>(at.y) - 9, width + 6, 18, Color{0, 0, 0, 140});
                DrawText(label, static_cast<int>(at.x) - width / 2, static_cast<int>(at.y) - 7, 14, Color{150, 230, 255, 255});
            }
        }
        for (size_t i = 0; i < threatText.size(); ++i)
        {
            DrawText(threatText[i].c_str(), 14, GetScreenHeight() - 26 - 22 * static_cast<int>(threatText.size() - 1 - i), 18,
                     Color{255, 210, 90, 255});
        }
        if (drawResult == DD_ERROR_UNSUPPORTED)
        {
            DrawText("This DragDoll build has no debug renderer (use a dev preset).", 14, 300, 20, RED);
        }
        if (app.attachPort > 0)
        {
            drawLiveHud(app);
        }
        else if (app.replay != nullptr)
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

    finishCapture(app); // Do not lose an open capture
    commands::stop();
    live::stop();
    dragdoll_world_destroy(app.live);
    rlImGuiShutdown();
    CloseWindow();
    dragdoll_replay_close(app.replay);
    app.main = Instance();
    app.shadow = Instance();
    dragdoll_shutdown();
    return 0;
}
