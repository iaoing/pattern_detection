#include <stdlib.h>
#include <errno.h>
#include "COPYRIGHT.h"
#include <string>
#include <fstream>
#include <iostream>
#include <sys/dir.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/param.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <time.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <map>
#include <stdio.h>
#include <stdarg.h>
using namespace std;

#include "Util.h"
#include "LogMessage.h"

#ifdef HAVE_SYS_FSUID_H
    #include <sys/fsuid.h>
#endif

#define SLOW_UTIL   2

#define O_CONCURRENT_WRITE                         020000000000

// TODO.  Some functions in here return -errno.  Probably none of them
// should

// shoot.  I just realized.  All this close timing stuff is not thread safe.
// maybe we should add a mutex in addBytes and addTime.
// might slow things down but this is supposed to just be for debugging...

#ifndef UTIL_COLLECT_TIMES
    off_t total_ops = 0;
    #define ENTER_UTIL int ret = 0; total_ops++;
    #define ENTER_IO   ssize_t ret = 0;
    #define EXIT_IO    return ret;
    #define EXIT_UTIL  return ret;
    #define ENTER_MUX  ENTER_UTIL;
    #define ENTER_PATH ENTER_UTIL;
#else
    #define DEBUG_ENTER /*Util::Debug("Enter %s\n", __FUNCTION__ );*/
    #define DEBUG_EXIT  LogMessage lm1;                             \
                        lm1 << "Util::" << setw(13) << __FUNCTION__ \
                            << setw(7) << setprecision(0) << ret    \
                            << " " << setprecision(4) << fixed      \
                            << end-begin << endl;                   \
                        lm1.flush();

    #define ENTER_MUX   LogMessage lm2;                             \
                        lm2 << "Util::" << setw(13) << __FUNCTION__ \
                            << endl;                                \
                        lm2.flush();                            \
                        ENTER_UTIL;

    #define ENTER_PATH   int ret = 0;                                \
                         LogMessage lm4;                             \
                         lm4 << "Util::" << setw(13) << __FUNCTION__ \
                             << " on "   << path << endl;            \
                         lm4.flush();                            \
                         ENTER_SHARED;

    #define ENTER_SHARED double begin,end;  \
                        DEBUG_ENTER;        \
                        begin = getTime(); 

    #define ENTER_UTIL  int ret = 0;       \
                        ENTER_SHARED;

    #define ENTER_IO    ssize_t ret = 0;    \
                        ENTER_SHARED;

    #define EXIT_SHARED DEBUG_EXIT;                                 \
                        addTime( __FUNCTION__, end - begin, (ret<0) );       \
                        if ( end - begin > SLOW_UTIL ) {            \
                            LogMessage lm3;                         \
                            lm3 << "WTF: " << __FUNCTION__          \
                                << " took " << end-begin << " secs" \
                                << endl;                            \
                            lm3.flush();                            \
                        }                                           \
                        return ret;

    #define EXIT_IO     end   = getTime();              \
                        addBytes( __FUNCTION__, size ); \
                        EXIT_SHARED;

    // hmm, want to pass an arg to this macro but it didn't work...
    #define EXIT_UTIL   end   = getTime();                          \
                        EXIT_SHARED;
#endif

#ifdef PLFS_DEBUG_ON
void
Util::Debug( const char *format, ... ) {
    va_list args;
    va_start(args, format);
    Util::Debug( format, args);
    va_end( args );
}

void
Util::Debug( const char *format, va_list args ) {
    static FILE *debugfile = NULL;
    static bool init = 0;
    if ( ! init ) {
        init = 1;
        if ( getenv( "PLFS_DEBUG" ) ) {
            // get the env and parse through it for %.h and %.s
            // and %.p.  Replace %h with hostname, %.s with timestamp,
            // %.p with pid
            debugfile = fopen( getenv("PLFS_DEBUG"), "w" );
        }
    }
    if ( debugfile ) {
        // not sure if this is a performance hit.  To remove
        // the added header comment out the next three lines
        // and restore the forth
        ostringstream oss;
        oss << "PDEBUG: pid (" << getpid() << ") " << format;
        vfprintf(debugfile, oss.str().c_str(), args);
        //vfprintf(debugfile, format, args);
        fflush(debugfile);
    }
}
#else
void Util::Debug( const char *format, ... ) { return; }
void Util::Debug( const char *format, va_list args ) { return; }
#endif

