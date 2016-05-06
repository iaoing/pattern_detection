##### The procedure of plfs_open,  plfs_close, plfs_write, plfs_read
```
- plfs_open

- pfls_close

- plfs_open

- plfs_write and call plfs_addWrite at WriteFile.cpp: 241

- plfs_close

- plfs_open

- plfs_read
```

---



1. In **WriteFile.cpp** lines 35, function **WriteFile::~WriteFile()** that calls **closeIndex()** which is in **WriteFile.cpp** lines 279 and it will calls **index->flush(**) which will flushes index to disk.

   - I think that can put the pattern detection function in **index->flush** 
   - But it seems not as well as good, maybe should not do this.

2. In **Index::addWrite()** , that will add the offset and length and pid to entry.

   - This must be the point to call **pattern detection**. 

3. In **plfs.cpp** function **perform_read_task()**, call **Util::Pread(fd, buf, length, offset)**
   - So this should be the point to call the function of pattern.

4. In **Index.h** that has:

   ```c++
   vector< ChunkFile > chunk_map;
   typedef struct
   {
     string path;  //the path is the full file path
     int fd;
   } ChunkFile;
   ```

   - So we can get path through **chunk_map**, we can see the code at function **Index::chunkFound** in **Index.cpp**.

   ```c++
     ChunkFile *cf_ptr = &(chunk_map[entry->id]);
     *fd = cf_ptr->fd;	//this is the file fd
     path = cf_ptr->path;	//this is the full file path
   ```

   - So the most important thing is to get the **pid** ! 

   - **NOTE! NOTE! NOTE! NOTE!**

     Chun_map is private class in Index.h, so can't use it in plfs.cpp or other function!

     **So** , how can we to get the **fd** and **path** !

5. ​

---

#### Mode code#

- **pattern detection**

  ```c++
   + Index.cpp: lines 26
     #include "Pattern.h"
   + Index.cpp: lines 1173
     add_elem(c_entry.id, c_entry.logical_offset, c_entry.physical_offset, c_entry.length);
   + plfs.cpp: lines 1888 - 1892
     if(o_logical.size() != 0)
      {
          if(pattern_init(pid_t o_id))
              ;
      }
  ```

- **find read off and read** 

  **NOTE!!!!!!     THE FOLLOW CODE IS WRROR!** 

  ```markdown
  ~~\+ plfs.cpp: lines 9~~

  ~~\#include "Pattern.h"~~

  ~~\- plfs.cpp: lines 854 - 858~~

  ~~\+ plfs.cpp: lines 860~~

  ~~ret = read_from_pattern(*buf, size, offset);~~
  ```

- **So we can that to read use pattern** 

  ```
  - plfs.cpp: lines 659 - 650
  + plfs.cpp: lines 652
    ret = read_from_pattern(task->fd, task->buf, task->chunk_id, task->logical_offset);
  ```

  ​