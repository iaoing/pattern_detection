#ifndef __Index_H__
#define __Index_H__

#include "COPYRIGHT.h"
#include <set>
#include <map>
#include <vector>
#include <list>
using namespace std;

#include "Util.h"
#include "Metadata.h"

// #include "PatternIndex.h"
// #include "patternIndex.cpp"
// #include "PatternDeSe.h"
// #include "PatternDeSe.cpp"

// the LocalEntry (HostEntry) and the ContainerEntry should maybe be derived from one another.
// there are two types of index files
// on a write, every host has a host index
// on a read, all the host index files get aggregated into one container index

class IndexFileInfo{
    public:
        IndexFileInfo();
        void* listToStream(vector<IndexFileInfo> &list,int *bytes);
        vector<IndexFileInfo> streamToList(void * addr);
        //bool operator<(IndexFileInfo d1);
        double timestamp;
        string hostname;
        pid_t  id;
};

// this is the class that represents the records that get written into the
// index file for each host.
class HostEntry {
    public:
        HostEntry() { }
        HostEntry( off_t o, size_t s, pid_t p ) {
            logical_offset = o; length = s; id = p;
        }
        bool overlap( const HostEntry & );
        bool contains ( off_t ) const;
        bool splittable ( off_t ) const;
        bool abut   ( const HostEntry & );
        off_t logical_tail( ) const;

        off_t get_logical_offset(){return this->logical_offset;}
        off_t get_physical_offset(){return this->physical_offset;}
        size_t get_length(){return this->length;}
        double get_begin_timestamp(){return this->begin_timestamp;}
        double get_end_timestamp(){return this->end_timestamp;}
        pid_t get_id(){return this->id;}

    protected:
        off_t  logical_offset;
        off_t  physical_offset;  // I tried so hard to not put this in here
                                 // to save some bytes in the index entries
                                 // on disk.  But truncate breaks it all.
                                 // we assume that each write makes one entry
                                 // in the data file and one entry in the index
                                 // file.  But when we remove entries and 
                                 // rewrite the index, then we break this 
                                 // assumption.  blech.
        size_t length;
        double begin_timestamp;
        double end_timestamp;
        pid_t  id;      // needs to be last so no padding

    friend class Index;
};


// this is the class that represents one record in the in-memory 
// data structure that is
// the index for the entire container (the aggregation of the multiple host
// index files).  
// this in-memory structure is used to answer read's by finding the appropriate
// requested logical offset within one of the physical host index files
class ContainerEntry : HostEntry {
    public:
        bool mergable( const ContainerEntry & );
        bool abut( const ContainerEntry & );
        ContainerEntry split(off_t); //split in half, this is back, return front

    protected:
        pid_t original_chunk;	// we just need to track this so we can 
				// rewrite the index appropriately for
				// things like truncate to the middle or
				// for the as-yet-unwritten index flattening

	friend ostream& operator <<(ostream &,const ContainerEntry &);

    friend class Index;
};

// this is a way to associate a integer with a local file
// so that the aggregated index can just have an int to point
// to a local file instead of putting the full string in there
typedef struct {
    string path;
    int fd;
} ChunkFile;


///////////////////////////////////////////////////////////////////
//PatternInde.h
///////////////////////////////////////////////////////////////////
#define PATTERN_LOGICAL_FLAG 1
#define PATTERN_PHYSICAL_FLAG 2
#define PATTERN_LENGTH_FLAG 3
#define SearchWindow 4

//////////////////
//some tools
//////////////////

off_t string_to_off(string str);
bool isContain( off_t logical, off_t physical, off_t length );


//////////////////
//some classes
//////////////////

// class HostEntry
// {
//  public:
//     /* constructors */
//     HostEntry(){
//     };
//     HostEntry(off_t o, off_t p, size_t s, pid_t id){
//      this->logical_offset = o;
//      this->physical_offset = p;
//      this->length = s;
//      this->id = id;
//     }
    
//     void setBegin(double t)
//     {
//      this->begin_timestamp = t;
//     }
    
