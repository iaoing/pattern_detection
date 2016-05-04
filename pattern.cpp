#include <errno.h>
// #include "COPYRIGHT.h"
#include <string>
// #include <fstream>
// #include <iostream>
// #include <fcntl.h>
// #include <sys/types.h>
// #include <sys/dir.h>
// #include <dirent.h>
// #include <math.h>
// #include <assert.h>
// #include <sys/syscall.h>
// #include <sys/param.h>
// #include <sys/mount.h>
// #include <sys/statvfs.h>
// #include <iomanip>
// #include <iostream>
// #include <sstream>
// #include <stdlib.h>
#include <unistd.h>
// #include <time.h>
// #include "plfs.h"
// #include "Container.h"
#include "Index.h"
// #include "plfs_private.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <vector>
#include <map>
#include "Pattern.h"

using namespace std;

void add_elem(pid_t id, off_t logical_offset, off_t physical_offset, size_t length)
{
	if(pattern_o_id == 0)
		pattern_o_id = id;
	if(pattern_o_id == id )
	{
		pattern_o_logical.push_back(logical_offset);
		pattern_o_physical.push_back(physical_offset);
		pattern_o_length.push_back(length);
	}else
	{
		if(pattern_init(pattern_o_id))
			;
		std::vector<off_t>().swap(pattern_o_logical);
		std::vector<off_t>().swap(pattern_o_physical);
		std::vector<size_t>().swap(pattern_o_length);
		pattern_o_id = id;
	}
}

bool pattern_init(pid_t id)
{
	char p_logical[pattern_o_logical.size()*8];
	memset(p_logical, 0, sizeof(p_logical));

	char p_physical[pattern_o_logical.size()*8];
	memset(p_physical, 0, sizeof(p_physical));

	char p_length[pattern_o_logical.size()*8];
	memset(p_length, 0, sizeof(p_length));

	pattern_detection(pattern_o_logical, p_logical);
	pattern_detection(pattern_o_physical, p_physical);
	pattern_detection(pattern_o_lengthm p_length);

	if(p_logical == NULL || p_physical == NULL || p_length == NULL)
	{
		TRACE_FUNC();
		return 0;
	}

	if (insert_to_pattern_entry(id, p_logical, p_physical, p_length))
		;

	// pattern_entry.insert(pair<pid_t, pattern_unit>(id, p_unit));
	return 1;
}

bool insert_to_pattern_entry(pid_t id, char *p_logical, char *p_physical, char *p_length)
{
	pattern_unit p_unit;

	p_unit.logical_offset = global_vector.end() - global_vector.begin();
	if(insert_to_vector(p_logical))
		;

	p_unit.physical_offset = global_vector.end() - global_vector.begin();
	if(insert_to_vector(p_physical))
		;

	p_unit.length = global_vector.end() - global_vector.begin();
	if(insert_to_vector(p_length))
		;

	p_unit.end_sub = global_vector.end() - global_vector.begin();

	global_map_entry.insert(pair<pid_t, pattern_unit> (id, p_unit));

	return 1;
}

bool insert_to_vector(char *pat)
{
	while(*pat != '\0')
	{
		global_vector.push_back(*pat++);
	}
	if(*(--global_vector.end()) == *(--pat) )
		return 1;
	TRACE_FUNC();
	return 0;
}

