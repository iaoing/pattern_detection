#include <sys/time.h>
#include "COPYRIGHT.h"
#include <time.h>
#include <math.h>
#include <sstream>
#include <queue>
#include <algorithm>
#include <assert.h>
#include <string>
#include <unistd.h>
using namespace std;

#include "Container.h"
#include "OpenFile.h"
#include "plfs.h"
#include "plfs_private.h"
#include "Util.h"
#include "ThreadPool.h"

#define BLKSIZE 512
                
blkcnt_t Container::bytesToBlocks( size_t total_bytes ) {
    return (blkcnt_t)ceil((float)total_bytes/BLKSIZE);
    //return (blkcnt_t)((total_bytes + BLKSIZE - 1) & ~(BLKSIZE-1));
}

bool checkMask(int mask,int value) {
    return (mask&value||mask==value);
}

int Container::Access( const string &path, int mask ) {
    // there used to be some concern here that the accessfile might not
    // exist yet but the way containers are made ensures that an accessfile
    // will exist if the container exists
    
    // doing just Access is insufficient when plfs daemon run as root
    // root can access everything.  so, we must also try the open
   
    mode_t open_mode;
    int ret;
    errno = 0;
    bool mode_set=false;
    string accessfile = getAccessFilePath(path);

    plfs_debug("%s Check existence of %s\n",__FUNCTION__,accessfile.c_str());
    ret = Util::Access( accessfile.c_str(), F_OK );
    if ( ret == 0 ) {
        // at this point, we know the file exists
        if(checkMask(mask,W_OK|R_OK)){
            open_mode = O_RDWR;
            mode_set=true;
        } else if(checkMask(mask,R_OK)||checkMask(mask,X_OK)) {
            open_mode = O_RDONLY;
            mode_set=true;
        } else if(checkMask(mask,W_OK)){
            open_mode = O_WRONLY;
            mode_set=true;
        } else if(checkMask(mask,F_OK)){
            return 0;   // we already know this
        }
        assert(mode_set);
    
        plfs_debug("The file exists attempting open\n");
        ret = Util::Open(accessfile.c_str(),open_mode);
        plfs_debug("Open returns %d\n",ret);
        if(ret >= 0 ) {
            ret = Util::Close(ret);
        }
    }
    return ret; 
}

size_t Container::hashValue( const char *str ) {
        // wonder if we need a fancy hash function or if we could just
        // count the bits or something in the string?
        // be pretty simple to just sum each char . . .
    size_t sum = 0, i;
    for( i = 0; i < strlen( str ); i++ ) {
        sum += (size_t)str[i];
    }
    return sum;
    /*
    #include <openssl/md5.h>
    unsigned char *ret = NULL;
    unsigned char value[MD5_DIGEST_LENGTH/sizeof(unsigned char)];
    ret = MD5( str, strlen(str), &value ); 
    */
}

// our simple rule currently is a directory with an access file in it 
// and not the S_ISUID bit.  The problem with the S_ISUID bit was that it
// makes makeTopLevel much more complicated and S_ISUID isn't available
// everywhere.
// when we make containers, we mkdir tmp, creat access, rename tmp 
//
// OK.  This might be creating a problem for layered PLFS's.  For example
// if someone, for some silly reason, uses PLFS-MPI onto a PLFS-MNT, then
// the PLFS-MPI will create a container for file foo.  It will do a mkdir
// foo which the PLFS-MNT will just do a mkdir for that as well, and then 
// PLFS-MPI will do a create of foo/access which the PLFS-MNT will then 
// create a container for foo/access.  At this point, the PLFS-MNT will
// think that the directory foo is a container since it has a file called
// access in it.  OK, this means that we can't ever have an access file
// in a directory or PLFS will think it's a container.  We need to make
// sure the access name is sufficiently strange.  Also we need to make 
// sure the access file is a file and not a directory
//
// OK.  Also, the access file will exist if it's a symlink.  so maybe
// we need to stat the top level as well and then only stat the accessfile
// if the top level is a directory.  That's annoying.  We really don't want
// to do two stats in this call.
bool Container::isContainer( const string &physical_path, mode_t *mode ) {
    plfs_debug("%s checking %s\n", __FUNCTION__, physical_path.c_str());

    struct stat buf;
    int ret = Util::Lstat( physical_path.c_str(), &buf );
    if ( ret == 0 ) {
        if ( mode ) *mode = buf.st_mode;
        if ( Util::isDirectory(&buf) ) {
            // it's either a directory or a container.  check for access file
            plfs_debug("%s %s is a directory\n", __FUNCTION__, 
                    physical_path.c_str());
            string accessfile = getAccessFilePath(physical_path); 
            ret = Util::Lstat( accessfile.c_str(), &buf );
            return ( ret == 0 ? true : false );    
        } else {
            // it's a regular file, or a link, or something
            return false;
        }
    } else {    
            // the stat failed.  Assume it's ENOENT.  It might be perms
            // in which case return an empty mode as well bec this means
            // that the caller lacks permission to stat the thing
        if ( mode ) *mode = 0;  // ENOENT
        return false;
    }

    // actually, I think we can do this the old one but then on 
    // a stat of a symlink, check then.  better to check in getattr
    // then here in isContainer
    //
    // actually this won't work because in order to resolve the path
    // to the access file, the underlying physical filesystem finds
    // a symlink that points to the logical file and then that would
    // recurse on the stack as it tried to resolve.
    /*
    string accessfile = getAccessFilePath(physical_path); 
    struct stat buf;
    int ret = Util::Stat( accessfile.c_str(), &buf );
    plfs_debug("%s checked %s: %d\n", __FUNCTION__, accessfile.c_str(),ret);
    return(ret==0 ? true:false);
    // I think if we really wanted to reduce this to one stat and have the
    // symlinks work, we could have the symlink point to the back-end instead
    // of to the frontend and then we should be able to just check the access
    // file which would make symlinks look like containers but then we'd have
    // to correctly identify symlinks in getattr, this also means that we'd
    // have to make the backwards mapping in f_readlink to get from a 
    // physical back-end pointer to a front-end one
    if ( Util::isDirectory(physical_path) ) {
        plfs_debug("%s %s is a directory\n", __FUNCTION__, physical_path);
        struct stat buf;
        string accessfile = getAccessFilePath(physical_path); 
        int ret = Util::Lstat( accessfile.c_str(), &buf );
        return ( ret == 0 ? true : false );    
    } else {
        // either a file or a symlink
        return false;
    }
    */
}

int Container::freeIndex( Index **index ) {
    delete *index;
    *index = NULL;
    return 0;
}

// a helper routine for functions that are accessing a file
// which may not exist.  It's not an error if it doesn't
// exist since it might not exist yet due to an error condition
int Container::ignoreNoEnt( int ret ) {
    if ( ret != 0 && ( errno == ENOENT || errno == ENOTDIR ) ) {
        return 0;
    } else {
        return ret;
    }
}

int Container::Chmod( const string &path, mode_t mode ) {
    return Container::chmodModify( path, mode );  
}



// just do the droppings and the access file
int Container::Utime( const string &path, const struct utimbuf *buf ) {
    // TODO: we maybe shouldn't need to fully recurse here...
    return Container::Modify( UTIME, path.c_str(), 0, 0, buf, 0 );  
}