//     void setEnd(double t)
//     {
//      this->end_timestamp = t;
//     }
    
//     void show(){
//      cout << "logical: " << this->logical_offset << "\t"
//           << "physical: " << this->physical_offset << "\t"
//           << "length: " << this->length << "\t"
//           << "pid: " << this->id << "\t"
//           << "beginTime: " << this->begin_timestamp << "\t"
//           << "endTime: " << this->end_timestamp << "\t"
//           << endl;
//     }
    
//     off_t get_logical_offset(){return this->logical_offset;}
//     off_t get_physical_offset(){return this->physical_offset;}
//     size_t get_length(){return this->length;}
//     double get_begin_timestamp(){return this->begin_timestamp;}
//     double get_end_timestamp(){return this->end_timestamp;}
//     pid_t get_id(){return this->id;}
    
//  protected:
//     off_t  logical_offset;    /* logical offset in container file */
//     off_t  physical_offset;   /* physical offset in data dropping file */
//     size_t length;            /* number of data bytes, can be zero */
//     double begin_timestamp;   /* time write started */
//     double end_timestamp;     /* time write completed */
//     pid_t  id;                /* id (to locate data dropping) */

// };

class PatternIndexCollect
{
    public:
        PatternIndexCollect(){
        };
        //~PatternIndexCollect();
        void push_entry(HostEntry h_entry);

        void show();    //for debug;

    //private:
        std::vector<off_t> collect_logical;
        std::vector<off_t> collect_physical;
        std::vector<size_t> collect_length;
        std::vector<double> collect_begin;
        std::vector<double> collect_end;
};

class PatternElem 
{
    public:
        PatternElem(){
        };
        ~PatternElem(){
            // init.clear();
            // init.resize(0);
            seq.clear();
            seq.resize(0);
        };      

        int size();                     //return size of seq;
        bool is_repeating();            //if all elem in seq is same that means is repeating;
        void pop(int n);                //delete the last n elems from seq;
        
        //for write; Serialization;
        size_t append_to_buf(string *buf);

        //for read; DeSerialization;
        //void get_ibuf_elem(void *ibuf);

        //for query;
        off_t sumVecSeq();
        off_t getValueByPos(int pos);
        off_t getLastValue();

        //for debug;
        void show();                   

    // private:
        long int init;
        int cnt;                    //using string to store off_t or size_t BUT not using template;
        std::vector<off_t> seq;
};

class PatternUnit
{
    public:
        PatternUnit(){
        };
        //~PatternUnit();

        //for write; Serialization;
        size_t append_to_buf(string *buf);

        //for read; DeSerialization;
        void get_ibuf_unit(string ibuf);

        //for query;
        bool look_up(off_t query_off, off_t *ret_log, off_t *ret_phy, size_t *ret_len);

        //for debug;
        void show();                                
        
    // private:
        PatternElem pat_off;                        //one logical correponding multiple physical or length;
        std::vector<PatternElem> pat_phy;
        std::vector<PatternElem> pat_len;
};

//PatternEntry is to store the off generated by the same process(have the same pid);
class PatternEntry
{
    public:
        PatternEntry(){};
        //~PatternEntry(){};
        
        //for write; Serialization;
        size_t append_to_buf(string *buf);

        //for read; DeSerialization;
        void get_ibuf_entry(string ibuf);

        void show();                //for debug;

    // private:
        pid_t id;
        std::vector<PatternUnit> entry;
        std::vector<double> collect_begin;
        std::vector<double> collect_end;
};

class PatternIndex
{
    public:
        PatternIndex(){
        };
        ~PatternIndex(){
            index_collect.clear();
            p_writebuf.clear();
            p_writebuf.resize(0);
        };

