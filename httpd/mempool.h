/********************************************************************
    created:    2008/08/03
    filename:   mempool.h    
    author:        Lichuang
                
    purpose:    仿SGI STL内存池的实现
*********************************************************************/

#ifndef __MEM_POOL_H__
#define __MEM_POOL_H__

#include "singleton.h"
#include "threadmutex.h"
#include <stdio.h>

// 字节对齐数
#define ALIGN                512
// 最大BLOCK的尺寸
#define MAX_BLOCK_SIZE        20 * 1024
// HASH表的数量
#define BLOCK_LIST_NUM        (MAX_BLOCK_SIZE) / (ALIGN)
// HASH表中每次初始化时LIST中元素的数量
#define LIST_NODE_NUM        20

class CMemPool
    : CSingleton<CMemPool>
{
public:
    void* Allocate(size_t nSize);
    void* Reallocate(void* p, size_t nOldSize, size_t nNewSize);
    void  Deallocate(void* p, size_t nSize);
    int   GetMemSize();

private:
    CMemPool();
    virtual ~CMemPool();

    char* AllocChunk(size_t nSize, int& nObjs);
    void* Refill(size_t n);
    size_t RoundUp(size_t nBytes);
    int GetFreeListIndex(size_t nBytes);

private:
    DECLARE_SINGLETON_CLASS(CMemPool)

private:        
    union Obj
    {
        union Obj* pFreeListLink;
        char szData[1];
    };

    Obj* m_szFreeList[BLOCK_LIST_NUM];

    char* m_pStartFree;
    char* m_pEndFree;
    size_t m_nHeapSize;
    CThreadMutex m_tThreadMutex;
};

#endif /* __MEM_POOL_H__ */

/********************************************************************
    created:    2008/08/01
    filename:     mempool.h
    author:        Lichuang
                
    purpose:    模拟SGI STL内存池的实现, 可以配置是否支持多线程
*********************************************************************/

#include "mempool.h"
#include <string.h>

CMemPool::CMemPool()
    : m_pStartFree(NULL)
    , m_pEndFree(NULL)
    , m_nHeapSize(0)                      
{
    ::memset(m_szFreeList, 0, sizeof(m_szFreeList));
}

CMemPool::~CMemPool()
{
}

// 从内存池中分配尺寸为n的内存
void* CMemPool::Allocate(size_t nSize)
{
    if (nSize > MAX_BLOCK_SIZE)
    {
        return ::malloc(nSize);
    }
    if (0 >= nSize)
    {
        return NULL;
    }

    Obj** ppFreeList;
    Obj*  pResult;

    THREAD_LOCK;

    // 获得尺寸n的HASH表地址
    ppFreeList = m_szFreeList + GetFreeListIndex(nSize);
    pResult = *ppFreeList;
    if (NULL == pResult)
    {
        // 如果之前没有分配, 或者已经分配完毕了, 就调用refill函数重新分配
        // 需要注意的是, 传入refill的参数是经过对齐处理的
        pResult = (Obj*)Refill(RoundUp(nSize));
    }
    else
    {
        // 否则就更新该HASH表的LIST头节点指向下一个LIST的节点, 当分配完毕时, 头结点为NULL
        *ppFreeList = pResult->pFreeListLink;
    }

    THREAD_UNLOCK;

    return pResult;
}

void* CMemPool::Reallocate(void* p, size_t nOldSize, size_t nNewSize)
{
    void* pResult;
    size_t nCopySize;

    // 如果超过内存池所能承受的最大尺寸, 调用系统API重新分配内存
    if (nOldSize > (size_t)MAX_BLOCK_SIZE && nNewSize > (size_t)MAX_BLOCK_SIZE)
    {
        return ::realloc(p, nNewSize);
    }

    // 如果新老内存尺寸在对齐之后相同, 那么直接返回
    if (RoundUp(nOldSize) == RoundUp(nNewSize))
        return p;

    // 首先按照新的尺寸分配内存
    pResult = Allocate(nNewSize);
    if (NULL == pResult)
    {
        return NULL;
    }
    // copy旧内存的数据到新的内存区域
    nCopySize = nNewSize > nOldSize ? nOldSize : nNewSize;
    ::memcpy(pResult, p, nCopySize);
    // 释放旧内存区域
    Deallocate(p, nOldSize);

    return pResult;
}

// 将尺寸为n的内存回收到内存池中
void CMemPool::Deallocate(void* p, size_t nSize)
{
    Obj* pObj = (Obj *)p;
    Obj **ppFreeList;

    if (0 >= nSize)
    {
        return;
    }
    // 如果要回收的内存大于MAX_BLOCK_SIZE, 直接调用free回收内存
    if (nSize > MAX_BLOCK_SIZE)
    {
        ::free(p);
        return;
    }

    // 将回收的内存作为链表的头回收
    ppFreeList = m_szFreeList + GetFreeListIndex(nSize);

    THREAD_LOCK;

    pObj->pFreeListLink = *ppFreeList;
    *ppFreeList = pObj;

    THREAD_UNLOCK;
}

