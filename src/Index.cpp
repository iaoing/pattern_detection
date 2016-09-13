#include <errno.h>
#include "COPYRIGHT.h"
#include <string>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <dirent.h>
#include <math.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include "plfs.h"
#include "Container.h"
#include "Index.h"
#include "plfs_private.h"

#ifndef MAP_NOCACHE
    // this is a way to tell mmap not to waste buffer cache.  since we just
    // read the index files once sequentially, we don't want it polluting cache
    // unfortunately, not all platforms support this (but they're small)
    #define MAP_NOCACHE 0
#endif

bool HostEntry::overlap( const HostEntry &other ) {
    return(contains(other.logical_offset) || other.contains(logical_offset));
}

bool HostEntry::contains( off_t offset ) const {
    return(offset >= logical_offset && offset < logical_offset + (off_t)length);
}

// subtly different from contains: excludes the logical offset
// (i.e. > instead of >= 
bool HostEntry::splittable( off_t offset ) const {
    return(offset > logical_offset && offset < logical_offset + (off_t)length);
}

bool HostEntry::abut( const HostEntry &other ) {
    return logical_offset + (off_t)length == other.logical_offset
        || other.logical_offset + (off_t)other.length == logical_offset;
}

off_t HostEntry::logical_tail() const {
    return logical_offset + (off_t)length - 1;
}

// a helper routine for global_to_stream: copies to a pointer and advances it
char *memcpy_helper(char *dst, void *src, size_t len) {
    char *ret = (char*)memcpy((void*)dst,src,len);
    ret += len;
    return ret;
}

// Addedd these next set of function for par index read
// might want to use the constructor for something useful
IndexFileInfo::IndexFileInfo(){
}

void * IndexFileInfo::listToStream(vector<IndexFileInfo> &list,int *bytes)
{

    char *buffer;
    char *buf_pos;
    int size;
    vector<IndexFileInfo>::iterator itr;

    for(itr=list.begin();itr!=list.end();itr++){
        (*bytes)+=sizeof(double);
        (*bytes)+=sizeof(pid_t);
        (*bytes)+=sizeof(int);
        // Null terminating char
        (*bytes)+=(*itr).hostname.size()+1;
    }
    // Make room for number of Index File Info
    (*bytes)+=sizeof(int);
    
    // This has to be freed somewhere
    buffer=(char *)malloc(*bytes);
    if(!buffer){
        *bytes=-1;
        return (void*)buffer;
    }
    buf_pos=buffer;

    size=list.size();
    buf_pos=memcpy_helper(buf_pos,&size,sizeof(int));

    for(itr=list.begin();itr!=list.end();itr++){
        double timestamp = (*itr).timestamp;
        pid_t  id = (*itr).id;
        // Putting the plus one for the null terminating  char
        // Try using the strcpy function
        int len =(*itr).hostname.size()+1;
        plfs_debug("Size of hostname is %d\n",len);
        char * hostname = strdup((*itr).hostname.c_str());
        buf_pos=memcpy_helper(buf_pos,&timestamp,sizeof(double));
        buf_pos=memcpy_helper(buf_pos,&id,sizeof(pid_t));
        buf_pos=memcpy_helper(buf_pos,&len,sizeof(int));
        buf_pos=memcpy_helper(buf_pos,(void *)hostname,len);
        free(hostname);
     }

    return (void *)buffer;
}

vector<IndexFileInfo> IndexFileInfo::streamToList(void * addr){

    vector<IndexFileInfo> list;

    int * sz_ptr;
    int size,count;

    sz_ptr = (int *)addr;
    size = sz_ptr[0];
    // Skip past the count
    addr = (void *)&sz_ptr[1];

    for(count=0;count<size;count++){
        int hn_sz;
        double * ts_ptr;
        pid_t* id_ptr;
        int *hnamesz_ptr;
        char *hname_ptr;
        string hostname;
        
        IndexFileInfo index_dropping;
        ts_ptr=(double *)addr;
        index_dropping.timestamp=ts_ptr[0];
        addr = (void *)&ts_ptr[1];
        id_ptr=(pid_t *)addr;
        index_dropping.id=id_ptr[0];
        addr = (void*)&id_ptr[1];
        hnamesz_ptr=(int *)addr;
        hn_sz=hnamesz_ptr[0];
        addr= (void*)&hnamesz_ptr[1];
        hname_ptr=(char *)addr;
        hostname.append(hname_ptr);
        index_dropping.hostname=hostname;
        addr=(void *)&hname_ptr[hn_sz];

        list.push_back(index_dropping);
        /*if(count==0 || count ==1){
            printf("stream to list size:%d \n",size);
            printf("TS :%f |",index_dropping.getTimeStamp());
            printf(" ID: %d |\n",index_dropping.getId());
            printf("HOSTNAME: %s\n",index_dropping.getHostname().c_str());
        }
        */
    }
    
    return list;
}

// for dealing with partial overwrites, we split entries in half on split
// points.  copy *this into new entry and adjust new entry and *this
// accordingly.  new entry gets the front part, and this is the back.
// return new entry
ContainerEntry ContainerEntry::split(off_t offset) {
    assert(contains(offset));   // the caller should ensure this 
    ContainerEntry front = *this;
    off_t split_offset = offset - this->logical_offset;
    front.length = split_offset;
    this->length -= split_offset;
    this->logical_offset += split_offset;
    this->physical_offset += split_offset;
    return front;
}

bool ContainerEntry::abut( const ContainerEntry &other ) {
    return ( HostEntry::abut(other) && 
            ( physical_offset + (off_t)length == other.physical_offset 
             || other.physical_offset + (off_t)other.length == physical_offset ) );
}

bool ContainerEntry::mergable( const ContainerEntry &other ) {
    return ( id == other.id && abut(other) );
}

ostream& operator <<(ostream &os,const ContainerEntry &entry) {
    double begin_timestamp = 0, end_timestamp = 0;
    begin_timestamp = entry.begin_timestamp;
    end_timestamp  = entry.end_timestamp;
    os  << setw(5) 
        << entry.id             << " w " 
        << setw(16)
        << entry.logical_offset << " " 
        << setw(8) << entry.length << " "
        << setw(16) << fixed << setprecision(16) 
        << begin_timestamp << " "
        << setw(16) << fixed << setprecision(16) 
        << end_timestamp   << " "
        << setw(16)
        << entry.logical_tail() << " "
        << " [" << entry.id << "." << setw(10) << entry.physical_offset << "]";
    return os;
}

ostream& operator <<(ostream &os,const Index &ndx ) {
    os << "# Index of " << ndx.logical_path << endl;
    os << "# Data Droppings" << endl;
    for(unsigned i = 0; i < ndx.chunk_map.size(); i++ ) {
        os << "# " << i << " " << ndx.chunk_map[i].path << endl;
    }
    map<off_t,ContainerEntry>::const_iterator itr;
    os << "# Entry Count: " << ndx.global_index.size() << endl;
    os << "# ID Logical_offset Length Begin_timestamp End_timestamp "
       << " Logical_tail ID.Chunk_offset " << endl;
    for(itr = ndx.global_index.begin();itr != ndx.global_index.end();itr++){
        os << itr->second << endl;
    }
    return os;
}

void Index::init( string logical ) {
    logical_path    = logical;
    populated       = false;
    buffering       = false;
    buffer_filled   = false;
    chunk_id        = 0;
    last_offset     = 0;
    total_bytes     = 0;
    hostIndex.clear();
    global_index.clear();
    chunk_map.clear();
    pthread_mutex_init( &fd_mux, NULL );
}

Index::Index( string logical, int fd ) : Metadata::Metadata() {
    init( logical );
    this->fd = fd;
    ostringstream os;
    os << __FUNCTION__ << ": " << this << " created index on " <<
        logical_path << endl;
    plfs_debug("%s", os.str().c_str() );
}

void
Index::lock( const char *function ) {
    Util::MutexLock( &fd_mux, function );

}

void
Index::unlock( const char *function ) {
    Util::MutexUnlock( &fd_mux, function );

}

Index::Index( string logical ) : Metadata::Metadata() {
    init( logical );
    ostringstream os;
    os << __FUNCTION__ << ": " << this 
       << " created index on " << logical_path << ", "
       << chunk_map.size() << " chunks" << endl;
    plfs_debug("%s", os.str().c_str() );
}

void Index::setPath( string p ) {
    this->logical_path = p;
}

Index::~Index() {
    ostringstream os;
    os << __FUNCTION__ << ": " << this 
       << " removing index on " << logical_path << ", " 
       << chunk_map.size() << " chunks"<< endl;
    plfs_debug("%s", os.str().c_str() );
    plfs_debug("There are %d chunks to close fds for\n", chunk_map.size());
    for( unsigned i = 0; i < chunk_map.size(); i++ ) {
        if ( chunk_map[i].fd > 0 ) {
            plfs_debug("Closing fd %d for %s\n",
                    (int)chunk_map[i].fd, chunk_map[i].path.c_str() );
            Util::Close( chunk_map[i].fd );
        }
    }
    pthread_mutex_destroy( &fd_mux );
    // I think these just go away, no need to clear them
    /*
    hostIndex.clear();
    global_index.clear();
    chunk_map.clear();
    */
}

void 
Index::startBuffering() {
    this->buffering=true; 
    this->buffer_filled=false;
}

void 
Index::stopBuffering() {
    this->buffering=false;
    this->buffer_filled=true;
    global_index.clear();
}

bool 
Index::isBuffering() {
    return this->buffering;
}
     
// this function makes a copy of the index
// and then clears the existing one
// walks the copy and merges where possible
// and then inserts into the existing one
void Index::compress() {
    // if ( global_index.size() <= 1 ) return;
    // map<off_t,ContainerEntry> old_global = global_index;
    // map<off_t,ContainerEntry>::const_iterator itr = old_global.begin();
    // global_index.clear();
    // ContainerEntry pEntry = itr->second;
    // bool merged = false;
    // while( ++itr != old_global.end() ) {
    //     if ( pEntry.mergable( itr->second ) ) {
    //         pEntry.length += itr->second.length;
    //         merged = true;
    //     } else {
    //         insertGlobal( &pEntry ); 
    //         pEntry = itr->second;
    //         merged = false;
    //     }
    // }
    // // need to put in the last one(s)
    // insertGlobal( &pEntry );
    /*
        // I think this line always inserts something that was already inserted
    if ( ! merged ) {
        pEntry = (--itr)->second;
        insertGlobal( &pEntry );
    }
    */
}

// merge another index into this one
// we're not looking for errors here probably we should....
void Index::merge(Index *other) {
        // the other has it's own chunk_map and the ContainerEntry have
        // an index into that chunk_map

        // copy over the other's chunk_map and remember how many chunks
        // we had originally
    size_t chunk_map_shift = chunk_map.size();
    vector<ChunkFile>::iterator itr;
    for(itr = other->chunk_map.begin(); itr != other->chunk_map.end(); itr++){
        chunk_map.push_back(*itr);
    }
        // copy over the other's container entries but shift the index 
        // so they index into the new larger chunk_map


    map<off_t,PatternUnitDeSe>::const_iterator ce_itr;
    map<off_t,PatternUnitDeSe> *og = &(other->pat_List_DeSe.dese_map);
    for( ce_itr = og->begin(); ce_itr != og->end(); ce_itr++ ) {
        PatternUnitDeSe entry = ce_itr->second;
        // Don't need to shift in the case of flatten on close
        entry.known_chunk += chunk_map_shift;
        pat_List_DeSe.dese_map.insert(pair<off_t,PatternUnitDeSe>(ce_itr->first, entry));
        // insertGlobal(&entry);
    }
}

off_t Index::lastOffset() {
    return last_offset;
}

size_t Index::totalBytes() {
    return total_bytes;
}

bool Index::ispopulated( ) {
    return populated;
}