int Container::Chown( const string &path, uid_t uid, gid_t gid ) {
    plfs_debug("Chowning to %d:%d\n", uid, gid );
    return Container::chownModify( path.c_str(), uid, gid );  
}

int Container::cleanupChmod( const string &path, mode_t mode , int top , 
    uid_t uid, gid_t gid  ) {

    struct dirent *dent         = NULL;
    DIR *dir                    = NULL; 
    int ret                     = 0;
    struct stat creator_info;   
    
    if ( top == 1 ) {
        string creator_path = getCreatorFilePath(path); 
        // Stat the creator file for the uid and gid 
        ret = Util::Stat( creator_path.c_str() , &creator_info );
        if (ret == 0 ) {
            uid = creator_info.st_uid;
            gid = creator_info.st_gid;
        }
        else return ret;
    }
    // Open the hostdir and then look for droppings 
    Util::Opendir( path.c_str() , &dir );
    if ( dir == NULL ) { 
        plfs_debug("%s wtf\n", __FUNCTION__ );
        return 0; 
    }
    while( ret == 0 && (dent = readdir( dir )) != NULL ) {
        string full_path( path ); full_path += "/"; full_path += dent->d_name;
        if (!strcmp(dent->d_name,".")||!strcmp(dent->d_name,"..")
                ||!strcmp(dent->d_name, ACCESSFILE )) continue;
        if (!strncmp( dent->d_name , HOSTDIRPREFIX, strlen(HOSTDIRPREFIX)) ||
            !strncmp( dent->d_name , DROPPINGPREFIX , strlen(DROPPINGPREFIX)) ||
                !strncmp( dent->d_name , METADIR , strlen(METADIR) ) || 
                top==2) {
            if ( Util::isDirectory( full_path.c_str() ) ) {

                if(!strncmp( dent->d_name , METADIR , strlen(METADIR))) {
                    ret = cleanupChmod(full_path,mode,2,uid,gid);
                }else{
                    ret = cleanupChmod(full_path,mode,0,uid,gid);
                }
                if ( ret != 0 ) break;
            }
            if ( top == 0 || top == 2) {         
                ret = Util::Chown( full_path.c_str() , uid , gid );
                if( ret == 0 ) {
                    ret = Util::Chmod( full_path.c_str() , mode ); 
                }
            } 
        }
        if(top == 1 ) {
            mode_t m = dirMode(mode);
            plfs_debug("%s chmod %s to %o\n", __FUNCTION__,full_path.c_str(),m);
            ret = Util::Chmod( full_path.c_str() , m );
        }
    }
    Util::Closedir( dir );
    ret = Util::Chmod( path.c_str(), dirMode( mode ) );
    //ret = Util::Chmod( path , mode );
    return ret;
}

int Container::cleanupChown( const string &path, uid_t uid, gid_t gid) {
    struct dirent *dent         = NULL;
    DIR *dir                    = NULL; 
    int ret                     = 0;
     
    Util::Opendir( path.c_str() , &dir );
    if ( dir == NULL ) { 
        plfs_debug("%s wtf\n", __FUNCTION__ );
        return 0; 
    }
    while( ret == 0 && (dent = readdir( dir )) != NULL ) {
        string full_path( path ); full_path += "/"; full_path += dent->d_name;
        if (!strcmp(dent->d_name,".")||!strcmp(dent->d_name,"..")) continue;

        if ( Util::isDirectory( full_path.c_str() ) ) {
                ret = cleanupChown( full_path.c_str() , uid , gid );
                if ( ret != 0 ) break;
            }
            
        ret = Util::Chown( full_path.c_str() , uid, gid);
    }
    Util::Closedir( dir );
    if (ret == 0) ret = Util::Chown( path.c_str() , uid, gid );
    return ret;
}


int Container::chmodModify (const string &path, mode_t mode) {
    int ret; 
    string accessfile = getAccessFilePath(path);
    ret = Util::Chmod( accessfile.c_str(), mode );
    if (ret == 0 ) ret = Util::Chmod( path.c_str(), dirMode( mode )); 
    return ret;
}

int Container::chownModify(const string &path, uid_t uid, gid_t gid ) {
    int ret; 
    string accessfile = getAccessFilePath(path);
    ret = Util::Chown( accessfile.c_str(), uid, gid );
    if (ret == 0 ) ret = Util::Chown( path.c_str() , uid, gid); 
    return ret;
}

int Container::Modify( DirectoryOperation type, 
        const string &path,
        uid_t uid, 
        gid_t gid,
        const struct utimbuf *utbuf,
        mode_t mode )
{
    plfs_debug("%s on %s\n", __FUNCTION__, path.c_str() );
    struct dirent *dent = NULL;
    DIR *dir            = NULL; 
    int ret             = 0;

    Util::Opendir( path.c_str(), &dir );
    if ( dir == NULL ) { 
        plfs_debug("%s wtf\n", __FUNCTION__ );
        return 0; 
    }
    while( ret == 0 && (dent = readdir( dir )) != NULL ) {
        if (!strcmp(dent->d_name,".")||!strcmp(dent->d_name,"..")) continue; 
        string full_path( path.c_str() ); full_path += "/"; full_path += dent->d_name;
        if ( Util::isDirectory( full_path.c_str() ) ) {
            ret = Container::Modify(type,full_path,uid, gid,utbuf,mode);
            if ( ret != 0 ) break;
        }
        errno = 0;
        if ( type == UTIME ) {
            ret = Util::Utime( full_path.c_str(), utbuf );
        } else if ( type == CHOWN ) {
            ret = Util::Chown( full_path.c_str(), uid, gid );
        } 
        plfs_debug("Modified dropping %s: %s\n", 
		full_path.c_str(), strerror(errno) );
    }
    if ( type == UTIME ) {
        plfs_debug("Setting utime on %s\n", path.c_str());
        ret = Util::Utime( path.c_str() , utbuf );
    } else if ( type == CHOWN ) {
        ret = Util::Chown( path.c_str(), uid, gid );
    } 
    
    Util::Closedir( dir );
    return ret;
}

// the shared arguments passed to all of the indexer threads
typedef struct {
    Index *index;
    deque<IndexerTask> *tasks;
    pthread_mutex_t mux;
} IndexerArgs;

// the routine for each indexer thread so that we can parallelize the
// construction of the global index
void *
indexer_thread( void *va ) {
    IndexerArgs *args = (IndexerArgs*)va;
    IndexerTask task;
    size_t ret = 0;
    bool tasks_remaining = true;

    while(true) {
        // try to get a task
        Util::MutexLock(&(args->mux),__FUNCTION__);
        if ( ! args->tasks->empty() ) {
            task = args->tasks->front();
            args->tasks->pop_front();
        } else {
            tasks_remaining = false;
        }
        Util::MutexUnlock(&(args->mux),__FUNCTION__);
        if ( ! tasks_remaining ) break;

        // handle the task
        Index subindex(task.path);
        ret = subindex.readIndex(task.path);
        if ( ret != 0 ) break;
        args->index->lock(__FUNCTION__);
        args->index->merge(&subindex);
        args->index->unlock(__FUNCTION__);
        plfs_debug("THREAD MERGE %s into main index\n", task.path.c_str());
    }

    pthread_exit((void*)ret);
}

