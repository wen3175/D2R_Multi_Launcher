#include <Windows.h>
#include <filesystem>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <winternl.h>
#include <ntstatus.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <tchar.h>


typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO
{
    USHORT ProcessId;
    USHORT CreatorBackTraceIndex;
    UCHAR ObjectTypeIndex;
    UCHAR HandleAttributes;
    USHORT Handle;
    PVOID Object;
    ULONG GrantedAccess;
} SYSTEM_HANDLE_TABLE_ENTRY_INFO, * PSYSTEM_HANDLE_TABLE_ENTRY_INFO;

typedef struct _SYSTEM_HANDLE_INFORMATION
{
    ULONG_PTR HandleCount;
    SYSTEM_HANDLE_TABLE_ENTRY_INFO Handles[1];
} SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

const OBJECT_INFORMATION_CLASS ObjectNameInformation = static_cast<OBJECT_INFORMATION_CLASS>(1);


#pragma comment(lib, "ntdll.lib")

typedef NTSTATUS(NTAPI* _NtQuerySystemInformation)(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID                    SystemInformation,
    ULONG                    SystemInformationLength,
    PULONG                   ReturnLength
    );

typedef NTSTATUS(NTAPI* _NtQueryObject)(
    HANDLE                   Handle,
    OBJECT_INFORMATION_CLASS ObjectInformationClass,
    PVOID                    ObjectInformation,
    ULONG                    ObjectInformationLength,
    PULONG                   ReturnLength
    );

typedef struct _OBJECT_NAME_INFORMATION {
    UNICODE_STRING Name;
} OBJECT_NAME_INFORMATION, * POBJECT_NAME_INFORMATION;


const SYSTEM_INFORMATION_CLASS SystemHandleInformation = static_cast<SYSTEM_INFORMATION_CLASS>(16);

_NtQuerySystemInformation NtQSI = (_NtQuerySystemInformation)GetProcAddress(GetModuleHandle(L"ntdll"), "NtQuerySystemInformation");
_NtQueryObject NtQO = (_NtQueryObject)GetProcAddress(GetModuleHandle(L"ntdll"), "NtQueryObject");


DWORD GetProcessIdByName(const wchar_t* processName) {
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return 0;

    if (Process32First(hSnapshot, &pe32)) {
        do {
            if (wcscmp(pe32.szExeFile, processName) == 0) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);
    return 0;
}

std::vector<HANDLE> EnumerateProcessHandles(DWORD processId) {
    std::vector<HANDLE> handles;
    ULONG bufferSize = 0x10000;
    std::vector<BYTE> buffer(bufferSize);
    PSYSTEM_HANDLE_INFORMATION handleInfo = nullptr;
    NTSTATUS status;

    do {
        handleInfo = reinterpret_cast<PSYSTEM_HANDLE_INFORMATION>(buffer.data());
        status = NtQuerySystemInformation(SystemHandleInformation, handleInfo, bufferSize, &bufferSize);
        if (status == STATUS_INFO_LENGTH_MISMATCH) {
            buffer.resize(bufferSize * 2);
        }
        else if (!NT_SUCCESS(status)) {
            return handles;
        }
    } while (status == STATUS_INFO_LENGTH_MISMATCH);

    for (size_t i = 0; i < handleInfo->HandleCount; i++) {
        if (handleInfo->Handles[i].ProcessId == processId) {
            handles.push_back((HANDLE)handleInfo->Handles[i].Handle);
        }
    }

    return handles;
}



std::wstring GetObjectName(HANDLE handle, HANDLE processHandle) {
    UCHAR buffer[0x200];
    ULONG returnLength = 0;
    POBJECT_NAME_INFORMATION objectNameInfo = (POBJECT_NAME_INFORMATION)buffer;

    HANDLE dupHandle;
    if (!DuplicateHandle(processHandle, handle, GetCurrentProcess(), &dupHandle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return L"";
    }

    NTSTATUS status = NtQueryObject(dupHandle, ObjectNameInformation, objectNameInfo, sizeof(buffer) - sizeof(WCHAR), &returnLength);
    if (!NT_SUCCESS(status) || objectNameInfo->Name.Buffer == nullptr || objectNameInfo->Name.Length == 0) {
        CloseHandle(dupHandle);
        return L"";
    }

    objectNameInfo->Name.Buffer[objectNameInfo->Name.Length / sizeof(WCHAR)] = L'\0';
    std::wstring objectName = objectNameInfo->Name.Buffer;
    CloseHandle(dupHandle);
    return objectName;
}

bool CloseRemoteHandle(HANDLE processHandle, HANDLE targetHandle) {
    // Get the address of CloseHandle function in kernel32.dll
    FARPROC closeHandleAddress = GetProcAddress(GetModuleHandle(L"kernel32.dll"), "CloseHandle");

    // Create a remote thread in the target process
    HANDLE remoteThread = CreateRemoteThread(processHandle, nullptr, 0, (LPTHREAD_START_ROUTINE)closeHandleAddress, targetHandle, 0, nullptr);

    if (remoteThread == nullptr) {
        std::cout << "创建远程线程失败，错误代码：" << GetLastError() << std::endl;
        return false;
    }

    // Wait for the remote thread to finish execution
    WaitForSingleObject(remoteThread, INFINITE);

    // Check the exit code of the remote thread
    DWORD exitCode;
    if (GetExitCodeThread(remoteThread, &exitCode)) {
        if (exitCode == 0) {
            std::cout << "事件句柄已成功关闭。" << std::endl;
        }
        else {
            std::cout << "关闭事件句柄失败，错误代码：" << exitCode << std::endl;
        }
    }
    else {
        std::cout << "获取远程线程退出代码失败，错误代码：" << GetLastError() << std::endl;
    }

    CloseHandle(remoteThread);

    return exitCode == 0;
}

std::wstring getGamePath()
{
    HKEY hKey;
    LPCTSTR subKey = _T("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Diablo II Resurrected");
    LPCTSTR valueName = _T("DisplayIcon");
    DWORD dwType = REG_SZ;
    BYTE data[1024];
    std::wstring displayIconValue;
    DWORD dataSize = sizeof(data);

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, subKey, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, valueName, NULL, &dwType, data, &dataSize) == ERROR_SUCCESS)
        {
            displayIconValue = reinterpret_cast<wchar_t*>(data);
            std::wcout << _T("DisplayIcon: ") << displayIconValue << std::endl;
        }
        else
        {
            std::cout << "Failed to query the registry key value." << std::endl;
        }

        RegCloseKey(hKey);
    }
    else
    {
        std::cout << "Failed to open the registry key." << std::endl;
    }

    return displayIconValue;
}