// returns 0 or -errno
// this dumps the local index
// and then clears it
int Index::flush() {
    // ok, vectors are guaranteed to be contiguous
    // so just dump it in one fell swoop
    size_t  len = hostIndex.size() * sizeof(HostEntry);
    ostringstream os;
    os << __FUNCTION__ << " flushing : " << len << " bytes" << endl; 
    plfs_debug("%s", os.str().c_str() );
    if ( len == 0 ) return 0;   // could be 0 if we weren't buffering
    // valgrind complains about writing uninitialized bytes here....
    // but it's fine as far as I can tell.


    PatternIndex p_index;
    for(HostEntry h_entry : hostIndex)
    {
        p_index.collectIndex(h_entry);
    }
    p_index.pattern_detection();
    string buf;
    size_t totltsize = p_index.append_to_buf(&buf);
    len = buf.size();
    if ( len == 0 ) return 0;
    void *start = (void *)&buf[0];


    int ret     = Util::Writen( fd, start, len );
    if ( (size_t)ret != (size_t)len ) {
        plfs_debug("%s failed write to fd %d: %s\n", 
                __FUNCTION__, fd, strerror(errno));
    }
    hostIndex.clear();
    return ( ret < 0 ? -errno : 0 );
}

// takes a path and returns a ptr to the mmap of the file 
// also computes the length of the file
void *Index::mapIndex( string hostindex, int *fd, off_t *length ) {
    void *addr;
    *fd = Util::Open( hostindex.c_str(), O_RDONLY );
    if ( *fd < 0 ) {
        return NULL;
    }
    // lseek doesn't always see latest data if panfs hasn't flushed
    // could be a zero length chunk although not clear why that gets
    // created.  
    Util::Lseek( *fd, 0, SEEK_END, length );
    if ( *length <= 0 ) {
        plfs_debug("%s is a zero length index file\n", hostindex.c_str() );
        return NULL;
    }

    Util::Mmap(*length,*fd,&addr);
    return addr;
}


// this builds a global in-memory index from a physical host index dropping
// return 0 for sucess, -errno for failure
int Index::readIndex( string hostindex ) {
    off_t length = (off_t)-1;
    int   fd = -1;
    void  *maddr = NULL;
    populated = true;

    ostringstream os;
    os << __FUNCTION__ << ": " << this << " reading index on " <<
        logical_path << endl;
    plfs_debug("%s", os.str().c_str() );

    maddr = mapIndex( hostindex, &fd, &length );
    if( maddr == (void*)-1 ) {
        return cleanupReadIndex( fd, maddr, length, 0, "mapIndex",
            hostindex.c_str() );
    }

    if(length < sizeof(int))
        return cleanupReadIndex(fd, maddr, length, 0, "mapIndex",hostindex.c_str());

    map<pid_t,pid_t> known_chunks;

    //DeSe the maddr into dese_map;
    pat_List_DeSe.Pattern_DeSe(maddr, length);

    //set the chunk_map;
    map<off_t, PatternUnitDeSe>::iterator iter;
    for(iter = pat_List_DeSe.dese_map.begin(); iter != pat_List_DeSe.dese_map.end(); ++iter)
    {
        if (known_chunks.find(iter->second.id) == known_chunks.end()) {
            ChunkFile cf;
            cf.path = Container::chunkPathFromIndexPath(hostindex, iter->second.id);
            cf.fd = -1;
            chunk_map.push_back( cf );
            known_chunks[iter->second.id]  = chunk_id++;
            //assert( (size_t)chunk_id == chunk_map.size() );
            plfs_debug("Inserting chunk %s (%d)\n", cf.path.c_str(),
                chunk_map.size());
        }
        iter->second.known_chunk = known_chunks[iter->second.id];
    }

    plfs_debug("After %s in %p, now are %d chunks\n",
        __FUNCTION__,this,chunk_map.size());
    return cleanupReadIndex(fd, maddr, length, 0, "DONE",hostindex.c_str());
}

// constructs a global index from a "stream" (i.e. a chunk of memory)
// returns 0 or -errno
int Index::global_from_stream(void *addr) {

    // first read the header to know how many entries there are
    size_t quant = 0;
    size_t *sarray = (size_t*)addr;
    quant = sarray[0];
    if ( quant < 0 ) return -EBADF;
    plfs_debug("%s for %s has %ld entries\n",
            __FUNCTION__,logical_path.c_str(),(long)quant);

    // then skip past the header
    addr = (void*)&(sarray[1]);

    // then read in all the entries
    ContainerEntry *entries = (ContainerEntry*)addr;
    for(size_t i=0;i<quant;i++) {
        ContainerEntry e = entries[i];
        // just put it right into place. no need to worry about overlap
        // since the global index on disk was already pruned
        // UPDATE : The global index may never touch the disk
        // this happens on our broadcast on close optimization
        
        // Something fishy here we insert the address of the entry
        // in the Index::insertGlobal code 
        //global_index[e.logical_offset] = e;
        insertGlobalEntry(&e);
    }

    // then skip past the entries
    addr = (void*)&(entries[quant]);

    // now read in the vector of chunk files
    plfs_debug("%s of %s now parsing data chunk paths\n",
            __FUNCTION__,logical_path.c_str());
    vector<string> chunk_paths;
    tokenize((char*)addr,"\n",chunk_paths); // might be inefficient...
    for( size_t i = 0; i < chunk_paths.size(); i++ ) {
        if(chunk_paths[i].size()<7) continue;
        ChunkFile cf;
        cf.path = logical_path + "/" + chunk_paths[i];
        cf.fd = -1;
        chunk_map.push_back(cf);
    }

    return 0;
}

// Helper function to debug global_to_stream
int Index::debug_from_stream(void *addr){

    // first read the header to know how many entries there are
    size_t quant = 0;
    size_t *sarray = (size_t*)addr;
    quant = sarray[0];
    if ( quant < 0 ) {
        plfs_debug("WTF the size of your stream index is less than 0\n");
        return -1;
    }
    plfs_debug("%s for %s has %ld entries\n",
        __FUNCTION__,logical_path.c_str(),(long)quant);
    // then skip past the entries
    ContainerEntry *entries = (ContainerEntry*)addr;
    addr = (void*)&(entries[quant]);

    // now read in the vector of chunk files
    plfs_debug("%s of %s now parsing data chunk paths\n",
                __FUNCTION__,logical_path.c_str());
    vector<string> chunk_paths;
    tokenize((char*)addr,"\n",chunk_paths); // might be inefficient...
    for( size_t i = 0; i < chunk_paths.size(); i++ ) {
        plfs_debug("Chunk path:%d is :%s\n",i,chunk_paths[i].c_str());
    }
    return 0;
}

// this writes a flattened in-memory global index to a physical file
// returns 0 or -errno
int Index::global_to_file(int fd){
    void *buffer; 
    size_t length;
    int ret = global_to_stream(&buffer,&length);
    if (ret==0) {
        ret = Util::Writen(fd,buffer,length);
        ret = ( (size_t)ret == length ? 0 : -errno );
        free(buffer); 
    }
    return ret;
}

// this writes a flattened in-memory global index to a memory address
// it allocates the memory.  The caller must free it.
// returns 0 or -errno
int Index::global_to_stream(void **buffer,size_t *length) {
    int ret = 0;
    // Global ?? or this
    size_t quant = global_index.size();

    //Check if we stopped buffering, if so return -1 and length of -1
    if(buffering && buffer_filled){
        *length=(size_t)-1;
        return -1;
    }
    
    // first build the vector of chunk paths, trim them to relative
    // to the container so they're smaller and still valid after rename
    // this gets written last but compute it first to compute length
    ostringstream chunks;
    for(unsigned i = 0; i < chunk_map.size(); i++ ) {
        chunks << chunk_map[i].path.substr(logical_path.length()) << endl;
    }
    chunks << '\0'; // null term the file
    size_t chunks_length = chunks.str().length();

    // compute the length 
    *length = sizeof(quant);    // the header
    *length += quant*sizeof(ContainerEntry); 
    *length += chunks_length; 

    // allocate the buffer
    *buffer = malloc(*length);
    // Let's check this malloc and make sure it succeeds
    if(!buffer){
        plfs_debug("%s, Malloc of stream buffer failed\n",__FUNCTION__);
        return -1;
    }
    char *ptr = (char*)*buffer;
    if ( ! *buffer ) return -ENOMEM;

    // copy in the header
    ptr = memcpy_helper(ptr,&quant,sizeof(quant));
    plfs_debug("%s: Copied header for global index of %s\n",
            __FUNCTION__, logical_path.c_str()); 

    // copy in each container entry
    size_t  centry_length = sizeof(ContainerEntry);
    map<off_t,ContainerEntry>::iterator itr;
    for( itr = global_index.begin(); itr != global_index.end(); itr++ ) {
        void *start = &(itr->second);
        ptr = memcpy_helper(ptr,start,centry_length);
    }
    plfs_debug("%s: Copied %ld entries for global index of %s\n",
            __FUNCTION__, (long)quant,logical_path.c_str()); 

    // copy the chunk paths
    ptr = memcpy_helper(ptr,(void*)chunks.str().c_str(),chunks_length);
    plfs_debug("%s: Copied the chunk map for global index of %s\n",
            __FUNCTION__, logical_path.c_str()); 
    assert(ptr==(char*)*buffer+*length);

    return ret;
}

size_t Index::splitEntry( ContainerEntry *entry, 
        set<off_t> &splits, 
        multimap<off_t,ContainerEntry> &entries) 
{
    set<off_t>::iterator itr;
    size_t num_splits = 0;
    for(itr=splits.begin();itr!=splits.end();itr++) {
        // break it up as needed, and insert every broken off piece
        if ( entry->splittable(*itr) ) {
            /*
            ostringstream oss;
            oss << "Need to split " << endl << *entry << " at " << *itr << endl;
            plfs_debug("%s",oss.str().c_str());
            */
            ContainerEntry trimmed = entry->split(*itr);
            entries.insert(make_pair(trimmed.logical_offset,trimmed));
            num_splits++;
        }
    }
    // insert whatever is left
    entries.insert(make_pair(entry->logical_offset,*entry));
    return num_splits;
}

void Index::findSplits(ContainerEntry &e,set<off_t> &s) {
    s.insert(e.logical_offset);
    s.insert(e.logical_offset+e.length);
}