// returns 0 or -errno
int Container::flattenIndex( const string &path, Index *index ) {

    // get unique names, and then rename on success so it's atomic
    string globalIndex = getGlobalIndexPath(path);
    string unique_temporary = makeUniquePath(globalIndex);
    int flags = O_WRONLY|O_CREAT|O_EXCL;
    mode_t mode = DROPPING_MODE;

    // open the unique temporary path 
    int index_fd = Util::Open(unique_temporary.c_str(),flags,mode);
    if ( index_fd <= 0 ) {
        return -errno;
    }

    // compress then dump and then close the files
    // compress adds overhead and no benefit if the writes weren't through FUSE
    //index->compress();  
    int ret = index->global_to_file(index_fd);
    plfs_debug("index->global_to_file returned %d\n",ret);
    Util::Close(index_fd);

    if ( ret == 0 ) { // dump was successful so do the atomic rename
        ret = Util::Rename(unique_temporary.c_str(),globalIndex.c_str());
        if ( ret != 0 ) ret = -errno;
    }
    return ret;
}

// this is the function that returns the container index
// should first check for top-level index and if it exists, just use it
// returns -errno or 0 
int Container::populateIndex(const string &path, Index *index,bool use_global) {
    int ret = 0;
    use_global = 0;
    // first try for the top-level global index
    plfs_debug("%s on %s %s attempt to use flattened index\n",
            __FUNCTION__,path.c_str(),(use_global?"will":"will not"));
    int idx_fd = -1;
    if ( use_global ) {
        idx_fd = Util::Open(getGlobalIndexPath(path).c_str(),O_RDONLY);
    }
    if ( idx_fd >= 0 ) {
        plfs_debug("Using cached global flattened index for %s\n",path.c_str());
        off_t len = -1;
        ret = Util::Lseek(idx_fd,0,SEEK_END,&len);
        if ( ret != -1 ) {
            void *addr;
            ret = Util::Mmap(len,idx_fd,&addr);
            if ( ret != -1 ) {
                ret = index->global_from_stream(addr);
                Util::Munmap(addr,len);
            } else {
                plfs_debug("WTF: mmap %s of len %ld: %s\n",
                        getGlobalIndexPath(path).c_str(),
                        (long)len, strerror(errno));
            }
        }
        Util::Close(idx_fd);
    } else {    // oh well, do it the hard way
        plfs_debug("Building global flattened index for %s\n",path.c_str());
        ret = aggregateIndices(path,index);
    }
    return ret;
}

int Container::indexTaskManager(deque<IndexerTask> &tasks,Index *index,string path){
    
    int ret=0;
    if ( tasks.empty() ) {
        ret = 0;    // easy, 0 length file
        plfs_debug("No THREADS needed to create index for empty %s\n", 
                path.c_str());
    } else {
            // shuffle might help for large parallel opens on a 
            // file w/ lots of index droppings
        random_shuffle(tasks.begin(),tasks.end()); 
        PlfsConf *pconf = get_plfs_conf();
        if ( tasks.size() == 1 || pconf->threadpool_size <= 1 ) {
            while( ! tasks.empty() ) {
                IndexerTask task = tasks.front();
                tasks.pop_front();
                ret = index->readIndex(task.path);
                if ( ret != 0 ) break;
            }
        } else {
            // here's where to do the threaded thing
            IndexerArgs args;
            args.index = index;
            args.tasks = &tasks;
            pthread_mutex_init( &(args.mux), NULL );
            size_t count = min(pconf->threadpool_size,tasks.size());
            ThreadPool threadpool(count,indexer_thread, (void*)&args);
            plfs_debug("%d THREADS to create index of %s\n",count,path.c_str());
            ret = threadpool.threadError();    // returns errno
            if ( ret ) {
                plfs_debug("THREAD pool error %s\n", strerror(ret) );
                ret = -ret;
            } else {
                vector<void*> *stati    = threadpool.getStati();
                ssize_t rc;  // needs to be size of void*
                for( size_t t = 0; t < count; t++ ) {
                    void *status = (*stati)[t];
                    rc = (ssize_t)status;
                    if ( rc != 0 ) {
                        ret = (int)rc;
                        break;
                    }
                }
            }
        }
    }
    return ret;
}

vector<IndexFileInfo> Container::hostdir_index_read(const char *path){
    
    DIR *dirp;
    struct dirent *dirent;
    vector<IndexFileInfo> index_droppings;

    // Open the directory and check value
    plfs_debug("%s on %s\n", __FUNCTION__, path);
    if((dirp=opendir(path)) == NULL) {
        plfs_debug("opendir error in hostdir %s directory reader\n",path);
        // Return some sort of error
        cerr << "opendir " << path << " error: " << strerror(errno) << endl;
        return index_droppings;
    }

    // I need the path so I am going to try this out
    // Should always be the first element
    IndexFileInfo path_holder;
    path_holder.timestamp=-1;
    path_holder.hostname=path;
    path_holder.id=-1;

    index_droppings.push_back(path_holder);
     // Start reading the directory
    while(dirent = readdir(dirp) ){
        // Get rid of these hardcoded values ASAP
        if(strncmp(INDEXPREFIX,dirent->d_name,strlen(INDEXPREFIX))==0){
            double time_stamp;
            string str_time_stamp;
            int frac_len;
            int frac;
            
            vector<string> tokens;
            IndexFileInfo index_dropping;
            
            tokenize(dirent->d_name,".",tokens);
            str_time_stamp+=tokens[2];
            str_time_stamp+=".";
            str_time_stamp+=tokens[3];
            time_stamp = strtod(str_time_stamp.c_str(),NULL);
            
            int left_over = (tokens.size()-1)-5;
            
            // Hostname can contain "."
            int count;
            for(count=0;count<=left_over;count++){
                index_dropping.hostname+=tokens[4+count];
                if(count!=left_over) index_dropping.hostname+=".";
            }
            index_dropping.id=atoi(tokens[5+left_over].c_str());
            index_dropping.timestamp=time_stamp;
            plfs_debug("Pushing path %s into index list from %s\n",
                    index_dropping.hostname.c_str(), dirent->d_name);
            index_droppings.push_back(index_dropping);
        }
    }
    return index_droppings;
}

Index Container::parAggregateIndices(vector<IndexFileInfo>& index_list,
                                int rank, int ranks_per_comm,string path)
{
    Index index(path);
    IndexerTask task;
    deque<IndexerTask> tasks;
    int count=0,ret;
    string exp_path;
    vector<string> path_pieces;

    plfs_debug("In parAgg indices before for loop\n");
    plfs_debug("Rank |%d| indexListSize |%d| ranksRerComm |%d|\n",rank,
            index_list.size(),ranks_per_comm);
    for(count=rank;count<index_list.size();count+=ranks_per_comm){
        // Used this pointer to make the next function call cleaner
        IndexFileInfo *current;
        current=&(index_list[count]);
        // get Index Path doesn't work for because we use the hostname
        // for a hash to hostdir. We already have the hostdir
        string index_path = getIndexHostPath(path,current->hostname,
                                    current->id,current->timestamp);
        task.path = index_path;
        plfs_debug("Task push path %s\n",index_path.c_str());
        tasks.push_back(task);
    }
    
    plfs_debug("Par agg indices path %s\n",path.c_str());
    ret=indexTaskManager(tasks,&index,path);
       
    return index;


}

