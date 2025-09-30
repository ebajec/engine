#include "utils/monitor.h"
#include <stdio.h>
#include <string.h>
#include <cstdlib>

#ifndef KILOBYTE
#define KILOBYTE 1024
#endif

#define MONITOR_MAX_EVENT_BUFFER 64*KILOBYTE

static int pathcat(char* dest, const char* src, size_t maxlen)
{
    if (!dest || !src) return -1;

    size_t len = strlen(dest);

    if (len < 1) return -1;

    char * c = &dest[len];
    const char * s = src;

    for (; (c > dest) && (*(c-1) == '/'); c--) {}
    for (; (*s == '/'); s++) {}

    int catlen = (int)maxlen - (int)len;

    if (catlen < 0) return -1;

    return snprintf(c,(size_t)catlen,"/%s",s);
}

namespace utils
{

monitor::monitor(void (*callback)(void*, monitor_event_t event), void* usr, const char *dir) 
: m_callback(callback), m_dir(dir), m_usr(usr)
{
#ifdef WIN32
    m_watchEvent = nullptr;
#endif
	m_watching = true;
    m_watcherThread = std::thread(&monitor::watch,this);
}

monitor::~monitor()
{   
    interrupt();

	m_watching = false;
     
    if (m_watcherThread.joinable())
        m_watcherThread.join();
}
};

// Windows implementation
#ifdef WIN32
#include <windows.h>
#include <string>

#define MAX_MSG_LEN MAX_PATH + 100

std::string utf16To8(const std::wstring& utf16String) 
{
    size_t maxUTF8Length = 2*utf16String.length();
    std::string utf8String(maxUTF8Length, 0);

    WideCharToMultiByte(CP_UTF8, 0, utf16String.c_str(), -1, &utf8String[0], (int)maxUTF8Length, NULL, NULL);

    utf8String.shrink_to_fit();

    return utf8String;
}

namespace utils
{

void monitor::interrupt()
{
    m_watching = false;
    SetEvent(m_watchEvent);
}

void monitor::watch()
{
    HANDLE hDir = CreateFileA(
        m_dir.c_str(), 
        GENERIC_READ, 
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 
        NULL, 
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == NULL)
    {
        fprintf(stderr,"ERROR: Failed to access %s\n",m_dir.c_str());
        CloseHandle(hDir);
        return;
    }

    char* resultBuffer  = new char [MONITOR_MAX_EVENT_BUFFER]; 
    char* workingBuffer = new char [MONITOR_MAX_EVENT_BUFFER];
    DWORD bufsize = MONITOR_MAX_EVENT_BUFFER*sizeof(char);

    DWORD notifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME   
                        |FILE_NOTIFY_CHANGE_DIR_NAME    
                        |FILE_NOTIFY_CHANGE_ATTRIBUTES  
                        |FILE_NOTIFY_CHANGE_SIZE        
                        |FILE_NOTIFY_CHANGE_LAST_WRITE  
                        |FILE_NOTIFY_CHANGE_CREATION;   
    DWORD bytesReturned = 0;

    m_watchEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = m_watchEvent;

    if (!ReadDirectoryChangesW(hDir, resultBuffer,bufsize, TRUE, notifyFilter,NULL, &overlapped, NULL))
    {
        printf("ReadDirectoryChangesW failed (%d)\n", GetLastError());
        CloseHandle(hDir);
        CloseHandle(overlapped.hEvent);
        delete[] resultBuffer;
        delete[] workingBuffer;
        return;
    }

    m_watching = true;
    
    while (m_watching)
    {
        WaitForSingleObject(overlapped.hEvent, INFINITE);

        // Return immediately if watch is terminated while waiting
        if (!m_watching) break;
        
        memcpy(workingBuffer,resultBuffer,bufsize);  
        
        ResetEvent(overlapped.hEvent);
        memset(resultBuffer,0,bufsize);
        if (!ReadDirectoryChangesW(hDir, resultBuffer,bufsize, TRUE, notifyFilter,&bytesReturned, &overlapped, NULL))
        {
            printf("ReadDirectoryChangesW failed (%d)\n", GetLastError());
            CloseHandle(hDir);
            CloseHandle(overlapped.hEvent);
            return;
        }
    
        _FILE_NOTIFY_INFORMATION* result = (_FILE_NOTIFY_INFORMATION*)workingBuffer;
        int nextOffset;
        do {
            WCHAR filename[MAX_PATH];
            wcsncpy(filename, result->FileName, result->FileNameLength / sizeof(WCHAR));
            filename[result->FileNameLength / sizeof(WCHAR)] = L'\0';

            // Convert windows utf-16 string to utf-8 
            std::string filenameUTF8 = utf16To8(std::wstring(filename));

            DWORD action = result->Action;

            int flags = 0;
            char path[MAX_PATH];
            snprintf(path,sizeof(path),"%s",m_dir.c_str());
            pathcat(path,filenameUTF8.c_str(),sizeof(path));

            DWORD attributes = GetFileAttributesA(path);

            if (action == FILE_ACTION_ADDED)
                flags |= MONITOR_FLAGS_CREATE;
            if (action == FILE_ACTION_REMOVED)
                flags |= MONITOR_FLAGS_DELETE;
            if (action == FILE_ACTION_MODIFIED)
                flags |= MONITOR_FLAGS_MODIFY;
            if (action == FILE_ACTION_RENAMED_OLD_NAME)
                flags |= MONITOR_FLAGS_DELETE;
            if (action == FILE_ACTION_RENAMED_NEW_NAME)
                flags |= MONITOR_FLAGS_CREATE;
            if (attributes & FILE_ATTRIBUTE_DIRECTORY)
                flags |= MONITOR_FLAGS_ISDIR;

            monitor_event_t monitorEvent = {path,flags};
            

            if (m_callback) m_callback(m_usr,monitorEvent);
            
            nextOffset = result->NextEntryOffset;
            result = (FILE_NOTIFY_INFORMATION*)((char*)result + nextOffset);
        } while (nextOffset != 0); 

        
    }

    CloseHandle(hDir);
    CloseHandle(overlapped.hEvent);
    delete[] resultBuffer;
    delete[] workingBuffer;
}
};
#endif

