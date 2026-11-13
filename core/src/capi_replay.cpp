// SPDX-License-Identifier: MIT
// C ABI: replaying .ddrec recordings (dragdoll.h, "Recording & replay").

#include "capi_guard.hpp"
#include "capi_world_handle.hpp"
#include "character.hpp"
#include "recording.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace dragdoll::detail;
using dragdoll::rec::Cursor;
using dragdoll::rec::Record;

struct DdReplay
{
    std::vector<uint8_t> file; // The whole recording (kept in memory: recordings are small)
    size_t pos = 0;            // Next record
    uint32_t apiVersion = 0;
    std::string build;
    bool sameBuild = false;
    uint64_t frameCount = 0;
    uint64_t frame = 0;
    uint64_t firstDivergence = 0;
    std::string note;
    std::unique_ptr<DdWorld> world;
    std::map<uint32_t, DdCharacter*> characters; // recorded id -> live character
    DdWorldDesc desc{};
};

namespace
{
struct RecordView
{
    Record type;
    const uint8_t* data;
    uint32_t size;
};

// Reads the record at `pos` and advances it; returns false at the end of the file.
bool nextRecord(const std::vector<uint8_t>& file, size_t& pos, RecordView& out)
{
    if (pos == file.size())
    {
        return false;
    }
    if (pos + 5 > file.size())
    {
        throw std::runtime_error("recording is truncated (record header)");
    }
    out.type = static_cast<Record>(file[pos]);
    std::memcpy(&out.size, &file[pos + 1], 4);
    if (pos + 5 + out.size > file.size())
    {
        throw std::runtime_error("recording is truncated (record payload)");
    }
    out.data = &file[pos + 5];
    pos += 5 + out.size;
    return true;
}

uint64_t hashOf(DdWorld& world)
{
    return world.world.stateHash();
}

// Applies one non-step record to the replayed world.
void apply(DdReplay& replay, const RecordView& record)
{
    Cursor in(record.data, record.size);
    dragdoll::World& world = replay.world->world;
    switch (record.type)
    {
    case Record::AddColliders: {
        const uint32_t count = in.u32();
        std::vector<std::vector<DdVec3>> vertices(count);
        std::vector<std::vector<uint32_t>> indices(count);
        std::vector<DdColliderDesc> descs(count);
        std::vector<DdColliderId> expected(count), ids(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            descs[i] = dragdoll::rec::readColliderDesc(in, vertices[i], indices[i]);
            expected[i] = in.u32();
        }
        world.addColliders(descs.data(), count, ids.data());
        if (ids != expected)
        {
            throw std::runtime_error("replay: collider ids differ from the recording");
        }
        break;
    }
    case Record::RemoveColliders: {
        std::vector<DdColliderId> ids(in.u32());
        if (!ids.empty())
        {
            in.bytes(ids.data(), ids.size() * sizeof(DdColliderId));
        }
        world.removeColliders(ids.data(), static_cast<uint32_t>(ids.size()));
        break;
    }
    case Record::KinematicTargets: {
        const uint32_t count = in.u32();
        std::vector<DdColliderId> ids(count);
        std::vector<DdTransform> targets(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            ids[i] = in.u32();
            targets[i] = in.transform();
        }
        world.setKinematicTargets(ids.data(), targets.data(), count);
        break;
    }
    case Record::CharacterCreate: {
        const uint32_t id = in.u32();
        const std::string rig = in.string();
        DdCharacterDesc desc;
        dragdoll_character_desc_default(&desc);
        desc.rig_json = rig.c_str();
        desc.transform = in.transform();
        desc.mass_scale = in.f32();
        desc.user_data = in.u64();
        DdCharacter* character = world.createCharacter(desc);
        if (character->id != id)
        {
            throw std::runtime_error("replay: character ids differ from the recording");
        }
        replay.characters[id] = character;
        break;
    }
    case Record::CharacterDestroy: {
        const uint32_t id = in.u32();
        auto it = replay.characters.find(id);
        if (it != replay.characters.end())
        {
            world.destroyCharacter(it->second);
            replay.characters.erase(it);
        }
        break;
    }
    case Record::Impulse: {
        const uint32_t id = in.u32();
        const uint32_t body = in.u32();
        const DdVec3 impulse = in.vec3();
        const bool hasPoint = in.u8() != 0;
        const DdVec3 point = in.vec3();
        auto it = replay.characters.find(id);
        if (it == replay.characters.end())
        {
            throw std::runtime_error("replay: impulse on an unknown character");
        }
        world.applyImpulse(*it->second, body, impulse, hasPoint ? &point : nullptr);
        break;
    }
    case Record::Note:
        replay.note = in.string();
        break;
    case Record::WorldCreate:
        throw std::runtime_error("replay: a second world in one recording");
    default:
        // Unknown record types from newer formats are skipped (forward compatibility).
        break;
    }
}
} // namespace

