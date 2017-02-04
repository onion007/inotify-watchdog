#include "inotify.h"

void *app_malloc_and_zero( int size )
{
    void *p = NULL;

    p = malloc( size );
    if( NULL != p )
    {
        bzero( p, size );
    }
  
    return p;
}

char *readln(FILE *fd, int blocksz)
{
    char *line, *ret;
    int LF=0x0A;
    int CR=0x0D;

    line = (char *)malloc(blocksz);
    ret = line;
    bzero(line, blocksz);
    if( fgets(ret, blocksz+1, fd) == NULL )
    {
        return line;
    }
    while( 1 )
    {
        if( ( line[blocksz - 1] != '\0' ) &&
            ( line[blocksz - 1] != LF ) &&
            ( line[blocksz - 1] != CR ) )
        {   
            /* line is too long, malloc more mm */
            blocksz += 512;
            line = (char *)realloc( line, blocksz );
            ret = line + blocksz - 512;
            bzero( ret, 512 );
        }
        else
        {
            break;
        }
        if( fgets( ret, 512+1, fd ) == NULL )
        {
            break;
        }
    }
    return line;
}

static char* time_to_str(struct timeval* val)
{
    static char buffer[64];
    static char year_str[5];
    int n = 0;

    bzero(buffer,sizeof(buffer));
    n = sprintf(buffer, "%s", ctime(&(val->tv_sec)));
    /* append millisecond to string */
    strcpy(year_str, &(buffer[n-5]));

    buffer[n-6]= '\0';
    year_str[4] = '\0';
    sprintf(buffer, "%s:%ld %s", buffer, (val->tv_usec/1000), year_str);
    return buffer;
}

void app_add_proclog( char *msg )
{
    char           *line;
    FILE           *fd;
    struct timeval now;

    line = (char *)app_malloc_and_zero( strlen(msg) + sizeof(char) * (64 + 2) );
    gettimeofday( &now, NULL );
    sprintf( line, "%s:%s", time_to_str(&now), msg );

    fd = fopen( INOTIFY_LOG_FILE, "a+" );
    if( NULL != fd )
    {
        fwrite( line, 1, strlen(line), fd );
        fclose( fd );
    }

    free( line );
}

void app_add_syslog( char *msg, int level )
{
    openlog( "inotify", LOG_CONS | LOG_PID, LOG_USER );
    syslog( level, msg );
    closelog();
}

int app_add_log( char *msg, int level = LOG_INFO )
{
    if( 0 )
    {
        app_add_syslog( msg, level );
    }
    else
    {
        app_add_proclog( msg );
    }

    return 0;
}

/*===================================
* Function:
*     dirpath_reduce
* Parameter:
*     char *dirpath
* Description:
*     Remove redundant '/'
*     Like: '/root/mydir////' --> '/root/mydir'
            '/' --> '/'
===================================*/
void dirpath_reduce( char *dirpath )
{
    char *p;

    for( p = dirpath + strlen( dirpath ) - 1;
         p != dirpath && '/' == p[0];
         p-- )
    {
        p[0] = '\0';
    }

    return;
}

int app_inotify_init( void )
{
    eInotifyFD = inotify_init();
    if( eInotifyFD < 0 )
    {
        // ADD LOG;
        app_add_log( (char *)"Fail to initialize inotify.\n", LOG_ERR );
        return -1;
    }
    return eInotifyFD;
}

STWD *app_ring_insert_wd( DLIST *pWatchdongRing, STWD *pwd )
{
    if( NULL != pWatchdongRing && NULL != pwd )
    {
        STWD *tpwd;

        app_dlistLock( pWatchdongRing->m_pLock );
        for( tpwd = (STWD *)pWatchdongRing->m_pNext;
             tpwd != (STWD *)pWatchdongRing;
             tpwd = (STWD *)tpwd->DLISTNext )
        {
            if( !strcmp( pwd->m_pPath, tpwd->m_pPath ) )
            {
               app_dlistUnlock( pWatchdongRing->m_pLock );

               /*
                * Path exists in pWatchdongRing. Skip and release pwd.
                */
               if( NULL != pwd->m_pPath )
               {
                   free( pwd->m_pPath );
               }
               free( pwd );

               return tpwd;
            }
        }
        app_dlistAddLocked( &pwd->m_stDlist, pWatchdongRing );
        app_dlistUnlock( pWatchdongRing->m_pLock );
    }

    return pwd;
}

