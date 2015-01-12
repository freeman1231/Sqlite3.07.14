/*
** 2008 November 05
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file implements the default page cache implementation (the
** sqlite3_pcache interface). It also contains part of the implementation
** of the SQLITE_CONFIG_PAGECACHE and sqlite3_release_memory() features.
** If the default page cache implementation is overriden, then neither of
** these two features are available.
**该文件实现默认页面缓存的实现(sqlite3_pcache接口)。
**它还包含部分SQLITE_CONFIG_PAGECACHE和sqlite3_release_memory()功能的实现.
**如果默认的页面缓存实现被复写，这时上述的两个功能都不可使用。
*/

#include "sqliteInt.h"

typedef struct PCache1 PCache1;
typedef struct PgHdr1 PgHdr1;
typedef struct PgFreeslot PgFreeslot;
typedef struct PGroup PGroup;

/* Each page cache (or PCache) belongs to a PGroup.  A PGroup is a set 
** of one or more PCaches that are able to recycle each others unpinned
** pages when they are under memory pressure.  A PGroup is an instance of
** the following object.
** 每个页面缓存(PAGE CACHE)属于一个PGroup. 
** PGroup是一组一个或多个的cache组,在低于主存可承受范围时
** 回收彼此非钉住页面
** 一个组是下面对象的一个实例
**
** This page cache implementation works in one of two modes:
** 这个页面缓存实现了以下两种工作模式
**
**   (1)  Every PCache is the sole member of its own PGroup.  There is
**        one PGroup per PCache.
**  	(1) 每个PCache 是它本身PGroup的唯一成员。每个PCache有一个PGroup
**
**   (2)  There is a single global PGroup that all PCaches are a member
**        of.
**	(2) 所有的PCache都是一个全局PGroup的成员
**
** Mode 1 uses more memory (since PCache instances are not able to rob
** unused pages from other PCaches) but it also operates without a mutex,
** and is therefore often faster.  Mode 2 requires a mutex in order to be
** threadsafe, but recycles pages more efficiently.
** 模式1使用更多的内存(因为PCache实例不能从其他PCache强制获取
** 未使用的页面)
** 但它运行不需要一个互斥锁，因此通常更快。
** 模式2为了threadsafe(线程安全)需要互斥锁,但是回收页面效率更高。
**
** For mode (1), PGroup.mutex is NULL.  For mode (2) there is only a single
** PGroup which is the pcache1.grp global variable and its mutex is
** SQLITE_MUTEX_STATIC_LRU.
** 对于模式(1)，PGroup.mutex值为空。对于模式(2)只有一个PGroup即pcache1.grp全局变量
** 和它的排它锁SQLITE_MUTEX_STATIC_LRU.
*/
struct PGroup {
  sqlite3_mutex *mutex;          /* MUTEX_STATIC_LRU or NULL 	*/
  unsigned int nMaxPage;         /* Sum of nMax for purgeable caches 	可净化缓存的nMax总额*/
  unsigned int nMinPage;         /* Sum of nMin for purgeable caches		可净化缓存的nMin总额*/
  unsigned int mxPinned;         /* nMaxpage + 10 - nMinPage 	*/
  unsigned int nCurrentPage;     /* Number of purgeable pages allocated 	已分配可净化页面的序号*/
  PgHdr1 *pLruHead, *pLruTail;   /* LRU list of unpinned pages 	可插拔页面的LRU列表*/
};

/* Each page cache is an instance of the following object.  Every
** open database file (including each in-memory database and each
** temporary or transient database) has a single page cache which
** is an instance of this object.
** 每个页面缓存都是以下对象的一个实例。
** 每个打开的数据库文件(包括每个内存数据库和每个临时
** 或瞬态数据库)都有一个单独的页面缓存，这个页面缓存
** 是这个对象的一个实例
**
** Pointers to structures of this type are cast and returned as 
** opaque sqlite3_pcache* handles.
** 这种类型的指针结构被计算并且作为不透明的sqlite3_pcache* 句柄被返回
*/
struct PCache1 {
  /* Cache configuration parameters. Page size (szPage) and the purgeable
  ** flag (bPurgeable) are set when the cache is created. nMax may be 
  ** modified at any time by a call to the pcache1Cachesize() method.
  ** The PGroup mutex must be held when accessing nMax.
  **缓存配置参数。页面大小和可净化(bPurgeable)在本结构创建时
  **设置。nMax可能在任何时候被一个pcache1Cachesize() 方法的调用
  **修改。当访问nMax时，PGroup互斥访问必须被执行。
  */
  PGroup *pGroup;                     /* PGroup this cache belongs to   这个缓存的归属的PGroup*/
  int szPage;                         /* Size of allocated pages in bytes 分配页的大小(字节数)*/
  int szExtra;                        /* Size of extra space in bytes 额外空间的大小*/
  int bPurgeable;                     /* True if cache is purgeable 真如果缓存是可净化的*/
  unsigned int nMin;                  /* Minimum number of pages reserved  最小页面数量*/
  unsigned int nMax;                  /* Configured "cache_size" value 配置缓冲区大小*/
  unsigned int n90pct;                /* nMax*9/10  */
  unsigned int iMaxKey;               /* Largest key seen since xTruncate()，从上一次xTruncate()之后的最大键值 */

  /* Hash table of all pages. The following variables may only be accessed
  ** when the accessor is holding the PGroup mutex.
  **所有页面的Hash表，以下变量只有 当the PGroup mutex 访问时才能被访问
  */
  unsigned int nRecyclable;           /* Number of pages in the LRU list LRU链表中总的页面数*/
  unsigned int nPage;                 /* Total number of pages in apHash apHash中总的页面数*/
  unsigned int nHash;                 /* Number of slots in apHash[]   apHash[]的槽位数*/
  PgHdr1 **apHash;                    /* Hash table for fast lookup by keyHash  hash表，用keyhash值快速查找 */
};

 /*
  **  本结构代表一个缓冲区，这个缓冲区有多个页，这些页存储在Hash表apHash中。
  **  从定义看，apHash是PgHdr1结构指针的 指针，实际编程时，apHash是PgHdr1结构的指针数组
  **  可见，apHash是一个采用链表法解决地址冲突的Hash表.apHash有nHash个槽位，
  **  每个槽位上是一个链表.Hash函数很简单:PgHdr1->iKey%nHash.
  */