void pattern_detection(std::vector<off_t> ori_offset, char *p_offset)
{
	int ori_len = ori_offset.size();
	int delta_len = ori_len - 1;
	int delta[ori_len];
	memset(delta, 0 , sizeof(delta));
	for(int i = 0; i < ori_len - 2 ; ++i)
	{
		delta[i] = ori_offset.at(i+1) - ori_offset.at(i);
	}

	// char p_offset[ori_offset.size()*8];
	memset(p_offset, 0, sizeof(p_offset));
	char *top = p_offset;
	top = off_to_char(ori_offset[0], top);
	*top++ = '#';
	top = int_to_char(delta[0], top);
	*top = '#';

	int delta_sub = 1;
	int power = 0, k = 0, count = 0, flag = 0;
	int o_tmp1[SearchWindow+1], o_tmp2[SearchWindow+1], p_tmp[SearchWindow+1];
	while(delta_len - delta_sub > 1)
	{
		//初始化k的值，如果在delta的头部或者尾部
		//则k的值就不应该是SearchWindow，而是小于SearchWindow
		//这里就是在一开始的时候先确定k的值，然后再k--
		k = SearchWindow;
		if(delta_len - delta_sub < SearchWindow)
			k = delta_len - delta_sub;
		if(delta_sub < k)
			k = delta_sub;

		//k的值就是当前SearchWindow的大小
		while(k)
		{
			memset(o_tmp1, 0, sizeof(o_tmp1));
			memset(o_tmp2, 0, sizeof(o_tmp2));
			memset(p_tmp, 0, sizeof(p_tmp));
			for(int i = 0; i < k; ++i)
			{
				o_tmp1[i] = delta[delta_sub - k + i];
				o_tmp2[i] = delta[delta_sub + i];
			}
			
			power = get_power(top);
			count = get_count(top);
			flag = 0;

			//power是get_p_tmp_off返回的power值，正常情况下返回1，或者大于1的数；
			//当pattern里面的最后一个unit，也就是两个# # 号之间的数据数量；
			//如果数量乘以^后面的power数值，如果不等于k；
			//说明不能merge，那么置power=0；
			//cmp_ori是进行比较Search window和Look ahead window内的内容是否相等
			//如果相等，则再调用函数canMerge，用来比较在pattern里面的k个数据，
			//是否与Look ahead window内的数据相等，如果相等则可以merge；
			//如果上述两个条件中有一个不符合，那么就会--k；
			//如果k==1，那么说明这次已经是最后一次比较了，将第一个数据压进pattern中，
			//然后将窗口向前滑动1个距离；
			if(cmp_int(o_tmp1, o_tmp2, k) && (flag = canMerge(top, o_tmp2, p_tmp, power, count,  k)) > 0)
			{
				top = mergePat(top, power, count ,k, flag);
				if(top == NULL)
				{
					TRACE_FUNC();
					return;
				}
				break;
			}else if(k == 1)
			{
				if(*top != '#')
				{
					TRACE_FUNC();
					return;
				}
				if(flag == 0 && power == 0)
				{
					*top++ = ',';
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				else if(flag == 0 && power > 1)
				{
					++top;
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				else if(flag == -1 || flag == -2 || flag == -3)
				{
					*top++ = ',';
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}else if(flag == -4 || flag == -5)
				{
					++top;
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				break;
			}
			--k;
		}
		//下面这条语句就是讲窗口向前滑动k个距离
		delta_sub += k;
	}
}

void pattern_detection(std::vector<size_t> ori_offlen, char *p_offlen)
{
	int ori_len = ori_offlen.size();
	int delta_len = ori_len - 1;
	int delta[ori_len];
	memset(delta, 0 , sizeof(delta));
	for(int i = 0; i < ori_len - 2 ; ++i)
	{
		delta[i] = ori_offlen.at(i+1) - ori_offlen.at(i);
	}

	// char p_offlen[ori_offlen.size()*8];
	memset(p_offlen, 0, sizeof(p_offlen));
	char *top = p_offlen;
	top = off_to_char(ori_offlen[0], top);
	*top++ = '#';
	top = int_to_char(delta[0], top);
	*top = '#';

	int delta_sub = 1;
	int power = 0, k = 0, count = 0, flag = 0;
	int o_tmp1[SearchWindow+1], o_tmp2[SearchWindow+1], p_tmp[SearchWindow+1];
	while(delta_len - delta_sub > 1)
	{
		//初始化k的值，如果在delta的头部或者尾部
		//则k的值就不应该是SearchWindow，而是小于SearchWindow
		//这里就是在一开始的时候先确定k的值，然后再k--
		k = SearchWindow;
		if(delta_len - delta_sub < SearchWindow)
			k = delta_len - delta_sub;
		if(delta_sub < k)
			k = delta_sub;

		//k的值就是当前SearchWindow的大小
		while(k)
		{
			memset(o_tmp1, 0, sizeof(o_tmp1));
			memset(o_tmp2, 0, sizeof(o_tmp2));
			memset(p_tmp, 0, sizeof(p_tmp));
			for(int i = 0; i < k; ++i)
			{
				o_tmp1[i] = delta[delta_sub - k + i];
				o_tmp2[i] = delta[delta_sub + i];
			}
			
			power = get_power(top);
			count = get_count(top);
			flag = 0;

			//power是get_p_tmp_off返回的power值，正常情况下返回1，或者大于1的数；
			//当pattern里面的最后一个unit，也就是两个# # 号之间的数据数量；
			//如果数量乘以^后面的power数值，如果不等于k；
			//说明不能merge，那么置power=0；
			//cmp_ori是进行比较Search window和Look ahead window内的内容是否相等
			//如果相等，则再调用函数canMerge，用来比较在pattern里面的k个数据，
			//是否与Look ahead window内的数据相等，如果相等则可以merge；
			//如果上述两个条件中有一个不符合，那么就会--k；
			//如果k==1，那么说明这次已经是最后一次比较了，将第一个数据压进pattern中，
			//然后将窗口向前滑动1个距离；
			if(cmp_int(o_tmp1, o_tmp2, k) && (flag = canMerge(top, o_tmp2, p_tmp, power, count,  k)) > 0)
			{
				top = mergePat(top, power, count ,k, flag);
				if(top == NULL)
				{
					TRACE_FUNC();
					return ;
				}
				break;
			}else if(k == 1)
			{
				if(*top != '#')
				{
					TRACE_FUNC();
					return ;
				}
				if(flag == 0 && power == 0)
				{
					*top++ = ',';
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				else if(flag == 0 && power > 1)
				{
					++top;
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				else if(flag == -1 || flag == -2 || flag == -3)
				{
					*top++ = ',';
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}else if(flag == -4 || flag == -5)
				{
					++top;
					if(o_tmp2[0] < 0)
					{
						*top ++ = '-';
						top = int_to_char(-o_tmp2[0], top);
					}
					else
						top = int_to_char(o_tmp2[0], top);
					*top++ = '#';
					*top-- = '\0';
					break;
				}
				break;
			}
			--k;
		}
		//下面这条语句就是讲窗口向前滑动k个距离
		delta_sub += k;
	}
}

int read_from_pattern(char *buf, size_t size, off_t offset)
{
	off_t phy_offset;
	size_t length;
	pid_t id;
	int fd = -1;
	string path;
	if(get_pid_pattern(id, phy_offset, length, offset))
		;

	ChunkFile *cf_ptr = &(chunk_map[id]);
	fd = cf_ptr->fd;
    path = cf_ptr->path;

    if(fd < 0)
    {
    	TRACE_FUNC();
    	return -errno;
    }
	ret = Util::Pread( fd, buf, length, phy_offset );
	return ret;

}

bool get_pid_pattern(pid_t &id, off_t &phy_offset, size_t &length, off_t offset)
{
	std::map<pid_t, pattern_unit>::iterator iter;
	int count = 0, sub_logical = 0, sub_physical = 0, sub_length, sub_end = 0;
	for(iter = global_map_entry.begin(); iter != global_map_entry.end(); ++iter)
	{
		sub_logical = iter->second.logical_offset;
		sub_physical = iter->second.physical_offset;
		count = find_in_pattern_logical(offset, sub_logical, sub_physical);
		if(count != 0)
		{
			sub_length = iter->second.length;
			sub_end = iter->second.end_sub;
			id = iter->first;
			break;
		}
	}
	if(count == 0)
	{
		TRACE_FUNC();
		return 0;
	}
	phy_offset = find_relevant_physical(sub_physical, sub_length, count);
	length = find_relevant_length(sub_physical, sub_end, count);
	if(phy_offset == 0 && length == 0 )
	{
		TRACE_FUNC();
		return 0;
	}
	return 1;
}

bool find_rel_elem(pid_t id, off_t logical_offset, off_t &relevant_physical, size_t &relevant_length)
{
	std::map<pid_t, pattern_unit>::iterator iter;
	iter = global_map_entry.find(id);
	if(iter == global_map_entry.end())
	{
		TRACE_FUNC();
		return 0;
	}

	int sub_logical = 0, sub_physical = 0, sub_length = 0, sub_end = 0;
	sub_logical = iter->second.logical_offset;
	sub_physical = iter->second.physical_offset;
	sub_length = iter->second.length;
	sub_end = iter->second.end_sub;

	int count = 0;
	count = find_in_pattern_logical(logical_offset, sub_logical, sub_physical);

	relevant_physical = find_relevant_physical(sub_physical, sub_length, count);
	relevant_length = find_relevant_length(sub_physical, sub_end, count);

	if(relevant_physical == 0 && relevant_length == 0 )
	{
		TRACE_FUNC();
		return 0;
	}
	return 1;

}

int find_in_pattern_logical(off_t logical_offset, int sub_logical, int sub_physical)
{
	if(sub_logical > sub_physical || sub_physical > global_vector.end() - global_vector.begin() 
		|| sub_logical > global_vector.end() - global_vector.begin())
	{
		TRACE_FUNC();
		return 0;
	}

	int ret_count = 0, count = 0, power = 0, neg = 0, sub = sub_logical;
	off_t cur_off = 0, incremental = 0;
	char *ptr = NULL;

	cur_off = get_initial_off(&global_vector[sub]);
	while(global_vector.at(sub++) != '#')
		;

	while(sub < sub_physical)
	{
		get_power_count(&global_vector[sub], power, count);
		if(power == 0)	power = 1;
		
		while(power -- )
		{
			ptr = &global_vector[sub];
			while(*ptr != '#' && *ptr != '^')
			{
				neg = 0 ;
				if(*ptr == '-')
				{
					++ptr;
					neg = 1;
				}
				incremental = 0;
				while(isdigit(*ptr))
				{
					incremental *= 10;
					incremental += (*ptr++ -'0');
				}
				if(neg)
					cur_off -= incremental;
				else
					cur_off += incremental;
				if(cur_off == logical_offset)
					return ret_count + 1;
				if(cur_off > logical_offset)
					return ret_count;
				++ret_count;
				if(*ptr == '^' || *ptr == '#')
					continue;
				++ptr;
			}
		}

		//this is move the sub to the last # 
		while(global_vector.at(sub++) != '#')
			;
	}
	TRACE_FUNC();
	return 0;
}

off_t find_relevant_physical(int sub_physical, int sub_length, int count)
{
	if(sub_physical > sub_length || sub_length > global_vector.end() - global_vector.begin()
		|| sub_physical > global_vector.end() - global_vector.begin())
	{
		TRACE_FUNC();
		return 0;
	}

	int sub = sub_physical, cur_count = 0, power = 0, neg = 0;
	off_t ret_off = 0, incremental = 0;
	char *ptr = NULL ;

	ret_off = get_initial_off(&global_vector[sub]);
	while(global_vector.at(sub++) != '#')
		;

	while(sub < sub_length)
	{
		power = get_power(&global_vector[sub]);
		if(power == 0)	power = 1;

		while(power -- )
		{
			ptr = &global_vector[sub];
			while(*ptr != '#' && *ptr != '^')
			{
				neg = 0 ;
				if(*ptr == '-')
				{
					++ptr;
					neg = 1;
				}
				incremental = 0;
				while(isdigit(*ptr))
				{
					incremental *= 10;
					incremental += (*ptr++ -'0');
				}
				if(neg)
					ret_off -= incremental;
				else
					ret_off += incremental;
				++cur_count;
				if(cur_count == count)
					return ret_off;
				if(*ptr == '^' || *ptr == '#')
					continue;
				++ptr;
			}
		}
	}
}

size_t find_relevant_length(int sub_length, int sub_end, int count)
{
	if(sub_length > sub_end || sub_end > global_vector.end() - global_vector.begin()
		|| sub_length > global_vector.end() - global_vector.begin())
	{
		TRACE_FUNC();
		return 0;
	}

	int sub = sub_length, cur_count = 0, power = 0, neg = 0;
	size_t ret_len = 0, incremental = 0;
	char *ptr = NULL ;

	ret_len = get_initial_off(&global_vector[sub]);
	while(global_vector.at(sub++) != '#')
		;

	while(sub < sub_length)
	{
		power = get_power(&global_vector[sub]);
		if(power == 0)	power = 1;

		while(power -- )
		{
			ptr = &global_vector[sub];
			while(*ptr != '#' && *ptr != '^')
			{
				neg = 0 ;
				if(*ptr == '-')
				{
					++ptr;
					neg = 1;
				}
				incremental = 0;
				while(isdigit(*ptr))
				{
					incremental *= 10;
					incremental += (*ptr++ -'0');
				}
				if(neg)
					ret_len -= incremental;
				else
					ret_len += incremental;
				++cur_count;
				if(cur_count == count)
					return ret_len;
				if(*ptr == '^' || *ptr == '#')
					continue;
				++ptr;
			}
		}
	}
}

off_t get_initial_off(char *p)
{
	off_t ret = 0;
	while(isdigit(*p))
	{
		ret *= 10;
		ret += *p++ -'0';
	}
	return ret;
}

size_t get_initial_size(char *p)
{
	size_t ret = 0;
	while(isdigit(*p))
	{
		ret *= 10;
		ret += *p++ -'0';
	}
	return ret;
}

void get_power_count(char *p, int &power, int &count)
{
	char *ptr = p;
	power = 0;
	count = 1;
	while(*ptr != '#')
	{
		if(*ptr == ',')
			++count;
		else if(*ptr == '^')
		{
			++ptr;
			while(*ptr != '#')	
			{
				power *=10;
				power += *ptr++ -'0';
			}
			return ;
		}			
		++ptr;
	}
}

char *int_to_char(int power, char *ch)
{
	int i = 1;
	while(power/i > 9)
		i*=10;
	while(power>9)
	{
		*ch++ = power/i + '0';
		power%=i;
		i/=10;
	}
	*ch++ = power+'0';
	return ch;	
}

char *off_to_char(off_t off, char *ch)
{
	int i = 1;
	while(off/i > 9)
		i*=10;
	while(off>9)
	{
		*ch++ = off/i + '0';
		off%=i;
		i/=10;
	}
	*ch++ = off+'0';
	return ch;
}

char *size_to_char(size_t size, char *ch)
{
	int i = 1;
	while(size/i > 9)
		i*=10;
	while(size>9)
	{
		*ch++ = size/i + '0';
		size%=i;
		i/=10;
	}
	*ch++ = size+'0';
	return ch;
}

int char_to_power(char *ptr)
{
	int ret = 0;
	while(*ptr != '#')	
	{
		ret *=10;
		ret += *ptr++ -'0';
	}
	return ret;
}

int char_to_int(char *ch, int n)
{
	int ret = 0;
	while(n--)
	{
		ret *= 10;
		ret += *ch++ -'0';
	}
	return ret;
}

off_t char_to_off(char *ch, int n)
{
	off_t ret = 0;
	while(n--)
	{
		ret *= 10;
		ret += *ch++ -'0';
	}
	return ret;
}

size_t char_to_size(char *ch, int n)
{
	size_t ret = 0;
	while(n--)
	{
		ret *= 10;
		ret += *ch++ -'0';
	}
	return ret;
}

bool cmp_int(int *p1, int *p2, int n)
{
	while(n--)
	{
		if(*p1++ != *p2++)
			return 0;
	}
	return 1;
}

int get_count(char *top)
{
	int count = 1;
	char *ptr = top;
	--ptr;
	while(*ptr != '#')
	{
		if(*ptr == ',')
			++count;
		--ptr;
	}
	return count;
}

int get_power(char *top)
{
	char *ptr = top;
	--ptr;
	int power = 0;
	while(*ptr != '#')
	{
		if(*ptr == '^')
		{
			++ptr;
			power = char_to_power(ptr);
			break;
		}
		--ptr;
	}
	return power;
}

int canMerge(char *top, int *ori, int *pat, int power, int count, int k)
{
	char *ptr = top;
	char *stop_ptr = top;
	--ptr;
	//stop_ptr要么指向两个# #中的最后一个#，要么指向^。
	//ptr最后指向# # 之间的第一个#.
	while(*ptr != '#')
	{
		if(*ptr == '^')
		{
			stop_ptr = ptr;
		}
		--ptr;
	}
	++ptr;		//此时ptr指向两个#1,2,3,4,5#之间的第一个数据，也就是1；

	//NOTE: flag = 1, 3, 4, 5 that can merge;
	//		flag = -1, -2, -3, -4, -5, -6 that can't merge
	//		flag = -1, -2, -3, -4, -5 that can insert an elem to pat when k == 1;
	//		flag = -6 that can't insert an elem to pat because k != 1;
	//		flag = -7 that must occur an error;

	//1. #1,2,3# o_tmp:1,2,3 , power = 0, then get pat and cmp;
	if(power == 0 && count == k)
	{
		get_pat(ptr, stop_ptr, pat, k, 0);
		if(cmp_int(ori, pat, k))
			return 1;
		return -1;
	}

	//2. #1,2,3#; o_tmp:1,2,3,4; k = 4; must can't merge
	if(power == 0 && count < k)
	{
		return -2;
	}

	//3. #1,2,3,4#; o_tmp: 2,3,4; k = 3; then get last k pat and cmp;
	if(power == 0 && count > k)
	{
		get_pat(ptr, stop_ptr, pat, k, count - k);
		if(cmp_int(ori, pat, k))
			return 3;
		return -3;
	}

	//4. #1,2,3,4^2#; power = 2; count = 4; o_tmp:1,2,3,4; k = 4; then can merge to #1,2,3,4^3#;
	if(power > 1 && count == k)
	{
		get_pat(ptr, stop_ptr, pat, k, 0);
		if(cmp_int(ori, pat, k))
			return 4;
		return -4;
	}

	//5. #1,2^2#; power = 2; count = 2; o_tmp = 1,2,1,2; k = 4; 
	//	 then can merge to #1,2^4# but NOT #1,2,3,4^2#;
	if(power > 1 && count < k )
	{
		if(k % count != 0)
			return -5;
		get_pat(ptr, stop_ptr, pat, count, 0);
		if(cmp_int(ori, pat, count))
			return 5;
		return -5;
	}

	//6. count < k, NOTE in this K != 1 that can't merge 
	//   AND also can't insert a elem to pat;
	if(power > 1 && count > k)
		return -6;

	//7. power != 0 && power !> 1
	//   NOTE that must occur an error.
	TRACE_FUNC();
	return -7;
}

// maybe stop_ptr is not needed;
void get_pat(char *ptr, char *stop_ptr, int *pat, int k, int n)
{
	//if n != 0 ,then reset the ptr;
	if(n > 0)
	{
		while(n--)
		{
			while(*ptr++ != ',')
				;
		}
	}

	//then get the pat between ptr and stop_ptr;
	int neg = 0, x = 0;
	char *cur_ptr = ptr;
	while(k)
	{
		neg = 0;
		x = 0;
		while(isdigit(*ptr) || *ptr == '-')
		{
			if(*ptr == '-')
			{
				neg = 1;
				++ptr;
			}else
			{
				++x;
				++ptr;
			}
		}
		++ptr;
		if(neg)
			++cur_ptr;
		*pat++ = char_to_int(cur_ptr, x);
		if(neg)
		{
			*(--pat) = -*pat;
			++pat;
		}
		cur_ptr = ptr;
		--k;
	}
}

int get_p_tmp(char *top, int *pat, int power, int count, int k)
{
	char *ptr = top;
	char *stop_ptr = top;
	--ptr;

	//stop_ptr要么指向两个# #中的最后一个#，要么指向^。
	//ptr最后指向# # 之间的第一个#.
	while(*ptr != '#')
	{
		if(*ptr == '^')
		{
			stop_ptr = ptr;
		}
		--ptr;
	}
	++ptr;		//此时ptr指向两个#12345#之间的第一个数据，也就是1；

	if(power != 1 || count == k)
	{
		int n = 0, neg = 0;
		char *cur_ptr = ptr;
		while(count)
		{
			neg = 0;
			n = 0;
			while(isdigit(*ptr) || *ptr == '-')
			{
				if(*ptr == '-')
				{
					neg = 1;
					++ptr;
				}else
				{
					++n;
					++ptr;
				}
			}
			++ptr;
			if(neg)
				++cur_ptr;
			*pat++ = char_to_int(cur_ptr, n);
			if(neg)
			{
				*(--pat) = -*pat;
				++pat;
			}
			cur_ptr = ptr;
			--count;
		}
		return power;
	}

	//3、这是第三种情况，power == 1 ，j具体情况为 power == 1 && count > k：
	//	 此种情况下，# #里面的数据个数count，比我需要的k个数据要多，
	//	 那么我就需要值获取# # 里面的后k个数据就可以了。
	if(power == 1)
	{
		int n = 0, neg = 0;
		char *cur_ptr = ptr;
		count -= k;

		//这个while的嵌套，是找到后k个数据的起始指针cur_ptr
		while(count--)
		{
			while(*cur_ptr++ != ',')
				;
		}
		if(*(--cur_ptr) != '-')
			++cur_ptr;
		count = k;
		ptr = cur_ptr;
		while(count)
		{
			neg = 0;
			n = 0;
			while(isdigit(*ptr) || *ptr == '-')
			{
				if(*ptr == '-')
				{
					neg = 1;
					++ptr;
				}else
				{
					++n;
					++ptr;
				}
			}
			++ptr;
			if(neg)
				++cur_ptr;
			*pat++ = char_to_int(cur_ptr, n);
			if(neg)
			{
				*(--pat) = -*pat;
				++pat;
			}
			cur_ptr = ptr;
			--count;
		}
		return power;
	}
}

char *mergePat(char *top, int power, int count, int k, int flag)
{
	if(*top != '#')
	{
		TRACE_FUNC();
		return NULL;
	}

	char *ptr = top;

	if(flag == 1)
	{
		power = 2;
		*ptr++ = '^';
		ptr = int_to_char(power, ptr);
		*ptr++ = '#';
		*ptr-- = '\0';
		return ptr;
	}

	if(flag == 4)
	{
		power += 1;
		while(*ptr != '^')
			--ptr;
		++ptr;
		ptr = int_to_char(power, ptr);
		*ptr++ = '#';
		*ptr-- = '\0';
		return ptr;
	}

	if(flag == 5)
	{
		power += k/count;
		while(*ptr != '^')
			--ptr;
		++ptr;
		ptr = int_to_char(power, ptr);
		*ptr++ = '#';
		*ptr-- = '\0';
		return ptr;
	}

	if(flag == 3)
	{
		char *last_ptr = top;
		power = 2;

		while(k--)
		{
			while(*ptr != ',')
				--ptr;
		}
		*ptr = '#';

		*last_ptr++ = '^';
		last_ptr = int_to_char(power, last_ptr);
		*last_ptr++ = '#';
		*last_ptr-- = '\0';
		return last_ptr;
	}

	TRACE_FUNC();
	return NULL;
}

char *mergePat(char *top, int power, int k)
{
	if(*top != '#')
	{
		TRACE_FUNC();
		return NULL;
	}
	char *ptr = top;
	--ptr;

	//1、如果power ！= 1 那么说明我只需要更改power的内容就可以了；
	//2、如果power == 1 ，那么就需要先统计两个# #之间的数据的数量，
	//   然后再细分两种情况：
	//	 2.1、此情况count == k，那么说明两个# # 号之间的所有数据都是可以merge的，
	//		  那么此时只要把# # 中最后一个# 变成 ^ 2（power + 1）就可以了；
	//	 2.2、此情况count > k， 那么说明两个# # 号之间的数据要多余我需要merge的k个数据,
	//		  那么首先需要找到# # 之间的后k个数据的起始地点，比如记此起始地点为*，例如：
	//		  原来数据为#1,2,3,4,5,6,7#，count = 7，k = 4；经过寻找为#1,2,3，(*)4,5,6,7#；
	//		  那么我们吧*号所在地方的，号变成#，将两个pattern之间分隔开来，
	//		  然后再在最后一个#号的位置。变换为 ^ 2(power + 1)；那么最终结果为：
	//		  #1,2,3#4,5,6,7^2#
	if(power != 1)
	{
		while(*ptr != '^')
			--ptr;
		++ptr;
		ptr = int_to_char(power + 1, ptr);
		*ptr++ = '#';
		*ptr-- = '\0';
		return ptr;
	}else
	{
		char *last_ptr = top;
		int count = 1;
		while(*ptr-- != '#')
		{
			if(*ptr == ',')
				++count;
		}
		if(count == k)
		{
			*last_ptr++ = '^';
			last_ptr = int_to_char(power + 1, last_ptr);
			*last_ptr++ = '#';
			*last_ptr-- = '\0';
			return last_ptr;
		}else if(count > k)
		{
			++ptr;
			count -= k;
			while(count--)
			{
				while(*ptr++ != ',')
					;
			}
			*(--ptr) = '#';
			*last_ptr++ = '^';
			last_ptr = int_to_char(power + 1, last_ptr);
			*ptr++ = '#';
			*ptr-- = '\0';
			return ptr;
		}else 
		{
			//power = 1 && count < k 的情况不存在，若存在，则必有错误发生
			//这个 else 删除应该也没问题.
			TRACE_FUNC();
			return top;
		}
	}
}


//以下是测试用的一些FUNCTION
void pp(int *ptr){
	while(*ptr != '\0')
		printf("%5d",*ptr++);
	printf("\n");
}

void pp(char *ptr){
	while(*ptr != '\0')
		printf("%c",*ptr++);
	printf("\n");
}

void pp(std::vector<off_t> v)
{
	std::vector<off_t>::iterator iter = v.begin();
	while(iter != v.end())
	{
		cout<< *iter++ <<"," ;
	}
	cout<<endl;
}

void pp(std::vector<size_t> v)
{
	std::vector<size_t>::iterator iter = v.begin();
	while(iter != v.end())
	{
		cout<< *iter++ <<"," ;
	}
	cout<<endl;
}

void pp(std::vector<char> v)
{
	std::vector<char>::iterator iter = v.begin();
	while(iter != v.end())
	{
		cout<< *iter++ <<"" ;
	}
	cout<<endl;
}

void pp(int begin, int end)
{
	while(begin != end)
	{
		cout << global_vector.at(begin++) ;
	}
	cout << endl;
}

void pp(std::map<pid_t, pattern_unit> global_map_entry)
{
	std::map<pid_t, pattern_unit>::iterator iter;
	for( iter = global_map_entry.begin(); iter != global_map_entry.end(); ++iter)
	{
		cout << "PID = " << iter->first << endl;
		cout << "pattern logical : " ;
		pp(iter->second.logical_offset, iter->second.physical_offset);
		cout << "pattern physical : " ;
		pp(iter->second.physical_offset, iter->second.length);
		cout << "pattern length : " ;
		pp(iter->second.length, iter->second.end_sub);
	}
}

void pp(pid_t id , std::map<pid_t, pattern_unit> global_map_entry)
{
	std::map<pid_t, pattern_unit>::iterator iter;
	iter = global_map_entry.find(id);
	if(iter == global_map_entry.end())
	{
		cout << "Not find the entry about PID = " + id << endl;
	}else
	{
		cout << "PID = " << iter->first << endl;
		cout << "pattern logical : " ;
		pp(iter->second.logical_offset, iter->second.physical_offset);
		cout << "pattern physical : " ;
		pp(iter->second.physical_offset, iter->second.length);
		cout << "pattern length : " ;
		pp(iter->second.length, iter->second.end_sub);
	}
}