void
Util::SeriousError( string msg, pid_t pid ) {
    string filename = getenv("HOME");
    ostringstream oss;
    oss << getenv("HOME") << "/plfs.error." << hostname() << "." << pid;
    FILE *debugfile = fopen( oss.str().c_str(), "a" );
    if ( ! debugfile ) {
        cerr << "PLFS ERROR: Couldn't open " << oss.str() 
             << " for serious error: " << msg << endl;
    } else {
        fprintf(debugfile,"%s\n",msg.c_str());
        fclose(debugfile);
    }
}

void 
Util::OpenError(const char *file, const char *func, int line, int Err, pid_t p){
    ostringstream oss;
    oss << "open() error seen at " << file << ":" << func << ":" << line << ": "
        << strerror(Err); 
    //SeriousError(oss.str(), p);
}

// initialize static variables
HASH_MAP<string, double> utimers;
HASH_MAP<string, off_t>  kbytes;
HASH_MAP<string, off_t>  counters;
HASH_MAP<string, off_t>  errors;

string Util::toString( ) {
    ostringstream oss;
    string output;
    off_t  total_ops  = 0;
    off_t  total_errs = 0;
    double total_time = 0.0;

    HASH_MAP<string,double>::iterator itr;
    HASH_MAP<string,off_t> ::iterator kitr;
    HASH_MAP<string,off_t> ::iterator count;
    HASH_MAP<string,off_t> ::iterator err;
    for( itr = utimers.begin(); itr != utimers.end(); itr++ ) {
        count  = counters.find( itr->first );
        err = errors.find( itr->first );
        output += timeToString( itr, err, count, &total_errs, 
                &total_ops, &total_time );
        if ( ( kitr = kbytes.find(itr->first) ) != kbytes.end() ) {
            output += bandwidthToString( itr, kitr );
        }
        output += "\n";
    }
    oss << "Util Total Ops " << total_ops << " Errs " 
        << total_errs << " in " 
        << std::setprecision(2) << std::fixed << total_time << "s\n";
    output += oss.str();
    return output;
}

string Util::bandwidthToString( HASH_MAP<string,double>::iterator itr,
                                HASH_MAP<string,off_t> ::iterator kitr ) 
{
    off_t kbs   = kitr->second;
    double time = itr->second;
    double bw   = (kbs/time) / 1024; 
    ostringstream oss;
    oss << ", " << setw(6) << kbs << "KBs " 
        << std::setprecision(2) << std::fixed << setw(8) << bw << "MB/s";
    return oss.str(); 
}

string Util::timeToString( HASH_MAP<string,double>::iterator itr,
                           HASH_MAP<string,off_t>::iterator eitr,
                           HASH_MAP<string,off_t>::iterator citr,
                           off_t *total_errs,
                           off_t *total_ops,
                           double *total_time ) 
{
    double value    = itr->second;
    off_t  count    = citr->second;
    off_t  errs     = eitr->second;
    double avg      = (double) count / value;
    ostringstream oss;

    *total_errs += errs;
    *total_ops  += count;
    *total_time += value;

    oss << setw(12) << itr->first << ": " << setw(8) << count << " ops, " 
        << setw(8) << errs << " errs, " 
        << std::setprecision(2)
        << std::fixed
        << setw(8)
        << value
        << "s time, "
        << setw(8)
        << avg
        << "ops/s";
    return oss.str();
}

void Util::addBytes( string function, size_t size ) {
    HASH_MAP<string,off_t>::iterator itr;
    itr = kbytes.find( function );
    if ( itr == kbytes.end( ) ) {
        kbytes[function] = (size / 1024);
    } else {
        kbytes[function] += (size / 1024);
    }
}