/*
** Each cache entry is represented by an instance of the following 
** structure. Unless SQLITE_PCACHE_SEPARATE_HEADER is defined, a buffer of
** PgHdr1.pCache->szPage bytes is allocated directly before this structure 
** in memory.
** 每个缓冲区入口由此结构的一个实例
** SQLITE_PCACHE_SEPARATE_HEADER 已经被定义，否则在这种结构之前，
** PgHdr1.pCache->szPage被按字节数直接分配在内存中
*/
struct PgHdr1 {
  sqlite3_pcache_page page;      /*缓存页面*/
  unsigned int iKey;             /* Key value (page number) 	关键值(页面的序号)*/
  PgHdr1 *pNext;                 /* Next in hash table chain 哈希表的next 指针*/
  PCache1 *pCache;               /* Cache that currently owns this page 	目前拥有这个页面的缓存*/
  PgHdr1 *pLruNext;              /* Next in LRU list of unpinned pages 	如果是未钉住的页，指向LRU链表中的后一个 */
  PgHdr1 *pLruPrev;              /* Previous in LRU list of unpinned pages 如果是未钉住的页，指向LRU链表中的前一个*/
};

/*
** Free slots in the allocator used to divide up the buffer provided using
** the SQLITE_CONFIG_PAGECACHE mechanism.
** 空闲槽分配器，用于分割缓冲区提供可使用的SQLITE_CONFIG_PAGECACHE机制
*/
struct PgFreeslot {
  PgFreeslot *pNext;  /* Next free slot 	下一个空闲插槽*/
};

/*
** Global data used by this cache.
** cache所使用的全局数据
*/
static SQLITE_WSD struct PCacheGlobal {
  PGroup grp;                    /* The global PGroup for mode (2)  模式(2)的全局PGroup*/

  /* Variables related to SQLITE_CONFIG_PAGECACHE settings.  The
  ** szSlot, nSlot, pStart, pEnd, nReserve, and isInit values are all
  ** fixed at sqlite3_initialize() time and do not require mutex protection.
  ** The nFreeSlot and pFree values do require mutex protection.
  ** 与SQLITE_CONFIG_PAGECACHE相关的变量的设置
  ** szSlot, nSlot, pStart, pEnd, nReserve, and isInit的值都被固定在sqlite3_initialize() 的生命期中，
  ** 并且不需要排它锁保护.nFreeSlot 和pFree的值，需要排它锁保护 
  */
  int isInit;                    /* True if initialized	如果已经初始化值为真 */
  int szSlot;                    /* Size of each free slot  每个空闲插槽的大小*/
  int nSlot;                     /* The number of pcache slots	pcache插槽的序号 */
  int nReserve;                  /* Try to keep nFreeSlot above this	 尽量保持以上的nFreeSlot*/
  void *pStart, *pEnd;           /* Bounds of pagecache malloc range    pagecache内存分配范围的界限*/
  /* Above requires no mutex.  Use mutex below for variable that follow. 
   ** 以上调用不需要排它锁.以下变量的使用需要排它锁
   */
  sqlite3_mutex *mutex;          /* Mutex for accessing the following: 		用排它锁访问以下*/
  PgFreeslot *pFree;             /* Free page blocks 	空闲页面块*/
  int nFreeSlot;                 /* Number of unused pcache slots 	没被使用的pcache插槽序号*/
  /* The following value requires a mutex to change.  We skip the mutex on
  ** reading because (1) most platforms read a 32-bit integer atomically and
  ** (2) even if an incorrect value is read, no great harm is done since this
  ** is really just an optimization. 
  ** 修改以下值需要排它锁。
  ** 我们在读取时跳过排它锁，因为:(1)大多数平台自动读取一个32位的整数；
  ** (2) 即使读取了一个脏数据也不会造成很大的损害，因为这样做只是为了优化
  */
  int bUnderPressure;            /* True if low on PAGECACHE memory  	如果存储值低于PAGECACHE时为真*/
} pcache1_g;

/*
** All code in this file should access the global structure above via the
** alias "pcache1". This ensures that the WSD emulation is used when
** compiling for systems that do not support real WSD.
** 这个文件中的所有代码都应该通过以上渠道(用pcache1别名)去访问全局结构
** 这将确保系统编译时，WSD仿真可用，不支持真正的WSD。
*/
#define pcache1 (GLOBAL(struct PCacheGlobal, pcache1_g))

/*
** Macros to enter and leave the PCache LRU mutex.
**　定义宏去进入或离开这个PCache LRU的排它锁
*/
#define pcache1EnterMutex(X) sqlite3_mutex_enter((X)->mutex)
#define pcache1LeaveMutex(X) sqlite3_mutex_leave((X)->mutex)

/******************************************************************************/
/******** Page Allocation/SQLITE_CONFIG_PCACHE Related Functions **************/
/***********页面配置/ SQLITE_CONFIG_PCACHE相关功能************/


/*
** This function is called during initialization if a static buffer is 
** supplied to use for the page-cache by passing the SQLITE_CONFIG_PAGECACHE
** verb to sqlite3_config(). Parameter pBuf points to an allocation large
** enough to contain 'n' buffers of 'sz' bytes each.
** 这个函数将被调用，当一个静态缓冲区被提供给这个page-cache
**( 通过传递SQLITE_CONFIG_PAGECACHE 动作给 sqlite3_config()这种方法)
** 参数pBuf指针指向一个足够大的分配空间，包含n个sz大小的buffer
**
** This routine is called from sqlite3_initialize() and so it is guaranteed
** to be serialized already.  There is no need for further mutexing.
** 从sqlite3_initialize()调用这个例程,所以它已保证已序列化。
** 没有必要进一步使用排它锁。
*/
void sqlite3PCacheBufferSetup(void *pBuf, int sz, int n){
  if( pcache1.isInit ){
    PgFreeslot *p;
    sz = ROUNDDOWN8(sz);
    pcache1.szSlot = sz;
    pcache1.nSlot = pcache1.nFreeSlot = n;
    pcache1.nReserve = n>90 ? 10 : (n/10 + 1);
    pcache1.pStart = pBuf;
    pcache1.pFree = 0;
    pcache1.bUnderPressure = 0;
    while( n-- ){ 
      p = (PgFreeslot*)pBuf;
      p->pNext = pcache1.pFree;
      pcache1.pFree = p;
      pBuf = (void*)&((char*)pBuf)[sz];
    }
    pcache1.pEnd = pBuf;
  }
}