// to deal with overlapped write records
// we split them into multiple writes where each one is either unique
// or perfectly colliding with another (i.e. same logical offset and length)
// then we keep all the unique ones and the more recent of the colliding ones
// 
// adam says this is most complex code in plfs.  Here's longer explanation: 
// A) we tried to insert an entry, incoming,  and discovered that it overlaps w/
// other entries already in global_index
// the attempted insertion was either: 
// 1) successful bec offset didn't collide but the range overlapped with others
// 2) error bec the offset collided with existing
// B) take the insert iterator and move it backward and forward until we find
// all entries that overlap with incoming.  As we do this, also create a set
// of all offsets, splits, at beginning and end of each entry
// C) remove those entries from global_index and insert into temporary 
// container, overlaps.
// if incoming was successfully insert originally, it's already in this range
// else we also need to explicity insert it
// D) iterate through overlaps and split each entry at split points, insert
// into yet another temporary multimap container, chunks
// all of the entries in chunks will either be:
// 1) unique, they don't overlap with any other chunk
// 2) or a perfect collision with another chunk (i.e. off_t and len are same)
// E) iterate through chunks, insert into temporary map container, winners
// on collision (i.e. insert failure) only retain entry with higher timestamp
// F) finally copy all of winners back into global_index
int Index::handleOverlap(ContainerEntry &incoming,
        pair<map<off_t,ContainerEntry>::iterator, bool> &insert_ret ) 
{

    // all the stuff we use
    map<off_t,ContainerEntry>::iterator first, last, cur; // place holders
    cur = first = last = insert_ret.first;
    set<off_t> splits;  // offsets to use to split into chunks
    multimap<off_t,ContainerEntry> overlaps;    // all offending entries
    multimap<off_t,ContainerEntry> chunks;   // offending entries nicely split
    map<off_t,ContainerEntry> winners;      // the set to keep 
    ostringstream oss;

    // OLD: this function is easier if incoming is not already in global_index
    // NEW: this used to be true but for overlap2 it was breaking things.  we 
    // wanted this here so we wouldn't insert it twice.  But now we don't insert
    // it twice so now we don't need to remove incoming here
\
    // find all existing entries that overlap
    // and the set of offsets on which to split
    // I feel like cur should be at most one away from the first overlap
    // but I've seen empirically that it's not, so move backwards while we
    // find overlaps, and then forwards the same
    for(first=insert_ret.first;;first--) {
        if (!first->second.overlap(incoming)) {  // went too far
            plfs_debug("Moving first %ld forward, "
                    "no longer overlaps with incoming %ld\n",
                    first->first, incoming.logical_offset);
            first++;
            break;
        }
        findSplits(first->second,splits);
        if ( first == global_index.begin() ) break;
    }
    for(;(last != global_index.end()) && (last->second.overlap(incoming)); last++ ){
        findSplits(last->second,splits);
    }
    findSplits(incoming,splits);  // get split points from incoming as well

    // now that we've found the range of overlaps, 
    // 1) put them in a temporary multimap with the incoming,
    // 2) remove them from the global index,
    // 3) then clean them up and reinsert them into global

    // insert the incoming if it wasn't already inserted into global (1)
    if ( insert_ret.second == false ) {
        overlaps.insert(make_pair(incoming.logical_offset,incoming));
    }
    overlaps.insert(first,last);    // insert the remainder (1)
    global_index.erase(first,last); // remove from global (2)

    /*
    // spit out debug info about our temporary multimap and our split points
    oss << "Examing the following overlapped entries: " << endl;
    for(cur=overlaps.begin();cur!=overlaps.end();cur++) oss<<cur->second<< endl;
    oss << "List of split points";
    for(set<off_t>::iterator i=splits.begin();i!=splits.end();i++)oss<<" "<< *i;
    oss << endl;
    */

    // now split each entry on split points and put split entries into another
    // temporary container chunks (3)
    for(cur=overlaps.begin();cur!=overlaps.end();cur++) {
        splitEntry(&cur->second,splits,chunks);
    }

    // now iterate over chunks and insert into 3rd temporary container, winners
    // on collision, possibly swap depending on timestamps
    multimap<off_t,ContainerEntry>::iterator chunks_itr;
    pair<map<off_t,ContainerEntry>::iterator,bool> ret;
    oss << "Entries have now been split:" << endl;
    for(chunks_itr=chunks.begin();chunks_itr!=chunks.end();chunks_itr++){
        oss << chunks_itr->second << endl;
        // insert all of them optimistically
        ret = winners.insert(make_pair(chunks_itr->first,chunks_itr->second));
        if ( ! ret.second ) { // collision
            // check timestamps, if one already inserted
            // is older, remove it and insert this one
            if ( ret.first->second.end_timestamp 
                    < chunks_itr->second.end_timestamp ) 
            {
                winners.erase(ret.first);
                winners.insert(make_pair(chunks_itr->first,chunks_itr->second));
            }
        }
    }

    oss << "Entries have now been trimmed:" << endl;
    for(cur=winners.begin();cur!=winners.end();cur++) oss << cur->second <<endl;
    plfs_debug("%s",oss.str().c_str());

    // I've seen weird cases where when a file is continuously overwritten
    // slightly (like config.log), that it makes a huge mess of small little
    // chunks.  It'd be nice to compress winners before inserting into global

    // now put the winners back into the global index
    global_index.insert(winners.begin(),winners.end());
    return 0;
}


map<off_t,ContainerEntry>::iterator Index::insertGlobalEntryHint(
        ContainerEntry *g_entry ,map<off_t,ContainerEntry>::iterator hint) 
{
    return global_index.insert(hint, 
            pair<off_t,ContainerEntry>( g_entry->logical_offset, *g_entry ) );
}

pair<map<off_t,ContainerEntry>::iterator,bool> Index::insertGlobalEntry(
        ContainerEntry *g_entry) 
{
    last_offset = max( (off_t)(g_entry->logical_offset+g_entry->length),
                            last_offset );
    total_bytes += g_entry->length;
    return global_index.insert( 
            pair<off_t,ContainerEntry>( g_entry->logical_offset, *g_entry ) );
}

int Index::insertGlobal( ContainerEntry *g_entry ) {
    pair<map<off_t,ContainerEntry>::iterator,bool> ret;
    bool overlap  = false;

    plfs_debug("Inserting offset %ld into index of %s\n",
            (long)g_entry->logical_offset, logical_path.c_str());
    ret = insertGlobalEntry( g_entry ); 
    if ( ret.second == false ) {
        ostringstream oss;
        oss << "overlap1" <<endl<< *g_entry <<endl << ret.first->second << endl;
        plfs_debug("%s", oss.str().c_str() );
        overlap  = true;
    }

        // also, need to check against prev and next for overlap 
    map<off_t,ContainerEntry>::iterator next, prev;
    next = ret.first; next++;
    prev = ret.first; prev--;
    if ( next != global_index.end() && g_entry->overlap( next->second ) ) {
        ostringstream oss;
        oss << "overlap2 " << endl << *g_entry << endl <<next->second << endl;
        plfs_debug("%s", oss.str().c_str() );
        overlap = true;
    }
    if (ret.first!=global_index.begin() && prev->second.overlap(*g_entry) ){
        ostringstream oss;
        oss << "overlap3 " << endl << *g_entry << endl <<prev->second << endl;
        plfs_debug("%s", oss.str().c_str() );
        overlap = true;
    }

    if ( overlap ) {
        ostringstream oss;
        oss << __FUNCTION__ << " of " << logical_path << " trying to insert "
            << "overlap at " << g_entry->logical_offset << endl;
        plfs_debug("%s", oss.str().c_str() );
        handleOverlap( *g_entry, ret );
    } else {
            // might as well try to merge any potentially adjoining regions
        /*
        if ( next != global_index.end() && g_entry->abut(next->second) ) {
            cerr << "Merging index for " << *g_entry << " and " << next->second 
                 << endl;
            g_entry->length += next->second.length;
            global_index.erase( next );
        }
        if (ret.first!=global_index.begin() && g_entry->abut(prev->second) ){
            cerr << "Merging index for " << *g_entry << " and " << prev->second 
                 << endl;
            prev->second.length += g_entry->length;
            global_index.erase( ret.first );
        }
        */
    }

    return 0;
}

// just a little helper to print an error message and make sure the fd is
// closed and the mmap is unmap'd
int Index::cleanupReadIndex( int fd, void *maddr, off_t length, int ret, 
        const char *last_func, const char *indexfile )
{
    int ret2 = 0, ret3 = 0;
    if ( ret < 0 ) {
        plfs_debug("WTF.  readIndex failed during %s on %s: %s\n",
                last_func, indexfile, strerror( errno ) );
    }

    if ( maddr != NULL && maddr != (void*)-1 ) {
        ret2 = Util::Munmap( maddr, length );
        if ( ret2 < 0 ) {
            ostringstream oss;
            oss << "WTF. readIndex failed during munmap of "  << indexfile 
                 << " (" << length << "): " << strerror(errno) << endl;
            plfs_debug("%s\n", oss.str().c_str() );
            ret = ret2; // set to error
        }
    }

    if ( maddr == (void*)-1 ) {
        plfs_debug("mmap failed on %s: %s\n",indexfile,strerror(errno));
    }

    if ( fd > 0 ) {
        ret3 = Util::Close( fd );
        if ( ret3 < 0 ) {
            plfs_debug(
                    "WTF. readIndex failed during close of %s: %s\n",
                    indexfile, strerror( errno ) );
            ret = ret3; // set to error
        }
    }

    return ( ret == 0 ? 0 : -errno );
}

// returns any fd that has been stashed for a data chunk
// if an fd has not yet been stashed, it returns the initial
// value of -1
int Index::getChunkFd( pid_t chunk_id ) {
    return chunk_map[chunk_id].fd;
}

// stashes an fd for a data chunk 
// the index no longer opens them itself so that 
// they might be opened in parallel when a single logical read
// spans multiple data chunks
int Index::setChunkFd( pid_t chunk_id, int fd ) {
    chunk_map[chunk_id].fd = fd;
    return 0;
}

// we found a chunk containing an offset, return necessary stuff 
// this opens an fd to the chunk if necessary
int Index::chunkFound( int *fd, off_t *chunk_off, size_t *chunk_len, 
        off_t shift, string &path, pid_t *chunk_id, ContainerEntry *entry ) 
{
    ChunkFile *cf_ptr = &(chunk_map[entry->id]); // typing shortcut
    *chunk_off  = entry->physical_offset + shift;
    *chunk_len  = entry->length       - shift;
    *chunk_id   = entry->id;
    if( cf_ptr->fd < 0 ) {
        /*
        cf_ptr->fd = Util::Open(cf_ptr->path.c_str(), O_RDONLY);
        if ( cf_ptr->fd < 0 ) {
            plfs_debug("WTF? Open of %s: %s\n", 
                    cf_ptr->path.c_str(), strerror(errno) );
            return -errno;
        } 
        */
        // I'm not sure why we used to open the chunk file here and
        // now we don't.  If you figure it out, pls explain it here.
        // we must have done the open elsewhere.  But where and why not here?
        plfs_debug("Not opening chunk file %s yet\n", cf_ptr->path.c_str());
    }
    plfs_debug("Will read from chunk %s at off %ld (shift %ld)\n",
            cf_ptr->path.c_str(), (long)*chunk_off, (long)shift );
    *fd = cf_ptr->fd;
    path = cf_ptr->path;
    return 0;
}


/**
 * chunkFound_pat is for pat chunkFound and PatternForQuery;
 */
int 
Index::chunkFound_pat( int *fd, off_t *chunk_off, size_t *chunk_len, 
        off_t shift, string &path, pid_t *chunk_id, PatternForQuery *entry )
{
    ChunkFile *cf_ptr = &(chunk_map[entry->id]); // typing shortcut
    *chunk_off  = entry->physical_offset + shift;
    *chunk_len  = entry->length       - shift;
    *chunk_id   = entry->id;
    if( cf_ptr->fd < 0 ) {
        /*
        cf_ptr->fd = Util::Open(cf_ptr->path.c_str(), O_RDONLY);
        if ( cf_ptr->fd < 0 ) {
            plfs_debug("WTF? Open of %s: %s\n", 
                    cf_ptr->path.c_str(), strerror(errno) );
            return -errno;
        } 
        */
        // I'm not sure why we used to open the chunk file here and
        // now we don't.  If you figure it out, pls explain it here.
        // we must have done the open elsewhere.  But where and why not here?
        plfs_debug("Not opening chunk file %s yet\n", cf_ptr->path.c_str());
    }
    plfs_debug("Will read from chunk %s at off %ld (shift %ld)\n",
            cf_ptr->path.c_str(), (long)*chunk_off, (long)shift );
    *fd = cf_ptr->fd;
    path = cf_ptr->path;
    return 0;
}


