#include "stdafx.h"

#include <cstring>

void SetDebugMode(DWORD) {}
void DebugPrint(CString) {}
BOOL CheckCodeSign(LPCWSTR, LPCWSTR) { return FALSE; }

ULONGLONG GetTickCountFx()
{
    return ::GetTickCount64();
}

ULONG64 B8toB64(BYTE b0, BYTE b1, BYTE b2, BYTE b3, BYTE b4, BYTE b5, BYTE b6, BYTE b7)
{
    return static_cast<ULONG64>(b0)
        | (static_cast<ULONG64>(b1) << 8)
        | (static_cast<ULONG64>(b2) << 16)
        | (static_cast<ULONG64>(b3) << 24)
        | (static_cast<ULONG64>(b4) << 32)
        | (static_cast<ULONG64>(b5) << 40)
        | (static_cast<ULONG64>(b6) << 48)
        | (static_cast<ULONG64>(b7) << 56);
}

DWORD B8toB32(BYTE b0, BYTE b1, BYTE b2, BYTE b3)
{
    return static_cast<DWORD>(b0)
        | (static_cast<DWORD>(b1) << 8)
        | (static_cast<DWORD>(b2) << 16)
        | (static_cast<DWORD>(b3) << 24);
}

template <typename T>
static T ReadLittleEndian(const BYTE* value) noexcept
{
    T result{};
    std::memcpy(&result, value, sizeof(result));
    return result;
}

ULONG64 B8toB64le_ptr(const BYTE* value) noexcept { return ReadLittleEndian<ULONG64>(value); }
DWORD B8toB32le_ptr(const BYTE* value) noexcept { return ReadLittleEndian<DWORD>(value); }
INT B8toINTle_ptr(const BYTE* value) noexcept { return ReadLittleEndian<INT>(value); }
USHORT B8toB16le_ptr(const BYTE* value) noexcept { return ReadLittleEndian<USHORT>(value); }
SHORT B8toSHORTle_ptr(const BYTE* value) noexcept { return ReadLittleEndian<SHORT>(value); }

ULONG64 B8toB64le(const BYTE (&value)[6]) noexcept
{
    return B8toB64(value[0], value[1], value[2], value[3], value[4], value[5]);
}

DWORD B8toB32le(const BYTE (&value)[6]) noexcept
{
    return B8toB32(value[0], value[1], value[2], value[3]);
}

INT B8toINTle(const BYTE (&value)[6]) noexcept
{
    return static_cast<INT>(B8toB32le(value));
}

USHORT B8toB16le(const BYTE (&value)[6]) noexcept
{
    return static_cast<USHORT>(value[0] | (static_cast<USHORT>(value[1]) << 8));
}

BOOL IsWindowsVersionOrGreaterFx(WORD major, WORD minor, WORD servicePack)
{
    OSVERSIONINFOEXW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    info.dwMajorVersion = major;
    info.dwMinorVersion = minor;
    info.wServicePackMajor = servicePack;
    const DWORDLONG mask = VerSetConditionMask(
        VerSetConditionMask(
            VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL),
            VER_MINORVERSION, VER_GREATER_EQUAL),
        VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
    return VerifyVersionInfoW(&info,
        VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, mask);
}