/*
** Malloc function used within this file to allocate space from the buffer
** configured using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no 
** such buffer exists or there is no space left in it, this function falls 
** back to sqlite3Malloc().
** 内存分配函数被运用到这个文件中，通过sqlite3_config(SQLITE_CONFIG_PAGECACH)选项
** 从缓冲区分配空间配置。如果没有这个缓存区或者缓存区中没有足够的
** 空间剩余，这个函数的返回执行sqlite3Malloc().
**
** Multiple threads can run this routine at the same time.  Global variables
** in pcache1 need to be protected via mutex.
** 多个线程可以同时运行这个程序。在pcache1中的全局变量
** 通过排它锁被保护。
*/
static void *pcache1Alloc(int nByte){
  void *p = 0;
  assert( sqlite3_mutex_notheld(pcache1.grp.mutex) );
  sqlite3StatusSet(SQLITE_STATUS_PAGECACHE_SIZE, nByte);
  if( nByte<=pcache1.szSlot ){//如果插槽大小比邋nByte小
    sqlite3_mutex_enter(pcache1.mutex);//获得排它锁
    p = (PgHdr1 *)pcache1.pFree;//获取全部变量中的空闲页面块
    if( p ){//如果存在空闲块
      pcache1.pFree = pcache1.pFree->pNext;
      pcache1.nFreeSlot--;//空闲插槽数减1
      pcache1.bUnderPressure = pcache1.nFreeSlot<pcache1.nReserve;//看一下需保留的最小数是
                                                                  //否大于空闲数
      assert( pcache1.nFreeSlot>=0 );
      sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, 1);
    }
    sqlite3_mutex_leave(pcache1.mutex);
  }
  if( p==0 ){
    /* Memory is not available in the SQLITE_CONFIG_PAGECACHE pool.  Get
    ** it from sqlite3Malloc instead.
    ** 在SQLITE_CONFIG_PAGECACHE池中，主存是不可用的
    ** 通过使用sqlite3Malloc 代替重新分配空间!
    */
    p = sqlite3Malloc(nByte);
#ifndef SQLITE_DISABLE_PAGECACHE_OVERFLOW_STATS
    if( p ){
      int sz = sqlite3MallocSize(p);
      sqlite3_mutex_enter(pcache1.mutex);
      sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, sz);
      sqlite3_mutex_leave(pcache1.mutex);
    }
#endif
    sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
  }
  return p;
}