int CMemPool::GetMemSize()
{
    return m_nHeapSize;
}

size_t CMemPool::RoundUp(size_t nBytes)
{
    return (nBytes + ALIGN - 1) & ~(ALIGN - 1);
}

int CMemPool::GetFreeListIndex(size_t nBytes)
{
    return (nBytes + ALIGN - 1) / ALIGN - 1;
}


char* CMemPool::AllocChunk(size_t nSize, int& nObjs)
{
    char* pResult;
    // 总共所需的内存
    size_t nTotalBytes = nSize * nObjs;
    // 剩余的内存
    size_t nBytesLeft = m_pEndFree - m_pStartFree;

    // 如果剩余的内存可以满足需求, 就直接返回之, 并且更新内存池的指针
    if (nBytesLeft >= nTotalBytes)
    {
        pResult = m_pStartFree;
        m_pStartFree += nTotalBytes;
        return pResult;
    }

    // 如果剩余的内存大于单位内存数量, 也就是说至少还可以分配一个单位内存
    // 计算出最多可以分配多少块单位内存, 保存至nobjs, 返回内存的指针
    if (nBytesLeft >= nSize)
    {
        nObjs = (int)(nBytesLeft / nSize);
        nTotalBytes = nSize * nObjs;
        pResult = m_pStartFree;
        m_pStartFree += nTotalBytes;
        return pResult;
    }

    // 如果还有剩余的内存, 将它放到对应的HASH-LIST头部
    if (0 < nBytesLeft)
    {
        Obj** ppFreeList = m_szFreeList + GetFreeListIndex(nBytesLeft);
        ((Obj*)m_pStartFree)->pFreeListLink = *ppFreeList;
        *ppFreeList = (Obj*)m_pStartFree;
    }

    // 需要获取的内存, 注意第一次分配都要两倍于total_bytes的大小
    // 同时要加上原有的heap_size / 4的对齐值
    size_t nBytesToGet = 2 * nTotalBytes + RoundUp(m_nHeapSize >> 4);
    m_pStartFree = (char*)::malloc(nBytesToGet);

    // 获取成功 重新调用chunk_alloc函数分配内存
    if (NULL != m_pStartFree)
    {
        m_nHeapSize += nBytesToGet;
        m_pEndFree = m_pStartFree + nBytesToGet;
        return AllocChunk(nSize, nObjs);
    }

    // 下面是获取不成功的处理.

    // 从下一个HASH-LIST中寻找可用的内存
    int i = (int)GetFreeListIndex(nSize) + 1;
    Obj **ppFreeList, *p;
    for (; i < BLOCK_LIST_NUM; ++i)
    {
        ppFreeList = m_szFreeList + i;
        p = *ppFreeList;

        if (NULL != p)
        {
            *ppFreeList = p->pFreeListLink;
            m_pStartFree = (char*)p;
            m_pEndFree = m_pStartFree + (i + 1) * ALIGN;
            return AllocChunk(nSize, nObjs);
        }
    }

    m_pEndFree = NULL;

    return NULL;
}

// 重新分配尺寸为n的内存, 其中n是经过字节对齐处理的数
void* CMemPool::Refill(size_t n)
{
    // 每个链表每次初始化时最多LIST_NODE_NUM个元素
    int nObjs = LIST_NODE_NUM;
    char* pChunk = AllocChunk(n, nObjs);
    Obj** ppFreeList;
    Obj* pResult;
    Obj *pCurrentObj, *pNextObj;
    int i;

    // 如果只请求成功了一个元素, 直接返回之
    if (1 == nObjs)
    {
        return pChunk;
    }
    // 获得尺寸n的HASH表地址
    ppFreeList = m_szFreeList + GetFreeListIndex(n);

    // 获得请求的内存地址
    pResult = (Obj*)pChunk;
    // 请求了一个单位内存, 减少一个计数
    --nObjs;
    // 从下一个单位开始将剩余的obj连接起来
    *ppFreeList = pNextObj = (Obj*)(pChunk + n);

    // 将剩余的obj连接起来
    for (i = 1; ; ++i)
    {
        pCurrentObj = pNextObj;
        pNextObj = (Obj*)((char*)pNextObj + n);

        // 分配完毕, 下一个节点为NULL, 退出循环
        if (nObjs == i)
        {
            pCurrentObj->pFreeListLink = NULL;
            break;
        }

        pCurrentObj->pFreeListLink = pNextObj;
    }

    return pResult;
}