// returns the fd for the chunk and the offset within the chunk
// and the size of the chunk beyond the offset 
// if the chunk does not currently have an fd, it is created here
// if the lookup finds a hole, it returns -1 for the fd and 
// chunk_len for the size of the hole beyond the logical offset
// returns 0 or -errno
int Index::globalLookup( int *fd, off_t *chunk_off, size_t *chunk_len, 
        string &path, bool *hole, pid_t *chunk_id, off_t logical ) 
{
    ostringstream os;
    os << __FUNCTION__ << ": " << this << " using index." << endl;
    plfs_debug("%s", os.str().c_str() );
    *hole = false;
    *chunk_id = (pid_t)-1;

    /* Using PatternDeSe to look up logical */

    PatternForQuery pfq;
    int ret = pat_List_DeSe.dese_map_look_up(logical, &pfq);
    if(pfq.exist  )
    {
        return chunkFound_pat( fd, chunk_off, chunk_len, 
            logical - pfq.logical_offset, path, chunk_id, &pfq);
    }else
    {
        *fd = -1;
        *chunk_len = 0;
        return 0;
    }


    global_index.clear();
    return orig_globalLookup(fd, chunk_off, chunk_len, path, hole, chunk_id, logical);


    //plfs_debug("Look up %ld in %s\n", 
    //        (long)logical, logical_path.c_str() );
    ContainerEntry entry, previous;
    MAP_ITR itr;
    MAP_ITR prev = (MAP_ITR)NULL;
        // Finds the first element whose key is not less than k. 
        // four possibilities:
        // 1) direct hit
        // 2) within a chunk
        // 3) off the end of the file
        // 4) in a hole
    itr = global_index.lower_bound( logical );

        // zero length file, nothing to see here, move along
    if ( global_index.size() == 0 ) {
        *fd = -1;
        *chunk_len = 0;
        return 0;
    }

        // back up if we went off the end
    if ( itr == global_index.end() ) {
            // this is safe because we know the size is >= 1
            // so the worst that can happen is we back up to begin()
        itr--;
    }
    if ( itr != global_index.begin() ) {
        prev = itr;
        prev--;
    }
    entry = itr->second;
    //ostringstream oss;
    //oss << "Considering whether chunk " << entry 
    //     << " contains " << logical; 
    //plfs_debug("%s\n", oss.str().c_str() );

        // case 1 or 2
    if ( entry.contains( logical ) ) {
        //ostringstream oss;
        //oss << "FOUND(1): " << entry << " contains " << logical;
        //plfs_debug("%s\n", oss.str().c_str() );
        return chunkFound( fd, chunk_off, chunk_len, 
                logical - entry.logical_offset, path, chunk_id, &entry );
    }

        // case 1 or 2
    if ( prev != (MAP_ITR)NULL ) {
        previous = prev->second;
        if ( previous.contains( logical ) ) {
            //ostringstream oss;
            //oss << "FOUND(2): "<< previous << " contains " << logical << endl;
            //plfs_debug("%s\n", oss.str().c_str() );
            return chunkFound( fd, chunk_off, chunk_len, 
                logical - previous.logical_offset, path, chunk_id, &previous );
        }
    }
        
        // now it's either before entry and in a hole or after entry and off
        // the end of the file

        // case 4: within a hole
    if ( logical < entry.logical_offset ) {
        ostringstream oss;
        oss << "FOUND(4): " << logical << " is in a hole" << endl;
        plfs_debug("%s", oss.str().c_str() );
        off_t remaining_hole_size = entry.logical_offset - logical;
        *fd = -1;
        *chunk_len = remaining_hole_size;
        *chunk_off = 0;
        *hole = true;
        return 0;
    }

        // case 3: off the end of the file
    //oss.str("");    // stupid way to clear the buffer
    //oss << "FOUND(3): " <<logical << " is beyond the end of the file" << endl;
    //plfs_debug("%s\n", oss.str().c_str() );
    *fd = -1;
    *chunk_len = 0;
    return 0;
}

int Index::orig_globalLookup( int *fd, off_t *chunk_off, size_t *chunk_len, 
        string &path, bool *hole, pid_t *chunk_id, off_t logical ) 
{
    ostringstream os;
    os << __FUNCTION__ << ": " << this << " using index." << endl;
    plfs_debug("%s", os.str().c_str() );
    *hole = false;
    *chunk_id = (pid_t)-1;
    //plfs_debug("Look up %ld in %s\n", 
    //        (long)logical, logical_path.c_str() );
    ContainerEntry entry, previous;
    MAP_ITR itr;
    MAP_ITR prev = (MAP_ITR)NULL;
        // Finds the first element whose key is not less than k. 
        // four possibilities:
        // 1) direct hit
        // 2) within a chunk
        // 3) off the end of the file
        // 4) in a hole
    itr = global_index.lower_bound( logical );

        // zero length file, nothing to see here, move along
    if ( global_index.size() == 0 ) {
        *fd = -1;
        *chunk_len = 0;
        return 0;
    }

        // back up if we went off the end
    if ( itr == global_index.end() ) {
            // this is safe because we know the size is >= 1
            // so the worst that can happen is we back up to begin()
        itr--;
    }
    if ( itr != global_index.begin() ) {
        prev = itr;
        prev--;
    }
    entry = itr->second;
    //ostringstream oss;
    //oss << "Considering whether chunk " << entry 
    //     << " contains " << logical; 
    //plfs_debug("%s\n", oss.str().c_str() );

        // case 1 or 2
    if ( entry.contains( logical ) ) {
        //ostringstream oss;
        //oss << "FOUND(1): " << entry << " contains " << logical;
        //plfs_debug("%s\n", oss.str().c_str() );
        return chunkFound( fd, chunk_off, chunk_len, 
                logical - entry.logical_offset, path, chunk_id, &entry );
    }

        // case 1 or 2
    if ( prev != (MAP_ITR)NULL ) {
        previous = prev->second;
        if ( previous.contains( logical ) ) {
            //ostringstream oss;
            //oss << "FOUND(2): "<< previous << " contains " << logical << endl;
            //plfs_debug("%s\n", oss.str().c_str() );
            return chunkFound( fd, chunk_off, chunk_len, 
                logical - previous.logical_offset, path, chunk_id, &previous );
        }
    }
        
        // now it's either before entry and in a hole or after entry and off
        // the end of the file

        // case 4: within a hole
    if ( logical < entry.logical_offset ) {
        ostringstream oss;
        oss << "FOUND(4): " << logical << " is in a hole" << endl;
        plfs_debug("%s", oss.str().c_str() );
        off_t remaining_hole_size = entry.logical_offset - logical;
        *fd = -1;
        *chunk_len = remaining_hole_size;
        *chunk_off = 0;
        *hole = true;
        return 0;
    }

        // case 3: off the end of the file
    //oss.str("");    // stupid way to clear the buffer
    //oss << "FOUND(3): " <<logical << " is beyond the end of the file" << endl;
    //plfs_debug("%s\n", oss.str().c_str() );
    *fd = -1;
    *chunk_len = 0;
    return 0;
}


// we're just estimating the area of these stl containers which ignores overhead
size_t Index::memoryFootprintMBs() {
    double KBs = 0;
    KBs += (hostIndex.size() * sizeof(HostEntry))/1024.0;
    KBs += (global_index.size()*(sizeof(off_t)+sizeof(ContainerEntry)))/1024.0;
    KBs += (chunk_map.size() * sizeof(ChunkFile))/1024.0;
    KBs += (physical_offsets.size() * (sizeof(pid_t)+sizeof(off_t)))/1024.0;
    return size_t(KBs/1024);
}

void Index::addWrite( off_t offset, size_t length, pid_t pid, 
        double begin_timestamp, double end_timestamp ) 
{
    Metadata::addWrite( offset, length );
    int quant = hostIndex.size();
    bool abutable = true;
        // we use this mode to be able to create trace vizualizations
        // so we don't want to merge anything bec that will reduce the
        // fidelity of the trace vizualization
    abutable = false; // BEWARE: 'true' path hasn't been tested in a LONG time.

        // incoming abuts with last
    if ( quant && hostIndex[quant-1].id == pid
        && hostIndex[quant-1].logical_offset + (off_t)hostIndex[quant-1].length 
            == offset )
    {
        plfs_debug("Merged new write with last at %ld\n",
             (long)hostIndex[quant-1].logical_offset ); 
        hostIndex[quant-1].length += length;
    } else {
        // where does the physical offset inside the chunk get set?
        // oh.  it doesn't.  On the read back, we assume there's a
        // one-to-one mapping btwn index and data file.  A truncate
        // which modifies the index file but not the data file will
        // break this assumption.  I believe this means we need to
        // put the physical offset into the host entries.
        // I think it also means that every open needs to be create 
        // unique index and data chunks and never append to existing ones
        // because if we append to existing ones, it means we need to
        // stat them to know where the offset is and we'd rather not
        // do a stat on the open
        //
        // so we need to do this:
        // 1) track current offset by pid in the index data structure that
        // we use for writing: DONE
        // 2) Change the merge code to only merge for consecutive writes
        // to the same pid: DONE
        // 3) remove the offset tracking when we create the read index: DONE
        // 4) add a timestamp to the index and data droppings.  make sure
        // that the code that finds an index path from a data path and
        // vice versa (if that code exists) still works: DONE
        HostEntry entry;
        entry.logical_offset = offset;
        entry.length         = length; 
        entry.id             = pid; 
        entry.begin_timestamp = begin_timestamp;
        // valgrind complains about this line as well:
        // Address 0x97373bc is 20 bytes inside a block of size 40 alloc'd
        entry.end_timestamp   = end_timestamp;

        // lookup the physical offset
        map<pid_t,off_t>::iterator itr = physical_offsets.find(pid);
        if ( itr == physical_offsets.end() ) {
            physical_offsets[pid] = 0;
        }
        entry.physical_offset = physical_offsets[pid];
        physical_offsets[pid] += length;
        hostIndex.push_back( entry );
        // Needed for our index stream function
        // It seems that we can store this pid for the global entry
        // ContainerEntry c_entry;
        // c_entry.logical_offset = entry.logical_offset;
        // c_entry.length            = entry.length;
        // c_entry.id                = entry.id;
        // c_entry.original_chunk    = entry.id;
        // c_entry.physical_offset   = entry.physical_offset;
        // c_entry.begin_timestamp   = entry.begin_timestamp;
        // c_entry.end_timestamp     = entry.end_timestamp;

        // // Only buffer if we are using the ADIO layer
        // if(buffering && !buffer_filled) insertGlobal(&c_entry);

        // Make sure we have the chunk path
        if(chunk_map.size()==0){
            ChunkFile cf;
            cf.fd = -1;
            cf.path = Container::chunkPathFromIndexPath(index_path,entry.id);
            // No good we need the Index Path please be stashed somewhere
            //plfs_debug("The hostIndex logical path is: %s\n",cf.path.c_str());
            plfs_debug("Use chunk path from index path: %s\n",cf.path.c_str());
            chunk_map.push_back( cf );
        }
    }
}

void Index::truncate( off_t offset ) {
    pat_List_DeSe.truncate(offset);
    // map<off_t,ContainerEntry>::iterator itr, prev;
    // bool first = false;
    // plfs_debug("Before %s in %p, now are %d chunks\n",
    //     __FUNCTION__,this,global_index.size());

    //     // Finds the first element whose offset >= offset. 
    // itr = global_index.lower_bound( offset );
    // if ( itr == global_index.begin() ) first = true;
    // prev = itr; prev--;
    
    //     // remove everything whose offset >= offset
    // global_index.erase( itr, global_index.end() );

    //     // check whether the previous needs to be
    //     // internally truncated
    // if ( ! first ) {
    //   if ((off_t)(prev->second.logical_offset + prev->second.length) > offset){
    //         // say entry is 5.5 that means that ten
    //         // is a valid offset, so truncate to 7
    //         // would mean the new length would be 3
    //     prev->second.length = offset - prev->second.logical_offset ;//+ 1;???
    //     plfs_debug("%s Modified a global index record to length %u\n",
    //             __FUNCTION__, (uint)prev->second.length);
    //     if (prev->second.length==0) {
    //       plfs_debug( "Just truncated index entry to 0 length\n" );
    //     }
    //   }
    // }
    // plfs_debug("After %s in %p, now are %d chunks\n",
    //     __FUNCTION__,this,global_index.size());
}

// operates on a host entry which is not sorted
void Index::truncateHostIndex( off_t offset ) {
    vector< HostEntry > new_entries;
    vector< HostEntry >::iterator itr;
    for( itr = hostIndex.begin(); itr != hostIndex.end(); itr++ ) {
        HostEntry entry = *itr;
        if ( entry.logical_offset < offset ) {
                // adjust if necessary and save this one
            if ( (off_t)(entry.logical_offset + entry.length) > offset ) {
                entry.length = offset - entry.logical_offset + 1;
            }
            new_entries.push_back( entry );
        }
    }
    hostIndex = new_entries; 
}

