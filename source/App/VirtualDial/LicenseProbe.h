#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <thread>
#include <atomic>
#pragma comment(lib,"bcrypt.lib")

namespace dial {
// Launch commerce at the interactive shell's ordinary integrity level.
inline bool LaunchUserProcess(const std::wstring& exe,const std::wstring& args,PROCESS_INFORMATION& process) {
    DWORD pid=0;GetWindowThreadProcessId(GetShellWindow(),&pid);
    HANDLE shell=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid),token=nullptr,primary=nullptr;
    bool ok=false;
    if(shell&&OpenProcessToken(shell,TOKEN_QUERY|TOKEN_DUPLICATE,&token)&&
       DuplicateTokenEx(token,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY,nullptr,SecurityImpersonation,TokenPrimary,&primary)) {
        auto command=L"\""+exe+L"\" "+args;
        STARTUPINFOW startup={};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        ok=CreateProcessWithTokenW(primary,LOGON_WITH_PROFILE,exe.c_str(),command.data(),CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE;
    }
    if(primary)CloseHandle(primary);if(token)CloseHandle(token);if(shell)CloseHandle(shell);
    return ok;
}

class LicenseProbe {
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread worker;
    std::atomic<bool> running{false},owned{false};
    bool Wait(HANDLE event,DWORD milliseconds) {
        HANDLE events[]={stop,event};
        return WaitForMultipleObjects(2,events,FALSE,milliseconds)==WAIT_OBJECT_0+1;
    }
    bool Query(const std::wstring& exe) {
        unsigned char random[16]={};
        if(BCryptGenRandom(nullptr,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return false;
        const wchar_t* hex=L"0123456789abcdef";
        auto name=L"CirDial.License."+std::to_wstring(GetCurrentProcessId())+L".";
        for(auto b:random){name+=hex[b>>4];name+=hex[b&15];}
        auto path=L"\\\\.\\pipe\\"+name;
        HANDLE pipe=CreateNamedPipeW(path.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,0,1,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)return false;
        HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event){CloseHandle(pipe);return false;}
        OVERLAPPED io={};io.hEvent=event;
        bool ok=false;
        PROCESS_INFORMATION process={};
        if(LaunchUserProcess(exe,L"--license-check "+name,process)) {
            CloseHandle(process.hThread);
            bool connected=ConnectNamedPipe(pipe,&io)!=FALSE;
            DWORD error=connected?ERROR_SUCCESS:GetLastError();
            if(error==ERROR_PIPE_CONNECTED)connected=true;
            else if(error==ERROR_IO_PENDING&&Wait(event,15000)) {
                DWORD count=0;connected=GetOverlappedResult(pipe,&io,&count,FALSE)!=FALSE;
            }
            ULONG client=0;
            if(connected&&GetNamedPipeClientProcessId(pipe,&client)&&client==process.dwProcessId) {
                ResetEvent(event);io={};io.hEvent=event;
                BYTE value=0;DWORD count=0;
                bool read=ReadFile(pipe,&value,1,&count,&io)!=FALSE;
                if(!read&&GetLastError()==ERROR_IO_PENDING&&Wait(event,5000))
                    read=GetOverlappedResult(pipe,&io,&count,FALSE)!=FALSE;
                ok=read&&count==1&&value==1;
                if(!read){CancelIoEx(pipe,&io);DWORD ignored=0;GetOverlappedResult(pipe,&io,&ignored,TRUE);}
            }
            CloseHandle(process.hProcess);
        }
        CancelIoEx(pipe,nullptr);
        // OVERLAPPED storage must outlive any pending connection operation.
        DWORD ignored=0;GetOverlappedResult(pipe,&io,&ignored,TRUE);
        CloseHandle(event);CloseHandle(pipe);return ok;
    }
public:
    ~LicenseProbe(){if(stop)SetEvent(stop);if(worker.joinable())worker.join();if(stop)CloseHandle(stop);}
    bool Owned()const{return owned.load();}
    bool Busy()const{return running.load();}
    void Begin(HWND target,const std::wstring& exe) {
        if(!stop||running.exchange(true))return;
        if(worker.joinable())worker.join();
        worker=std::thread([this,target,exe]{
            owned=Query(exe);running=false;
            PostMessageW(target,WM_APP+6,0,0);
        });
    }
};
}
