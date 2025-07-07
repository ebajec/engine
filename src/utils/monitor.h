#ifndef MONITOR_HPP
#define MONITOR_HPP

#include <thread>
#include <string>

#ifdef WIN32
#include <atomic>
#endif 

#ifndef KILOBYTE
#define KILOBYTE 1024
#endif

#define MONITOR_MAX_EVENT_BUFFER 64*KILOBYTE

#define MONITOR_FLAGS_CREATE 0x01
#define MONITOR_FLAGS_DELETE 0x02
#define MONITOR_FLAGS_MODIFY 0x04
#define MONITOR_FLAGS_ISDIR  0x08

namespace utils
{

/**
* Wrapper for filesystem event info.  Passed to the callback set for
* an active Monitor when a change is detected.
*/
struct monitor_event_t
{
    const char* path;
    int flags;
};

/**
 * File monitor class.  Watches a given directory and subdirectories, and generates
 * a callback each time a change is detected in the filesystem. 
 * 
 * For each callback, the path to the changed file/directory is given, along with a set
 * of flags indicating the operation (see MonitorEvent).
 *
 * NOTE: Implementated with OS events, so changes across filesystems will NOT be detected. 
 * I.e., changes made in the windows file explorer would not be detected if your program is 
 * running in WSL. 
 * 
 * Additionally, when using WSL, there is a weird bug with inotify where directories created on the
 * windows filesystem during runtime are not watched. May be an issue with inotify on mounted filesystems;
 * for more info see https://man7.org/linux/man-pages/man7/inotify.7.html.
 *
 */
class monitor
{
public:
    typedef void(*callback_t)(void*, monitor_event_t);
    /**
     * Creates a new monitor for the given directory.
     * 
     * @param callback - Callback to be generated each time a change is detected. Executed in
     * the order of events as they appear, and blocks the monitoring thread until completed. 
     * However, any changes occuring while the callback is running will still be detected.  
     *
     * @param usr - User data to be passed to the callback.
     *
     * @param dir - Path to directory to be monitored.
     *
     */
    monitor(callback_t callback, void* usr, const char *dir);

    /**
    * On deletion, the monitoring thread is interrupted after it completes its current update. 
    */
    ~monitor();

private:
    void watch();
    void interrupt();

    void              (*m_callback)(void*, monitor_event_t event);
    std::string       m_dir;
    void*             m_usr;
    std::thread       m_watcherThread;

    std::atomic<bool> m_watching;
#ifdef WIN32
    void*             m_watchEvent;   
#endif

};

};

#endif