        void collectIndex(HostEntry h_entry);
        void pattern_detection(); //The main function of pattern detection.
        PatternEntry pattern_by_pid(pid_t id);
        std::vector<PatternElem> build_off_pat(int type, int begin, int end, pid_t id);
        std::vector<off_t> build_delta(int type, int begin, int end, pid_t id); //build the delta of offset;
        bool equal_lw_sw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k);
        std::vector<off_t> init_lw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k);
        bool pat_merge(std::vector<PatternElem> &pat_stack, std::vector<off_t> lw, int k);
        vector<PatternElem> build_ps_init(vector<PatternElem> pat_stack, int type, int begin, int end, pid_t id);

        PatternEntry build_entry(std::vector<PatternElem> pat_stack, pid_t id);
        PatternUnit build_unit(PatternElem pat, int begin, int end, pid_t id);

        std::vector<off_t> getCollectOffElem(int type, pid_t id);
        std::vector<size_t> getCollectLenElem(int type, pid_t id);

    //////////for debug;
        void show_collect();
        void show_writebuf();
        void show_readbuf();

    //////////for write; Serialization;
        size_t append_to_buf(string *buf);

    //////////for read; DeSerialization;
    //////////maybe this should be delete;
        void get_ibuf_info(void *ibuf, size_t len);

    //private:
        /* index_collect is collect the index info when the flushIndex() */
        std::map<pid_t, PatternIndexCollect> index_collect;
        /* p_writebuf store the struct that should be serialization and then write to file */
        std::vector<PatternEntry> p_writebuf;
        /* p_readbuf is orignal from the buf that read from file */
        std::vector<PatternEntry> p_readbuf;
};




///////////////////////////////////
//for debug;
///////////////////////////////////
template <typename T>
void show_vector(vector<T> v)
{
    int len = v.size();
    for(int i = 0; i < len; ++i)
    {
        cout << v[i] << "\t" ;
    }
    cout << endl;
}

///////////////////////////////////////////////////////////////////////
// END PatternINdex.h
///////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////
// PatternDeSe.h
///////////////////////////////////////////////////////////////////////

/**
* PatternForQuery using in func query_helper_getrec()
* The class is to store the info when query index 
* where in the map<off_t, ContainerPatternUnit> idx;
*/
class PatternForQuery
{
public:
    PatternForQuery(){
        exist = 0;
    };
    PatternForQuery(bool, pid_t, off_t, off_t, size_t);
    ~PatternForQuery(){};

    ////////for debug;
    void show();

    bool exist;
    pid_t id;
    pid_t original_chunk;
    off_t logical_offset;
    off_t physical_offset;
    size_t length;
};




/**
 * PatternEntryDeSe and PatternListDeSe is using when DeSerilization;
 * Not real DeSe, only split the readbuf into small parttion which 
 * contains the info about every PatternEntry, and will get the PatternUnit
 * pat_off's init(init of logical offset);
 * So that we can easy to query the index info and small the 
 * time for DeSe the readbuf;
 */

// class PatternElemDeSe
// {
// public:
//  PatternElemDeSe(){};
//  ~PatternElemDeSe(){};
    
//  off_t logical_offset;
//  off_t physical_offset;
//  size_t length;
// };


class PatternUnitDeSe
{
    public:
        PatternUnitDeSe(){
            id = -1; known_chunk = -1; ql_pos = 0;
        };
        ~PatternUnitDeSe(){};
        
        void setInit_offset();  // get logical_offset by the dese_unit;

        bool look_up(off_t logical, PatternForQuery *pfq);
        void analyze_unit();
        void arrange_queryList();

        void truncate(off_t offset);

        void show();

        off_t logical_offset;
        pid_t id;   // the process pid;
        pid_t known_chunk;  //the chunk_map id;
        string dese_unit;
        PatternUnit query_unit;
        // queryList is for analyze dese_unit when query;
        // and will store it that when lookup in the unit,
        // we can search in the queryList rathen than analyze again.
        int ql_pos;
        std::vector<PatternForQuery> queryList;

};

class PatternListDeSe
{
    public:
        PatternListDeSe(){};
        ~PatternListDeSe(){};

        //////////for read; DeSerialization; 
        //And the DeSe.ed info storing in dese_map;
        void Pattern_DeSe(void *ibuf, size_t len);

        void Pattern_DeSe_Entry(string *ibuf);

