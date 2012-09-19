/*
   COLAFileSystem
   
   Adrian Duraj
   
   Simple filesystem based on COLA data structure, using FUSE
   
   Directory listing, file and directory creation
   
   Max path length: 30 characters
   
   Compilation: "gcc -lfuse -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=25 cfs.c -o cfs"
   
   Sample run: "./cfs ~/data /tmp/fuse"
   First parameter: the file containing the filesystem, second one: mount dir
   
   Edit:
   
   added FAT, reading from and writing to files, max file size - more than one block
*/

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <regex.h>
#include <asm/page.h>

#define pageSize PAGE_SIZE
#define block PAGE_SIZE
#define numOfFiles 32767 //2^k - 1
#define colaSize numOfFiles * sizeof(struct fileInfo)
#define FATSize numOfFiles * sizeof(int)
#define dataSize numOfFiles * block
#define fileSize (colaSize + FATSize + dataSize + 2*block)
#define fileNameLen 30
#define EMPTY -1
#define DIR -2
#define END -3
#define OFFC (colaSize - (colaSize % pageSize) + pageSize)
#define OFFF (OFFC + FATSize - (FATSize % pageSize) + pageSize)

/*
   Other functions
*/
int match(const char *string, const char *pattern) //regex 
{
   int status = 0;
   regex_t re;
   if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) != 0) 
   {
      return 0; /* report error */
   }
   status = regexec(&re, string, (size_t) 0, NULL, 0);
   regfree(&re);
   if (status != 0) 
   {
      return 0; /* report error */
   }
   return 1;
}

int checkPath(const char *path)
{
   if (strlen(path + 1) > fileNameLen || path[1] == '.' || match(path, ".*~.*"))
      return 1;
   else
      return 0;
}

int power(int a, int n)
{
   int temp = 0;
   if (n==0)
      return 1;
   else
   {
      temp = power(a, n / 2);
      if (n % 2 == 0)
         return temp * temp;
      else
         return a * temp * temp;
   }
}

/* 
   COLA implementation
   
   blockNum = -1 (EMPTY) -> empty tab
   heads[i] = -1 (EMPTY) -> empty tab
*/

struct fileInfo
{
   int size;
   char name[fileNameLen + 1];
   int blockNum;
};

static struct fileInfo *COLA;

struct fileInfo findMin(struct fileInfo *cola, int *heads, struct fileInfo fi, int *fiInserted, int numOfTabs)
//find minimal element for merging
{
   struct fileInfo min;
   int minIndex = EMPTY;
   int i = 0;
   
   for (i = 0; i < numOfTabs; i++)
   {
      if (heads[i] != EMPTY)
      {
         minIndex = i;
         break;
      }
   }
      
   if (minIndex == EMPTY) //only fi is left
      return fi;
   
   min = cola[heads[minIndex]];
   
   for (i = minIndex + 1; i < numOfTabs; i++)
   {
      if (heads[i] != EMPTY)
         if (strcmp(min.name, cola[heads[i]].name) >= 0)
         {
            min = cola[heads[i]];
            minIndex = i;
         }   
   }
   
   if (*fiInserted)
   {
      if ((heads[minIndex] + 2) == power(2, minIndex + 1))
      {
         heads[minIndex] = EMPTY;
         for (i = power(2, minIndex) - 1; i < 2 * power(2, minIndex) - 1; i++)
            cola[i].blockNum = EMPTY;      
      }
      else
         heads[minIndex]++;
            
      return min;
   }
   else
      if (strcmp (fi.name, min.name) <= 0)
      {
         *fiInserted = 1;
         return fi;
      }
      else
      {
         if ((heads[minIndex] + 2) == power(2, minIndex + 1))
         {
            heads[minIndex] = EMPTY;
            for (i = power(2, minIndex) - 1; i < 2 * power(2, minIndex) - 1; i++)
               cola[i].blockNum = EMPTY;         
         }
         else
            heads[minIndex]++;
            
         return min;
      }
}

void merge(int i, int numOfTabs, struct fileInfo fi, struct fileInfo *cola) 
//merge arrays
{
   int *heads;
   int j = 0;
   int *fiInserted;
   struct fileInfo min;
   
   heads = (int*)malloc(numOfTabs * sizeof(int));
   fiInserted = (int*)malloc(sizeof(int));
   *fiInserted = 0;
   
   for (j = 0; j < numOfTabs; j++)
      heads[j] = power(2,j) - 1;
   
   int done = 0;
   
   while (done <= i)
   {  
      min = findMin(cola, heads,fi,fiInserted, numOfTabs); 
      cola[i + done] = min;
      done++; 
   }
   free(heads);
   free(fiInserted);
    
   return;
}

