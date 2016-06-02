﻿#include "remote_inject.h"

#include <base/basictypes.h>
#include <base/files/file_path.h>

#include "../privilege/promote_privilege.h"

using base::win::ScopedHandle;

namespace
{
const wchar_t* InjectDllName = L"quirky.dll";
}

RemoteInject::RemoteInject()
{

}

RemoteInject::~RemoteInject()
{

}

bool RemoteInject::InjectDll(InjectType type)
{
    std::wstring str = L"MyCmd.exe";
    InjectDllByRemoteThread(str);
    EjectDllByRemoteThread(str);
    return true;
}

bool RemoteInject::InjectDllByRemoteThread(const std::wstring& procName)
{
    int processID = 0;
    if (!plug::ProcessSnapshoot(procName, &processID) && (processID == 0))
        return false;

    ScopedHandle tagerProcess(plug::OpenProcess(processID));
    if (!tagerProcess.IsValid())
    {
        if (plug::PromotePrivilege())
        {
            tagerProcess.Set(plug::OpenProcess(processID));
            if (!tagerProcess.IsValid())
                return false;
        }
    }

    wchar_t dllpath[MAX_PATH] = { 0 };
    ::GetModuleFileName(NULL, dllpath, MAX_PATH);
    base::FilePath filepath(dllpath);
    base::FilePath p = filepath.DirName();
    p = p.Append(InjectDllName);
    uint32 strBuffer = (p.value().length() + 1) * sizeof(wchar_t);
    void* strBufPoint = ::VirtualAllocEx(tagerProcess.Get(), NULL, strBuffer,
                                         MEM_COMMIT | MEM_RESERVE,
                                         PAGE_READWRITE);
    if (!strBufPoint)
    {
        return false;
    }


    SIZE_T out = 0;
    wcscpy_s(dllpath, strBuffer, p.value().c_str());
    if (!::WriteProcessMemory(tagerProcess.Get(), strBufPoint,
                              reinterpret_cast<void*>(&dllpath[0]), strBuffer,
                              &out))
    {
        ::VirtualFreeEx(tagerProcess.Get(), strBufPoint, strBuffer, MEM_DECOMMIT);
        return false;
    }
    
    LoadLibraryAddres LoadLibraryObject =
        reinterpret_cast<LoadLibraryAddres>(
            ::GetProcAddress(::GetModuleHandle(L"Kernel32.dll"), "LoadLibraryW"));

    NtCreateThreadEx NtCreateThreadObject =
        reinterpret_cast<NtCreateThreadEx>(
        ::GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
    if (NtCreateThreadObject)
    {
        OnNtCreateThread(LoadLibraryObject, NtCreateThreadObject, tagerProcess,
                         strBufPoint);
    }
    else
    {
        ScopedHandle remote(::CreateRemoteThread(
            tagerProcess.Get(), NULL, NULL,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryObject),
            strBufPoint, NULL, NULL));
        DWORD err = ::GetLastError();
        if (!remote.IsValid())
        {
            ::VirtualFreeEx(tagerProcess.Get(), strBufPoint, strBuffer,
                            MEM_DECOMMIT);
            return false;
        }

        ::WaitForSingleObject(remote.Get(), INFINITE);
    }

    ::VirtualFreeEx(tagerProcess.Get(), strBufPoint, strBuffer, MEM_DECOMMIT);
    return true;
}

bool RemoteInject::InjectDllByHook()
{
    return true;
}

bool RemoteInject::InjectDllByRegedit()
{
    return true;
}

void RemoteInject::OnNtCreateThread(const LoadLibraryAddres& proc,
                                    const NtCreateThreadEx& NtCreateThreadObject,
                                    const base::win::ScopedHandle& handle,
                                    void* param)
{
    plug::PromotePrivilege();
    struct UNKNOWN
    {
        ULONG Length;
        ULONG Unknown1;
        ULONG Unknown2;
        PULONG Unknown3;
        ULONG Unknown4;
        ULONG Unknown5;
        ULONG Unknown6;
        PULONG Unknown7;
        ULONG Unknown8;
    };

    UNKNOWN buffer = { 0 };
    memset(&buffer, 0, sizeof(buffer));
    DWORD dw0 = 0;
    DWORD dw1 = 0;

    buffer.Length = sizeof(buffer);
    buffer.Unknown1 = 0x10003;
    buffer.Unknown2 = 0x8;
    buffer.Unknown3 = &dw1;
    buffer.Unknown4 = 0;
    buffer.Unknown5 = 0x10004;
    buffer.Unknown6 = 4;
    buffer.Unknown7 = &dw0;
    buffer.Unknown8 = 0;

    if (NtCreateThreadObject)
    {
        HANDLE remote = NULL;
        NtCreateThreadObject(&remote, 0x1FFFFF, NULL, handle.Get(),
                             reinterpret_cast<LPTHREAD_START_ROUTINE>(proc),
                             param, FALSE, NULL, NULL, NULL, NULL);
        ::WaitForSingleObject(remote, INFINITE);
        ::CloseHandle(remote);
    }
}

bool RemoteInject::EjectDllByRemoteThread(const std::wstring& procName)
{
    int processID = 0;
    if (!plug::ProcessSnapshoot(procName, &processID) && (processID == 0))
        return false;

    ScopedHandle tagerProcess(plug::OpenProcess(processID));
    if (!tagerProcess.IsValid())
    {
        if (plug::PromotePrivilege())
        {
            tagerProcess.Set(plug::OpenProcess(processID));
            if (!tagerProcess.IsValid())
                return false;
        }
    }

    BYTE* baseAddress = plug::ProcessSnapshootModule(InjectDllName, processID);
    if (!baseAddress)
        return false;

    LoadLibraryAddres LoadLibraryObject =
        reinterpret_cast<LoadLibraryAddres>(
        ::GetProcAddress(::GetModuleHandle(L"Kernel32.dll"), "FreeLibrary"));

    NtCreateThreadEx NtCreateThreadObject =
        reinterpret_cast<NtCreateThreadEx>(
        ::GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
    if (NtCreateThreadObject)
    {
        OnNtCreateThread(LoadLibraryObject, NtCreateThreadObject, tagerProcess,
                         reinterpret_cast<void*>(baseAddress));
    }
    else
    {
        ScopedHandle remote(::CreateRemoteThread(
            tagerProcess.Get(), NULL, NULL,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryObject),
            reinterpret_cast<void*>(baseAddress), NULL, NULL));
        if (remote.IsValid())
            ::WaitForSingleObject(remote.Get(), INFINITE);
    }
}