/*
** Free an allocated buffer obtained from phe1Alloc().
** 通过 phe1Alloc()释放或者分配缓冲区
*/
static int pcache1Free(void *p){
  int nFreed = 0;
  if( p==0 ) return 0;
  if( p>=pcache1.pStart && p<pcache1.pEnd ){//如果p是在分配的 内存范围内
    PgFreeslot *pSlot;
    sqlite3_mutex_enter(pcache1.mutex);
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_USED, -1);
    pSlot = (PgFreeslot*)p;
    pSlot->pNext = pcache1.pFree;
    pcache1.pFree = pSlot;
    pcache1.nFreeSlot++;
    pcache1.bUnderPressure = pcache1.nFreeSlot<pcache1.nReserve;
    assert( pcache1.nFreeSlot<=pcache1.nSlot );
    sqlite3_mutex_leave(pcache1.mutex);
  }else{//如果不在这个范围内
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    nFreed = sqlite3MallocSize(p);//重新分配
#ifndef SQLITE_DISABLE_PAGECACHE_OVERFLOW_STATS
    sqlite3_mutex_enter(pcache1.mutex);
    sqlite3StatusAdd(SQLITE_STATUS_PAGECACHE_OVERFLOW, -nFreed);
    sqlite3_mutex_leave(pcache1.mutex);
#endif
    sqlite3_free(p);
  }
  return nFreed;//
}

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
** Return the size of a pcache allocation
** 返回分配的pcache大小
*/
static int pcache1MemSize(void *p){
  if( p>=pcache1.pStart && p<pcache1.pEnd ){
    return pcache1.szSlot;
  }else{
    int iSize;
    assert( sqlite3MemdebugHasType(p, MEMTYPE_PCACHE) );
    sqlite3MemdebugSetType(p, MEMTYPE_HEAP);
    iSize = sqlite3MallocSize(p);
    sqlite3MemdebugSetType(p, MEMTYPE_PCACHE);
    return iSize;
  }
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

/*
** Allocate a new page object initially associated with cache pCache.
** 分配新的页面对象给最初关联的cache pCache
*/
static PgHdr1 *pcache1AllocPage(PCache1 *pCache){
  PgHdr1 *p = 0;
  void *pPg;

  /* The group mutex must be released before pcache1Alloc() is called. This
  ** is because it may call sqlite3_release_memory(), which assumes that 
  ** this mutex is not held.
  ** 在 pcache1Alloc() 函数被调用前必须先释放一组排它锁
  ** 因为它可能会调用sqlite3_release_memory()，而这是假设
  ** 它没持有排它锁的(前提下进行的)
  */
  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  pcache1LeaveMutex(pCache->pGroup);
#ifdef SQLITE_PCACHE_SEPARATE_HEADER
  pPg = pcache1Alloc(pCache->szPage);
  p = sqlite3Malloc(sizeof(PgHdr1) + pCache->szExtra);
  if( !pPg || !p ){
    pcache1Free(pPg);
    sqlite3_free(p);
    pPg = 0;
  }
#else
  pPg = pcache1Alloc(sizeof(PgHdr1) + pCache->szPage + pCache->szExtra);
  p = (PgHdr1 *)&((u8 *)pPg)[pCache->szPage];
#endif
  pcache1EnterMutex(pCache->pGroup);

  if( pPg ){
    p->page.pBuf = pPg;
    p->page.pExtra = &p[1];
    if( pCache->bPurgeable ){
      pCache->pGroup->nCurrentPage++;
    }
    return p;
  }
  return 0;
}

/*
** Free a page object allocated by pcache1AllocPage().
** 释放一个由pcache1AllocPage()分配的page 对象
**
** The pointer is allowed to be NULL, which is prudent.  But it turns out
** that the current implementation happens to never call this routine
** with a NULL pointer, so we mark the NULL test with ALWAYS().
** 这个指针可以为空，这样做是很节省(资源的)
** 但事实证明,从未发生过用一个空指针调用这个程序，
** 所以我们用ALWAYS()去进行空测试
*/
static void pcache1FreePage(PgHdr1 *p){
  if( ALWAYS(p) ){
    PCache1 *pCache = p->pCache;
    assert( sqlite3_mutex_held(p->pCache->pGroup->mutex) );
    pcache1Free(p->page.pBuf);
#ifdef SQLITE_PCACHE_SEPARATE_HEADER
    sqlite3_free(p);
#endif
    if( pCache->bPurgeable ){
      pCache->pGroup->nCurrentPage--;
    }
  }
}

/*
** Malloc function used by SQLite to obtain space from the buffer configured
** using sqlite3_config(SQLITE_CONFIG_PAGECACHE) option. If no such buffer
** exists, this function falls back to sqlite3Malloc().
** SQLite使用Malloc函数，通过sqlite3_config(SQLITE_CONFIG_PAGECACHE)选项从缓冲区
** 获取空间配置.如果不存在这样的缓冲区，这个函数返回执行sqlite3Malloc().
*/
void *sqlite3PageMalloc(int sz){
  return pcache1Alloc(sz);
}

/*
** Free an allocated buffer obtained from sqlite3PageMalloc().
** 用qlite3PageMalloc()获得自由分配缓冲区
*/
void sqlite3PageFree(void *p){
  pcache1Free(p);
}


/*
** Return true if it desirable to avoid allocating a new page cache
** entry.
** 返回true,如果它能避免分配一个新的页面缓存条目
** 
** If memory was allocated specifically to the page cache using
** SQLITE_CONFIG_PAGECACHE but that memory has all been used, then
** it is desirable to avoid allocating a new page cache entry because
** presumably SQLITE_CONFIG_PAGECACHE was suppose to be sufficient
** for all page cache needs and we should not need to spill the
** allocation onto the heap.
** 如果主存(被用 SQLITE_CONFIG_PAGECACHE )专门分配给页面缓存，
** 但是这部分已经被使用完，那么它可以避免分配一
** 个新的页面缓存条目,因为可假定SQLITE_CONFIG_PAGECACHE对所有页面缓存的
** 需求是足够的,我们应该不需要在堆上溢出分配。
** 
** Or, the heap is used for all page cache memory but the heap is
** under memory pressure, then again it is desirable to avoid
** allocating a new page cache entry in order to avoid stressing
** the heap even further.
** 或者这个堆已经被用于所有的页面缓存内存，
** 但这个堆仍在内存可承受范围内，然后它又可用于避免分配一个
** 新的页面缓存条目，以避免堆的压力进一步增大
*/
static int pcache1UnderMemoryPressure(PCache1 *pCache){
  if( pcache1.nSlot && (pCache->szPage+pCache->szExtra)<=pcache1.szSlot ){
    return pcache1.bUnderPressure;
  }else{
    return sqlite3HeapNearlyFull();
  }
}

/******************************************************************************/
/*********** General Implementation Functions  一般实现功能******************/

/*
** This function is used to resize the hash table used by the cache passed
** as the first argument.
** 这个函数被用于调整hash表的大小(被cache作为第一个参数传递)。
**
** The PCache mutex must be held when this function is called.
** 当这个函数被调用时，这个PCache 排它锁必须被执行
*/
static int pcache1ResizeHash(PCache1 *p){
  PgHdr1 **apNew;
  unsigned int nNew;
  unsigned int i;

  assert( sqlite3_mutex_held(p->pGroup->mutex) );

  nNew = p->nHash*2;
  if( nNew<256 ){
    nNew = 256;
  }

  pcache1LeaveMutex(p->pGroup);
  if( p->nHash ){ sqlite3BeginBenignMalloc(); }
  apNew = (PgHdr1 **)sqlite3MallocZero(sizeof(PgHdr1 *)*nNew);
  if( p->nHash ){ sqlite3EndBenignMalloc(); }
  pcache1EnterMutex(p->pGroup);
  if( apNew ){
    for(i=0; i<p->nHash; i++){
      PgHdr1 *pPage;
      PgHdr1 *pNext = p->apHash[i];
      while( (pPage = pNext)!=0 ){
        unsigned int h = pPage->iKey % nNew;
        pNext = pPage->pNext;
        pPage->pNext = apNew[h];
        apNew[h] = pPage;
      }
    }
    sqlite3_free(p->apHash);
    p->apHash = apNew;
    p->nHash = nNew;
  }

  return (p->apHash ? SQLITE_OK : SQLITE_NOMEM);
}

/*
** This function is used internally to remove the page pPage from the 
** PGroup LRU list, if is part of it. If pPage is not part of the PGroup
** LRU list, then this function is a no-op.
** 这个函数是在内部使用于从pPage PGroup LRU列表删除pPage页
** 如果 pPage 是它(PGroup LRU list,)的一部分。如果pPage不是 PGroup LRU列表
** 的一部分,那么这个函数是一个空操作。
**
** The PGroup mutex must be held when this function is called.
** 这个函数被调用时这个 PGroup 排它锁必须被执行
**
** If pPage is NULL then this routine is a no-op.
** 如果pPage 是空，那么这个程序是个空操作
*/
static void pcache1PinPage(PgHdr1 *pPage){
  PCache1 *pCache;
  PGroup *pGroup;

  if( pPage==0 ) return;
  pCache = pPage->pCache;
  pGroup = pCache->pGroup;
  assert( sqlite3_mutex_held(pGroup->mutex) );
  if( pPage->pLruNext || pPage==pGroup->pLruTail ){
    if( pPage->pLruPrev ){
      pPage->pLruPrev->pLruNext = pPage->pLruNext;
    }
    if( pPage->pLruNext ){
      pPage->pLruNext->pLruPrev = pPage->pLruPrev;
    }
    if( pGroup->pLruHead==pPage ){
      pGroup->pLruHead = pPage->pLruNext;
    }
    if( pGroup->pLruTail==pPage ){
      pGroup->pLruTail = pPage->pLruPrev;
    }
    pPage->pLruNext = 0;
    pPage->pLruPrev = 0;
    pPage->pCache->nRecyclable--;
  }
}


/*
** Remove the page supplied as an argument from the hash table 
** (PCache1.apHash structure) that it is currently stored in.
**  删除当前被当做参数存储在hash表 (PCache1.apHash 结构)中的页面
** 
** The PGroup mutex must be held when this function is called.
** 当这个函数被调用时，PGroup排它锁必须已经被执行
*/
static void pcache1RemoveFromHash(PgHdr1 *pPage){
  unsigned int h;
  PCache1 *pCache = pPage->pCache;
  PgHdr1 **pp;

  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  h = pPage->iKey % pCache->nHash;
  for(pp=&pCache->apHash[h]; (*pp)!=pPage; pp=&(*pp)->pNext);
  *pp = (*pp)->pNext;

  pCache->nPage--;
}

/*
** If there are currently more than nMaxPage pages allocated, try
** to recycle pages to reduce the number allocated to nMaxPage.
** 如果目前有超过nMaxPage(页面最大数)页面被分配，
** 尽量回收页面来降低已分配nMaxPage的值
*/
static void pcache1EnforceMaxPage(PGroup *pGroup){
  assert( sqlite3_mutex_held(pGroup->mutex) );
  while( pGroup->nCurrentPage>pGroup->nMaxPage && pGroup->pLruTail ){
    PgHdr1 *p = pGroup->pLruTail;
    assert( p->pCache->pGroup==pGroup );
    pcache1PinPage(p);
    pcache1RemoveFromHash(p);
    pcache1FreePage(p);
  }
}

/*
** Discard all pages from cache pCache with a page number (key value) 
** greater than or equal to iLimit. Any pinned pages that meet this 
** criteria are unpinned before they are discarded.
** 
** 丢弃所有缓存pCache中(键值)页码大于或等于iLimit的所有页面。
** 任何符合这个标准的固定页面都是先拔掉后丢弃。
** The PCache mutex must be held when this function is called.
** 在这个函数被调用前PCache排它锁必须已经被执行
*/
static void pcache1TruncateUnsafe(
  PCache1 *pCache,             /* The cache to truncate 　缓存截断*/
  unsigned int iLimit          /* Drop pages with this pgno or larger 		用pgno或者larger 	删除页面*/
){
  TESTONLY( unsigned int nPage = 0; )  /* To assert pCache->nPage is correct */
  unsigned int h;
  assert( sqlite3_mutex_held(pCache->pGroup->mutex) );
  for(h=0; h<pCache->nHash; h++){
    PgHdr1 **pp = &pCache->apHash[h]; 
    PgHdr1 *pPage;
    while( (pPage = *pp)!=0 ){
      if( pPage->iKey>=iLimit ){
        pCache->nPage--;
        *pp = pPage->pNext;
        pcache1PinPage(pPage);
        pcache1FreePage(pPage);
      }else{
        pp = &pPage->pNext;
        TESTONLY( nPage++; )
      }
    }
  }
  assert( pCache->nPage==nPage );
}

/******************************************************************************/
/******** sqlite3_pcache Methods isqlite3_pcache方法*************/

/*
** Implementation of the sqlite3_pcache.xInit method.
**　 sqlite3_pcache.xInit方法的实现　　
*/
static int pcache1Init(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  assert( pcache1.isInit==0 );
  memset(&pcache1, 0, sizeof(pcache1));
  if( sqlite3GlobalConfig.bCoreMutex ){
    pcache1.grp.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_LRU);
    pcache1.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_PMEM);
  }
  pcache1.grp.mxPinned = 10;
  pcache1.isInit = 1;
  return SQLITE_OK;
}