// ok, someone is truncating a file, so we reread a local index,
// created a partial global index, and truncated that global
// index, so now we need to dump the modified global index into
// a new local index
int Index::rewriteIndex( int fd ) {
    this->fd = fd;
    double begin_timestamp = 0, end_timestamp = 0;
    map<off_t, PatternUnitDeSe>::iterator it;
    for(it = pat_List_DeSe.dese_map.begin(); it != pat_List_DeSe.dese_map.end(); ++it)
    {
        if(it->second.queryList.empty())
            it->second.analyze_unit();
        for(PatternForQuery pfq : it->second.queryList)
        {
            addWrite( pfq.logical_offset, pfq.length, 
                pfq.id, begin_timestamp, end_timestamp );
        }
    }

    // map<off_t,ContainerEntry>::iterator itr;
    // map<double,ContainerEntry> global_index_timesort;
    // map<double,ContainerEntry>::iterator itrd;
    
    // // so this is confusing.  before we dump the global_index back into
    // // a physical index entry, we have to resort it by timestamp instead
    // // of leaving it sorted by offset.
    // // this is because we have a small optimization in that we don't 
    // // actually write the physical offsets in the physical index entries.
    // // we don't need to since the writes are log-structured so the order
    // // of the index entries matches the order of the writes to the data
    // // chunk.  Therefore when we read in the index entries, we know that
    // // the first one to a physical data dropping is to the 0 offset at the
    // // physical data dropping and the next one is follows that, etc.
    // //
    // // update, we know include physical offsets in the index entries so
    // // we don't have to order them by timestamps anymore.  However, I'm
    // // reluctant to change this code so near a release date and it doesn't
    // // hurt them to be sorted so just leave this for now even though it
    // // is technically unnecessary
    // for( itr = global_index.begin(); itr != global_index.end(); itr++ ) {
    //     global_index_timesort.insert(
    //             make_pair(itr->second.begin_timestamp,itr->second));
    // }

    // for( itrd = global_index_timesort.begin(); itrd != 
    //         global_index_timesort.end(); itrd++ ) 
    // {
    //     double begin_timestamp = 0, end_timestamp = 0;
    //     begin_timestamp = itrd->second.begin_timestamp;
    //     end_timestamp   = itrd->second.end_timestamp;
    //     addWrite( itrd->second.logical_offset,itrd->second.length, 
    //             itrd->second.original_chunk, begin_timestamp, end_timestamp );
    //     /*
    //     ostringstream os;
    //     os << __FUNCTION__ << " added : " << itr->second << endl; 
    //     plfs_debug("%s", os.str().c_str() );
    //     */
    // }
    return flush(); 
}





///////////////////////////////////////////////////////////////////////
// PatternIndex.cpp
///////////////////////////////////////////////////////////////////////

void 
PatternIndexCollect::push_entry(HostEntry h_entry)
{
    this->collect_logical.push_back(h_entry.get_logical_offset());
    this->collect_physical.push_back(h_entry.get_physical_offset());
    this->collect_length.push_back(h_entry.get_length());
    /**
     * 想了想，因为 timestamp 不应该放在 PatternIndexCollect 里面，
     * 因为它不需要进行 pattern ，也不具有一定的模式，
     * 所有这里的代码删除掉了，包括 PatternIndexCollect 里面的 vector.
     */
    /**
     * 又想了想，还是放在这里吧。
     */
    this->collect_begin.push_back(h_entry.get_begin_timestamp());
    this->collect_end.push_back(h_entry.get_end_timestamp());
}

void 
PatternIndex::collectIndex(HostEntry h_entry)
{
    std::map<pid_t, PatternIndexCollect>::iterator iter;
    iter = this->index_collect.find(h_entry.get_id());
    if( this->index_collect.empty() || iter == this->index_collect.end())
    {
        PatternIndexCollect ins_col;
        ins_col.push_entry(h_entry);
        this->index_collect.insert(pair<pid_t,PatternIndexCollect>(h_entry.get_id(), ins_col));
    }else{
        PatternIndexCollect add_col = iter->second;
        add_col.push_entry(h_entry);
        this->index_collect[h_entry.get_id()] = add_col;
    }
}

//the main function of pattern detection;
void
PatternIndex::pattern_detection()
{
    assert(!this->index_collect.empty());
    std::map<pid_t, PatternIndexCollect>::iterator iter;
    for(iter = this->index_collect.begin(); iter != this->index_collect.end(); ++iter)
    {
        ////////////////////for debug////////////////
        // cout << "PID: " << iter->first << endl; ;    
        // iter->second.show();                     
        // cout << endl;                                
        ////////////////////////////////////////////
        PatternEntry pat_entry = pattern_by_pid(iter->first);
        /////////////////for debug//////////////////
        // pat_entry.show();
        ////////////////////////////////////////////
        pat_entry.collect_begin.swap(iter->second.collect_begin);
        pat_entry.collect_end.swap(iter->second.collect_end);
        this->p_writebuf.push_back(pat_entry);
    }
}

PatternEntry 
PatternIndex::pattern_by_pid(pid_t id)
{
    std::vector<PatternElem> pat_stack;
    int begin = 0, end = 0;
    end = (this->getCollectOffElem(PATTERN_LOGICAL_FLAG, id)).size();

    if(end == 1)
    {
        PatternEntry pat_entry_one;
        PatternUnit pat_unit_one;

        PatternElem pat_one;        

        //pat_off:
        pat_one.init = (long int)(index_collect.find(id)->second.collect_logical[0]);
        pat_one.cnt = 1;
        pat_one.seq.push_back(0);
        pat_unit_one.pat_off = pat_one;

        //pat_phy:
        pat_one.init = (long int)(index_collect.find(id)->second.collect_physical[0]);
        // pat_one.cnt = 1;
        // pat_one.seq.push_back(0);
        pat_unit_one.pat_phy.push_back(pat_one);

        //pat_len:
        pat_one.init = (long int)(index_collect.find(id)->second.collect_length[0]);
        // pat_one.cnt = 1;
        // pat_one.seq.push_back(0);
        pat_unit_one.pat_len.push_back(pat_one);

        //
        pat_entry_one.id = id;
        pat_entry_one.entry.push_back(pat_unit_one);
        
        return pat_entry_one;
    }

    pat_stack = build_off_pat(PATTERN_LOGICAL_FLAG, begin, end, id);
    /////////////////for debug//////////////////
    // for(vector<PatternElem>::iterator it = pat_stack.begin(); it != pat_stack.end(); ++it)
    // {
    //  (*it).show();
    // }
    ////////////////////////////////////////////

    //if(pat_stack.empty())
    //  return NULL;

    PatternEntry pat_entry;
    pat_entry = build_entry(pat_stack, id);
    //if(pat_entry.entry.empty())
    //  return ;

    pat_entry.id = id;
    return pat_entry;
}

//build pattern entry ;
PatternEntry 
PatternIndex::build_entry(std::vector<PatternElem> pat_stack, pid_t id)
{
    PatternUnit pat_unit;
    PatternEntry pat_entry;
    std::vector<PatternElem>::iterator iter;
    int begin = 0, end = 0;
    for(iter = pat_stack.begin(); iter != pat_stack.end(); ++iter)
    {
        end += ((iter->size()) * (iter->cnt));
        pat_unit = build_unit(*iter, begin, end + 1, id);
        pat_entry.entry.push_back(pat_unit);
        begin += (iter->size() * iter->cnt);
    }
    return pat_entry;
}

//build pattern unit;
PatternUnit 
PatternIndex::build_unit(PatternElem pat, int begin, int end, pid_t id)
{
    std::vector<PatternElem> phy;
    std::vector<PatternElem> len;
    PatternUnit ret;
    phy = build_off_pat(PATTERN_PHYSICAL_FLAG, begin, end, id);
    len = build_off_pat(PATTERN_LENGTH_FLAG, begin, end, id);
    assert(!phy.empty() && !len.empty());

    ret.pat_off = pat;
    ret.pat_phy = phy;
    ret.pat_len = len;

    return ret;
}

//build the offset pattern;
std::vector<PatternElem> 
PatternIndex::build_off_pat(int type, int begin, int end, pid_t id)
{
    std::vector<off_t> delta;
    delta = build_delta(type, begin, end, id);
    /////////////////for debug//////////////////
    // for(off_t dt : delta)
    // {
    //  cout << dt << "\t" ;
    // }
    // cout << endl;
    ////////////////////////////////////////////

    std::vector<PatternElem> pat_stack;                     //pattern stack;
    std::vector<off_t> lw;                                  //look ahead window;
    std::vector<off_t>::iterator iter = delta.begin();      //iter represents the demarcation point 
    ++iter;                                                 //of serach window and look ahead window;
                                                            //begin to iter-1 is sw, iter to end is lw;
    PatternElem tmp0;
    tmp0.seq.push_back(delta[0]);
    tmp0.cnt = 1;
    pat_stack.push_back(tmp0);
    while(iter != delta.end())
    {
        lw.clear();
        int k = SearchWindow;                               //the size of the sliding window;
        if(iter - k < delta.begin() || iter + k >= delta.end())
        {
            if((iter - delta.begin()) > (delta.end() - iter))
                k = delta.end() - iter;
            else
                k = iter - delta.begin();
            // k = ((iter - delta.begin()) > (delta.end() - iter))?(delta.end() - iter) : (iter - delta.begin());
        }
        // PatternElem pat;
        while(k > 0)
        {
            if(!equal_lw_sw(delta, iter, k) )
            {
                --k;
                continue;
            }
            lw = init_lw(delta, iter, k);
            if(pat_merge(pat_stack, lw, k) )                //weather can merge and merge in this func;
            {
                iter += k;                                  //sliding window;
                break;
            }
            --k;
        }
        //if k==-1 means can't merge that only push one elem to ps;
        if(k == 0)
        {
            //if ps is empty need creat a new PatternElem and push back;
            //if cnt is not 1 means can't directly into ps, need creat a new and push back;
            if(pat_stack.empty() || ((pat_stack[pat_stack.size() - 1]).cnt != 1))
            {
                PatternElem tmp;
                tmp.seq.push_back(*iter);
                tmp.cnt = 1;
                pat_stack.push_back(tmp);
            }
            else
            {
                pat_stack[pat_stack.size() - 1].seq.push_back(*iter);
            }
            ++iter; 
                                                    //sliding window;
        }
        //if seq is repeating then compressing it;
        if(pat_stack[pat_stack.size() - 1].is_repeating())
        {
            pat_stack[pat_stack.size() - 1].cnt = pat_stack[pat_stack.size() - 1].size();
            pat_stack[pat_stack.size() - 1].pop(pat_stack[pat_stack.size() - 1].size() - 1);
            ++iter;
        }
    }
    pat_stack = build_ps_init(pat_stack, type, begin, end, id);
    /////////////////for debug//////////////////
    // cout << "pat_stack show : " << endl;
    // for(PatternElem pe : pat_stack)
    // {
    //  pe.show();
    // }
    ////////////////////////////////////////////
    return pat_stack;
}

//this function is to build the delta;
std::vector<off_t>
PatternIndex::build_delta(int type, int begin, int end, pid_t id)
{
    assert( (type == PATTERN_LOGICAL_FLAG) || (type == PATTERN_PHYSICAL_FLAG) || (type == PATTERN_LENGTH_FLAG) );
    std::vector<off_t> delta;
    if(type == PATTERN_LOGICAL_FLAG || (type == PATTERN_PHYSICAL_FLAG))
    {
        vector<off_t> vt = this->getCollectOffElem(type, id);
        vector<off_t>::iterator it_1, it_2;
        it_1 = vt.begin() + begin;
        it_2 = vt.begin() + end;
        off_t tmp;
        while(it_1 != it_2 - 1)
        {
            tmp = (off_t)(*(it_1 + 1) - *it_1);
            delta.push_back(tmp);
            ++it_1;
        }

        // std::vector<off_t>::iterator it_1, it_2;
        // it_1 = this->getCollectOffElem(type, id).begin() + begin;
        // it_2 = this->getCollectOffElem(type, id).begin() + end;
        // /////////////////for debug//////////////////
        // // printf("%d\t%d\n",it_1,it_2 );
        // ////////////////////////////////////////////
        // off_t tmp;
        // while(it_1 != it_2 - 1)
        // {
        //  tmp = (off_t)(*(it_1 + 1) - *it_1);
        //  delta.push_back(tmp);
        //  ++it_1;
        // }
    }else if(type == PATTERN_LENGTH_FLAG)
    {
        vector<size_t> vt = this->getCollectLenElem(type, id);
        vector<size_t>::iterator it_1, it_2;
        it_1 = vt.begin() + begin;
        it_2 = vt.begin() + end;
        size_t tmp;
        while(it_1 != it_2 - 1)
        {
            tmp = (size_t)(*(it_1 + 1) - *it_1);
            delta.push_back(tmp);
            ++it_1;
        }

        // std::vector<size_t>::iterator it_1, it_2;
        // it_1 = this->getCollectLenElem(type, id).begin() + begin;
        // it_2 = this->getCollectLenElem(type, id).begin() + end;
        // off_t tmp;
        // while(it_1 != it_2 - 1)
        // {
        //  tmp = (off_t)(*(it_1 + 1) - *it_1);
        //  delta.push_back(tmp);
        //  ++it_1;
        // }
    }
    return delta;
}

