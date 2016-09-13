#include "PatternIndex.h"
#include <ctime>
#include <unistd.h>
#include <iostream>
#include <vector>
#include "patternIndex.cpp"

using namespace std;

vector<HostEntry> writebuf;
void init_writebuf();
void show_writebuf();

int main()
{
	init_writebuf();
	//show_writebuf();
	
	size_t len;
	void *start;
	PatternIndex p_index;
	for(HostEntry h_entry : writebuf)
    {
    	p_index.collectIndex(h_entry);
    }
    //p_index.show_collect();
    
    //now all index info pattern into vector<PatternEntry> p_writebuf;
    p_index.pattern_detection();
	p_index.show_writebuf();

    //////////for write;Serialization;
    string buf;
    p_index.append_to_buf(&buf);

    cout << "Serialization:\n"
         << buf << endl;

    p_index.~PatternIndex();
	
	size_t buf_len = buf.length();
	cout << buf_len << endl;

	void *ibuf = (void *)&buf[0];
	// p_index.get_ibuf_info(ibuf,buf_len);

	// p_index.show_readbuf();

	// cout << "===================" << "\n\n" << endl;

	// /**
	//  * 0. 到此为止，测试的主要是三个方面的东西: 
	//  * 1. 将初始化的放在 Entry 中的 index 模式识别，转换成 patternindex， 并存放在 writebuf 里面；
	//  * 2. 将存放在 writebuf 里面的已经整个的 index 信息进行序列化 Serialization， 并存在在 buf 中 ；
	//  * 3. 将 string &buf[0] 转化为 void *ibuf， 模拟从文件中读取时获得的 void* 指针，
	//  *	  然后解序列化 DeSerialization；并将还原的 patternindex 信息存放在 readbuf 里面；
	//  */
	
	// /* 开始下一步的测试，在 read 的过程中会出现的一些情况，主要是对 query 的测试 */
	// map<off_t, ContainerPatternUnit> idx;

	// for(PatternEntry pat_entry : p_index.p_readbuf)
 //    {
 //        for(PatternUnit pat_unit : pat_entry.entry)
 //        {
 //            ContainerPatternUnit c_p_unit;

 //            c_p_unit.id                = pat_entry.id; /* slot# */
 //            c_p_unit.original_chunk    = pat_entry.id; /* save old pid for rewrites */

 //            c_p_unit.pat_off           = pat_unit.pat_off;
 //            c_p_unit.pat_phy           = pat_unit.pat_phy;
 //            c_p_unit.pat_len           = pat_unit.pat_len;

 //            off_t ins_off = string_to_off(pat_unit.pat_off.init);
 //            idx.insert(pair<off_t, ContainerPatternUnit>(ins_off, c_p_unit));
 //        }
 //    }

 //    show_idx(idx);
 //    cout << "===================" << "\n\n" << endl;

 //    map<off_t,ContainerPatternUnit>::iterator qitr;
 //    bool end_flag;
 //    PatternForQuery prev_pfq, qitr_pfq, next_pfq;
 //    qitr = idx.lower_bound(0);
 //    //size_t sss = qitr->second.get_totle_length();
 //    //cout << "totle_length: " << sss << endl;
 //    off_t sss = qitr->second.get_last_logical();
 //    cout << "last_logical: " << sss << endl;
 //    end_flag = qitr->second.get_query_info(43, &prev_pfq, &qitr_pfq, &next_pfq);

 //    prev_pfq.show();
 //    qitr_pfq.show();
 //    next_pfq.show();


	/**
	 * test the pattern_dese;
	 */
	cout << "\n\n\n" 
	 	 << "Pattern DeSerialization!"
	 	 << "\n\n" << endl;
	PatternListDeSe pat_dese;
	pat_dese.Pattern_DeSe(ibuf, buf_len);
	pat_dese.show();

	cout << "\n\n\n" 
	 	 << "Pattern lookUp!"
	 	 << "\n\n" << endl;
	PatternForQuery pfq;
	pat_dese.dese_map_look_up(55, &pfq);
	pfq.show();

	// cout << "\n\n\n" 
	//  	 << "Test PatternUnitDeSe analyze unit!"
	//  	 << "\n\n" << endl;
	// map<off_t, PatternUnitDeSe>::iterator it;
	// for(it = pat_dese.dese_map.begin(); it != pat_dese.dese_map.end(); ++it)
	// {
	// 	it->second.analyze_unit();
	// }
	// pat_dese.show();


	// cout << "\n\n\n" 
	//  	 << "Test PatternListDeSe LookUp!"
	//  	 << "\n\n" << endl;
	// PatternForQuery pfq;
	// pat_dese.dese_map_look_up(10, &pfq);
	// pfq.show();

	return 0;
}

void init_writebuf()
{
	off_t ch1[100] = {0,3,7,14,17,21,28,31,35,42,46,50,54,58};
	off_t ch2[100] = {0,2,4,8,10,12,16,18,20,24,27,30,33,36};
	size_t ch3[100] = {2,2,4,2,2,4,2,2,4,3,3,3,3,3};
	pid_t id = 4287;
	for(int i = 0; i < 14; ++i)
	{
		HostEntry h_entry(ch1[i], ch2[i], ch3[i], id);
		h_entry.setBegin(time(NULL));
		h_entry.setEnd(time(NULL));
		writebuf.push_back(h_entry);
	}
}

// void test_query()
// {
// 	for(PatternEntry pat_entry : pat_index.p_readbuf)
//     {
//         for(PatternUnit pat_unit : pat_entry.entry)
//         {
//             ContainerPatternUnit c_p_unit;

//             c_p_unit.id                = known_chunks[pat_entry.id]; /* slot# */
//             c_p_unit.original_chunk    = pat_entry.id; /* save old pid for rewrites */

//             c_p_unit.pat_off           = pat_unit.pat_off;
//             c_p_unit.pat_phy           = pat_unit.pat_phy;
//             c_p_unit.pat_len           = pat_unit.pat_len;
            
//             off_t ins_off = string_to_off(pat_unit.pat_off.init);
//             idxout.insert(pair<off_t, ConatinerPatternUnit>(ins_off, c_p_unit));
//         }
//     }
// }

void show_writebuf()
{
	for(HostEntry h_entry : writebuf)
	{
		h_entry.show();
		
	}
	cout << endl;
}

void show_idx(map<off_t, ContainerPatternUnit> idx)
{
	map<off_t, ContainerPatternUnit>::iterator it;
	for(it = idx.begin(); it != idx.end(); ++it)
	{
		cout << "===\n" << endl;
		cout << "off_t: " << it->first << endl;
		it->second.show_cpu();
	}
}