        bool dese_map_look_up(off_t logical, PatternForQuery *pfq);

        void truncate(off_t offset);

        void show();

        // vector<PatternUnitDeSe> dese_list;   
        map<off_t, PatternUnitDeSe> dese_map;
    
};
///////////////////////////////////////////////////////////////////////
// END PatternDeSe.h
///////////////////////////////////////////////////////////////////////



class Index : public Metadata {
    public:
        Index( string ); 
        Index( string path, int fd ); 
        ~Index();

        int readIndex( string hostindex );
    
        void setPath( string );

        bool ispopulated( );

        void addWrite( off_t offset, size_t bytes, pid_t, double, double );

        size_t memoryFootprintMBs();    // how much area the index is occupying

        int flush();

        off_t lastOffset( );

        void lock( const char *function );
        void unlock(  const char *function );

        int getFd() { return fd; }
        void resetFd( int fd ) { this->fd = fd; }

        size_t totalBytes( );

        int getChunkFd( pid_t chunk_id );

        int setChunkFd( pid_t chunk_id, int fd );

        int globalLookup( int *fd, off_t *chunk_off, size_t *length, 
                string &path, bool *hole, pid_t *chunk_id, off_t logical ); 

        int insertGlobal( ContainerEntry * );
        void merge( Index *other);
        void truncate( off_t offset );
        int rewriteIndex( int fd );
        void truncateHostIndex( off_t offset );

        void compress();
        int debug_from_stream(void *addr);
        int global_to_file(int fd);
        int global_from_stream(void *addr); 
        int global_to_stream(void **buffer,size_t *length);
		friend ostream& operator <<(ostream &,const Index &);
        // Added to get chunk path on write
        string index_path;
        void startBuffering(); 
        void stopBuffering();  
        bool isBuffering();


        /**
         * Using the pattern detection;
         * By Bing.
         */
        int chunkFound_pat( int *, off_t *, size_t *, off_t, 
                string &, pid_t *, PatternForQuery* );
        int orig_globalLookup( int *fd, off_t *chunk_off, size_t *length, 
                string &path, bool *hole, pid_t *chunk_id, off_t logical ); 
        // PatternIndex pat_index;
        PatternListDeSe pat_List_DeSe;
        
    private:
        void init( string );
        int chunkFound( int *, off_t *, size_t *, off_t, 
                string &, pid_t *, ContainerEntry* );
        int cleanupReadIndex(int, void *, off_t, int, const char*, const char*);
        void *mapIndex( string, int *, off_t * );
        int handleOverlap( ContainerEntry &g_entry,
            pair< map<off_t,ContainerEntry>::iterator, bool > &insert_ret );
        map<off_t,ContainerEntry>::iterator insertGlobalEntryHint(
            ContainerEntry *g_entry ,map<off_t,ContainerEntry>::iterator hint);
        pair<map<off_t,ContainerEntry>::iterator,bool> insertGlobalEntry(
            ContainerEntry *g_entry);
        size_t splitEntry(ContainerEntry*,set<off_t> &,
                multimap<off_t,ContainerEntry> &);
        void findSplits(ContainerEntry&,set<off_t> &);
            // where we buffer the host index (i.e. write)
        vector< HostEntry > hostIndex;

            // this is a global index made by aggregating multiple locals
        map< off_t, ContainerEntry > global_index;

            // this is a way to associate a integer with a local file
            // so that the aggregated index can just have an int to point
            // to a local file instead of putting the full string in there
        vector< ChunkFile >       chunk_map;

            // need to remember the current offset position within each chunk 
        map<pid_t,off_t> physical_offsets;

        bool   populated;
        pid_t  mypid;
        string logical_path;
        int    chunk_id;
        off_t  last_offset;
        size_t total_bytes;
        int    fd;
        bool buffering;    // are we buffering the index on write?
        bool buffer_filled; // were we buffering but ran out of space? 
        pthread_mutex_t    fd_mux;   // to allow thread safety

};

#define MAP_ITR map<off_t,ContainerEntry>::iterator

#endif