int insert(struct fileInfo fi, struct fileInfo *cola)
//insert new element into cola
{
   int i = 0;
   int numOfTabs = 0;
   while (cola[i].blockNum != EMPTY)
   {
      i = 2 * i + 1;
      if ((2 * i + 1) > numOfFiles)
      {
         return 0;      
      }
      numOfTabs++;
   }
   
   if (i == 0)
      cola[0] = fi;
   else
      merge(i,numOfTabs,fi,cola);
      
   return 1;
}

int binSearch(const char* name, struct fileInfo *cola, int f, int l)
{
   int first = f;
   int last = l;
   int mid = 0;
   struct fileInfo temp;
   
   while (first <= last)
   {
      mid = (first + last) / 2;
      temp = cola[mid];
      
      if (strcmp(name, temp.name) == 0)
         return mid;
         
      if (strcmp(name, temp.name) < 0)
         last = mid - 1;
      else
         first = mid + 1; 
   }
   
   return EMPTY;
}

int find(const char* name, struct fileInfo *cola)
//find element's position in cola
{
   int i = 0;
   int cur = 0;
   
   while (2 * i + 1 <= numOfFiles)
   {
      if (cola[i].blockNum != EMPTY)
      {
         cur = binSearch(name, cola, i, 2 * i);
         if (cur != EMPTY)
            return cur;
      }
      i = 2 * i + 1;       
   }
   
   return EMPTY;
}

/*
   End of COLA implementation
*/


/*
   FileSystem
*/

static struct fileInfo *COLA;
static int *FAT;
static char *data;
static char *cfsFile;

static int searchFAT(int *FAT)
{
   int i = 0;
   for (i = 0; i <= numOfFiles; i++)
   {
      if (FAT[i] == EMPTY)
         return i;
   }
   return EMPTY;
}

static int cfs_getattr(const char *path, struct stat *stbuf)
{
   if (checkPath(path))
      return -EACCES;
      
   memset(stbuf, 0, sizeof(struct stat));
   int fd = 0;
   int ret = 0;
   int pos = 0;
   
   if(strcmp(path,"/") == 0) 
   {
      stbuf->st_mode = S_IFDIR | 0777;
      stbuf->st_nlink = 2;
   }
   else
   {
      fd = open(cfsFile, O_RDONLY);
      COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
      
      pos = find(path + 1, COLA);
      
      if (pos != EMPTY)
      {         
         if (COLA[pos].blockNum != DIR)
         {
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_nlink = 1;
            stbuf->st_size = COLA[pos].size;
         }
         else
         {
            stbuf->st_mode = S_IFDIR | 0777;
            stbuf->st_nlink = 2;
         }
         ret = 0;
      }
      else
         ret = -ENOENT;
         
      munmap(COLA,colaSize);
      close(fd);
   }
   
   return ret;
}

static int cfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{  
   if (checkPath(path))
      return -EACCES;
      
   (void) offset;
   (void) fi;
   int fd = 0;
   int i = 0;
   
   if (strlen(path + 1) > fileNameLen || path[1] == '.')
      return -ENOENT;
   
   filler(buf, ".", NULL, 0);
   filler(buf, "..", NULL, 0);
   
   char str[fileNameLen + 2] = "";
   strcpy(str, path + 1);
   strcat(str, "/");
   
   fd = open(cfsFile, O_RDONLY);
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
   for (i = 0; i < (colaSize / sizeof(struct fileInfo)); i++)
   {          
      if (COLA[i].blockNum != EMPTY)
      {
         if (strcmp(path, "/") == 0)
         {
            if (!match(COLA[i].name, ".+/"))
               filler(buf,COLA[i].name,NULL,0);
         }
         else 
         {
            if (match(COLA[i].name, str))
               if (!match(COLA[i].name + strlen(path), ".+/"))
                  filler(buf,COLA[i].name + strlen(path),NULL,0);
         }           
      }
   }   
   
   munmap(COLA,colaSize);
   close(fd);
   
   return 0;
}

static int cfs_mknod(const char *path, mode_t mode, dev_t rdev)
{   
   if (checkPath(path))
      return -EACCES;
      
   struct fileInfo fi;
   
   int fd = open(cfsFile, O_RDWR);
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
   
   if (find(path + 1,COLA) != EMPTY)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -EEXIST;
   }
   
   munmap(COLA,colaSize);
   
   //
   strcpy((char*)&fi.name,path + 1);
   fi.size = 0;   
   FAT = (int*)mmap(0, FATSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OFFC);
   fi.blockNum = searchFAT(FAT);
   
   if (fi.blockNum == EMPTY)
   {
      munmap(FAT,FATSize);
      close (fd);
      return -ENOMEM;
   }
   
   FAT[fi.blockNum] = END;
   munmap(FAT,FATSize);
   //
   
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (insert(fi,COLA) == 0)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -ENOSPC;
   }
   
   munmap(COLA,colaSize);
   close(fd);
   return 0;
}