pthread_mutex_t time_mux;
void Util::addTime( string function, double elapsed, bool error ) {
    HASH_MAP<string,double>::iterator itr;
    HASH_MAP<string,off_t>::iterator two;
    HASH_MAP<string,off_t>::iterator three;
        // plfs is hanging in here for some reason
        // is it a concurrency problem?
        // idk.  be safe and put it in a mux.  testing
        // or rrp3 is where I saw the problem and 
        // adding this lock didn't slow it down
        // also, if you're worried, just turn off
        // both util timing (-DUTIL) and -DNPLFS_TIMES
    pthread_mutex_lock( &time_mux );
    itr   = utimers.find( function );
    two   = counters.find( function );
    three = errors.find( function );
    if ( itr == utimers.end( ) ) {
        utimers[function] = elapsed;
        counters[function] = 1;
        if ( error ) errors[function] = 1;
    } else {
        utimers[function] += elapsed;
        counters[function] ++;
        if ( error ) errors[function] ++;
    }
    pthread_mutex_unlock( &time_mux );
}

int Util::Utime( const char *path, const struct utimbuf *buf ) {
    ENTER_PATH;
    ret = utime( path, buf );
    EXIT_UTIL;
}

int Util::Unlink( const char *path ) {
    ENTER_PATH;
    ret = unlink( path );
    EXIT_UTIL;
}


int Util::Access( const char *path, int mode ) {
    ENTER_PATH;
    ret = access( path, mode );
    EXIT_UTIL;
}

int Util::Mknod( const char *path, mode_t mode, dev_t dev ) {
    ENTER_PATH;
    ret = mknod( path, mode, dev );
    EXIT_UTIL;
}

int Util::Truncate( const char *path, off_t length ) {
    ENTER_PATH;
    ret = truncate( path, length ); 
    EXIT_UTIL;
}


int Util::MutexLock(  pthread_mutex_t *mux , const char * where ) {
    ENTER_MUX;
    ostringstream os, os2;
    os << "Locking mutex " << mux << " from " << where << endl;
    Util::Debug("%s", os.str().c_str() );
    pthread_mutex_lock( mux );
    os2 << "Locked mutex " << mux << " from " << where << endl;
    Util::Debug("%s", os2.str().c_str() );
    EXIT_UTIL;
}

int Util::MutexUnlock( pthread_mutex_t *mux, const char *where ) {
    ENTER_MUX;
    ostringstream os;
    os << "Unlocking mutex " << mux << " from " << where << endl;
    Util::Debug("%s", os.str().c_str() );
    pthread_mutex_unlock( mux );
    EXIT_UTIL;
}

ssize_t Util::Pread( int fd, void *buf, size_t size, off_t off ) {
    ENTER_IO;
    ret = pread( fd, buf, size, off );
    EXIT_IO;
}

ssize_t Util::Pwrite( int fd, const void *buf, size_t size, off_t off ) {
    ENTER_IO;
    ret = pwrite( fd, buf, size, off );
    EXIT_IO;
}

int Util::Rmdir( const char *path ) {
    ENTER_PATH;
    ret = rmdir( path );
    EXIT_UTIL;
}

int Util::Lstat( const char *path, struct stat *st ) {
    ENTER_PATH;
    ret = lstat( path, st );
    EXIT_UTIL;
}

int Util::Rename( const char *path, const char *to ) {
    ENTER_PATH;
    ret = rename( path, to );
    EXIT_UTIL;
}
        
ssize_t Util::Readlink(const char*link, char *buf, size_t bufsize) {
    ENTER_IO;
    ret = readlink(link,buf,bufsize);
    EXIT_UTIL;
}

int Util::Link( const char *path, const char *to ) {
    ENTER_PATH;
    ret = link( path, to );
    EXIT_UTIL;
}

int Util::Symlink( const char *path, const char *to ) {
    ENTER_PATH;
    ret = symlink( path, to );
    EXIT_UTIL;
}

ssize_t Util::Read( int fd, void *buf, size_t size) {
    ENTER_IO;
    ret = read( fd, buf, size ); 
    EXIT_IO;
}

ssize_t Util::Write( int fd, const void *buf, size_t size) {
    ENTER_IO;
    ret = write( fd, buf, size ); 
    EXIT_IO;
}

int Util::Close( int fd ) {
    ENTER_UTIL;
    ret = close( fd ); 
    EXIT_UTIL;
}

