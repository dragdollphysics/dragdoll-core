// SPDX-License-Identifier: MIT
// C ABI: characters (dragdoll.h).

#include "capi_guard.hpp"
#include "capi_world_handle.hpp"
#include "character.hpp"

#include <cstring>
#include <stdexcept>

using namespace dragdoll::detail;

extern "C"
{

DD_API const char* DD_CALL dragdoll_get_builtin_rig(const char* name)
{
    return name != nullptr ? dragdoll::builtinRig(name) : nullptr;
}

DD_API void DD_CALL dragdoll_character_desc_default(DdCharacterDesc* desc)
{
    if (desc == nullptr)
    {
        return;
    }
    *desc = DdCharacterDesc{};
    desc->struct_size = sizeof(DdCharacterDesc);
    desc->transform.rotation = DdQuat{0.0f, 0.0f, 0.0f, 1.0f};
    desc->mass_scale = 1.0f;
}

DD_API DdResult DD_CALL dragdoll_character_create(DdWorld* world, const DdCharacterDesc* desc, DdCharacter** out_character)
{
    DD_GUARD_BEGIN
    clearLastError();
    if (out_character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_create: out_character is required");
    }
    *out_character = nullptr;
    if (world == nullptr || desc == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_create: world and desc are required");
    }
    try
    {
        *out_character = world->world.createCharacter(*desc);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_character_destroy(DdCharacter* character)
{
    try
    {
        if (character != nullptr)
        {
            character->world->destroyCharacter(character);
        }
    }
    catch (...)
    {
        setLastError("dragdoll_character_destroy: unexpected internal error");
    }
}

DD_API uint32_t DD_CALL dragdoll_character_get_body_count(const DdCharacter* character)
{
    return character != nullptr ? static_cast<uint32_t>(character->names.size()) : 0;
}

DD_API const char* DD_CALL dragdoll_character_get_body_name(const DdCharacter* character, uint32_t body)
{
    if (character == nullptr || body >= character->names.size())
    {
        return nullptr;
    }
    return character->names[body].c_str();
}

DD_API int32_t DD_CALL dragdoll_character_find_body(const DdCharacter* character, const char* name)
{
    if (character == nullptr || name == nullptr)
    {
        return -1;
    }
    for (size_t i = 0; i < character->names.size(); ++i)
    {
        if (std::strcmp(character->names[i].c_str(), name) == 0)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

DD_API float DD_CALL dragdoll_character_get_mass(const DdCharacter* character)
{
    return character != nullptr ? character->totalMass : 0.0f;
}

DD_API DdResult DD_CALL dragdoll_character_get_body_transforms(const DdCharacter* character, int interpolated,
                                                               DdTransform* out, uint32_t capacity)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_body_transforms: character and out are required");
    }
    if (!character->world->characterTransforms(*character, interpolated != 0, out, capacity))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_body_transforms: capacity < body count");
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_apply_impulse(DdCharacter* character, uint32_t body, DdVec3 impulse,
                                                         const DdVec3* world_point)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_apply_impulse: character is required");
    }
    try
    {
        character->world->applyImpulse(*character, body, impulse, world_point);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

} // extern "C"
