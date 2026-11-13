// SPDX-License-Identifier: MIT
#include "recording.hpp"

#include <cstring>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#endif

namespace dragdoll::rec
{

std::FILE* openUtf8(const char* path, const char* mode)
{
#ifdef _WIN32
    auto widen = [](const char* s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        std::wstring w(n > 0 ? static_cast<size_t>(n - 1) : 0, wchar_t{});
        if (n > 1)
        {
            MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
        }
        return w;
    };
    std::FILE* file = nullptr;
    return _wfopen_s(&file, widen(path).c_str(), widen(mode).c_str()) == 0 ? file : nullptr;
#else
    return std::fopen(path, mode);
#endif
}

// ------------------------------------------------------------------------------------------------
// Description serialisation
// ------------------------------------------------------------------------------------------------
void writeWorldDesc(Buffer& out, const DdWorldDesc& d)
{
    out.vec3(d.up);
    out.f32(d.gravity);
    out.f32(d.step_hz);
    out.u32(d.max_steps_per_call);
    out.u32(d.worker_threads);
    out.u32(d.max_bodies);
}

DdWorldDesc readWorldDesc(Cursor& in)
{
    DdWorldDesc d;
    dragdoll_world_desc_default(&d);
    d.up = in.vec3();
    d.gravity = in.f32();
    d.step_hz = in.f32();
    d.max_steps_per_call = in.u32();
    d.worker_threads = in.u32();
    d.max_bodies = in.u32();
    d.record_path = nullptr;
    return d;
}

void writeColliderDesc(Buffer& out, const DdColliderDesc& d)
{
    out.u32(static_cast<uint32_t>(d.shape));
    out.u32(static_cast<uint32_t>(d.motion));
    out.transform(d.transform);
    out.f32(d.radius);
    out.f32(d.half_height);
    out.vec3(d.half_extents);
    out.f32(d.friction);
    out.f32(d.restitution);
    out.f32(d.mass);
    out.u64(d.user_data);
    const uint32_t vertexCount = d.vertices != nullptr ? d.vertex_count : 0;
    out.u32(vertexCount);
    for (uint32_t i = 0; i < vertexCount; ++i)
    {
        out.vec3(d.vertices[i]);
    }
    const uint32_t indexCount = d.indices != nullptr ? d.index_count : 0;
    out.u32(indexCount);
    if (indexCount > 0)
    {
        out.bytes(d.indices, indexCount * sizeof(uint32_t));
    }
}

DdColliderDesc readColliderDesc(Cursor& in, std::vector<DdVec3>& vertices, std::vector<uint32_t>& indices)
{
    DdColliderDesc d;
    dragdoll_collider_desc_default(&d);
    d.shape = static_cast<DdShapeType>(in.u32());
    d.motion = static_cast<DdMotionType>(in.u32());
    d.transform = in.transform();
    d.radius = in.f32();
    d.half_height = in.f32();
    d.half_extents = in.vec3();
    d.friction = in.f32();
    d.restitution = in.f32();
    d.mass = in.f32();
    d.user_data = in.u64();
    vertices.resize(in.u32());
    for (DdVec3& v : vertices)
    {
        v = in.vec3();
    }
    indices.resize(in.u32());
    if (!indices.empty())
    {
        in.bytes(indices.data(), indices.size() * sizeof(uint32_t));
    }
    d.vertices = vertices.empty() ? nullptr : vertices.data();
    d.vertex_count = static_cast<uint32_t>(vertices.size());
    d.indices = indices.empty() ? nullptr : indices.data();
    d.index_count = static_cast<uint32_t>(indices.size());
    return d;
}

// ------------------------------------------------------------------------------------------------
// Recorder
// ------------------------------------------------------------------------------------------------
Recorder::Recorder(const char* path, const DdWorldDesc& desc)
{
    mFile = openUtf8(path, "wb");
    if (mFile == nullptr)
    {
        throw std::invalid_argument(std::string("record_path: cannot create ") + path);
    }
    std::setvbuf(mFile, nullptr, _IOFBF, 1 << 20);
    Buffer header;
    header.bytes(kMagic, sizeof(kMagic));
    header.u32(kFormatVersion);
    header.u32(DD_API_VERSION);
    const char* build = dragdoll_get_build_info();
    header.string(build, std::strlen(build));
    mFailed = std::fwrite(header.data().data(), 1, header.data().size(), mFile) != header.data().size();

    Buffer payload;
    writeWorldDesc(payload, desc);
    write(Record::WorldCreate, payload);
}

Recorder::~Recorder()
{
    if (mFile != nullptr)
    {
        std::fclose(mFile);
    }
}

void Recorder::write(Record type, const Buffer& payload)
{
    if (mFile == nullptr)
    {
        return;
    }
    const uint8_t t = static_cast<uint8_t>(type);
    const uint32_t size = static_cast<uint32_t>(payload.data().size());
    bool ok = std::fwrite(&t, 1, 1, mFile) == 1;
    ok = ok && std::fwrite(&size, 4, 1, mFile) == 1;
    ok = ok && (size == 0 || std::fwrite(payload.data().data(), 1, size, mFile) == size);
    mFailed = mFailed || !ok;
}

void Recorder::step(float dt, uint32_t steps, uint64_t hash)
{
    mScratch.clear();
    mScratch.f32(dt);
    mScratch.u32(steps);
    mScratch.u64(hash);
    write(Record::Step, mScratch);
}

void Recorder::stepFixed(uint32_t steps, uint64_t hash)
{
    mScratch.clear();
    mScratch.u32(steps);
    mScratch.u64(hash);
    write(Record::StepFixed, mScratch);
}

void Recorder::addColliders(const DdColliderDesc* descs, uint32_t count, const DdColliderId* ids)
{
    mScratch.clear();
    mScratch.u32(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        writeColliderDesc(mScratch, descs[i]);
        mScratch.u32(ids[i]);
    }
    write(Record::AddColliders, mScratch);
}

void Recorder::removeColliders(const DdColliderId* ids, uint32_t count)
{
    mScratch.clear();
    mScratch.u32(count);
    mScratch.bytes(ids, count * sizeof(DdColliderId));
    write(Record::RemoveColliders, mScratch);
}

void Recorder::kinematicTargets(const DdColliderId* ids, const DdTransform* targets, uint32_t count)
{
    mScratch.clear();
    mScratch.u32(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        mScratch.u32(ids[i]);
        mScratch.transform(targets[i]);
    }
    write(Record::KinematicTargets, mScratch);
}

void Recorder::characterCreate(uint32_t id, const DdCharacterDesc& desc)
{
    mScratch.clear();
    mScratch.u32(id);
    mScratch.string(desc.rig_json, std::strlen(desc.rig_json));
    mScratch.transform(desc.transform);
    mScratch.f32(desc.mass_scale);
    mScratch.u64(desc.user_data);
    write(Record::CharacterCreate, mScratch);
}

void Recorder::characterDestroy(uint32_t id)
{
    mScratch.clear();
    mScratch.u32(id);
    write(Record::CharacterDestroy, mScratch);
}

void Recorder::impulse(uint32_t character, uint32_t body, const DdVec3& impulse, const DdVec3* point)
{
    mScratch.clear();
    mScratch.u32(character);
    mScratch.u32(body);
    mScratch.vec3(impulse);
    mScratch.u8(point != nullptr ? 1 : 0);
    mScratch.vec3(point != nullptr ? *point : DdVec3{0, 0, 0});
    write(Record::Impulse, mScratch);
}

void Recorder::tension(uint32_t character, uint32_t profile, float blendSeconds)
{
    mScratch.clear();
    mScratch.u32(character);
    mScratch.u32(profile);
    mScratch.f32(blendSeconds);
    write(Record::Tension, mScratch);
}

void Recorder::targetPose(uint32_t character, const DdQuat* rotations, uint32_t count)
{
    mScratch.clear();
    mScratch.u32(character);
    mScratch.u32(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        mScratch.quat(rotations[i]);
    }
    write(Record::TargetPose, mScratch);
}

void Recorder::activate(uint32_t character, const DdTransform* previousRoot, const DdQuat* previousJoints,
                        const DdTransform& root, const DdQuat* joints, uint32_t count, float dt)
{
    mScratch.clear();
    mScratch.u32(character);
    mScratch.f32(dt);
    mScratch.u8(static_cast<uint8_t>((previousRoot != nullptr ? 1 : 0) | (previousJoints != nullptr ? 2 : 0) | (joints != nullptr ? 4 : 0)));
    if (previousRoot != nullptr) mScratch.transform(*previousRoot);
    for (uint32_t i = 0; previousJoints != nullptr && i < count; ++i) mScratch.quat(previousJoints[i]);
    mScratch.transform(root);
    for (uint32_t i = 0; joints != nullptr && i < count; ++i) mScratch.quat(joints[i]);
    write(Record::Activate, mScratch);
}

void Recorder::balance(uint32_t character, bool enabled)
{
    mScratch.clear();
    mScratch.u32(character);
    mScratch.u8(enabled ? 1 : 0);
    write(Record::Balance, mScratch);
}

void Recorder::note(const char* text)
{
    mScratch.clear();
    mScratch.string(text, std::strlen(text));
    write(Record::Note, mScratch);
    if (mFile != nullptr)
    {
        std::fflush(mFile); // Notes mark interesting moments: make sure they survive a crash.
    }
}

} // namespace dragdoll::rec