int Util::Creat( const char *path, mode_t mode ) {
    ENTER_PATH;
    ret = creat( path, mode );
    if ( ret > 0 ) {
        ret = close( ret );
    }
    else ret = -errno;
    EXIT_UTIL;
}
// returns 0 or -errno
int Util::Statvfs( const char *path, struct statvfs* stbuf ) {
    ENTER_PATH;
    ret = statvfs(path,stbuf);
    EXIT_UTIL;
}

// returns 0 or -errno
int Util::Opendir( const char *path, DIR **dp ) {
    ENTER_PATH;
    *dp = opendir( path );
    ret = ( *dp == NULL ? -errno : 0 );
    EXIT_UTIL;
}

int Util::Closedir( DIR *dp ) {
    ENTER_UTIL;
    ret = closedir( dp );
    EXIT_UTIL;
}

int Util::Munmap(void *addr,size_t len) {
    ENTER_UTIL;
    ret = munmap(addr,len);
    EXIT_UTIL;
}

int Util::Mmap( size_t len, int fildes, void **retaddr) {
    ENTER_UTIL;
    int prot  = PROT_READ;
    int flags = MAP_PRIVATE|MAP_NOCACHE;
    *retaddr = mmap( NULL, len, prot, flags, fildes, 0 );
    ret = ( *retaddr == (void*)NULL || *retaddr == (void*)-1 ? -1 : 0 );
    EXIT_UTIL;
}

int Util::Lseek( int fildes, off_t offset, int whence, off_t *result ) {
    ENTER_UTIL;
    *result = lseek( fildes, offset, whence );
    ret = (int)*result;
    EXIT_UTIL;
}

int Util::Open( const char *path, int flags ) {
    ENTER_PATH;
    ret = open( path, flags ); 
    EXIT_UTIL;
}

int Util::Open( const char *path, int flags, mode_t mode ) {
    ENTER_PATH;
    ret = open( path, flags, mode ); 
    EXIT_UTIL;
}

bool Util::exists( const char *path ) {
    ENTER_PATH;
    bool exists = false;
    struct stat buf;
    if ( Util::Stat( path, &buf ) == 0 ) {
        exists = true;
    }
    ret = exists;
    EXIT_UTIL;
}

bool Util::isDirectory( struct stat *buf ) {
    return (S_ISDIR(buf->st_mode) && !S_ISLNK(buf->st_mode));
}

bool Util::isDirectory( const char *path ) {
    ENTER_PATH;
    bool exists = false;
    struct stat buf;
    if ( Util::Lstat( path, &buf ) == 0 ) {
        exists = isDirectory( &buf );
    }
    ret = exists;
    EXIT_UTIL;
}

int Util::Chown( const char *path, uid_t uid, gid_t gid ) {
    ENTER_PATH;
    ret = chown( path, uid, gid );
    EXIT_UTIL;
}

int Util::Chmod( const char *path, int flags ) {
    ENTER_PATH;
    ret = chmod( path, flags );
    EXIT_UTIL;
}

int Util::Mkdir( const char *path, mode_t mode ) {
    ENTER_PATH;
    ret = mkdir( path, mode );
    EXIT_UTIL;
}

int Util::Fsync( int fd) {
    ENTER_UTIL;
    ret = fsync( fd ); 
    EXIT_UTIL;
}

double Util::getTime( ) {
    // shoot this seems to be solaris only
    // how does MPI_Wtime() work?
    //return 1.0e-9 * gethrtime();
    struct timeval time;
    if ( gettimeofday( &time, NULL ) != 0 ) {
        Util::Debug("WTF: %s failed: %s\n", 
                __FUNCTION__, strerror(errno));
    }
    return (double)time.tv_sec + time.tv_usec/1.e6; 
}

// returns n or returns -1
ssize_t Util::Writen( int fd, const void *vptr, size_t n ) {
    ENTER_UTIL;
    size_t      nleft;
    ssize_t     nwritten;
    const char  *ptr;

    ptr = (const char *)vptr;
    nleft = n;
    ret   = n;
    while (nleft > 0) {
        if ( (nwritten = Util::Write(fd, ptr, nleft)) <= 0) {
            if (errno == EINTR)
                nwritten = 0;       /* and call write() again */
            else {
                ret = -1;           /* error */
                break;
            }
        }

        nleft -= nwritten;
        ptr   += nwritten;
    }
    EXIT_UTIL;
}