//if lw[1:k] is equal to sw[1:k] return TURE;
//sw is [iter-k : iter), lw is [iter : iter + k)
bool  
PatternIndex::equal_lw_sw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k)
{
    // if((iter - k < delta.begin()) || (iter + k > delta.end()))
    //  return 0;
    std::vector<off_t>::iterator it_1, it_2;
    it_1 = iter - 1;
    it_2 = iter + k - 1;
    while(k--)
    {
        if(*it_1-- != *it_2--)
            return 0;
    }
    return 1;
}

//init look ahead window; NOT NEED to init search window;
std::vector<off_t> 
PatternIndex::init_lw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k)
{
    std::vector<off_t> lw;
    if(iter == delta.begin())
        return lw;

    while(iter != delta.end() && k-- )
    {
        lw.push_back(*iter++);
    }
    return lw;
}

//whether can lw merged with the last elem in pattern stack;
//NOTE: ONLY cmp with the LAST pat in pattern stack;
//if can merge then merge and return 1;
//if can't merge or ps is empty return 0;
bool
PatternIndex::pat_merge(std::vector<PatternElem> &pat_stack, std::vector<off_t> lw, int k)
{
    //case 0: if ps is empty means can't merge;
    if(pat_stack.empty() || lw.size() != k)
    {
        // TRACE_FUNC();
        return 0;
    }

    int p_size = pat_stack[pat_stack.size() - 1].size();
    int cnt = pat_stack[pat_stack.size() - 1].cnt;

    //case 1: p: 1,2,3,4^cnt  lw: 1,2,3,4
    if( p_size == lw.size() )
    {
        // cout << "case: 1" << endl;
        pat_stack[pat_stack.size() - 1].cnt += 1;
        return 1;
    }

    //case 2: p: 1,2^2  lw: 1,2,1,2
    //if p: 1,2^3  lw: 1,2,1,2 not care it, when lw smaller ,it will be merged;
    if( p_size * cnt == lw.size() )
    {
        // cout << "case: 2" << endl;
        pat_stack[pat_stack.size() - 1].cnt *= 2;
        return 1;
    }

    //case 3: p: 1,2,3,4,5^1  lw:2,3,4,5
    if(cnt == 1 && p_size > lw.size())
    {
        // cout << "case: 3" << endl;
        pat_stack[pat_stack.size() - 1].pop(k);
        PatternElem tmp;
        tmp.seq.assign(lw.begin(), lw.end());
        tmp.cnt = 2;
        pat_stack.push_back(tmp);
        return 1;
    }

    //other cases: means can't merge;
    // cout << "none case" <<endl;
    return 0;
}

//insert init to pattern stack;
vector<PatternElem> 
PatternIndex::build_ps_init(vector<PatternElem> pat_stack, int type, int begin, int end, pid_t id)
{
    assert( (type == PATTERN_LOGICAL_FLAG) || (type == PATTERN_PHYSICAL_FLAG) || (type == PATTERN_LENGTH_FLAG) );
    vector<PatternElem>::iterator it_1;
    it_1 = pat_stack.begin();
    stringstream oss;
    if(type == PATTERN_LOGICAL_FLAG)
    {
        vector<off_t> vt = this->getCollectOffElem(type, id);
        std::vector<off_t>::iterator it_2, it_3;
        it_2 = vt.begin() + begin;
        it_3 = vt.begin() + end;
        while(it_1 != pat_stack.end() && it_2 != it_3)
        {
            /////////////////for debug//////////////////
            // cout << __FUNCTION__ << ": " << *it_2 << endl;
            ////////////////////////////////////////////
            // oss << *it_2;
            // oss >> it_1->init;
            it_1->init = (long int)*it_2;
            /////////////////for debug//////////////////
            // cout << "it_1->init: " << it_1->init << endl;;
            ////////////////////////////////////////////
            // oss.clear();
            // oss.str("");
            it_2 += (it_1->size() * (it_1->cnt));
            ++it_1;
        }
    }else if(type == PATTERN_PHYSICAL_FLAG)
    {
        vector<off_t> vt = this->getCollectOffElem(type, id);
        std::vector<off_t>::iterator it_2, it_3;
        it_2 = vt.begin() + begin;
        it_3 = vt.begin() + end;
        while(it_1 != pat_stack.end() && it_2 != it_3)
        {
            // oss << *it_2;
            // oss >> it_1->init;
            it_1->init = (long int)*it_2;
            /////////////////for debug//////////////////
            // cout << "it_1->init: " << it_1->init << endl;
            ////////////////////////////////////////////
            // oss.clear();
            // oss.str("");
            it_2 += (it_1->size() * (it_1->cnt));
            ++it_1;
        }
    }else if(type == PATTERN_LENGTH_FLAG)
    {
        vector<size_t> vt = this->getCollectLenElem(type, id);
        std::vector<size_t>::iterator it_2, it_3;
        it_2 = vt.begin() + begin;
        it_3 = vt.begin() + end;
        while(it_1 != pat_stack.end() && it_2 != it_3)
        {
            // oss << *it_2;
            // oss >> it_1->init;
            it_1->init = (long int)*it_2;
            // oss.clear();
            // oss.str("");
            it_2 += (it_1->size() * (it_1->cnt));
            ++it_1;
        }
    }
    /////////////////for debug//////////////////
    // cout << "pat_stack show : " << endl;
    // for(PatternElem pe : pat_stack)
    // {
    //  pe.show();
    // }
    ////////////////////////////////////////////
    return pat_stack;
}

//delete the last n elems from seq;
void 
PatternElem::pop(int n)
{
    assert(this->seq.size() >= n);
    assert(this->cnt == 1);
    while(n--)
    {
        this->seq.pop_back();
    }
}

//return the size of vctor<off_t> seq in PatternElem;
int 
PatternElem::size()
{
    return this->seq.size();
}



//if all elem in seq is same that means is repeating;
bool 
PatternElem::is_repeating()
{
    std::vector<off_t>::iterator it_1;
    it_1 = this->seq.begin();
    if(it_1 != this->seq.end())             //if equal means seq is empty;
        return 0;
    while(it_1 != this->seq.end() - 1)
    {
        if(*it_1 != *it_1 + 1)
            return 0;
    }
    return 1;
}

std::vector<off_t> 
PatternIndex::getCollectOffElem(int type, pid_t id)
{
    if(type == PATTERN_LOGICAL_FLAG)
        return this->index_collect.find(id)->second.collect_logical;
    return this->index_collect.find(id)->second.collect_physical;
}

std::vector<size_t> 
PatternIndex::getCollectLenElem(int type, pid_t id)
{
    return this->index_collect.find(id)->second.collect_length;
}


///////////////////////////////////////
// append to string buf;
// for write;
///////////////////////////////////////

/**
 * PatternElem: long int init, int cnt, vector<off_t> seq;
 * buf: init, cnt, seq_size, seq;
 */
size_t
PatternElem::append_to_buf(string *buf)
{
    int seq_size = 0;
    size_t totlesize = 0;
    string tmp_buf;
    seq_size = seq.size() * sizeof(off_t);

    tmp_buf.append((char *)&init, sizeof(init));
    tmp_buf.append((char *)&cnt, sizeof(cnt));
    tmp_buf.append((char *)&seq_size, sizeof(seq_size));
    tmp_buf.append((char *)&seq[0], seq_size);
    
    totlesize = sizeof(init) + sizeof(cnt) + sizeof(seq_size) + seq_size;
    // assert(tmp_buf.size() == totlesize);

    buf->append((char *)&tmp_buf[0], totlesize);
    return totlesize;
}

/**
 * PatternUnit: PatternElem pat_off, vector<PatternElem> pat_phy, vector<PatternElem> pat_len;
 * buf: sizeof(O,pat_off,pat_phy,pat_len), O, pat_off, P, off_phy, L, off_len;
 * O: logical_offset, P: physical_offset, L: length;
 * save the sizeof(Unit), because when find the pat_off and the init is small
 * than the need, we can jump to the next Unit;
 */
size_t
PatternUnit::append_to_buf(string *buf)
{
    char logical_type = 'O', physical_type = 'P', length_type = 'L';
    size_t totlesize = 0;
    string tmp_buf;

    //append the logical offset;
    tmp_buf.append((char *)&logical_type, sizeof(logical_type));
    totlesize += sizeof(logical_type);
    totlesize += pat_off.append_to_buf(&tmp_buf);

    //append the physical offset;
    tmp_buf.append((char *)&physical_type, sizeof(physical_type));
    totlesize += sizeof(physical_type);
    for(PatternElem pe : pat_phy)
    {
        totlesize += pe.append_to_buf(&tmp_buf);
    }

    //append the length offset;
    tmp_buf.append((char *)&length_type, sizeof(length_type));
    totlesize += sizeof(length_type);
    for(PatternElem pe : pat_len)
    {
        totlesize += pe.append_to_buf(&tmp_buf);
    }

    // assert(totlesize == tmp_buf.size());
    buf->append((char *)&totlesize, sizeof(totlesize));
    buf->append((char *)&tmp_buf[0], totlesize);
    totlesize += sizeof(totlesize);

    return totlesize;
}

/**
 * PatternEntry: pid_t id, vector<PatternUnit> entry;
 * buf: sizeof(id, entry), id, entry;
 */
size_t
PatternEntry::append_to_buf(string *buf)
{
    size_t totlesize = 0;
    string tmp_buf;

    //append the pid;
    tmp_buf.append((char *)&id, sizeof(id));
    totlesize += sizeof(id);

    //append the vector<PatternUnit> entry;
    for(PatternUnit pu : entry)
    {
        totlesize += pu.append_to_buf(&tmp_buf);
    }

    // assert(totlesize == tmp_buf.size());
    buf->append((char *)&totlesize, sizeof(totlesize));
    buf->append((char *)&tmp_buf[0], totlesize);
    totlesize += sizeof(totlesize);

    return totlesize;
}

/**
 * 最终写入到磁盘中的 dropping index 的结构
 * totlesizeofGlobal, {
 *   totlesizeofEntry, EntryPid, [
 *     totlesizeofUnit, O, (off_pat), 
 *     P, (off_phy, off_phy, ..., off_phy), 
 *     L, (off_len, off_len, ..., off_len),
 *   ];
 *   totlesizeofEntry, EntryPid, [...];
 *   ......
 *   totlesizeofEntry, EntryPid, [...];
 * }
 */
size_t 
PatternIndex::append_to_buf(string *buf)
{
    size_t totlesize = 0;
    string tmp_buf;

    for(PatternEntry pe : p_writebuf)
    {
        totlesize += pe.append_to_buf(&tmp_buf);
    }

    // assert(totlesize == tmp_buf.size());
    buf->append((char *)&totlesize, sizeof(totlesize));
    buf->append((char *)&tmp_buf[0], totlesize);
    totlesize += sizeof(totlesize);
    return totlesize;
}


//////////////////////////////////////////////////////
// get info from ibuf;
// for read;
//////////////////////////////////////////////////////