static int cfs_utime(const char *path, struct utimbuf *time)
{
   if (checkPath(path))
      return -EACCES;
      
   return 0;
}

static int cfs_mkdir(const char *path, mode_t mode)
{
   if (checkPath(path))
      return -EACCES;
      
   struct fileInfo fi;
      
   strcpy((char*)&fi.name,path + 1);
   fi.size = 0;
   fi.blockNum = DIR;
   
   int fd = open(cfsFile, O_RDWR);
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   
   if (find(fi.name,COLA) != EMPTY)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -EEXIST;
   }
      
   if (insert(fi,COLA) == 0)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -ENOSPC;
   }
   
   munmap(COLA,colaSize);
   close(fd);
   return 0;
}

static int cfs_open(const char *path, struct fuse_file_info *fi)
{
   if (checkPath(path))
      return -EACCES;
      
   int fd = open(cfsFile, O_RDONLY);
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
   
   if (find(path+1,COLA) == EMPTY)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -ENOENT;
   }
   
   munmap(COLA,colaSize);
   close(fd);
   return 0;
   
}


static int cfs_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
   if (checkPath(path))
      return -EACCES;
      
   int index = 0, blockNum = 0, tempOff = 0, left = 0, written = 0;
   
   int fd = open(cfsFile, O_RDWR);
   
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
   index = find(path+1,COLA);
      
   if (index == EMPTY)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -ENOENT;
   }
   
   blockNum = COLA[index].blockNum;
   munmap(COLA,colaSize);   
   
   tempOff = offset;
   
   if (tempOff > block)
   {
      FAT = (int*)mmap(0, FATSize, PROT_READ, MAP_SHARED, fd, OFFC);
      
      while (tempOff > block)
      {
         blockNum = FAT[blockNum];
         tempOff = tempOff - block;
      }      
      munmap(FAT,FATSize);
   }   
   
   if (tempOff + size > block)
   {
      data = (char*)mmap(0,block,PROT_READ | PROT_WRITE,MAP_SHARED,fd,OFFF + blockNum*block);      
      memcpy(data + tempOff, buf, block - tempOff);      
      munmap(data,block);
      
      written = block - tempOff;
      
      left = size - (block - tempOff);
      
      FAT = (int*)mmap(0, FATSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OFFC);
      
      //if (FAT[blockNum] == EMPTY)
      FAT[blockNum] = searchFAT(FAT);
      
      if (FAT[blockNum] == EMPTY)
      {
         FAT[blockNum] = END;
         munmap(FAT,FATSize);
         return -ENOMEM;
      }
      
      blockNum = FAT[blockNum];
      munmap(FAT,FATSize);
      
      while (left > 0)
      {
         if (left > block)
         {
            data = (char*)mmap(0,block,PROT_READ | PROT_WRITE,MAP_SHARED,fd,OFFF + blockNum*block);      
            memcpy(data, buf + written, block);      
            munmap(data,block);
            
            FAT = (int*)mmap(0, FATSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OFFC);
            
            //if (FAT[blockNum] == EMPTY)
            FAT[blockNum] = searchFAT(FAT);
            
            if (FAT[blockNum] == EMPTY)
            {
               FAT[blockNum] = END;
               munmap(FAT,FATSize);
               return -ENOMEM;
            }
            
            blockNum = FAT[blockNum];
            munmap(FAT,FATSize);
            
            left = left - block;
            written += block;
         }
         else
         {
            data = (char*)mmap(0,block,PROT_READ | PROT_WRITE,MAP_SHARED,fd,OFFF + blockNum*block);      
            memcpy(data, buf + written, left);      
            munmap(data,block);
            
            FAT = (int*)mmap(0, FATSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OFFC);
            
            int cur = FAT[blockNum];
            int temp = 0;
            
            while (FAT[cur] != EMPTY && FAT[cur] != END)
            {
               temp = FAT[cur];
               FAT[cur] = EMPTY;
               cur = temp;
            }
            FAT[cur] = EMPTY;   
            
            FAT[blockNum] = END;
            munmap(FAT,FATSize);
            
            written += left;
            left = 0;
         }
      }
      
   }
   else
   {
      data = (char*)mmap(0,block,PROT_READ | PROT_WRITE,MAP_SHARED,fd,OFFF + blockNum*block);      
      memcpy(data + tempOff, buf, size);      
      munmap(data,block);
      
      written += size;
   }
   
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   COLA[index].size = written + offset;
   munmap(COLA,colaSize);
   
   close(fd);
   return size;
}