// this function traverses the container, finds all the index droppings,
// and aggregates them into a global in-memory index structure
// returns 0 or -errno
int Container::aggregateIndices(const string &path, Index *index) {
    string hostindex;
    IndexerTask task;
    deque<IndexerTask> tasks;
    int ret = 0;

    plfs_debug("In %s\n", __FUNCTION__);
    
    // create the list of tasks
    DIR *td = NULL, *hd = NULL; struct dirent *tent = NULL;
    while((ret = nextdropping(path,&hostindex,INDEXPREFIX, &td,&hd,&tent))== 1){
        task.path = hostindex;
        tasks.push_back(task);  // makes a copy and pushes it
        plfs_debug("Ag indices path is %s\n",path.c_str());
    }
    ret=indexTaskManager(tasks,index,path);
    return ret;
}

string Container::getDataPath(const string &path, const string &host, int pid,
        double ts) 
{
    return getChunkPath( path, host, pid, DATAPREFIX, ts );
}

string Container::getIndexHostPath(const string &path,const string &host, int pid,
        double ts)
{

    ostringstream oss;
    oss.setf(ios::fixed,ios::floatfield);
    oss << path << "/" << INDEXPREFIX;  
    oss << ts << "." << host << "." << pid;
    return oss.str();
}

string Container::getIndexPath(const string &path, const string &host, int pid,
        double ts) 
{
    return getChunkPath( path, host, pid, INDEXPREFIX, ts );
}

// this function takes a container path, a hostname, a pid, and a type and 
// returns a path to a chunk (type is either DATAPREFIX or INDEXPREFIX)
// the resulting path looks like this:
// container/HOSTDIRPREFIX.hash(host)/type.host.pid
string Container::getChunkPath( const string &container, const string &host, 
        int pid, const char *type, double timestamp )
{
    ostringstream oss;
    oss.setf(ios::fixed,ios::floatfield);
    oss << timestamp;
    return chunkPath(getHostDirPath(container,host),type,host,pid,oss.str());
}

string Container::makeUniquePath( const string &physical ) {
    static bool init = false;
    static char hostname[_POSIX_PATH_MAX];

    if ( ! init ) {
        init = true;
        if (gethostname(hostname, sizeof(hostname)) < 0) {
            plfs_debug("plfsfuse gethostname failed");
            return ""; 
        }
    }

    ostringstream oss;
    oss.setf(ios::fixed,ios::floatfield);
    oss<<physical<<"."<<hostname<<"."<<getpid()<<"."<<Util::getTime();
    return oss.str();
}

string Container::getGlobalIndexPath( const string &physical ) {
    ostringstream oss;
    oss << physical << "/" << GLOBALINDEX;
    return oss.str();
}

string Container::getGlobalChunkPath( const string &physical ) {
    ostringstream oss;
    oss << physical << "/" << GLOBALCHUNK;
    return oss.str();
}

string Container::chunkPath( const string &hostdir, const char *type, 
        const string &host, int pid, const string &ts ) 
{
    ostringstream oss;
    oss << hostdir << "/" << type << ts << "." << host << "." << pid;
    plfs_debug("%s: ts %s, host %s\n",__FUNCTION__,ts.c_str(),host.c_str());
    return oss.str();
}

// container/HOSTDIRPREFIX.XXX/type.ts.host.pid
string Container::hostdirFromChunk( string chunkpath, const char *type ) {
    // this finds the type (either INDEX or DATA prefix and deletes up to it
    chunkpath.erase( chunkpath.rfind(type), chunkpath.size() );
    return chunkpath;
}

// take the path to an index and a pid, and return the path to that chunk file
// path to index looks like: 
// container/HOSTDIRPREFIX.XXX/INDEXPREFIX.ts.host.pid
string Container::chunkPathFromIndexPath( const string &hostindex, pid_t pid ) {
    string host      = hostFromChunk( hostindex, INDEXPREFIX);
    string hostdir   = hostdirFromChunk( hostindex, INDEXPREFIX);
    string timestamp = timestampFromChunk(hostindex,INDEXPREFIX);
    string chunkpath = chunkPath(hostdir, DATAPREFIX, host, pid,timestamp);
    plfs_debug("%s: Returning %s from %s\n",__FUNCTION__,chunkpath.c_str(),
            hostindex.c_str());
    return chunkpath;
}

// a chunk looks like: container/HOSTDIRPREFIX.XXX/type.ts.host.pid
string Container::timestampFromChunk( string chunkpath, const char *type ) {
    // cut off everything through the type
    plfs_debug("%s:%d path is %s\n",__FUNCTION__,__LINE__,chunkpath.c_str());
    chunkpath.erase( 0, chunkpath.rfind(type) + strlen(type) );
    // erase everything after the second "." 
    size_t firstdot = chunkpath.find(".")+1;
    chunkpath.erase(chunkpath.find(".",firstdot),chunkpath.size());
    plfs_debug("%s: Returning %s\n",__FUNCTION__,chunkpath.c_str());
    return chunkpath;
    // cut off the pid 
    chunkpath.erase( chunkpath.rfind("."),chunkpath.size());
    // cut off the host
    chunkpath.erase( chunkpath.rfind("."),chunkpath.size());
    // what's left is a double
    return chunkpath; 
}

// a chunk looks like: container/HOSTDIRPREFIX.XXX/type.ts.host.pid
string Container::containerFromChunk( string chunkpath ) {
    chunkpath.erase( chunkpath.rfind(HOSTDIRPREFIX), chunkpath.size() );
    return chunkpath;
}

// a chunk looks like: container/HOSTDIRPREFIX.XXX/type.ts.host.pid
// where type is either DATAPREFIX or INDEXPREFIX
string Container::hostFromChunk( string chunkpath, const char *type ) {
    // cut off everything through the type
    chunkpath.erase( 0, chunkpath.rfind(type) + strlen(type) );
    // cut off everything though the ts
    chunkpath.erase( 0, chunkpath.find(".")+1);
    chunkpath.erase( 0, chunkpath.find(".")+1);
    // then cut off everything after the host
    chunkpath.erase( chunkpath.rfind("."), chunkpath.size() );
    //plfs_debug("%s:%d path is %s\n",__FUNCTION__,__LINE__,chunkpath.c_str());
    return chunkpath;
}

// this function drops a file in the metadir which contains
// stat info so that we can later satisfy stats using just readdir
int Container::addMeta( off_t last_offset, size_t total_bytes, 
        const string &path, const string &host, uid_t uid ) 
{
    string metafile;
    struct timeval time;
    int ret = 0;
    if ( gettimeofday( &time, NULL ) != 0 ) {
        plfs_debug("WTF: gettimeofday in %s failed: %s\n",
                __FUNCTION__, strerror(errno ) );
        return -errno;
    }
    ostringstream oss;
    oss << getMetaDirPath(path) << "/" 
        << last_offset << "." << total_bytes  << "."
        << time.tv_sec << "." << time.tv_usec << "."
        << host;
    metafile = oss.str();
    plfs_debug("Creating metafile %s\n", metafile.c_str() );
    ret = ignoreNoEnt(Util::Creat( metafile.c_str(), DROPPING_MODE ));

    // now let's maybe make a global summary dropping
    PlfsConf *pconf = get_plfs_conf();    
    if (pconf->global_summary_dir) {
        string path_without_slashes = path;
        size_t pos = path_without_slashes.find("/");
        while(pos!=string::npos) {
            path_without_slashes.replace(pos,1,"_");
            pos = path_without_slashes.find("/");
        }
        ostringstream oss_global;
        oss_global << *(pconf->global_summary_dir) << "/" 
            << last_offset << "." << total_bytes  << "."
            << time.tv_sec << "." << time.tv_usec << "."
            << host << "." << uid << "." << path_without_slashes;
        metafile = oss_global.str();
        plfs_debug("Creating metafile %s\n", metafile.c_str() );
        Util::Creat( metafile.c_str(), DROPPING_MODE);
    }

    return ret;
}

