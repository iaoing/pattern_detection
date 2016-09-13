#ifndef __PLFS_PRIVATE__
#define __PLFS_PRIVATE__

// is this file needed?  Why not move all this into plfs.h itself?  Anything
// really private in here?

#include <vector>
#include <string>
#include <map>
using namespace std;

#define SVNVERS $Rev$

#define EISDIR_DEBUG \
    if(ret!=0) {\
        Util::OpenError(__FILE__,__FUNCTION__,__LINE__,pid,errno);\
    }

vector<string> &tokenize(const string& str,const string& delimiters,
        vector<string> &tokens);

typedef struct {
    string mnt_pt;  // the logical mount point
    string *statfs; // where to resolve statfs calls
    vector<string> backends;    // a list of physical locations 
} PlfsMount;

typedef struct {
    string file;
    size_t num_hostdirs;
    size_t threadpool_size;
    size_t buffer_mbs;  // how many mbs to buffer for write indexing
    map<string,PlfsMount*> mnt_pts;
    bool direct_io; // a flag FUSE needs.  Sorry ADIO and API for the wasted bit
    string *err_msg;
    string *global_summary_dir;
} PlfsConf;

/* get_plfs_conf
   get a pointer to a struct holding plfs configuration values
   parse $HOME/.plfsrc or /etc/plfsrc to find parameter values
   if root, check /etc/plfsrc first and then if fail, then check $HOME/.plfsrc
   if not root, reverse order
*/
PlfsConf* get_plfs_conf( );  

PlfsMount * find_mount_point(PlfsConf *pconf, string path, bool &found);

/* plfs_init
    it just warms up the plfs structures used in expandPath
*/
bool plfs_init(PlfsConf*);

int plfs_chmod_cleanup(const char *logical,mode_t mode );
int plfs_chown_cleanup (const char *logical,uid_t uid,gid_t gid );

ssize_t plfs_reference_count( Plfs_fd * );
void plfs_stat_add(const char*func, double time, int );

int plfs_mutex_lock( pthread_mutex_t *mux, const char *whence );
int plfs_mutex_unlock( pthread_mutex_t *mux, const char *whence );

uid_t plfs_getuid();
gid_t plfs_getgid();
int plfs_setfsuid(uid_t);
int plfs_setfsgid(gid_t);

#endif
