#include "Pattern.h"

using namespace std;

std::vector<off_t> pattern_logical_offset;
std::vector<off_t> pattern_physical_offset;
std::vector<size_t> pattern_length;
std::vector<PatternEntry> Global_Pat_Entry;

//push the data into global variable vector
void 
add_off(off_t off, off_t phy, size_t len)
{
	pattern_logical_offset.push_back(off);
	pattern_physical_offset.push_back(phy);
	pattern_length.push_back(len);
}

//the main function of pattern detection;
void 
pattern_detection(pid_t id)
{
	if(pattern_logical_offset.empty())
		return ;
	pp(pattern_logical_offset);
	pp(pattern_physical_offset);
	pp(pattern_length);
	std::vector<PatternElem> pat_stack;
	pat_stack = bulid_off_pat(LOGICAL, 0, pattern_logical_offset.size());
	if(pat_stack.empty())
		return ;

	PatternEntry pat_entry;
	pat_entry = bulid_entry(pat_stack);
	if(pat_entry.entry.empty())
		return ;

	pat_entry.id = id;
	Global_Pat_Entry.push_back(pat_entry);

	//clean the global vector logical physical and length
	std::vector<off_t>().swap(pattern_logical_offset);
	std::vector<off_t>().swap(pattern_physical_offset);
	std::vector<size_t>().swap(pattern_length);
}

//bulid pattern entry ;
PatternEntry 
bulid_entry(std::vector<PatternElem> pat_stack)
{
	PatternUnit pat_unit;
	PatternEntry pat_entry;
	std::vector<PatternElem>::iterator iter;
	int be = 0, en = 0;
	for(iter = pat_stack.begin(); iter != pat_stack.end(); ++iter)
	{
		en += ((iter->size()) * (iter->cnt));
		pat_unit = bulid_unit(*iter, be, en);
		pat_entry.entry.push_back(pat_unit);
		be += (iter->size() * iter->cnt);
	}
	return pat_entry;
}

//bulid pattern unit;
PatternUnit 
bulid_unit(PatternElem pat, int be, int en)
{
	std::vector<PatternElem> phy;
	std::vector<PatternElem> len;
	PatternUnit ret;
	phy = bulid_off_pat(PHYSICAL, be, en);
	len = bulid_off_pat(LENGTH, be, en);
	assert(!phy.empty() && !len.empty());

	ret.pat_off = pat;
	ret.pat_phy = phy;
	ret.pat_len = len;

	return ret;
}

//this function is to build the pattern;
std::vector<PatternElem> 
bulid_off_pat(int ty, int be, int en)
{
	// assert(en == pattern_logical_offset.size());
	std::vector<off_t> delta;
	delta = build_delta(ty, be, en);

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
			if(pat_merge(pat_stack, lw, k) )				//weath can merge and merge in this func;
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

	return pat_stack;
}

//this function is to bulid the delta;
std::vector<off_t>
build_delta(int ty, int be, int en)
{
	assert( (ty == LOGICAL) || (ty == PHYSICAL) || (ty == LENGTH) );
	std::vector<off_t> delta;
	if(ty == LOGICAL || ty==PHYSICAL)
	{
		std::vector<off_t>::iterator it_1, it_2;
		it_1 = pattern_logical_offset.begin() + be;
		it_2 = pattern_logical_offset.begin() + en;
		off_t tmp;
		while(it_1 != it_2 - 1)
		{
			tmp = (off_t)(*(it_1 + 1) - *it_1);
			delta.push_back(tmp);
			++it_1;
		}
	}else if(ty == LENGTH)
	{
		std::vector<size_t>::iterator it_1, it_2;
		it_1 = pattern_length.begin() + be;
		it_2 = pattern_length.begin() + en;
		off_t tmp;
		while(it_1 != it_2 - 1)
		{
			tmp = (off_t)(*(it_1 + 1) - *it_1);
			delta.push_back(tmp);
			++it_1;
		}
	}
	return delta;
}