char *app_get_inotify_conf_file( )
{
    FILE *fd = NULL;
    char *watch_conf_file = NULL;
    char *p = NULL;
    char name[128];
    char value[512];
    STWD *pwd;

    /*
     * Add INOTIFY_WATCH_CONFIG_FILELIST to eWatchdogRing
     */
    pwd = (STWD *)app_malloc_and_zero( sizeof( STWD ) );
    if( NULL != pwd )
    {
        pwd->m_pPath = strdup( INOTIFY_WATCH_CONFIG_FILELIST );
        pwd->itype   = 1;
        app_ring_insert_wd( &eWatchdogRing, pwd );
    }

    fd = fopen( INOTIFY_WATCH_CONFIG_FILELIST, "r" );
    if( fd == NULL )
    {
        // ADD LOG;
        char msg[1024];
        sprintf( msg, "Fail to open INOTIFY CONFIG FILELIST %s\n", INOTIFY_WATCH_CONFIG_FILELIST );
        app_add_log( msg, LOG_ERR );
        return NULL;
    }

    while( ( p = readln(fd, 1024) ) &&
           ( p[0] != '\0' ) &&
           ( p[0] != '#' ) )
    {
       sscanf( p, "%s\t%s", name, value);
       if( !strcmp( name, "WATCHCONF" ) )
       {
           watch_conf_file = (char *)malloc( strlen(value) + 1 );
           strcpy( watch_conf_file, value );
           break;
       }
    }
    if( p != NULL )
    {
        free( p );
    }
    fclose( fd );

    return watch_conf_file;
}

TiXmlDocument *app_open_watch_file( char *watch_conf_file )
{
	TiXmlDocument *pDom;

	pDom = new TiXmlDocument;
	if( !pDom->LoadFile( watch_conf_file ) )
	{
	    delete pDom;
	    return NULL;	
	}
	return pDom;
}

STCMD *app_ring_insert_cmd( DLIST *pCommandRing, STCMD *pcmd )
{
    if( NULL != pCommandRing && NULL != pcmd )
    {
        STCMD *tpcmd;

        app_dlistLock( pCommandRing->m_pLock );
        for( tpcmd = (STCMD *)pCommandRing->m_pNext;
             tpcmd != (STCMD *)pCommandRing;
             tpcmd = (STCMD *)tpcmd->DLISTNext )
        {
            if( !strcmp( pcmd->m_pCommand, tpcmd->m_pCommand ) )
            {
               app_dlistUnlock( pCommandRing->m_pLock );

               /*
                * Command exists in pRing. Skip and release pwd.
                */
               if( pcmd->m_pCommand ) 
               {
                   free( pcmd->m_pCommand );
               }
               free( pcmd );
               return tpcmd;
            }
        }
        app_dlistAddLocked( &pcmd->m_stDlist, pCommandRing );
        app_dlistUnlock( pCommandRing->m_pLock );
    }
    return pcmd;
}

void app_reset_watchdog_ring( )
{
    STWD *pwd;

    pwd = (STWD *)eWatchdogRing.m_pNext;
    while( pwd != (STWD *)&eWatchdogRing )
    {
        STWD *tmppwd = (STWD *)pwd->DLISTNext;

        if( 0 == pwd->itype )
        {
            app_dlistDrop( &pwd->m_stDlist );
            if( NULL != pwd->m_pPath )
            {
                free( pwd->m_pPath );
            }
            free( pwd );
        }
        pwd = tmppwd;
    }
}