string Container::fetchMeta( const string &metafile_name, 
        off_t *last_offset, size_t *total_bytes,
        struct timespec *time ) 
{
    istringstream iss( metafile_name );
    string host;
    char dot;
    iss >> *last_offset >> dot >> *total_bytes
        >> dot >> time->tv_sec >> dot
        >> time->tv_nsec >> dot >> host;
    time->tv_nsec *= 1000; // convert from micro
    return host;
}

string Container::getOpenHostsDir( const string &path ) {
    string openhostsdir( path );
    openhostsdir += "/";
    openhostsdir += METADIR; // OPENHOSTDIR;
    return openhostsdir;
}

// a function that reads the open hosts dir to discover which hosts currently
// have the file open
// now the open hosts file has a pid in it so we need to separate this out
int Container::discoverOpenHosts( DIR *openhostsdir, set<string> *openhosts ) {
    struct dirent *dent = NULL;
    while( (dent = readdir( openhostsdir )) != NULL ) {
        string host;
        if ( ! strncmp( dent->d_name, ".", 1 ) ) continue;  // skip . and ..
        // also skip anything that isn't an opendropping
        if ( strncmp( dent->d_name, OPENPREFIX, strlen(OPENPREFIX))) continue;
        host = dent->d_name;
        host.erase(0,strlen(OPENPREFIX));
        host.erase( host.rfind("."), host.size() );
        plfs_debug("Host %s (%s) has open handle\n", dent->d_name,host.c_str());
        openhosts->insert( host );
    }
    return 0;
}

string Container::getOpenrecord( const string &path, const string &host, pid_t pid){
    ostringstream oss;
    oss << getOpenHostsDir( path ) << "/" << OPENPREFIX << host << "." << pid;
    plfs_debug("created open record path %s\n", oss.str().c_str() );
    return oss.str();
}

// if this fails because the openhostsdir doesn't exist, then make it
// and try again
int Container::addOpenrecord( const string &path, const string &host, pid_t pid) {
    string openrecord = getOpenrecord( path, host, pid );
    int ret = Util::Creat( openrecord.c_str(), DEFAULT_MODE );
    if ( ret != 0 && ( errno == ENOENT || errno == ENOTDIR ) ) {
        makeSubdir( getOpenHostsDir(path), DEFAULT_MODE );
        ret = Util::Creat( openrecord.c_str(), DEFAULT_MODE );
    }
    if ( ret != 0 ) {
        plfs_debug("Couldn't make openrecord %s: %s\n", 
                openrecord.c_str(), strerror( errno ) );
    }
    return ret;
}

int Container::removeOpenrecord( const string &path, const string &host, pid_t pid){
    string openrecord = getOpenrecord( path, host, pid ); 
    return Util::Unlink( openrecord.c_str() );
}

// can this work without an access file?
// just return the directory mode right but change it to be a normal file
mode_t Container::getmode( const string &path ) {
    struct stat stbuf;
    if ( Util::Lstat( path.c_str(), &stbuf ) < 0 ) {
        plfs_debug("Failed to getmode for %s\n", path.c_str() );
        return DEFAULT_MODE;
    } else {
        return fileMode(stbuf.st_mode);
    }
}

int Container::getattr( const string &path, struct stat *stbuf ) {
        // Need to walk the whole structure
        // and build up the stat.
        // three ways to do so:
        // used cached info when available
        // otherwise, either stat the data files or
        // read the index files
        // stating the data files is much faster 
        // (see ~/Testing/plfs/doc/SC09/data/stat/stat_full.png)
        // but doesn't correctly account for holes
        // but reading index files might fail if they're being buffered
        // safest to use_cache and stat_data
        // ugh, but we can't stat the data dropping, actually need to read the
        // index.  this is because Chombo truncates the thing to a future
        // value and we don't see it since it's only in the index file
        // maybe safest to get all of them.  But using both is no good bec 
        // it adds both the index and the data.  ugh.
    const char *prefix   = INDEXPREFIX;
    
    int chunks = 0;
    int ret = 0;

        // the easy stuff has already been copied from the directory
        // but get the permissions and stuff from the access file
    string accessfile = getAccessFilePath( path );
    if ( Util::Lstat( accessfile.c_str(), stbuf ) < 0 ) {
        plfs_debug("%s lstat of %s failed: %s\n",
                __FUNCTION__, accessfile.c_str(), strerror( errno ) );
        return -errno;
    }
    stbuf->st_size    = 0;  
    stbuf->st_blocks  = 0;
    stbuf->st_mode    = fileMode( stbuf->st_mode );

        // first read the open dir to see who has
        // the file open
        // then read the meta dir to pull all useful
        // droppings out of there (use everything as
        // long as it's not open), if we can't use
        // meta than we need to pull the info from
        // the hostdir by stating the data files and
        // maybe even actually reading the index files!
    DIR *metadir;
    Util::Opendir( (getMetaDirPath(path)).c_str(), &metadir );
    time_t most_recent_mod = 0;
    set< string > openHosts;
    set< string > validMeta;
    if ( metadir != NULL ) {
        discoverOpenHosts( metadir, &openHosts );

        rewinddir(metadir);
        struct dirent *dent = NULL;
        while( (dent = readdir( metadir )) != NULL ) {
            if ( ! strncmp( dent->d_name, ".", 1 ) ) continue;  // . and ..
            if ( ! strncmp( dent->d_name, OPENPREFIX, strlen(OPENPREFIX)))
                continue; // now we place openhosts in same dir as meta
                          // so skip them here
            off_t last_offset;
            size_t total_bytes;
            struct timespec time;
            ostringstream oss;
            string host = fetchMeta( dent->d_name, 
                    &last_offset, &total_bytes, &time );
            if ( openHosts.find(host) != openHosts.end() ) {
                plfs_debug("Can't use metafile %s because %s has an "
                        " open handle.\n", dent->d_name, host.c_str() );
                continue;
            }
            oss  << "Pulled meta " << last_offset << " " << total_bytes
                 << ", " << time.tv_sec << "." << time.tv_nsec 
                 << " on host " << host << endl;
            plfs_debug("%s", oss.str().c_str() );

            // oh, let's get rewrite correct.  if someone writes
            // a file, and they close it and then later they
            // open it again and write some more then we'll
            // have multiple metadata droppings.  That's fine.
            // just consider all of them.
            stbuf->st_size   =  max( stbuf->st_size, last_offset );
            stbuf->st_blocks += bytesToBlocks( total_bytes );
            most_recent_mod  =  max( most_recent_mod, time.tv_sec );
            validMeta.insert( host );
        }
        Util::Closedir( metadir );
    }
    stbuf->st_mtime = most_recent_mod;

    // if we're using cached data we don't do this part unless there
    // were open hosts
    // the way this works is we find each index file, then we find
    if ( openHosts.size() > 0 ) {
        string dropping; 
        blkcnt_t index_blocks = 0, data_blocks = 0;
        off_t    index_size = 0, data_size = 0;
        DIR *td = NULL, *hd = NULL; struct dirent *tent = NULL;
        while((ret=nextdropping(path,&dropping,prefix, &td,&hd,&tent))==1)
        {
            string host = hostFromChunk( dropping, prefix );
            if ( validMeta.find(host) != validMeta.end() ) {
                plfs_debug("Used stashed stat info for %s\n", 
                        host.c_str() );
                continue;
            } else {
                chunks++;
            }

            // we'll always stat the dropping to get at least the timestamp
            // if it's an index dropping then we'll read it
            // it it's a data dropping, we'll just use more stat info
            struct stat dropping_st;
            if (Util::Lstat(dropping.c_str(), &dropping_st) < 0 ) {
                ret = -errno;
                plfs_debug("lstat of %s failed: %s\n",
                    dropping.c_str(), strerror( errno ) );
                continue;   // shouldn't this be break?
            }
            stbuf->st_ctime = max( dropping_st.st_ctime, stbuf->st_ctime );
            stbuf->st_atime = max( dropping_st.st_atime, stbuf->st_atime );
            stbuf->st_mtime = max( dropping_st.st_mtime, stbuf->st_mtime );

            if ( dropping.find(DATAPREFIX) != dropping.npos ) {
                plfs_debug("Getting stat info from data dropping\n" );
                data_blocks += dropping_st.st_blocks;
                data_size   += dropping_st.st_size;
            } else {
                plfs_debug("Getting stat info from index dropping\n");
                Index index(path);
                index.readIndex( dropping ); 
                index_blocks     += bytesToBlocks( index.totalBytes() );
                index_size        = max( index.lastOffset(), index_size );
            }

        }
        stbuf->st_blocks += max( data_blocks, index_blocks );
        stbuf->st_size   = max( stbuf->st_size, max( data_size, index_size ) );
    }
    ostringstream oss;
    oss  << "Examined " << chunks << " droppings:"
         << path << " total size " << stbuf->st_size <<  ", usage "
         << stbuf->st_blocks << " at " << stbuf->st_blksize << endl;
    plfs_debug("%s", oss.str().c_str() );
    return ret;
}

