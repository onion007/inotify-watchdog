#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/time.h>
#include <syslog.h>
#include <pthread.h>
#include "tinyxml.h"
#include "tinystr.h"
#include "dlist.h"

#define VERSION "1.2.4"
#define INOTIFY_WATCH_CONFIG_FILELIST "/etc/inotify.conf"
#define INOTIFY_LOG_FILE "/var/log/inotify.log"
#define WATCHROOT "WATCHS"
#define WATCH "WATCH"
#define WATCHATTR "cmd"
#define WATCHPATH "PATH"
#define WATCHTIME "ITIME"
#define MAX_INTERVALTIME 3600
#define MIN_INTERVALTIME 3
#define MAX_BUFFER_SIZE 2048
#define FPG_INOTIFY_EVENT IN_CLOSE_WRITE|IN_CREATE|IN_DELETE


typedef struct stCommand
{
    DLIST m_stDlist;
    int   m_iReference;
    int   m_iRunning;
    char  *m_pCommand;
}STCMD;

typedef struct stWatchDog
{
    DLIST m_stDlist;
    int   itype;
    int   iWD;
    int   iHandling;
    int   interval;
    char  *m_pPath;
    char  *m_pTargetFile;
    STCMD *m_pStcommand;
}STWD;
#define DLISTNext m_stDlist.m_pNext
#define DLISTPrev m_stDlist.m_pPrev
#define DLISTLock m_stDlist.m_pLock


DLIST     eWatchdogRing;
DLIST     eCommandRing;
DLISTLOCK eWatchdogRingLock;
DLISTLOCK eCommandRingLock;
int       eInotifyFD;