string Util::openFlagsToString( int flags ) {
    string fstr;

    if ( flags & O_WRONLY ) {
        fstr += "w";
    }
    if ( flags & O_RDWR ) {
        fstr += "rw";
    }
    if ( flags & O_RDONLY ) {
        fstr += "r";
    }
    if ( flags & O_CREAT ) {
        fstr += "c";
    }
    if ( flags & O_EXCL ) {
        fstr += "e";
    }
    if ( flags & O_TRUNC ) {
        fstr += "t";
    }
    if ( flags & O_APPEND ) {
        fstr += "a";
    }
    if ( flags & O_NONBLOCK || flags & O_NDELAY ) {
        fstr += "n";
    }
    if ( flags & O_SYNC ) {
        fstr += "s";
    }
    if ( flags & O_DIRECTORY ) {
        fstr += "D";
    }
    if ( flags & O_NOFOLLOW ) {
        fstr += "N";
    }
    #ifndef __APPLE__
        if ( flags & O_LARGEFILE ) {
            fstr += "l";
        }
        if ( flags & O_DIRECT ) {
            fstr += "d";
        }
        if ( flags & O_NOATIME ) {
            fstr += "A";
        }
    #else
        if ( flags & O_SHLOCK ) {
            fstr += "S";
        }
        if ( flags & O_EXLOCK ) {
            fstr += "x";
        }
        if ( flags & O_SYMLINK ) {
            fstr += "L";
        }
    #endif
    /*
    if ( flags & O_ATOMICLOOKUP ) {
        fstr += "d";
    }
    */
    // what the hell is flags: 0x8000
    if ( flags & 0x8000 ) {
        fstr += "8";
    }
    if ( flags & O_CONCURRENT_WRITE ) {
        fstr += "cw";
    }
    if ( flags & O_NOCTTY ) {
        fstr += "c";
    }
    if ( O_RDONLY == 0 ) { // this is O_RDONLY I think
        // in the header, O_RDONLY is 00
        int rdonlymask = 0x0002;
        if ( (rdonlymask & flags) == 0 ) {
            fstr += "r";
        }
    }
    ostringstream oss;
    oss << fstr << " (" << flags << ")";
    fstr = oss.str();
    return oss.str(); 
}

/*
// replaces a "%h" in a path with the hostname
string Util::expandPath( string path, string hostname ) {
    size_t found = path.find( "%h" );
    if ( found != string::npos ) {
        path.replace( found, strlen("%h"), hostname );
    }
    return path;
}
*/
        
uid_t Util::Getuid() {
    ENTER_UTIL;
    #ifndef __APPLE__
    ret = getuid();
    #endif
    EXIT_UTIL;
}
        
gid_t Util::Getgid() {
    ENTER_UTIL;
    #ifndef __APPLE__
    ret = getgid();
    #endif
    EXIT_UTIL;
}
        
int Util::Setfsgid( gid_t g ) {
    ENTER_UTIL;
    #ifndef __APPLE__
    errno = 0;
    ret = setfsgid( g );
    Util::Debug("Set gid %d: %s\n", g, strerror(errno) ); 
    #endif
    EXIT_UTIL;
}
        
int Util::Setfsuid( uid_t u ) {
    ENTER_UTIL;
    #ifndef __APPLE__
    errno = 0;
    ret = setfsuid( u );
    Util::Debug("Set uid %d: %s\n", u, strerror(errno) ); 
    #endif
    EXIT_UTIL;
}

// a utility for turning return values into 0 or -ERRNO
int Util::retValue( int res ) {
    return ( res == 0 ? 0 : -errno );
}

char *Util::hostname() {
    static bool init = false;
    static char hname[128];
    if ( !init && gethostname(hname, sizeof(hname)) < 0) {
        return NULL;
    }
    init = true;
    return hname;
}

int Util::Stat( const char *path, struct stat * file_info)
{
    return stat( path , file_info );
}  