void app_remove_watchdog_ring( )
{
    STWD *pwd;

    pwd = (STWD *)eWatchdogRing.m_pNext;
    while( pwd != (STWD *)&eWatchdogRing )
    {
        app_dlistDrop( &pwd->m_stDlist );
        if( NULL != pwd->m_pPath )
        {
            free( pwd->m_pPath );
        }
        free( pwd );
        pwd = (STWD *)eWatchdogRing.m_pNext;
    }
}

void app_remove_command_ring( )
{
    STCMD *pcmd;

    pcmd = (STCMD *)eCommandRing.m_pNext;
    while( pcmd != (STCMD *)&eCommandRing )
    {
        app_dlistDrop( &pcmd->m_stDlist );
        if( NULL != pcmd->m_pCommand )
        {
            free( pcmd->m_pCommand );
        }
        free( pcmd );
        pcmd = (STCMD *)eCommandRing.m_pNext;
    }
}

void app_reset_command_ring( )
{
    app_remove_command_ring( );
}

void app_remove_normal_rings( )
{
    app_reset_watchdog_ring( );
    app_reset_command_ring( );
}

void app_remove_all_rings( )
{
    app_remove_watchdog_ring( );
    app_remove_command_ring( );
}

int app_load_from_watch_file( DLIST *pRing, char *watch_conf_file )
{
    TiXmlElement  *pWatch = NULL;
    TiXmlElement  *pWatchRoot = NULL;
    TiXmlDocument *pDom;
    STWD          *pwd;
    STCMD         *pcmd;

    /*
     * Add watch_conf_file to eWatchdogRing
     */
    pwd = (STWD *)malloc( sizeof( STWD ) );
    pwd->m_pPath = strdup( watch_conf_file );
    app_ring_insert_wd( pRing, pwd );

    pDom = app_open_watch_file( watch_conf_file );
    if (pDom == NULL)
    {
        return -1;
    }

    pWatchRoot = pDom->FirstChildElement(WATCHROOT);
    for( pWatch = pWatchRoot->FirstChildElement(WATCH);
         pWatch;
         pWatch = pWatch->NextSiblingElement() )
    {
        TiXmlElement *pWatchPath = NULL;
        TiXmlElement *pWatchTime = NULL;

        pWatchPath = pWatch->FirstChildElement(WATCHPATH);
        pWatchTime = pWatch->FirstChildElement(WATCHTIME);

        pwd        = (STWD *)app_malloc_and_zero( sizeof( STWD ) );
        if( NULL != pWatchPath )
        {
            pwd->m_pPath = (char *)malloc( strlen( pWatchPath->GetText() ) + 1 );
            strcpy( pwd->m_pPath, pWatchPath->GetText() );
            dirpath_reduce( pwd->m_pPath );
        }
        pwd->interval = MIN_INTERVALTIME;
        if( NULL != pWatchTime )
        {
            /*
             * Make sure pWatchTime->GetText() is in 0~3600
             */
            pwd->interval = atoi( pWatchTime->GetText() );
            if( MAX_INTERVALTIME < pwd->interval )
            {
                pwd->interval = MAX_INTERVALTIME;
            }
            else if( 0 > pwd->interval )
            {
                pwd->interval = MIN_INTERVALTIME;
            }
        }
        pwd = app_ring_insert_wd( pRing, pwd );

        pcmd               = (STCMD *)app_malloc_and_zero( sizeof( STCMD ) );
        pcmd->m_iReference = 0;
        pcmd->m_iRunning   = 0;
        pcmd->m_pCommand   = (char *)malloc( strlen( pWatch->Attribute( WATCHATTR ) ) + 1 );
        strcpy( pcmd->m_pCommand, pWatch->Attribute( WATCHATTR ) );
        pcmd = app_ring_insert_cmd( &eCommandRing, pcmd );

        pwd->m_pStcommand = pcmd;
        pcmd->m_iReference++;
    }

    if( pDom )
    {
        delete pDom;
    }
    return 0;
}