extern "C"
{

DD_API DdResult DD_CALL dragdoll_replay_open(const char* path, DdReplay** out_replay)
{
    DD_GUARD_BEGIN
    clearLastError();
    if (path == nullptr || out_replay == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_open: path and out_replay are required");
    }
    *out_replay = nullptr;
    if (!isLibraryInitialized())
    {
        return fail(DD_ERROR_NOT_INITIALIZED, "dragdoll_replay_open: call dragdoll_init first");
    }
    std::FILE* f = dragdoll::rec::openUtf8(path, "rb");
    if (f == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_open: cannot open the file");
    }
    auto replay = std::make_unique<DdReplay>();
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    replay->file.resize(size > 0 ? static_cast<size_t>(size) : 0);
    const size_t read = replay->file.empty() ? 0 : std::fread(replay->file.data(), 1, replay->file.size(), f);
    std::fclose(f);
    if (read != replay->file.size())
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_open: cannot read the file");
    }

    try
    {
        // Header
        Cursor header(replay->file.data(), replay->file.size());
        char magic[8];
        header.bytes(magic, sizeof(magic));
        if (std::memcmp(magic, dragdoll::rec::kMagic, sizeof(magic)) != 0)
        {
            return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_open: not a .ddrec file");
        }
        if (header.u32() != dragdoll::rec::kFormatVersion)
        {
            return fail(DD_ERROR_VERSION_MISMATCH, "dragdoll_replay_open: unsupported .ddrec format version");
        }
        replay->apiVersion = header.u32();
        replay->build = header.string();
        replay->sameBuild = replay->build == dragdoll_get_build_info();
        replay->pos = 8 + 4 + 4 + 4 + replay->build.size();

        // Count host frames (and validate the record framing) in one pass.
        size_t scan = replay->pos;
        RecordView record{};
        while (nextRecord(replay->file, scan, record))
        {
            if (record.type == Record::Step || record.type == Record::StepFixed)
            {
                ++replay->frameCount;
            }
        }

        // The first record creates the world.
        if (!nextRecord(replay->file, replay->pos, record) || record.type != Record::WorldCreate)
        {
            return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_open: recording does not start with a world");
        }
        Cursor in(record.data, record.size);
        replay->desc = dragdoll::rec::readWorldDesc(in);
        replay->world = std::make_unique<DdWorld>(replay->desc);
    }
    catch (const std::runtime_error& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    *out_replay = replay.release();
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_replay_get_info(const DdReplay* replay, DdReplayInfo* out_info)
{
    DD_GUARD_BEGIN
    if (replay == nullptr || out_info == nullptr || out_info->struct_size != sizeof(DdReplayInfo))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_get_info: replay and a valid out_info are required");
    }
    out_info->frame_count = replay->frameCount;
    out_info->recorded_api_version = replay->apiVersion;
    out_info->recorded_build = replay->build.c_str();
    out_info->same_build = replay->sameBuild ? 1 : 0;
    out_info->up = replay->desc.up;
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_replay_advance(DdReplay* replay, DdReplayFrame* out_frame)
{
    DD_GUARD_BEGIN
    if (replay == nullptr || out_frame == nullptr || out_frame->struct_size != sizeof(DdReplayFrame))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_replay_advance: replay and a valid out_frame are required");
    }
    replay->note.clear();
    out_frame->steps = 0;
    out_frame->host_dt = 0.0f;
    out_frame->hash_matches = 1;
    out_frame->note = nullptr;
    try
    {
        RecordView record{};
        bool stepped = false;
        while (!stepped && nextRecord(replay->file, replay->pos, record))
        {
            Cursor in(record.data, record.size);
            if (record.type == Record::Step)
            {
                const float dt = in.f32();
                const uint32_t recordedSteps = in.u32();
                const uint64_t recordedHash = in.u64();
                out_frame->steps = replay->world->world.step(static_cast<double>(dt));
                out_frame->host_dt = dt;
                out_frame->hash_matches = (out_frame->steps == recordedSteps && hashOf(*replay->world) == recordedHash) ? 1 : 0;
                stepped = true;
            }
            else if (record.type == Record::StepFixed)
            {
                const uint32_t steps = in.u32();
                const uint64_t recordedHash = in.u64();
                replay->world->world.stepFixed(steps);
                out_frame->steps = steps;
                out_frame->host_dt = static_cast<float>(steps) / replay->desc.step_hz;
                out_frame->hash_matches = hashOf(*replay->world) == recordedHash ? 1 : 0;
                stepped = true;
            }
            else
            {
                apply(*replay, record);
            }
        }
        if (stepped)
        {
            ++replay->frame;
            if (out_frame->hash_matches == 0 && replay->firstDivergence == 0)
            {
                replay->firstDivergence = replay->frame;
            }
        }
    }
    catch (const std::runtime_error& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    out_frame->frame = replay->frame;
    out_frame->first_divergence = replay->firstDivergence;
    out_frame->note = replay->note.empty() ? nullptr : replay->note.c_str();
    out_frame->at_end = replay->pos >= replay->file.size() ? 1 : 0;
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdWorld* DD_CALL dragdoll_replay_get_world(DdReplay* replay)
{
    return replay != nullptr ? replay->world.get() : nullptr;
}

DD_API void DD_CALL dragdoll_replay_close(DdReplay* replay)
{
    try
    {
        delete replay;
    }
    catch (...)
    {
        setLastError("dragdoll_replay_close: unexpected internal error");
    }
}

} // extern "C"