/**
 * ibuf 的结构：
 * totlesizeofGlobal, {
 *   totlesizeofEntry, EntryPid, [
 *     totlesizeofUnit, O, (off_pat), 
 *     P, (off_phy, off_phy, ..., off_phy), 
 *     L, (off_len, off_len, ..., off_len),
 *   ];
 *   totlesizeofEntry, EntryPid, [...];
 *   ......
 *   totlesizeofEntry, EntryPid, [...];
 * }
 */
void
PatternIndex::get_ibuf_info(void *ibuf, size_t len)
{
    string buf;
    buf.resize(len);
    memcpy(&buf[0], ibuf, len);

    /////////////////for debug//////////////////
    // cout << buf << endl;
    ////////////////////////////////////////////

    size_t position = 0;
    size_t totlesize = 0;

    memcpy(&totlesize, &buf[position], sizeof(totlesize));
    position += sizeof(totlesize);

    // assert(len == (totlesize + sizeof(totlesize)));
    while(position < len)
    {
        size_t entry_size = 0;
        string entry_ibuf;
        PatternEntry p_entry;

        memcpy(&entry_size, &buf[position], sizeof(entry_size));
        position += sizeof(entry_size);

        entry_ibuf.resize(entry_size);
        memcpy(&entry_ibuf[0], &buf[position], entry_size);
        // assert(entry_size == entry_ibuf.size());
        /////////////////for debug//////////////////
        // cout << entry_ibuf << endl;
        ////////////////////////////////////////////
        p_entry.get_ibuf_entry(entry_ibuf);
        position += entry_size;

        p_readbuf.push_back(p_entry);
    }
    /////////////////for debug//////////////////
    // show_writebuf();
    // cout << "get_ibuf_info END!" << endl;
    ////////////////////////////////////////////
}

void 
PatternEntry::get_ibuf_entry(string ibuf)
{
    size_t position = 0, ibuf_len = ibuf.size();

    memcpy(&id, &ibuf[position], sizeof(id));
    position += sizeof(id);
    /////////////////for debug//////////////////
    // cout << id << endl;
    ////////////////////////////////////////////
    while(position < ibuf_len)
    {
        size_t unit_size = 0;
        string unit_ibuf;
        PatternUnit p_unit;

        memcpy(&unit_size, &ibuf[position], sizeof(unit_size));
        position += sizeof(unit_size);

        unit_ibuf.resize(unit_size);
        memcpy(&unit_ibuf[0], &ibuf[position], unit_size);
        // assert(unit_size == unit_ibuf.size());
        /////////////////for debug//////////////////
        // cout << unit_ibuf << endl;
        ////////////////////////////////////////////
        p_unit.get_ibuf_unit(unit_ibuf);
        position += unit_size;

        entry.push_back(p_unit);
    }
}

void 
PatternUnit::get_ibuf_unit(string ibuf)
{
    size_t position = 0, ibuf_len = ibuf.size();
    char type;
    /////////////////for debug//////////////////
    // cout << __FUNCTION__ << "ibuf_len: " << ibuf_len << endl;
    ////////////////////////////////////////////
    //init the segment that needed;
    int seq_size = 0;
    PatternElem p_elem;

    //get the PatternElem pat_off;
    memcpy(&type, &ibuf[position], sizeof(type));
    /////////////////for debug//////////////////
    // cout << "****type: " << type << endl;
    ////////////////////////////////////////////
    assert(type == 'O');
    position += sizeof(type);

    memcpy(&(p_elem.init), &ibuf[position], sizeof(long int));
    /////////////////for debug//////////////////
    // cout << "p_elem.init: " << p_elem.init << endl;
    ////////////////////////////////////////////
    position += sizeof(long int);
    memcpy(&(p_elem.cnt), &ibuf[position], sizeof(int));
    position += sizeof(int);
    memcpy(&seq_size, &ibuf[position], sizeof(seq_size));
    position += sizeof(seq_size);
    p_elem.seq.resize(seq_size/sizeof(off_t));
    memcpy(&(p_elem.seq[0]), &ibuf[position], seq_size);
    position += seq_size;

    pat_off = p_elem;

    //get the vector<PatternElem> pat_phy;
    memcpy(&type, &ibuf[position], sizeof(type));
    /////////////////for debug//////////////////
    // show();
    // cout << "****type: " << type << endl;
    ////////////////////////////////////////////
    assert(type == 'P');
    position += sizeof(type);

    while(position < ibuf_len)
    {
        PatternElem pt_elem;
        /////////////////for debug//////////////////
        // cout << endl;
        // cout << "****type: " << type << endl;
        // cout << "position: " << position << "\t"
        //   << "ibuf_len: " << ibuf_len << endl;
        // show();
        // cout << endl;
        ////////////////////////////////////////////

        pt_elem.init = 0;
        memcpy(&(pt_elem.init), &ibuf[position], sizeof(long int));
        position += sizeof(long int);

        memcpy(&(pt_elem.cnt), &ibuf[position], sizeof(int));
        position += sizeof(int);

        memcpy(&seq_size, &ibuf[position], sizeof(seq_size));
        position += sizeof(seq_size);

        pt_elem.seq.clear();
        pt_elem.seq.resize(seq_size/sizeof(off_t));
        memcpy(&(pt_elem.seq[0]), &ibuf[position], seq_size);
        position += seq_size;

        pat_phy.push_back(pt_elem);

        memcpy(&type, &ibuf[position], sizeof(type));
        
        /////////////////for debug//////////////////
        // cout << endl;
        // cout << "****type: " << type << endl;
        // cout << "position: " << position << "\t"
        //   << "ibuf_len: " << ibuf_len << endl;
        // show();
        // cout << endl;
        ////////////////////////////////////////////
        if(type == 'L')
            break;
    }

    //get the vector<PatternElem> pat_len;
    /////////////////for debug//////////////////
    // cout << "****type: " << type << endl;
    ////////////////////////////////////////////
    assert(type == 'L');
    position += sizeof(type);
    while(position < ibuf_len)
    {
        PatternElem pt_elem;

        pt_elem.init = 0;
        memcpy(&(pt_elem.init), &ibuf[position], sizeof(long int));
        position += sizeof(long int);
        memcpy(&(pt_elem.cnt), &ibuf[position], sizeof(int));
        position += sizeof(int);
        memcpy(&seq_size, &ibuf[position], sizeof(seq_size));
        position += sizeof(seq_size);
        pt_elem.seq.resize(seq_size/sizeof(off_t));
        memcpy(&(pt_elem.seq[0]), &ibuf[position], seq_size);
        position += seq_size;

        pat_len.push_back(pt_elem);
    }
}




///////////////////////
// for query;
///////////////////////

off_t 
PatternElem::sumVecSeq()
{
    off_t ret = 0;
    for(int i = 0; i < seq.size(); ++i)
        ret += seq[i];
    return ret;
}


off_t 
PatternElem::getValueByPos(int pos)
{
    off_t ret_off = (off_t)init;
    if(pos == 0)
    {
        return ret_off;
    }

    if(pos > seq.size() * cnt)
        pos = seq.size() * cnt;

    int col = pos % seq.size();
    int row = pos / seq.size();
    off_t sumSeq = sumVecSeq(); 

    ret_off += sumSeq * row;
    for(int i = 0; i < col; ++i)
    {
        ret_off += seq[i];
    }

    return ret_off;

}

off_t 
PatternElem::getLastValue()
{
    int pos = seq.size() * cnt;
    return getValueByPos(pos);
}


bool 
PatternUnit::look_up(off_t query_off, off_t *ret_log, off_t *ret_phy, size_t *ret_len)
{
    off_t cur_off = (off_t)pat_off.init;
    off_t cur_phy = 0;
    off_t cur_len = 0;


    if(query_off < cur_off)
        return 0;

    if(pat_off.seq.size() * pat_off.cnt <= 1 || query_off == cur_off)
    {
        cur_phy = (off_t)pat_phy[0].init;
        cur_len = (off_t)pat_len[0].init;
        
        if(query_off - cur_off < cur_len)
        {
            *ret_log = cur_off;
            *ret_phy = cur_phy;
            *ret_len = (size_t)cur_len;
            return 1;
        }else 
            return 0;
    }

    off_t sumOffSeq = pat_off.sumVecSeq();
    if(sumOffSeq <= 0) 
        return 0;

    cur_off = query_off - cur_off;

    // if(cur_off >= sumOffSeq * pat_off.cnt)
    //  return 0;

    int col = cur_off % sumOffSeq;
    int row = cur_off / sumOffSeq;

    if(row >= pat_off.cnt)
    {
        cur_off = pat_off.getLastValue();
        cur_phy = pat_phy.rbegin()->getLastValue();
        cur_len = (size_t)pat_len.rbegin()->getLastValue();

        if(query_off - cur_off < cur_len)
        {
            *ret_log = cur_off;
            *ret_phy = cur_phy;
            *ret_len = (size_t)cur_len;
            return 1;
        }else 
            return 0;
    }else
    {
        off_t sum = 0;
        int col_pos = 0;
        for(col_pos = 0; sum <= col; ++col_pos)
        {
            sum += pat_off.seq[col_pos];
        }

        --col_pos;
        // if(sum == col || col_pos < 0)
        //  ++col_pos;

        /////////////////for debug//////////////////
        // cout << "sum: " << sum << endl;
        // cout << "col: " << col << endl;
        // cout << "col_pos: " << col_pos << endl;
        ////////////////////////////////////////////

        int pos = col_pos + row * pat_off.seq.size();

        // get cur_off value;
        cur_off = pat_off.getValueByPos(pos);

        // get cur_phy value;
        for(int i = 0; i < pat_phy.size(); ++i)
        {
            if(pos <= pat_phy[i].seq.size() * pat_phy[i].cnt)
            {
                cur_phy = pat_phy[i].getValueByPos(pos);
                break;
            }
            else pos -= pat_phy[i].seq.size() * pat_phy[i].cnt;
        }

        // get cur_len value;
        for(int i = 0; i < pat_len.size(); ++i)
        {
            if(pos <= pat_len[i].seq.size() * pat_len[i].cnt)
            {
                cur_len = pat_len[i].getValueByPos(pos);
                break;
            }
            else pos -= pat_len[i].seq.size() * pat_len[i].cnt;
        }

        if(query_off - cur_off < cur_len)
        {
            /////////////////for debug//////////////////
            // cout << "cur_off: " << cur_off << endl;
            ////////////////////////////////////////////
            *ret_log = cur_off;
            *ret_phy = cur_phy;
            *ret_len = (size_t)cur_len;
            return 1;
        }else 
            return 0;

    }

    return 0;
}


//////////////////
//some tools
//////////////////

off_t 
string_to_off(string str)
{
    off_t ret = 0;
    for(int i = 0; i < str.size(); ++i)
    {
        ret *= 10;
        ret += str[i] - '0';
    }
    return ret;
}

bool 
pat_isContain( off_t off, off_t offset, off_t length )
{
    return ( offset <= off && off < offset+length );
}


//////////////////////////////////////////////////////
//for debug
//////////////////////////////////////////////////////
void 
PatternElem::show()
{
    cout << "[" << init << "(" ;
    std::vector<off_t>::iterator v;
    for(v = seq.begin(); v != (this->seq.end() - 1); ++v)
    {
        cout << *v << "," ;
    }
    cout << *v << ")^"  << cnt << "]" << endl;
}

void
PatternUnit::show()
{
    cout << "logical pattern: "  << endl;
    this->pat_off.show();

    cout << "physical pattern: "  << endl;
    std::vector<PatternElem>::iterator v;
    for(v = this->pat_phy.begin(); v != this->pat_phy.end(); ++v)
    {
        v->show();
    }

    cout << "length pattern: "  << endl;
    for(v = this->pat_len.begin(); v != this->pat_len.end(); ++v)
    {
        v->show();
    }
}

void
PatternEntry::show()
{
    cout << "pid: " << this->id << endl;
    std::vector<PatternUnit>::iterator v;
    for(v = this->entry.begin(); v != this->entry.end(); ++v)
    {
        cout << "******* Pattern " << v - this->entry.begin() + 1 << " *******" << endl;
        v->show();
    }
}