int app_inotify_add_watch( char *path, uint32_t mask, int trytime = 1 )
{
    int  wd = 0;
    char msg[5000];

    while( 0 < trytime && wd <= 0 )
    {
        wd = inotify_add_watch( eInotifyFD, path, FPG_INOTIFY_EVENT );
        sleep(1);
        trytime--;
    }
    if( wd > 0 )
    {
        sprintf( msg, "Add %s to Watchdog list. id=%d\n", path, wd );
        app_add_log( msg );
    }
    else
    {
        sprintf( msg, "Can't add %s. Maybe it doesn't exist!\n", path );
        app_add_log( msg, LOG_WARNING );
    }

    return wd;
}

void app_watch_inheritance( STWD *newpwd, STWD *oldpwd )
{
    if( NULL != newpwd && NULL != oldpwd )
    {
        newpwd->interval     = oldpwd->interval;
        newpwd->m_pStcommand = oldpwd->m_pStcommand;
        app_dlistLock( newpwd->m_pStcommand->DLISTLock );
        newpwd->m_pStcommand->m_iReference++;
        app_dlistUnlock( newpwd->m_pStcommand->DLISTLock );
    }
}

void app_add_watchs_for_subdirectory( STWD *pwd, int myself )
{
    char          *psubdir;
    DIR           *pdir;
    struct dirent *pdirent;
    struct stat   statbuf;

    if( 0 != myself )
    {
        pwd->iWD = app_inotify_add_watch( pwd->m_pPath, FPG_INOTIFY_EVENT );
    }

    pdir = opendir( pwd->m_pPath );
    if( NULL == pdir )
    {
        return;
    }

    while( NULL != (pdirent = readdir( pdir )) )
    {
        if( 0 == strcmp(pdirent->d_name, ".") ||
            0 == strcmp(pdirent->d_name, "..") )
        {
            continue;
        }

        psubdir = (char *)malloc( strlen(pwd->m_pPath) + strlen(pdirent->d_name) + 2 );
        sprintf( psubdir, "%s/%s", pwd->m_pPath, pdirent->d_name );

        if( 0 == stat(psubdir, &statbuf) && S_ISDIR(statbuf.st_mode) )
        {
            STWD *newpwd;

            newpwd = (STWD *)app_malloc_and_zero( sizeof(STWD) );
            newpwd->m_pPath = (char *)app_malloc_and_zero( strlen( psubdir ) + 1 );
            strcpy( newpwd->m_pPath, psubdir );
            app_watch_inheritance( newpwd, pwd );
            app_ring_insert_wd( &eWatchdogRing, newpwd );
            /*
             * No need to recur, because insert subdirtory to the end of eWatchdogRing,
             * at last, all subdirectory will be readdir.
             * app_add_watchs_for_subdirectory( newpwd, 1 );
             */
        }

        free( psubdir );
    }
}

int app_add_watchs_from_ring( DLIST *pRing )
{
    STWD *pwd;

    for( pwd = (STWD *)pRing->m_pNext;
         pwd != (STWD *)pRing;
         pwd = (STWD *)pwd->DLISTNext )
    {
        pwd->iWD = app_inotify_add_watch( pwd->m_pPath, FPG_INOTIFY_EVENT, 2 );
        if( pwd->iWD > 0 )
        {
            /*
             * if pwd is directory, need add watchs for subdirectory
             */
            app_add_watchs_for_subdirectory( pwd, 0 );
        }
    }

    return 0;
}

int app_remove_watch( STWD *pwd )
{
    int  ret;
    char msg[5000];

    ret = inotify_rm_watch( eInotifyFD, pwd->iWD );
    if( 0 == ret )
    {
        sprintf( msg, "Remove watch for %s.\n", pwd->m_pPath );
        app_add_log( msg );
        ret = 0;
    }
    else
    {
        sprintf( msg, "Failed to remove watch for %s.\n", pwd->m_pPath );
        app_add_log( msg, LOG_WARNING );
        ret = pwd->iWD;
    }

    return ret;
}

void app_exec_cmd( STCMD *pcmd )
{
    char  msg[5000];

    if( NULL != pcmd )
    {
        if( 0 == pcmd->m_iRunning && NULL != pcmd->m_pCommand )
        {
            /*
             * Execute pcmd->m_pCommand
             */
            sprintf( msg, "Executing [%s] command...\n", pcmd->m_pCommand );
            app_add_log( msg );
            system( pcmd->m_pCommand );
        }
    }
}