// this assumes we're in a mutex!
// it's a little bit complex bec we use S_ISUID to determine whether it is 
// a container.  but Mkdir doesn't handle S_ISUID so we can't do an atomic
// Mkdir.  
// we need to Mkdir, chmod, rename
// we want atomicity bec someone might make a dir of the same name as a file
// and we need to be absolutely sure a container is really a container and not
// just a directory
// the above is out of date.  We don't use S_ISUID anymore.  Now we use
// the existence of the access file
// returns -errno or 0
int Container::makeTopLevel( const string &expanded_path,  
        const string &hostname, mode_t mode, pid_t pid )
{
    /*
        // ok, instead of mkdir tmp ; chmod tmp ; rename tmp top
        // we tried just mkdir top ; chmod top to remove the rename 
        // but the damn chmod would sometimes take 5 seconds on 5 hosts
        // also, this is safe within an MPI group but if some
        // external proc does an ls or something, it might catch
        // it between the mkdir and the chmod, so the mkdir/chmod/rename
        // is best
    */

    // ok, let's try making the temp dir in /tmp
    // change all slashes to .'s so we have a unique filename
    // actually, if we use /tmp, probably don't need the hostname in there...
    // shouldn't need the pid in there because this should be wrapped in a mux
    // doesn't work in /tmp because rename can't go acros different file sys's

    // ok, here's the real code:  mkdir tmp ; chmod tmp; rename tmp
    // get rid of the chmod; now it's mkdir tmp; create accessfile; rename tmp
    ostringstream oss;
    oss << expanded_path << "." << hostname << "." << pid;
    string tmpName( oss.str() ); 
    if ( Util::Mkdir( tmpName.c_str(), dirMode(mode) ) < 0 ) {
        if ( errno != EEXIST && errno != EISDIR ) {
            plfs_debug("Mkdir %s to %s failed: %s\n",
                tmpName.c_str(), expanded_path.c_str(), strerror(errno) );
            return -errno;
        } else if ( errno == EEXIST ) {
            if ( ! Container::isContainer(tmpName.c_str(),NULL) ) {
                plfs_debug("Mkdir %s to %s failed: %s\n",
                    tmpName.c_str(), expanded_path.c_str(), strerror(errno) );
            } else {
                plfs_debug("%s is already a container.\n",
                        tmpName.c_str() );
            }
        }
    }
    string tmpAccess( getAccessFilePath(tmpName) );
    if ( makeAccess( tmpAccess, mode ) < 0 ) {
        plfs_debug("create access file in %s failed: %s\n", 
                        tmpName.c_str(), strerror(errno) );
        int saveerrno = errno;
        if ( Util::Rmdir( tmpName.c_str() ) != 0 ) {
            plfs_debug("rmdir of %s failed : %s\n",
                tmpName.c_str(), strerror(errno) );
        }
        return -saveerrno;
    }
    // ok, this rename sometimes takes a long time
    // what if we check first to see if the dir already exists
    // and if it does don't bother with the rename
    // this just moves the bottleneck to the isDirectory
    // plus scared it could double it if they were both slow...
    //if ( ! isDirectory( expanded_path ) ) 
    int attempts = 0;
    while (attempts < 2 ) {
        attempts++;
        if ( Util::Rename( tmpName.c_str(), expanded_path.c_str() ) < 0 ) {
            int saveerrno = errno;
            plfs_debug("rename of %s -> %s failed: %s\n",
                tmpName.c_str(), expanded_path.c_str(), strerror(errno) );
            if ( saveerrno == ENOTDIR ) {
                // there's a normal file where we want to make our container
                saveerrno = Util::Unlink( expanded_path.c_str() );
                // should be success or ENOENT if someone else already unlinked
                if ( saveerrno != 0 && saveerrno != ENOENT ) {
                    return -saveerrno;
                }
                continue;
            }
            // if we get here, we lost the race
            if ( Util::Unlink( tmpAccess.c_str() ) < 0 ) {
                plfs_debug("unlink of temporary %s failed : %s\n",
                        tmpAccess.c_str(), strerror(errno) );
            }
            if ( Util::Rmdir( tmpName.c_str() ) < 0 ) {
                plfs_debug("rmdir of temporary %s failed : %s\n",
                        tmpName.c_str(), strerror(errno) );
            }
            // probably what happened is some other node outraced us
            // if it is here now as a container, that's what happened
            // this check for whether it's a container might be slow
            // if worried about that, change it to check saveerrno
            // if it's something like EEXIST or ENOTEMPTY or EISDIR
            // then that probably means the same thing 
            //if ( ! isContainer( expanded_path ) ) 
            if ( saveerrno != EEXIST && saveerrno != ENOTEMPTY 
                    && saveerrno != EISDIR ) {
                plfs_debug("rename %s to %s failed: %s\n",
                        tmpName.c_str(), expanded_path.c_str(), strerror
                        (saveerrno) );
                return -saveerrno;
            }
            break;
        } else {
            // we made the top level container
            // this is like the only time we know that we won the global race
            // hmmm, any optimizations we could make here?
            // make the metadir after we do the rename so that all nodes
            // don't make an extra unnecessary dir, but this does create
            // a race if someone wants to use the meta dir and it doesn't
            // exist, so we need to make sure we never assume the metadir
            if ( makeSubdir(getMetaDirPath(expanded_path), mode ) < 0){
                return -errno;
            }
            if ( makeSubdir( getOpenHostsDir(expanded_path), mode )< 0){
                return -errno;
            }

                // make the version stuff here?  this means that it is 
                // possible for someone to find a container without the
                // version stuff in it.  In that case, just assume
                // compatible?  move this up above?
            ostringstream oss2;
            oss2 << expanded_path << "/" << VERSIONPREFIX
                 << "-tag." << STR(TAG_VERSION)
                 << "-svn." << STR(SVN_VERSION)
                 << "-dat." << STR(DATA_VERSION);
            if (makeDropping(oss2.str()) < 0) {
                return -errno;
            }

            /*
               // we don't need to make this creator file separately.
               // just use the accessfile for the creator
            if ( makeCreator( getCreatorFilePath(expanded_path) ) < 0 ) {
                plfs_debug("create access file int %s failed\n", 
                                expanded_path.c_str() );
                return -errno;
            }
            */
            break;
        }
    }
    return 0;
}

