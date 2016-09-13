#ifndef __PatternDeSe_H__
#define __PatternDeSe_H__

#include "PatternIndex.h"

using namespace std;



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
// 	PatternElemDeSe(){};
// 	~PatternElemDeSe(){};
	
// 	off_t logical_offset;
// 	off_t physical_offset;
// 	size_t length;
// };


class PatternUnitDeSe
{
	public:
		PatternUnitDeSe(){
			id = -1; known_chunk = -1;
		};
		~PatternUnitDeSe(){};
		
		void setInit_offset();	// get logical_offset by the dese_unit;

		int look_up(off_t logical, PatternForQuery *pfq);
		void analyze_unit();

		void truncate(off_t offset);

		void show();

		off_t logical_offset;
		pid_t id;	// the process pid;
		pid_t known_chunk;	//the chunk_map id;
		string dese_unit;
		// queryList is for analyze dese_unit when query;
		// and will store it that when lookup in the unit,
		// we can search in the queryList rathen than analyze again.
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

		int dese_map_look_up(off_t logical, PatternForQuery *pfq);

		void truncate(off_t offset);

		void show();

		// vector<PatternUnitDeSe> dese_list;	
		map<off_t, PatternUnitDeSe> dese_map;
	
};


#endif