int app_get_all_blocks( char *dirpath, int subdir )
{
    char          *psubdir;
    DIR           *pdir;
    struct dirent *pdirent;
    struct stat   statbuf;
    uint          itotal;      

    pdir = opendir( dirpath );
    if( NULL == pdir )
    {
        return 0;
    }

    itotal = 0;
    while( NULL != (pdirent = readdir( pdir )) )
    {
        if( 0 == strcmp(pdirent->d_name, ".") ||
            0 == strcmp(pdirent->d_name, "..") )
        {
            continue;
        }

        psubdir = (char *)malloc( strlen(dirpath) + strlen(pdirent->d_name) + 2 );
        sprintf( psubdir, "%s/%s", dirpath, pdirent->d_name );

        if( 0 == stat(psubdir, &statbuf) )
        {
            itotal += statbuf.st_blocks;
        }

        if( 0 != subdir && S_ISDIR(statbuf.st_mode) )
        {
            itotal += app_get_all_blocks( psubdir, 1 );
        }

        free( psubdir );
    }

    return itotal;
}

void app_close_operation_check( STWD *pwd )
{
    uint   oldblocks;
    uint   newblocks;

    if( NULL == pwd->m_pStcommand )
    {
        return;
    }

    for( newblocks = app_get_all_blocks( pwd->m_pPath, 1 );
         oldblocks != newblocks;
          )
    {
        oldblocks = newblocks;
        sleep( pwd->interval );
        newblocks = app_get_all_blocks( pwd->m_pPath, 1 );
    }

    app_exec_cmd( pwd->m_pStcommand );
}

void *app_pthread_function( void *arg )
{
    STWD *pwd = (STWD *)arg;

    if( 0 < pwd->iHandling )
    {
        /*
         * Maybe need wrlock.
         * the pwd is handling. Skip.
         * printf("the pwd [%s] is handling. Skip!\n", pwd->m_pPath);
         */
    }
    else
    {
        pwd->iHandling++;
        app_close_operation_check( pwd );
        pwd->iHandling--;
    }
}

/*
 * RETURN:
 *      0: Need to STOP this process.
 *      1: Continue.
 */