/*
** Implementation of the sqlite3_pcache.xShutdown method.
** Note that the static mutex allocated in xInit does 
** not need to be freed.
**
**　sqlite3_pcache.xShutdown　方法的实现
**　注意，分配在xInit中的静态排它锁不需被释放
*/
static void pcache1Shutdown(void *NotUsed){
  UNUSED_PARAMETER(NotUsed);
  assert( pcache1.isInit!=0 );
  memset(&pcache1, 0, sizeof(pcache1));
}

/*
** Implementation of the sqlite3_pcache.xCreate method.
**　sqlite3_pcache.xCreate方法的实现
**
** Allocate a new cache.
**　分配一个新的ｃａｃｈｅ
*/
static sqlite3_pcache *pcache1Create(int szPage, int szExtra, int bPurgeable){
  PCache1 *pCache;      /* The newly created page cache 	新创建的页面ｃａｃｈｅ*/
  PGroup *pGroup;       /* The group the new page cache will belong to 	 	新建cache将归属的group*/
  int sz;               /* Bytes of memory required to allocate the new cache 	新建cache所需要的内存字节数*/

  /*
  ** The seperateCache variable is true if each PCache has its own private
  ** PGroup.  In other words, separateCache is true for mode (1) where no
  ** mutexing is required.
  ** 如果每个PCache都有自己的私有PGroup，seperateCache变量为真。
  ** 换句话说，separateCache对mode(1)是可用的，并且不需排它锁。
  **
  **   *  Always use a unified cache (mode-2) if ENABLE_MEMORY_MANAGEMENT
  **	总是使用一个统一的缓存(模式2)如果ENABLE_MEMORY_MANAGEMENT（为真)
  **
  **   *  Always use a unified cache in single-threaded applications
  **  在单线程应用程序中总是使用一个统一的缓存
  **
  **   *  Otherwise (if multi-threaded and ENABLE_MEMORY_MANAGEMENT is off)
  **      use separate caches (mode-1)
  ** 否则(如果多线程和ENABLE_MEMORY_MANAGEMENT是关闭的)使用单独的缓存(模式1)
  */
#if defined(SQLITE_ENABLE_MEMORY_MANAGEMENT) || SQLITE_THREADSAFE==0
  const int separateCache = 0;
#else
  int separateCache = sqlite3GlobalConfig.bCoreMutex>0;
#endif

  assert( (szPage & (szPage-1))==0 && szPage>=512 && szPage<=65536 );
  assert( szExtra < 300 );

  sz = sizeof(PCache1) + sizeof(PGroup)*separateCache;
  pCache = (PCache1 *)sqlite3MallocZero(sz);
  if( pCache ){
    if( separateCache ){
      pGroup = (PGroup*)&pCache[1];
      pGroup->mxPinned = 10;
    }else{
      pGroup = &pcache1.grp;
    }
    pCache->pGroup = pGroup;
    pCache->szPage = szPage;
    pCache->szExtra = szExtra;
    pCache->bPurgeable = (bPurgeable ? 1 : 0);
    if( bPurgeable ){
      pCache->nMin = 10;
      pcache1EnterMutex(pGroup);
      pGroup->nMinPage += pCache->nMin;
      pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
      pcache1LeaveMutex(pGroup);
    }
  }
  return (sqlite3_pcache *)pCache;
}

