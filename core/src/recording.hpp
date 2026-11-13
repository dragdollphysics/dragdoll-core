// SPDX-License-Identifier: MIT
// .ddrec — recording of every call that changes a world, captured at the C ABI boundary.
//
// File layout (little-endian):
//   header : "DDREC\0\0\0" (8 bytes) | u32 format version | u32 API version | string build info
//   records: u8 type | u32 payload size | payload
//   string = u32 length + bytes (no terminator); vec3 = 3 x f32; quat = 4 x f32; transform = vec3 + quat
#pragma once

#include <dragdoll/dragdoll.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace dragdoll::rec
{

inline constexpr char kMagic[8] = {'D', 'D', 'R', 'E', 'C', 0, 0, 0};
inline constexpr uint32_t kFormatVersion = 1;

enum class Record : uint8_t
{
    WorldCreate = 1,      // desc
    Step = 2,             // f32 dt | u32 steps taken | u64 hash after
    StepFixed = 3,        // u32 steps | u64 hash after
    AddColliders = 4,     // u32 count | count x (collider desc + u32 id)
    RemoveColliders = 5,  // u32 count | ids
    KinematicTargets = 6, // u32 count | count x (u32 id + transform)
    CharacterCreate = 7,  // u32 id | string rig json | transform | f32 mass scale | u64 user data
    CharacterDestroy = 8, // u32 id
    Impulse = 9,          // u32 character id | u32 body | vec3 impulse | u8 has point | vec3 point
    Note = 10,            // string text
    Tension = 11,         // u32 character id | u32 profile | f32 blend seconds
    TargetPose = 12,      // u32 character id | u32 count (0 = reset to rest) | count x quat
    Activate = 13,        // u32 id | f32 dt | u8 flags (1 prev root, 2 prev joints, 4 joints) | [prev root] | [count x prev quat] | root | [count x quat]
};

// Growable little-endian byte buffer for one record payload.
class Buffer
{
public:
    void u8(uint8_t v) { bytes(&v, 1); }
    void u32(uint32_t v) { bytes(&v, 4); }
    void u64(uint64_t v) { bytes(&v, 8); }
    void f32(float v) { bytes(&v, 4); }
    void vec3(const DdVec3& v)
    {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void quat(const DdQuat& q)
    {
        f32(q.x);
        f32(q.y);
        f32(q.z);
        f32(q.w);
    }
    void transform(const DdTransform& t)
    {
        vec3(t.position);
        quat(t.rotation);
    }
    void string(const char* s, size_t length)
    {
        u32(static_cast<uint32_t>(length));
        bytes(s, length);
    }
    void bytes(const void* data, size_t size)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        mData.insert(mData.end(), p, p + size);
    }
    const std::vector<uint8_t>& data() const { return mData; }
    void clear() { mData.clear(); }

private:
    std::vector<uint8_t> mData;
};

// Bounds-checked reader over one record payload (throws std::runtime_error on truncation).
class Cursor
{
public:
    Cursor(const uint8_t* data, size_t size) : mData(data), mSize(size) {}
    uint8_t u8() { return get<uint8_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    uint64_t u64() { return get<uint64_t>(); }
    float f32() { return get<float>(); }
    DdVec3 vec3()
    {
        DdVec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }
    DdQuat quat()
    {
        DdQuat q;
        q.x = f32();
        q.y = f32();
        q.z = f32();
        q.w = f32();
        return q;
    }
    DdTransform transform()
    {
        DdTransform t;
        t.position = vec3();
        t.rotation = quat();
        return t;
    }
    std::string string()
    {
        const uint32_t length = u32();
        need(length);
        std::string s(reinterpret_cast<const char*>(mData + mPos), length);
        mPos += length;
        return s;
    }
    void bytes(void* out, size_t size)
    {
        need(size);
        std::memcpy(out, mData + mPos, size);
        mPos += size;
    }

private:
    template <typename T>
    T get()
    {
        T value;
        bytes(&value, sizeof(T));
        return value;
    }
    void need(size_t size) const
    {
        if (mPos + size > mSize)
        {
            throw std::runtime_error("recording is truncated or corrupt");
        }
    }
    const uint8_t* mData;
    size_t mSize;
    size_t mPos = 0;
};

// Opens a file whose path is UTF-8 (paths with non-ASCII characters work on Windows too).
std::FILE* openUtf8(const char* path, const char* mode);

// Writes records to a file. All methods are no-throw on I/O errors after open (a failed recording must never break
// the host); `failed()` reports whether anything was lost.
class Recorder
{
public:
    // Throws std::invalid_argument if the file can't be created.
    Recorder(const char* path, const DdWorldDesc& desc);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void step(float dt, uint32_t steps, uint64_t hash);
    void stepFixed(uint32_t steps, uint64_t hash);
    void addColliders(const DdColliderDesc* descs, uint32_t count, const DdColliderId* ids);
    void removeColliders(const DdColliderId* ids, uint32_t count);
    void kinematicTargets(const DdColliderId* ids, const DdTransform* targets, uint32_t count);
    void characterCreate(uint32_t id, const DdCharacterDesc& desc);
    void characterDestroy(uint32_t id);
    void impulse(uint32_t character, uint32_t body, const DdVec3& impulse, const DdVec3* point);
    void note(const char* text);
    void tension(uint32_t character, uint32_t profile, float blendSeconds);
    void targetPose(uint32_t character, const DdQuat* rotations, uint32_t count);
    void activate(uint32_t character, const DdTransform* previousRoot, const DdQuat* previousJoints, const DdTransform& root,
                  const DdQuat* joints, uint32_t count, float dt);
    bool failed() const { return mFailed; }

private:
    void write(Record type, const Buffer& payload);
    std::FILE* mFile = nullptr;
    bool mFailed = false;
    Buffer mScratch;
};

// Serialisation of the variable-size descriptions, shared by recorder and replay.
void writeWorldDesc(Buffer& out, const DdWorldDesc& desc);
DdWorldDesc readWorldDesc(Cursor& in);
void writeColliderDesc(Buffer& out, const DdColliderDesc& desc);
// Reads a collider desc; mesh arrays are stored in `vertices` / `indices` (kept alive by the caller).
DdColliderDesc readColliderDesc(Cursor& in, std::vector<DdVec3>& vertices, std::vector<uint32_t>& indices);

} // namespace dragdoll::rec