int app_handle_watch( DLIST *pRing, char *watch_conf_file )
{
    int len, tmp_len;
    char *offset = NULL;
    char buffer[MAX_BUFFER_SIZE];
    char msg[5000];
    struct inotify_event *event;

    while( len = read(eInotifyFD, buffer, MAX_BUFFER_SIZE) )
    {
        offset = buffer;
        event = (struct inotify_event *)buffer;
        while( ((char *)event - buffer) < len )
        {
            STWD *pwd;

            /*
             * TBD:
             * !!!!Needs a read lock to lock pRing!!!!
             */
            for( pwd = (STWD *)pRing->m_pNext;
                 pwd != (STWD *)pRing;
                 pwd = (STWD *)pwd->DLISTNext )
            {
                if( event->wd != pwd->iWD )
                {
                    continue;
                }
                if( !strcmp( pwd->m_pPath, watch_conf_file ) )
                {
                    sprintf( msg, "Reload watch file [%s]\n", watch_conf_file );
                    app_add_log( msg );
                    return 1;
                }
                else if( !strcmp( pwd->m_pPath, INOTIFY_WATCH_CONFIG_FILELIST ) &&
                         (event->mask & IN_CLOSE_WRITE) )
                {
                    sprintf( msg, "Terminating the inotify process.\n" );
                    app_add_log( msg );
                    return 0;
                }

                if( event->mask & IN_ISDIR && event->mask & IN_CREATE )
                {
                    STWD *newpwd;

                    /*
                     * Debug:
                     * Create a directory in pwd->path
                     * sprintf( msg, "Create DIR [%s] [%s]\n", pwd->m_pPath, event->name );
                     * app_add_log( msg );
                     */

                    /*
                     * Create new Watch and inherit from pwd
                     */
                    newpwd = (STWD *)malloc( sizeof( STWD ) );
                    bzero( newpwd, sizeof( STWD ) );
                    newpwd->m_pPath = (char *)malloc( strlen( pwd->m_pPath ) + strlen( event->name ) + 2 );
                    sprintf( newpwd->m_pPath, "%s/%s", pwd->m_pPath, event->name );
                    newpwd->iWD = app_inotify_add_watch( newpwd->m_pPath, FPG_INOTIFY_EVENT );
                    app_watch_inheritance( newpwd, pwd );
                    app_ring_insert_wd( pRing, newpwd );
                }
                if( event->mask & IN_DELETE )
                {
                    char *path;
                    STWD *tmppwd;

                    /*
                     * Debug:
                     * Delete a directory or file in pwd->m_pPath
                     * sprintf( msg, "DELETE [%s] [%s]\n", pwd->m_pPath, event->name );
                     * app_add_log( msg );
                     */

                    /*
                     * Remove pwd from eWatchdogRing, and de-reference eCommandRing
                     */
                    path = (char *)malloc( strlen( pwd->m_pPath ) + strlen( event->name ) + 2 );
                    sprintf( path, "%s/%s", pwd->m_pPath, event->name );
                    for( tmppwd = (STWD *)pRing->m_pNext;
                         tmppwd != (STWD *)pRing;
                         tmppwd = (STWD *)tmppwd->DLISTNext )
                    {
                        if( !strcmp( tmppwd->m_pPath, path ) )
                        {
                            STCMD *pcmd;

                            tmppwd->iWD = app_remove_watch( tmppwd );
                            app_dlistDrop( &tmppwd->m_stDlist );
                            if( NULL != tmppwd->m_pPath )
                            {
                                free( tmppwd->m_pPath );
                            }

                            pcmd = tmppwd->m_pStcommand;
                            pcmd->m_iReference--;
                            if( 0 >= pcmd->m_iReference )
                            {
                               app_dlistDrop( &pcmd->m_stDlist );
                               if( pcmd->m_pCommand )
                               {
                                   free( pcmd->m_pCommand );
                               }
                               free( pcmd );
                            }
                            free( tmppwd );

                            break;
                        }
                    }
                    if( NULL != path )
                    {
                        free( path );
                    }
                }
                if( event->mask & IN_CLOSE_WRITE )
                {
                    pthread_attr_t attr;
                    pthread_t      tid;

                    /*
                     * Debug:
                     * sprintf( msg, "Write and close [%s] [%s]\n", pwd->m_pPath, event->name );
                     * app_add_log( msg );
                     */

                    /*
                     * EXEC cmd
                     * ret=pthread_create(&ptid,NULL,(void *)app_close_operation_check,NULL);
                     * pthread_join( ptid, NULL );
                     */
                    pthread_attr_init( &attr );
                    pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
                    pthread_create( &tid, NULL, &app_pthread_function, pwd );
                }
                break;
            }
            tmp_len = sizeof(struct inotify_event) + event->len;
            event = (struct inotify_event *)(offset + tmp_len);
            offset += tmp_len;
        }
    }

    printf("You can't see this message!\n");
    return 2;
}

int app_remove_normal_watchs( )
{
    STWD  *pwd;

    for( pwd = (STWD *)eWatchdogRing.m_pNext;
         pwd != (STWD *)&eWatchdogRing;
         pwd = (STWD *)pwd->DLISTNext )
    {
        if( 0 < pwd->iWD && 0 == pwd->itype )
        {
            pwd->iWD = app_remove_watch( pwd );
        }
    }
}

int app_remove_all_watchs( )
{
    STWD  *pwd;

    for( pwd = (STWD *)eWatchdogRing.m_pNext;
         pwd != (STWD *)&eWatchdogRing;
         pwd = (STWD *)pwd->DLISTNext )
    {
        if( 0 < pwd->iWD )
        {
            pwd->iWD = app_remove_watch( pwd );
        }
    }
}