int Container::makeCreator(const string &path) {
    return makeDroppingReal( path , S_IRWXU );
}
int Container::makeAccess(const string &path, mode_t mode) {
    return makeDroppingReal( path, mode );
}
int Container::makeDroppingReal(const string &path, mode_t mode) {
    return Util::Creat( path.c_str(), mode );
}
int Container::makeDropping(const string &path) {
    mode_t save_umask = umask(0);
    int ret = makeDroppingReal( path, DROPPING_MODE );
    umask(save_umask);
    return ret;
}
// returns 0 or -errno
int Container::makeHostDir(const string &path,
        const string &host, mode_t mode, parentStatus pstat)
{
    int ret = 0;
    if (pstat == PARENT_ABSENT) {
        plfs_debug("Making absent parent %s\n", path.c_str());
        ret = makeSubdir(path.c_str(),mode);
    }
    if (ret == 0) {
        ret = makeSubdir(getHostDirPath(path,host), mode);
    }
    return ( ret == 0 ? ret : -errno );
}

// returns 0 or -1
int Container::makeSubdir( const string &path, mode_t mode ) {
    int ret;
    //mode = mode | S_IXUSR | S_IXGRP | S_IXOTH;
    mode = DROPPING_MODE;
    ret = Util::Mkdir( path.c_str(), mode );
    return ( ret == 0 || errno == EEXIST || errno == EISDIR ) ? 0 : -1;
}
// this just creates a dir/file but it ignores an EEXIST error
int Container::makeMeta( const string &path, mode_t type, mode_t mode ) {
    int ret;
    if ( type == S_IFDIR ) {
        ret = Util::Mkdir( path.c_str(), mode );
    } else if ( type == S_IFREG ) {
        ret = Util::Creat( path.c_str(), mode ); 
    } else {
        cerr << "WTF.  Unknown type passed to " << __FUNCTION__ << endl;
        ret = -1;
        errno = ENOSYS;
    }
    return ( ret == 0 || errno == EEXIST ) ? 0 : -1;
}

// this returns the path to the metadir
// don't ever assume that this exists bec it's possible
// that it doesn't yet
string Container::getMetaDirPath( const string& strPath ) {
    string metadir( strPath + "/" + METADIR ); 
    return metadir;
}

string Container::getAccessFilePath( const string& path ) {
    string accessfile( path + "/" + ACCESSFILE );
    return accessfile;
}

// there is no longer a special creator file.  Just use the access file
string Container::getCreatorFilePath( const string& path ) {
    return getAccessFilePath(path);
    /*
    string creatorfile( path + "/" + CREATORFILE );
    return creatorfile;
    */
}

size_t Container::getHostDirId( const string &hostname ) {
    PlfsConf *pconf = get_plfs_conf();    
    return (hashValue(hostname.c_str())%pconf->num_hostdirs) + 1;
}

string Container::getHostDirPath( const string & expanded_path, 
        const string & hostname )
{
    ostringstream oss;
    size_t host_value = getHostDirId(hostname); 
    oss << expanded_path << "/" << HOSTDIRPREFIX << host_value; 
    //plfs_debug("%s : %s %s -> %s\n", 
    //        __FUNCTION__, hostname, expanded_path, oss.str().c_str() );
    return oss.str();
}

// this makes the mode of a directory look like it's the mode
// of a file.  
// e.g. someone does a stat on a container, make it look like a file
mode_t Container::fileMode( mode_t mode ) {
    int dirmask  = ~(S_IFDIR);
    mode         = ( mode & dirmask ) | S_IFREG;    
    return mode;
}

// this makes a mode for a file look like a directory
// e.g. someone is creating a file which is actually a container
// so use this to get the mode to pass to the mkdir
// need to add S_IWUSR to the flag incase a file has --r--r--r
//    the file can be --r--r--r but the top level dir can't
// also need to make it a dir and need to make exec by all
mode_t Container::dirMode( mode_t mode ) {
    mode = (mode) | S_IRUSR | S_IWUSR | S_IXUSR | S_IXGRP | S_IXOTH;
    return mode;
}

mode_t Container::containerMode( mode_t mode ) {
    return dirMode(mode);
}

int Container::createHelper(const string &expanded_path, const string &hostname,
        mode_t mode, int flags, int *extra_attempts, pid_t pid ) 
{
    // TODO we're in a mutex here so only one thread will
    // make the dir, and the others will stat it
    // but we could reduce the number of stats by maintaining
    // some memory state that the first thread sets and the
    // others check

        // first the top level container
    double begin_time, end_time;
    int res = 0;
    if ( ! isContainer( expanded_path.c_str(), NULL ) ) {
        plfs_debug("Making top level container %s %x\n", 
                expanded_path.c_str(),mode);
        begin_time = time(NULL);
        res = makeTopLevel( expanded_path, hostname, mode, pid );
        end_time = time(NULL);
        if ( end_time - begin_time > 2 ) {
            plfs_debug("WTF: TopLevel create of %s took %.2f\n", 
                    expanded_path.c_str(), end_time - begin_time );
        }
        if ( res != 0 ) {
            plfs_debug("Failed to make top level container %s:%s\n",
                    expanded_path.c_str(), strerror(errno));
            return res;
        }
    }

        // then the host dir
        // this is an interesting dilemna here.
        // if we don't make the hostdir here, then N-1 through FUSE
        // will distribute itself across backends 
        // but N-N through ADIO will make extra containers (one hashed
        // by name and one hashed by node)
        // is the performance for ADIO N-1 better with this removed?
        // currently, the LANL main target is ADIO so let's leave this
        // optimized for ADIO.  Later, we can augment the args to take the
        // open opts which specify whether we're in ADIO or FUSE so we could
        // optimize accordingly
    if ( res == 0 ) {
        res = makeHostDir( expanded_path, hostname, mode, PARENT_CREATED ); 
    }
    return res;
}

// This should be in a mutex if multiple procs on the same node try to create
// it at the same time
int Container::create( const string &expanded_path, const string &hostname,
        mode_t mode, int flags, int *extra_attempts, pid_t pid ) 
{
    int res = 0;
    do {
        res = createHelper(expanded_path, hostname, mode,flags,extra_attempts,
                pid);
        if ( res != 0 ) {
            if ( errno != EEXIST && errno != ENOENT && errno != EISDIR
                    && errno != ENOTEMPTY ) 
            {
                // if it's some other errno, than it's a real error so return it
                res = -errno;
                break;
            }
        }
        if ( res != 0 ) (*extra_attempts)++;
    } while( res && *extra_attempts <= 5 );

    return res;
}