void
PatternIndexCollect::show()
{
    show_vector(this->collect_logical);
    show_vector(this->collect_physical);
    show_vector(this->collect_length);
    show_vector(this->collect_begin);
    show_vector(this->collect_end);
}

void 
PatternIndex::show_collect()
{
    std::map<pid_t, PatternIndexCollect>::iterator it;
    for(it = index_collect.begin(); it != index_collect.end(); ++it)
    {
        cout << "PID: " << it->first << endl;
        it->second.show();
    }
}

void 
PatternIndex::show_writebuf()
{
    for(PatternEntry pe : p_writebuf)
    {
        pe.show();
    }
}

void 
PatternIndex::show_readbuf()
{
    for(PatternEntry pe : p_readbuf)
    {
        pe.show();
    }
}


///////////////////////////////////////////////////////////////////////
// END PatternIndex.cpp
///////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////
// PatternDeSe.cpp
///////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////
// PatternForQuery
/////////////////////////////////////////////

PatternForQuery::PatternForQuery(bool a, pid_t b, off_t c, off_t d, size_t e)
{
    exist = a;
    id = b;
    logical_offset = c;
    physical_offset = d;
    length = e;
}


/////////////////////////////////////////////
// PatternListDeSe
/////////////////////////////////////////////

bool
PatternListDeSe::dese_map_look_up(off_t logical, PatternForQuery *pfq)
{
    pfq->exist = 0;

    map<off_t, PatternUnitDeSe>::iterator iter;
    iter = dese_map.lower_bound(logical);

    if(dese_map.size() == 1 && logical >= dese_map.begin()->second.logical_offset)
        return dese_map.begin()->second.look_up(logical, pfq);

    if(iter == dese_map.begin() && logical != iter->second.logical_offset)
    {   
        // cout << "iter == dese_map.begin() && logical != iter->second.logical_offset"
        //   << endl;
        return 0;
    }

    if(iter == dese_map.end())
    {
        /////////////////for debug//////////////////
        // cout << "dese_map.end!" << endl;
        ////////////////////////////////////////////
        --iter;
        return iter->second.look_up(logical, pfq);
    }

    // PatternUnitDeSe pds_unit;
    /* if == means we can find logical by *iter, otherwhile we should find it to *pre */
    if(logical == iter->second.logical_offset)
    {
        return iter->second.look_up(logical, pfq);
    }else{
        /////////////////for debug//////////////////
        // cout << "--iter" << endl;
        ////////////////////////////////////////////
        --iter;
        return iter->second.look_up(logical, pfq);
    }
    return 0;

}

bool
PatternUnitDeSe::look_up(off_t logical, PatternForQuery *pfq)
{
    // when analyze_unit, dese_unit is destroyed, so don't use it anymore;
    if(!dese_unit.empty())
    {
        /////////////////for debug//////////////////
        // cout << dese_unit << endl;
        ////////////////////////////////////////////
        query_unit.get_ibuf_unit(dese_unit);
        if(query_unit.pat_off.cnt == 1 && query_unit.pat_off.seq.size() == 2
             && query_unit.pat_off.seq[0] == 0)
        {
            query_unit.pat_off.seq.resize(1);
            query_unit.pat_off.seq[0] = 0;

            query_unit.pat_phy[0].seq.resize(1);
            query_unit.pat_phy[0].cnt = 1;
            query_unit.pat_phy[0].seq[0] = 0;

            query_unit.pat_len[0].seq.resize(1);
            query_unit.pat_len[0].cnt = 1;
            query_unit.pat_len[0].seq[0] = 0;
        }
        /////////////////for debug//////////////////
        // query_unit.show();
        ////////////////////////////////////////////
        dese_unit.clear();
    }

    if(query_unit.look_up(logical, &pfq->logical_offset, &pfq->physical_offset, &pfq->length))
        pfq->exist = 1;
    else 
        pfq->exist = 0;

    pfq->id = known_chunk;
    pfq->original_chunk = id;

    return 0;

}

void
PatternUnitDeSe::analyze_unit()
{
    // assert(!dese_unit.empty());

    PatternUnit p_unit;
    p_unit.get_ibuf_unit(dese_unit);

    // analyze the logical offset pat_off;
    off_t cur_off = logical_offset;
    PatternForQuery pfq;
    pfq.logical_offset = cur_off;
    queryList.push_back(pfq);
    int n = p_unit.pat_off.cnt;
    while(n--)
    {
        for(off_t of : p_unit.pat_off.seq)
        {
            cur_off += of;
            pfq.logical_offset = cur_off;
            queryList.push_back(pfq);
        }
    }

    //analyze the physical offset pat_phy;
    assert(!p_unit.pat_phy.empty());
    off_t cur_phy = (off_t)(p_unit.pat_phy[0].init);
    int position = 0;
    queryList[position++].physical_offset = cur_phy;
    for(PatternElem pe : p_unit.pat_phy)
    {
        n = pe.cnt;
        while(n--)
        {
            for(off_t of : pe.seq)
            {
                cur_phy += of;
                queryList[position++].physical_offset = cur_phy;
            }
        }
    }
    /////////////////for debug//////////////////
    // cout << "*****************" << endl;
    ////////////////////////////////////////////

    //analyze the length pat_len;
    assert(!p_unit.pat_len.empty());
    size_t cur_len = (size_t)(p_unit.pat_len[0].init);
    position = 0;
    queryList[position++].length = cur_len;
    for(PatternElem pe : p_unit.pat_len)
    {
        n = pe.cnt;
        while(n--)
        {
            for(off_t of : pe.seq)
            {
                of = (size_t)of;
                cur_len += of;
                queryList[position++].length = cur_len;
            }
        }
    }
    /////////////////for debug//////////////////
    // cout << "*****************" << endl;
    ////////////////////////////////////////////

    // init the queryList.id and queryList.exist;
    for(int i = 0; i < queryList.size(); i++)
    {
        queryList[i].id = known_chunk;
        queryList[i].original_chunk = id;
        queryList[i].exist = 1;
    }
    /////////////////for debug//////////////////
    // cout << "dese_unit analyzed!" << endl;
    ////////////////////////////////////////////

    //when analyzed the unit, delete the dese_unit;
    arrange_queryList();
    ql_pos = 0;
    dese_unit.~string();
}

void 
PatternUnitDeSe::arrange_queryList()
{
    vector<PatternForQuery>::iterator it, itNext;
    for(it = queryList.begin(); it != queryList.end() - 1; )
    {
        itNext = it + 1;
        if(    it->logical_offset == itNext->logical_offset
            && it->physical_offset == itNext->physical_offset
            && it->length == itNext->length)
        {
            queryList.erase(it);
        }
        else
            ++it;
    }
}


void
PatternListDeSe::Pattern_DeSe(void *addr, size_t len)
{
    string buf;
    buf.resize(len);
    memcpy(&buf[0], addr, len);
    /////////////////for debug//////////////////
    // cout << "************Pattern_DeSe" << "\t"
    //   << "size: " << buf.size() << endl;
    // cout << buf << endl;
    ////////////////////////////////////////////

    size_t position = 0, totlesize = 0;

    memcpy(&totlesize, &buf[position], sizeof(totlesize));
    position += sizeof(totlesize);

    // assert(len == (totlesize + sizeof(totlesize)));
    while(position < len)
    {
        size_t entry_size = 0;
        string entry_ibuf;

        memcpy(&entry_size, &buf[position], sizeof(entry_size));
        position += sizeof(entry_size);

        entry_ibuf.resize(entry_size);
        memcpy(&entry_ibuf[0], &buf[position], entry_size);
        // assert(entry_ibuf.size() == entry_size);
        Pattern_DeSe_Entry(&entry_ibuf);

        position += entry_size;
    }

}

void
PatternListDeSe::Pattern_DeSe_Entry(string *ibuf)
{
    /////////////////for debug//////////////////
    // cout << "-> Pattern_DeSe_entry" << "\t"
    //   << "size: " << ibuf->size() << endl;
    // cout << *ibuf << endl;
    ////////////////////////////////////////////
    pid_t id = -1;
    size_t position = 0, buf_len = ibuf->size();

    memcpy(&id, &(*ibuf)[position], sizeof(id));
    position += sizeof(id);
    // assert(id != -1);
    /////////////////for debug//////////////////
    // cout << "id: " << id << endl;
    ////////////////////////////////////////////

    while(position < buf_len)
    {
        size_t unit_size = 0;
        PatternUnitDeSe pds_unit;
        pds_unit.id = id;

        memcpy(&unit_size, &(*ibuf)[position], sizeof(unit_size));
        position += sizeof(unit_size);
        /////////////////for debug//////////////////
        // cout << "unit_size: " << unit_size << endl;
        ////////////////////////////////////////////

        pds_unit.dese_unit.resize(unit_size);
        memcpy(&(pds_unit.dese_unit[0]), &(*ibuf)[position], unit_size);
        // assert(pds_unit.dese_unit.size() == unit_size);

        pds_unit.setInit_offset();
        position += unit_size;

        /////////////////for debug//////////////////
        // pds_unit.show();
        ////////////////////////////////////////////
        dese_map.insert(pair<off_t, PatternUnitDeSe>(pds_unit.logical_offset, pds_unit));
    }
    /////////////////for debug//////////////////
    // cout << dese_map.size() << endl;
    // show();
    ////////////////////////////////////////////
}

void 
PatternUnitDeSe::setInit_offset()
{
    assert(!dese_unit.empty());
    /////////////////for debug//////////////////
    // cout << dese_unit << endl;
    ////////////////////////////////////////////
    size_t position = 0;

    char type;
    memcpy(&type, &dese_unit[position], sizeof(type));
    assert(type == 'O');
    position += sizeof(type);

    // int init_size = 0;
    // string init;
 //    position = position + sizeof(int) + sizeof(char);

    long int init = 0;
    memcpy(&init, &dese_unit[position], sizeof(long int));

    logical_offset = (off_t)init;


}

void 
PatternListDeSe::truncate(off_t offset)
{
    bool first;
    map<off_t, PatternUnitDeSe>::iterator it;
    it = dese_map.lower_bound(offset);

    if(it == dese_map.begin()) first = 1;

    dese_map.erase(it, dese_map.end());

    if(!first)
    {
        --it;
        it->second.truncate(offset);
    }
}

void 
PatternUnitDeSe::truncate(off_t offset)
{
    if(queryList.empty())
        analyze_unit();
    vector<PatternForQuery>::iterator it, pos;
    for(it = queryList.begin(); it != queryList.end() && it->logical_offset < offset; ++it)
        ;
    if(it != queryList.begin() && it != queryList.end())
    {
        pos = it;
        --pos;
        queryList.erase(it, queryList.end());
        if((off_t)(pos->logical_offset + pos->length) > offset)
        {
            pos->length = offset - pos->logical_offset;
        }
    }
}

////////////////////////////
// for debug;
////////////////////////////

void 
PatternForQuery::show()
{
    if(exist == 0)
        cout << "Not exist!" << endl;
    else
    {
        cout << "id: " << id << "  "
             << "logical_offset: " << logical_offset << "  "
             << "physical_offset: " << physical_offset << "  "
             << "length: " << length << endl;
    }
}

void
PatternListDeSe::show()
{
    map<off_t, PatternUnitDeSe>::iterator it;
    for(it = dese_map.begin(); it != dese_map.end(); ++it)
    {
        cout << "logical_offset: " << it->first << "\t" << endl;
        it->second.show();
    }
}

void 
PatternUnitDeSe::show()
{
    cout << "logical_offset: " << logical_offset << "\t"
         << "id: " << id << "\t"
         << "known_chunk: " << known_chunk 
         << endl;
    if(queryList.empty())
        cout << dese_unit << endl;
    else
        for(PatternForQuery pfq : queryList)
            pfq.show();
    /////////////////for debug//////////////////
    // cout << "PatternUnitDeSe Show Donw!" << endl;
    ////////////////////////////////////////////
}

///////////////////////////////////////////////////////////////////////
// END PatternDeSe.cpp
///////////////////////////////////////////////////////////////////////