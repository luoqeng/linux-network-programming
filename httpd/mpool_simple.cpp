
#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H

#include <stdio.h>
#include <assert.h>

using namespace std;

class MemoryPool 
{
private:
  
    // Really we should use static const int x = N
    // instead of enum { x = N }, but few compilers accept the former.
    enum {__ALIGN = 8};                            //小型区块的上调边界，即小型内存块每次上调8byte
    enum {__MAX_BYTES = 128};                    //小型区块的上界
    enum {__NFREELISTS = __MAX_BYTES/__ALIGN};    //free-lists的个数，为:16，每个free-list管理不同大小内存块的配置

  //将请求的内存大小上调整为8byte的倍数，比如8byte, 16byte, 24byte, 32byte
  static size_t ROUND_UP(size_t bytes)
  {
        return (((bytes) + __ALIGN-1) & ~(__ALIGN - 1));
  }

  union obj 
  {
      union obj* free_list_link;        //下一个区块的内存地址，如果为NULL，则表示无可用区块
      char client_data[1];                //内存区块的起始地址          
  };
  
private:
    static obj *free_list[__NFREELISTS];    // __NFREELISTS = 16
    /*
        free_list[0] --------> 8 byte（free_list[0]管理8bye区块的配置）
        free_list[1] --------> 16 byte
        free_list[2] --------> 24 byte
        free_list[3] --------> 32 byte
        ... ...
        free_list[15] -------> 128 byte
    */

  //根据区块大小，决定使用第n号的free_list。n = [0, 15]开始
  static  size_t FREELIST_INDEX(size_t bytes) 
  {
        return (((bytes) + __ALIGN-1)/__ALIGN - 1);
  }

  // Returns an object of size n, and optionally adds to size n free list.
  static void *refill(size_t n);
  
  // 配置一大块空间，可容纳nobjs个大小为size的区块
  // 如果配置nobjs个区块有所不便，nobjs可能会降低
  static char *chunk_alloc(size_t size, int &nobjs);

  // Chunk allocation state.
  static char *start_free;        //内存池起始位置
  static char *end_free;        //内存池结束位置
  static size_t heap_size;        //内存池的大小

public:

  // 公开接口，内存分配函数     
    static void* allocate(size_t n)
    {
        obj** my_free_list = NULL;
        obj* result = NULL;

        //如果待分配的内存字节数大于128byte,就调用C标准库函数malloc
        if (n > (size_t) __MAX_BYTES) 
        {
            return malloc(n);
        }

        //调整my_free_lisyt，从这里取用户请求的区块
        my_free_list = free_list + FREELIST_INDEX(n);
    

        result = *my_free_list;        //欲返回给客户端的区块

        if (result == 0)    //没有区块了
        {
            void *r = refill(ROUND_UP(n));

            return r;
        }
    
        *my_free_list = result->free_list_link;        //调整链表指针，使其指向下一个有效区块
    
        return result;
    };


    //归还区块
    static void deallocate(void *p, size_t n)
    {
        assert(p != NULL);

        obj* q = (obj *)p;
        obj** my_free_list = NULL;

        //大于128byte就调用第一级内存配置器
        if (n > (size_t) __MAX_BYTES) 
        {
            free(p) ;
        }

        // 寻找对应的free_list
        my_free_list = free_list + FREELIST_INDEX(n);
    
        // 调整free_lis，回收内存
        q -> free_list_link = *my_free_list;
        *my_free_list = q;
  }

  static void * reallocate(void *p, size_t old_sz, size_t new_sz);

} ;


/* We allocate memory in large chunks in order to avoid fragmenting     */
/* the malloc heap too much.                                            */
/* We assume that size is properly aligned.                             */
/* We hold the allocation lock.                                         */