// Linux implementation
#ifdef __linux__

// linux kernel
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

// C
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// C++
#include <string>
#include <unordered_map>

// Misc
#include "tinydir.h"


static int add_watch_recursive(int inotifyFd, std::unordered_map<int,std::string>& watches, const char * path, uint32_t flags)
{
    if (!path) 
        return 0;

    tinydir_dir dir;

    if (tinydir_open(&dir,path) == -1) 
        return 0;

    int wd = inotify_add_watch(inotifyFd, path ,flags);

    watches[wd] = std::string(path);

    if (wd == -1) 
    {
        perror("inotify_add_watch");
        return 0;
    }

    while (dir.has_next)
    {
        tinydir_file file;
        tinydir_readfile(&dir, &file);

        if (file.is_dir && strcmp(file.name,".") && strcmp(file.name,".."))
            add_watch_recursive(inotifyFd, watches, file.path, flags);

        tinydir_next(&dir);
    }

    tinydir_close(&dir);
    return 1;
}

static void interrupt_handler(int) 
{
    
}

namespace utils
{

void monitor::watch() {
    int inotifyFd;
    inotifyFd = inotify_init1(IN_NONBLOCK);
    ssize_t numRead;
    struct inotify_event* event;

    if (inotifyFd < 0)
    {
        perror("inotify_init");
        return;
    }

	std::unique_ptr<char[]> buffer (new (std::align_val_t{alignof(struct inotify_event)}) char[MONITOR_MAX_EVENT_BUFFER]);

    uint32_t watchFlags =
      IN_CREATE 
    | IN_DELETE 
    | IN_MODIFY 
    | IN_MOVED_TO 
    | IN_MOVED_FROM;
    

    struct sigaction sa;
    sa.sa_handler = interrupt_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("sigaction");
        return;
    }

    struct pollfd fds[1];
    fds[0].fd = inotifyFd;
    fds[0].events = POLLIN;

    std::unordered_map<int,std::string> activeWatches;

    add_watch_recursive(inotifyFd, activeWatches, m_dir.c_str(), watchFlags);

    while (m_watching)
    {
        int pollNum = poll(fds, 1, 1000);  // Timeout after 1 second
        if (pollNum < 0) {
            if (errno == EINTR) {
                printf("Poll interrupted by signal\n");
                break;
            }
            perror("poll");
            break;
        }

        if (pollNum == 0) {
            // Timeout - continue loop
            continue;
        }

        if (! (fds[0].revents & POLLIN)) 
        {
            continue;
        }

        // TODO: robust handling for overflows

		numRead = read(inotifyFd, buffer.get(), MONITOR_MAX_EVENT_BUFFER*sizeof(char));
        if (numRead < 0)
        {
            if (errno == EINTR)
            {
                printf("Read interrupted by signal\n");
                break;
            }
            else 
            {
                perror("read");
                break;
            }
        }

		for (char* p = buffer.get(); p < buffer.get() + numRead; ) 
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
            event = (struct inotify_event *) p;
#pragma clang diagnostic pop

            if (!activeWatches.count(event->wd))
            {
                fprintf(stderr, "ERROR: Watch not tracked for %d\n",event->wd);
                continue;
            }

            char path[PATH_MAX] = {0};
            int flags = 0;

            snprintf(path,sizeof(path),"%s",activeWatches[event->wd].c_str());
            pathcat(path,event->name,sizeof(path));

            if (event->mask & IN_CREATE)
                flags |= MONITOR_FLAGS_CREATE;
            if (event->mask & IN_DELETE)
                flags |= MONITOR_FLAGS_DELETE;
            if (event->mask & IN_MODIFY)
                flags |= MONITOR_FLAGS_MODIFY;
            if (event->mask & IN_ISDIR)
                flags |= MONITOR_FLAGS_ISDIR;
            if (event->mask & IN_MOVED_TO)
                flags |= MONITOR_FLAGS_CREATE;
            if (event->mask & IN_MOVED_FROM)
                flags |= MONITOR_FLAGS_DELETE;
            if (event->mask & IN_CLOSE_WRITE)
                flags |= MONITOR_FLAGS_CREATE;

            if (event->mask & IN_CREATE && event->mask & IN_ISDIR)
            {
                add_watch_recursive(inotifyFd, activeWatches, path, IN_CREATE | IN_DELETE | IN_MODIFY);            
            }
            else if (event->mask & IN_IGNORED)
            {
                inotify_rm_watch(inotifyFd, event->wd);
                activeWatches.erase(event->wd);
            }

            monitor_event_t result = {path,flags};
            
            if (m_callback && !(event->mask & IN_IGNORED)) 
                m_callback(m_usr,result);

            p += sizeof(struct inotify_event) + event->len;
        }
    }

    close(inotifyFd);
}

void monitor::interrupt()
{
    m_watching = false;
    //pthread_kill(m_watcherThread.native_handle(),SIGINT);
}

};
#endif
