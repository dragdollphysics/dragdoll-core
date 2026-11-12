// SPDX-License-Identifier: MIT
// Shared helpers for the C ABI boundary (ADR-0020): thread-local last error + try/catch guards.
#pragma once

#include <dragdoll/dragdoll.h>

#include <exception>

namespace dragdoll::detail
{
void setLastError(const char* message) noexcept;
void clearLastError() noexcept;
const char* lastError() noexcept;

bool isLibraryInitialized() noexcept;

// Record an error message and return the given result code.
inline DdResult fail(DdResult code, const char* message) noexcept
{
    setLastError(message);
    return code;
}
} // namespace dragdoll::detail

// Wrap the body of every exported function: no exception may cross into a host.
#define DD_GUARD_BEGIN                                                                                                 \
    try                                                                                                                \
    {
#define DD_GUARD_END(failValue)                                                                                        \
    }                                                                                                                  \
    catch (const std::bad_alloc&)                                                                                      \
    {                                                                                                                  \
        ::dragdoll::detail::setLastError("out of memory");                                                             \
        return failValue;                                                                                              \
    }                                                                                                                  \
    catch (const std::exception& e)                                                                                    \
    {                                                                                                                  \
        ::dragdoll::detail::setLastError(e.what());                                                                    \
        return failValue;                                                                                              \
    }                                                                                                                  \
    catch (...)                                                                                                        \
    {                                                                                                                  \
        ::dragdoll::detail::setLastError("unknown internal error");                                                    \
        return failValue;                                                                                              \
    }
