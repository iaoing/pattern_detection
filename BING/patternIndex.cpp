#include "PatternIndex.h"

using namespace std;

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

	pat_stack = build_off_pat(PATTERN_LOGICAL_FLAG, begin, end, id);
	/////////////////for debug//////////////////
	// for(vector<PatternElem>::iterator it = pat_stack.begin(); it != pat_stack.end(); ++it)
	// {
	// 	(*it).show();
	// }
	////////////////////////////////////////////

	//if(pat_stack.empty())
	//	return NULL;

	PatternEntry pat_entry;
	pat_entry = build_entry(pat_stack, id);
	//if(pat_entry.entry.empty())
	//	return ;

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
	// 	cout << dt << "\t" ;
	// }
	// cout << endl;
	////////////////////////////////////////////

	std::vector<PatternElem> pat_stack;						//pattern stack;
	std::vector<off_t> lw;									//look ahead window;
	std::vector<off_t>::iterator iter = delta.begin();		//iter represents the demarcation point 
	++iter;													//of serach window and look ahead window;
															//begin to iter-1 is sw, iter to end is lw;
	PatternElem tmp0;
	tmp0.seq.push_back(delta[0]);
	tmp0.cnt = 1;
	pat_stack.push_back(tmp0);
	while(iter != delta.end())
	{
		lw.clear();
		int k = SearchWindow;								//the size of the sliding window;
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
			if(pat_merge(pat_stack, lw, k) )				//weather can merge and merge in this func;
			{
				iter += k;									//sliding window;
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
	// 	pe.show();
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
		// 	tmp = (off_t)(*(it_1 + 1) - *it_1);
		// 	delta.push_back(tmp);
		// 	++it_1;
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
		// 	tmp = (off_t)(*(it_1 + 1) - *it_1);
		// 	delta.push_back(tmp);
		// 	++it_1;
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
	// 	return 0;
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
	if(	p_size == lw.size() )
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
	// 	pe.show();
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
	if(it_1 != this->seq.end())				//if equal means seq is empty;
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
	assert(tmp_buf.size() == totlesize);

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

	assert(totlesize == tmp_buf.size());
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

	assert(totlesize == tmp_buf.size());
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

	assert(totlesize == tmp_buf.size());
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

	assert(len == (totlesize + sizeof(totlesize)));
	while(position < len)
	{
		size_t entry_size = 0;
		string entry_ibuf;
		PatternEntry p_entry;

		memcpy(&entry_size, &buf[position], sizeof(entry_size));
		position += sizeof(entry_size);

		entry_ibuf.resize(entry_size);
		memcpy(&entry_ibuf[0], &buf[position], entry_size);
		assert(entry_size == entry_ibuf.size());
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
		assert(unit_size == unit_ibuf.size());
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
		// 	 << "ibuf_len: " << ibuf_len << endl;
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
		// 	 << "ibuf_len: " << ibuf_len << endl;
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
	if(pos <= 0)
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
			*ret_len = cur_off;
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
	// 	return 0;

	int col = cur_off % sumOffSeq;
	int row = cur_off / sumOffSeq;

	if(row >= pat_off.cnt)
	{
		cur_off = pat_off.getLastValue();
		cur_phy = pat_phy.rbegin()->getLastValue();
		cur_len = pat_len.rbegin()->getLastValue();

		if(query_off - cur_off < cur_len)
		{
			*ret_len = cur_off;
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
		// 	++col_pos;

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
			*ret_len = cur_off;
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