static int cfs_truncate(const char* path, off_t size)
{
   if (checkPath(path))
      return -EACCES;
   
   int index = 0;
   
   int fd = open(cfsFile, O_RDWR);
   
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   index = find(path+1,COLA);
   COLA[index].size = size;
   
   munmap(COLA,colaSize);
   close(fd);
   
   return 0;
}

static int cfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
   if (checkPath(path))
      return -EACCES;
   
   int index = 0,tempOff = 0, i = 0, blockNum = 0, read = 0, left = 0;     
           
   int fd = open(cfsFile, O_RDONLY);
   COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ, MAP_SHARED, fd, 0);
   
   index = find(path+1,COLA);   
   if (index == EMPTY)
   {
      munmap(COLA,colaSize);
      close(fd);
      return -ENOENT;
   }
   
   if (offset >= COLA[index].size)
   {      
      munmap(COLA,colaSize);
      close(fd);
      return 0;
   }
   
   if (size + offset > COLA[index].size)
   {
      size = COLA[index].size - offset;  
   }
   
   blockNum = COLA[index].blockNum;
   
   munmap(COLA,colaSize);
   
   tempOff = offset;   
   if (tempOff >= block)
   {
      FAT = (int*)mmap(0, FATSize, PROT_READ, MAP_SHARED, fd, OFFC);
      
      while (tempOff >= block)
      {
         blockNum = FAT[blockNum];
         tempOff = tempOff - block;
      }      
      munmap(FAT,FATSize);
   }      
   
   if (size + tempOff > block)
   {
      data = (char*)mmap(0,block,PROT_READ, MAP_SHARED, fd, OFFF + blockNum*block);      
      memcpy(buf, data + tempOff, block - tempOff);      
      munmap(data,block);
      
      read = block - tempOff;      
      left = size - (block - tempOff);
      
      FAT = (int*)mmap(0, FATSize, PROT_READ, MAP_SHARED, fd, OFFC);
      blockNum = FAT[blockNum];      
      munmap(FAT,FATSize);
      
      while (left > 0)
      {
         if (left > block)
         {
            data = (char*)mmap(0,block,PROT_READ, MAP_SHARED, fd, OFFF + blockNum*block);      
            memcpy(buf + read, data, block);      
            munmap(data,block);
            
            FAT = (int*)mmap(0, FATSize, PROT_READ, MAP_SHARED, fd, OFFC);
            blockNum = FAT[blockNum];            
            munmap(FAT,FATSize);
            
            left = left - block;
            read += block;
         }
         else
         {
            data = (char*)mmap(0,block,PROT_READ,MAP_SHARED,fd,OFFF + blockNum*block);      
            memcpy(buf + read, data, left);      
            munmap(data,block);
            
            left = 0;
         }
      }
      
   }
   else
   {
      data = (char*)mmap(0,block,PROT_READ, MAP_SHARED,fd,OFFF + blockNum*block);      
      memcpy(buf, data + tempOff, size);      
      munmap(data,block);
   }
         
   close(fd); 
   return size;
}

/*
   End of FileSystem
*/

static struct fuse_operations oper;

int main(int argc, char **argv)
{  
   cfsFile = argv[1];

   oper.getattr = cfs_getattr;
   oper.readdir = cfs_readdir;
   oper.utime = cfs_utime;
   oper.mknod = cfs_mknod;
   oper.mkdir = cfs_mkdir;
   oper.open = cfs_open;
   oper.read = cfs_read;
   oper.write = cfs_write;
   oper.truncate = cfs_truncate;
 
   int i;   
   int fd;
   FILE *check;
   
   struct fileInfo fi;
   
   check = fopen(cfsFile, "r");
     
   if (!check)      
   {   
      fd = open(cfsFile, O_RDWR | O_CREAT, (mode_t)0600);
      
      lseek(fd, fileSize-1, SEEK_SET);
      write(fd, "", 1);
      
      COLA = (struct fileInfo*)mmap(0, colaSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      
      fi.blockNum = EMPTY;
      fi.name[0] = '\0';
      fi.size = 0;
      
      for (i = 0; i < numOfFiles; i++)
         COLA[i] = fi;
         
      munmap(COLA,colaSize);
      
      FAT = (int*)mmap(0, FATSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, OFFC);
      for (i = 0; i < numOfFiles; i++)
         FAT[i] = EMPTY;
      munmap(FAT,FATSize);
      
      close(fd);
   }
   else
      fclose(check);
       
   return fuse_main(argc - 1, argv + 1, &oper);
}