void app_show_all_rings( )
{
    int   i;
    STWD  *pwd;
    STCMD *pcmd;

    printf("===== eWatchdogRing INFO =====\n");
    printf("ID\tTYPE\tWD\tInterval\tPath\n");
    i = 1;
    app_dlistLock( eWatchdogRing.m_pLock );
    for( pwd = (STWD *)eWatchdogRing.m_pNext;
         pwd != (STWD *)&eWatchdogRing;
         pwd = (STWD *)pwd->DLISTNext )
    {
        printf("%d\t%d\t%d\t%d\t%s\n", i, pwd->itype, pwd->iWD, pwd->interval, pwd->m_pPath );
        i++;
    }
    app_dlistUnlock( eWatchdogRing.m_pLock );
    printf("===============================\n");

    printf("====== eCommandRing INFO ======\n");
    printf("ID\tRef\tRunning\tCommand\n");
    i = 1;
    app_dlistLock( eCommandRing.m_pLock );
    for( pcmd = (STCMD *)eCommandRing.m_pNext;
         pcmd != (STCMD *)&eCommandRing;
         pcmd = (STCMD *)pcmd->DLISTNext )
    {
        printf("%d\t%d\t%d\t%s\n", i, pcmd->m_iReference, pcmd->m_iRunning, pcmd->m_pCommand);
        i++;
    }
    app_dlistUnlock( eCommandRing.m_pLock );
    printf("===============================\n\n");
}

void app_init( )
{
    char msg[64];

    sprintf( msg, "Starting inotify process... version is %s\n", VERSION );
    app_add_log( msg );
}

int app_check_running( const char *pmyname, int pid )
{
    int  procnum = 0;
    char cmd[512];
    char cmdresult[512];
    FILE *fp;

    snprintf(cmd,sizeof(cmd)-1,"/bin/ps -aef|/bin/grep /sbin/%s|grep -v grep|/usr/bin/awk '$2 != %d'|wc -l",pmyname,pid);
    fp = popen(cmd, "r");
    if (fp == NULL)
    {
        fprintf(stderr,"%s failed\n",cmd);
        return 1;
    }
    memset(cmdresult,0,sizeof(cmdresult));
    fgets(cmdresult,sizeof(cmdresult)-1,fp);
    sscanf(cmdresult,"%d",&procnum);
    pclose(fp);
    if (procnum > 0)
        return 1;

    return 0;
}

int main( int argc, const char *argv[] )
{
    int       fd = 0;
    int       ret = 0;
    int       iAgain = 1;
    char      *watch_conf_file = NULL;

    /*
     * Make sure no inotify process running. if running, exist.
     */
    if( 0 != app_check_running(argv[0],getpid()) )
    {
        return 1;
    }

    app_init( );
    /*
     * Init inotify fd
     */
    fd = app_inotify_init( );
    if( fd < 0 )
    {
        printf("app_inotify_init function false!\n");
        return fd;
    }

    /*
     * Init Rings
     */
    app_dlistInit( &eWatchdogRing, &eWatchdogRingLock );
    app_dlistInit( &eCommandRing, &eCommandRingLock );

    /*
     * Read inotify configuration file
     */
    watch_conf_file = app_get_inotify_conf_file( );
    if( NULL == watch_conf_file )
    {
        printf("app_get_inotify_conf_file function false!\n");
        return ret;
    }

    /*
     * this needs a loop for reload watch file of configuration file
     */
    while( iAgain )
    { 
        app_load_from_watch_file( &eWatchdogRing, watch_conf_file );
        app_add_watchs_from_ring( &eWatchdogRing );
        iAgain = app_handle_watch( &eWatchdogRing, watch_conf_file );
        app_remove_normal_watchs( );
        //app_show_all_rings( );
        app_remove_normal_rings( );
        //app_show_all_rings( );
    }
    app_remove_all_watchs( );
    app_remove_all_rings( );

    if( NULL != watch_conf_file )
    {
        free( watch_conf_file );
    }

    return 0;
}