// 假设size已经上调至8的倍数
// 注意nobjs是passed by reference,是输入输出参数
char* MemoryPool::chunk_alloc(size_t size, int& nobjs)
{
    char* result = NULL;    
    
    size_t total_bytes = size * nobjs;                //请求分配内存块的总大小
    size_t bytes_left = end_free - start_free;        //内存池剩余空间的大小

    if (bytes_left >= total_bytes)     //内存池剩余空间满足要求量
    {
        result = start_free;
        start_free += total_bytes;
        
        return result;
    } 
    else if (bytes_left >= size)         //内存池剩余空间不能完全满足需求量，但足够供应一个(含)以上的区块
    {
        nobjs = bytes_left/size;        //计算内存池剩余空间足够配置的区块数目
        total_bytes = size * nobjs;
        
        result = start_free;
        start_free += total_bytes;
        
        return result;
    } 
    else         //内存池剩余空间连一个区块都无法提供
    {
        //bytes_to_get为内存池向malloc请求的内存总量
        size_t bytes_to_get = 2 * total_bytes + ROUND_UP(heap_size >> 4);
        
        // Try to make use of the left-over piece.
        if (bytes_left > 0) 
        {
            obj** my_free_list = free_list + FREELIST_INDEX(bytes_left);

            ((obj *)start_free) -> free_list_link = *my_free_list;
            *my_free_list = (obj *)start_free;
        }

        // 调用malloc分配堆空间，用于补充内存池
        start_free = (char *)malloc(bytes_to_get);
        if (0 == start_free)     //heap空间已满，malloc分配失败
        {
            int i;
            obj ** my_free_list, *p;

            //遍历free_list数组，试图通过释放区块达到内存配置需求
            for (i = size; i <= __MAX_BYTES; i += __ALIGN) 
            {
                my_free_list = free_list + FREELIST_INDEX(i);
                p = *my_free_list;
                
                if (0 != p) 
                {
                    *my_free_list = p -> free_list_link;
                    start_free = (char *)p;
                    end_free = start_free + i;
                    
                    return chunk_alloc(size, nobjs);
                    // Any leftover piece will eventually make it to the
                    // right free list.
                }
            }

            end_free = 0;    // In case of exception.

            // 调用第一级内存配置器，看看out-of-memory机制能否尽点力
            
            // This should either throw an
            // exception or remedy the situation.  Thus we assume it
            // succeeded.
        }
        
        heap_size += bytes_to_get;
        end_free = start_free + bytes_to_get;
        
        return chunk_alloc(size, nobjs);
    }
    
}


/* Returns an object of size n, and optionally adds to size n free list.*/
/* We assume that n is properly aligned.                                */
/* We hold the allocation lock.                                         */
void* MemoryPool::refill(size_t n)
{
    int nobjs = 20;

    // 注意nobjs是输入输出参数，passed by reference。
    char* chunk = chunk_alloc(n, nobjs);
    
    obj* * my_free_list = NULL;
    obj* result = NULL;
    obj* current_obj = NULL;
    obj* next_obj = NULL;
    int i;

    // 如果chunk_alloc只获得了一个区块，这个区块就直接返回给调用者，free_list无新结点
    if (1 == nobjs) 
    {
        return chunk;
    }

    // 调整free_list，纳入新结点
    my_free_list = free_list + FREELIST_INDEX(n);

    result = (obj*)chunk;    //这一块返回给调用者(客户端)


    //用chunk_alloc分配而来的大量区块配置对应大小之free_list  
    *my_free_list = next_obj = (obj *)(chunk + n);
      
    for (i = 1; ; i++) 
    {
        current_obj = next_obj;
        next_obj = (obj *)((char *)next_obj + n);
        
        if (nobjs - 1 == i) 
        {
            current_obj -> free_list_link = NULL;
            break;
        } 
        else 
        {
            current_obj -> free_list_link = next_obj;
        }
    }
      
    return result;
}

//重新配置内存，p指向原有的区块，old_sz为原有区块的大小，new_sz为新区块的大小
void* MemoryPool::reallocate(void *p, size_t old_sz, size_t new_sz)
{
    void* result = NULL;
    size_t copy_sz = 0;

    if (old_sz > (size_t) __MAX_BYTES && new_sz > (size_t) __MAX_BYTES) 
    {
        return realloc(p, new_sz);
    }

    if (ROUND_UP(old_sz) == ROUND_UP(new_sz)) 
    {
        return p;
    }

    result = allocate(new_sz);
    copy_sz = new_sz > old_sz? old_sz : new_sz;

    memcpy(result, p, copy_sz);

    deallocate(p, old_sz);

    return result;
}

//静态成员变量初始化
char* MemoryPool::start_free = 0;

char* MemoryPool::end_free = 0;

size_t MemoryPool::heap_size = 0;    

MemoryPool::obj* MemoryPool::free_list[MemoryPool::__NFREELISTS] 
                        = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

#endif