/*
** Implementation of the sqlite3_pcache.xCachesize method. 
** sqlite3_pcache.xCachesize方法的实现方法
**
** Configure the cache_size limit for a cache.
** 为一个缓存配置cache_size限制。
*/
static void pcache1Cachesize(sqlite3_pcache *p, int nMax){
  PCache1 *pCache = (PCache1 *)p;
  if( pCache->bPurgeable ){
    PGroup *pGroup = pCache->pGroup;
    pcache1EnterMutex(pGroup);
    pGroup->nMaxPage += (nMax - pCache->nMax);
    pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
    pCache->nMax = nMax;
    pCache->n90pct = pCache->nMax*9/10;
    pcache1EnforceMaxPage(pGroup);
    pcache1LeaveMutex(pGroup);
  }
}

/*
** Implementation of the sqlite3_pcache.xShrink method. 
** sqlite3_pcache.xShrink方法的实现
**
** Free up as much memory as possible.
** 尽可能释放更多的内存
*/
static void pcache1Shrink(sqlite3_pcache *p){
  PCache1 *pCache = (PCache1*)p;
  if( pCache->bPurgeable ){
    PGroup *pGroup = pCache->pGroup;
    int savedMaxPage;
    pcache1EnterMutex(pGroup);
    savedMaxPage = pGroup->nMaxPage;
    pGroup->nMaxPage = 0;
    pcache1EnforceMaxPage(pGroup);
    pGroup->nMaxPage = savedMaxPage;
    pcache1LeaveMutex(pGroup);
  }
}

/*
** Implementation of the sqlite3_pcache.xPagecount method. 
** sqlite3_pcache.xPagecount 方法的实现
*/
static int pcache1Pagecount(sqlite3_pcache *p){
  int n;
  PCache1 *pCache = (PCache1*)p;
  pcache1EnterMutex(pCache->pGroup);
  n = pCache->nPage;
  pcache1LeaveMutex(pCache->pGroup);
  return n;
}

/*
** Implementation of the sqlite3_pcache.xFetch method. 
**  sqlite3_pcache.xFetch 方法的实现
**
** Fetch a page by key value.
** 用键值对获取一个页面
**
** Whether or not a new page may be allocated by this function depends on
** the value of the createFlag argument.  0 means do not allocate a new
** page.  1 means allocate a new page if space is easily available.  2 
** means to try really hard to allocate a new page.
** 新页面是否可以由该函数分配取决于createFlag参数的值。
**0表示不分配一个新页面。1.意味着如果空间是容易的（空间足够），
**分配一个新页面。2意味着尝试强行分配一个新页面。
**
** For a non-purgeable cache (a cache used as the storage for an in-memory
** database) there is really no difference between createFlag 1 and 2.  So
** the calling function (pcache.c) will never have a createFlag of 1 on
** a non-purgeable cache.
** 对于non-purgeable缓存(缓存作为一个内存数据库的存储)createFlag1和2是没有区别的。
** 因此,调用函数(pcache.c) 在non-purgeable缓存中，永远不会有createFlag 1。
**
** There are three different approaches to obtaining space for a page,
** depending on the value of parameter createFlag (which may be 0, 1 or 2).
** 根据createFlag参数的值(这可能是0、1或者2)，为页面获取空间有三种不同的方法
**
**   1. Regardless of the value of createFlag, the cache is searched for a 
**      copy of the requested page. If one is found, it is returned.
** 不管createFlag的值,请求页面副本搜索缓存。如果找到一个（缓存）,返回它。
**
**   2. If createFlag==0 and the page is not already in the cache, NULL is
**      returned.
**  如果createFlag==0，并且 这个页面没有在cache中时，返回NULL.
**
**   3. If createFlag is 1, and the page is not already in the cache, then
**      return NULL (do not allocate a new page) if any of the following
**      conditions are true:
** 如果createFlag 为1,并且页面没有在缓存中,那么返回NULL(不要分配一个新页面)如果下列条件为真:
** \如果下列条件为真:
**
**       (a) the number of pages pinned by the cache is greater than
**           PCache1.nMax, or
** 缓存的页面数量大于PCache1.nMax.或
**
**       (b) the number of pages pinned by the cache is greater than
**           the sum of nMax for all purgeable caches, less the sum of 
**           nMin for all other purgeable caches, or
**  缓存的属相大于所有可净化cache的nMax总和，低于其他可净化cache的nMin总数，或者
** 
**   4. If none of the first three conditions apply and the cache is marked
**      as purgeable, and if one of the following is true:
** 如果没有前三个条件的应用和缓存被标记为purgeable,如果下列之一为真:
**
**       (a) The number of pages allocated for the cache is already 
**           PCache1.nMax, or
** 为cache分配的的page数量已经（就位）PCache1.nMax,或
** 
**       (b) The number of pages allocated for all purgeable caches is
**           already equal to or greater than the sum of nMax for all
**           purgeable caches,
** 分配给所有purgeable cache的页面数已经等于或大于所有purgeable cache的nMax总和。
**
**       (c) The system is under memory pressure and wants to avoid
**           unnecessary pages cache entry allocations
** 系统在memory可承受范围内并且想避免分配不必要的页面cache实体
**
**      then attempt to recycle a page from the LRU list. If it is the right
**      size, return the recycled buffer. Otherwise, free the buffer and
**      proceed to step 5. 
** 这时，尝试从LRU列表中回收一个页面。如果有合适大小(的页面)，返回
** 它的回收buffer，否则，释放它的buffer并执行 步骤5
**
**   5. Otherwise, allocate and return a new page buffer.
**   5.分配并返回一个新页面buffer
*/
static sqlite3_pcache_page *pcache1Fetch(
  sqlite3_pcache *p, 
  unsigned int iKey, 
  int createFlag
){
  unsigned int nPinned;
  PCache1 *pCache = (PCache1 *)p;
  PGroup *pGroup;
  PgHdr1 *pPage = 0;

  assert( pCache->bPurgeable || createFlag!=1 );
  assert( pCache->bPurgeable || pCache->nMin==0 );
  assert( pCache->bPurgeable==0 || pCache->nMin==10 );
  assert( pCache->nMin==0 || pCache->bPurgeable );
  pcache1EnterMutex(pGroup = pCache->pGroup);

  /* Step 1: Search the hash table for an existing entry.  步骤1.对一个已存在的实体查找hash table*/
  if( pCache->nHash>0 ){
    unsigned int h = iKey % pCache->nHash;
    for(pPage=pCache->apHash[h]; pPage&&pPage->iKey!=iKey; pPage=pPage->pNext);
  }

  /* Step 2: Abort if no existing page is found and createFlag is 0  	步骤2.中止，如果没有找到现
                                                                                                  有页面并且createFlag为0*/
  if( pPage || createFlag==0 ){
    pcache1PinPage(pPage);
    goto fetch_out;
  }

  /* The pGroup local variable will normally be initialized by the
  ** pcache1EnterMutex() macro above.  But if SQLITE_MUTEX_OMIT is defined,
  ** then pcache1EnterMutex() is a no-op, so we have to initialize the
  ** local variable here.  Delaying the initialization of pGroup is an
  ** optimization:  The common case is to exit the module before reaching
  ** this point.
  ** pGroup的局部变量通常将被pcache1EnterMutex() 宏初始化。
  ** 但是如果SQLITE_MUTEX_OMIT被定义了，这时 pcache1EnterMutex()不可用，我们只能
  ** 在这里初始化这个局部变量。推迟pGroup是一种优化的初始化，
  ** :常见的情况是在达到这一点前退出模块.
  */
#ifdef SQLITE_MUTEX_OMIT
  pGroup = pCache->pGroup;
#endif

/* Step 3: Abort if createFlag is 1 but the cache is nearly full 中止如果createFlag 为1，但是cache快满了 */

  assert( pCache->nPage >= pCache->nRecyclable );
  nPinned = pCache->nPage - pCache->nRecyclable;
  assert( pGroup->mxPinned == pGroup->nMaxPage + 10 - pGroup->nMinPage );
  assert( pCache->n90pct == pCache->nMax*9/10 );
  if( createFlag==1 && (
        nPinned>=pGroup->mxPinned
     || nPinned>=pCache->n90pct
     || pcache1UnderMemoryPressure(pCache)
  )){
    goto fetch_out;
  }

  if( pCache->nPage>=pCache->nHash && pcache1ResizeHash(pCache) ){
    goto fetch_out;
  }

  /* Step 4. Try to recycle a page. 		步骤4. 尝试回收一个页面*/
  if( pCache->bPurgeable && pGroup->pLruTail && (
         (pCache->nPage+1>=pCache->nMax)
      || pGroup->nCurrentPage>=pGroup->nMaxPage
      || pcache1UnderMemoryPressure(pCache)
  )){
    PCache1 *pOther;
    pPage = pGroup->pLruTail;
    pcache1RemoveFromHash(pPage);
    pcache1PinPage(pPage);
    pOther = pPage->pCache;

    /* We want to verify that szPage and szExtra are the same for pOther
    ** and pCache.  Assert that we can verify this by comparing sums.
    ** 我们想确认szPage和 szExtra对于pOther和pCache是相同的。
    ** 我们可以断言(的确这样)，通过验证比较这个总结。
    */
    assert( (pCache->szPage & (pCache->szPage-1))==0 && pCache->szPage>=512 );
    assert( pCache->szExtra<512 );
    assert( (pOther->szPage & (pOther->szPage-1))==0 && pOther->szPage>=512 );
    assert( pOther->szExtra<512 );

    if( pOther->szPage+pOther->szExtra != pCache->szPage+pCache->szExtra ){
      pcache1FreePage(pPage);
      pPage = 0;
    }else{
      pGroup->nCurrentPage -= (pOther->bPurgeable - pCache->bPurgeable);
    }
  }

  /* Step 5. If a usable page buffer has still not been found, 
  ** attempt to allocate a new one. 
  ** 步骤5. 如果一个可用的页面缓冲还没有被发现,尝试分配一个新的。
  */
  if( !pPage ){
    if( createFlag==1 ) sqlite3BeginBenignMalloc();
    pPage = pcache1AllocPage(pCache);
    if( createFlag==1 ) sqlite3EndBenignMalloc();
  }

  if( pPage ){
    unsigned int h = iKey % pCache->nHash;
    pCache->nPage++;
    pPage->iKey = iKey;
    pPage->pNext = pCache->apHash[h];
    pPage->pCache = pCache;
    pPage->pLruPrev = 0;
    pPage->pLruNext = 0;
    *(void **)pPage->page.pExtra = 0;
    pCache->apHash[h] = pPage;
  }

fetch_out:
  if( pPage && iKey>pCache->iMaxKey ){
    pCache->iMaxKey = iKey;
  }
  pcache1LeaveMutex(pGroup);
  return &pPage->page;
}