// returns the first dirent that matches a prefix (or NULL)
struct dirent *Container::getnextent( DIR *dir, const char *prefix ) {
    if ( dir == NULL ) return NULL; // this line not necessary, but doesn't hurt
    struct dirent *next = NULL;
    do {
        next = readdir( dir );
    } while( next && prefix && 
            strncmp( next->d_name, prefix, strlen(prefix) ) != 0 );
    return next;
}

// this function traverses a container and returns the next dropping
// it is shared by different parts of the code that want to traverse
// a container and fetch all the indexes or to traverse a container
// and fetch all the chunks
// this returns 0 if done.  1 if OK.  -errno if a problem
int Container::nextdropping( const string& physical_path, 
        string *droppingpath, const char *dropping_type,
        DIR **topdir, DIR **hostdir, struct dirent **topent ) 
{
    ostringstream oss;
    oss << "looking for nextdropping in " << physical_path; 
    //plfs_debug("%s\n", oss.str().c_str() );
        // open it on the initial 
    if ( *topdir == NULL ) {
        Util::Opendir( physical_path.c_str(), topdir );
        if ( *topdir == NULL ) return -errno;
    }

        // make sure topent is valid
    if ( *topent == NULL ) {
            // here is where nextdropping is specific to HOSTDIR
        *topent = getnextent( *topdir, HOSTDIRPREFIX );
        if ( *topent == NULL ) {
            // all done
            Util::Closedir( *topdir );
            *topdir = NULL;
            return 0;
        }
    }

        // set up the hostpath here.  We either need it in order to open hostdir
        // or we need it in order to populate the chunk
    string hostpath = physical_path;
    hostpath       += "/";
    hostpath       += (*topent)->d_name;

        // make sure hostdir is valid
        // oh, ok, this only looks for droppings in the hostdir
        // but we need it to look for droppings everywhere
        // is that right?
    if ( *hostdir == NULL ) {
        Util::Opendir( hostpath.c_str(), hostdir );
        if ( *hostdir == NULL ) {
            plfs_debug("opendir %s: %s\n",
                    hostpath.c_str(),strerror(errno));
            return -errno;
        } else {
            plfs_debug("%s opened dir %s\n", 
                    __FUNCTION__, hostpath.c_str() );
        }
    }

        // get the next hostent, if null, reset topent and hostdir and try again
    struct dirent *hostent = getnextent( *hostdir, dropping_type );
    if ( hostent == NULL ) {
        Util::Closedir( *hostdir );
        *topent  = NULL;
        *hostdir = NULL;
        return nextdropping( physical_path, droppingpath, dropping_type, 
                topdir, hostdir, topent );
    }

        // once we make it here, we have a hostent to an dropping 
    droppingpath->clear();
    droppingpath->assign( hostpath + "/" + hostent->d_name );
    return 1;
}

// this should be called when the truncate offset is less than the
// current size of the file
// we don't actually remove any data here, we just edit index files
// and meta droppings
// when a file is truncated to zero, that is handled separately and
// that does actually remove data files
// returns 0 or -errno
int Container::Truncate( const string &path, off_t offset ) {
    int ret;
    string indexfile;

	// this code here goes through each index dropping and rewrites it
	// preserving only entries that contain data prior to truncate offset
    DIR *td = NULL, *hd = NULL; struct dirent *tent = NULL;
    while((ret = nextdropping(path,&indexfile,INDEXPREFIX, &td,&hd,&tent))== 1){
        Index index( indexfile, -1 );
        plfs_debug("%s new idx %p %s\n", __FUNCTION__,&index,indexfile.c_str());
        ret = index.readIndex( indexfile );
        if ( ret == 0 ) {
            if ( index.lastOffset() > offset ) {
                plfs_debug("%s %p at %ld\n",__FUNCTION__,&index,offset);
                index.truncate( offset );
                int fd = Util::Open(indexfile.c_str(), O_TRUNC | O_WRONLY);
                if ( fd < 0 ) {
                    cerr << "Couldn't overwrite index file " << indexfile
                         << ": " << strerror( fd ) << endl;
                    return -errno;
                }
                ret = index.rewriteIndex( fd );
                Util::Close( fd );
                if ( ret != 0 ) break;
            }
        } else {
            cerr << "Failed to read index file " << indexfile 
                 << ": " << strerror( -ret ) << endl;
            break;
        }
    }

	if ( ret == 0 ) {
		ret = truncateMeta(path,offset);
	}
	/*
	// if this code is still here in 2011, delete it
    // now remove all the meta droppings
    ret = Util::Opendir( getMetaDirPath( path ).c_str(), &td ); 
    if ( ret == 0 ) {
        while( ( tent = readdir( td ) ) != NULL ) {
            if ( strcmp( ".", tent->d_name ) && strcmp( "..", tent->d_name ) ) {
                string metadropping = getMetaDirPath( path );
                metadropping += "/"; metadropping += tent->d_name;
                plfs_debug("Need to remove meta dropping %s\n",
                        metadropping.c_str() );
                Util::Unlink( metadropping.c_str() ); 
            }
        }
        Util::Closedir( td );
    }
	*/
    plfs_debug("%s on %s to %ld ret: %d\n", 
            __FUNCTION__, path.c_str(), (long)offset, ret);
    return ret;
}

int
Container::truncateMeta(const string &path, off_t offset){

	// TODO:
   	// it's unlikely but if a previously closed file is truncated
   	// somewhere in the middle, then future stats on the file will
   	// be incorrect because they'll reflect incorrect droppings in
   	// METADIR, so we need to go through the droppings in METADIR
   	// and modify or remove droppings that show an offset beyond
   	// this truncate point

   	struct dirent *dent         = NULL;
   	DIR *dir                    = NULL;
   	string meta_path            = getMetaDirPath(path);

   	int ret = Util::Opendir( meta_path.c_str() , &dir );
    
   	if ( dir == NULL ) { 
		plfs_debug("%s wtf\n", __FUNCTION__ );
		return 0; 
   	}

   	while( ret == 0 && (dent = readdir( dir )) != NULL ) {
		if ( !strcmp( ".", dent->d_name ) || !strcmp( "..", dent->d_name ) ) {
			continue;
		}
        if ( !strncmp( OPENPREFIX, dent->d_name, strlen(OPENPREFIX))) {
            continue;   // skip open droppings
        }
		string full_path( meta_path ); full_path += "/"; 
		full_path += dent->d_name;
   	
		off_t last_offset;
		size_t total_bytes;
		struct timespec time;
		ostringstream oss;
		string host = fetchMeta( dent->d_name, 
			&last_offset, &total_bytes, &time );
        // We are making smaller so we can use the offset as the total_bytes
		if(last_offset > offset) {
			oss << meta_path << "/" << offset << "."  
				<< offset    << "." << time.tv_sec 
				<< "." << time.tv_nsec << "." << host;
			ret = Util::Rename(full_path.c_str(), oss.str().c_str());
			if ( ret != 0 ) {
				plfs_debug("%s wtf, Rename of metadata in truncate failed\n",
					__FUNCTION__ );
			}
		}
   	}
    // This was added because valgrind discovered a memory leak from not closing the directory
    // pointer
    ret=Util::Closedir(dir);
    return ret;
}
