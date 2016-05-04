#ifndef __Pattern_H__
#define __Pattern_H__

#include <iostream>
#include <unistd.h>
#include <vector>
#include <map>
#include "COPYRIGHT.h"

#define TRACE_FUNC(fmt,...) \
    printf("%s-->LINE:%d-->FUNC:%s: \n"##fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define SearchWindow 4

using namespace std;

// typedef struct 
// {
// 	off_t offset[SearchWindow];
// 	int repeat;
// }pattern_off_elem;

// typedef struct 
// {
// 	size_t length[SearchWindow];
// 	int repeat;
// }pattern_len_elem;

typedef struct 
{
	int logical_offset;
	int physical_offset;
	int length;
	int end_sub;
}pattern_unit;

static std::map<pid_t, pattern_unit> global_map_entry;
static std::vector<char> global_vector;

static pid_t pattern_o_id = 0;
static std::vector<off_t> pattern_o_logical;
static std::vector<off_t> pattern_o_physical;
static std::vector<size_t> pattern_o_length;

void add_elem(pid_t id, off_t logical_offset, off_t physical_offset, size_t length);

bool pattern_init(pid_t id);

bool insert_to_pattern_entry(pid_t id, char *p_logical, char *p_physical, char *p_length);
bool insert_to_vector(char *pat);

void pattern_detection(std::vector<off_t> original, char *pat_string);
void pattern_detection(std::vector<size_t> original, char *pat_string);

char *int_to_char(int power, char *ch);
char *off_to_char(off_t off, char *ch);
char *size_to_char(size_t size, char *ch);

int char_to_power(char *ch);
int char_to_int(char *ch, int n);
off_t char_to_off(char *ch, int n);
size_t char_to_size(char *ch, int n);

bool cmp_int(int *p1, int *p2, int k);

int get_count(char *top);

int get_power(char *top);

void get_pat(char *ptr, char *stop_ptr, int *pat, int k, int n);
int get_p_tmp(char *top, int *pat, int power, int count, int k);

int canMerge(char *top, int *ori, int *pat, int power, int count, int k);

char *mergePat(char *top, int power, int count, int k, int flag);


//This is read use pattern detection
int read_from_pattern(char *buf, size_t size, off_t offset);

bool get_pid_pattern(pid_t &id, off_t &phy_offset, size_t &length, off_t offset);

//below functions that can to find the corresponding physical offset depanding the input ,
//inout is a logical offset. depanding the logical offset we can find the corresponding
//physical offset and the length;
bool find_rel_elem(pid_t id, off_t logical_offset, off_t &cor_physical, size_t &cor_length);

//this function find_in_pattern_logical is to get the relativer number in pattern logical ;
//example pattern logical :10#2,3,4# (common view is 10,12,15,19); we find logical_offset is 16 ; 
//that we count this number when we find 16, that it between 15 and 19;
//Step: find 10, less than 16, count = 1; find 12, less than 16, count = 2; 
//		find 15, less than 16, count = 3; find 19, more than 16, count = 3;
//		Return count = 3;
//So:	we find the real logical_offset, and get the count that we can use it to find the correspongding
//		physical_offset and length.
int find_in_pattern_logical(off_t logical_offset, int sub_logical, int sub_physical);

off_t find_relevant_physical(int sub_physical, int sub_length, int count);

size_t find_relevant_length(int sub_length, int sub_end, int count);

off_t get_initial_off(char *p);

size_t get_initial_size(char *p);

// int get_incremental(char *p);

void get_power_count(char *p, int &power, int &count);




//below functions are test method that to print some elem in container ;
void pp(int *ptr);
void pp(char *ptr);
void pp(std::vector<off_t> v);
void pp(std::vector<size_t> v);
void pp(std::vector<char> v);
void pp(int begin, int end);
void pp(std::map<pid_t, pattern_unit> global_map_entry);
void pp(pid_t id , std::map<pid_t, pattern_unit> global_map_entry);
// class Pattern
// {
// 	public:
// 		Pattern();
// 		~Pattern();

// 		// add_elem: to creat a tmp array to store the offset and length,
// 		// then sent to a function to detecte this pattern as arguments.
// 		int add_elem(off_t logical_offset, off_t physical_offset, size_t length)
// 		{

// 		}



// 	protected:
// 		std::vector<pattern_unit> pattern_entry;
// 		std::vector<off_t> logical_offset;
// 		std::vector<off_t> physical_offset;
// 		std::vector<size_t> length;
// };

#endif
