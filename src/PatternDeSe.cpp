#include "PatternDeSe.h"

using namespace std;

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

int 
PatternListDeSe::dese_map_look_up(off_t logical, PatternForQuery *pfq)
{
	pfq->exist = 0;

	map<off_t, PatternUnitDeSe>::iterator iter;
	iter = dese_map.lower_bound(logical);

	if(iter == dese_map.begin() && logical != iter->second.logical_offset)
	{	
		cout << "iter == dese_map.begin() && logical != iter->second.logical_offset"
			 << endl;
		return 0;
	}

	// PatternUnitDeSe pds_unit;
	/* if == means we can find logical by *iter, otherwhile we should find it to *pre */
	if(logical == iter->second.logical_offset)
	{
		return iter->second.look_up(logical, pfq);
	}else{
		/////////////////for debug//////////////////
		cout << "--iter" << endl;
		////////////////////////////////////////////
		--iter;
		return iter->second.look_up(logical, pfq);
	}
	return 0;

}

int 
PatternUnitDeSe::look_up(off_t logical, PatternForQuery *pfq)
{
	// when analyze_unit, dese_unit is destroyed, so don't use it anymore;
	if(queryList.empty())
	{
		/////////////////for debug//////////////////
		cout << "not had anazlyed! " << endl;
		////////////////////////////////////////////
		analyze_unit();
	}

	int i = 0;
	for(i = 0; i < queryList.size(); ++i)
	{
		/////////////////for debug//////////////////
		// cout << "queryList[i].logical_offset: " << queryList[i].logical_offset << endl;
		////////////////////////////////////////////
		if(queryList[i].logical_offset >= logical)
		{
			if(queryList[i].logical_offset == logical)
			{
				*pfq = queryList[i];
				pfq->exist = 1;
				return 0;
			}
			if(queryList[i].logical_offset > logical)
			{
				--i;
				/////////////////for debug//////////////////
				// cout << "queryList[i].logical_offset > logical"
				// 	 << "\nqueryList[i].logical_offset: "
				// 	 << queryList[i].logical_offset
				// 	 << endl;
				////////////////////////////////////////////
				if(i >= 0 && queryList[i].logical_offset + queryList[i].length > logical
						  && queryList[i].logical_offset <= logical )
				{
					// off_t shift = queryList[i].logical_offset + length - logical;
					// pfq->id = queryList[i].id;
					// pfq->logical_offset = logical;
					// pfq->physical_offset = queryList[i].physical_offset + shift;
					// pfq->length = queryList[i].length + (size_t)shift;
					*pfq = queryList[i];
					pfq->exist = 1;
					return 0;
				}
				pfq->exist = 0;
				return 0;
			}
		}
	}

	--i;
	if(i >= 0 && queryList[i].logical_offset + queryList[i].length > logical
						  && queryList[i].logical_offset <= logical )
	{
		*pfq = queryList[i];
		pfq->exist = 1;
		return 0;
	}

	pfq->exist = 0;
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
		queryList[i].exist = 1;
	}
	/////////////////for debug//////////////////
	// cout << "dese_unit analyzed!" << endl;
	////////////////////////////////////////////

	//when analyzed the unit, delete the dese_unit;
	dese_unit.~string();
}




void
PatternListDeSe::Pattern_DeSe(void *addr, size_t len)
{
	string buf;
    buf.resize(len);
    memcpy(&buf[0], addr, len);
    /////////////////for debug//////////////////
	// cout << "************Pattern_DeSe" << "\t"
	// 	 << "size: " << buf.size() << endl;
	// cout << buf << endl;
	////////////////////////////////////////////

    size_t position = 0, totlesize = 0;

    memcpy(&totlesize, &buf[position], sizeof(totlesize));
    position += sizeof(totlesize);

    assert(len == (totlesize + sizeof(totlesize)));
    while(position < len)
    {
        size_t entry_size = 0;
        string entry_ibuf;

        memcpy(&entry_size, &buf[position], sizeof(entry_size));
        position += sizeof(entry_size);

        entry_ibuf.resize(entry_size);
        memcpy(&entry_ibuf[0], &buf[position], entry_size);
        assert(entry_ibuf.size() == entry_size);
        Pattern_DeSe_Entry(&entry_ibuf);

        position += entry_size;
    }

}

void
PatternListDeSe::Pattern_DeSe_Entry(string *ibuf)
{
	/////////////////for debug//////////////////
	// cout << "-> Pattern_DeSe_entry" << "\t"
	// 	 << "size: " << ibuf->size() << endl;
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
        assert(pds_unit.dese_unit.size() == unit_size);

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
	if(queryList.empty)
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