std::wstring getAddressByRegion(std::wstring region) {
    if (region == L"us") {
        return L"us.actual.battle.net";
    }
    else if (region == L"eu") {
        return L"eu.actual.battle.net";
    }
    else {
        // Default to KR
        return L"kr.actual.battle.net";
    }
}

bool fileExists(const wchar_t* fileName) {
    DWORD attributes = GetFileAttributes(fileName);
    return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}


int main(int argc, char* argv[]) {
    const wchar_t* processName = L"D2R.exe";
    const wchar_t* targetHandleName = L"\\Sessions\\4\\BaseNamedObjects\\DiabloII Check For Other Instances";

    //std::wstring configFileName = L"D:\\Source\\D2R_LauncherCMD\\x64\\Debug\\account.txt";
    //std::wstring configFileName = L"账号配置.txt";

    std::wstring configFileName;
  
    if (fileExists(L"账号配置.txt")) {
        configFileName = L"账号配置.txt";
    }
    else if (fileExists(L"account.txt")) {
        configFileName = L"account.txt";
    }
    else {
        std::cout << "无法找到账号配置文件。" << std::endl;
        return 1;
    }
 

    std::wstring gamePath = getGamePath();

    std::wifstream configFile(configFileName);
    if (!configFile.is_open()) {
        std::cout << "无法打开账号配置文件。" << std::endl;
        return 1;
    }
    std::wstring region = L"kr"; // 默认为kr
    std::wstring line;
    while (std::getline(configFile, line)) {
        std::wstringstream lineStream(line);
        std::wstring username, password, region;
        std::getline(lineStream, username, L',');
        std::getline(lineStream, password, L',');
        std::getline(lineStream, region, L',');


        std::wstring commandLineTemplate = L"\"" + gamePath + L"\" -mod wing -uid osi -launcher -username USERNAMEINPUT -password PASSWORDINPUT -address " + getAddressByRegion(region);
        std::wstring commandLine = commandLineTemplate;
        size_t usernamePos = commandLine.find(L"USERNAMEINPUT");
        commandLine.replace(usernamePos, wcslen(L"USERNAMEINPUT"), username);
        size_t passwordPos = commandLine.find(L"PASSWORDINPUT");
        commandLine.replace(passwordPos, wcslen(L"PASSWORDINPUT"), password);


        STARTUPINFO si = { sizeof(STARTUPINFO) };
        PROCESS_INFORMATION pi;

        if (CreateProcess(NULL, (LPWSTR)commandLine.c_str(), NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);

            WaitForSingleObject(pi.hProcess, 1000);

            DWORD processId = pi.dwProcessId;
            HANDLE hProcess = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_CREATE_THREAD, FALSE, processId);
            if (hProcess == NULL) {
                std::cout << "无法打开目标进程。错误代码：" << GetLastError() << std::endl;
                CloseHandle(pi.hProcess);
                continue;
            }

            std::vector<HANDLE> handles = EnumerateProcessHandles(processId);
            HANDLE targetHandle = NULL;

            for (HANDLE handle : handles) {
                std::wstring objectName = GetObjectName(handle, hProcess);
                if (objectName == targetHandleName) {
                    targetHandle = handle;
                    break;
                }
            }

            if (targetHandle != NULL) {
                if (CloseRemoteHandle(hProcess, targetHandle)) {
                    std::cout << "事件句柄已成功关闭。" << std::endl;
                }
                else {
                    std::cout << "关闭事件句柄失败。" << std::endl;
                }
            }
            else {
                std::cout << "找不到目标句柄。" << std::endl;
            }

            CloseHandle(hProcess);
            CloseHandle(pi.hProcess);

            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
        else {
            std::wcout << L"启动游戏进程失败。错误代码：" << GetLastError() << std::endl;
        }
    }

    configFile.close();
    return 0;
}