/*
** Implementation of the sqlite3_pcache.xUnpin method.
**  sqlite3_pcache.xUnpin 方法的实现
**
** Mark a page as unpinned (eligible for asynchronous recycling).
** 标记一个页面是可拔掉的(异步回收资格)。
*/
static void pcache1Unpin(
  sqlite3_pcache *p, 
  sqlite3_pcache_page *pPg, 
  int reuseUnlikely
){
  PCache1 *pCache = (PCache1 *)p;
  PgHdr1 *pPage = (PgHdr1 *)pPg;
  PGroup *pGroup = pCache->pGroup;
 
  assert( pPage->pCache==pCache );
  pcache1EnterMutex(pGroup);

  /* It is an error to call this function if the page is already 
  ** part of the PGroup LRU list.
  ** 如果页面已经是PGroup LRU列表的一部分，调用这个函数是一个错误。
  */
  assert( pPage->pLruPrev==0 && pPage->pLruNext==0 );
  assert( pGroup->pLruHead!=pPage && pGroup->pLruTail!=pPage );

  if( reuseUnlikely || pGroup->nCurrentPage>pGroup->nMaxPage ){
    pcache1RemoveFromHash(pPage);
    pcache1FreePage(pPage);
  }else{
    /* Add the page to the PGroup LRU list. 	添加一个页面到PGroup LRU列表*/
    if( pGroup->pLruHead ){
      pGroup->pLruHead->pLruPrev = pPage;
      pPage->pLruNext = pGroup->pLruHead;
      pGroup->pLruHead = pPage;
    }else{
      pGroup->pLruTail = pPage;
      pGroup->pLruHead = pPage;
    }
    pCache->nRecyclable++;
  }

  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xRekey method. 
**  sqlite3_pcache.xRekey方法的实现
*/
static void pcache1Rekey(
  sqlite3_pcache *p,
  sqlite3_pcache_page *pPg,
  unsigned int iOld,
  unsigned int iNew
){
  PCache1 *pCache = (PCache1 *)p;
  PgHdr1 *pPage = (PgHdr1 *)pPg;
  PgHdr1 **pp;
  unsigned int h; 
  assert( pPage->iKey==iOld );
  assert( pPage->pCache==pCache );

  pcache1EnterMutex(pCache->pGroup);

  h = iOld%pCache->nHash;
  pp = &pCache->apHash[h];
  while( (*pp)!=pPage ){
    pp = &(*pp)->pNext;
  }
  *pp = pPage->pNext;

  h = iNew%pCache->nHash;
  pPage->iKey = iNew;
  pPage->pNext = pCache->apHash[h];
  pCache->apHash[h] = pPage;
  if( iNew>pCache->iMaxKey ){
    pCache->iMaxKey = iNew;
  }

  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xTruncate method. 
**  sqlite3_pcache.xTruncate方法的实现
**
** Discard all unpinned pages in the cache with a page number equal to
** or greater than parameter iLimit. Any pinned pages with a page number
** equal to or greater than iLimit are implicitly unpinned.
** 丢弃所有在cache中的，页面数大于或等于参数iLimit的可拔掉页面.
** 任何页面数等于或大于iLimit的固定页面，被默认地拔掉。
*/
static void pcache1Truncate(sqlite3_pcache *p, unsigned int iLimit){
  PCache1 *pCache = (PCache1 *)p;
  pcache1EnterMutex(pCache->pGroup);
  if( iLimit<=pCache->iMaxKey ){
    pcache1TruncateUnsafe(pCache, iLimit);
    pCache->iMaxKey = iLimit-1;
  }
  pcache1LeaveMutex(pCache->pGroup);
}

/*
** Implementation of the sqlite3_pcache.xDestroy method. 
**  sqlite3_pcache.xDestroy方法的实现
** 
** Destroy a cache allocated using pcache1Create().
** 用 pcache1Create()方法销毁一个已分配的cache
*/
static void pcache1Destroy(sqlite3_pcache *p){
  PCache1 *pCache = (PCache1 *)p;
  PGroup *pGroup = pCache->pGroup;
  assert( pCache->bPurgeable || (pCache->nMax==0 && pCache->nMin==0) );
  pcache1EnterMutex(pGroup);
  pcache1TruncateUnsafe(pCache, 0);
  assert( pGroup->nMaxPage >= pCache->nMax );
  pGroup->nMaxPage -= pCache->nMax;
  assert( pGroup->nMinPage >= pCache->nMin );
  pGroup->nMinPage -= pCache->nMin;
  pGroup->mxPinned = pGroup->nMaxPage + 10 - pGroup->nMinPage;
  pcache1EnforceMaxPage(pGroup);
  pcache1LeaveMutex(pGroup);
  sqlite3_free(pCache->apHash);
  sqlite3_free(pCache);
}

/*
** This function is called during initialization (sqlite3_initialize()) to
** install the default pluggable cache module, assuming the user has not
** already provided an alternative.
** 这个函数在初始化期间(sqlite3_initialize())来安装默认缓存可插拔模块，
** 假设用户已经不需要一个选择。
*/
void sqlite3PCacheSetDefault(void){
  static const sqlite3_pcache_methods2 defaultMethods = {
    1,                       /* iVersion */
    0,                       /* pArg */
    pcache1Init,             /* xInit */
    pcache1Shutdown,         /* xShutdown */
    pcache1Create,           /* xCreate */
    pcache1Cachesize,        /* xCachesize */
    pcache1Pagecount,        /* xPagecount */
    pcache1Fetch,            /* xFetch */
    pcache1Unpin,            /* xUnpin */
    pcache1Rekey,            /* xRekey */
    pcache1Truncate,         /* xTruncate */
    pcache1Destroy,          /* xDestroy */
    pcache1Shrink            /* xShrink */
  };
  sqlite3_config(SQLITE_CONFIG_PCACHE2, &defaultMethods);
}

#ifdef SQLITE_ENABLE_MEMORY_MANAGEMENT
/*
** This function is called to free superfluous dynamically allocated memory
** held by the pager system. Memory in use by any SQLite pager allocated
** by the current thread may be sqlite3_free()ed.
** 这个函数被pager系统调用来释放多余的动态分配的内存。
** 任何SQLite pager分配的内存有可能被当前线程使用(可能是sqlite3_free()ed).
** 
** nReq is the number of bytes of memory required. Once this much has
** been released, the function returns. The return value is the total number 
** of bytes of memory released.
** nReq是内存所需的字节数。一旦被释放，这个函数就返回(这个值)
** 返回的值是所有释放内存的字节数。
*/
int sqlite3PcacheReleaseMemory(int nReq){
  int nFree = 0;
  assert( sqlite3_mutex_notheld(pcache1.grp.mutex) );
  assert( sqlite3_mutex_notheld(pcache1.mutex) );
  if( pcache1.pStart==0 ){
    PgHdr1 *p;
    pcache1EnterMutex(&pcache1.grp);
    while( (nReq<0 || nFree<nReq) && ((p=pcache1.grp.pLruTail)!=0) ){
      nFree += pcache1MemSize(p->page.pBuf);
#ifdef SQLITE_PCACHE_SEPARATE_HEADER
      nFree += sqlite3MemSize(p);
#endif
      pcache1PinPage(p);
      pcache1RemoveFromHash(p);
      pcache1FreePage(p);
    }
    pcache1LeaveMutex(&pcache1.grp);
  }
  return nFree;
}
#endif /* SQLITE_ENABLE_MEMORY_MANAGEMENT */

#ifdef SQLITE_TEST
/*
** This function is used by test procedures to inspect the internal state
** of the global cache.
** 这个函数时用于测试 程序，去检查全局cache的内部状态
*/
void sqlite3PcacheStats(
  int *pnCurrent,      /* OUT: Total number of pages cached 		所有页面cache的值*/
  int *pnMax,          /* OUT: Global maximum cache size 	全局maximum cache的大小*/
  int *pnMin,          /* OUT: Sum of PCache1.nMin for purgeable caches 	可净化cache的PCache1.nMin 的总和*/
  int *pnRecyclable    /* OUT: Total number of pages available for recycling 		回收可用也页面的总数*/
){
  PgHdr1 *p;
  int nRecyclable = 0;
  for(p=pcache1.grp.pLruHead; p; p=p->pLruNext){
    nRecyclable++;
  }
  *pnCurrent = pcache1.grp.nCurrentPage;
  *pnMax = (int)pcache1.grp.nMaxPage;
  *pnMin = (int)pcache1.grp.nMinPage;
  *pnRecyclable = nRecyclable;
}
#endif
