#include <iostream>
#include <vector>
#include <string>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <sstream>

using namespace std;

#define SearchWindow 4

#define LOGICAL 1
#define PHYSICAL 2
#define LENGTH 3

#define TRACE_FUNC(fmt,...) \
    printf("%s-->LINE:%d-->FUNC:%s: \n"##fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
//this three vector to get the original off when plfs_write;
//and will pattern detection when plfs_close;
//so can use pattern when plfs_read.
extern std::vector<off_t> pattern_logical_offset;
extern std::vector<off_t> pattern_physical_offset;
extern std::vector<size_t> pattern_length;

class PatternElem 
{
	public:
		PatternElem(){};
		~PatternElem(){};		

		int size();						//return size of seq;
		bool is_repeating();			//if all elem in seq is same that means is repeating;
		void pop(int n);				//delete the last n elems from seq;
		// std::vector<off_t>::iterator begin();
		
		void show();					//for debug;

	// private:
		string init;					//using string to store off_t or size_t BUT not using template;
		std::vector<off_t> seq;
		int cnt;
};

class PatternUnit
{
	public:
		PatternUnit(){};
		~PatternUnit(){};

		void show();								//for debug;
		
	// private:
		PatternElem pat_off;						//one logical correponding multiple physical or length;
		std::vector<PatternElem> pat_phy;
		std::vector<PatternElem> pat_len;
};

//PatternEntry is to store the off generated bt the process(have same pid);
class PatternEntry
{
	public:
		PatternEntry(){};
		~PatternEntry(){};
	
		void show();				//for debug;

	// private:
		pid_t id;
		std::vector<PatternUnit> entry;
};

//The global pattern entry;
extern std::vector<PatternEntry> Global_Pat_Entry;


void add_off(off_t off, off_t phy, size_t len);

//the mian function of pattern detection;
void pattern_detection(pid_t id);

//bulid the offset pattern;
std::vector<PatternElem> bulid_off_pat(int ty, int be, int en);

//bulid the delta of offset;
std::vector<off_t> build_delta(int ty, int be, int en);

//if lw[1:k] is equal to sw[1:k] return TURE;
bool equal_lw_sw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k);

//init look ahead window; NOT NEED to init search window;
std::vector<off_t> init_lw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k);

//lw merged with the last elem in pattern stack;
bool pat_merge(std::vector<PatternElem> &pat_stack, std::vector<off_t> lw, int k);

//insert init to pattern stack;
void bulid_ps_init(vector<PatternElem> pat_stack, int ty, int be, int en );

//bulid pattern entry ;
PatternEntry bulid_entry(std::vector<PatternElem> pat_stack);

//bulid pattern unit;
PatternUnit bulid_unit(PatternElem pat_off, int be, int en);


////////////////////////////////////////////
//Some Test Functions
////////////////////////////////////////////
void pp(std::vector<off_t> v);
void pp(std::vector<PatternElem> v);
void pp(std::vector<size_t> v);
void print_global();
void Test1(off_t *ch);