//if lw[1:k] is equal to sw[1:k] return TURE;
//sw is [iter-k : iter), lw is [iter : iter + k)
bool  
equal_lw_sw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k)
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
init_lw(std::vector<off_t> delta, std::vector<off_t>::iterator iter, int k)
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
pat_merge(std::vector<PatternElem> &pat_stack, std::vector<off_t> lw, int k)
{
	//case 0: if ps is empty means can't merge;
	if(pat_stack.empty() || lw.size() != k)
	{
		TRACE_FUNC();
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
void 
bulid_ps_init(vector<PatternElem> pat_stack, int ty, int be, int en)
{
	assert(ty == LOGICAL || ty == PHYSICAL || ty == LENGTH);
	vector<PatternElem>::iterator it_1;
	it_1 = pat_stack.begin();
	stringstream oss;
	if(ty == LOGICAL)
	{
		std::vector<off_t>::iterator it_2, it_3;
		it_2 = pattern_logical_offset.begin() + be;
		it_3 = pattern_logical_offset.begin() + en;
		while(it_1 != pat_stack.end() && it_2 != it_3)
		{
			oss << *it_2;
			oss >> it_1->init;
			oss.clear();
			oss.str("");
			it_2 += (it_1->size() * (it_1->cnt));
			++it_1;
		}
	}else if(ty == PHYSICAL)
	{
		std::vector<off_t>::iterator it_2, it_3;
		it_2 = pattern_physical_offset.begin() + be;
		it_3 = pattern_physical_offset.begin() + en;
		while(it_1 != pat_stack.end() && it_2 != it_3)
		{
			oss << *it_2;
			oss >> it_1->init;
			oss.clear();
			oss.str("");
			it_1->init = *it_2;
			it_2 += (it_1->size() * (it_1->cnt));
			++it_1;
		}
	}else if(ty == LENGTH)
	{
		std::vector<size_t>::iterator it_2, it_3;
		it_2 = pattern_length.begin() + be;
		it_3 = pattern_length.begin() + en;
		while(it_1 != pat_stack.end() && it_2 != it_3)
		{
			oss << *it_2;
			oss >> it_1->init;
			oss.clear();
			oss.str("");
			it_1->init = *it_2;
			it_2 += (it_1->size() * (it_1->cnt));
			++it_1;
		}
	}
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



//////////////////////////////////////////////////////
//for debug
//////////////////////////////////////////////////////
void 
PatternElem::show()
{
	cout << "[" << this->init << "(" ;
	std::vector<off_t>::iterator v;
	for(v = this->seq.begin(); v != (this->seq.end() - 1); ++v)
	{
		cout << *v << "," ;
	}
	cout << *v << ")^"  << this->cnt << "]" << endl;
}

// std::vector<off_t>::iterator 
// PatternElem::begin()
// {

// }

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

////////////////////////////////////////////
//Some Test Functions
////////////////////////////////////////////
void pp(std::vector<off_t> v)
{
	std::vector<off_t>::iterator it;
	for(it = v.begin(); it != v.end(); ++it)
	{
		cout << *it << "   " ;
	}
	cout << endl;
}

void pp(std::vector<PatternElem> v)
{
	std::vector<PatternElem>::iterator it;
	for(it = v.begin(); it != v.end(); ++it)
	{
		it->show();
	}
	cout << endl;
}

void pp(std::vector<size_t> v)
{
	std::vector<size_t>::iterator it;
	for(it = v.begin(); it != v.end(); ++it)
	{
		cout << *it << "   " ;
	}
	cout << endl;
}

void print_global()
{
	if(Global_Pat_Entry.empty())
		return ;
	std::vector<PatternEntry>::iterator it;
	for(it = Global_Pat_Entry.begin(); it != Global_Pat_Entry.end(); ++it)
	{
		cout << "******* Process " << it - Global_Pat_Entry.begin() + 1 << " *******" << endl;
		it->show();
		cout << endl;
	}
}

void Test1(off_t *ch)
{
	int be = 0, en = 0, ty = 1, i = 0;
	for(i = 0; ch[i] != '\0'; ++i)
	{
		pattern_logical_offset.push_back(ch[i]);
		++en;
	}
	std::vector<PatternElem> ps = bulid_off_pat(ty, be, en);
	pp(ps);
	std::vector<off_t>().swap(pattern_logical_offset);
}
