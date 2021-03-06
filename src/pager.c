/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This is the implementation of the page cache subsystem or "pager".
    这是页面缓存子系统的实现或者叫"pager"。
** The pager is used to access a database disk file.  It implements
** atomic commit and rollback through the use of a journal file that
** is separate from the database file.  The pager also implements file
** locking to prevent two processes from writing the same database
** file simultaneously, or one process from reading the database while
** another is writing.
<<<<<<< HEAD
    Pager是用于访问数据库的磁盘文件。它通过使用独立于数据库文件的日志文件实现原子提交和恢复。
    Pager同样实现利用文件锁防止两个进程同时编写相同的数据库，或者防止一个进程读取正在被编写的数据库。
=======
**Pager是用来实现数据库磁盘文件的代码段。它实现了原子性的事务提交和回滚。
**这些功能是使用从数据库文件中分离的日志文件实现的。Pager也实现了文件锁来
**防止两个进程同时对相同的数据库进行写操作或者一个进程进行写操作时，另一个
**另一个进程在进行度操作。
>>>>>>> da6e9bfe7165b689ad9006b67395edebe64ecd6c
*/
#ifndef SQLITE_OMIT_DISKIO
#include "sqliteInt.h"
#include "wal.h"


/******************* NOTES ON THE DESIGN OF THE PAGER ************************
**
** This comment block describes invariants that hold when using a rollback
** journal.  These invariants do not apply for journal_mode=WAL,
** journal_mode=MEMORY, or journal_mode=OFF.
**
** Within this comment block, a page is deemed to have been synced
** automatically as soon as it is written when PRAGMA synchronous=OFF.
** Otherwise, the page is not synced until the xSync method of the VFS
** is called successfully on the file containing the page.
**
** Definition:  A page of the database file is said to be "overwriteable" if
** one or more of the following are true about the page:
** 
**     (a)  The original content of the page as it was at the beginning of
**          the transaction has been written into the rollback journal and
**          synced.
** 
**     (b)  The page was a freelist leaf page at the start of the transaction.
** 
**     (c)  The page number is greater than the largest page that existed in
**          the database file at the start of the transaction.
** 
** (1) A page of the database file is never overwritten unless one of the
**     following are true:
** 
**     (a) The page and all other pages on the same sector are overwriteable.
** 
**     (b) The atomic page write optimization is enabled, and the entire
**         transaction other than the update of the transaction sequence
**         number consists of a single page change.
** 
** (2) The content of a page written into the rollback journal exactly matches
**     both the content in the database when the rollback journal was written
**     and the content in the database at the beginning of the current
**     transaction.
** 
** (3) Writes to the database file are an integer multiple of the page size
**     in length and are aligned on a page boundary.
** 
** (4) Reads from the database file are either aligned on a page boundary and
**     an integer multiple of the page size in length or are taken from the
**     first 100 bytes of the database file.
** 
** (5) All writes to the database file are synced prior to the rollback journal
**     being deleted, truncated, or zeroed.
** 
** (6) If a master journal file is used, then all writes to the database file
**     are synced prior to the master journal being deleted.
** 
** Definition: Two databases (or the same database at two points it time)
** are said to be "logically equivalent" if they give the same answer to
** all queries.  Note in particular the content of freelist leaf
** pages can be changed arbitarily without effecting the logical equivalence
** of the database.
** 
** (7) At any time, if any subset, including the empty set and the total set,
**     of the unsynced changes to a rollback journal are removed and the 
**     journal is rolled back, the resulting database file will be logical
**     equivalent to the database file at the beginning of the transaction.
** 
** (8) When a transaction is rolled back, the xTruncate method of the VFS
**     is called to restore the database file to the same size it was at
**     the beginning of the transaction.  (In some VFSes, the xTruncate
**     method is a no-op, but that does not change the fact the SQLite will
**     invoke it.)
** 
** (9) Whenever the database file is modified, at least one bit in the range
**     of bytes from 24 through 39 inclusive will be changed prior to releasing
**     the EXCLUSIVE lock, thus signaling other connections on the same
**     database to flush their caches.
**
** (10) The pattern of bits in bytes 24 through 39 shall not repeat in less
**      than one billion transactions.
**
** (11) A database file is well-formed at the beginning and at the conclusion
**      of every transaction.
**
** (12) An EXCLUSIVE lock is held on the database file when writing to
**      the database file.
**
** (13) A SHARED lock is held on the database file while reading any
**      content out of the database file.
**
**这个注释块描述了使用回滚日志时的不变量。这些不变量没有声明journal_mode=WAL,
** journal_mode=MEMORY, 或者 journal_mode=OFF.这个注释块中，当synchronous=OFF时
**一个页面被写入的同时自动同步。否则直到包含这个页面的文件在xSync中VFS方法执行的
**时候才会同步。
**定义：数据库文件中一页只有当下面的条件有一个成立时被称为溢出：
**（a）食物刚开始页面目录就被写入回滚日志文件并且被同步
**（b）在事务的开始这页是空闲的列表子页
**（c）在事务的开始这页的页码大雨存在数据库文件中的最大页数
**（1）数据库的任何一页决定不会被重写，除非下面任一个条件为真：
**  （a）这一页或者其它页在相同的扇区都被覆盖
**  （b）原子页最优化读写是可能的，全部的事务由单页变化组成而不是跟新的事务序列
**（2）一个页面的目录写进回滚日志。正确匹配着回滚日志被写入的目录和当前事务开始时的目录
**（3）写入数据库文件在长度上是整数倍
**（4）从数据库文件读取页面，页面大小是整数或者从数据库文件前100b开始
**（5）数据库文件优化同步回滚日志被删除，缩短，清零
**（6）如果一个日志文件被使用，所有对数据库文件的读写优先同步日志被删除
**定义：两个数据库如果他们对所有等值的问题给出相同的答案叫逻辑等价
**（7）在任意时刻，如果任何子集包括空集合和总的集合，日志文件中未同步的修改会溢出，日志会回滚，数据库会回到事务刚开始时的状态
**（8）当事务回滚，xTruncate的VFs方法会将数据库文件恢复到事务开始时的大小
**（9）无论何时数据库被修改，至少1b，从24到39位的空间被释放，EXCLUSIVE锁将被释并且发信号放运作他们的高速缓存
**（10）24列新的模式不会重写至少100万次
**（11）在每一个事务的开始和结束时一个数据库文件是结构良好的
**（12）当写入数据库时EXCLUSIVE锁保存在数据文件中
**（13）当读人已不在数据文件的目录时，SHARED锁保存在数据库文件中
******************************************************************************/

/*
** Macros for troubleshooting.  Normally turned off
*/
#if 0
int sqlite3PagerTrace=1;  /* True to enable tracing */
#define sqlite3DebugPrintf printf
#define PAGERTRACE(X)     if( sqlite3PagerTrace ){ sqlite3DebugPrintf X; }
#else
#define PAGERTRACE(X)
#endif

/*
** The following two macros are used within the PAGERTRACE() macros above
** to print out file-descriptors. 
**
** PAGERID() takes a pointer to a Pager struct as its argument. The
** associated file-descriptor is returned. FILEHANDLEID() takes an sqlite3_file
** struct as its argument.
*/
#define PAGERID(p) ((int)(p->fd))
#define FILEHANDLEID(fd) ((int)fd)

/*
** The Pager.eState variable stores the current 'state' of a pager. A
** pager may be in any one of the seven states shown in the following
** state diagram.
**该Pager.eState变量存储寻呼机的当前“状态”。一个Pager可能在七个状态的下面所示的任何一个状态图。
**
**                            OPEN <------+------+
**                              |         |      |
**                              V         |      |
**               +---------> READER-------+      |
**               |              |                |
**               |              V                |
**               |<-------WRITER_LOCKED------> ERROR
**               |              |                ^  
**               |              V                |
**               |<------WRITER_CACHEMOD-------->|
**               |              |                |
**               |              V                |
**               |<-------WRITER_DBMOD---------->|
**               |              |                |
**               |              V                |
**               +<------WRITER_FINISHED-------->+
**
**
** List of state transitions and the C [function] that performs each:
**状态转换和C [function]执行每个列表
** 
**   OPEN              -> READER              [sqlite3PagerSharedLock]
**   READER            -> OPEN                [pager_unlock]
**
**   READER            -> WRITER_LOCKED       [sqlite3PagerBegin]
**   WRITER_LOCKED     -> WRITER_CACHEMOD     [pager_open_journal]
**   WRITER_CACHEMOD   -> WRITER_DBMOD        [syncJournal]
**   WRITER_DBMOD      -> WRITER_FINISHED     [sqlite3PagerCommitPhaseOne]
**   WRITER_***        -> READER              [pager_end_transaction]
**
**   WRITER_***        -> ERROR               [pager_error]
**   ERROR             -> OPEN                [pager_unlock]
** 
**
**  OPEN:
**
**pager在该状态下启动。没有保证这个状态 - 该文件可能或不可能被锁定，并且数据库的大小是未知的。该数据库可以不进行读或写。
**    The pager starts up in this state. Nothing is guaranteed in this
**    state - the file may or may not be locked and the database size is
**    unknown. The database may not be read or written.
**
**    * No read or write transaction is active.
**    * Any lock, or no lock at all, may be held on the database file.
**    * The dbSize, dbOrigSize and dbFileSize variables may not be trusted.
**
**没有读或写的动作。任何锁或无锁，可能被追究的数据库文件。dbSize，dbOrigSize和dbFileSize变量可能不被信任
**  READER:
**
**    In this state all the requirements for reading the database in 
**    rollback (non-WAL) mode are met. Unless the pager is (or recently
**    was) in exclusive-locking mode, a user-level read transaction is 
**    open. The database size is known in this state.
**
**在这种状态下所有用于读取数据库中的规定，回滚（非WAL）模式得到满足。
**除非该pager是（或最近是）在独占锁模式，用户级读取状态是打开。这种状态下，该数据库的大小是已知的
**
**与locking_mode运行的连接=正常进入此状态时，
**它会打开一个读事务中的数据库，并返回状态
**读成交后OPEN完成。然而，一个连接
**在locking_mode运行=独占（包括临时数据库）保持
**即使在读取事务被关闭这个状态。唯一的办法
**一个locking_mode=独占连接可以从READER过渡到OPEN
**是通过错误状态（见下文）。
**    A connection running with locking_mode=normal enters this state when
**    it opens a read-transaction on the database and returns to state
**    OPEN after the read-transaction is completed. However a connection
**    running in locking_mode=exclusive (including temp databases) remains in
**    this state even after the read-transaction is closed. The only way
**    a locking_mode=exclusive connection can transition from READER to OPEN
**    is via the ERROR state (see below).
** 
**一个读事务可能是积活的（但写事务不能）。
**共享或以上持有锁的数据库文件。
**该dbSize变量可以信任（即使用户级读交易不活跃）。该dbOrigSize和dbFileSize变量可能不被信任在此点。
**如果数据库是WAL数据库，那么WAL连接打开。
**即使一个读事务不打开，这保证没有在文件系统中没有hot-journal。
**    * A read transaction may be active (but a write-transaction cannot).
**    * A SHARED or greater lock is held on the database file.
**    * The dbSize variable may be trusted (even if a user-level read 
**      transaction is not active). The dbOrigSize and dbFileSize variables
**      may not be trusted at this point.
**    * If the database is a WAL database, then the WAL connection is open.
**    * Even if a read-transaction is not open, it is guaranteed that 
**      there is no hot-journal in the file-system.
**
**  WRITER_LOCKED:
**
**当进行写事务时，pager从准备状态转移到这个状态
**第一次打开的数据库。在WRITER_LOCKED状态下，所有的锁需要启动一个写事务处理被保持，但没有实际的修改高速缓存或数据库已经发生。
**    The pager moves to this state from READER when a write-transaction
**    is first opened on the database. In WRITER_LOCKED state, all locks 
**    required to start a write-transaction are held, but no actual 
**    modifications to the cache or database have taken place.
**
**在回滚模式中保留或（如打开了BEGIN EXCLUSIVE）上的数据库文件时获得EXCLUSIVE锁
**移动到这个状态，但日志文件不写入或打开
**对处于这种状态。如果事务被提交或回滚时
**在WRITER_LOCKED状态，所有需要的是解锁数据库文件。
**    In rollback mode, a RESERVED or (if the transaction was opened with 
**    BEGIN EXCLUSIVE) EXCLUSIVE lock is obtained on the database file when
**    moving to this state, but the journal file is not written to or opened 
**    to in this state. If the transaction is committed or rolled back while 
**    in WRITER_LOCKED state, all that is required is to unlock the database 
**    file.
**
**在WAL模式，WalBeginWriteTransaction（）被调用来锁定日志文件。
**如果连接与locking_mode运行=独占，由获取对数据库文件的独占锁定。
**    IN WAL mode, WalBeginWriteTransaction() is called to lock the log file.
**    If the connection is running with locking_mode=exclusive, an attempt
**    is made to obtain an EXCLUSIVE lock on the database file.
**
**写事务处于活动状态。
**如果连接在回滚模式打开，保留或更大持有锁的数据库文件。
**如果连接是打开的WAL-模式下，WAL写交易是开放的（即**sqlite3WalBeginWriteTransaction（）已成功唤醒）。
*** dbSize，dbOrigSize和dbFileSize变量都是有效的。
** *pager缓存的内容没有被修改。
** *该日志文件可能会或可能不会被打开。
** 没有东西（不连在第一报头）被写入日志。
**    * A write transaction is active.
**    * If the connection is open in rollback-mode, a RESERVED or greater 
**      lock is held on the database file.
**    * If the connection is open in WAL-mode, a WAL write transaction
**      is open (i.e. sqlite3WalBeginWriteTransaction() has been successfully
**      called).
**    * The dbSize, dbOrigSize and dbFileSize variables are all valid.
**    * The contents of the pager cache have not been modified.
**    * The journal file may or may not be open.
**    * Nothing (not even the first header) has been written to the journal.
**
**  WRITER_CACHEMOD:
**
**当页面首先由上层修改时，pager从WRITER_LOCKED状态转移到这种状态。在回滚模**式下，日志文件被打开（如果尚未打开）和一个首标写入到
**它的开始。磁盘上的数据库文件还没有被修改。
**    A pager moves from WRITER_LOCKED state to this state when a page is
**    first modified by the upper layer. In rollback mode the journal file
**    is opened (if it is not already open) and a header written to the
**    start of it. The database file on disk has not been modified.
**
**写事务处于活动状态。
** 保留或更大的锁被保留在数据库文件。
**日志文件是开放的，第一头已被写入
**但标题尚未同步到磁盘。
**页面缓存的内容已被修改。
**    * A write transaction is active.
**    * A RESERVED or greater lock is held on the database file.
**    * The journal file is open and the first header has been written 
**      to it, but the header has not been synced to disk.
**    * The contents of the page cache have been modified.
**
**当它修改了数据库文件的内容时，pager从过渡到WRITER_CACHEMOD状态**WRITER_DBMOD。 WAL连接
**永远不会进入这种状态（因为它们不修改数据库文件，
**仅仅是日志文件）。
**  WRITER_DBMOD:
**
**    The pager transitions from WRITER_CACHEMOD into WRITER_DBMOD state
**    when it modifies the contents of the database file. WAL connections
**    never enter this state (since they do not modify the database file,
**    just the log file).
**
***写事务处于活动状态。
**独占或更高的锁持有的数据库文件。
**日志文件是开放的，第一头已被写入
**并同步到磁盘。
**页面缓存的内容已被修改（并且可能
**写入到磁盘）。
**    * A write transaction is active.
**    * An EXCLUSIVE or greater lock is held on the database file.
**    * The journal file is open and the first header has been written 
**      and synced to disk.
**    * The contents of the page cache have been modified (and possibly
**      written to disk).
**
**  WRITER_FINISHED:
**
**    It is not possible for a WAL connection to enter this state.
**
**回滚模式pager的变化，从WRITER_DBMOD WRITER_FINISHED状态
**整个交易后状态已成功写进
**数据库文件。在这种状态下，事务可以简单地致力于
**被敲定日志文件。一旦在WRITER_FINISHED状态时，它是
**不能够进一步修改数据库。在这一点上，上
**层必须提交或回滚事务。
**    A rollback-mode pager changes to WRITER_FINISHED state from WRITER_DBMOD
**    state after the entire transaction has been successfully written into the
**    database file. In this state the transaction may be committed simply
**    by finalizing the journal file. Once in WRITER_FINISHED state, it is 
**    not possible to modify the database further. At this point, the upper 
**    layer must either commit or rollback the transaction.
**
**写事务处于活动状态。
**独占或更高的锁持有的数据库文件。
**所有写日志和数据库中的数据，并同步完成。
**如果没有出错，所有剩下的工作就是敲定为日志
**提交事务。如果没有出现错误，主叫方将需要
**回滚事务
**    * A write transaction is active.
**    * An EXCLUSIVE or greater lock is held on the database file.
**    * All writing and syncing of journal and database data has finished.
**      If no error occured, all that remains is to finalize the journal to
**      commit the transaction. If an error did occur, the caller will need
**      to rollback the transaction. 
**
**  ERROR:
**
**错误状态时输入的IO或磁盘已满错误（包括
** SQLITE_IOERR_NOMEM）发生在代码中的点，使得它
**难以确保该内存pager状态（高速缓存内容，
**db大小等）都与文件系统中的内容是一致的。
**    The ERROR state is entered when an IO or disk-full error (including
**    SQLITE_IOERR_NOMEM) occurs at a point in the code that makes it 
**    difficult to be sure that the in-memory pager state (cache contents, 
**    db size etc.) are consistent with the contents of the file-system.
**
**临时pager文件可能进入错误状态，但在内存中的pager不能。
**    Temporary pager files may enter the ERROR state, but in-memory pagers
**    cannot.
**
**例如，如果在执行一个回滚时IO错误，
**页面缓存的内容可能会处于不一致的状态。
**在这一点上是有危险的变回READER状态
**（如通常回滚之后发生）。任何后续的读取可能
**报告数据库损坏（由于不一致的高速缓存），如果
**他们升级到写入，他们可能会在无意中损坏数据库
**文件。为避免这种危害，pager切换到错误状态
**而不是写入以下这样的错误。
**    For example, if an IO error occurs while performing a rollback, 
**    the contents of the page-cache may be left in an inconsistent state.
**    At this point it would be dangerous to change back to READER state
**    (as usually happens after a rollback). Any subsequent readers might
**    report database corruption (due to the inconsistent cache), and if
**    they upgrade to writers, they may inadvertently corrupt the database
**    file. To avoid this hazard, the pager switches into the ERROR state
**    instead of READER following such an error.
**
**一旦进入错误状态，任何企图利用该pager
**读取或写入数据返回一个错误。最后，一旦所有
**好的事务已被抛弃，pager能
**过渡回打开状态，丢弃的内容
**页面缓存和其他内存状态在同一时间。一切
**从磁盘重新装入（如果需要的话，回滚peformed）
**当读事务打开旁边的pager（过渡
**pager进入读取状态）。在这一点上，系统已经从错误中恢复
**    Once it has entered the ERROR state, any attempt to use the pager
**    to read or write data returns an error. Eventually, once all 
**    outstanding transactions have been abandoned, the pager is able to
**    transition back to OPEN state, discarding the contents of the 
**    page-cache and any other in-memory state at the same time. Everything
**    is reloaded from disk (and, if necessary, hot-journal rollback peformed)
**    when a read-transaction is next opened on the pager (transitioning
**    the pager into READER state). At that point the system has recovered 
**    from the error.
**
**具体来说，pager跳进错误状态，如果：
**    Specifically, the pager jumps into the ERROR state if:
**
**试图在回滚时
**1.错误。这种情况发生在
** 功能sqlite3PagerRollback（）。
**      1. An error occurs while attempting a rollback. This happens in
**         function sqlite3PagerRollback().
**
**2.试图完成一个日志文件时出错
**      2. An error occurs while attempting to finalize a journal file
**         following a commit in function sqlite3PagerCommitPhaseTwo().
**
**3.在尝试写日记或发生错误
**在功能pagerStress（），以便释放数据库文件
**内存。
**      3. An error occurs while attempting to write to the journal or
**         database file in function pagerStress() in order to free up
**         memory.
**
**在其他情况下，返回到b树层的误差。 B树
**层，然后试图回滚操作。如果错误状况
**仍然存在，pager通过条件进入ERROR状态上述（1）
**    In other cases, the error is returned to the b-tree layer. The b-tree
**    layer then attempts a rollback operation. If the error condition 
**    persists, the pager enters the ERROR state via condition (1) above.
**
**条件（3）是必要的，因为它可以由一个只读触发
**在一个事务中执行的语句。在这种情况下，如果错误
**代码进行简单的返回给用户，B树层不会
**自动尝试回滚，因为它假设的误差在一
**只读语句不能离开pager在内部不一致
**状态。
**    Condition (3) is necessary because it can be triggered by a read-only
**    statement executed within a transaction. In this case, if the error
**    code were simply returned to the user, the b-tree layer would not
**    automatically attempt a rollback, as it assumes that an error in a
**    read-only statement cannot leave the pager in an internally inconsistent 
**    state.
**
**Pager.errCode变量设置为比SQLITE_OK其他的东西。
**有后一个或多个未完成的引用页（
**最后引用被丢弃的pager应该搬回打开状态）。
** pager不是一个内存pager。
**    * The Pager.errCode variable is set to something other than SQLITE_OK.
**    * There are one or more outstanding references to pages (after the
**      last reference is dropped the pager should move back to OPEN state).
**    * The pager is not an in-memory pager.
**    
**
** Notes:
**
**如果
**连接在WAL模式打开，pager从来没有在WRITER_DBMOD或WRITER_FINISHED状**态，WAL一直在一个
**前四个状态
**   * A pager is never in WRITER_DBMOD or WRITER_FINISHED state if the
**     connection is open in WAL mode. A WAL connection is always in one
**     of the first four states.
**
**通常情况下，以独占模式连接打开从来没有在PAGER_OPEN
**状态。有两个例外：后独占模式有
**被打开（和之前的任何读或写交易
**执行），并且当pager被留下“错误状态”。
**   * Normally, a connection open in exclusive mode is never in PAGER_OPEN
**     state. There are two exceptions: immediately after exclusive-mode has
**     been turned on (and before any read or write transactions are 
**     executed), and when the pager is leaving the "error state".
**
**   * See also: assert_pager_state().
*/
#define PAGER_OPEN                  0
#define PAGER_READER                1
#define PAGER_WRITER_LOCKED         2
#define PAGER_WRITER_CACHEMOD       3
#define PAGER_WRITER_DBMOD          4
#define PAGER_WRITER_FINISHED       5
#define PAGER_ERROR                 6

/*
**所述Pager.eLock变量几乎总是设置为所述一个
**下面的锁定状态，根据当前持有的锁
**数据库文件：NO_LOCK，SHARED_LOCK，RESERVED_LOCK或EXCLUSIVE_LOCK。
**此变量保持最新的锁被采取并通过释放
**的pagerLockDb（）和pagerUnlockDb（）封装。
** The Pager.eLock variable is almost always set to one of the 
** following locking-states, according to the lock currently held on
** the database file: NO_LOCK, SHARED_LOCK, RESERVED_LOCK or EXCLUSIVE_LOCK.
** This variable is kept up to date as locks are taken and released by
** the pagerLockDb() and pagerUnlockDb() wrappers.
**
**如果VFS XLOCK（）或xUnlock（）返回一个错误比其他SQLITE_BUSY
**（即SQLITE_IOERR亚型之一），它是不明确是否
**操作成功。在这些情况下pagerLockDb（）和
** pagerUnlockDb（）采取保守态度 - eLock总是更新
**解锁文件时，并且仅当所述锁定的文件时，更新
** VFS调用成功。通过这种方式，Pager.eLock变量可以被设置
**到少独占（低）值比锁实际上是举行
**在系统级，但它从未设置为更独占值。
** If the VFS xLock() or xUnlock() returns an error other than SQLITE_BUSY
** (i.e. one of the SQLITE_IOERR subtypes), it is not clear whether or not
** the operation was successful. In these circumstances pagerLockDb() and
** pagerUnlockDb() take a conservative approach - eLock is always updated
** when unlocking the file, and only updated when locking the file if the
** VFS call is successful. This way, the Pager.eLock variable may be set
** to a less exclusive (lower) value than the lock that is actually held
** at the system level, but it is never set to a more exclusive value.
**
**这通常是安全的。如果xUnlock失败或出现故障，也有可能
**是几冗余XLOCK（）调用或锁可举办长于
**必需的，但没有什么错误
** This is usually safe. If an xUnlock fails or appears to fail, there may 
** be a few redundant xLock() calls or a lock may be held for longer than
** required, but nothing really goes wrong.
**
**当数据库文件被锁定为pager移动的例外是
**从错误到开状态。在这一点上，可能有hot-journal文件
**在文件系统需要被回滚（作为开的一部分> SHARED
**过渡，用相同的pager或任何其他）。如果调用xUnlock（）
**失败，在这一点上和pager左边一个排它锁，这
**可以混淆调用xCheckReservedLock（），后来由于部分调用hot-journal 的最后部分xCheckReservedLock()
** The exception is when the database file is unlocked as the pager moves
** from ERROR to OPEN state. At this point there may be a hot-journal file 
** in the file-system that needs to be rolled back (as part of a OPEN->SHARED
** transition, by the same pager or any other). If the call to xUnlock()
** fails at this point and the pager is left holding an EXCLUSIVE lock, this
** can confuse the call to xCheckReservedLock() call made later as part
** of hot-journal detection.
**
** xCheckReservedLock（）被定义为返回真“，如果有一个RESERVED
**通过这个过程或任何其他人“设定的锁，所以xCheckReservedLock可能
**返回true，因为调用者本身持有的排他锁（但
**不知道，因为在xUnlock前一个错误的话）。如果发生这种情况
**通过积极创建hot-journal可能被误认为是日记
**交易在另一个过程中，引起的SQLite从数据库读
**不回滚。
** xCheckReservedLock() is defined as returning true "if there is a RESERVED 
** lock held by this process or any others". So xCheckReservedLock may 
** return true because the caller itself is holding an EXCLUSIVE lock (but
** doesn't know it because of a previous error in xUnlock). If this happens
** a hot-journal may be mistaken for a journal being created by an active
** transaction in another process, causing SQLite to read from the database
** without rolling it back.
**
**要解决这个问题，如果解锁时调用xUnlock（）失败
**数据库中的错误状态，Pager.eLock设置为UNKNOWN_LOCK。它
**只变回真正的锁定状态成功调用后
**与XLOCK（EXCLUSIVE）。此外，该代码做开>共享状态过渡
**忽略了检查hot-journal，如果Pager.eLock设置为UNKNOWN_LOCK
**锁。相反，它假定存在一个热杂志并获得独家
**在试图回滚之前数据库文件锁。见功能
** PagerSharedLock（）的更多细节。
** To work around this, if a call to xUnlock() fails when unlocking the
** database in the ERROR state, Pager.eLock is set to UNKNOWN_LOCK. It
** is only changed back to a real locking state after a successful call
** to xLock(EXCLUSIVE). Also, the code to do the OPEN->SHARED state transition
** omits the check for a hot-journal if Pager.eLock is set to UNKNOWN_LOCK 
** lock. Instead, it assumes a hot-journal exists and obtains an EXCLUSIVE
** lock on the database file before attempting to roll it back. See function
** PagerSharedLock() for more detail.
**
** Pager.eLock仅可设定为UNKNOWN_LOCK当pager在
** PAGER_OPEN状态。
** Pager.eLock may only be set to UNKNOWN_LOCK when the pager is in 
** PAGER_OPEN state.
*/
#define UNKNOWN_LOCK                (EXCLUSIVE_LOCK+1)

/*
** A macro used for invoking the codec if there is one
*/
#ifdef SQLITE_HAS_CODEC
# define CODEC1(P,D,N,X,E) \
    if( P->xCodec && P->xCodec(P->pCodec,D,N,X)==0 ){ E; }
# define CODEC2(P,D,N,X,E,O) \
    if( P->xCodec==0 ){ O=(char*)D; }else \
    if( (O=(char*)(P->xCodec(P->pCodec,D,N,X)))==0 ){ E; }
#else
# define CODEC1(P,D,N,X,E)   /* NO-OP */
# define CODEC2(P,D,N,X,E,O) O=(char*)D
#endif

/*
** The maximum allowed sector size. 64KiB. If the xSectorsize() method 
** returns a value larger than this, then MAX_SECTOR_SIZE is used instead.
** This could conceivably cause corruption following a power failure on
** such a system. This is currently an undocumented limit.
**扇区最大允许大小为64kib。如果xSectorsize()方法返回大于这个值,然后使用MAX_SECTOR_SIZE代替这个值。
*/
#define MAX_SECTOR_SIZE 0x10000

/*
** An instance of the following structure is allocated for each active
** savepoint and statement transaction in the system. All such structures
** are stored in the Pager.aSavepoint[] array, which is allocated and
** resized using sqlite3Realloc().
**
**以下结构的实例分配给每个活动系统中保存点和语句的事务。所有结构都被保存在Pager。
**aSavepoint[]数组将会使用sqlite3Realloc()。
**
** When a savepoint is created, the PagerSavepoint.iHdrOffset field is
** set to 0. If a journal-header is written into the main journal while
** the savepoint is active, then iHdrOffset is set to the byte offset 
** immediately following the last journal record written into the main
** journal before the journal-header. This is required during savepoint
** rollback (see pagerPlaybackSavepoint()).
**当创建一个保存点,iHdrOffset字段设置为0。如果写入journal-header主日志而保存点是活跃的,
**则设置iHdrOffset字节抵消后，立即将过去的日志记录写入journal-header前的主日志里。这是需要在保存点回滚。
**
*/
typedef struct PagerSavepoint PagerSavepoint;
struct PagerSavepoint {
  i64 iOffset;                 /* Starting offset in main journal */
  i64 iHdrOffset;              /* See above */
  Bitvec *pInSavepoint;        /* Set of pages in this savepoint */
  Pgno nOrig;                  /* Original number of pages in file */
  Pgno iSubRec;                /* Index of first record in sub-journal */
#ifndef SQLITE_OMIT_WAL
  u32 aWalData[WAL_SAVEPOINT_NDATA];        /* WAL savepoint context */
#endif
};

/*
** A open page cache is an instance of struct Pager. A description of
** some of the more important member variables follows:
**
**一个打开的页面缓存是Pager结构体的一个实例。一些更重要的成员变量的描述如下:
** eState
**
**   The current 'state' of the pager object. See the comment and state
**   diagram above for a description of the pager state.
**
**Pager的对象的当前状态可以看看到上面的评论和状态图的描述。
** eLock
**
**   For a real on-disk database, the current lock held on the database file -
**   NO_LOCK, SHARED_LOCK, RESERVED_LOCK or EXCLUSIVE_LOCK.
**
**对于一个真正的磁盘数据库,数据库文件状态包括没有锁,共享锁,保留锁或独占锁。
**   For a temporary or in-memory database (neither of which require any
**   locks), this variable is always set to EXCLUSIVE_LOCK. Since such
**   databases always have Pager.exclusiveMode==1, this tricks the pager
**   logic into thinking that it already has all the locks it will ever
**   need (and no reason to release them).
**
**临时或内存数据库(既不需要一个锁),该变量总是设置为独占锁。因为这类数据库
总是有页面。当exclusiveMode = = 1,逻辑上认为它已经有了所有需要的锁（没有理由释放)。
**   In some (obscure) circumstances, this variable may also be set to
**   UNKNOWN_LOCK. See the comment above the #define of UNKNOWN_LOCK for
**   details.
**
**在某些(模糊的)情况下,这个变量也可以被设置为未知的锁。看到上面的解释是关于定义不明的锁的细节。
** changeCountDone
**
**   This boolean variable is used to make sure that the change-counter 
**   (the 4-byte header field at byte offset 24 of the database file) is 
**   not updated more often than necessary. 
<<<<<<< HEAD
=======
**
**这个布尔变量用于确保change-counter(4字节的头字段在数据库文件中代替24字节)不超过必要的更新
>>>>>>> da6e9bfe7165b689ad9006b67395edebe64ecd6c
**   It is set to true when the change-counter field is updated, which 
**   can only happen if an exclusive lock is held on the database file.
**   It is cleared (set to false) whenever an exclusive lock is 
**   relinquished on the database file. Each time a transaction is committed,
**   The changeCountDone flag is inspected. If it is true, the work of
**   updating the change-counter is omitted for the current transaction.
<<<<<<< HEAD
=======
**
**当change-counter字段更新时它被设置为true,仅仅当独占锁在数据库文件中。
**它被清除(设置为false)独占锁时放弃了数据库文件。每次提交一个事务,
**changeCountDone标志被检查。如果它是真的,更新change-counter的工作省略了对当前事务的操作。
>>>>>>> da6e9bfe7165b689ad9006b67395edebe64ecd6c
**   This mechanism means that when running in exclusive mode, a connection 
**   need only update the change-counter once, for the first transaction
**   committed.
**
**这种机制意味着以独占模式运行时,只需要更新连接change-counter一次,第一次事务提交。
** setMaster
**
**   When PagerCommitPhaseOne() is called to commit a transaction, it may
**   (or may not) specify a master-journal name to be written into the 
**   journal file before it is synced to disk.
**
**　当PagerCommitPhaseOne()提交一个事务,它可能(也可能不)指定一个master-journal名字被写入日志文件在它被同步到磁盘之前。
**   Whether or not a journal file contains a master-journal pointer affects 
**   the way in which the journal file is finalized after the transaction is 
**   committed or rolled back when running in "journal_mode=PERSIST" mode.
**   If a journal file does not contain a master-journal pointer, it is
**   finalized by overwriting the first journal header with zeroes. If
**   it does contain a master-journal pointer the journal file is finalized 
**   by truncating it to zero bytes, just as if the connection were 
**   running in "journal_mode=truncate" mode.
**
**日志文件包含一个master-journal指针是否影响
**在事务提交或回滚之后日志文件的完成,在“journal_mode =PERSIST”模式下运行。
**如果日志文件不包含master-journal指针,它最终通过用0覆盖日志文件头。
**如果它包含master-journal指针，文件通过删除零字节,正如如果连接在“journal_mode =truncate”模式下运行。
**   Journal files that contain master journal pointers cannot be finalized
**   simply by overwriting the first journal-header with zeroes, as the
**   master journal pointer could interfere with hot-journal rollback of any
**   subsequently interrupted transaction that reuses the journal file.
**
**包含指针的日志文件不能通过用0覆盖journal-header,因为指针可能干扰hot-journal中断事务的回滚或重用日志文件。
**   The flag is cleared as soon as the journal file is finalized (either
**   by PagerCommitPhaseTwo or PagerRollback). If an IO error prevents the
**   journal file from being successfully finalized, the setMaster flag
**   is cleared anyway (and the pager will move to ERROR state).
**
**当日志文件一被定义标志之会立刻被清除。如果输入错误导致日志文件不能正确的定义，setMaster无论如何都会被清除。
** doNotSpill, doNotSyncSpill
**
**   These two boolean variables control the behaviour of cache-spills
**   (calls made by the pcache module to the pagerStress() routine to
**   write cached data to the file-system in order to free up memory).
**
**这两个布尔变量控制cache-spills的行为
**   When doNotSpill is non-zero, writing to the database from pagerStress()
**   is disabled altogether. This is done in a very obscure case that
**   comes up during savepoint rollback that requires the pcache module
**   to allocate a new page to prevent the journal file from being written
**   while it is being traversed by code in pager_playback().
** 
**当doNotSpill不等于0时,从pagerStress()写入数据库完全是禁用的。这样做是在一个非常
**模糊的情况下,在保存点回滚,要求pcache模块分配一个新页面防止日志文件被写入，虽然
**是由pager_playback()遍历代码。
**   If doNotSyncSpill is non-zero, writing to the database from pagerStress()
**   is permitted, but syncing the journal file is not. This flag is set
**   by sqlite3PagerWrite() when the file-system sector-size is larger than
**   the database page-size in order to prevent a journal sync from happening 
**   in between the journalling of two pages on the same sector. 
**
**如果doNotSyncSpill不为0,从pagerStress()写入数据库是允许的,但同步文件是不与允许的。
**设定这个标志的是sqlite3PagerWrite(),当文件系统扇区大小是大于数据库页面大小以防止
**同步日志之间的发生在两个页面在同一个扇区。
** subjInMemory
**
**   This is a boolean variable. If true, then any required sub-journal
**   is opened as an in-memory journal file. If false, then in-memory
**   sub-journals are only used for in-memory pager files.
**
**这是一个布尔变量。如果这是真的,那么任何需要sub-journal打开内存中的一个日志文件。如果错误,那么内存sub-journals只是用于内存页面文件。
**   This variable is updated by the upper layer each time a new 
**   write-transaction is opened.
**
**这个变量是由上层更新，每次跟新都会有一个新的写事务被打开。
** dbSize, dbOrigSize, dbFileSize
**
**   Variable dbSize is set to the number of pages in the database file.
**   It is valid in PAGER_READER and higher states (all states except for
**   OPEN and ERROR). 
**
**变量dbSize将被设置成数据库文件的页面数量。
**   dbSize is set based on the size of the database file, which may be 
**   larger than the size of the database (the value stored at offset
**   28 of the database header by the btree). If the size of the file
**   is not an integer multiple of the page-size, the value stored in
**   dbSize is rounded down (i.e. a 5KB file with 2K page-size has dbSize==2).
**   Except, any file that is greater than 0 bytes in size is considered
**   to have at least one page. (i.e. a 1KB file with 2K page-size leads
**   to dbSize==1).
**
**dbSize设置基于数据库文件的大小,可能比数据库的大小大。如果文件的大小不是一个
**页面大小的整数倍数的值存储在dbSize则四舍五入。任何文件大小大于0字节被认为至少有一页。
**   During a write-transaction, if pages with page-numbers greater than
**   dbSize are modified in the cache, dbSize is updated accordingly.
**   Similarly, if the database is truncated using PagerTruncateImage(), 
**   dbSize is updated.
**
**在写事务,在缓存中如果页面的页码数大于dbSize将会修改修,dbSize相应更新。同样,如果数据库是使用PagerTruncateImage(),dbSize更新。
**   Variables dbOrigSize and dbFileSize are valid in states 
**   PAGER_WRITER_LOCKED and higher. dbOrigSize is a copy of the dbSize
**   variable at the start of the transaction. It is used during rollback,
**   and to determine whether or not pages need to be journalled before
**   being modified.
**
**变量dbOrigSize和dbFileSize PAGER_WRITER_LOCKED在这些部分中最有效。dbOrigSize dbSize变量的副本在事务开始的时候被创建。期间使用回滚,并确定是否需要日志页面之前修改
**   Throughout a write-transaction, dbFileSize contains the size of
**   the file on disk in pages. It is set to a copy of dbSize when the
**   write-transaction is first opened, and updated when VFS calls are made
**   to write or truncate the database file on disk. 
**
**在写事务,dbFileSize包含页面的磁盘上的文件的大小。它将一份dbSize写事务是第一次打开时,和更新VFS调用时写或截断磁盘上的数据库文件。
**   The only reason the dbFileSize variable is required is to suppress 
**   unnecessary calls to xTruncate() after committing a transaction. If, 
**   when a transaction is committed, the dbFileSize variable indicates 
**   that the database file is larger than the database image (Pager.dbSize), 
**   pager_truncate() is called. The pager_truncate() call uses xFilesize()
**   to measure the database file on disk, and then truncates it if required.
**   dbFileSize is not used when rolling back a transaction. In this case
**   pager_truncate() is called unconditionally (which means there may be
**   a call to xFilesize() that is not strictly required). In either case,
**   pager_truncate() may cause the file to become smaller or larger.
**
**dbFileSize变量的作用是是不必调用xTruncate()在提交一个事务之后。
**事务提交之后,如果dbFileSize变量表明数据库文件大于数据库映像。
**pager_truncate()调用xFilesize()来测量磁盘上的数据库文件。
**当事务回滚时dbFileSize不使用。
**在这两种情况下,pager_truncate()可能会导致文件变得更小或更大。
** dbHintSize
**
**   The dbHintSize variable is used to limit the number of calls made to
**   the VFS xFileControl(FCNTL_SIZE_HINT) method. 
**
**   dbHintSize is set to a copy of the dbSize variable when a
**   write-transaction is opened (at the same time as dbFileSize and
**   dbOrigSize). If the xFileControl(FCNTL_SIZE_HINT) method is called,
**   dbHintSize is increased to the number of pages that correspond to the
**   size-hint passed to the method call. See pager_write_pagelist() for 
**   details.
** 
**dbHintSiz将复制dbSize变量当写事务打开时。如果xFileControl(FCNTL_SIZE_HINT)方法
**被调用时,dbHintSize增加的页数,对应size-hint传递到方法调用。详情见pager_write_pagelist()。
** errCode
**
**   The Pager.errCode variable is only ever used in PAGER_ERROR state. It
**   is set to zero in all other states. In PAGER_ERROR state, Pager.errCode 
**   is always set to SQLITE_FULL, SQLITE_IOERR or one of the SQLITE_IOERR_XXX 
**   sub-codes.仅仅用在PAGER_ERROR状态。
*/
struct Pager {
  sqlite3_vfs *pVfs;          /* OS functions to use for IO */
  u8 exclusiveMode;           /* Boolean. True if locking_mode==EXCLUSIVE */
  u8 journalMode;             /* One of the PAGER_JOURNALMODE_* values */
  u8 useJournal;              /* Use a rollback journal on this file */
  u8 noSync;                  /* Do not sync the journal if true */
  u8 fullSync;                /* Do extra syncs of the journal for robustness */
  u8 ckptSyncFlags;           /* SYNC_NORMAL or SYNC_FULL for checkpoint */
  u8 walSyncFlags;            /* SYNC_NORMAL or SYNC_FULL for wal writes */
  u8 syncFlags;               /* SYNC_NORMAL or SYNC_FULL otherwise */
  u8 tempFile;                /* zFilename is a temporary file */
  u8 readOnly;                /* True for a read-only database */
  u8 memDb;                   /* True to inhibit all file I/O */

  /**************************************************************************
  ** The following block contains those class members that change during
  ** routine opertion.  Class members not in this block are either fixed
  ** when the pager is first created or else only change when there is a
  ** significant mode change (such as changing the page_size, locking_mode,
  ** or the journal_mode).  From another view, these class members describe
  ** the "state" of the pager, while other class members describe the
  ** "configuration" of the pager.
  下面的代码块包含了在程序运行时会改变的成员变量。当页面第一次被创建或者有重要的状态改变了（例如页面的大小，锁，或者日志模式）。
  换句话说，这些成员变量描述了页面的状态，其他的成员变量描述了页面的结构。
  */
  u8 eState;                  /* Pager state (OPEN, READER, WRITER_LOCKED..) */
  u8 eLock;                   /* Current lock held on database file */
  u8 changeCountDone;         /* Set after incrementing the change-counter */
  u8 setMaster;               /* True if a m-j name has been written to jrnl */
  u8 doNotSpill;              /* Do not spill the cache when non-zero */
  u8 doNotSyncSpill;          /* Do not do a spill that requires jrnl sync */
  u8 subjInMemory;            /* True to use in-memory sub-journals */
  Pgno dbSize;                /* Number of pages in the database */
  Pgno dbOrigSize;            /* dbSize before the current transaction */
  Pgno dbFileSize;            /* Number of pages in the database file */
  Pgno dbHintSize;            /* Value passed to FCNTL_SIZE_HINT call */
  int errCode;                /* One of several kinds of errors */
  int nRec;                   /* Pages journalled since last j-header written */
  u32 cksumInit;              /* Quasi-random value added to every checksum */
  u32 nSubRec;                /* Number of records written to sub-journal */
  Bitvec *pInJournal;         /* One bit for each page in the database file */ /*数据库文件的每一页中的一位*/
  sqlite3_file *fd;           /* File descriptor for database */
  sqlite3_file *jfd;          /* File descriptor for main journal */
  sqlite3_file *sjfd;         /* File descriptor for sub-journal */
  i64 journalOff;             /* Current write offset in the journal file */
  i64 journalHdr;             /* Byte offset to previous journal header */
  sqlite3_backup *pBackup;    /* Pointer to list of ongoing backup processes */
  PagerSavepoint *aSavepoint; /* Array of active savepoints */
  int nSavepoint;             /* Number of elements in aSavepoint[] */
  char dbFileVers[16];        /* Changes whenever database file changes */
  /*
  ** End of the routinely-changing class members
  ***************************************************************************/

  u16 nExtra;                 /* Add this many bytes to each in-memory page */
  i16 nReserve;               /* Number of unused bytes at end of each page */
  u32 vfsFlags;               /* Flags for sqlite3_vfs.xOpen() */
  u32 sectorSize;             /* Assumed sector size during rollback */
  int pageSize;               /* Number of bytes in a page */
  Pgno mxPgno;                /* Maximum allowed size of the database */
  i64 journalSizeLimit;       /* Size limit for persistent journal files */
  char *zFilename;            /* Name of the database file */
  char *zJournal;             /* Name of the journal file */
  int (*xBusyHandler)(void*); /* Function to call when busy */
  void *pBusyHandlerArg;      /* Context argument for xBusyHandler */
  int aStat[3];               /* Total cache hits, misses and writes */
#ifdef SQLITE_TEST
  int nRead;                  /* Database pages read */
#endif
  void (*xReiniter)(DbPage*); /* Call this routine when reloading pages */
#ifdef SQLITE_HAS_CODEC
  void *(*xCodec)(void*,void*,Pgno,int); /* Routine for en/decoding data */
  void (*xCodecSizeChng)(void*,int,int); /* Notify of page size changes */
  void (*xCodecFree)(void*);             /* Destructor for the codec */
  void *pCodec;               /* First argument to xCodec... methods */
#endif
  char *pTmpSpace;            /* Pager.pageSize bytes of space for tmp use */
  PCache *pPCache;            /* Pointer to page cache object */
#ifndef SQLITE_OMIT_WAL
  Wal *pWal;                  /* Write-ahead log used by "journal_mode=wal" */
  char *zWal;                 /* File name for write-ahead log */
#endif
};

/*
** Indexes for use with Pager.aStat[]. The Pager.aStat[] array contains
** the values accessed by passing SQLITE_DBSTATUS_CACHE_HIT, CACHE_MISS 
** or CACHE_WRITE to sqlite3_db_status().
**索引是和Pager.aStat[]一起使用。
*/
#define PAGER_STAT_HIT   0
#define PAGER_STAT_MISS  1
#define PAGER_STAT_WRITE 2

/*
** The following global variables hold counters used for
** testing purposes only.  These variables do not exist in
** a non-testing build.  These variables are not thread-safe.
**下面的全局变量保存计数器仅用于测试目的。在非测试构建这些变量不存在。这些变量不是线程安全的
*/
#ifdef SQLITE_TEST
int sqlite3_pager_readdb_count = 0;    /* Number of full pages read from DB */
int sqlite3_pager_writedb_count = 0;   /* Number of full pages written to DB */
int sqlite3_pager_writej_count = 0;    /* Number of pages written to journal */
# define PAGER_INCR(v)  v++
#else
# define PAGER_INCR(v)
#endif



/*
** Journal files begin with the following magic string.  The data
** was obtained from /dev/random.  It is used only as a sanity check.
**
**日志文件从以下字符串开始。从/dev/random.获得的数据它仅作为一个检查。
** Since version 2.8.0, the journal format contains additional sanity
** checking information.  If the power fails while the journal is being
** written, semi-random garbage data might appear in the journal
** file after power is restored.  If an attempt is then made
** to roll the journal back, the database could be corrupted.  The additional
** sanity checking data is an attempt to discover the garbage in the
** journal and ignore it.
**
** The sanity checking information for the new journal format consists
** of a 32-bit checksum on each page of data.  The checksum covers both
** the page number and the pPager->pageSize bytes of data for the page.
** This cksum is initialized to a 32-bit random value that appears in the
** journal file right after the header.  The random initializer is important,
** because garbage data that appears at the end of a journal is likely
** data that was once in other files that have now been deleted.  If the
** garbage data came from an obsolete journal file, the checksums might
** be correct.  But by initializing the checksum to random value which
** is different for every journal, we minimize that risk.
*/
static const unsigned char aJournalMagic[] = {
  0xd9, 0xd5, 0x05, 0xf9, 0x20, 0xa1, 0x63, 0xd7,
};

/*
** The size of the of each page record in the journal is given by
** the following macro.
**每一页记录的大小在由以下宏给出。
*/
#define JOURNAL_PG_SZ(pPager)  ((pPager->pageSize) + 8)

/*
** The journal header size for this pager. This is usually the same 
** size as a single disk sector. See also setSectorSize().
**该页面日志文件头的大小通畅和单个单曲一样大。参见setSectorSize()。
*/
#define JOURNAL_HDR_SZ(pPager) (pPager->sectorSize)

/*
** The macro MEMDB is true if we are dealing with an in-memory database.
** We do this as a macro so that if the SQLITE_OMIT_MEMORYDB macro is set,
** the value of MEMDB will be a constant and the compiler will optimize
** out code that would never execute.
**宏MEMDB值是真的，如果我们正在处理一个内存中的数据库。我们这样做作为一个宏,如果设置SQLITE_OMIT_MEMORYDB宏,MEMDB的价值将是一个常量,编译器会优化代码不会执行。
*/
#ifdef SQLITE_OMIT_MEMORYDB
# define MEMDB 0
#else
# define MEMDB pPager->memDb
#endif

/*
** The maximum legal page number is (2^31 - 1).
**最大合法的页面大小是2^31 - 1
*/
#define PAGER_MAX_PGNO 2147483647

/*
** The argument to this macro is a file descriptor (type sqlite3_file*).
** Return 0 if it is not open, or non-zero (but not 1) if it is.
**
** This is so that expressions can be written as:
**
**   if( isOpen(pPager->jfd) ){ ...
**
** instead of
**
**   if( pPager->jfd->pMethods ){ ...
*/
#define isOpen(pFd) ((pFd)->pMethods)

/*
** Return true if this pager uses a write-ahead log instead of the usual
** rollback journal. Otherwise false.
**如果这个页面使用写日志而不是通常的回滚日志，返回ture。否则返回false。
*/
#ifndef SQLITE_OMIT_WAL
static int pagerUseWal(Pager *pPager){
  return (pPager->pWal!=0);
}
#else
# define pagerUseWal(x) 0
# define pagerRollbackWal(x) 0
# define pagerWalFrames(v,w,x,y) 0
# define pagerOpenWalIfPresent(z) SQLITE_OK
# define pagerBeginReadTransaction(z) SQLITE_OK
#endif

#ifndef NDEBUG 
/*
** Usage:
**
**   assert( assert_pager_state(pPager) );
**
** This function runs many asserts to try to find inconsistencies in
** the internal state of the Pager object.
**这个函数运行许多断点,试图发现页面对象的内部状态的不一致。
*/
static int assert_pager_state(Pager *p){
  Pager *pPager = p;

  /* State must be valid. */
  assert( p->eState==PAGER_OPEN
       || p->eState==PAGER_READER
       || p->eState==PAGER_WRITER_LOCKED
       || p->eState==PAGER_WRITER_CACHEMOD
       || p->eState==PAGER_WRITER_DBMOD
       || p->eState==PAGER_WRITER_FINISHED
       || p->eState==PAGER_ERROR
  );

  /* Regardless of the current state, a temp-file connection always behaves
  ** as if it has an exclusive lock on the database file. It never updates
  ** the change-counter field, so the changeCountDone flag is always set.
  */
  assert( p->tempFile==0 || p->eLock==EXCLUSIVE_LOCK );
  assert( p->tempFile==0 || pPager->changeCountDone );

  /* If the useJournal flag is clear, the journal-mode must be "OFF". 
  ** And if the journal-mode is "OFF", the journal file must not be open.
  */
  assert( p->journalMode==PAGER_JOURNALMODE_OFF || p->useJournal );
  assert( p->journalMode!=PAGER_JOURNALMODE_OFF || !isOpen(p->jfd) );

  /* Check that MEMDB implies noSync. And an in-memory journal. Since 
  ** this means an in-memory pager performs no IO at all, it cannot encounter 
  ** either SQLITE_IOERR or SQLITE_FULL during rollback or while finalizing 
  ** a journal file. (although the in-memory journal implementation may 
  ** return SQLITE_IOERR_NOMEM while the journal file is being written). It 
  ** is therefore not possible for an in-memory pager to enter the ERROR 
  ** state.
  */
  if( MEMDB ){
    assert( p->noSync );
    assert( p->journalMode==PAGER_JOURNALMODE_OFF 
         || p->journalMode==PAGER_JOURNALMODE_MEMORY 
    );
    assert( p->eState!=PAGER_ERROR && p->eState!=PAGER_OPEN );
    assert( pagerUseWal(p)==0 );
  }

  /* If changeCountDone is set, a RESERVED lock or greater must be held
  ** on the file.
  */
  assert( pPager->changeCountDone==0 || pPager->eLock>=RESERVED_LOCK );
  assert( p->eLock!=PENDING_LOCK );

  switch( p->eState ){
    case PAGER_OPEN:
      assert( !MEMDB );
      assert( pPager->errCode==SQLITE_OK );
      assert( sqlite3PcacheRefCount(pPager->pPCache)==0 || pPager->tempFile );
      break;

    case PAGER_READER:
      assert( pPager->errCode==SQLITE_OK );
      assert( p->eLock!=UNKNOWN_LOCK );
      assert( p->eLock>=SHARED_LOCK );
      break;

    case PAGER_WRITER_LOCKED:
      assert( p->eLock!=UNKNOWN_LOCK );
      assert( pPager->errCode==SQLITE_OK );
      if( !pagerUseWal(pPager) ){
        assert( p->eLock>=RESERVED_LOCK );
      }
      assert( pPager->dbSize==pPager->dbOrigSize );
      assert( pPager->dbOrigSize==pPager->dbFileSize );
      assert( pPager->dbOrigSize==pPager->dbHintSize );
      assert( pPager->setMaster==0 );
      break;

    case PAGER_WRITER_CACHEMOD:
      assert( p->eLock!=UNKNOWN_LOCK );
      assert( pPager->errCode==SQLITE_OK );
      if( !pagerUseWal(pPager) ){
        /* It is possible that if journal_mode=wal here that neither the
        ** journal file nor the WAL file are open. This happens during
        ** a rollback transaction that switches from journal_mode=off
        ** to journal_mode=wal.
        */
        assert( p->eLock>=RESERVED_LOCK );
        assert( isOpen(p->jfd) 
             || p->journalMode==PAGER_JOURNALMODE_OFF 
             || p->journalMode==PAGER_JOURNALMODE_WAL 
        );
      }
      assert( pPager->dbOrigSize==pPager->dbFileSize );
      assert( pPager->dbOrigSize==pPager->dbHintSize );
      break;

    case PAGER_WRITER_DBMOD:
      assert( p->eLock==EXCLUSIVE_LOCK );
      assert( pPager->errCode==SQLITE_OK );
      assert( !pagerUseWal(pPager) );
      assert( p->eLock>=EXCLUSIVE_LOCK );
      assert( isOpen(p->jfd) 
           || p->journalMode==PAGER_JOURNALMODE_OFF 
           || p->journalMode==PAGER_JOURNALMODE_WAL 
      );
      assert( pPager->dbOrigSize<=pPager->dbHintSize );
      break;

    case PAGER_WRITER_FINISHED:
      assert( p->eLock==EXCLUSIVE_LOCK );
      assert( pPager->errCode==SQLITE_OK );
      assert( !pagerUseWal(pPager) );
      assert( isOpen(p->jfd) 
           || p->journalMode==PAGER_JOURNALMODE_OFF 
           || p->journalMode==PAGER_JOURNALMODE_WAL 
      );
      break;

    case PAGER_ERROR:
      /* There must be at least one outstanding reference to the pager if
      ** in ERROR state. Otherwise the pager should have already dropped
      ** back to OPEN state.
      */
      assert( pPager->errCode!=SQLITE_OK );
      assert( sqlite3PcacheRefCount(pPager->pPCache)>0 );
      break;
  }

  return 1;
}
#endif /* ifndef NDEBUG */

#ifdef SQLITE_DEBUG 
/*
** Return a pointer to a human readable string in a static buffer
** containing the state of the Pager object passed as an argument. This
** is intended to be used within debuggers. For example, as an alternative
** to "print *pPager" in gdb:
**
**返回一个指向一个可读的字符串的指针在静态缓冲区包含页面的状态对象作为参数传递。这是打算在调试器使用。
** (gdb) printf "%s", print_pager_state(pPager)
*/
static char *print_pager_state(Pager *p){
  static char zRet[1024];

  sqlite3_snprintf(1024, zRet,
      "Filename:      %s\n"
      "State:         %s errCode=%d\n"
      "Lock:          %s\n"
      "Locking mode:  locking_mode=%s\n"
      "Journal mode:  journal_mode=%s\n"
      "Backing store: tempFile=%d memDb=%d useJournal=%d\n"
      "Journal:       journalOff=%lld journalHdr=%lld\n"
      "Size:          dbsize=%d dbOrigSize=%d dbFileSize=%d\n"
      , p->zFilename
      , p->eState==PAGER_OPEN            ? "OPEN" :
        p->eState==PAGER_READER          ? "READER" :
        p->eState==PAGER_WRITER_LOCKED   ? "WRITER_LOCKED" :
        p->eState==PAGER_WRITER_CACHEMOD ? "WRITER_CACHEMOD" :
        p->eState==PAGER_WRITER_DBMOD    ? "WRITER_DBMOD" :
        p->eState==PAGER_WRITER_FINISHED ? "WRITER_FINISHED" :
        p->eState==PAGER_ERROR           ? "ERROR" : "?error?"
      , (int)p->errCode
      , p->eLock==NO_LOCK         ? "NO_LOCK" :
        p->eLock==RESERVED_LOCK   ? "RESERVED" :
        p->eLock==EXCLUSIVE_LOCK  ? "EXCLUSIVE" :
        p->eLock==SHARED_LOCK     ? "SHARED" :
        p->eLock==UNKNOWN_LOCK    ? "UNKNOWN" : "?error?"
      , p->exclusiveMode ? "exclusive" : "normal"
      , p->journalMode==PAGER_JOURNALMODE_MEMORY   ? "memory" :
        p->journalMode==PAGER_JOURNALMODE_OFF      ? "off" :
        p->journalMode==PAGER_JOURNALMODE_DELETE   ? "delete" :
        p->journalMode==PAGER_JOURNALMODE_PERSIST  ? "persist" :
        p->journalMode==PAGER_JOURNALMODE_TRUNCATE ? "truncate" :
        p->journalMode==PAGER_JOURNALMODE_WAL      ? "wal" : "?error?"
      , (int)p->tempFile, (int)p->memDb, (int)p->useJournal
      , p->journalOff, p->journalHdr
      , (int)p->dbSize, (int)p->dbOrigSize, (int)p->dbFileSize
  );

  return zRet;
}
#endif

/*
** Return true if it is necessary to write page *pPg into the sub-journal.
** A page needs to be written into the sub-journal if there exists one
** or more open savepoints for which:
**
**   * The page-number is less than or equal to PagerSavepoint.nOrig, and
**   * The bit corresponding to the page-number is not set in
**     PagerSavepoint.pInSavepoint.
*/
static int subjRequiresPage(PgHdr *pPg){
  Pgno pgno = pPg->pgno;
  Pager *pPager = pPg->pPager;
  int i;
  for(i=0; i<pPager->nSavepoint; i++){
    PagerSavepoint *p = &pPager->aSavepoint[i];
    if( p->nOrig>=pgno && 0==sqlite3BitvecTest(p->pInSavepoint, pgno) ){
      return 1;
    }
  }
  return 0;
}

/*
** Return true if the page is already in the journal file.
*/
static int pageInJournal(PgHdr *pPg){
  return sqlite3BitvecTest(pPg->pPager->pInJournal, pPg->pgno);
}

/*
** Read a 32-bit integer from the given file descriptor.  Store the integer
** that is read in *pRes.  Return SQLITE_OK if everything worked, or an
** error code is something goes wrong.
**
** All values are stored on disk as big-endian.
*/
static int read32bits(sqlite3_file *fd, i64 offset, u32 *pRes){
  unsigned char ac[4];
  int rc = sqlite3OsRead(fd, ac, sizeof(ac), offset);
  if( rc==SQLITE_OK ){
    *pRes = sqlite3Get4byte(ac);
  }
  return rc;
}

/*
** Write a 32-bit integer into a string buffer in big-endian byte order.
*/
#define put32bits(A,B)  sqlite3Put4byte((u8*)A,B)


/*
** Write a 32-bit integer into the given file descriptor.  Return SQLITE_OK
** on success or an error code is something goes wrong.
*/
static int write32bits(sqlite3_file *fd, i64 offset, u32 val){
  char ac[4];
  put32bits(ac, val);
  return sqlite3OsWrite(fd, ac, 4, offset);
}

/*
** Unlock the database file to level eLock, which must be either NO_LOCK
** or SHARED_LOCK. Regardless of whether or not the call to xUnlock()
** succeeds, set the Pager.eLock variable to match the (attempted) new lock.
**
** Except, if Pager.eLock is set to UNKNOWN_LOCK when this function is
** called, do not modify it. See the comment above the #define of 
** UNKNOWN_LOCK for an explanation of this.
**打开数据库文件eLock,必须使用NO_LOCK或SHARED_LOCK。
**不管是否调用xUnlock()成功,eLock变量匹配新锁。
**UNKNOWN_LOCK调用这个函数时,不要修改elock。
**# define UNKNOWN_LOCK作出定义。
*/
static int pagerUnlockDb(Pager *pPager, int eLock){
  int rc = SQLITE_OK;

  assert( !pPager->exclusiveMode || pPager->eLock==eLock );
  assert( eLock==NO_LOCK || eLock==SHARED_LOCK );
  assert( eLock!=NO_LOCK || pagerUseWal(pPager)==0 );
  if( isOpen(pPager->fd) ){
    assert( pPager->eLock>=eLock );
    rc = sqlite3OsUnlock(pPager->fd, eLock);
    if( pPager->eLock!=UNKNOWN_LOCK ){
      pPager->eLock = (u8)eLock;
    }
    IOTRACE(("UNLOCK %p %d\n", pPager, eLock))
  }
  return rc;
}

/*
** Lock the database file to level eLock, which must be either SHARED_LOCK,
** RESERVED_LOCK or EXCLUSIVE_LOCK. If the caller is successful, set the
** Pager.eLock variable to the new locking state. 
**
**锁定数据库文件eLock,必须要有SHARED_LOCK,RESERVED_LOCK或EXCLUSIVE_LOCK。如果调用成功,设置页面的eLock变量为新的锁定状态。
** Except, if Pager.eLock is set to UNKNOWN_LOCK when this function is 
** called, do not modify it unless the new locking state is EXCLUSIVE_LOCK. 
** See the comment above the #define of UNKNOWN_LOCK for an explanation 
** of this.
*/
static int pagerLockDb(Pager *pPager, int eLock){
  int rc = SQLITE_OK;

  assert( eLock==SHARED_LOCK || eLock==RESERVED_LOCK || eLock==EXCLUSIVE_LOCK );
  if( pPager->eLock<eLock || pPager->eLock==UNKNOWN_LOCK ){
    rc = sqlite3OsLock(pPager->fd, eLock);
    if( rc==SQLITE_OK && (pPager->eLock!=UNKNOWN_LOCK||eLock==EXCLUSIVE_LOCK) ){
      pPager->eLock = (u8)eLock;
      IOTRACE(("LOCK %p %d\n", pPager, eLock))
    }
  }
  return rc;
}

/*
** This function determines whether or not the atomic-write optimization
** can be used with this pager. The optimization can be used if:
**
**  (a) the value returned by OsDeviceCharacteristics() indicates that
**      a database page may be written atomically, and
**  (b) the value returned by OsSectorSize() is less than or equal
**      to the page size.
**
** The optimization is also always enabled for temporary files. It is
** an error to call this function if pPager is opened on an in-memory
** database.
**
** If the optimization cannot be used, 0 is returned. If it can be used,
** then the value returned is the size of the journal file when it
** contains rollback data for exactly one page.
*/
#ifdef SQLITE_ENABLE_ATOMIC_WRITE
static int jrnlBufferSize(Pager *pPager){
  assert( !MEMDB );
  if( !pPager->tempFile ){
    int dc;                           /* Device characteristics */
    int nSector;                      /* Sector size */
    int szPage;                       /* Page size */

    assert( isOpen(pPager->fd) );
    dc = sqlite3OsDeviceCharacteristics(pPager->fd);
    nSector = pPager->sectorSize;
    szPage = pPager->pageSize;

    assert(SQLITE_IOCAP_ATOMIC512==(512>>8));
    assert(SQLITE_IOCAP_ATOMIC64K==(65536>>8));
    if( 0==(dc&(SQLITE_IOCAP_ATOMIC|(szPage>>8)) || nSector>szPage) ){
      return 0;
    }
  }

  return JOURNAL_HDR_SZ(pPager) + JOURNAL_PG_SZ(pPager);
}
#endif

/*
** If SQLITE_CHECK_PAGES is defined then we do some sanity checking
** on the cache using a hash function.  This is used for testing
** and debugging only.
*/
#ifdef SQLITE_CHECK_PAGES
/*
** Return a 32-bit hash of the page data for pPage.
*/
static u32 pager_datahash(int nByte, unsigned char *pData){
  u32 hash = 0;
  int i;
  for(i=0; i<nByte; i++){
    hash = (hash*1039) + pData[i];
  }
  return hash;
}
static u32 pager_pagehash(PgHdr *pPage){
  return pager_datahash(pPage->pPager->pageSize, (unsigned char *)pPage->pData);
}
static void pager_set_pagehash(PgHdr *pPage){
  pPage->pageHash = pager_pagehash(pPage);
}

/*
** The CHECK_PAGE macro takes a PgHdr* as an argument. If SQLITE_CHECK_PAGES
** is defined, and NDEBUG is not defined, an assert() statement checks
** that the page is either dirty or still matches the calculated page-hash.
*/
#define CHECK_PAGE(x) checkPage(x)
static void checkPage(PgHdr *pPg){
  Pager *pPager = pPg->pPager;
  assert( pPager->eState!=PAGER_ERROR );
  assert( (pPg->flags&PGHDR_DIRTY) || pPg->pageHash==pager_pagehash(pPg) );
}

#else
#define pager_datahash(X,Y)  0
#define pager_pagehash(X)  0
#define pager_set_pagehash(X)
#define CHECK_PAGE(x)
#endif  /* SQLITE_CHECK_PAGES */

/*
** When this is called the journal file for pager pPager must be open.
** This function attempts to read a master journal file name from the 
** end of the file and, if successful, copies it into memory supplied 
** by the caller. See comments above writeMasterJournal() for the format
** used to store a master journal file name at the end of a journal file.
**
**当日志文件被执行pPager必须处于打开状态。这个函数尝试从文件末尾读取主日志文件名
**，如果成功将其调入内存中。看上面writeMasterJournal评论（）的格式常常在一个日志文
**件的末尾存储一个主日志文件名。
** zMaster must point to a buffer of at least nMaster bytes allocated by
** the caller. This should be sqlite3_vfs.mxPathname+1 (to ensure there is
** enough space to write the master journal name). If the master journal
** name in the journal is longer than nMaster bytes (including a
** nul-terminator), then this is handled as if no master journal name
** were present in the journal.
**
**zMaster必须指向至少nnMaster字节的缓冲区。如果主日志名字比 nMaster字节还长，那么
**如果没有主日志名已经存在日志中这将被处理。
** If a master journal file name is present at the end of the journal
** file, then it is copied into the buffer pointed to by zMaster. A
** nul-terminator byte is appended to the buffer following the master
** journal file name.
**
**如果主日志文件名已经存在日志文件的末尾，然后然后将其复制到缓冲区指向ZMASTER。
**一个终止字符将被添加到缓存区中日志文件的末尾。
** If it is determined that no master journal file name is present 
** zMaster[0] is set to 0 and SQLITE_OK returned.
**
**如果没有确定没有主日志文件名存在，zMaster[0]将被设置成0并且返回SQLITE_OK。
** If an error occurs while reading from the journal file, an SQLite
** error code is returned.
**如果从日志文件读取时发生错误，那么错误代码将被返回。
*/
static int readMasterJournal(sqlite3_file *pJrnl, char *zMaster, u32 nMaster){
  int rc;                    /* Return code */
  u32 len;                   /* Length in bytes of master journal name */
  i64 szJ;                   /* Total size in bytes of journal file pJrnl */
  u32 cksum;                 /* MJ checksum value read from journal */
  u32 u;                     /* Unsigned loop counter */
  unsigned char aMagic[8];   /* A buffer to hold the magic header */
  zMaster[0] = '\0';

  if( SQLITE_OK!=(rc = sqlite3OsFileSize(pJrnl, &szJ))
   || szJ<16
   || SQLITE_OK!=(rc = read32bits(pJrnl, szJ-16, &len))
   || len>=nMaster 
   || SQLITE_OK!=(rc = read32bits(pJrnl, szJ-12, &cksum))
   || SQLITE_OK!=(rc = sqlite3OsRead(pJrnl, aMagic, 8, szJ-8))
   || memcmp(aMagic, aJournalMagic, 8)
   || SQLITE_OK!=(rc = sqlite3OsRead(pJrnl, zMaster, len, szJ-16-len))
  ){
    return rc;
  }

  /* See if the checksum matches the master journal name */
  for(u=0; u<len; u++){
    cksum -= zMaster[u];
  }
  if( cksum ){
    /* If the checksum doesn't add up, then one or more of the disk sectors
    ** containing the master journal filename is corrupted. This means
    ** definitely roll back, so just return SQLITE_OK and report a (nul)
    ** master-journal filename.
    */
    len = 0;
  }
  zMaster[len] = '\0';
   
  return SQLITE_OK;
}

/*
** Return the offset of the sector boundary at or immediately 
** following the value in pPager->journalOff, assuming a sector 
** size of pPager->sectorSize bytes.
**
** i.e for a sector size of 512:
**
**   Pager.journalOff          Return value
**   ---------------------------------------
**   0                         0
**   512                       512
**   100                       512
**   2000                      2048
** 
**立即返回扇区边界的偏移量或pPager - > journalOff值后,假设pPager - > sectorSize字节是扇区大小。
**   Pager.journalOff          Return value
**   ---------------------------------------
**   0                         0
**   512                       512
**   100                       512
**   2000                      2048
*/
static i64 journalHdrOffset(Pager *pPager){
  i64 offset = 0;
  i64 c = pPager->journalOff;
  if( c ){
    offset = ((c-1)/JOURNAL_HDR_SZ(pPager) + 1) * JOURNAL_HDR_SZ(pPager);
  }
  assert( offset%JOURNAL_HDR_SZ(pPager)==0 );
  assert( offset>=c );
  assert( (offset-c)<JOURNAL_HDR_SZ(pPager) );
  return offset;
}

/*
** The journal file must be open when this function is called.
**
**文件必须打开当调用此函数时。
** This function is a no-op if the journal file has not been written to
** within the current transaction (i.e. if Pager.journalOff==0).
**
**这个函数是一个空操作如果日志文件没有被写入当前事务的文件。
** If doTruncate is non-zero or the Pager.journalSizeLimit variable is
** set to 0, then truncate the journal file to zero bytes in size. Otherwise,
** zero the 28-byte header at the start of the journal file. In either case, 
** if the pager is not in no-sync mode, sync the journal file immediately 
** after writing or truncating it.
**
**如果doTruncate非零。journalSizeLimit变量设置为0,然后将日志文件变成零字节。
**否则,28-byte的头日志文件在这两种情况下,在页面转变成no-sync模式后立即同步日志文件写或删除它。
** If Pager.journalSizeLimit is set to a positive, non-zero value, and
** following the truncation or zeroing described above the size of the 
** journal file in bytes is larger than this value, then truncate the
** journal file to Pager.journalSizeLimit bytes. The journal file does
** not need to be synced following this operation.
**
**如果Pager.journalSizeLimit被设置成私有的，非零值。上述日志文件的大小大于这个值,然后截断页面的日志文件上journalSizeLimit字节。
** If an IO error occurs, abandon processing and return the IO error code.
** Otherwise, return SQLITE_OK.
*/
static int zeroJournalHdr(Pager *pPager, int doTruncate){
  int rc = SQLITE_OK;                               /* Return code */
  assert( isOpen(pPager->jfd) );
  if( pPager->journalOff ){
    const i64 iLimit = pPager->journalSizeLimit;    /* Local cache of jsl */

    IOTRACE(("JZEROHDR %p\n", pPager))
    if( doTruncate || iLimit==0 ){
      rc = sqlite3OsTruncate(pPager->jfd, 0);
    }else{
      static const char zeroHdr[28] = {0};
      rc = sqlite3OsWrite(pPager->jfd, zeroHdr, sizeof(zeroHdr), 0);
    }
    if( rc==SQLITE_OK && !pPager->noSync ){
      rc = sqlite3OsSync(pPager->jfd, SQLITE_SYNC_DATAONLY|pPager->syncFlags);
    }

    /* At this point the transaction is committed but the write lock 
    ** is still held on the file. If there is a size limit configured for 
    ** the persistent journal and the journal file currently consumes more
    ** space than that limit allows for, truncate it now. There is no need
    ** to sync the file following this operation.
    */
    if( rc==SQLITE_OK && iLimit>0 ){
      i64 sz;
      rc = sqlite3OsFileSize(pPager->jfd, &sz);
      if( rc==SQLITE_OK && sz>iLimit ){
        rc = sqlite3OsTruncate(pPager->jfd, iLimit);
      }
    }
  }
  return rc;
}

/*
** The journal file must be open when this routine is called. A journal
** header (JOURNAL_HDR_SZ bytes) is written into the journal file at the
** current location.
**
**必须打开日志文件时,调用这个例程。日记头写入日志文件的当前位置。
** The format for the journal header is as follows:
** - 8 bytes: Magic identifying journal format.
** - 4 bytes: Number of records in journal, or -1 no-sync mode is on.
** - 4 bytes: Random number used for page hash.
** - 4 bytes: Initial database page count.
** - 4 bytes: Sector size used by the process that wrote this journal.
** - 4 bytes: Database page size.
** 
** Followed by (JOURNAL_HDR_SZ - 28) bytes of unused space.
**紧随其后的是字节的未使用空间.
*/
static int writeJournalHdr(Pager *pPager){
  int rc = SQLITE_OK;                 /* Return code */
  char *zHeader = pPager->pTmpSpace;  /* Temporary space used to build header */
  u32 nHeader = (u32)pPager->pageSize;/* Size of buffer pointed to by zHeader */
  u32 nWrite;                         /* Bytes of header sector written */
  int ii;                             /* Loop counter */

  assert( isOpen(pPager->jfd) );      /* Journal file must be open. */

  if( nHeader>JOURNAL_HDR_SZ(pPager) ){
    nHeader = JOURNAL_HDR_SZ(pPager);
  }

  /* If there are active savepoints and any of them were created 
  ** since the most recent journal header was written, update the 
  ** PagerSavepoint.iHdrOffset fields now.
  */
  for(ii=0; ii<pPager->nSavepoint; ii++){
    if( pPager->aSavepoint[ii].iHdrOffset==0 ){
      pPager->aSavepoint[ii].iHdrOffset = pPager->journalOff;
    }
  }

  pPager->journalHdr = pPager->journalOff = journalHdrOffset(pPager);

  /* 
  ** Write the nRec Field - the number of page records that follow this
  ** journal header. Normally, zero is written to this value at this time.
  ** After the records are added to the journal (and the journal synced, 
  ** if in full-sync mode), the zero is overwritten with the true number
  ** of records (see syncJournal()).
  **
  ** A faster alternative is to write 0xFFFFFFFF to the nRec field. When
  ** reading the journal this value tells SQLite to assume that the
  ** rest of the journal file contains valid page records. This assumption
  ** is dangerous, as if a failure occurred whilst writing to the journal
  ** file it may contain some garbage data. There are two scenarios
  ** where this risk can be ignored:
  **
  **   * When the pager is in no-sync mode. Corruption can follow a
  **     power failure in this case anyway.
  **
  **   * When the SQLITE_IOCAP_SAFE_APPEND flag is set. This guarantees
  **     that garbage data is never appended to the journal file.
  */
  assert( isOpen(pPager->fd) || pPager->noSync );
  if( pPager->noSync || (pPager->journalMode==PAGER_JOURNALMODE_MEMORY)
   || (sqlite3OsDeviceCharacteristics(pPager->fd)&SQLITE_IOCAP_SAFE_APPEND) 
  ){
    memcpy(zHeader, aJournalMagic, sizeof(aJournalMagic));
    put32bits(&zHeader[sizeof(aJournalMagic)], 0xffffffff);
  }else{
    memset(zHeader, 0, sizeof(aJournalMagic)+4);
  }

  /* The random check-hash initialiser */ 
  sqlite3_randomness(sizeof(pPager->cksumInit), &pPager->cksumInit);
  put32bits(&zHeader[sizeof(aJournalMagic)+4], pPager->cksumInit);
  /* The initial database size */
  put32bits(&zHeader[sizeof(aJournalMagic)+8], pPager->dbOrigSize);
  /* The assumed sector size for this process */
  put32bits(&zHeader[sizeof(aJournalMagic)+12], pPager->sectorSize);

  /* The page size */
  put32bits(&zHeader[sizeof(aJournalMagic)+16], pPager->pageSize);

  /* Initializing the tail of the buffer is not necessary.  Everything
  ** works find if the following memset() is omitted.  But initializing
  ** the memory prevents valgrind from complaining, so we are willing to
  ** take the performance hit.
  */
  memset(&zHeader[sizeof(aJournalMagic)+20], 0,
         nHeader-(sizeof(aJournalMagic)+20));

  /* In theory, it is only necessary to write the 28 bytes that the 
  ** journal header consumes to the journal file here. Then increment the 
  ** Pager.journalOff variable by JOURNAL_HDR_SZ so that the next 
  ** record is written to the following sector (leaving a gap in the file
  ** that will be implicitly filled in by the OS).
  **
  ** However it has been discovered that on some systems this pattern can 
  ** be significantly slower than contiguously writing data to the file,
  ** even if that means explicitly writing data to the block of 
  ** (JOURNAL_HDR_SZ - 28) bytes that will not be used. So that is what
  ** is done. 
  **
  ** The loop is required here in case the sector-size is larger than the 
  ** database page size. Since the zHeader buffer is only Pager.pageSize
  ** bytes in size, more than one call to sqlite3OsWrite() may be required
  ** to populate the entire journal header sector.
  */ 
  for(nWrite=0; rc==SQLITE_OK&&nWrite<JOURNAL_HDR_SZ(pPager); nWrite+=nHeader){
    IOTRACE(("JHDR %p %lld %d\n", pPager, pPager->journalHdr, nHeader))
    rc = sqlite3OsWrite(pPager->jfd, zHeader, nHeader, pPager->journalOff);
    assert( pPager->journalHdr <= pPager->journalOff );
    pPager->journalOff += nHeader;
  }

  return rc;
}

/*
** The journal file must be open when this is called. A journal header file
** (JOURNAL_HDR_SZ bytes) is read from the current location in the journal
** file. The current location in the journal file is given by
** pPager->journalOff. See comments above function writeJournalHdr() for
** a description of the journal header format.
**
**日志文件必须打开当调用这个函数的时候。日记头文件
**从当前位置读取日志文件。的当前位置在《文件pPager - > journalOff。
**参见上面的评论writeJournalHdr()函数描述的日志标题格式。
** If the header is read successfully, *pNRec is set to the number of
** page records following this header and *pDbSize is set to the size of the
** database before the transaction began, in pages. Also, pPager->cksumInit
** is set to the value read from the journal header. SQLITE_OK is returned
** in this case.
**
** If the journal header file appears to be corrupted, SQLITE_DONE is
** returned and *pNRec and *PDbSize are undefined.  If JOURNAL_HDR_SZ bytes
** cannot be read from the journal file an error code is returned.
**如果头读取成功,记录下* pNRec设置页面的数量和* pDbSize，设置数据库的大小在事务开始前。
**此外,pPager - > cksumInit设置为值读取日志标题。在这种情况下SQLITE_OK返回。
*/
static int readJournalHdr(
  Pager *pPager,               /* Pager object */
  int isHot,
  i64 journalSize,             /* Size of the open journal file in bytes */
  u32 *pNRec,                  /* OUT: Value read from the nRec field */
  u32 *pDbSize                 /* OUT: Value of original database size field */
){
  int rc;                      /* Return code */
  unsigned char aMagic[8];     /* A buffer to hold the magic header */
  i64 iHdrOff;                 /* Offset of journal header being read */

  assert( isOpen(pPager->jfd) );      /* Journal file must be open. */

  /* Advance Pager.journalOff to the start of the next sector. If the
  ** journal file is too small for there to be a header stored at this
  ** point, return SQLITE_DONE.
  */
  pPager->journalOff = journalHdrOffset(pPager);
  if( pPager->journalOff+JOURNAL_HDR_SZ(pPager) > journalSize ){
    return SQLITE_DONE;
  }
  iHdrOff = pPager->journalOff;

  /* Read in the first 8 bytes of the journal header. If they do not match
  ** the  magic string found at the start of each journal header, return
  ** SQLITE_DONE. If an IO error occurs, return an error code. Otherwise,
  ** proceed.
  */
  if( isHot || iHdrOff!=pPager->journalHdr ){
    rc = sqlite3OsRead(pPager->jfd, aMagic, sizeof(aMagic), iHdrOff);
    if( rc ){
      return rc;
    }
    if( memcmp(aMagic, aJournalMagic, sizeof(aMagic))!=0 ){
      return SQLITE_DONE;
    }
  }

  /* Read the first three 32-bit fields of the journal header: The nRec
  ** field, the checksum-initializer and the database size at the start
  ** of the transaction. Return an error code if anything goes wrong.
  */
  if( SQLITE_OK!=(rc = read32bits(pPager->jfd, iHdrOff+8, pNRec))
   || SQLITE_OK!=(rc = read32bits(pPager->jfd, iHdrOff+12, &pPager->cksumInit))
   || SQLITE_OK!=(rc = read32bits(pPager->jfd, iHdrOff+16, pDbSize))
  ){
    return rc;
  }

  if( pPager->journalOff==0 ){
    u32 iPageSize;               /* Page-size field of journal header */
    u32 iSectorSize;             /* Sector-size field of journal header */

    /* Read the page-size and sector-size journal header fields. */
    if( SQLITE_OK!=(rc = read32bits(pPager->jfd, iHdrOff+20, &iSectorSize))
     || SQLITE_OK!=(rc = read32bits(pPager->jfd, iHdrOff+24, &iPageSize))
    ){
      return rc;
    }

    /* Versions of SQLite prior to 3.5.8 set the page-size field of the
    ** journal header to zero. In this case, assume that the Pager.pageSize
    ** variable is already set to the correct page size.
    */
    if( iPageSize==0 ){
      iPageSize = pPager->pageSize;
    }

    /* Check that the values read from the page-size and sector-size fields
    ** are within range. To be 'in range', both values need to be a power
    ** of two greater than or equal to 512 or 32, and not greater than their 
    ** respective compile time maximum limits.
    */
    if( iPageSize<512                  || iSectorSize<32
     || iPageSize>SQLITE_MAX_PAGE_SIZE || iSectorSize>MAX_SECTOR_SIZE
     || ((iPageSize-1)&iPageSize)!=0   || ((iSectorSize-1)&iSectorSize)!=0 
    ){
      /* If the either the page-size or sector-size in the journal-header is 
      ** invalid, then the process that wrote the journal-header must have 
      ** crashed before the header was synced. In this case stop reading 
      ** the journal file here.
      */
      return SQLITE_DONE;
    }

    /* Update the page-size to match the value read from the journal. 
    ** Use a testcase() macro to make sure that malloc failure within 
    ** PagerSetPagesize() is tested.
    */
    rc = sqlite3PagerSetPagesize(pPager, &iPageSize, -1);
    testcase( rc!=SQLITE_OK );

    /* Update the assumed sector-size to match the value used by 
    ** the process that created this journal. If this journal was
    ** created by a process other than this one, then this routine
    ** is being called from within pager_playback(). The local value
    ** of Pager.sectorSize is restored at the end of that routine.
    */
    pPager->sectorSize = iSectorSize;
  }

  pPager->journalOff += JOURNAL_HDR_SZ(pPager);
  return rc;
}


/*
** Write the supplied master journal name into the journal file for pager
** pPager at the current location. The master journal name must be the last
** thing written to a journal file. If the pager is in full-sync mode, the
** journal file descriptor is advanced to the next sector boundary before
** anything is written. The format is:
**
**将主日志文件名写入之日文件中pPager所指向的当前所在的位置。
**主日志必须最后写入一个日志文件。如果页面处于同步模式，在一切被写入之前，日志文件的描述符前进到下一个扇区边界。
**格式如下：
**   + 4 bytes: PAGER_MJ_PGNO.
**   + N bytes: Master journal filename in utf-8.
**   + 4 bytes: N (length of master journal name in bytes, no nul-terminator).
**   + 4 bytes: Master journal name checksum.
**   + 8 bytes: aJournalMagic[].
**
**   4个字节：PAGER_MJ_PGNO
**   N个字节：主日志文件名必须使用utf-8
**   4个字节：主日志文件名的长度，没有终止符
**   4个字符：主主日志名校验
**   8个字节：aJournalMagic[]
** The master journal page checksum is the sum of the bytes in the master
** journal name, where each byte is interpreted as a signed 8-bit integer.
**
**主日志校验是所有日志文件名的字节总和，其中每个字节被解释为一个有符号的8位整数。
** If zMaster is a NULL pointer (occurs for a single database transaction), 
** this call is a no-op.
**如果zMaster是一个空指针，这被称为一个无用的操作。
*/
static int writeMasterJournal(Pager *pPager, const char *zMaster){
  int rc;                          /* Return code */
  int nMaster;                     /* Length of string zMaster */
  i64 iHdrOff;                     /* Offset of header in journal file */
  i64 jrnlSize;                    /* Size of journal file on disk */
  u32 cksum = 0;                   /* Checksum of string zMaster */

  assert( pPager->setMaster==0 );
  assert( !pagerUseWal(pPager) );

  if( !zMaster 
   || pPager->journalMode==PAGER_JOURNALMODE_MEMORY 
   || pPager->journalMode==PAGER_JOURNALMODE_OFF 
  ){
    return SQLITE_OK;
  }
  pPager->setMaster = 1;
  assert( isOpen(pPager->jfd) );
  assert( pPager->journalHdr <= pPager->journalOff );

  /* Calculate the length in bytes and the checksum of zMaster */
  for(nMaster=0; zMaster[nMaster]; nMaster++){
    cksum += zMaster[nMaster];
  }

  /* If in full-sync mode, advance to the next disk sector before writing
  ** the master journal name. This is in case the previous page written to
  ** the journal has already been synced.
  */
  if( pPager->fullSync ){
    pPager->journalOff = journalHdrOffset(pPager);
  }
  iHdrOff = pPager->journalOff;

  /* Write the master journal data to the end of the journal file. If
  ** an error occurs, return the error code to the caller.
  */
  if( (0 != (rc = write32bits(pPager->jfd, iHdrOff, PAGER_MJ_PGNO(pPager))))
   || (0 != (rc = sqlite3OsWrite(pPager->jfd, zMaster, nMaster, iHdrOff+4)))
   || (0 != (rc = write32bits(pPager->jfd, iHdrOff+4+nMaster, nMaster)))
   || (0 != (rc = write32bits(pPager->jfd, iHdrOff+4+nMaster+4, cksum)))
   || (0 != (rc = sqlite3OsWrite(pPager->jfd, aJournalMagic, 8, iHdrOff+4+nMaster+8)))
  ){
    return rc;
  }
  pPager->journalOff += (nMaster+20);

  /* If the pager is in peristent-journal mode, then the physical 
  ** journal-file may extend past the end of the master-journal name
  ** and 8 bytes of magic data just written to the file. This is 
  ** dangerous because the code to rollback a hot-journal file
  ** will not be able to find the master-journal name to determine 
  ** whether or not the journal is hot. 
  **
  ** Easiest thing to do in this scenario is to truncate the journal 
  ** file to the required size.
  */ 
  if( SQLITE_OK==(rc = sqlite3OsFileSize(pPager->jfd, &jrnlSize))
   && jrnlSize>pPager->journalOff
  ){
    rc = sqlite3OsTruncate(pPager->jfd, pPager->journalOff);
  }
  return rc;
}

/*
** Find a page in the hash table given its page number. Return
** a pointer to the page or NULL if the requested page is not 
** already in memory.如果请求页面没有在内存中返回一个指针或者空，找出在哈希表给出的页码输的页面。
*/
static PgHdr *pager_lookup(Pager *pPager, Pgno pgno){
  PgHdr *p;                         /* Return value */

  /* It is not possible for a call to PcacheFetch() with createFlag==0 to
  ** fail, since no attempt to allocate dynamic memory will be made.
  */
  (void)sqlite3PcacheFetch(pPager->pPCache, pgno, 0, &p);
  return p;
}

/*
** Discard the entire contents of the in-memory page-cache.
*/
static void pager_reset(Pager *pPager){
  sqlite3BackupRestart(pPager->pBackup);
  sqlite3PcacheClear(pPager->pPCache);
}

/*
** Free all structures in the Pager.aSavepoint[] array and set both
** Pager.aSavepoint and Pager.nSavepoint to zero. Close the sub-journal
** if it is open and the pager is not in exclusive mode.
释放页面里的所有结构体。aSavepoint[]数组和集合并Pager.aSavepoint 和 Pager.nSavepoint清零。
如果sub-journal打开或者页面没有加上排它锁关闭sub-journal。
*/
static void releaseAllSavepoints(Pager *pPager){
  int ii;               /* Iterator for looping through Pager.aSavepoint */
  for(ii=0; ii<pPager->nSavepoint; ii++){
    sqlite3BitvecDestroy(pPager->aSavepoint[ii].pInSavepoint);
  }
  if( !pPager->exclusiveMode || sqlite3IsMemJournal(pPager->sjfd) ){
    sqlite3OsClose(pPager->sjfd);
  }
  sqlite3_free(pPager->aSavepoint);
  pPager->aSavepoint = 0;
  pPager->nSavepoint = 0;
  pPager->nSubRec = 0;
}

/*
** Set the bit number pgno in the PagerSavepoint.pInSavepoint 
** bitvecs of all open savepoints. Return SQLITE_OK if successful
** or SQLITE_NOMEM if a malloc failure occurs.
*/
static int addToSavepointBitvecs(Pager *pPager, Pgno pgno){
  int ii;                   /* Loop counter */
  int rc = SQLITE_OK;       /* Result code */

  for(ii=0; ii<pPager->nSavepoint; ii++){
    PagerSavepoint *p = &pPager->aSavepoint[ii];
    if( pgno<=p->nOrig ){
      rc |= sqlite3BitvecSet(p->pInSavepoint, pgno);
      testcase( rc==SQLITE_NOMEM );
      assert( rc==SQLITE_OK || rc==SQLITE_NOMEM );
    }
  }
  return rc;
}

/*
** This function is a no-op if the pager is in exclusive mode and not
** in the ERROR state. Otherwise, it switches the pager to PAGER_OPEN
** state.如果pager是在独占模式而且不是在异常状态，这个函数是个空操作。否则的话，它将使pager转换到打开状态。
**
** If the pager is not in exclusive-access mode, the database file is completely unlocked. 
**If the file is unlocked and the file-system does
** not exhibit the UNDELETABLE_WHEN_OPEN property, the journal file is
** closed (if it is open).如果pager不是在独占访问，数据库文件将会完全解锁。如果文件是开放的，文件系统不会在打开时不能删除的特点，这个日志文件将会被关闭。
**
** If the pager is in ERROR state when this function is called, the 
** contents of the pager cache are discarded before switching back to 
** the OPEN state.若这个函数被调用时pager是在异常状态，它缓存中的文件在转回打开状态之前将会被丢弃。
 Regardless of whether the pager is in exclusive-mode
** or not, any journal file left in the file-system will be treated
** as a hot-journal and rolled back the next time a read-transaction
** is opened (by this or by any other connection).不管pager是否处于独占模式
与否，留在该文件系统的任何日志文件将被处理
作为热日志，下一次回滚一个读事务
 被打开（通过这个或通过任何其它连接）。
*/
static void pager_unlock(Pager *pPager){

  assert( pPager->eState==PAGER_READER 
       || pPager->eState==PAGER_OPEN 
       || pPager->eState==PAGER_ERROR 
  ); /* assert() 是宏，而不是函数，assert(int expression)的作用是判断expression的值是否为假 若为假，
  则他先向标准错误流stderr打印一条出错信息，然后通过调用abort来终止程序运行，否则无任何作用。其作用就是用于确认程序的正常操作，
  即若pPager是在读，打开，异常的状态下就执行下面的代码*/

  sqlite3BitvecDestroy(pPager->pInJournal);/* 摧毁这个位图，回收所有的内存使用*/
  pPager->pInJournal = 0;
  releaseAllSavepoints(pPager);/*释放Pager.aSavepoint[]数组中所有的结构，
把Pager.aSavepoint and Pager.nSavepoint都设置为0,若pager不是在独占模式而且是打开的，则要关闭这个日志。*/

  if( pagerUseWal(pPager) ){     
    assert( !isOpen(pPager->jfd) );
    sqlite3WalEndReadTransaction(pPager->pWal);
    pPager->eState = PAGER_OPEN;/*如果pPager用的是写日志，pPager的主要指日文件没有打开。则完成一个读操作，释放读锁，pPager设置为打开状态*/
  }else if( !pPager->exclusiveMode ){           /*如果pPager用的不是写日志，而且不是在独占模式，则执行*/
                  
    int rc;                       /* Error code returned by pagerUnlockDb() */
    int iDc = isOpen(pPager->fd)?sqlite3OsDeviceCharacteristics(pPager->fd):0;/*若pPager数据库文件打开了，则返回打开文件的位图给变量iDc，否则返回0给变量iDc*/

    /* If the operating system support deletion of open files, then
    ** close the journal file when dropping the database lock.  Otherwise
    ** another connection with journal_mode=delete might delete the file
    ** out from under us.  如果操作系统支持打开文件的删除，当删除数据库锁时将关闭日志文件。否则另一个journal_mode=delete的连接将会从我们下面删除文件。
    */
    assert( (PAGER_JOURNALMODE_MEMORY   & 5)!=1 );/*若PAGER_JOURNALMODE_MEMORY与5进行&计算的值不为1，则程序继续执行，否则停止*/
    assert( (PAGER_JOURNALMODE_OFF      & 5)!=1 );/*若PAGER_JOURNALMODE_OFF与5进行&计算的值不为1，则程序继续执行，否则停止*/
    assert( (PAGER_JOURNALMODE_WAL      & 5)!=1 );/*若PAGER_JOURNALMODE_WAL进行&计算的值不为1，则程序继续执行，否则停止*/
    assert( (PAGER_JOURNALMODE_DELETE   & 5)!=1 );/*若PAGER_JOURNALMODE_DELETE进行&计算的值为1，则程序继续执行，否则停止*/
    assert( (PAGER_JOURNALMODE_TRUNCATE & 5)==1 );/*若PAGER_JOURNALMODE_TRUNCATE 进行&计算的值为1，则程序继续执行，否则停止*/
    assert( (PAGER_JOURNALMODE_PERSIST  & 5)==1 );/*若PAGER_JOURNALMODE_PERSIST 进行&计算的值为1，则程序继续执行，否则停止*/
    if( 0==(iDc & SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN)/*若SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN与iDc进行&运算结果为0或者pPager->journalMode & 5的值不为1
                                                则关闭打开的主要日志文件*/
     || 1!=(pPager->journalMode & 5)
    ){
      sqlite3OsClose(pPager->jfd);
    }

    /* If the pager is in the ERROR state and the call to unlock the database
    ** file fails, set the current lock to UNKNOWN_LOCK. See the comment
    ** above the #define for UNKNOWN_LOCK for an explanation of why this
    ** is necessary.如果pager是在异常状态 并且调用打开的的数据库文件失败，则要设置当前锁为UNKNOWN_LOCK。
    看前面关于UNKNOWN_LOCK的预定义可以知道为什么这是必要的。
    */
    rc = pagerUnlockDb(pPager, NO_LOCK);/*打开数据库文件，把值赋给rc,若数据库文件打开失败而且pPager状态异常，设置当前锁为UNKNOWN_LOCK*/
    if( rc!=SQLITE_OK && pPager->eState==PAGER_ERROR ){
      pPager->eLock = UNKNOWN_LOCK;
    }

    /* The pager state may be changed from PAGER_ERROR to PAGER_OPEN here
    ** without clearing the error code. This is intentional - the error
    ** code is cleared and the cache reset in the block below.
    */      /*pager状态可能从异常转变到打开状态没有清除错误的代码，异常代码的清除和缓存在下面块的重置是人为的。*/
     assert( pPager->errCode || pPager->eState!=PAGER_ERROR );
    pPager->changeCountDone = 0;
    pPager->eState = PAGER_OPEN;//有错误的代码或者是pPager是在异常状态，则不做什么改变，把pPager设置为打开状态
  }

  /* If Pager.errCode is set, the contents of the pager cache cannot be
  ** trusted. Now that there are no outstanding references to the pager,
  ** it can safely move back to PAGER_OPEN state. This happens in both
  ** normal and exclusive-locking mode.
  */  
  //如果Pager出现了错误，Pager的缓存内容则不被信任。现在没有引用pager，他可以安全的返回到打开状态，这种情况发生在正常和独立所模式下。
  if( pPager->errCode ){//若存在错误
    assert( !MEMDB );//若没有禁止所有的I/O文件
    pager_reset(pPager);//丢弃整个缓存页面的要求
    pPager->changeCountDone = pPager->tempFile;//看临时文件是否被更新
    pPager->eState = PAGER_OPEN;//把pPager设置为打开状态
    pPager->errCode = SQLITE_OK;//pPager状态设置为正常状态。
  }

  pPager->journalOff = 0;
  pPager->journalHdr = 0;
  pPager->setMaster = 0;
}

/*
** This function is called whenever an IOERR or FULL error that requires
** the pager to transition into the ERROR state may ahve occurred.
** The first argument is a pointer to the pager structure, the second 
** the error-code about to be returned by a pager API function. The 
** value returned is a copy of the second argument to this function. 
** If the second argument is SQLITE_FULL, SQLITE_IOERR or one of the
** IOERR sub-codes, the pager enters the ERROR state and the error code
** is stored in Pager.errCode. While the pager remains in the ERROR state,
** all major API calls on the Pager will immediately return Pager.errCode.
**The ERROR state indicates that the contents of the pager-cache 
** cannot be trusted. This state can be cleared by completely discarding 
** the contents of the pager-cache. If a transaction was active when
** the persistent error occurred, then the rollback journal may need
** to be replayed to restore the contents of the database file (as if
** it were a hot-journal).每当IOERR或FULL错误，需要pager调用此函数过渡到异常状态，其中可能会发生ahve。
 第一个参数是一个指向pager的指针，第二错误代码即将被pager的API函数返回。这个函数返回的值是第二个参数的副本。
 如果第二个参数是SQLITE_FULL，SQLITE_IOERR或之一IOERR子代码，pager进入ERROR状态和错误代码
 存储在Pager.errCode。而pager仍处于错误状态，在pager所有主要的API调用将立即返回Pager.errCode。
 异常状态下，pager-cache的内容不能被信任。这种状态可以完全丢弃pager-cache的内容。
若如果一个事务积极持久的错误发生时，然后回滚日志可能需要被重播恢复数据库文件的内容。
*/
static int pager_error(Pager *pPager, int rc){
  int rc2 = rc & 0xff;
  assert( rc==SQLITE_OK || !MEMDB );
  assert(
       pPager->errCode==SQLITE_FULL ||
       pPager->errCode==SQLITE_OK ||
       (pPager->errCode & 0xff)==SQLITE_IOERR
  );
  if( rc2==SQLITE_FULL || rc2==SQLITE_IOERR ){
    pPager->errCode = rc;
    pPager->eState = PAGER_ERROR;
  }
  return rc;
}

/*
** This routine ends a transaction. A transaction is usually ended by 
** either a COMMIT or a ROLLBACK operation. This routine may be called 
** after rollback of a hot-journal, or if an error occurs while opening
** the journal file or writing the very first journal-header of a
** database transaction.这个程序结束一个事务。这个事务通常是被COMMIT或者ROLLBACK操作结束。
** 这个程序可能在回滚一个hot-journal之后，或者如果当打开日志文件或者写一个数据库事务的journal-header时被调用。
** This routine is never called in PAGER_ERROR state. If it is called
** in PAGER_NONE or PAGER_SHARED state and the lock held is less
** exclusive than a RESERVED lock, it is a no-op.
** Otherwise, any active savepoints are released.
这段程序从不在异常状态下被调用。如果它在PAGER_NONE 或 PAGER_SHARED状态被调用，
而且数据库锁是RESERVED lock而不是排他锁，那么他是一个空程序。否则，任何活动的保存点被释放。
** If the journal file is open, then it is "finalized". Once a journal 
** file has been finalized it is not possible to use it to roll back a 
** transaction. Nor will it be considered to be a hot-journal by this
** or any other database connection. Exactly how a journal is finalized
** depends on whether or not the pager is running in exclusive mode and
** the current journal-mode (Pager.journalMode value), as follows:
**如果日志文件时空的，它就会结束。一旦一个日志文件已经结束，那么再用它去回滚一个事务是不可能的。
既不会认为是一个hot-journal也不是是任何的数据库连接。准确的说一个日志是怎样结束的依赖于是不是
pager运行在独占模式和当前的Pager.journalMode的值。比如下面的

**   journalMode==MEMORY
**     Journal file descriptor is simply closed. This destroys an 
**     in-memory journal.//日志文件标识符是关闭的，这毁坏了内存中的日志。
**
**   journalMode==TRUNCATE
**     Journal file is truncated to zero bytes in size.//日志文件被截成0字节。
**
**   journalMode==PERSIST
**     The first 28 bytes of the journal file are zeroed. This invalidates
**     the first journal header in the file, and hence the entire journal
**     file. An invalid journal file cannot be rolled back.//日志文件的前28位置零。这使得文件中的日志前面部分没有意义。
**     因此整个日志文件无效，一个无效的日志文件不能被回滚。
**   journalMode==DELETE
**     The journal file is closed and deleted using sqlite3OsDelete().
**
**     If the pager is running in exclusive mode, this method of finalizing
**     the journal file is never used. Instead, if the journalMode is
**     DELETE and the pager is in exclusive mode, the method described under
**     journalMode==PERSIST is used instead.//这个日志文件被关闭，并且被sqlite3OsDelete()函数删除。
    如果pager运行在独占模式下，日志文件的结束方法将从不被用。如果journalMode为DELETE而且pager是在独占模式下。
    journalMode==PERSIST 下的方法会被替代。
**
** After the journal is finalized, the pager moves to PAGER_READER state.
** If running in non-exclusive rollback mode, the lock on the file is 
** downgraded to a SHARED_LOCK.日志被关闭之后，pager转变到PAGER_READER状态。
如果是运行在非独占回滚模式，文件的锁会变为SHARED_LOCK锁。
**
** SQLITE_OK is returned if no error occurs. If an error occurs during
** any of the IO operations to finalize the journal file or unlock the
** database then the IO error code is returned to the user. If the 
** operation to finalize the journal file fails, then the code still
** tries to unlock the database file if not in exclusive mode. If the
** unlock operation fails as well, then the first error code related
** to the first error encountered (the journal finalization one) is
** returned.如果没有错误发生，将返回SQLITE_OK，如果任何的IO操作去结束日志文件或者解锁数据库过程中发生错误，
然后IO错误代码返回给用户。如果结束日志文件的操作失败，那么代码仍然试图打开数据库文件如果不是在独占模式。
如果打开操作也失败，那么跟第一个错误有关的第一个错误代码将会被返回。
*/
static int pager_end_transaction(Pager *pPager, int hasMaster){
  int rc = SQLITE_OK;      /* Error code from journal finalization operation */ //结束日志操作的错误代码
  int rc2 = SQLITE_OK;     /* Error code from db file unlock operation */ //数据库文件解锁操作的错误代码

  /* Do nothing if the pager does not have an open write transaction
  ** or at least a RESERVED lock. This function may be called when there
  ** is no write-transaction active but a RESERVED or greater lock is
  ** held under two circumstances:
  **如果pager没有打开的写事务或者至少一个RESERVED锁，什么都不做。
  当没有写操作除了在这两种情况下RESERVED或更大的锁被执行，这个函数可能被调用。
  **   1. After a successful hot-journal rollback, it is called with
  **      eState==PAGER_NONE and eLock==EXCLUSIVE_LOCK.
  **在hot-journal成功回滚后，在eState==PAGER_NONE and eLock==EXCLUSIVE_LOCK时被调用。
  **   2. If a connection with locking_mode=exclusive holding an EXCLUSIVE 
  **      lock switches back to locking_mode=normal and then executes a
  **      read-transaction, this function is called with eState==PAGER_READER 
  **      and eLock==EXCLUSIVE_LOCK when the read-transaction is closed.
  如果一个locking_mode=exclusive，持有独占锁的连接转换成locking_mode=normal，而且执行一个读操作，
  那么读事务关闭时eState==PAGER_READER and eLock==EXCLUSIVE_LOCK 这个函数被调用。
  */
  assert( assert_pager_state(pPager) );//判断是否有错误发生，没有正常执行，否则停止。
  assert( pPager->eState!=PAGER_ERROR );//pPager状态没有出现异常时程序正常执行，否则停止。
  if( pPager->eState<PAGER_WRITER_LOCKED && pPager->eLock<RESERVED_LOCK ){
    return SQLITE_OK;
  }//如果pPager状态是在PAGER_OPEN或者是PAGER_READER状态，并且pPager->eLock为NO_LOCK 或SHARED_LOCK时，返回SQLITE_OK。

  releaseAllSavepoints(pPager);/*释放Pager.aSavepoint[]数组中所有的结构，
把Pager.aSavepoint and Pager.nSavepoint都设置为0,若pager不是在独占模式而且是打开的，则要关闭这个日志。*/
  assert( isOpen(pPager->jfd) || pPager->pInJournal==0 );//若打开了主日志文件或者数据库文件的每一页中的一位为0
  if( isOpen(pPager->jfd) ){
    assert( !pagerUseWal(pPager) );//如果pagerUseWal(pPager)返回的值不为0，程序正常执行，否则停止。

    /* Finalize the journal file. */  //结束日志文件
    if( sqlite3IsMemJournal(pPager->jfd) ){
      assert( pPager->journalMode==PAGER_JOURNALMODE_MEMORY );
      sqlite3OsClose(pPager->jfd);       //若主日志文件是in-memory 日志，pPager->journalMode==PAGER_JOURNALMODE_MEMORY ，则关闭主日志文件。
    }else if( pPager->journalMode==PAGER_JOURNALMODE_TRUNCATE ){
      if( pPager->journalOff==0 ){
        rc = SQLITE_OK;
      }else{
        rc = sqlite3OsTruncate(pPager->jfd, 0);
      }
      pPager->journalOff = 0;
    }else if( pPager->journalMode==PAGER_JOURNALMODE_PERSIST
      || (pPager->exclusiveMode && pPager->journalMode!=PAGER_JOURNALMODE_WAL)
    ){
      rc = zeroJournalHdr(pPager, hasMaster);
      pPager->journalOff = 0;
    }else{
      /* This branch may be executed with Pager.journalMode==MEMORY if
      ** a hot-journal was just rolled back. In this case the journal
      ** file should be closed and deleted. If this connection writes to
      ** the database file, it will do so using an in-memory journal. 
      */
      //这个分支可能执行。如果一个热日志回滚journalMode==MEMORY。这种情况下日志文件应该被关闭或者删除。
      //如果这个连接写的是数据库文件，它将被写进内存中的日志。
      assert( pPager->journalMode==PAGER_JOURNALMODE_DELETE 
           || pPager->journalMode==PAGER_JOURNALMODE_MEMORY 
           || pPager->journalMode==PAGER_JOURNALMODE_WAL 
      );
      sqlite3OsClose(pPager->jfd);//关闭主日志文件
      if( !pPager->tempFile ){  //如果不是一个临时文件
        rc = sqlite3OsDelete(pPager->pVfs, pPager->zJournal, 0);//删除日志文件。
      }
    }
  }

#ifdef SQLITE_CHECK_PAGES
  sqlite3PcacheIterateDirty(pPager->pPCache, pager_set_pagehash);
  if( pPager->dbSize==0 && sqlite3PcacheRefCount(pPager->pPCache)>0 ){
    PgHdr *p = pager_lookup(pPager, 1);
    if( p ){
      p->pageHash = 0;
      sqlite3PagerUnref(p);
    }
  } //如果定义SQLITE_CHECK_PAGES，执行sqlite3PcacheIterateDirty（）函数，如果数据库页面的数目为零而且返回的页面缓存数目大于0，
//找出在哈希表给出的页码输的页面，如果请求页面没有在内存中返回一个指针或者空给p，若p不为0，令p->pageHash = 0，释放对这个页面的引用。

#endif

  sqlite3BitvecDestroy(pPager->pInJournal);
  pPager->pInJournal = 0;//数据库文件中的每一页中的一位为0
  pPager->nRec = 0;
  sqlite3PcacheCleanAll(pPager->pPCache);
  sqlite3PcacheTruncate(pPager->pPCache, pPager->dbSize);

  if( pagerUseWal(pPager) ){
    /* Drop the WAL write-lock, if any. Also, if the connection was in 
    ** locking_mode=exclusive mode but is no longer, drop the EXCLUSIVE 
    ** lock held on the database file.
    */
    //终止WAL写锁，如果连接是在独占模式下，在数据库文件中不再终止EXCLUSIVE锁
    rc2 = sqlite3WalEndWriteTransaction(pPager->pWal);
    assert( rc2==SQLITE_OK );
  }
  if( !pPager->exclusiveMode 
   && (!pagerUseWal(pPager) || sqlite3WalExclusiveMode(pPager->pWal, 0))
  ){
    rc2 = pagerUnlockDb(pPager, SHARED_LOCK);
    pPager->changeCountDone = 0;
  }
  pPager->eState = PAGER_READER;
  pPager->setMaster = 0;

  return (rc==SQLITE_OK?rc2:rc);
}

/*
** Execute a rollback if a transaction is active and unlock the 
** database file. //如果一个事务是活跃的而且数据库文件是打开的，执行一个回滚。
**
** If the pager has already entered the ERROR state, do not attempt 
** the rollback at this time. Instead, pager_unlock() is called. The
** call to pager_unlock() will discard all in-memory pages, unlock
** the database file and move the pager back to OPEN state. If this 
** means that there is a hot-journal left in the file-system, the next 
** connection to obtain a shared lock on the pager (which may be this one) 
** will roll it back.
**如果pager已经是异常状态，不要在这个时候企图回滚，而是调用pager_unlock()。调用pager_unlock()将会丢弃所有在内存中的页面，
解锁数据库文件，pager转换回打开状态。若果这意味着有一个hot-journal被留在文件系统，下一个获得一个共享锁的链接将会回滚。
** If the pager has not already entered the ERROR state, but an IO or
** malloc error occurs during a rollback, then this will itself cause 
** the pager to enter the ERROR state. Which will be cleared by the
** call to pager_unlock(), as described above.
*/
//如果pager还没有进入异常状态，但是在回滚时发生malloc错误或IO错误，它自己就会进入异常状态，像上面描述的一样，在调用pager_unlock()
//它将会被清除。
static void pagerUnlockAndRollback(Pager *pPager){
  if( pPager->eState!=PAGER_ERROR && pPager->eState!=PAGER_OPEN ){
    assert( assert_pager_state(pPager) );
    if( pPager->eState>=PAGER_WRITER_LOCKED ){
      sqlite3BeginBenignMalloc();
      sqlite3PagerRollback(pPager);
      sqlite3EndBenignMalloc();
    }else if( !pPager->exclusiveMode ){
      assert( pPager->eState==PAGER_READER );
      pager_end_transaction(pPager, 0);
    }
  }
  pager_unlock(pPager);
}

/*
** Parameter aData must point to a buffer of pPager->pageSize bytes
** of data. Compute and return a checksum based ont the contents of the 
** page of data and the current value of pPager->cksumInit.
**参数aData必须指向页面中的字节数数据的缓冲区，计算并返回一个基于页面数据内容和Pager->cksumInit当前值的校验和。
** This is not a real checksum. It is really just the sum of the 
** random initial value (pPager->cksumInit) and every 200th byte
** of the page data, starting with byte offset (pPager->pageSize%200).
** Each byte is interpreted as an 8-bit unsigned integer.
**这不是真的校验和，它只是pPager->cksumInit随机初始值的和，以pPager->pageSize%200的之开始，每个字节都是8位的无符号整形。
** Changing the formula used to compute this checksum results in an
** incompatible journal file format.
**在一个不兼容的日志文件格式改变通常用于计算校验和结果的公式。
** If journal corruption occurs due to a power failure, the most likely 
** scenario is that one end or the other of the record will be changed. 
** It is much less likely that the two ends of the journal record will be
** correct and the middle be corrupt.  Thus, this "checksum" scheme,
** though fast and simple, catches the mostly likely kind of corruption.
*/  //如果由于电源故障日志发生错误，一种最可能的情况就是一端或一个记录将会被改变。
//日志两端的记录正确，中间部分将会发生错误是不太可能的。这样，这个校验和机制，虽然快而且简单，但会捕获最有可能的错误。
static u32 pager_cksum(Pager *pPager, const u8 *aData){
  u32 cksum = pPager->cksumInit;         /* Checksum value to return */ //返回校验和
  int i = pPager->pageSize-200;          /* Loop counter */ //循环计数器
  while( i>0 ){
    cksum += aData[i];
    i -= 200;
  }
  return cksum;
}

/*
** Report the current page size and number of reserved bytes back
** to the codec.
*/ //报告当前页面大小和保留字节的数量给编解码器。，
#ifdef SQLITE_HAS_CODEC
static void pagerReportSize(Pager *pPager){
  if( pPager->xCodecSizeChng ){
    pPager->xCodecSizeChng(pPager->pCodec, pPager->pageSize,
                           (int)pPager->nReserve);
  }
}
#else
# define pagerReportSize(X)     /* No-op if we do not support a codec */  //如果我们不支持编解码器，这将是个空操作。
#endif

/*
** Read a single page from either the journal file (if isMainJrnl==1) or
** from the sub-journal (if isMainJrnl==0) and playback that page.
** The page begins at offset *pOffset into the file. The *pOffset
** value is increased to the start of the next page in the journal.
**从主日志文件中或者从子日志文件中读一个页面，并且回放这个页面。
**页面开始抵消*pOffset到文件。*pOffset的值增加到下一个页面的开始。
** The main rollback journal uses checksums - the statement journal does 
** not.
**主要的回滚日志用校验和，日志的声明不用。
** If the page number of the page record read from the (sub-)journal file
** is greater than the current value of Pager.dbSize, then playback is
** skipped and SQLITE_OK is returned.
**如果从（子）的日志文件读出的页的记录的页面数大于Pager.dbSize的当前值，然后跳过回放，返回SQLITE_OK。
** If pDone is not NULL, then it is a record of pages that have already
** been played back.  If the page at *pOffset has already been played back
** (if the corresponding pDone bit is set) then skip the playback.
** Make sure the pDone bit corresponding to the *pOffset page is set
** prior to returning.
**若pDone不为空，页面的记录已经被回放。若*pOffset页面已经被回放，则跳过回放。确保pDone位对应的*pOffset页面设置优先级返回。
** If the page record is successfully read from the (sub-)journal file
** and played back, then SQLITE_OK is returned. If an IO error occurs
** while reading the record from the (sub-)journal file or while writing
** to the database file, then the IO error code is returned. If data
** is successfully read from the (sub-)journal file but appears to be
** corrupted, SQLITE_DONE is returned. Data is considered corrupted in
** two circumstances:
** 若页面记录从（子）日志文件中成功的被读或者回放，则将返回SQLITE_OK。
**如果当从（子）日志文件中读或写进数据库文件时发生IO错误，则错误的代码将会返回。
**如果从（子）日志文件中的数据成功的读但是出现了破坏，将返回SQLITE_DONE。数据被破坏在两种情况下：
**   * If the record page-number is illegal (0 or PAGER_MJ_PGNO), or
**   * If the record is being rolled back from the main journal file
**     and the checksum field does not match the record content.
**如果这个页面记录是不合法的，或者这个记录从主日志文件回滚，校验和文件没有匹配日志内容。
** Neither of these two scenarios are possible during a savepoint rollback.
**在保存点回滚，这两种情况都是不可能的。
** If this is a savepoint rollback, then memory may have to be dynamically
** allocated by this function. If this is the case and an allocation fails,
** SQLITE_NOMEM is returned.
*/ //如果这是保存点回滚，内存则会不得不被这个函数动态的分配。如果遇到这种情况而且分配失败，将会返回SQLITE_NOMEM。
static int pager_playback_one_page( 
  Pager *pPager,                /* The pager being played back */  //pager的回放
  i64 *pOffset,                 /* Offset of record to playback */  //抵消回放的记录
  Bitvec *pDone,                /* Bitvec of pages already played back */ //Bitvec页面已经回放
  int isMainJrnl,               /* 1 -> main journal. 0 -> sub-journal. */
  int isSavepnt                 /* True for a savepoint rollback */ //保存点回滚是为true
){
  int rc;
  PgHdr *pPg;                   /* An existing page in the cache */ //缓存中存在的文件
  Pgno pgno;                    /* The page number of a page in journal */  //在日志中一个页面的页码
  u32 cksum;                    /* Checksum used for sanity checking */  // 用于完整性检查的校验和
  char *aData;                  /* Temporary storage for the page */ //存储页面的临时文件
  sqlite3_file *jfd;            /* The file descriptor for the journal file */  //日志文件
  int isSynced;                 /* True if journal page is synced */ //日志页面同步时为true

  assert( (isMainJrnl&~1)==0 );      /* isMainJrnl is 0 or 1 */
  assert( (isSavepnt&~1)==0 );       /* isSavepnt is 0 or 1 */
  assert( isMainJrnl || pDone );     /* pDone always used on sub-journals */ 
  assert( isSavepnt || pDone==0 );   /* pDone never used on non-savepoint */

  aData = pPager->pTmpSpace;
  assert( aData );         /* Temp storage must have already been allocated */ //临时存储必须已经被分配，否则程序不执行。
  assert( pagerUseWal(pPager)==0 || (!isMainJrnl && isSavepnt) );

  /* Either the state is greater than PAGER_WRITER_CACHEMOD (a transaction 
  ** or savepoint rollback done at the request of the caller) or this is
  ** a hot-journal rollback. If it is a hot-journal rollback, the pager
  ** is in state OPEN and holds an EXCLUSIVE lock. Hot-journal rollback
  ** only reads from the main journal, not the sub-journal.
  */ //要么大于PAGER_WRITER_CACHEMOD状态（事务或保存点回滚完成请求的调用者）要么这是热日志的回滚。如果hot-journal回滚
  //pager是在打开状态，并拥有一个独占锁。热日志回滚仅仅在从主日志中读的时候，子日志中不会。
  assert( pPager->eState>=PAGER_WRITER_CACHEMOD
       || (pPager->eState==PAGER_OPEN && pPager->eLock==EXCLUSIVE_LOCK)
  );
  assert( pPager->eState>=PAGER_WRITER_CACHEMOD || isMainJrnl );

  /* Read the page number and page data from the journal or sub-journal
  ** file. Return an error code to the caller if an IO error occurs.
  */ //从日志或子日志文件读页码和页面的数据。若有IO错误发生，返回错误代码给调用者。
  jfd = isMainJrnl ? pPager->jfd : pPager->sjfd;
  rc = read32bits(jfd, *pOffset, &pgno);
  if( rc!=SQLITE_OK ) return rc;
  rc = sqlite3OsRead(jfd, (u8*)aData, pPager->pageSize, (*pOffset)+4);
  if( rc!=SQLITE_OK ) return rc;
  *pOffset += pPager->pageSize + 4 + isMainJrnl*4;

  /* Sanity checking on the page.  This is more important that I originally
  ** thought.  If a power failure occurs while the journal is being written,
  ** it could cause invalid data to be written into the journal.  We need to
  ** detect this invalid data (with high probability) and ignore it.
  */ //页面上的完备性检查。我原本想这是更重要的。若当文件被写时，发生电源故障时，他将会造成无效的数据写进日志。
  //我们需要发现无效的数据并忽视它。
  if( pgno==0 || pgno==PAGER_MJ_PGNO(pPager) ){
    assert( !isSavepnt );
    return SQLITE_DONE;
  }
  if( pgno>(Pgno)pPager->dbSize || sqlite3BitvecTest(pDone, pgno) ){
    return SQLITE_OK;
  }
  if( isMainJrnl ){
    rc = read32bits(jfd, (*pOffset)-4, &cksum);
    if( rc ) return rc;
    if( !isSavepnt && pager_cksum(pPager, (u8*)aData)!=cksum ){
      return SQLITE_DONE;
    }
  }

  /* If this page has already been played by before during the current
  ** rollback, then don't bother to play it back again.
  */ //如果这个页面已经回放被当前回滚，不要再去回放。
  if( pDone && (rc = sqlite3BitvecSet(pDone, pgno))!=SQLITE_OK ){
    return rc;
  }

  /* When playing back page 1, restore the nReserve setting
  */  //当回放页面1，恢复nReserve设置
  if( pgno==1 && pPager->nReserve!=((u8*)aData)[20] ){
    pPager->nReserve = ((u8*)aData)[20];
    pagerReportSize(pPager);
  }

  /* If the pager is in CACHEMOD state, then there must be a copy of this
  ** page in the pager cache. In this case just update the pager cache,
  ** not the database file. The page is left marked dirty in this case.
  **如果pager是在CACHEMOD状态，那么在pager缓存中必须有这个页面的副本。
  ** 在这种情况下仅仅更新pager缓存，而不是数据库文件。这种情况下剩下的页面记为脏数据。
  ** An exception to the above rule: If the database is in no-sync mode
  ** and a page is moved during an incremental vacuum then the page may
  ** not be in the pager cache. Later: if a malloc() or IO error occurs
  ** during a Movepage() call, then the page may not be in the cache
  ** either. So the condition described in the above paragraph is not
  ** assert()able.
  **一个例外对于上述规则，如果数据库是在没有同步的模式，页面被移动在一个在增值的真空，则页面将无法在pager缓存中。
  随后，若在调用Movepage()时，发生malloc()或IO错误，页面也将无法在pager缓存中。
  因此上个段落描述的条件不是assert()函数可以实现的。
  ** If in WRITER_DBMOD, WRITER_FINISHED or OPEN state, then we update the
  ** pager cache if it exists and the main file. The page is then marked 
  ** not dirty. Since this code is only executed in PAGER_OPEN state for
  ** a hot-journal rollback, it is guaranteed that the page-cache is empty
  ** if the pager is in OPEN state.
  **如果在WRITER_DBMOD, WRITER_FINISHED or OPEN状态，那我们要更新pager 缓存（若存在），和主文件。
  页面不标记为脏。由于hot-journal回滚 ，这个代码仅仅执行在PAGER_OPEN状态，如果pager是在打开状态，要保证页面缓存为空。
  ** Ticket #1171:  The statement journal might contain page content that is
  ** different from the page content at the start of the transaction.
  标签#1171：日志的状态可能包含页面内容，不同于事务开始时的页面内容。
  ** This occurs when a page is changed prior to the start of a statement
  ** then changed again within the statement.  When rolling back such a
  ** statement we must not write to the original database unless we know
  ** for certain that original page contents are synced into the main rollback
  ** journal. 这发生在一个页面改变开始之前，然后在声明中 再次改变。当这样的声明回滚，我们不必写进原始数据库，
  除非我们知道对于某些原始页面的内容同步到主回滚日志上。
  **Otherwise, a power loss might leave modified data in the
  ** database file without an entry in the rollback journal that can
  ** restore the database to its original form. 
  否则，功率损耗可能会把修改后的数据留在数据库文件没有一个条目在回滚日志上，将数据库恢复到原来的形式。
  Two conditions must be met before writing to the database files. (1) the database must be
  ** locked.  (2) we know that the original page content is fully synced
  ** in the main journal either because the page is not in cache or else
  ** the page is marked as needSync==0.
  **在写进数据库文件之前两种情况必须有，（1）数据库必须有锁（2）我们知道在主日志中原始页面内容完全被同步，
  因为页面没有在缓存中或者是页面被标记为needSync==0。
  ** 2008-04-14:  When attempting to vacuum a corrupt database file, it
  ** is possible to fail a statement on a database that does not yet exist.
  ** Do not attempt to write if database file has never been opened.
  *///2008-04-14： 当试图去消除一个腐败的数据库文件，失败的声明在一个上不存在的数据库中是可能的，
  // 如果数据库文件从没有被打开，不要试着去写。
  if( pagerUseWal(pPager) ){
    pPg = 0;
  }else{
    pPg = pager_lookup(pPager, pgno);
  }
  assert( pPg || !MEMDB );
  assert( pPager->eState!=PAGER_OPEN || pPg==0 );
  PAGERTRACE(("PLAYBACK %d page %d hash(%08x) %s\n",
           PAGERID(pPager), pgno, pager_datahash(pPager->pageSize, (u8*)aData),
           (isMainJrnl?"main-journal":"sub-journal")
  ));
  if( isMainJrnl ){
    isSynced = pPager->noSync || (*pOffset <= pPager->journalHdr);
  }else{
    isSynced = (pPg==0 || 0==(pPg->flags & PGHDR_NEED_SYNC));
  }
  if( isOpen(pPager->fd)
   && (pPager->eState>=PAGER_WRITER_DBMOD || pPager->eState==PAGER_OPEN)
   && isSynced
  ){
    i64 ofst = (pgno-1)*(i64)pPager->pageSize;
    testcase( !isSavepnt && pPg!=0 && (pPg->flags&PGHDR_NEED_SYNC)!=0 );
    assert( !pagerUseWal(pPager) );
    rc = sqlite3OsWrite(pPager->fd, (u8*)aData, pPager->pageSize, ofst);
    if( pgno>pPager->dbFileSize ){
      pPager->dbFileSize = pgno;
    }
    if( pPager->pBackup ){
      CODEC1(pPager, aData, pgno, 3, rc=SQLITE_NOMEM);
      sqlite3BackupUpdate(pPager->pBackup, pgno, (u8*)aData);
      CODEC2(pPager, aData, pgno, 7, rc=SQLITE_NOMEM, aData);
    }
  }else if( !isMainJrnl && pPg==0 ){
    /* If this is a rollback of a savepoint and data was not written to
    ** the database and the page is not in-memory, there is a potential
    ** problem. When the page is next fetched by the b-tree layer, it 
    ** will be read from the database file, which may or may not be 
    ** current.如果这是一个保存点回滚,数据不写入数据库和页面不是内存, 这会有一个潜在的问题。
    当页面被b-tree层获取，它将会从数据库文件中读，可能是当前也可能不是。
    ** There are a couple of different ways this can happen. All are quite
    ** obscure. When running in synchronous mode, this can only happen 
    ** if the page is on the free-list at the start of the transaction, then
    ** populated, then moved using sqlite3PagerMovepage().
    **有两种不同的方法可能会发生。所有的都很不清晰。在同步模式下运行时,其前提条件是空闲列表的页面
    是在事务的开始,然后填充,然后转移到用sqlite3PagerMovepage()。
    ** The solution is to add an in-memory page to the cache containing
    ** the data just read from the sub-journal. Mark the page as dirty 
    ** and if the pager requires a journal-sync, then mark the page as 
    ** requiring a journal-sync before it is written.
    */ //解决方案是将内存中的页面添加到缓存，其包含的数据只是从sub-journal读取。
    //标记页面为dirty，如果pager需要一个同步日志，那么标记这个页面需要在journal-sync之前写。
    assert( isSavepnt );
    assert( pPager->doNotSpill==0 );
    pPager->doNotSpill++;
    rc = sqlite3PagerAcquire(pPager, pgno, &pPg, 1);
    assert( pPager->doNotSpill==1 );
    pPager->doNotSpill--;
    if( rc!=SQLITE_OK ) return rc;
    pPg->flags &= ~PGHDR_NEED_READ;
    sqlite3PcacheMakeDirty(pPg);
  }
  if( pPg ){
    /* No page should ever be explicitly rolled back that is in use, except
    ** for page 1 which is held in use in order to keep the lock on the
    ** database active.　没有页面应该显式地使用回滚,除了在第1页在使用为了保持锁定数据库活动
     However such a page may be rolled back as a result 
    ** of an internal error resulting in an automatic call to
    ** sqlite3PagerRollback().
    */ //然而这样一个页面可能回滚，一个内部错误导致自动调用sqlite3PagerRollback()。
    void *pData;
    pData = pPg->pData;
    memcpy(pData, (u8*)aData, pPager->pageSize);
    pPager->xReiniter(pPg);
    if( isMainJrnl && (!isSavepnt || *pOffset<=pPager->journalHdr) ){
      /* If the contents of this page were just restored from the main 
      ** journal file, then its content must be as they were when the 
      ** transaction was first opened. In this case we can mark the page
      ** as clean, since there will be no need to write it out to the
      ** database.如果这个页面的内容仅仅从主日志文件恢复，它的内容必须是事务第一次打开时的。
      **在这种情况下，我们可以标志页面为clean，因为不会有需要写出来的数据库。
      ** There is one exception to this rule. If the page is being rolled
      ** back as part of a savepoint (or statement) rollback from an 
      ** unsynced portion of the main journal file, then it is not safe
      ** to mark the page as clean.，这里有个例外对于这个规则。如果页面被回滚作为保存点回滚从主日志文件不同步的部分。
      **This is because marking the page as clean will clear the PGHDR_NEED_SYNC flag. 
      这是因为标记这个页面为clean将会清除PGHDR_NEED_SYNC标志。
      ** Since the page is already in the journal file (recorded in Pager.pInJournal) and
      ** the PGHDR_NEED_SYNC flag is cleared, if the page is written to
      ** again within this transaction, it will be marked as dirty but
      ** the PGHDR_NEED_SYNC flag will not be set. 由于页面已经在日志文件，PGHDR_NEED_SYNC标记被清除，如果这个页面再次被写进这个事务，
       它将会标记为dirty，但是 PGHDR_NEED_SYNC标志不会再被设置。
      **It could then potentially be written out into the database file before its journal file
      ** segment is synced. If a crash occurs during or following this,database corruption may ensue.
      它可能被写入数据库文件在它的日志文件部分被同步。如果事故发生在这期间或之后，继而数据库也会被污染。*/
      assert( !pagerUseWal(pPager) );
      sqlite3PcacheMakeClean(pPg);
    }
    pager_set_pagehash(pPg);

    /* If this was page 1, then restore the value of Pager.dbFileVers. 如果这是第一个页面， Pager.dbFileVers的值会被恢复。
    ** Do this before any decoding. */
    if( pgno==1 ){
      memcpy(&pPager->dbFileVers, &((u8*)pData)[24],sizeof(pPager->dbFileVers));
    }

    /* Decode the page just read from disk */ //解码页面，仅仅从磁盘上读。
    CODEC1(pPager, pData, pPg->pgno, 3, rc=SQLITE_NOMEM);
    sqlite3PcacheRelease(pPg);
  }
  return rc;
}
/*Parameter zMaster is the name of a master journal file. A single journal
** file that referred to the master journal file has just been rolled back.
** 参数zMaster是主日志文件的名称。引用主日志文件的一个单独的日志文件刚刚回滚。
** This routine checks if it is possible to delete the master journal file,
** and does so if it is. 这个例程检查是否删除主日志文件，若这样做的话。
**
** Argument zMaster may point to Pager.pTmpSpace. So that buffer is not 
** available for use within this function.  
**zMaster可能指向Pager.pTmpSpace。在这个函数用用，以至于缓冲区没有被利用

** When a master journal file is created, it is populated with the names 
** of all of its child journals, one after another, formatted as utf-8 
** encoded text. The end of each child journal file is marked with a 
** nul-terminator byte (0x00). i.e. the entire contents of a master journal
** file for a transaction involving two databases might b  e:
**当主日志文件被创建，它将被所有的子日志文件填充，格式化为utf-8编码文本。
每个子日志文件以0x00字节结束。比如，一个事务涉及两个数据库可能是：
**   "/home/bill/a.db-journal\x00/home/bill/b.db-journal\x00"
**
** A master journal file may only be deleted once all of its child 
** journals have been rolled back.
**一旦主日志文件的日志文件回滚，这个主日志文件将会被删除。
** This function reads the contents of the master-journal file into 
** memory and loops through each of the child journal names. For
** each child journal, it checks if:
**这个函数把主日志文件的内容读进内存，循环通过每个子日志的名字。 对于每个子日志，它检查
**   * if the child journal exists, and if so 是否子日志存在
**   * if the child journal contains a reference to master journal 
**     file zMaster 是否子日志引用主日志文件zMaster
**
** If a child journal can be found that matches both of the criteria
** above, this function returns without doing anything. Otherwise, if
** no such child journal can be found, file zMaster is deleted from
** the file-system using sqlite3OsDelete().如果一个子日志可以被找到相匹配的两个标准（上面的），这个函数将什么也不返回。
**否则，如果没有这样的子日志，文件系统将用sqlite3OsDelete()函数删除zMaster文件。
** If an IO error within this function, an error code is returned. This
** function allocates memory by calling sqlite3Malloc(). If an allocation
** fails, SQLITE_NOMEM is returned. Otherwise, if no IO or malloc errors 
** occur, SQLITE_OK is returned. 如果有IO错误在这个函数中，一个错误代码将被返回。通过调用sqlite3Malloc()给这个函数分配内存。
**如果分配失败，将返回SQLITE_NOMEM。否则，若没有分配错误或IO错误发生，将返回SQLITE_OK。
** TODO: This function allocates a single block of memory to load
** the entire contents of the master journal file. This could be
** a couple of kilobytes or so - potentially larger than the page 
** size.
*/  //备注：这个函数分配一个单独的内存块装在整个日志文件的内容。这可能是几kb左右也可呢过可能大于页面的大小。
static int pager_delmaster(Pager *pPager, const char *zMaster){
  sqlite3_vfs *pVfs = pPager->pVfs;
  int rc;                   /* Return code */  //用于返回值
  sqlite3_file *pMaster;    /* Malloc'd master-journal file descriptor */  //Malloc'd 主日志文件的描述付
  sqlite3_file *pJournal;   /* Malloc'd child-journal file descriptor */ //Malloc'd 子日志文件的描述付
  char *zMasterJournal = 0; /* Contents of master journal file */   //主日志文件的内容
  i64 nMasterJournal;       /* Size of master journal file */    //主日志文件的大小。
  char *zJournal;           /* Pointer to one journal within MJ file */  //在主日志文件指向一个日志
  char *zMasterPtr;         /* Space to hold MJ filename from a journal file */ //从日志文件空间来保存MJ文件名
  int nMasterPtr;           /* Amount of space allocated to zMasterPtr[] */     //分配给zMasterPtr[]的空间数
 
  /* Allocate space for both the pJournal and pMaster file descriptors. 为pJournal and pMaster文件的描述符分配空间。
  ** If successful, open the master journal file for reading.   如果成功打开主日志文件进行读取。
  */
  pMaster = (sqlite3_file *)sqlite3MallocZero(pVfs->szOsFile * 2);
  pJournal = (sqlite3_file *)(((u8 *)pMaster) + pVfs->szOsFile);
  if( !pMaster ){
    rc = SQLITE_NOMEM;
  }else{
    const int flags = (SQLITE_OPEN_READONLY|SQLITE_OPEN_MASTER_JOURNAL);
    rc = sqlite3OsOpen(pVfs, zMaster, pMaster, flags, 0);
  }
  if( rc!=SQLITE_OK ) goto delmaster_out;

  /* Load the entire master journal file into space obtained from
  ** sqlite3_malloc() and pointed to by zMasterJournal.   Also obtain
  ** sufficient space (in zMasterPtr) to hold the names of master
  ** journal files extracted from regular rollback-journals.
  */  // 装载主日志文件到剩余的空间，从sqlite3_malloc()获得的和zMasterJournal指向的。
  //　要有足够的空间装载定期从回滚日志提取出来的。
  rc = sqlite3OsFileSize(pMaster, &nMasterJournal);
  if( rc!=SQLITE_OK ) goto delmaster_out;
  nMasterPtr = pVfs->mxPathname+1;
  zMasterJournal = sqlite3Malloc((int)nMasterJournal + nMasterPtr + 1);
  if( !zMasterJournal ){
    rc = SQLITE_NOMEM;
    goto delmaster_out;
  }
  zMasterPtr = &zMasterJournal[nMasterJournal+1];
  rc = sqlite3OsRead(pMaster, zMasterJournal, (int)nMasterJournal, 0);
  if( rc!=SQLITE_OK ) goto delmaster_out;
  zMasterJournal[nMasterJournal] = 0;

  zJournal = zMasterJournal;
  while( (zJournal-zMasterJournal)<nMasterJournal ){
    int exists;
    rc = sqlite3OsAccess(pVfs, zJournal, SQLITE_ACCESS_EXISTS, &exists);
    if( rc!=SQLITE_OK ){
      goto delmaster_out;
    }
    if( exists ){ 
      /* One of the journals pointed to by the master journal exists.  //存在的主日志指向的一个日志。
      ** Open it and check if it points at the master journal. If
      ** so, return without deleting the master journal file.
      */ //打开并检查是否它是否指向主日志。如果是返回没有删除主日志文件。
      int c;
      int flags = (SQLITE_OPEN_READONLY|SQLITE_OPEN_MAIN_JOURNAL);
      rc = sqlite3OsOpen(pVfs, zJournal, pJournal, flags, 0);
      if( rc!=SQLITE_OK ){
        goto delmaster_out;
      }

      rc = readMasterJournal(pJournal, zMasterPtr, nMasterPtr);
      sqlite3OsClose(pJournal);
      if( rc!=SQLITE_OK ){
        goto delmaster_out;
      }

      c = zMasterPtr[0]!=0 && strcmp(zMasterPtr, zMaster)==0;
      if( c ){
        /* We have a match. Do not delete the master journal file. */ // 我们有一个匹配，不要删除主日志文件。
        goto delmaster_out;
      }
    }
    zJournal += (sqlite3Strlen30(zJournal)+1);
  }
 
  sqlite3OsClose(pMaster);
  rc = sqlite3OsDelete(pVfs, zMaster, 0);

delmaster_out:
  sqlite3_free(zMasterJournal);
  if( pMaster ){
    sqlite3OsClose(pMaster);
    assert( !isOpen(pJournal) );
    sqlite3_free(pMaster);
  }
  return rc;
}


/*
** This function is used to change the actual size of the database  
** file in the file-system. This only happens when committing a transaction,
** or rolling back a transaction (including rolling back a hot-journal).    
** 这个函数用来改变文件系统中数据库文件的真正大小。这仅仅发生在提交一个事务或回滚一个事务（包括回滚一个热日志）。
** If the main database file is not open, or the pager is not in either
** DBMOD or OPEN state, this function is a no-op. Otherwise, the size 
** of the file is changed to nPage pages (nPage*pPager->pageSize bytes). 
** If the file on disk is currently larger than nPage pages, then use the VFS
** xTruncate() method to truncate it.
**如果主数据库文件没有打开，或者pager不是在DBMOD或者 OPEN状态，这个函数是一个空操作。否则，文件的大小改变为nPage。
如果当前磁盘上的文件大于nPage页面,则使用VFSxTruncate()方法截断它。
** Or, it might might be the case that the file on disk is smaller than 
** nPage pages. Some operating system implementations can get confused if 
** you try to truncate a file to some size that is larger than it 
** currently is, so detect this case and write a single zero byte to 
** the end of the new file instead.
**或者也许可能磁盘上的文件小于nPage页面。一些操作系统启用可能出现困惑，如果你想截取一个文件，所以有这种情况，写一个单独的0字节在新文件的结束。
** If successful, return SQLITE_OK. If an IO error occurs while modifying
** the database file, return the error code to the caller.
*/ //如果成功则返回SQLITE_OK，如果当修改数据库文件时发生IO错误，返回错误代码给调用者。
static int pager_truncate(Pager *pPager, Pgno nPage){
  int rc = SQLITE_OK;
  assert( pPager->eState!=PAGER_ERROR );
  assert( pPager->eState!=PAGER_READER );
  
  if( isOpen(pPager->fd) 
   && (pPager->eState>=PAGER_WRITER_DBMOD || pPager->eState==PAGER_OPEN) 
  ){
    i64 currentSize, newSize;
    int szPage = pPager->pageSize;
    assert( pPager->eLock==EXCLUSIVE_LOCK );
    /* TODO: Is it safe to use Pager.dbFileSize here? */  //这里用Pager.dbFileSize安全吗？
    rc = sqlite3OsFileSize(pPager->fd, &currentSize);
    newSize = szPage*(i64)nPage;
    if( rc==SQLITE_OK && currentSize!=newSize ){
      if( currentSize>newSize ){
        rc = sqlite3OsTruncate(pPager->fd, newSize);
      }else if( (currentSize+szPage)<=newSize ){
        char *pTmp = pPager->pTmpSpace;
        memset(pTmp, 0, szPage);
        testcase( (newSize-szPage) == currentSize );
        testcase( (newSize-szPage) >  currentSize );
        rc = sqlite3OsWrite(pPager->fd, pTmp, szPage, newSize-szPage);
      }
      if( rc==SQLITE_OK ){
        pPager->dbFileSize = nPage;
      }
    }
  }
  return rc;
}

/*
** Set the value of the Pager.sectorSize variable for the given
** pager based on the value returned by the xSectorSize method
** of the open database file. The sector size will be used used 
** to determine the size and alignment of journal header and 
** master journal pointers within created journal files.
**对于给定的给予xSectorSize方法返回值的pager设置Pager.sectorSize变量的值，打开数据库。
扇形的大小将决定大小和对齐日志的头文件，主日志文件指针。
** For temporary files the effective sector size is always 512 bytes.
**对于临时文件有效的扇形区域大小一直是512字节。
** Otherwise, for non-temporary files, the effective sector size is
** the value returned by the xSectorSize() method rounded up to 32 if
** it is less than 32, or rounded down to MAX_SECTOR_SIZE if it
** is greater than MAX_SECTOR_SIZE.
**否则，对于non-temporary文件，有效的区域大小是xSectorSize()方法返回的值，若小于32约为32，或者等于MAX_SECTOR_SIZE，若它大于MAX_SECTOR_SIZE。
** If the file has the SQLITE_IOCAP_POWERSAFE_OVERWRITE property, then set
** the effective sector size to its minimum value (512).  The purpose of
** pPager->sectorSize is to define the "blast radius" of bytes that
** might change if a crash occurs while writing to a single byte in
** that range.  But with POWERSAFE_OVERWRITE, the blast radius is zero
** (that is what POWERSAFE_OVERWRITE means), so we minimize the sector
** size.  For backwards compatibility of the rollback journal file format,
** we cannot reduce the effective sector size below 512.
*/ // 如果文件有SQLITE_IOCAP_POWERSAFE_OVERWRITE的属性，则设置有效扇形区域大小为最小值512。pPager->sectorSize的目的是定义"blast radius"如果
//当写一个单独的字节在那个范围内时，发生错误。

static void setSectorSize(Pager *pPager){
  assert( isOpen(pPager->fd) || pPager->tempFile );

  if( pPager->tempFile
   || (sqlite3OsDeviceCharacteristics(pPager->fd) & 
              SQLITE_IOCAP_POWERSAFE_OVERWRITE)!=0
  ){
    /* Sector size doesn't matter for temporary files. Also, the file  //临时文件的扇形大小并不重要。
    ** may not have been opened yet, in which case the OsSectorSize()
    ** call will segfault. */  //此外，可能还没有打开的文件，这种情况下OsSectorSize()调用将segfault。
    pPager->sectorSize = 512;
  }else{
    pPager->sectorSize = sqlite3OsSectorSize(pPager->fd);
    if( pPager->sectorSize<32 ){
      pPager->sectorSize = 512;
    }
    if( pPager->sectorSize>MAX_SECTOR_SIZE ){
      assert( MAX_SECTOR_SIZE>=512 );
      pPager->sectorSize = MAX_SECTOR_SIZE;
    }
  }
}

/*
** Playback the journal and thus restore the database file to
** the state it was in before we started making changes.  
** 回放日志，从而恢复数据库文件到在我们做出改变之前。
** The journal file format is as follows: 
**日志文件的格式如下：
**  (1)  8 byte prefix.  A copy of aJournalMagic[]. 8字节前缀   
**  (2)  4 byte big-endian integer which is the number of valid page records  四字节高位优先整数，日志中有效页面记录的数量
**       in the journal.  If this value is 0xffffffff, then compute the
**       number of page records from the journal size. 若它的值为0xffffffff，则计算页面的记录日志的大小的数量
**  (3)  4 byte big-endian integer which is the initial value for the 四字节高位优先整数，完整校验和的初始值。
**       sanity checksum.
**  (4)  4 byte integer which is the number of pages to truncate the
**       database to during a rollback.  四字节优先整数，在回滚期间页面截断数据库页面的数量。
**  (5)  4 byte big-endian integer which is the sector size.  The header
**       is this many bytes in size. 四字节优先整数，扇区大小。   
**  (6)  4 byte big-endian integer which is the page size. 四字节优先整数，页面的大小。
**  (7)  zero padding out to the next sector size. 零填充第二扇区大小
**  (8)  Zero or more pages instances, each as follows:  零个或多个页面的实例,如下所示
**        +  4 byte page number.    4字节页码数
**        +  pPager->pageSize bytes of data. 
**        +  4 byte checksum  四字节校验和
**
** When we speak of the journal header, we mean the first 7 items above.
** Each entry in the journal is an instance of the 8th item. 当说到日志头文件，我们首先想到上面的七项。
**日志中的每一个入口是第八项的一个实例。
** Call the value from the second bullet "nRec".  nRec is the number of
** valid page entries in the journal.  In most cases, you can compute the
** value of nRec from the size of the journal file.  But if a power
** failure occurred while the journal was being written, it could be the
** case that the size of the journal file had already been increased but
** the extra entries had not yet made it safely to disk.  In such a case,
** the value of nRec computed from the file size would be too large.  For
** that reason, we always use the nRec value in the header.
**调用的值从第二个符号"nRec"，nRec时进入日志有效页面的数量。在大多数情况下，你可以计算nRec的值从日志文件的大小。
**但是如果当日志正在写时，电源故障，它可能的情况是,日志文件的大小被增加，额外的条目还没有使它安全地到磁盘。在这种情况下，nRec的值
出于这个原因,我们总是使用nRec值在标题。
** If the nRec value is 0xffffffff it means that nRec should be computed
** from the file size.  This value is used when the user selects the
** no-sync option for the journal.  A power failure could lead to corruption
** in this case.  But for things like temporary table (which will be
** deleted when the power is restored) we don't care. 
**若nRec的值为0xffffffff，这意味着nRec应该被计算从文件的大小。这个值在用户为这个日志选择no-sync操作时使用。
在这种情况下，若电源故障可能导致腐败。但是对于诸如临时表(电源恢复时将被删除),我们不关心。
** If the file opened as the journal file is not a well-formed
** journal file then all pages up to the first corrupted page are rolled
** back (or no pages if the journal header is corrupted). The journal file
** is then deleted and SQLITE_OK returned, just as if no corruption had
** been encountered. 如果打开的文件不是一个格式良好的日志文件，那么所有页面的第一个损坏的页面将会被回滚。（若日志的头文件被破坏，将没有页面）
**日志文件将被删除，并返回SQLITE_OK，就像没有被破坏。
** If an I/O or malloc() error occurs, the journal-file is not deleted
** and an error code is returned.
**若一个I/O 或 malloc()错误发生，日志文件不被删除，错误代码将会被返回。
** The isHot parameter indicates that we are trying to rollback a journal
** that might be a hot journal. 参数isHot表示我们试图回滚有一个日志，这个日志可能是一个hot journal Or, it could be that the journal is 
** preserved because of JOURNALMODE_PERSIST or JOURNALMODE_TRUNCATE. 或者它是一个被保护的日志，因为JOURNALMODE_PERSIST 或 JOURNALMODE_TRUNCATE。
** If the journal really is hot, reset the pager cache prior rolling
** back any content.  If the journal is merely persistent, no reset is
** needed.若这个日志真是hot，回滚之前重置页面缓存。若日志是持久的，没有重置的必要。
static int pager_playback(Pager *pPager, int isHot){
  sqlite3_vfs *pVfs = pPager->pVfs;
  i64 szJ;                 /* Size of the journal file in bytes */  //日志文件的大小,单位为字节
  u32 nRec;                /* Number of Records in the journal */   //日志中记录的数量
  u32 u;                   /* Unsigned loop counter */              //无符号循环计数器
  int rc;                  /* Result code of a subroutine */        //结果代码的子例程
  int res = 1;             /* Value returned by sqlite3OsAccess() */  //sqlite3OsAccess()返回的值
  char *zMaster = 0;       /* Name of master journal file if any */   //主日志文件的名称
  int needPagerReset;      /* True to reset page prior to first page rollback */  //第一个页面回滚之前重置页面

  /* Figure out how many records are in the journal.  Abort early if
  ** the journal is empty.
  */ //找出在文件中有多少记录，若日志是空的，就要终止。
  assert( isOpen(pPager->jfd) );
  rc = sqlite3OsFileSize(pPager->jfd, &szJ);
  if( rc!=SQLITE_OK ){
    goto end_playback;
  }

  /* Read the master journal name from the journal, if it is present.
  ** If a master journal file name is specified, but the file is not
  ** present on disk, then the journal is not hot and does not need to be
  ** played back.  
  **，如果日志存在，读主日志的名称从这个日志中。如果指定一个主日志的名字，但是文件不是在磁盘中，则这个日志不是hot，不需要被回滚。
  ** TODO: Technically the following is an error because it assumes that
  ** buffer Pager.pTmpSpace is (mxPathname+1) bytes or larger. i.e. that
  ** (pPager->pageSize >= pPager->pVfs->mxPathname+1). Using os_unix.c,
  **  mxPathname is 512, which is the same as the minimum allowable value
  ** for pageSize.
  */ //备注：下面是一个错误，因为它假设缓冲Pager.pTmpSpace是mxPathname+1字节或更大。比如pPager->pageSize >= pPager->pVfs->mxPathname+1)。
  //用os_unix.c,  mxPathname是512个字节，跟页面最小分配的值一样。
  zMaster = pPager->pTmpSpace;
  rc = readMasterJournal(pPager->jfd, zMaster, pPager->pVfs->mxPathname+1);
  if( rc==SQLITE_OK && zMaster[0] ){
    rc = sqlite3OsAccess(pVfs, zMaster, SQLITE_ACCESS_EXISTS, &res);
  }
  zMaster = 0;
  if( rc!=SQLITE_OK || !res ){
    goto end_playback;
  }
  pPager->journalOff = 0;
  needPagerReset = isHot;

  /* This loop terminates either when a readJournalHdr() or 
  ** pager_playback_one_page() call returns SQLITE_DONE or an IO error 
  ** occurs. 
  */   //readJournalHdr()或者pager_playback_one_page()被调用并返回SQLITE_DONE或者一个IO错误时，这个循环终止。
  while( 1 ){
    /* Read the next journal header from the journal file.  If there are
    ** not enough bytes left in the journal file for a complete header, or
    ** it is corrupted, then a process must have failed while writing it.
    ** This indicates nothing more needs to be rolled back.
    */ //从日志文件中读下一个日志头文件。若没有足够的字节留在日志文件作为一个完整的头文件，或者它被破坏，当对它进行写操作时这个进程必定会失败。
    rc = readJournalHdr(pPager, isHot, szJ, &nRec, &mxPg);
    if( rc!=SQLITE_OK ){ 
      if( rc==SQLITE_DONE ){
        rc = SQLITE_OK;
      }
      goto end_playback;
    }

    /* If nRec is 0xffffffff, then this journal was created by a process
    ** working in no-sync mode. This means that the rest of the journal
    ** file consists of pages, there are no more journal headers. Compute
    ** the value of nRec based on this assumption.
    */  //如果nRec是0xffffffff，这个日志会被工作在no-sync模式的进程创建。这意味着剩余的日志文件包含页面，没有更多的日志头文件。
    // 基于这个假设，计算nRec的值。
    if( nRec==0xffffffff ){
      assert( pPager->journalOff==JOURNAL_HDR_SZ(pPager) );
      nRec = (int)((szJ - JOURNAL_HDR_SZ(pPager))/JOURNAL_PG_SZ(pPager));
    }

    /* If nRec is 0 and this rollback is of a transaction created by this
    ** process and if this is the final header in the journal, then it means
    ** that this part of the journal was being filled but has not yet been
    ** synced to disk.  Compute the number of pages based on the remaining
    ** size of the file.  如果nRec是0，这个进程创建事务的回滚。如果日志文件的最后的头文件，意味着这一部分的日志被填满，但是尚未同步到磁盘。
    ** //计算基于剩余文件的大小的页面的数量。
    ** The third term of the test was added to fix ticket #2565. 添加第三个任期的测试是为了解决标签# 2565
    ** When rolling back a hot journal, nRec==0 always means that the next
    ** chunk of the journal contains zero pages to be rolled back.  But 当回滚一个hot journal，nRec==0 意味着下一块的日志包含0页面被回滚。
    ** when doing a ROLLBACK and the nRec==0 chunk is the last chunk in 但是当执行一次回滚和nRec==0的块是日志中最后的一个块，
    ** the journal, it means that the journal might contain additional它意味着日志可能包含附加的需要回滚的页面，页面的数量也应该基于日志文件的大小计算。
    ** pages that need to be rolled back and that the number of pages 
    ** should be computed based on the journal file size.
    */
    if( nRec==0 && !isHot &&
        pPager->journalHdr+JOURNAL_HDR_SZ(pPager)==pPager->journalOff ){
      nRec = (int)((szJ - pPager->journalOff) / JOURNAL_PG_SZ(pPager));
    }

    /* If this is the first header read from the journal, truncate the
    ** database file back to its original size.
    */  //若从日志中读的是第一个日志头文件，截取数据库文件为它的原始大小。
    if( pPager->journalOff==JOURNAL_HDR_SZ(pPager) ){
      rc = pager_truncate(pPager, mxPg);
      if( rc!=SQLITE_OK ){
        goto end_playback;
      }
      pPager->dbSize = mxPg;
    }

    /* Copy original pages out of the journal and back into the 
    ** database file and/or page cache.
    */ //复制日志中的原始页，返回到数据库文件或者页面缓存。
    for(u=0; u<nRec; u++){
      if( needPagerReset ){
        pager_reset(pPager);
        needPagerReset = 0;
      }
      rc = pager_playback_one_page(pPager,&pPager->journalOff,0,1,0);
      if( rc!=SQLITE_OK ){
        if( rc==SQLITE_DONE ){
          pPager->journalOff = szJ;
          break;
        }else if( rc==SQLITE_IOERR_SHORT_READ ){
          /* If the journal has been truncated, simply stop reading and
          ** processing the journal. This might happen if the journal was
          ** not completely written and synced prior to a crash.  In that
          ** case, the database should have never been written in the
          ** first place so it is OK to simply abandon the rollback. */
          //如果日志已经被截取，停止读和处理日志。在崩溃之前，如果日志没有完全被写和同步，这有可能发生。
          //在这种情况下，首先数据库从来没有被写，所以简单的放弃回滚是可以的。
          rc = SQLITE_OK;
          goto end_playback;
        }else{
          /* If we are unable to rollback, quit and return the error
          ** code.  This will cause the pager to enter the error state
          ** so that no further harm will be done.  Perhaps the next
          ** process to come along will be able to rollback the database.
          */  //如果我们不能回滚，退出并返回错误代码。这将造成pager进入错误状态，以至于没有更大的损害。
          //也许下一个流程能回滚数据库。
          goto end_playback;
        }
      }
    }
  }
  /*NOTREACHED*/
  assert( 0 );

end_playback:
  /* Following a rollback, the database file should be back in its original
  ** state prior to the start of the transaction, so invoke the
  ** SQLITE_FCNTL_DB_UNCHANGED file-control method to disable the
  ** assertion that the transaction counter was modified.
  */ //回滚后，数据库文件在开始事务之前应该能回到它的原始状态，所以调用SQLITE_FCNTL_DB_UNCHANGED文件控制方法不能断言事务计数器被修改。
#ifdef SQLITE_DEBUG
  if( pPager->fd->pMethods ){
    sqlite3OsFileControlHint(pPager->fd,SQLITE_FCNTL_DB_UNCHANGED,0);
  }
#endif

  /* If this playback is happening automatically as a result of an IO or     如果回放作为IO的结果自动发生，
  ** malloc error that occurred after the change-counter was updated but     或者在change-counter更新之后但在事务提交之前发生分配错误。
  ** before the transaction was committed, then the change-counter           则change-counter的改变可能被恢复。
  ** modification may just have been reverted. If this happens in exclusive  若这个发生在exclusive模式，
  ** mode, then subsequent transactions performed by the connection will not 则随后的连接的事务执行，将不会再更新change-counter。
  ** update the change-counter at all. This may lead to cache inconsistency
  ** problems for other processes at some point in the future. So, just      这可能会导致缓存不一致，对于其他进程在未来的某一时刻。

  ** in case this has happened, clear the changeCountDone flag now.          以防发生，现在清除changeCountDone标志。
  */
  pPager->changeCountDone = pPager->tempFile;

  if( rc==SQLITE_OK ){
    zMaster = pPager->pTmpSpace;
    rc = readMasterJournal(pPager->jfd, zMaster, pPager->pVfs->mxPathname+1);
    testcase( rc!=SQLITE_OK );
  }
  if( rc==SQLITE_OK
   && (pPager->eState>=PAGER_WRITER_DBMOD || pPager->eState==PAGER_OPEN)
  ){
    rc = sqlite3PagerSync(pPager);
  }
  if( rc==SQLITE_OK ){
    rc = pager_end_transaction(pPager, zMaster[0]!='\0');
    testcase( rc!=SQLITE_OK );
  }
  if( rc==SQLITE_OK && zMaster[0] && res ){
    /* If there was a master journal and this routine will return success,
    ** see if it is possible to delete the master journal.
    */  //如果有一个主日志，这个程序将会返回成功，看删除主日志是否是可能的。
    rc = pager_delmaster(pPager, zMaster);
    testcase( rc!=SQLITE_OK );
  }

  /* The Pager.sectorSize variable may have been updated while rolling  Pager.sectorSize变量可能已经被更新，
  ** back a journal created by a process with a different sector size   当回滚由一个有不同扇区大小值的进程创建的日志。
  ** value. Reset it to the correct value for this process.             为这个进程重置正确的值。
  */
  setSectorSize(pPager);
  return rc;
}


/*
** Read the content for page pPg out of the database file and into      从数据库中读页面pPg 的内容，并赋给pPg->pData。
** pPg->pData. A shared lock or greater must be held on the database    在这个函数被调用之前，一个共享锁或者更高级的必须在数据库文件中进行。
** file before this function is called.
** 
** If page 1 is read, then the value of Pager.dbFileVers[] is set to     若页面1被读，Pager.dbFileVers[]的值将根据从数据库文件读的值设置。
** the value read from the database file.
**
** If an IO error occurs, then the IO error is returned to the caller. 若发生IO错误，则这个IO错误将会返回给调用者。否则将返回SQLITE_OK。
** Otherwise, SQLITE_OK is returned.
*/
static int readDbPage(PgHdr *pPg){
  Pager *pPager = pPg->pPager; /* Pager object associated with page pPg */ //Pager对象相关的页面 pPg
  Pgno pgno = pPg->pgno;       /* Page number to read */                   //读的页码
  int rc = SQLITE_OK;          /* Return code */                           //返回值
  int isInWal = 0;             /* True if page is in log file */           //若页面是个日志文件为真
  int pgsz = pPager->pageSize; /* Number of bytes to read */               //读取的字节数

  assert( pPager->eState>=PAGER_READER && !MEMDB );
  assert( isOpen(pPager->fd) );

  if( NEVER(!isOpen(pPager->fd)) ){
    assert( pPager->tempFile );
    memset(pPg->pData, 0, pPager->pageSize);
    return SQLITE_OK;
  }

  if( pagerUseWal(pPager) ){
    /* Try to pull the page from the write-ahead log. */    
    rc = sqlite3WalRead(pPager->pWal, pgno, &isInWal, pgsz, pPg->pData);
  }
  if( rc==SQLITE_OK && !isInWal ){
    i64 iOffset = (pgno-1)*(i64)pPager->pageSize;
    rc = sqlite3OsRead(pPager->fd, pPg->pData, pgsz, iOffset);
    if( rc==SQLITE_IOERR_SHORT_READ ){
      rc = SQLITE_OK;
    }
  }

  if( pgno==1 ){
    if( rc ){
      /* If the read is unsuccessful, set the dbFileVers[] to something     若读没有成功，设置dbFileVers[]为无效的。
      ** that will never be a valid file version.  dbFileVers[] is a copyd  bFileVers[]是数据库的24..39字节。
      ** of bytes 24..39 of the database.  Bytes 28..31 should always be    字节28..31应该总是为0或者数据库页面的大小。
      ** zero or the size of the database in page. Bytes 32..35 and 35..39  字节32..35和35..39应该是页面的码数，但不可能为0xffffffff。
      ** should be page numbers which are never 0xffffffff.  So filling
      ** pPager->dbFileVers[] with all 0xff bytes should suffice.            所以用所有255字节填充pPager->dbFileVers[]数组应该可以满足。
      **
      ** For an encrypted database, the situation is more complex:  bytes    对于密文数据库，情况更复杂。数据库的字节24..39是白色噪音。但是
      ** 24..39 of the database are white noise.  But the probability of      白色噪音的几率相当于16/255，非常的小，所以我们应该能成功。
      ** white noising equaling 16 bytes of 0xff is vanishingly small so
      ** we should still be ok.
      */
      memset(pPager->dbFileVers, 0xff, sizeof(pPager->dbFileVers));
    }else{
      u8 *dbFileVers = &((u8*)pPg->pData)[24];
      memcpy(&pPager->dbFileVers, dbFileVers, sizeof(pPager->dbFileVers));
    }
  }
  CODEC1(pPager, pPg->pData, pgno, 3, rc = SQLITE_NOMEM);

  PAGER_INCR(sqlite3_pager_readdb_count);
  PAGER_INCR(pPager->nRead);
  IOTRACE(("PGIN %p %d\n", pPager, pgno));
  PAGERTRACE(("FETCH %d page %d hash(%08x)\n",
               PAGERID(pPager), pgno, pager_pagehash(pPg)));

  return rc;
}

/*
** Update the value of the change-counter at offsets 24 and 92 in             更新change-counter的值，偏移24和92在头和sqlite版本号使偏移96.
** the header and the sqlite version number at offset 96.           
**
** This is an unconditional update.  See also the pager_incr_changecounter()   这是一个无条件的更新。看pager_incr_changecounter()程序，这个程序仅仅更新change-counter
** routine which only updates the change-counter if the update is actually     若更新真的需要，作为pPager->changeCountDone状态的变量。
** needed, as determined by the pPager->changeCountDone state variable.
*/
static void pager_write_changecounter(PgHdr *pPg){
  u32 change_counter;

  /* Increment the value just read and write it back to byte 24. */            //增量的值只是度和写回24个字节。
  change_counter = sqlite3Get4byte((u8*)pPg->pPager->dbFileVers)+1;
  put32bits(((char*)pPg->pData)+24, change_counter);

  /* Also store the SQLite version number in bytes 96..99 and in                //同样存储SQLite版本号在字节96到99，在字节92到95存储有效版本号的改变计数器，
  ** bytes 92..95 store the change counter for which the version number
  ** is valid. */
  put32bits(((char*)pPg->pData)+92, change_counter);
  put32bits(((char*)pPg->pData)+96, SQLITE_VERSION_NUMBER);
}

#ifndef SQLITE_OMIT_WAL
/*
** This function is invoked once for each page that has already been            这个函数是为每一个页面,该页面已经被调用一次
** written into the log file when a WAL transaction is rolled back.             写入日志文件当WAL事务回滚。
** Parameter iPg is the page number of said page. The pCtx argument             参数iPg表示页面的页码。
** is actually a pointer to the Pager structure.                                pCtx参数实际上是一个指针结构。
**
** If page iPg is present in the cache, and has no outstanding references,      若页面iPg在目前的缓存中，没有明显的引用，它将被丢弃。
** it is discarded. Otherwise, if there are one or more outstanding             否则，若这有一个或者更多的明显的引用，页面的内容将会从数据库重新加载。
** references, the page content is reloaded from the database. If the
** attempt to reload content from the database is required and fails,            如果试图从数据库中加载的内容是必须的，但是失败，返回一个SQLite错误代码，否则返回SQLITE_OK。
** return an SQLite error code. Otherwise, SQLITE_OK.
*/
static int pagerUndoCallback(void *pCtx, Pgno iPg){
  int rc = SQLITE_OK;
  Pager *pPager = (Pager *)pCtx;
  PgHdr *pPg;

  pPg = sqlite3PagerLookup(pPager, iPg);
  if( pPg ){
    if( sqlite3PcachePageRefcount(pPg)==1 ){
      sqlite3PcacheDrop(pPg);
    }else{
      rc = readDbPage(pPg);
      if( rc==SQLITE_OK ){
        pPager->xReiniter(pPg);
      }
      sqlite3PagerUnref(pPg);
    }
  }

  /* Normally, if a transaction is rolled back, any backup processes are  通常的，若一个事务被回滚，任何的进程
  ** updated as data is copied out of the rollback journal and into the   被更新，作为一种从日志中回滚出来，进入数据库的数据。
  ** database. This is not generally possible with a WAL database, as     对于一个WAL数据库，这通常是不可能的，回滚就是删除日志文件。
  ** rollback involves simply truncating the log file. Therefore, if one
  ** or more frames have already been written to the log (and therefore    如果一个或者更多的框架被写进日志，（所以复制为数据库备份）作为事务的一部分。
  ** also copied into the backup databases) as part of this transaction,
  ** the backups must be restarted.                                         备份必须重新启动。
  */ 
  sqlite3BackupRestart(pPager->pBackup);

  return rc;
}

/*
** This function is called to rollback a transaction on a WAL database.    //在WAL数据库中，这个函数被调用为回滚事务。
*/
static int pagerRollbackWal(Pager *pPager){  
  int rc;                         /* Return Code */                        //返回值
  PgHdr *pList;                   /* List of dirty pages to revert */      //dirty页面恢复列表

  /* For all pages in the cache that are currently dirty or have already   //对于缓存中所有的页面为dirty或者已经被写进日志文件，做下列之一：
  ** been written (but not committed) to the log file, do one of the 
  ** following:
  **
  **   + Discard the cached page (if refcount==0), or                     若refcount==0丢弃缓存页面或者
  **   + Reload page content from the database (if refcount>0).           若refcount>0从数据库中重新加载页面内容
  */
  pPager->dbSize = pPager->dbOrigSize;
  rc = sqlite3WalUndo(pPager->pWal, pagerUndoCallback, (void *)pPager);
  pList = sqlite3PcacheDirtyList(pPager->pPCache);
  while( pList && rc==SQLITE_OK ){
    PgHdr *pNext = pList->pDirty;
    rc = pagerUndoCallback((void *)pPager, pList->pgno);
    pList = pNext;
  }

  return rc;
}

/*
** This function is a wrapper around sqlite3WalFrames(). As well as logging    这个函数是sqlite3WalFrames()函数的一个包装。
** the contents of the list of pages headed by pList (connected by pDirty),    以及pList记录页面列表的内容(由pDirty连接)
** this function notifies any active backup processes that the pages have      这个函数通知所有活动（页面已经改变）的备份流程，
** changed. 
**
** The list of pages passed into this routine is always sorted by page number.   页面的列表传递给这个程序总是按页码排序。
** Hence, if page 1 appears anywhere on the list, it will be the first page.     因此，若页面1出现在列表的任意地方，它都是第一个页面。
*/ 
static int pagerWalFrames(       
  Pager *pPager,                  /* Pager object */                            //Pager对象
  PgHdr *pList,                   /* List of frames to log */                 
  Pgno nTruncate,                 /* Database size after this commit */         //提交后数据库的大小
  int isCommit                    /* True if this is a commit */               //若提交了则为真
){ 
  int rc;                         /* Return code */
  int nList;                      /* Number of pages in pList */              //pList中的页面的数量
#if defined(SQLITE_DEBUG) || defined(SQLITE_CHECK_PAGES)
  PgHdr *p;                       /* For looping over pages */               //在页面中循环
#endif

  assert( pPager->pWal );
  assert( pList );
#ifdef SQLITE_DEBUG
  /* Verify that the page list is in accending order */                  //验证页面列表在accending顺序
  for(p=pList; p && p->pDirty; p=p->pDirty){
    assert( p->pgno < p->pDirty->pgno );
  }
#endif

  assert( pList->pDirty==0 || isCommit );
  if( isCommit ){
    /* If a WAL transaction is being committed, there is no point in writing     若WAL事务已经被提交，
    ** any pages with page numbers greater than nTruncate into the WAL file.      把比nTruncate的页码大的任何页面写进WAL文件是没有任何意义的。
    ** They will never be read by any client. So remove them from the pDirty      他们将不被任何客户机读取，所以从pDirty列表移走他们。
    ** list here. */
    PgHdr *p;
    PgHdr **ppNext = &pList;
    nList = 0;
    for(p=pList; (*ppNext = p)!=0; p=p->pDirty){
      if( p->pgno<=nTruncate ){
        ppNext = &p->pDirty;
        nList++;
      }
    }
    assert( pList );
  }else{
    nList = 1;
  }
  pPager->aStat[PAGER_STAT_WRITE] += nList;

  if( pList->pgno==1 ) pager_write_changecounter(pList);
  rc = sqlite3WalFrames(pPager->pWal, 
      pPager->pageSize, pList, nTruncate, isCommit, pPager->walSyncFlags
  );
  if( rc==SQLITE_OK && pPager->pBackup ){
    PgHdr *p;
    for(p=pList; p; p=p->pDirty){
      sqlite3BackupUpdate(pPager->pBackup, p->pgno, (u8 *)p->pData);
    }
  }

#ifdef SQLITE_CHECK_PAGES
  pList = sqlite3PcacheDirtyList(pPager->pPCache);
  for(p=pList; p; p=p->pDirty){
    pager_set_pagehash(p);
  }
#endif

  return rc;
}

/*
** Begin a read transaction on the WAL.                                          在WAL开始一个读事务。
**
** This routine used to be called "pagerOpenSnapshot()" because it essentially   这段程序通常调用pagerOpenSnapshot()，所以他会及时的对当前数据库做一个快照，
** makes a snapshot of the database at the current point in time and preserves   并保存这个快照，为读者使用，尽管其他写进程或检查点同时发生变化。
** that snapshot for use by the reader in spite of concurrently changes by
** other writers or checkpointers.
*/
static int pagerBeginReadTransaction(Pager *pPager){
  int rc;                         /* Return code */                              
  int changed = 0;                /* True if cache must be reset */            //若缓存必须重置则返回true

  assert( pagerUseWal(pPager) );
  assert( pPager->eState==PAGER_OPEN || pPager->eState==PAGER_READER );
 
  /* sqlite3WalEndReadTransaction() was not called for the previous           //sqlite3WalEndReadTransaction()没有调用在locking_mode=EXCLUSIVE
  ** transaction in locking_mode=EXCLUSIVE.  So call it now.  If we           //现在调用它，若我们在locking_mode=NORMAL模式下，并且之前调用了EndRead()
  ** are in locking_mode=NORMAL and EndRead() was previously called,         
  ** the duplicate call is harmless.                                          //重复调用是没有害的
  */
  sqlite3WalEndReadTransaction(pPager->pWal);

  rc = sqlite3WalBeginReadTransaction(pPager->pWal, &changed);
  if( rc!=SQLITE_OK || changed ){
    pager_reset(pPager);
  }

  return rc;
}
#endif

/*
** This function is called as part of the transition from PAGER_OPEN        //这个函数被调用作为事务的一部分，
** to PAGER_READER state to determine the size of the database file         //从PAGER_OPEN状态到PAGER_READER状态去确定页面中数据库文件的大小
** in pages (assuming the page size currently stored in Pager.pageSize).    //假定Pager.pageSize储存了当前页面的大小。
**
** If no error occurs, SQLITE_OK is returned and the size of the database    //若没有错误发生，将返回SQLITE_OK或者存储在*pnPage中页面中数据库的大小
** in pages is stored in *pnPage. Otherwise, an error code (perhaps          //否则，将返回错误代码，和没有修改的*pnPage。
** SQLITE_IOERR_FSTAT) is returned and *pnPage is left unmodified.
*/
static int pagerPagecount(Pager *pPager, Pgno *pnPage){
  Pgno nPage;                     /* Value to return via *pnPage */          //通过*pnPage返回值

  /* Query the WAL sub-system for the database size. The WalDbsize()         //为数据库的大小查询WAL子系统。
  ** function returns zero if the WAL is not open (i.e. Pager.pWal==0), or   //WalDbsize()函数返回0，若WAL没有打开，或者若数据库大小不可用。比如Pager.pWal==0
  ** if the database size is not available. The database size is not         //若
  ** available from the WAL sub-system if the log file is empty or           //日志文件为空或者包含无效的提交过的事务，WAL子系统的数据库大小不可用。
  ** contains no valid committed transactions.
  */
  assert( pPager->eState==PAGER_OPEN );
  assert( pPager->eLock>=SHARED_LOCK );
  nPage = sqlite3WalDbsize(pPager->pWal);

  /* If the database size was not available from the WAL sub-system,       //如果数据库大小在WAL子系统中不能用，
  ** determine it based on the size of the database file. If the size      //确定基于数据库文件的大小。若数据库文件的大小不是页面大小的整数倍
  ** of the database file is not an integer multiple of the page-size,
  ** round down to the nearest page. Except, any file larger than 0       //四舍五入到最近的页面。
  ** bytes in size is considered to contain at least one page.          //除了，任何一个比0字节大的页面，被认为至少包含一个页面。
  */
  if( nPage==0 ){
    i64 n = 0;                    /* Size of db file in bytes */        
    assert( isOpen(pPager->fd) || pPager->tempFile); 
    if( isOpen(pPager->fd) ){
      int rc = sqlite3OsFileSize(pPager->fd, &n);
      if( rc!=SQLITE_OK ){
        return rc;
      }
    }
    nPage = (Pgno)((n+pPager->pageSize-1) / pPager->pageSize);
  }

  /* If the current number of pages in the file is greater than the  //若在文件中当前页面的数量比配置的最大的pager号大，
  ** configured maximum pager number, increase the allowed limit so  //增加允许限制，以保证这个文件可以被读。
  ** that the file can be read.
  */
  if( nPage>pPager->mxPgno ){
    pPager->mxPgno = (Pgno)nPage;
  }

  *pnPage = nPage;
  return SQLITE_OK;
}

#ifndef SQLITE_OMIT_WAL
/*
** Check if the *-wal file that corresponds to the database opened by pPager   //检查若被pPager打开的数据库对应的*-wal文件存在，而且数据库不为空
** exists if the database is not empy, or verify that the *-wal file does     //或者若数据库文件为空，则验证*-wal文件不存在。
** not exist (by deleting it) if the database file is empty.
**
** If the database is not empty and the *-wal file exists, open the pager     //如果数据库不为空，而且*-wal文件存在，在WAL模式打开pager。
** in WAL mode.  If the database is empty or if no *-wal file exists and      //如果数据库为空，或者没有*-wal文件存在，而且如果没有错误发生，
** if no error occurs, make sure Pager.journalMode is not set to              //则确保Pager.journalMode没有设置为PAGER_JOURNALMODE_WAL。
** PAGER_JOURNALMODE_WAL.
**
** Return SQLITE_OK or an error code.                                         //返回SQLITE_OK或者错误代码。
**
** The caller must hold a SHARED lock on the database file to call this       //调用这个函数，调用者必须持有共享锁在数据库文件调用这个函数。
** function. Because an EXCLUSIVE lock on the db file is required to delete    //所以数据库文件中的排它锁需要删除WAL，在none-empty数据库。
** a WAL on a none-empty database, this ensures there is no race condition     //它确保没有竞争条件在下面的xAccess()函数和在其他连接条件下正在执行的xDelete()函数之间
** between the xAccess() below and an xDelete() being executed by some 
** other connection.
*/
static int pagerOpenWalIfPresent(Pager *pPager){
  int rc = SQLITE_OK;
  assert( pPager->eState==PAGER_OPEN );
  assert( pPager->eLock>=SHARED_LOCK );

  if( !pPager->tempFile ){
    int isWal;                    /* True if WAL file exists */      //若WAL文件存在，则为True
    Pgno nPage;                   /* Size of the database file */    //数据库文件的大小

    rc = pagerPagecount(pPager, &nPage);
    if( rc ) return rc;
    if( nPage==0 ){
      rc = sqlite3OsDelete(pPager->pVfs, pPager->zWal, 0);
      isWal = 0;
    }else{
      rc = sqlite3OsAccess(
          pPager->pVfs, pPager->zWal, SQLITE_ACCESS_EXISTS, &isWal
      );
    }
    if( rc==SQLITE_OK ){
      if( isWal ){
        testcase( sqlite3PcachePagecount(pPager->pPCache)==0 );
        rc = sqlite3PagerOpenWal(pPager, 0);
      }else if( pPager->journalMode==PAGER_JOURNALMODE_WAL ){
        pPager->journalMode = PAGER_JOURNALMODE_DELETE;
      }
    }
  }
  return rc;
}
#endif

/*
** Playback savepoint pSavepoint. Or, if pSavepoint==NULL, then playback     //回放保存点pSavepoint。或者，若pSavepoint==NULL，则回放整个主日志文件。
** the entire master journal file. The case pSavepoint==NULL occurs when     //当ROLLBACK TO命令在保存点事务中调用，pSavepoint==NULL的情况将会发生。
** a ROLLBACK TO command is invoked on a SAVEPOINT that is a transaction 
** savepoint.
** 
** When pSavepoint is not NULL (meaning a non-transaction savepoint is      //当pSavepoint不为空（意思就是non-transaction保存点被回滚），
** being rolled back), then the rollback consists of up to three stages,    //回滚则包括按指定阶段执行的三个阶段，
** performed in the order specified:
**
**   * Pages are played back from the main journal starting at byte        //页面被回滚。从以偏移PagerSavepoint.iOffset字节开始到PagerSavepoint.iHdrOffset字节的主日志文件
**     offset PagerSavepoint.iOffset and continuing to                    
**     PagerSavepoint.iHdrOffset, or to the end of the main journal        //或者若PagerSavepoint.iHdrOffset为0到主日志文件结束的字节
**     file if PagerSavepoint.iHdrOffset is zero.
** 
**   * If PagerSavepoint.iHdrOffset is not zero, then pages are played    //若PagerSavepoint.iHdrOffset不为0，页面被回滚从紧接着PagerSavepoint.iHdrOffset的日志文件头开始
**     back starting from the journal header immediately following         //到主日志文件结束
**     PagerSavepoint.iHdrOffset to the end of the main journal file.
**
**   * Pages are then played back from the sub-journal file, starting    //页面从子日志文件被回滚，从PagerSavepoint.iSubRec开始一直到日志文件结束。
**     with the PagerSavepoint.iSubRec and continuing to the end of
**     the journal file.
**
** Throughout the rollback process, each time a page is rolled back, the   //在回滚的过程中，每当页面被回滚，对应的位被设置在位图结构
** corresponding bit is set in a bitvec structure (variable pDone in the   
** implementation below). This is used to ensure that a page is only      //这确保在日志中的页面仅仅在第一时间被回滚，
** rolled back the first time it is encountered in either journal.
**
** If pSavepoint is NULL, then pages are only played back from the main   //若pSavepoint为空，页面仅仅从主日志文件回滚，在这种情况下没有必要设置为bitvec。
** journal file. There is no need for a bitvec in this case.
**
** In either case, before playback commences the Pager.dbSize variable    //在这两种情况下，在回滚开始之前，Pager.dbSize变量被设为保存点开始的值。
** is reset to the value that it held at the start of the savepoint 
** (or transaction). No page with a page-number greater than this value   //没有页面页码值大于这个值的页面被回滚。若遇到则跳过。
** is played back. If one is encountered it is simply skipped.
*/
static int pagerPlaybackSavepoint(Pager *pPager, PagerSavepoint *pSavepoint){
  i64 szJ;                 /* Effective size of the main journal */             //主日志有效的大小
  i64 iHdrOff;             /* End of first segment of main-journal records */   //主日志记录第一节的结束
  int rc = SQLITE_OK;      /* Return code */  
  Bitvec *pDone = 0;       /* Bitvec to ensure pages played back only once */   //Bitvec用于确保页面仅仅只回滚一次

  assert( pPager->eState!=PAGER_ERROR );
  assert( pPager->eState>=PAGER_WRITER_LOCKED );

  /* Allocate a bitvec to use to store the set of pages rolled back */        //分配一个bitvec用于存储一组页面的回滚
  if( pSavepoint ){
    pDone = sqlite3BitvecCreate(pSavepoint->nOrig);
    if( !pDone ){
      return SQLITE_NOMEM;
    }
  }

  /* Set the database size back to the value it was before the savepoint   //在保存点被恢复之前，设置数据库大小的值。
  ** being reverted was opened.
  */
  pPager->dbSize = pSavepoint ? pSavepoint->nOrig : pPager->dbOrigSize;
  pPager->changeCountDone = pPager->tempFile;

  if( !pSavepoint && pagerUseWal(pPager) ){
    return pagerRollbackWal(pPager);
  }

  /* Use pPager->journalOff as the effective size of the main rollback   // 用pPager->journalOff来作为主回滚日志的有效大小
  ** journal.  The actual file might be larger than this in              //真实的文件可能比在PAGER_JOURNALMODE_TRUNCATE或 PAGER_JOURNALMODE_PERSIST中大
  ** PAGER_JOURNALMODE_TRUNCATE or PAGER_JOURNALMODE_PERSIST.  But anything   
  ** past pPager->journalOff is off-limits to us.                         //但是任何的过去pPager->journalOff，对于我们是禁止的。
  */ 
  szJ = pPager->journalOff;
  assert( pagerUseWal(pPager)==0 || szJ==0 );

  /* Begin by rolling back records from the main journal starting at     //开始回滚记录从以PagerSavepoint.iOffset开始到下一个日志头的主日志。
  ** PagerSavepoint.iOffset and continuing to the next journal header.    
  ** There might be records in the main journal that have a page number  //这可能有在主日志中有一个页码数大于当前数据库大小的记录，但这将自动跳过。
  ** greater than the current database size (pPager->dbSize) but those
  ** will be skipped automatically.  Pages are added to pDone as they    //当他们被回滚，页面将被加上pDone。
  ** are played back.
  */
  if( pSavepoint && !pagerUseWal(pPager) ){
    iHdrOff = pSavepoint->iHdrOffset ? pSavepoint->iHdrOffset : szJ;
    pPager->journalOff = pSavepoint->iOffset;
    while( rc==SQLITE_OK && pPager->journalOff<iHdrOff ){
      rc = pager_playback_one_page(pPager, &pPager->journalOff, pDone, 1, 1);
    }
    assert( rc!=SQLITE_DONE );
  }else{
    pPager->journalOff = 0;
  }

  /* Continue rolling back records out of the main journal starting at   //继续回滚记录从以第一个日志头文件开始一直到结束的主日志文件
  ** the first journal header seen and continuing until the effective end
  ** of the main journal file.  Continue to skip out-of-range pages and  //继续跳过超出范围的页面，继续对回滚的页面增加pDone
  ** continue adding pages rolled back to pDone.
  */
  while( rc==SQLITE_OK && pPager->journalOff<szJ ){
    u32 ii;            /* Loop counter */
    u32 nJRec = 0;     /* Number of Journal Records */                 //日志记录的数量
    u32 dummy;
    rc = readJournalHdr(pPager, 0, szJ, &nJRec, &dummy);
    assert( rc!=SQLITE_DONE );

    /*
    ** The "pPager->journalHdr+JOURNAL_HDR_SZ(pPager)==pPager->journalOff"   pPager->journalHdr+JOURNAL_HDR_SZ(pPager)==pPager->journalOff的测试
    ** test is related to ticket #2565.  See the discussion in the           跟标签#2565有关。  
    ** pager_playback() function for additional information.                 看对pager_playback()函数附加信息的讨论。
    */ 
    if( nJRec==0 
     && pPager->journalHdr+JOURNAL_HDR_SZ(pPager)==pPager->journalOff
    ){
      nJRec = (u32)((szJ - pPager->journalOff)/JOURNAL_PG_SZ(pPager));
    }
    for(ii=0; rc==SQLITE_OK && ii<nJRec && pPager->journalOff<szJ; ii++){
      rc = pager_playback_one_page(pPager, &pPager->journalOff, pDone, 1, 1);
    }
    assert( rc!=SQLITE_DONE );
  }
  assert( rc!=SQLITE_OK || pPager->journalOff>=szJ );

  /* Finally,  rollback pages from the sub-journal.  Page that were        // 最后，从子日志回滚页面。
  ** previously rolled back out of the main journal (and are hence in pDone)  //页面在主日志文件中首先被回滚。超出范围的跳过
  ** will be skipped.  Out-of-range pages are also skipped.
  */
  if( pSavepoint ){
    u32 ii;            /* Loop counter */
    i64 offset = (i64)pSavepoint->iSubRec*(4+pPager->pageSize);

    if( pagerUseWal(pPager) ){
      rc = sqlite3WalSavepointUndo(pPager->pWal, pSavepoint->aWalData);
    }
    for(ii=pSavepoint->iSubRec; rc==SQLITE_OK && ii<pPager->nSubRec; ii++){
      assert( offset==(i64)ii*(4+pPager->pageSize) );
      rc = pager_playback_one_page(pPager, &offset, pDone, 0, 1);
    }
    assert( rc!=SQLITE_DONE );
  }

  sqlite3BitvecDestroy(pDone);
  if( rc==SQLITE_OK ){
    pPager->journalOff = szJ;
  }

  return rc;
}

/*
** Change the maximum number of in-memory pages that are allowed.   //改变内存页面的最大数目     
*/
void sqlite3PagerSetCachesize(Pager *pPager, int mxPage){
  sqlite3PcacheSetCachesize(pPager->pPCache, mxPage);
}

/*
** Free as much memory as possible from the pager.                //从pager释放尽可能多的内存。
*/
void sqlite3PagerShrink(Pager *pPager){
  sqlite3PcacheShrink(pPager->pPCache);
}

/*
** Adjust the robustness of the database to damage due to OS crashes   //调整当写回滚日志时改变syncs()的数量时，由于操作系统死机或者电源故障造成的破坏数据库的鲁棒性。
** or power failures by changing the number of syncs()s when writing
** the rollback journal.  There are three levels:                       有三个情况
**
**    OFF       sqlite3OsSync() is never called.  This is the default    sqlite3OsSync()函数从来不被调用。这对于一个临时文件和瞬时文件是默认的
**              for temporary and transient files.
**
**    NORMAL    The journal is synced once before writes begin on the    在开始在数据库中写之前，日志被同步一次。
**              database.  This is normally adequate protection, but     这个是一个适当的防护，
**              it is theoretically possible, though very unlikely,      但是
**              that an inopertune power failure could leave the journal  当他被回滚时，不可预测的电源故障可能让日志处于对数据库造成破坏的状态
**              in a state which would cause damage to the database       这在理论上的可能，即使非常不可能。
**              when it is rolled back.
**
**    FULL      The journal is synced twice before writes begin on the       在开始在数据库（有一些附加的信息，日志的头，在两次同步之间被写）中写之前，日志被同步。
**              database (with some additional information - the nRec field
**              of the journal header - being written in between the two
**              syncs).  If we assume that writing a                          如果我们假设编写单个磁盘扇区是不能被改变，则这个模式在回滚期间提供保障，日志不会被破坏而造成
**              single disk sector is atomic, then this mode provides
**              assurance that the journal will not be corrupted to the
**              point of causing damage to the database during rollback.     数据库的破坏。
**
** The above is for a rollback-journal mode.  For WAL mode, OFF continues      上述是rollback-journal模式。对于WAL模式，OFF 仍然意味着没有同步发生。
** to mean that no syncs ever occur.  NORMAL means that the WAL is synced      NORMAL意味着在检查点开始之前同步 WAL，如果整个的WAL被写进数据库，。
** prior to the start of checkpoint and that the database file is synced        数据库被同步作为检查点的结论
** at the conclusion of the checkpoint if the entire content of the WAL
** was written back into the database.  But no sync operations occur for       但是在一个普通的提交在WAL模式下的NORMAL，不会有同步发生。
** an ordinary commit in NORMAL mode with WAL.  FULL means that the WAL        FULL意味着，WAL文件被同步在每个提交操作之后，除了跟NORMAL有关的同步。
** file is synced following each commit operation, in addition to the
** syncs associated with NORMAL.
**
** Do not confuse synchronous=FULL with SQLITE_SYNC_FULL.  The                不要混淆synchronous=FULL和SQLITE_SYNC_FULL。
** SQLITE_SYNC_FULL macro means to use the MacOSX-style full-fsync             SQLITE_SYNC_FULL宏，意味着使用使用用fcntl(F_FULLFSYNC)的MacOSX-style full-fsync。
** using fcntl(F_FULLFSYNC).  SQLITE_SYNC_NORMAL means to do an                 SQLITE_SYNC_NORMAL意味着座椅这普通的 fsync()函数调用。
** ordinary fsync() call.  There is no difference between SQLITE_SYNC_FULL
** and SQLITE_SYNC_NORMAL on platforms other than MacOSX.  But the             除了MacOSX之外的平台SQLITE_SYNC_FULL和SQLITE_SYNC_NORMAL没有太大的差别。
** synchronous=FULL versus synchronous=NORMAL setting determines when           但是synchronous=FULL 对比synchronous=NORMAL的设置，决定当原始的xSync被调用，
** the xSync primitive is called and is relevant to all platforms.               并且跟所有的平台相关。
**
** Numeric values associated with these states are OFF==1, NORMAL=2,        这些状态数值型的值为 OFF==1, NORMAL=2，FULL=3
** and FULL=3.
*/
#ifndef SQLITE_OMIT_PAGER_PRAGMAS
void sqlite3PagerSetSafetyLevel(
  Pager *pPager,        /* The pager to set safety level for */       //pager设置安全级别
  int level,            /* PRAGMA synchronous.  1=OFF, 2=NORMAL, 3=FULL */   
  int bFullFsync,       /* PRAGMA fullfsync */
  int bCkptFullFsync    /* PRAGMA checkpoint_fullfsync */
){
  assert( level>=1 && level<=3 );
  pPager->noSync =  (level==1 || pPager->tempFile) ?1:0;
  pPager->fullSync = (level==3 && !pPager->tempFile) ?1:0;
  if( pPager->noSync ){
    pPager->syncFlags = 0;
    pPager->ckptSyncFlags = 0;
  }else if( bFullFsync ){
    pPager->syncFlags = SQLITE_SYNC_FULL;
    pPager->ckptSyncFlags = SQLITE_SYNC_FULL;
  }else if( bCkptFullFsync ){
    pPager->syncFlags = SQLITE_SYNC_NORMAL;
    pPager->ckptSyncFlags = SQLITE_SYNC_FULL;
  }else{
    pPager->syncFlags = SQLITE_SYNC_NORMAL;
    pPager->ckptSyncFlags = SQLITE_SYNC_NORMAL;
  }
  pPager->walSyncFlags = pPager->syncFlags;
  if( pPager->fullSync ){
    pPager->walSyncFlags |= WAL_SYNC_TRANSACTIONS;
  }
}
#endif

/*
** The following global variable is incremented whenever the library  不管什么时候库企图打开一个临时文件，
** attempts to open a temporary file.  This information is used for   下面的全局变量都会增加。这段信息仅仅用来测试和分析。
** testing and analysis only.  
*/
#ifdef SQLITE_TEST
int sqlite3_opentemp_count = 0;
#endif

/*
** Open a temporary file.                                                  //打开一个临时文件
**
** Write the file descriptor into *pFile. Return SQLITE_OK on success      //写文件描述符给*pFile。若成功返回SQLITE_OK，或者若失败则返回错误代码。
** or some other error code if we fail. The OS will automatically          
** delete the temporary file when it is closed.                            //当它被关闭，数据库系统将自动的删除临时文件。
**
** The flags passed to the VFS layer xOpen() call are those specified       //参数通过VFS层调用xOpen()，这些具体的参数为
** by parameter vfsFlags ORed with the following:
**
**     SQLITE_OPEN_READWRITE
**     SQLITE_OPEN_CREATE
**     SQLITE_OPEN_EXCLUSIVE
**     SQLITE_OPEN_DELETEONCLOSE
*/
static int pagerOpentemp(
  Pager *pPager,        /* The pager object */                         
  sqlite3_file *pFile,  /* Write the file descriptor here */        //写文件的描述
  int vfsFlags          /* Flags passed through to the VFS */    
  int rc;               /* Return code */

#ifdef SQLITE_TEST
  sqlite3_opentemp_count++;  /* Used for testing and analysis only */
#endif

  vfsFlags |=  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
            SQLITE_OPEN_EXCLUSIVE | SQLITE_OPEN_DELETEONCLOSE;
  rc = sqlite3OsOpen(pPager->pVfs, 0, pFile, vfsFlags, 0);
  assert( rc!=SQLITE_OK || isOpen(pFile) );
  return rc;
}

/*
** Set the busy handler function.                                       //设置忙处理函数
**
** The pager invokes the busy-handler if sqlite3OsLock() returns        //pager引起busy-handler，当试图更新从无锁到共享锁，若sqlite3OsLock()返回SQLITE_BUSY。
** SQLITE_BUSY when trying to upgrade from no-lock to a SHARED lock,  
** or when trying to upgrade from a RESERVED lock to an EXCLUSIVE        // 或者试图更新从保留锁到排它锁。
** lock. It does *not* invoke the busy handler when upgrading from       //当更新从SHARED到RESERVED，
** SHARED to RESERVED, or when upgrading from SHARED to EXCLUSIVE        //或者更新从SHARED到EXCLUSIVE（发生在hot-journal回滚期间）不会引起busy-handler。
** (which occurs during hot-journal rollback). Summary:                  总结如下
**
**   Transition                        | Invokes xBusyHandler         转换                  是否引起BusyHandler
**   --------------------------------------------------------
**   NO_LOCK       -> SHARED_LOCK      | Yes
**   SHARED_LOCK   -> RESERVED_LOCK    | No
**   SHARED_LOCK   -> EXCLUSIVE_LOCK   | No
**   RESERVED_LOCK -> EXCLUSIVE_LOCK   | Yes
**
** If the busy-handler callback returns non-zero, the lock is       //若回调busy-handler，返回non-zero，锁将会再试。
** retried. If it returns zero, then the SQLITE_BUSY error is       //若返回0，则pager的API函数将返回SQLITE_BUSY错误。
** returned to the caller of the pager API function. 
*/
void sqlite3PagerSetBusyhandler(
  Pager *pPager,                       /* Pager object */
  int (*xBusyHandler)(void *),         /* Pointer to busy-handler function */   //指向busy-handle函数
  void *pBusyHandlerArg                /* Argument to pass to xBusyHandler */   //传给xBusyHandler的参数 
){  
  pPager->xBusyHandler = xBusyHandler;
  pPager->pBusyHandlerArg = pBusyHandlerArg;
}
//我的到此结束。
/*
** Change the page size used by the Pager object. The new page size 
** is passed in *pPageSize.

**改变所使用的pager对象的页大小。新的页面大小传入* pPageSize。

** If the pager is in the error state when this function is called, it
** is a no-op. The value returned is the error state error code (i.e. 
** one of SQLITE_IOERR, an SQLITE_IOERR_xxx sub-code or SQLITE_FULL).

**如果在函数调用时pager处于错误状态，则这是一种无操作。返回的值是错误状态的错误代码（即
 1 SQLITE_IOERR的，一个SQLITE_IOERR_xxx子码或SQLITE_FULL）。

** Otherwise, if all of the following are true:
**否则，如果下面的条件都为真：

**   * the new page size (value of *pPageSize) is valid (a power 
**     of two between 512 and SQLITE_MAX_PAGE_SIZE, inclusive), and
** 新的页面大小（*pPageSize 的值）是有效的（含两个权值在512和最大页面值之间的），并且

**   * there are no outstanding page references, and
** 没有突出的页面引用，以及

**   * the database is either not an in-memory database or it is
**     an in-memory database that currently consists of zero pages.
**不是一个内存数据库或一个由零页开始的内存数据库

** then the pager object page size is set to *pPageSize.
**然后该pager对象页大小设置为* pPageSize。

** If the page size is changed, then this function uses sqlite3PagerMalloc() 
** to obtain a new Pager.pTmpSpace buffer. If this allocation attempt 
** fails, SQLITE_NOMEM is returned and the page size remains unchanged. 
** In all other cases, SQLITE_OK is returned.
**如果页面大小发生变化，那么这个功能使用sqlite3PagerMalloc（）
   以获取新Pager.pTmpSpace缓冲区。如果这种分配的尝试
   失败，SQLITE_NOMEM返回的页面大小保持不变。
   在其他所有情况下，SQLITE_OK返回。

** If the page size is not changed, either because one of the enumerated
** conditions above is not true, the pager was in error state when this
** function was called, or because the memory allocation attempt failed, 
** then *pPageSize is set to the old, retained page size before returning.
   如果页面大小不改变，一个原因是在pager是处于错误状态
   函数被调用时，上面所列举的条件不真，或者是因为内存分配尝试失败，
   然后* pPageSize被设置为旧的，保留的页大小在返回之前。
*/
int sqlite3PagerSetPagesize(Pager *pPager, u32 *pPageSize, int nReserve)
{
  int rc = SQLITE_OK;

  /* It is not possible to do a full assert_pager_state() here, as this
  ** function may be called from within PagerOpen(), before the state
  ** of the Pager object is internally consistent.
  ** 这里不可能做一个完整的assert_pager_state()，在pager对象内部一致的状态
     之前这个函数可能从内部被称为PagerOpen()。

  ** At one point this function returned an error if the pager was in 
  ** PAGER_ERROR state. But since PAGER_ERROR state guarantees that
  ** there is at least one outstanding page reference, this function    
  ** is a no-op for that case anyhow.
     如果pager在PAGER_ERROR的状态这个函数返回一个错误。但由于PAGER_ERROR 状态确保至少有一个突出页面引用
	 在这种情况下，函数无论如何也会是空操作的。
  */

  u32 pageSize = *pPageSize;
  assert( pageSize==0 || (pageSize>=512 && pageSize<=SQLITE_MAX_PAGE_SIZE) );
  if( (pPager->memDb==0 || pPager->dbSize==0)
   && sqlite3PcacheRefCount(pPager->pPCache)==0 
   && pageSize && pageSize!=(u32)pPager->pageSize 
  ){
    char *pNew = NULL;             /* New temp space */ //新的临时空间
    i64 nByte = 0;

    if( pPager->eState>PAGER_OPEN && isOpen(pPager->fd) ){
      rc = sqlite3OsFileSize(pPager->fd, &nByte);
    }
    if( rc==SQLITE_OK ){
      pNew = (char *)sqlite3PageMalloc(pageSize);
      if( !pNew ) rc = SQLITE_NOMEM;
    }

    if( rc==SQLITE_OK ){
      pager_reset(pPager);
      pPager->dbSize = (Pgno)((nByte+pageSize-1)/pageSize);
      pPager->pageSize = pageSize;
      sqlite3PageFree(pPager->pTmpSpace);
      pPager->pTmpSpace = pNew;
      sqlite3PcacheSetPageSize(pPager->pPCache, pageSize);
    }
  }

  *pPageSize = pPager->pageSize;
  if( rc==SQLITE_OK ){
    if( nReserve<0 ) nReserve = pPager->nReserve;
    assert( nReserve>=0 && nReserve<1000 );
    pPager->nReserve = (i16)nReserve;
    pagerReportSize(pPager);
  }
  return rc;
}

/*
** Return a pointer to the "temporary page" buffer held internally
** by the pager.  This is a buffer that is big enough to hold the
** entire content of a database page.  This buffer is used internally
** during rollback and will be overwritten whenever a rollback
** occurs.  But other modules are free to use it too, as long as
** no rollbacks are happening.
  通过pager返回一个内部指向“临时页面”缓冲区的指针。这是一个大得足以容纳整个数据库内容页面的缓冲区。
  这个缓冲区中内部使用回滚和发生回滚时将被重写。但其他模块也可以自由地使用它,只要不发生回滚。

*/
void *sqlite3PagerTempSpace(Pager *pPager){
  return pPager->pTmpSpace;
}

/*
** Attempt to set the maximum database page count if mxPage is positive. 
** Make no changes if mxPage is zero or negative.  And never reduce the
** maximum page count below the current size of the database.
** 如果mxPage是正数则试图设置最大数据库的页面数。如果mxPage是0或者是负数则不改变
   而且当最大页面数低于当前时从不减少数据库大小。

** Regardless of mxPage, return the current maximum page count.
   不管mxPage,返回当前最大的页面数。
*/
int sqlite3PagerMaxPageCount(Pager *pPager, int mxPage){
  if( mxPage>0 ){
    pPager->mxPgno = mxPage;
  }
  assert( pPager->eState!=PAGER_OPEN );      /* Called only by OP_MaxPgcnt */ // 由 OP_MaxPgcn声明
  assert( pPager->mxPgno>=pPager->dbSize );  /* OP_MaxPgcnt enforces this */  // OP_MaxPgcn 强制执行
  return pPager->mxPgno;
}

/*
** The following set of routines are used to disable the simulated
** I/O error mechanism.  These routines are used to avoid simulated
** errors in places where we do not care about errors.
** 下面的例程用于禁用模拟I/O错误机制。 这些例程用于避免某些我们忽略的模拟错误

** Unless -DSQLITE_TEST=1 is used, these routines are all no-ops
** and generate no code.
   除非 -DSQLITE_TEST=1 正在使用， 这些例程全是空操作，没有生存代码。
   
*/
#ifdef SQLITE_TEST
extern int sqlite3_io_error_pending;
extern int sqlite3_io_error_hit;
static int saved_cnt;
void disable_simulated_io_errors(void){
  saved_cnt = sqlite3_io_error_pending;
  sqlite3_io_error_pending = -1;
}
void enable_simulated_io_errors(void){
  sqlite3_io_error_pending = saved_cnt;
}
#else
# define disable_simulated_io_errors()
# define enable_simulated_io_errors()
#endif

/*
** Read the first N bytes from the beginning of the file into memory
** that pDest points to. 
   在内存中读取从pDest存入点开始的第N个字节。
** 
** If the pager was opened on a transient file (zFilename==""), or
** opened on a file less than N bytes in size, the output buffer is
** zeroed and SQLITE_OK returned. The rationale for this is that this 
** function is used to read database headers, and a new transient or
** zero sized database has a header than consists entirely of zeroes.
**  如果pager开了一个临时文件(zFilename = = " "),
    或者打开一个文件小于N个字节大小,输出缓冲区被调到零位并返回SQLITE_OK。
	这个的原理是，这个函数是用来读取数据库标题，比起完全由零组成的有一个新的临时的或者0大小
	的数据库。

** If any IO error apart from SQLITE_IOERR_SHORT_READ is encountered,
** the error code is returned to the caller and the contents of the
** output buffer undefined.
    如果遇到除了SQLITE_IOERR_SHORT_READ外的任何IO错误,
	错误代码返回给调用者和输出缓冲区的内容未定义。
*/
int sqlite3PagerReadFileheader(Pager *pPager, int N, unsigned char *pDest){
  int rc = SQLITE_OK;
  memset(pDest, 0, N);
  assert( isOpen(pPager->fd) || pPager->tempFile );

  /* This routine is only called by btree immediately after creating
  ** the Pager object.  There has not been an opportunity to transition
  ** to WAL mode yet.
     这个程序只在被btree调用后立即创建pager对象。没有一个转变为WAL模式的机会。  
  */
  assert( !pagerUseWal(pPager) );

  if( isOpen(pPager->fd) ){
    IOTRACE(("DBHDR %p 0 %d\n", pPager, N))
    rc = sqlite3OsRead(pPager->fd, pDest, N, 0);
    if( rc==SQLITE_IOERR_SHORT_READ ){
      rc = SQLITE_OK;
    }
  }
  return rc;
}

/*
** This function may only be called when a read-transaction is open on
** the pager. It returns the total number of pages in the database.
**这个函数只能当读取失误在pager中打开时才能调用。 它返回数据库中的页面总数。

** However, if the file is between 1 and <page-size> bytes in size, then 
** this is considered a 1 page file.
   然而,如果该文件是介于1和<页面大小>字节大小,那么这个被认为是一个1页文件。

*/
void sqlite3PagerPagecount(Pager *pPager, int *pnPage){
  assert( pPager->eState>=PAGER_READER );
  assert( pPager->eState!=PAGER_WRITER_FINISHED );
  *pnPage = (int)pPager->dbSize;
}


/*
** Try to obtain a lock of type locktype on the database file. If
** a similar or greater lock is already held, this function is a no-op
** (returning SQLITE_OK immediately).
**试图在数据库文件上获得一个锁的类型为locktype。
如果一个类似或更大的锁已经被持有,这个函数是空操作(返回SQLITE_OK立即)。

** Otherwise, attempt to obtain the lock using sqlite3OsLock(). Invoke 
** the busy callback if the lock is currently not available. Repeat 
** until the busy callback returns false or until the attempt to 
** obtain the lock succeeds.
**  否则， 试图获得使用锁sqlite3OsLock()。 如果锁目前不可用则调用忙回调函数。
    一直重复,直到忙回调函数返回false或直到尝试成功获得锁。

** Return SQLITE_OK on success and an error code if we cannot obtain
** the lock. If the lock is obtained successfully, set the Pager.state 
** variable to locktype before returning.
   如果我们不能获得锁返回SQLITE_OK成功和错误的代码
   如果锁被成功获得，设置Pager.state变量返回之前的LockType。
*/
static int pager_wait_on_lock(Pager *pPager, int locktype){
  int rc;                              /* Return code */ //返回代码

  /* Check that this is either a no-op (because the requested lock is 
  ** already held, or one of the transistions that the busy-handler
  ** may be invoked during, according to the comment above
  ** sqlite3PagerSetBusyhandler().
    检查这也许是一个空操作（根据上述sqlite3PagerSetBusyhandler评论（），
	因为所请求的锁已经被持有，或其中的转换在被调用时可能繁忙的处理程序。

  */
  assert( (pPager->eLock>=locktype)
       || (pPager->eLock==NO_LOCK && locktype==SHARED_LOCK)
       || (pPager->eLock==RESERVED_LOCK && locktype==EXCLUSIVE_LOCK)
  );

  do {
    rc = pagerLockDb(pPager, locktype);
  }while( rc==SQLITE_BUSY && pPager->xBusyHandler(pPager->pBusyHandlerArg) );
  return rc;
}

/*
** Function assertTruncateConstraint(pPager) checks that one of the 
** following is true for all dirty pages currently in the page-cache:
** //assertTruncateConstraint(pPager)函数检查下面情况之一是在页面缓存
     中适用于所有当前脏页。    

**   a) The page number is less than or equal to the size of the 
**      current database image, in pages, OR
        1、页面数小于或等于当前的数据库的图像的大小，以页为单位， 或者
**
**   b) if the page content were written at this time, it would not
**      be necessary to write the current content out to the sub-journal
**      (as determined by function subjRequiresPage()).
**     2、  如果页面内容写在这个时候,
           它就没有必要写当前内容的日志下(由函数subjRequiresPage())。
     
** If the condition asserted by this function were not true, and the
** dirty page were to be discarded from the cache via the pagerStress()
** routine, pagerStress() would not write the current page content to
** the database file. If a savepoint transaction were rolled back after
** this happened, the correct behaviour would be to restore the current
** content of the page. However, since this content is not present in either
** the database file or the portion of the rollback journal and 
** sub-journal rolled back the content could not be restored and the
** database image would become corrupt. It is therefore fortunate that 
** this circumstance cannot arise.
   如果条件声称这个函数是不真的，并且缓存中的脏页通过pagerStress()程序被丢弃，
   pagerStress()不会把当前页面的内容写入数据库文件。
   如果一个保存点事务被回滚后发生,正确的行为将会恢复当前页面的内容。
   然而，由于这些内容不存在于数据库文件或者部分回滚日志和回滚子日志内容中不能被
   恢复，数据库图像会被损坏。
   因此不出现这种情况是幸运的。
*/
#if defined(SQLITE_DEBUG)
static void assertTruncateConstraintCb(PgHdr *pPg){
  assert( pPg->flags&PGHDR_DIRTY );
  assert( !subjRequiresPage(pPg) || pPg->pgno<=pPg->pPager->dbSize );
}
static void assertTruncateConstraint(Pager *pPager){
  sqlite3PcacheIterateDirty(pPager->pPCache, assertTruncateConstraintCb);
}
#else
# define assertTruncateConstraint(pPager)
#endif

/*
** Truncate the in-memory database file image to nPage pages. This 
** function does not actually modify the database file on disk. It 
** just sets the internal state of the pager object so that the 
** truncation will be done when the current transaction is committed.
   截断nPage页面的内存数据库文件。这个函数并不实际修改磁盘上的数据库文件。
   它只是设置pager对象的内部状态,当当前事务被提交时截断。
*/
void sqlite3PagerTruncateImage(Pager *pPager, Pgno nPage){
  assert( pPager->dbSize>=nPage );
  assert( pPager->eState>=PAGER_WRITER_CACHEMOD );
  pPager->dbSize = nPage;
  assertTruncateConstraint(pPager);
}


/*
** This function is called before attempting a hot-journal rollback. It
** syncs the journal file to disk, then sets pPager->journalHdr to the
** size of the journal file so that the pager_playback() routine knows
** that the entire journal file has been synced.
**这个函数被调用之前hot-journal回滚。它同步日志文件到磁盘
   然后将pPager-> journalHdr集合到日志文件的大小，
   以便pager_playback() 程序知道整个日志文件已经同步。

** Syncing a hot-journal to disk before attempting to roll it back ensures 
** that if a power-failure occurs during the rollback, the process that
** attempts rollback following system recovery sees the same journal
** content as this process.
**在它回滚事务前同步一个hot-journal到磁盘确保在回滚时若发生电源故障，
  在系统回复后在这个进程中看见同样的日志内容。

** If everything goes as planned, SQLITE_OK is returned. Otherwise, 
** an SQLite error code.
   如果一切按计划进行,返回SQLITE_OK。否则,返回一个SQLite的错误代码。
*/
static int pagerSyncHotJournal(Pager *pPager){
  int rc = SQLITE_OK;
  if( !pPager->noSync ){
    rc = sqlite3OsSync(pPager->jfd, SQLITE_SYNC_NORMAL);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3OsFileSize(pPager->jfd, &pPager->journalHdr);
  }
  return rc;
}

/*
** Shutdown the page cache.  Free all memory and close all files.
**关闭页面缓存。释放所有内存和关闭所有文件。

** If a transaction was in progress when this routine is called, that
** transaction is rolled back.  All outstanding pages are invalidated
** and their memory is freed.  Any attempt to use a page associated
** with this page cache after this function returns will likely
** result in a coredump.
** 如果一个事务在进程中被程序调用发生回滚。 所有突出页面无效并且释放其内存。
   任何试图在函数返回后使用页面相关的页面缓存时可能会导致核心转储。

** This function always succeeds. If a transaction is active an attempt
** is made to roll it back. If an error occurs during the rollback 
** a hot journal may be left in the filesystem but no error is returned
** to the caller.
   这个函数总是成功的。如果一个事务积极的尝试回滚该事务。如果一个错误在回滚到hot-journal
   时可能留在文件系统中，但是不会讲错误放回给调用者。
   
*/
int sqlite3PagerClose(Pager *pPager){
  u8 *pTmp = (u8 *)pPager->pTmpSpace;

  assert( assert_pager_state(pPager) );
  disable_simulated_io_errors();
  sqlite3BeginBenignMalloc();
  /* pPager->errCode = 0; */
  pPager->exclusiveMode = 0;
#ifndef SQLITE_OMIT_WAL
  sqlite3WalClose(pPager->pWal, pPager->ckptSyncFlags, pPager->pageSize, pTmp);
  pPager->pWal = 0;
#endif
  pager_reset(pPager);
  if( MEMDB ){
    pager_unlock(pPager);
  }else{
    /* If it is open, sync the journal file before calling UnlockAndRollback.
    ** If this is not done, then an unsynced portion of the open journal 
    ** file may be played back into the database. If a power failure occurs 
    ** while this is happening, the database could become corrupt.
    **如果它是开放的,同步调用UnlockAndRollback前的日志文件。
	如果不这么做,那么开放日志文件中不同步的部分可能放回到数据库中。
	如果出现电源故障,而这正在发生,数据库可能损坏。

    ** If an error occurs while trying to sync the journal, shift the pager
    ** into the ERROR state. This causes UnlockAndRollback to unlock the
    ** database and close the journal file without attempting to roll it
    ** back or finalize it. The next database user will have to do hot-journal
    ** rollback before accessing the database file.
       如果尝试同步到日志时发生了错误，转换pager到错误状态。
	   这将导致UnlockAndRollback打开数据库并关闭日志文件没有试图回滚该事务或完成它。
	   下一个数据库用户将必须做hot-journal回滚之前访问数据库文件。
    */
    if( isOpen(pPager->jfd) ){
      pager_error(pPager, pagerSyncHotJournal(pPager));
    }
    pagerUnlockAndRollback(pPager);
  }
  sqlite3EndBenignMalloc();
  enable_simulated_io_errors();
  PAGERTRACE(("CLOSE %d\n", PAGERID(pPager)));
  IOTRACE(("CLOSE %p\n", pPager))
  sqlite3OsClose(pPager->jfd);
  sqlite3OsClose(pPager->fd);
  sqlite3PageFree(pTmp);
  sqlite3PcacheClose(pPager->pPCache);

#ifdef SQLITE_HAS_CODEC
  if( pPager->xCodecFree ) pPager->xCodecFree(pPager->pCodec);
#endif

  assert( !pPager->aSavepoint && !pPager->pInJournal );
  assert( !isOpen(pPager->jfd) && !isOpen(pPager->sjfd) );

  sqlite3_free(pPager);
  return SQLITE_OK;
}

#if !defined(NDEBUG) || defined(SQLITE_TEST)
/*
** Return the page number for page pPg.   //返回pPg页的页码
*/
Pgno sqlite3PagerPagenumber(DbPage *pPg){
  return pPg->pgno;
}
#endif

/*
** Increment the reference count for page pPg.  // 增加pPg页的引用计数。
*/
void sqlite3PagerRef(DbPage *pPg){
  sqlite3PcacheRef(pPg);
}

/*
** Sync the journal. In other words, make sure all the pages that have
** been written to the journal have actually reached the surface of the
** disk and can be restored in the event of a hot-journal rollback.
** 日志同步。 换句话说，确保所有的页面都已经写入磁盘的表面，
   可以在hot-journal回滚的情况下被恢复。

** If the Pager.noSync flag is set, then this function is a no-op.
** Otherwise, the actions required depend on the journal-mode and the 
** device characteristics of the file-system, as follows:
** 如果Pager.noSync标志被设置，那么这个函数是一个空操作。 否则，
   所需的行动取决于日志方式和文件系统的设备特征,如下:

**   * If the journal file is an in-memory journal file, no action need
**     be taken.
        如果日志文件是一个内存日志文件,不需要采取任何行动。
**
**   * Otherwise, if the device does not support the SAFE_APPEND property,
**     then the nRec field of the most recently written journal header
**     is updated to contain the number of journal records that have
**     been written following it. If the pager is operating in full-sync
**     mode, then the journal file is synced before this field is updated.
**     否则，如果设备不支持SAFE_APPEND属性，那么最近写入日志头的NRec字段更新为
       一直在写的日志记录数。 如果pager在全同步模式下操作，那么日志文件在字段被更新前同步。

**   * If the device does not support the SEQUENTIAL property, then 
**     journal file is synced.
**     如果设备不支持SEQUENTIAL属性，那么日志文件同步。

** Or, in pseudo-code: // 或者， 在伪代码中：
**
**   if( NOT <in-memory journal> )
{
**     if( NOT SAFE_APPEND ){
**       if( <full-sync mode> ) xSync(<journal file>);
**       <update nRec field>
**     } 
**     if( NOT SEQUENTIAL ) xSync(<journal file>);
**   }
**
** If successful, this routine clears the PGHDR_NEED_SYNC flag of every 
** page currently held in memory before returning SQLITE_OK. If an IO
** error is encountered, then the IO error code is returned to the caller.
   如果成功的话，这个程序会清除PGHDR_NEED_SYNC标志在返回SQLITE_OK之前内存中已有的每一页。
   如果遇到一个IO错误，那么这个IO错误会被返回给调用者。

*/
static int syncJournal(Pager *pPager, int newHdr){
  int rc;                         /* Return code */ // 返回代码

  assert( pPager->eState==PAGER_WRITER_CACHEMOD
       || pPager->eState==PAGER_WRITER_DBMOD
  );
  assert( assert_pager_state(pPager) );
  assert( !pagerUseWal(pPager) );

  rc = sqlite3PagerExclusiveLock(pPager);
  if( rc!=SQLITE_OK ) return rc;

  if( !pPager->noSync ){
    assert( !pPager->tempFile );
    if( isOpen(pPager->jfd) && pPager->journalMode!=PAGER_JOURNALMODE_MEMORY ){
      const int iDc = sqlite3OsDeviceCharacteristics(pPager->fd);
      assert( isOpen(pPager->jfd) );

      if( 0==(iDc&SQLITE_IOCAP_SAFE_APPEND) ){
        /* This block deals with an obscure problem. If the last connection
        ** that wrote to this database was operating in persistent-journal
        ** mode, then the journal file may at this point actually be larger
        ** than Pager.journalOff bytes. If the next thing in the journal
        ** file happens to be a journal-header (written as part of the
        ** previous connection's transaction), and a crash or power-failure 
        ** occurs after nRec is updated but before this connection writes 
        ** anything else to the journal file (or commits/rolls back its 
        ** transaction), then SQLite may become confused when doing the 
        ** hot-journal rollback following recovery. It may roll back all
        ** of this connections data, then proceed to rolling back the old,
        ** out-of-date data that follows it. Database corruption.
        **  此块处理一个不起眼的问题。如果最后一个连接是写在在当前日志模式下操作
		    的数据库下，那么日志文件可能在这一点上大于Pager.journalOff字节。
			如果日志文件接下来的事情正好是一个日志头（写为上一个连接事务的一部分），
			以及崩溃或电源故障发生后NREC被更新，但这种连接之前写入任何东西到日志文件（或提交/回滚其事务），
			那么SQLite的可能在hot-journal回滚恢复时变得无所适从。

        ** To work around this, if the journal file does appear to contain
        ** a valid header following Pager.journalOff, then write a 0x00
        ** byte to the start of it to prevent it from being recognized.
        ** 要解决这个问题，如果日志文件确实出现了包含一个有效的Pager.journalOff头，
		   就要写一个0×00字节到它的开始，以防止它被认可。

        ** Variable iNextHdrOffset is set to the offset at which this
        ** problematic header will occur, if it exists. aMagic is used 
        ** as a temporary buffer to inspect the first couple of bytes of
        ** the potential journal header.
		   变量iNextHdrOffset 被设置为这个会发生的问题头的便宜，如果它存在的话。
		   aMagic被用作临时缓冲器，检查第一对潜在日志头的字节。
        */
        i64 iNextHdrOffset;
        u8 aMagic[8];
        u8 zHeader[sizeof(aJournalMagic)+4];

        memcpy(zHeader, aJournalMagic, sizeof(aJournalMagic));
        put32bits(&zHeader[sizeof(aJournalMagic)], pPager->nRec);

        iNextHdrOffset = journalHdrOffset(pPager);
        rc = sqlite3OsRead(pPager->jfd, aMagic, 8, iNextHdrOffset);
        if( rc==SQLITE_OK && 0==memcmp(aMagic, aJournalMagic, 8) ){
          static const u8 zerobyte = 0;
          rc = sqlite3OsWrite(pPager->jfd, &zerobyte, 1, iNextHdrOffset);
        }
        if( rc!=SQLITE_OK && rc!=SQLITE_IOERR_SHORT_READ ){
          return rc;
        }

        /* Write the nRec value into the journal file header. If in
        ** full-synchronous mode, sync the journal first. This ensures that
        ** all data has really hit the disk before nRec is updated to mark
        ** it as a candidate for rollback.
        ** nRec值写入日志文件头。如果在全同步模式，首先同步的日志。
		这将确保所有数据在nRec更新之前已经达到了磁盘并将其标记为回滚的候选

        ** This is not required if the persistent media supports the
        ** SAFE_APPEND property. Because in this case it is not possible 
        ** for garbage data to be appended to the file, the nRec field
        ** is populated with 0xFFFFFFFF when the journal header is written
        ** and never needs to be updated.
		   持续媒体支持SAFE_APPEND属性不是必需的。
		   因为在这种情况下垃圾数据附加到文件是不可能的,
		   nRec字段是用0xffffffff填充当日志标题被写入时,而且从不需要更新。

        */
        if( pPager->fullSync && 0==(iDc&SQLITE_IOCAP_SEQUENTIAL) ){
          PAGERTRACE(("SYNC journal of %d\n", PAGERID(pPager)));
          IOTRACE(("JSYNC %p\n", pPager))
          rc = sqlite3OsSync(pPager->jfd, pPager->syncFlags);
          if( rc!=SQLITE_OK ) return rc;
        }
        IOTRACE(("JHDR %p %lld\n", pPager, pPager->journalHdr));
        rc = sqlite3OsWrite(
            pPager->jfd, zHeader, sizeof(zHeader), pPager->journalHdr
        );
        if( rc!=SQLITE_OK ) return rc;
      }
      if( 0==(iDc&SQLITE_IOCAP_SEQUENTIAL) ){
        PAGERTRACE(("SYNC journal of %d\n", PAGERID(pPager)));
        IOTRACE(("JSYNC %p\n", pPager))
        rc = sqlite3OsSync(pPager->jfd, pPager->syncFlags| 
          (pPager->syncFlags==SQLITE_SYNC_FULL?SQLITE_SYNC_DATAONLY:0)
        );
        if( rc!=SQLITE_OK ) return rc;
      }

      pPager->journalHdr = pPager->journalOff;
      if( newHdr && 0==(iDc&SQLITE_IOCAP_SAFE_APPEND) ){
        pPager->nRec = 0;
        rc = writeJournalHdr(pPager);
        if( rc!=SQLITE_OK ) return rc;
      }
    }else{
      pPager->journalHdr = pPager->journalOff;
    }
  }

  /* Unless the pager is in noSync mode, the journal file was just 
  ** successfully synced. Either way, clear the PGHDR_NEED_SYNC flag on 
  ** all pages.
     除非pager在不同步的模式下，日志文件只是成功地同步。无论什么方式，
	 明确所有页面PGHDR_NEED_SYNC标志。
  */
  sqlite3PcacheClearSyncFlags(pPager->pPCache);
  pPager->eState = PAGER_WRITER_DBMOD;
  assert( assert_pager_state(pPager) );
  return SQLITE_OK;
}

/*
** The argument is the first in a linked list of dirty pages connected
** by the PgHdr.pDirty pointer.
   这个参数是第一个通过PgHdr.pDirty指针连接的劣质页链表。
** This function writes each one of the
** in-memory pages in the list to the database file. 
   这个函数以表的形式把每一个内存页记录到数据库文件。
** The argument may
** be NULL, representing an empty list.
   这个参数可以是NULL,代表一个空列表。
** In this case this function is
** a no-op.
** 在这种情况下,这个函数是一个空操作。

** The pager must hold at least a RESERVED lock when this function
** is called.
    pager分页类在调用这个函数必须至少持有保留锁。
** Before writing anything to the database file, this lock
** is upgraded to an EXCLUSIVE lock. 
   在写任何数据库文件之前,这个锁升级为独占锁。
** If the lock cannot be obtained,
** SQLITE_BUSY is returned and no data is written to the database file.
**  如果不能获得锁, SQLITE_BUSY被返回并且没有数据被写入数据库文件。

** If the pager is a temp-file pager and the actual file-system file
** is not yet open, it is created and opened before any data is 
** written out.
** 如果pager分页类是一个临时文件pager分页类并且实际文件系统文件尚未开放,
   它会首先被创建和打开在写入任何数据之前。

** Once the lock has been upgraded and, if necessary, the file opened,
** the pages are written out to the database file in list order.
   一旦锁升级,如果有必要的话,该文件被打开,页面将以列表次序写入到数据库文件。
** Writing a page is skipped if it meets either of the following criteria:
** 如果它满足下列标准，“页面写入”这一步将被跳过:
**   * The page number is greater than Pager.dbSize, or
**   * The PGHDR_DONT_WRITE flag is set on the page.
**   页码数大于Pager.dbSize或PGHDR_DONT_WRITE标志被设置在页面上。

** If writing out a page causes the database file to grow, Pager.dbFileSize
** is updated accordingly. 
   如果写入页面使数据库文件增长,Pager.dbFileSize也将相应的更新。
** If page 1 is written out, then the value cached
** in Pager.dbFileVers[] is updated to match the new value stored in
** the database file.
** 如果第1页写出,那么缓存在Pager.dbFileVers[]的值会被更新用来匹配新的存储在数据库文件中的值。

** If everything is successful, SQLITE_OK is returned. If an IO error 
** occurs, an IO error code is returned. Or, if the EXCLUSIVE lock cannot
** be obtained, SQLITE_BUSY is returned.
   如果一切成功,返回SQLITE_OK。如果一个IO错误发生时,则返回IO错误代码。
   又如果独占锁不能被获得,则返回SQLITE_BUSY。

*/
static int pager_write_pagelist(Pager *pPager, PgHdr *pList){
  int rc = SQLITE_OK;                  /* Return code */  //返回代码

  /* This function is only called for rollback pagers in WRITER_DBMOD state. */ 
  // 此函数仅在pager回滚在WRITER_DBMOD状态下被调用。
  assert( !pagerUseWal(pPager) );
  assert( pPager->eState==PAGER_WRITER_DBMOD );
  assert( pPager->eLock==EXCLUSIVE_LOCK );

  /* If the file is a temp-file has not yet been opened, open it now. It
  ** is not possible for rc to be other than SQLITE_OK if this branch
  ** is taken, as pager_wait_on_lock() is a no-op for temp-files.
      如果这是一个临时的未被打开的文件，现在打开它。如果这个分支，
	  作为pager_wait_on_lock（RC比SQLITE_OK除外）是无操作的临时文件，是不可能的。
  */ 
  if( !isOpen(pPager->fd) ){
    assert( pPager->tempFile && rc==SQLITE_OK );
    rc = pagerOpentemp(pPager, pPager->fd, pPager->vfsFlags);
  }

  /* Before the first write, give the VFS a hint of what the final
  ** file size will be.
  在第一次写之前，给VFS一点最终文件大小将是多少的提示
  */
  assert( rc!=SQLITE_OK || isOpen(pPager->fd) );
  if( rc==SQLITE_OK && pPager->dbSize>pPager->dbHintSize ){
    sqlite3_int64 szFile = pPager->pageSize * (sqlite3_int64)pPager->dbSize;
    sqlite3OsFileControlHint(pPager->fd, SQLITE_FCNTL_SIZE_HINT, &szFile);
    pPager->dbHintSize = pPager->dbSize;
  }

  while( rc==SQLITE_OK && pList ){
    Pgno pgno = pList->pgno;

    /* If there are dirty pages in the page cache with page numbers greater
    ** than Pager.dbSize, this means sqlite3PagerTruncateImage() was called to
    ** make the file smaller (presumably by auto-vacuum code). Do not write
    ** any such pages to the file.
    ** 如果缓存中有脏页且页数比Pager.dbSize大， 这意味着sqlite3PagerTruncateImage()将被
	   调用用来使文件变小一点（大概由auto-vacuum 代码）。不要写任何这样的页面文件。

    ** Also, do not write out any page that has the PGHDR_DONT_WRITE flag
    ** set (set by sqlite3PagerDontWrite()).
	   同时，也不要写任何设置了PGHDR_DONT_WRITE（由sqlite3PagerDontWrite()设置）标志的页面
    */
    if( pgno<=pPager->dbSize && 0==(pList->flags&PGHDR_DONT_WRITE) ){
      i64 offset = (pgno-1)*(i64)pPager->pageSize;   /* Offset to write */// 写偏移
      char *pData;                                   /* Data to write */  //写数据 

      assert( (pList->flags&PGHDR_NEED_SYNC)==0 );
      if( pList->pgno==1 ) pager_write_changecounter(pList);

      /* Encode the database */ //编码数据库
      CODEC2(pPager, pList->pData, pgno, 6, return SQLITE_NOMEM, pData);

      /* Write out the page data. *///写出页面数据
      rc = sqlite3OsWrite(pPager->fd, pData, pPager->pageSize, offset);

      /* If page 1 was just written, update Pager.dbFileVers to match
      ** the value now stored in the database file. If writing this 
      ** page caused the database file to grow, update dbFileSize.
	     如果页面1只是写、更新Pager.dbFileVers 来匹配现在存储在数据库文件中的值。
		 如果写这个页面导致数据库文件增长,更新dbFileSize。

      */
      if( pgno==1 ){
        memcpy(&pPager->dbFileVers, &pData[24], sizeof(pPager->dbFileVers));
      }
      if( pgno>pPager->dbFileSize ){
        pPager->dbFileSize = pgno;
      }
      pPager->aStat[PAGER_STAT_WRITE]++;

      /* Update any backup objects copying the contents of this pager. */ //更新复制次pager的内容和备份对象
      sqlite3BackupUpdate(pPager->pBackup, pgno, (u8*)pList->pData);

      PAGERTRACE(("STORE %d page %d hash(%08x)\n",
                   PAGERID(pPager), pgno, pager_pagehash(pList)));
      IOTRACE(("PGOUT %p %d\n", pPager, pgno));
      PAGER_INCR(sqlite3_pager_writedb_count);
    }else{
      PAGERTRACE(("NOSTORE %d page %d\n", PAGERID(pPager), pgno));
    }
    pager_set_pagehash(pList);
    pList = pList->pDirty;
  }

  return rc;
}

/*
** Ensure that the sub-journal file is open. If it is already open, this 
** function is a no-op.
** 确保子日志文件是开放的。如果已经打开,这个函数是一个空操作。
** SQLITE_OK is returned if everything goes according to plan. An 
** SQLITE_IOERR_XXX error code is returned if a call to sqlite3OsOpen() 
** fails.
   如果一切按计划进行，返回SQLITE_OK。 如果调用sqlite3OsOpen()失败，返回一个SQLITE_IOERR_XXX错误。
*/
static int openSubJournal(Pager *pPager){
  int rc = SQLITE_OK;
  if( !isOpen(pPager->sjfd) ){
    if( pPager->journalMode==PAGER_JOURNALMODE_MEMORY || pPager->subjInMemory ){
      sqlite3MemJournalOpen(pPager->sjfd);
    }else{
      rc = pagerOpentemp(pPager, pPager->sjfd, SQLITE_OPEN_SUBJOURNAL);
    }
  }
  return rc;
}

/*
** Append a record of the current state of page pPg to the sub-journal. 
** It is the callers responsibility to use subjRequiresPage() to check 
** that it is really required before calling this function.
** 追加pPg页当前状态记录到子日志中。
  调用者的责任是使用subjRequiresPage()来检查之前真正需要调用这个函数。
** If successful, set the bit corresponding to pPg->pgno in the bitvecs
** for all open savepoints before returning.
** 如果成功， 设置相应的pPg->pgno 在bitvecs中所有打开的返回前的保存点。
** This function returns SQLITE_OK if everything is successful, an IO
** error code if the attempt to write to the sub-journal fails, or 
** SQLITE_NOMEM if a malloc fails while setting a bit in a savepoint
** bitvec.
   如果所有都成功，函数返回SQLITE_OK，如果试图写入子日志失败或者SQLITE_NOMEM 在设置bitvec
   保存点内存分配失败，则会偶一个IO错误代码。
*/
static int subjournalPage(PgHdr *pPg){
  int rc = SQLITE_OK;
  Pager *pPager = pPg->pPager;
  if( pPager->journalMode!=PAGER_JOURNALMODE_OFF ){

    /* Open the sub-journal, if it has not already been opened */// 打开子日志，如果它没有被打开
    assert( pPager->useJournal );
    assert( isOpen(pPager->jfd) || pagerUseWal(pPager) );
    assert( isOpen(pPager->sjfd) || pPager->nSubRec==0 );
    assert( pagerUseWal(pPager) 
         || pageInJournal(pPg) 
         || pPg->pgno>pPager->dbOrigSize 
    );
    rc = openSubJournal(pPager);

    /* If the sub-journal was opened successfully (or was already open),
    ** write the journal record into the file.  */// 如果子日志成功打开（或者已经打开）写日志记录
    if( rc==SQLITE_OK ){
      void *pData = pPg->pData;
      i64 offset = (i64)pPager->nSubRec*(4+pPager->pageSize);
      char *pData2;
  
      CODEC2(pPager, pData, pPg->pgno, 7, return SQLITE_NOMEM, pData2);
      PAGERTRACE(("STMT-JOURNAL %d page %d\n", PAGERID(pPager), pPg->pgno));
      rc = write32bits(pPager->sjfd, offset, pPg->pgno);
      if( rc==SQLITE_OK ){
        rc = sqlite3OsWrite(pPager->sjfd, pData2, pPager->pageSize, offset+4);
      }
    }
  }
  if( rc==SQLITE_OK ){
    pPager->nSubRec++;
    assert( pPager->nSavepoint>0 );
    rc = addToSavepointBitvecs(pPager, pPg->pgno);
  }
  return rc;
}

/*
** This function is called by the pcache layer when it has reached some
** soft memory limit.
   这个函数被PCACHE层调用当它已经达到了一些软内存限制时。
** The first argument is a pointer to a Pager object
** (cast as a void*).  
    第一个参数是一个指向pager对象的指针（转换为一个void*）
** The pager is always 'purgeable' (not an in-memory
** database).
   pager总是“可清除的”（不是一个内存数据库）。
** The second argument is a reference to a page that is 
** currently dirty but has no outstanding references. 
   他第二个参数是一个引用页面,目前脏但没有杰出的引用。
** The page is always associated with the Pager object passed as the first  argument.
** pager对象相关的页面总是作为第一个参数传递

** The job of this function is to make pPg clean by writing its contents
** out to the database file, if possible. 
   如果可能的话，这个函数的功能是使pPg通过写它的内容清洁，输出到数据库文件。

** This may involve syncing the
** journal file. 
**这个可能包含同步日志文件。

** If successful, sqlite3PcacheMakeClean() is called on the page and
** SQLITE_OK returned. 
   如果成功的话，页面数会调用sqlite3PcacheMakeClean()并且返回SQLITE_OK。
** If an IO error occurs while trying to make the
** page clean, the IO error code is returned. 
   当试图清楚页面时发生IO错误，返回IO错误。
** If the page cannot be
** made clean for some other reason, but no error occurs, then SQLITE_OK
** is returned by sqlite3PcacheMakeClean() is not called.
   如果页面不能被其他原因清除而且没有错误发生， 那么返回SQLITE_OK，并且不调用sqlite3PcacheMakeClean() 

*/
static int pagerStress(void *p, PgHdr *pPg){
  Pager *pPager = (Pager *)p;
  int rc = SQLITE_OK;

  assert( pPg->pPager==pPager );
  assert( pPg->flags&PGHDR_DIRTY );

  /* The doNotSyncSpill flag is set during times when doing a sync of
  ** journal (and adding a new header) is not allowed.  This occurs
  ** during calls to sqlite3PagerWrite() while trying to journal multiple
  ** pages belonging to the same sector.
  **当日志同步（和添加一个新头）不被允许时doNotSyncSpill标志在这过程中被设置。
   当试图记录多重页面在同一个扇区时调用sqlite3PagerWrite()发生这个。
  ** The doNotSpill flag inhibits all cache spilling regardless of whether
  ** or not a sync is required.  This is set during a rollback.
  **doNotSpill标志抑制所有缓存溢出，无论是否需要同步。这是在一个回滚中设置的。

  ** Spilling is also prohibited when in an error state since that could
  ** lead to database corruption.
      当一个错误状态可能导致数据库损坏时溢出也是被禁止的。

  ** In the current implementaton it is impossible for sqlite3PcacheFetch()
    to be called with createFlag==1 while in the error state,
	hence it is impossible for this routine to be called in the error state.
	在错误状态，createFlag==1情况下调用 sqlite3PcacheFetch()时不可能实现的，
	因此这个程序不肯能在错误状态下被调用。
  ** Nevertheless, we include a NEVER()
    test for the error state as a safeguard against future changes.
	  不过，我们有一个NEVER（）测试的错误状态作为对未来变化的保障。
  */
  if( NEVER(pPager->errCode) ) return SQLITE_OK;
  if( pPager->doNotSpill ) return SQLITE_OK;
  if( pPager->doNotSyncSpill && (pPg->flags & PGHDR_NEED_SYNC)!=0 ){
    return SQLITE_OK;
  }

  pPg->pDirty = 0;
  if( pagerUseWal(pPager) ){
    /* Write a single frame for this page to the log. */// 在这页写一个单帧日志
    if( subjRequiresPage(pPg) ){ 
      rc = subjournalPage(pPg); 
    }
    if( rc==SQLITE_OK ){
      rc = pagerWalFrames(pPager, pPg, 0, 0);
    }
  }else{
  
    /* Sync the journal file if required. *///如果需要进行日志同步。
    if( pPg->flags&PGHDR_NEED_SYNC 
     || pPager->eState==PAGER_WRITER_CACHEMOD
    ){
      rc = syncJournal(pPager, 1);
    }
  
    /* If the page number of this page is larger than the current size of
    ** the database image, it may need to be written to the sub-journal.
    ** This is because the call to pager_write_pagelist() below will not
    ** actually write data to the file in this case.
    ** 如果这个页面的页码大于当前数据库镜像的大小,它可能需要写入子日志文件。
	   这是因为下面调用pager_write_pagelist()不会实际的将数据写入文件。
    ** Consider the following sequence of events: //考虑以下事件：
    **
    **   BEGIN;
    **     <journal page X>   日志页
    **     <modify page X>    修改页
    **     SAVEPOINT sp;      保存点
    **       <shrink database file to Y pages>    收缩数据库文件页面
    **       pagerStress(page X)  调用pagerStress（）
    **     ROLLBACK TO sp;
    **
    ** If (X>Y), then when pagerStress is called page X will not be written
    ** out to the database file, but will be dropped from the cache. 
	   如果X>Y，那么当pagerStress（）被调用时page X不会写入数据库文件，但将从缓存中删除。
	** Then, following the "ROLLBACK TO sp" statement, reading page X will read
	   data from the database file. 
	   然后，在声明“回滚sp”后，阅读page X将从数据库中读取数据文件。
	** This will be the copy of page X as it
    ** was when the transaction started, not as it was when "SAVEPOINT sp"
    ** was executed.
    **事务开始时page X将有副本，不是因为"SAVEPOINT sp"没有了.
    ** The solution is to write the current data for page X into the 
    ** sub-journal file now (if it is not already there), so that it will
    ** be restored to its current value when the "ROLLBACK TO sp" is 
    ** executed.
	   解决办法是现在写当前的page X数据到子日志文件中（如果还没有写进去的话），所以当“回滚sp”
	   生效时它能恢复它的当前值。
    */
    if( NEVER(
        rc==SQLITE_OK && pPg->pgno>pPager->dbSize && subjRequiresPage(pPg)
    ) ){
      rc = subjournalPage(pPg);
    }
  
    /* Write the contents of the page out to the database file. */// 将页面内容写入数据库
    if( rc==SQLITE_OK ){
      assert( (pPg->flags&PGHDR_NEED_SYNC)==0 );
      rc = pager_write_pagelist(pPager, pPg);
    }
  }

  /* Mark the page as clean. */ //标记页面为干净。
  if( rc==SQLITE_OK ){
    PAGERTRACE(("STRESS %d page %d\n", PAGERID(pPager), pPg->pgno));
    sqlite3PcacheMakeClean(pPg);
  }

  return pager_error(pPager, rc); 
}


/*
** Allocate and initialize a new Pager object and put a pointer to it
** in *ppPager. The pager should eventually be freed by passing it
** to sqlite3PagerClose().
** 分配和初始化一个新的pager对象并放一个指针到*ppPager。
   pager最终应通过它来释放sqlite3PagerClose()
   
** The zFilename argument is the path to the database file to open.
** If zFilename is NULL then a randomly-named temporary file is created
** and used as the file to be cached. Temporary files are be deleted
** automatically when they are closed. If zFilename is ":memory:" then 
** all information is held in cache. It is never written to disk. 
** This can be used to implement an in-memory database.
** zFilename参数是打开数据库文件的路径。
   如果zFilename为空，那么创建一个随意命名的临时文件并作为文件被缓存。
   当它们被关闭时临时文件将自动删除。如果zFilename是“：memory：”那么所有信息保存在缓存中。
    这是从来没有写入磁盘。这可以用来实现一个内存中的数据库。
** The nExtra parameter specifies the number of bytes of space allocated
** along with each page reference. This space is available to the user
** via the sqlite3PagerGetExtra() API.
** 所述的NExtra参数制定每个音容页面的空间分配的字节数。
   这个空间通过sqlite3PagerGetExtra() API对用户可用。
** The flags argument is used to specify properties that affect the
** operation of the pager. It should be passed some bitwise combination
** of the PAGER_* flags.
** 标志参数用于指定影响pager操作的属性。
   它应该通过一些PAGER_* 标志的位组合来传递。
** The vfsFlags parameter is a bitmask to pass to the flags parameter
** of the xOpen() method of the supplied VFS when opening files. 
** vfsFlags参数是个位掩码，当打开文件时传到提供VFS的xOpen(）方法的标志参数。

** If the pager object is allocated and the specified file opened 
** successfully, SQLITE_OK is returned and *ppPager set to point to
** the new pager object. If an error occurs, *ppPager is set to NULL
** and error code returned. This function may return SQLITE_NOMEM
** (sqlite3Malloc() is used to allocate memory), SQLITE_CANTOPEN or 
** various SQLITE_IO_XXX errors.
   如果pager对象分配和指定文件打开成功，返回SQLITE_OK并且设置*ppPager指向新的pager对象。
   如果出现错误,* ppPager设置为空,返回错误代码。
   这个函数可以返回SQLITE_NOMEM(sqlite3Malloc()用于分配内存),
   SQLITE_CANTOPEN或各种SQLITE_IO_XXX错误。
*/
int sqlite3PagerOpen(
  sqlite3_vfs *pVfs,       /* The virtual file system to use   虚拟文件系统使用*/
  Pager **ppPager,         /* OUT: Return the Pager structure here 返回pager的结构 */
  const char *zFilename,   /* Name of the database file to open  打开数据库文件的名称*/
  int nExtra,              /* Extra bytes append to each in-memory page  额外的字节附加到每个内存页面*/
  int flags,               /* flags controlling this file   标志*/
  int vfsFlags,            /* flags passed through to sqlite3_vfs.xOpen()  通过标志传递给sqlite3_vfs.xOpen（）*/
  void (*xReinit)(DbPage*) /* Function to reinitialize pages 功能重新初始化页面 */
){
  u8 *pPtr;
  Pager *pPager = 0;       /* Pager object to allocate and return */// pager 对象的分配和返回
  int rc = SQLITE_OK;      /* Return code */ //返回代码
  int tempFile = 0;        /* True for temp files (incl. in-memory files) 适用于临时文件（包括内存中的文件） */
  int memDb = 0;           /* True if this is an in-memory file */ // 如果这是内存中的文件
  int readOnly = 0;        /* True if this is a read-only file */ // 如果这是一个只读文件
  int journalFileSize;     /* Bytes to allocate for each journal fd *///字节分配给每个日志fd
  char *zPathname = 0;     /* Full path to database file */ //完整路径数据库文件
  int nPathname = 0;       /* Number of bytes in zPathname *///zPathname中的字节数
  int useJournal = (flags & PAGER_OMIT_JOURNAL)==0; /* False to omit journal */// 假 省略日志
  int pcacheSize = sqlite3PcacheSize();       /* Bytes to allocate for PCache *///字节分配给PCACHE
  u32 szPageDflt = SQLITE_DEFAULT_PAGE_SIZE;  /* Default page size */// 默认页面大小
  const char *zUri = 0;    /* URI args to copy *///URI参数复制 
  int nUri = 0;            /* Number of bytes of URI args at *zUri *///* zUri的URI参数的字节数

  /* Figure out how much space is required for each journal file-handle
  ** (there are two of them, the main journal and the sub-journal). 
     找出需要多少空间给每个日志句柄（有两个，主要日志和子日志）    
  ** This is the maximum space required for an in-memory journal file handle 
  ** and a regular journal file-handle. 
     这是一个内存日志文件和常规日志文件句柄所需要的最大空间。
  **Note that a "regular journal-handle"  may be a wrapper capable of caching the first portion of the journal
  ** file in memory to implement the atomic-write optimization (see 
  ** source file journal.c).
  需要注意的是，“常规日志句柄”可以是能够缓存内存中日志文件第一部分来实现优化atomic-write（见源文件journal.c）的一个包装
  */
  if( sqlite3JournalSize(pVfs)>sqlite3MemJournalSize() ){
    journalFileSize = ROUND8(sqlite3JournalSize(pVfs));
  }else{
    journalFileSize = ROUND8(sqlite3MemJournalSize());
  }

  /* Set the output variable to NULL in case an error occurs. */ //设置输出变量为NULL,以防发生错误
  *ppPager = 0;

#ifndef SQLITE_OMIT_MEMORYDB
  if( flags & PAGER_MEMORY ){
    memDb = 1;
    if( zFilename && zFilename[0] ){
      zPathname = sqlite3DbStrDup(0, zFilename);
      if( zPathname==0  ) return SQLITE_NOMEM;
      nPathname = sqlite3Strlen30(zPathname);
      zFilename = 0;
    }
  }
#endif

  /* Compute and store the full pathname in an allocated buffer pointed
  ** to by zPathname, length nPathname. Or, if this is a temporary file,
  ** leave both nPathname and zPathname set to 0.
  计算和存储分配缓冲区的完整路径名zPathname,nPathname长度。
  或者,如果这是一个临时文件,离开nPathname和zPathname设置为0。
  */
  if( zFilename && zFilename[0] ){
    const char *z;
    nPathname = pVfs->mxPathname+1;
    zPathname = sqlite3DbMallocRaw(0, nPathname*2);
    if( zPathname==0 ){
      return SQLITE_NOMEM;
    }
    zPathname[0] = 0; /* Make sure initialized even if FullPathname() fails *///确保初始化，即使FullPathname()失败。
    rc = sqlite3OsFullPathname(pVfs, zFilename, nPathname, zPathname);
    nPathname = sqlite3Strlen30(zPathname);
    z = zUri = &zFilename[sqlite3Strlen30(zFilename)+1];
    while( *z ){
      z += sqlite3Strlen30(z)+1;
      z += sqlite3Strlen30(z)+1;
    }
    nUri = (int)(&z[1] - zUri);
    assert( nUri>=0 );
    if( rc==SQLITE_OK && nPathname+8>pVfs->mxPathname ){
      /* This branch is taken when the journal path required by
      ** the database being opened will be more than pVfs->mxPathname
      ** bytes in length. This means the database cannot be opened,
      ** as it will not be possible to open the journal file or even
      ** check for a hot-journal before reading.
	     当所需数据库日志路径被打开将超过pVfs->mxPathname的长度字节时，这个分支被获取。
		 这意味着数据库不能打开，因为它不能在阅读之前打开日志文件甚至检查hot-journal。
      */
      rc = SQLITE_CANTOPEN_BKPT;
    }
    if( rc!=SQLITE_OK ){
      sqlite3DbFree(0, zPathname);
      return rc;
    }
  }

  /* Allocate memory for the Pager structure, PCache object, the
  ** three file descriptors, the database file name and the journal 
  ** file name. The layout in memory is as follows:
     为pager结构分配内存，PCache对象，三个文件描述符，数据库文件名和日志文件名。
	 在内存的分布如下：
  **
  **     Pager object         pager对象           (sizeof(Pager) bytes)
  **     PCache object        PCache对象           (sqlite3PcacheSize() bytes)
  **     Database file handle   数据库文件句柄         (pVfs->szOsFile bytes)
  **     Sub-journal file handle   子日志文件句柄      (journalFileSize bytes)
  **     Main journal file handle      主要日志未接句柄  (journalFileSize bytes)
  **     Database file name        数据库名      (nPathname+1 bytes)
  **     Journal file name         日志文件名     (nPathname+8+1 bytes)
  */
  pPtr = (u8 *)sqlite3MallocZero(
    ROUND8(sizeof(*pPager)) +      /* Pager structure */ //pager结构
    ROUND8(pcacheSize) +           /* PCache object */  //PCache对象
    ROUND8(pVfs->szOsFile) +       /* The main db file */ // 主要的数据库文件
    journalFileSize * 2 +          /* The two journal files */ //两个日志文件
    nPathname + 1 + nUri +         /* zFilename */
    nPathname + 8 + 2              /* zJournal */
#ifndef SQLITE_OMIT_WAL
    + nPathname + 4 + 2            /* zWal */
#endif
  );
  assert( EIGHT_BYTE_ALIGNMENT(SQLITE_INT_TO_PTR(journalFileSize)) );
  if( !pPtr ){
    sqlite3DbFree(0, zPathname);
    return SQLITE_NOMEM;
  }
  pPager =              (Pager*)(pPtr);
  pPager->pPCache =    (PCache*)(pPtr += ROUND8(sizeof(*pPager)));
  pPager->fd =   (sqlite3_file*)(pPtr += ROUND8(pcacheSize));
  pPager->sjfd = (sqlite3_file*)(pPtr += ROUND8(pVfs->szOsFile));
  pPager->jfd =  (sqlite3_file*)(pPtr += journalFileSize);
  pPager->zFilename =    (char*)(pPtr += journalFileSize);
  assert( EIGHT_BYTE_ALIGNMENT(pPager->jfd) );

  /* Fill in the Pager.zFilename and Pager.zJournal buffers, if required. */
  // 如果需要的话，填满Pager.zFilename和Pager.zJournal缓冲区
  if( zPathname ){
    assert( nPathname>0 );
    pPager->zJournal =   (char*)(pPtr += nPathname + 1 + nUri);
    memcpy(pPager->zFilename, zPathname, nPathname);
    if( nUri ) memcpy(&pPager->zFilename[nPathname+1], zUri, nUri);
    memcpy(pPager->zJournal, zPathname, nPathname);
    memcpy(&pPager->zJournal[nPathname], "-journal\000", 8+1);
    sqlite3FileSuffix3(pPager->zFilename, pPager->zJournal);
#ifndef SQLITE_OMIT_WAL
    pPager->zWal = &pPager->zJournal[nPathname+8+1];
    memcpy(pPager->zWal, zPathname, nPathname);
    memcpy(&pPager->zWal[nPathname], "-wal\000", 4+1);
    sqlite3FileSuffix3(pPager->zFilename, pPager->zWal);
#endif
    sqlite3DbFree(0, zPathname);
  }
  pPager->pVfs = pVfs;
  pPager->vfsFlags = vfsFlags;

  /* Open the pager file.   打开pager文件
  */
  if( zFilename && zFilename[0] ){
    int fout = 0;                    /* VFS flags returned by xOpen() */
	                                  // VFS标志通过xOpen()返回
    rc = sqlite3OsOpen(pVfs, pPager->zFilename, pPager->fd, vfsFlags, &fout);
    assert( !memDb );
    readOnly = (fout&SQLITE_OPEN_READONLY);

    /* If the file was successfully opened for read/write access,
    ** choose a default page size in case we have to create the
    ** database file. The default page size is the maximum of:
    ** 如果文件被成功打开进行读/写访问，选择默认页面大小的情况下，我们必须创建数据库文件。
	   默认页面大小最大为：
    **    + SQLITE_DEFAULT_PAGE_SIZE,    
    **    + The value returned by sqlite3OsSectorSize()    通过sqlite3OsSectorSize（）返回的值
    **    + The largest page size that can be written atomically.
	        最大页面大小可以写入的原子。
    */
    if( rc==SQLITE_OK && !readOnly ){
      setSectorSize(pPager);
      assert(SQLITE_DEFAULT_PAGE_SIZE<=SQLITE_MAX_DEFAULT_PAGE_SIZE);
      if( szPageDflt<pPager->sectorSize ){
        if( pPager->sectorSize>SQLITE_MAX_DEFAULT_PAGE_SIZE ){
          szPageDflt = SQLITE_MAX_DEFAULT_PAGE_SIZE;
        }else{
          szPageDflt = (u32)pPager->sectorSize;
        }
      }
#ifdef SQLITE_ENABLE_ATOMIC_WRITE
      {
        int iDc = sqlite3OsDeviceCharacteristics(pPager->fd);
        int ii;
        assert(SQLITE_IOCAP_ATOMIC512==(512>>8));
        assert(SQLITE_IOCAP_ATOMIC64K==(65536>>8));
        assert(SQLITE_MAX_DEFAULT_PAGE_SIZE<=65536);
        for(ii=szPageDflt; ii<=SQLITE_MAX_DEFAULT_PAGE_SIZE; ii=ii*2){
          if( iDc&(SQLITE_IOCAP_ATOMIC|(ii>>8)) ){
            szPageDflt = ii;
          }
        }
      }
#endif
    }
  }else{
    /* If a temporary file is requested, it is not opened immediately.
    ** In this case we accept the default page size and delay actually
    ** opening the file until the first call to OsWrite().
    ** 如果要求一个临时文件,它不是立即打开。
	   在这种情况下,我们接受默认页面大小和延迟打开文件,直到第一次调用OsWrite()。
    ** This branch is also run for an in-memory database. An in-memory
    ** database is the same as a temp-file that is never written out to
    ** disk and uses an in-memory rollback journal.
	   这个分支也运行内存数据库。
	   一个内存数据库和一个永远不会被写入磁盘并且使用内存回滚日志的临时文件是一样的。
    */ 
    tempFile = 1;
    pPager->eState = PAGER_READER;
    pPager->eLock = EXCLUSIVE_LOCK;
    readOnly = (vfsFlags&SQLITE_OPEN_READONLY);
  }

  /* The following call to PagerSetPagesize() serves to set the value of 
  ** Pager.pageSize and to allocate the Pager.pTmpSpace buffer.
     下面调用PagerSetPagesize（）用于设置Pager.pageSize的值，并分配所述Pager.pTmpSpace缓冲区。
  */
  if( rc==SQLITE_OK ){
    assert( pPager->memDb==0 );
    rc = sqlite3PagerSetPagesize(pPager, &szPageDflt, -1);
    testcase( rc!=SQLITE_OK );
  }

  /* If an error occurred in either of the blocks above, free the 
  ** Pager structure and close the file.
     如果在任一上述的块中出现错误，释放pager结构并关闭文件。
  */
  if( rc!=SQLITE_OK ){
    assert( !pPager->pTmpSpace );
    sqlite3OsClose(pPager->fd);
    sqlite3_free(pPager);
    return rc;
  }

  /* Initialize the PCache object. */ //初始化PCache对象
  assert( nExtra<1000 );
  nExtra = ROUND8(nExtra);
  sqlite3PcacheOpen(szPageDflt, nExtra, !memDb,
                    !memDb?pagerStress:0, (void *)pPager, pPager->pPCache);

  PAGERTRACE(("OPEN %d %s\n", FILEHANDLEID(pPager->fd), pPager->zFilename));
  IOTRACE(("OPEN %p %s\n", pPager, pPager->zFilename))

  pPager->useJournal = (u8)useJournal;
  /* pPager->stmtOpen = 0; */
  /* pPager->stmtInUse = 0; */
  /* pPager->nRef = 0; */
  /* pPager->stmtSize = 0; */
  /* pPager->stmtJSize = 0; */
  /* pPager->nPage = 0; */  //设置上述指针对象的值为0
  pPager->mxPgno = SQLITE_MAX_PAGE_COUNT;
  /* pPager->state = PAGER_UNLOCK; */  //将PAGER_UNLOCK的值赋给pPager->state
#if 0
  assert( pPager->state == (tempFile ? PAGER_EXCLUSIVE : PAGER_UNLOCK) );
#endif
  /* pPager->errMask = 0; */ 
  pPager->tempFile = (u8)tempFile;
  assert( tempFile==PAGER_LOCKINGMODE_NORMAL 
          || tempFile==PAGER_LOCKINGMODE_EXCLUSIVE );
  assert( PAGER_LOCKINGMODE_EXCLUSIVE==1 );
  pPager->exclusiveMode = (u8)tempFile; 
  pPager->changeCountDone = pPager->tempFile;
  pPager->memDb = (u8)memDb;
  pPager->readOnly = (u8)readOnly;
  assert( useJournal || pPager->tempFile );
  pPager->noSync = pPager->tempFile;
  if( pPager->noSync ){
    assert( pPager->fullSync==0 );
    assert( pPager->syncFlags==0 );
    assert( pPager->walSyncFlags==0 );
    assert( pPager->ckptSyncFlags==0 );
  }else{
    pPager->fullSync = 1;
    pPager->syncFlags = SQLITE_SYNC_NORMAL;
    pPager->walSyncFlags = SQLITE_SYNC_NORMAL | WAL_SYNC_TRANSACTIONS;
    pPager->ckptSyncFlags = SQLITE_SYNC_NORMAL;
  }
  /* pPager->pFirst = 0; */
  /* pPager->pFirstSynced = 0; */
  /* pPager->pLast = 0; */
  pPager->nExtra = (u16)nExtra;
  pPager->journalSizeLimit = SQLITE_DEFAULT_JOURNAL_SIZE_LIMIT;
  assert( isOpen(pPager->fd) || tempFile );
  setSectorSize(pPager);
  if( !useJournal ){
    pPager->journalMode = PAGER_JOURNALMODE_OFF;
  }else if( memDb ){
    pPager->journalMode = PAGER_JOURNALMODE_MEMORY;
  }
  /* pPager->xBusyHandler = 0; */
  /* pPager->pBusyHandlerArg = 0; */
  pPager->xReiniter = xReinit;
  /* memset(pPager->aHash, 0, sizeof(pPager->aHash)); */

  *ppPager = pPager;
  return SQLITE_OK;
}



/*
** This function is called after transitioning from PAGER_UNLOCK to
** PAGER_SHARED state. It tests if there is a hot journal present in
** the file-system for the given pager. A hot journal is one that 
** needs to be played back. According to this function, a hot-journal
** file exists if the following criteria are met:
** 从PAGER_UNLOCK PAGER_SHARED状态转换之后，此函数被调用。
   如果有一个hot journal存在于文件系统对于给定的pager，则测试。
   hot journal需要回放。根据hot-journal文件存在此功能，如果满足一下条件：
**   * The journal file exists in the file system, and //文件系统内存在日志文件，
**   * No process holds a RESERVED or greater lock on the database file, and
       没有进程持有保留锁或更大的锁在数据库文件上
**   * The database file itself is greater than 0 bytes in size, and
       数据库文件本身大小大于0字节
**   * The first byte of the journal file exists and is not 0x00.
**     日志文件的第一字节不是以0x00存在的。

** If the current size of the database file is 0 but a journal file
** exists, that is probably an old journal left over from a prior
** database with the same name. In this case the journal file is
** just deleted using OsDelete, *pExists is set to 0 and SQLITE_OK
** is returned.
** 如果数据库文件的当前大小为0，但一个日志文件存在，这很可能从一个同样名字的
   预先数据库上留下一个旧日志。在这种情况下,日志文件只是删除使用OsDelete ，
   * pExists设置为0并且返回SQLITE_OK。
** This routine does not check if there is a master journal filename
** at the end of the file. If there is, and that master journal file
** does not exist, then the journal file is not really hot. In this
** case this routine will return a false-positive. The pager_playback()
** routine will discover that the journal file is not really hot and 
** will not roll it back. 
** 这个程序不检查是否有一个主日志文件名在文件的末尾。
   如果有，而且主日志文件不存在，那么日志文件是不是真的热门。
   在这种情况下，该程序将返回一个假正类。
   该pager_playback（）例程将发现该日志文件是不是真的热，不会回滚。
** If a hot-journal file is found to exist, *pExists is set to 1 and 
** SQLITE_OK returned. If no hot-journal file is present, *pExists is
** set to 0 and SQLITE_OK returned. If an IO error occurs while trying
** to determine whether or not a hot-journal file exists, the IO error
** code is returned and the value of *pExists is undefined.
   如果hot-journal文件被发现存在，* pExists被设置为1并且返回SQLITE_OK。
*/
static int hasHotJournal(Pager *pPager, int *pExists){
  sqlite3_vfs * const pVfs = pPager->pVfs;
  int rc = SQLITE_OK;           /* Return code */ //返回代码
  int exists = 1;               /* True if a journal file is present */ //如果日志文件存在 为真。
  int jrnlOpen = !!isOpen(pPager->jfd);

  assert( pPager->useJournal );
  assert( isOpen(pPager->fd) );
  assert( pPager->eState==PAGER_OPEN );

  assert( jrnlOpen==0 || ( sqlite3OsDeviceCharacteristics(pPager->jfd) &
    SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN
  ));

  *pExists = 0;
  if( !jrnlOpen ){
    rc = sqlite3OsAccess(pVfs, pPager->zJournal, SQLITE_ACCESS_EXISTS, &exists);
  }
  if( rc==SQLITE_OK && exists ){
    int locked = 0;             /* True if some process holds a RESERVED lock */ //如果有一些进程持有保留锁，为真。

    /* Race condition here:  Another process might have been holding the
    ** the RESERVED lock and have a journal open at the sqlite3OsAccess() 
    ** call above, but then delete the journal and drop the lock before
    ** we get to the following sqlite3OsCheckReservedLock() call.  If that
    ** is the case, this routine might think there is a hot journal when
    ** in fact there is none.  This results in a false-positive which will
    ** be dealt with by the playback routine.  Ticket #3883.
	   这里的竞态条件： 另一个进程可能已持有保留的锁，并有日志在sqlite3OsAccess（）上打开，
	   但随后我们得到以下sqlite3OsCheckReservedLock() 调用前删除日志并释放锁。
	   如果这样的话，这个程序可能会认为有一个hot journal但实际上没有。 
	   这个导致了被回滚程序处理的假正类。
    */
    rc = sqlite3OsCheckReservedLock(pPager->fd, &locked);
    if( rc==SQLITE_OK && !locked ){
      Pgno nPage;                 /* Number of pages in database file */

      /* Check the size of the database file. If it consists of 0 pages,
      ** then delete the journal file. See the header comment above for 
      ** the reasoning here.  Delete the obsolete journal file under
      ** a RESERVED lock to avoid race conditions and to avoid violating
      ** [H33020].
	     检查数据库文件的大小。 如果它是0个页面，那么删掉日志文件。
		 见上文这里的头注释推理。删除保留锁下过期的日志文件，以避免竞态条件和违反[H33020]。
      */
      rc = pagerPagecount(pPager, &nPage);
      if( rc==SQLITE_OK ){
        if( nPage==0 ){
          sqlite3BeginBenignMalloc();
          if( pagerLockDb(pPager, RESERVED_LOCK)==SQLITE_OK ){
            sqlite3OsDelete(pVfs, pPager->zJournal, 0);
            if( !pPager->exclusiveMode ) pagerUnlockDb(pPager, SHARED_LOCK);
          }
          sqlite3EndBenignMalloc();
        }else{
          /* The journal file exists and no other connection has a reserved
          ** or greater lock on the database file. Now check that there is
          ** at least one non-zero bytes at the start of the journal file.
          ** If there is, then we consider this journal to be hot. If not, 
          ** it can be ignored.
		     日志文件存在，并且没有其他连接对数据库文件保留锁或更大的锁。
			 现在检查是否有在日志文件的开始的至少一个非零字节。
			 如果有，那么我们认为这个日志时热的。
			 如果没有，它可以被忽略。
          */
          if( !jrnlOpen ){
            int f = SQLITE_OPEN_READONLY|SQLITE_OPEN_MAIN_JOURNAL;
            rc = sqlite3OsOpen(pVfs, pPager->zJournal, pPager->jfd, f, &f);
          }
          if( rc==SQLITE_OK ){
            u8 first = 0;
            rc = sqlite3OsRead(pPager->jfd, (void *)&first, 1, 0);
            if( rc==SQLITE_IOERR_SHORT_READ ){
              rc = SQLITE_OK;
            }
            if( !jrnlOpen ){
              sqlite3OsClose(pPager->jfd);
            }
            *pExists = (first!=0);
          }else if( rc==SQLITE_CANTOPEN ){
            /* If we cannot open the rollback journal file in order to see if
            ** its has a zero header, that might be due to an I/O error, or
            ** it might be due to the race condition described above and in
            ** ticket #3883.  Either way, assume that the journal is hot.
            ** This might be a false positive.  But if it is, then the
            ** automatic journal playback and recovery mechanism will deal
            ** with it under an EXCLUSIVE lock where we do not need to
            ** worry so much with race conditions.
			   如果我们不能为了看它有一个0头打开一个回滚日志，这可能是一个I/O错误，
			   或者可能因为描述上面的竞态条件。 无论哪种方式，认为日志时热的。
			   这可能是一个假正类。但如果它是，那么自动日志回放和恢复机制将在排它锁下处理，
			   不需要我们担心竞态条件.
            */
            *pExists = 1;
            rc = SQLITE_OK;
          }
        }
      }
    }
  }

  return rc;
}

/*
** This function is called to obtain a shared lock on the database file.
** It is illegal to call sqlite3PagerAcquire() until after this function
** has been successfully called. If a shared-lock is already held when
** this function is called, it is a no-op.
** 调用这个函数来获得数据库文件的共享锁。 
   它是非法的调用sqlite3PagerAcquire（）除非这个功能已经被成功调用。
   如果调用这个函数时，共享锁已被持有，则它是一个空操作。
** The following operations are also performed by this function.
** 也通过该函数执行以下的操作。
**   1) If the pager is currently in PAGER_OPEN state (no lock held
**      on the database file), then an attempt is made to obtain a
**      SHARED lock on the database file. Immediately after obtaining
**      the SHARED lock, the file-system is checked for a hot-journal,
**      which is played back if present. Following any hot-journal 
**      rollback, the contents of the cache are validated by checking
**      the 'change-counter' field of the database file header and
**      discarded if they are found to be invalid.
**      如果该pager目前处于PAGER_OPEN状态（没有数据库文件锁被持有），
        然后试图获得对数据库文件共享锁。
		在获得共享锁后，如果该回放存在，文件系统hot-journal被检查。
		下列任何hot-journal回滚，高速缓存的内容通过检查数据库文件报头的'改变计数器“字段验证，
		并丢弃，如果它们被发现是无效的。
**   2) If the pager is running in exclusive-mode, and there are currently
**      no outstanding references to any pages, and is in the error state,
**      then an attempt is made to clear the error state by discarding
**      the contents of the page cache and rolling back any open journal
**      file.
**     如果pager在独占模式中运行,目前没有杰出的任何页面的引用并处于错误状态,
       然后试图清除错误状态丢弃页面缓存的内容和回滚任何打开的日志文件。
** If everything is successful, SQLITE_OK is returned. If an IO error 
** occurs while locking the database, checking for a hot-journal file or 
** rolling back a journal file, the IO error code is returned.
   如果一切顺利，SQLITE_OK返回。如果在锁定数据库，
   检查hot-journal文件或回滚一个日志文件时发生IO错误，将返回IO错误代码。
*/
int sqlite3PagerSharedLock(Pager *pPager){
  int rc = SQLITE_OK;                /* Return code */

  /* This routine is only called from b-tree and only when there are no
  ** outstanding pages. This implies that the pager state should either
  ** be OPEN or READER. READER is only possible if the pager is or was in 
  ** exclusive access mode.
     当没有显著的页面时这个程序仅从b-tree调用。这意味着pager状态应为打开的或
     读状态。读状态仅在pager在或已在独占访问模式下可能。
  */
  assert( sqlite3PcacheRefCount(pPager->pPCache)==0 );
  assert( assert_pager_state(pPager) );
  assert( pPager->eState==PAGER_OPEN || pPager->eState==PAGER_READER );
  if( NEVER(MEMDB && pPager->errCode) ){ return pPager->errCode; }

  if( !pagerUseWal(pPager) && pPager->eState==PAGER_OPEN ){
    int bHotJournal = 1;          /* True if there exists a hot journal-file */
	                              //如果存在一个hot journal文件则为真
    assert( !MEMDB );

    rc = pager_wait_on_lock(pPager, SHARED_LOCK);
    if( rc!=SQLITE_OK ){
      assert( pPager->eLock==NO_LOCK || pPager->eLock==UNKNOWN_LOCK );
      goto failed;
    }

    /* If a journal file exists, and there is no RESERVED lock on the
    ** database file, then it either needs to be played back or deleted.
	   如果一个日志文件存在，在数据库文件没有保留锁，那么它需要被回放或者被
       删除。
    */
    if( pPager->eLock<=SHARED_LOCK ){
      rc = hasHotJournal(pPager, &bHotJournal);
    }
    if( rc!=SQLITE_OK ){
      goto failed;
    }
    if( bHotJournal ){
      /* Get an EXCLUSIVE lock on the database file. At this point it is
      ** important that a RESERVED lock is not obtained on the way to the
      ** EXCLUSIVE lock. If it were, another process might open the
      ** database file, detect the RESERVED lock, and conclude that the
      ** database is safe to read while this process is still rolling the 
      ** hot-journal back.
      ** 在数据库文件中获得一个排它锁。此时在排它锁的过程中没有获得保留锁是很重要的。
         如果是这样，另一个进程可能打开数据库文件，检测保留锁，并推断当这个
         进程仍在回滚hot-journal时数据库读是安全的。
      ** Because the intermediate RESERVED lock is not requested, any
      ** other process attempting to access the database file will get to 
      ** this point in the code and fail to obtain its own EXCLUSIVE lock 
      ** on the database file.
      ** 因为中间保留锁没有请求，任何其他进程试图访问该数据库文件将
	     在代码上得到这一点并且无法获得对数据库文件自身的排它锁。
      ** Unless the pager is in locking_mode=exclusive mode, the lock is
      ** downgraded to SHARED_LOCK before this function returns.
	     除非pager在locking_mode=exclusive模式，该锁在这个函数返回之前
         降级到共享锁
      */
      rc = pagerLockDb(pPager, EXCLUSIVE_LOCK);
      if( rc!=SQLITE_OK ){
        goto failed;
      }
 
      /* If it is not already open and the file exists on disk, open the 
      ** journal for read/write access. Write access is required because 
      ** in exclusive-access mode the file descriptor will be kept open 
      ** and possibly used for a transaction later on. Also, write-access 
      ** is usually required to finalize the journal in journal_mode=persist 
      ** mode (and also for journal_mode=truncate on some systems).
      ** 如果它已经打开并且文件在磁盘上存在，打开读/写访问日志。请求写访问，因为
         在排它访问模式下，文件描述符将保持打开，并且可能随后用于一个事物。而且，
         写访问经常被请求来在journal_mode=persist模式下完成日志（且对于在一些系统上
         日志mode=truncate也是这样）
      ** If the journal does not exist, it usually means that some 
      ** other connection managed to get in and roll it back before 
      ** this connection obtained the exclusive lock above. Or, it 
      ** may mean that the pager was in the error-state when this
      ** function was called and the journal file does not exist.
	     如果日志不存在，它通常意味着一些其他连接设法和转回来之前就此获得上述的排它锁。
		 或者，它可能意味着当这个函数被调用并且这个日志文件不存在的时候，
		 pager处在错误状态。
      */
      if( !isOpen(pPager->jfd) ){
        sqlite3_vfs * const pVfs = pPager->pVfs;
        int bExists;              /* True if journal file exists */
        rc = sqlite3OsAccess(
            pVfs, pPager->zJournal, SQLITE_ACCESS_EXISTS, &bExists);
        if( rc==SQLITE_OK && bExists ){
          int fout = 0;
          int f = SQLITE_OPEN_READWRITE|SQLITE_OPEN_MAIN_JOURNAL;
          assert( !pPager->tempFile );
          rc = sqlite3OsOpen(pVfs, pPager->zJournal, pPager->jfd, f, &fout);
          assert( rc!=SQLITE_OK || isOpen(pPager->jfd) );
          if( rc==SQLITE_OK && fout&SQLITE_OPEN_READONLY ){
            rc = SQLITE_CANTOPEN_BKPT;
            sqlite3OsClose(pPager->jfd);
          }
        }
      }
 
      /* Playback and delete the journal.  Drop the database write
      ** lock and reacquire the read lock. Purge the cache before
      ** playing back the hot-journal so that we don't end up with
      ** an inconsistent cache.  Sync the hot journal before playing
      ** it back since the process that crashed and left the hot journal
      ** probably did not sync it and we are required to always sync
      ** the journal before playing it back.
	     回放和删除日志。删除数据库的写锁并重新获取读取锁。在回放hot-journal前清除缓存
         以便于我们不以一个不一致的缓存结束。在回滚前同步hot-journal，因为这个碰撞并留下
         hot-journal的程序可能不与它同步，我们需要去总是在回滚之前同步日志。
      */
      if( isOpen(pPager->jfd) ){
        assert( rc==SQLITE_OK );
        rc = pagerSyncHotJournal(pPager);
        if( rc==SQLITE_OK ){
          rc = pager_playback(pPager, 1);
          pPager->eState = PAGER_OPEN;
        }
      }else if( !pPager->exclusiveMode ){
        pagerUnlockDb(pPager, SHARED_LOCK);
      }

      if( rc!=SQLITE_OK ){
        /* This branch is taken if an error occurs while trying to open
        ** or roll back a hot-journal while holding an EXCLUSIVE lock. The
        ** pager_unlock() routine will be called before returning to unlock
        ** the file. If the unlock attempt fails, then Pager.eLock must be
        ** set to UNKNOWN_LOCK (see the comment above the #define for 
        ** UNKNOWN_LOCK above for an explanation). 
        ** 当持有一个排它锁并且试图打开或者回滚一个的hot-journal时发生错误，那么这个
           分支被获得。 pager_unlock()程序将在返回未加锁的文件前被调用。如果解锁
           失败，那么Pager.eLock必须被设置为UNKNOWN_LOCK
		   （见上面评论的# define UNKNOWN_LOCK上的一个解释）
        ** In order to get pager_unlock() to do this, set Pager.eState to
        ** PAGER_ERROR now. This is not actually counted as a transition
        ** to ERROR state in the state diagram at the top of this file,
        ** since we know that the same call to pager_unlock() will very
        ** shortly transition the pager object to the OPEN state. Calling
        ** assert_pager_state() would fail now, as it should not be possible
        ** to be in ERROR state when there are zero outstanding page 
        ** references.
		   为了获得pager_unlock()而做这个，现在设置Pager.eState为PAGER_ERROR。
           实际上这不是算作这个文件顶部状态图中一个错误状态的转换，因为我们知道
           同样的pager_unlock()调用将立即转换pager对象到打开状态。现在调用
           assert_pager_state()将失败，因为当有0个显著页面引用。它将不可能在错误状态。
        */
        pager_error(pPager, rc);
        goto failed;
      }

      assert( pPager->eState==PAGER_OPEN );
      assert( (pPager->eLock==SHARED_LOCK)
           || (pPager->exclusiveMode && pPager->eLock>SHARED_LOCK)
      );
    }

    if( !pPager->tempFile 
     && (pPager->pBackup || sqlite3PcachePagecount(pPager->pPCache)>0) 
    ){
      /* The shared-lock has just been acquired on the database file
      ** and there are already pages in the cache (from a previous
      ** read or write transaction).  Check to see if the database
      ** has been modified.  If the database has changed, flush the
      ** cache.
      ** 数据库文件的共享锁刚被获得，并且已经有页面缓存（从先前的读或写事务）
         检查看数据库是否被修改。如果数据库已改变，清除缓存。
      ** Database changes is detected by looking at 15 bytes beginning
      ** at offset 24 into the file.  The first 4 of these 16 bytes are
      ** a 32-bit counter that is incremented with each change.  The
      ** other bytes change randomly with each file change when
      ** a codec is in use.
      ** 数据库的改变通过观察检测15个字节处开始偏移24到文件中。
	     这16个字节的前4个是一个32位计数器，他们在变化的时候是递增。
	     其他字节与每个文件变更随机变化时的编解码器在使用中。
      ** There is a vanishingly small chance that a change will not be 
      ** detected.  The chance of an undetected change is so small that
      ** it can be neglected.
	     有一个很小很小的机率，一个改变将不会被发觉。一个未发觉变化的机率是如此小，
         以至于它能被忽略。
      */
      Pgno nPage = 0;
      char dbFileVers[sizeof(pPager->dbFileVers)];

      rc = pagerPagecount(pPager, &nPage);
      if( rc ) goto failed;

      if( nPage>0 ){
        IOTRACE(("CKVERS %p %d\n", pPager, sizeof(dbFileVers)));
        rc = sqlite3OsRead(pPager->fd, &dbFileVers, sizeof(dbFileVers), 24);
        if( rc!=SQLITE_OK ){
          goto failed;
        }
      }else{
        memset(dbFileVers, 0, sizeof(dbFileVers));
      }

      if( memcmp(pPager->dbFileVers, dbFileVers, sizeof(dbFileVers))!=0 ){
        pager_reset(pPager);
      }
    }

    /* If there is a WAL file in the file-system, open this database in WAL
    ** mode. Otherwise, the following function call is a no-op.
	   如果在文件系统中有一个WAL文件，以WAL模式打开这个数据库。否则，下面的函数
       调用是一个空操作。
    */
    rc = pagerOpenWalIfPresent(pPager);
#ifndef SQLITE_OMIT_WAL
    assert( pPager->pWal==0 || rc==SQLITE_OK );
#endif
  }

  if( pagerUseWal(pPager) ){
    assert( rc==SQLITE_OK );
    rc = pagerBeginReadTransaction(pPager);
  }

  if( pPager->eState==PAGER_OPEN && rc==SQLITE_OK ){
    rc = pagerPagecount(pPager, &pPager->dbSize);
  }

 failed:
  if( rc!=SQLITE_OK ){
    assert( !MEMDB );
    pager_unlock(pPager);
    assert( pPager->eState==PAGER_OPEN );
  }else{
    pPager->eState = PAGER_READER;
  }
  return rc;
}

/*
** If the reference count has reached zero, rollback any active
** transaction and unlock the pager.
** 如果引用计数达到零，回滚任何活动事务和解锁pager。
** Except, in locking_mode=EXCLUSIVE when there is nothing to in
** the rollback journal, the unlock is not performed and there is
** nothing to rollback, so this routine is a no-op.
   除了在locking_mode= EXCLUSIVE时，当没有什么可回滚日志，解锁不执行并且
   没什么回滚，所以这个程序是一个空操作。
*/ 
static void pagerUnlockIfUnused(Pager *pPager){
  if( (sqlite3PcacheRefCount(pPager->pPCache)==0) ){
    pagerUnlockAndRollback(pPager);
  }
}

/*
** Acquire a reference to page number pgno in pager pPager (a page
** reference has type DbPage*). If the requested reference is 
** successfully obtained, it is copied to *ppPage and SQLITE_OK returned.
** 获取pager pPager的引用页码pgno（一个页面引用拥有DbPage*类型）。如果这个
   请求引用成功被获得，它被复制到*ppPage并且返回SQLITE_OK。
** If the requested page is already in the cache, it is returned. 
** Otherwise, a new page object is allocated and populated with data
** read from the database file. In some cases, the pcache module may
** choose not to allocate a new page object and may reuse an existing
** object with no outstanding references.
** 如果请求页面已经在缓存中，它被返回。
   否则，一个新的页面对象被分配，并且用从数据库文件读取的数据填充。
   在某些情况下，pcache模块可能选择不分配一个新的页面对象并且可能复用一个存在的
   没有显著引用的对象
** The extra data appended to a page is always initialized to zeros the 
** first time a page is loaded into memory. If the page requested is 
** already in the cache when this function is called, then the extra
** data is left as it was when the page object was last used.
** 额外的数据追加到一个页面总是在第一次该页面被加载进内存时被初始化为0。
   当这个函数被调用的时候，如果这个页面请求已经在缓存中，那么当该页面对象被最后
   使用的时候，额外的数据保持原样。
** If the database image is smaller than the requested page or if a 
** non-zero value is passed as the noContent parameter and the 
** requested page is not already stored in the cache, then no 
** actual disk read occurs. In this case the memory image of the 
** page is initialized to all zeros. 
** 如果数据库图像小于请求的页面或者如果非零值作为noContent参数传递并且
   请求的页面不是已经存储在缓存中，那么没有实际磁盘读发生。在这种情况下,
   页面内存映像都被初始化为0。

** If noContent is true, it means that we do not care about the contents
** of the page. This occurs in two seperate scenarios:
**  如果noContent为真，这意味着我们不关心页面内容。这发生在两个独立的场景：
**   a) When reading a free-list leaf page from the database, and
**      当从数据库中读取一个空闲列表的叶子页面，并且
**   b) When a savepoint is being rolled back and we need to load
**      a new page into the cache to be filled with the data read
**      from the savepoint journal.
**      当一个保存点回滚并且我们需要去加载一个新的页面到从保存点日志读取的数据填充的
        缓存中。
** If noContent is true, then the data returned is zeroed instead of
** being read from the database. Additionally, the bits corresponding
** to pgno in Pager.pInJournal (bitvec of pages already written to the
** journal file) and the PagerSavepoint.pInSavepoint bitvecs of any open
** savepoints are set. This means if the page is made writable at any
** point in the future, using a call to sqlite3PagerWrite(), its contents
** will not be journaled. This saves IO.
** 如果noContent为真，那么返回的数据为0而不是从数据库读取。此外，在
   Pager.pInJournal的pgno对应位（页bitvec已经写入日志文件）并且
   任何保存点的PagerSavepoint.pInSavepoint bitvecs被设置。这意味着如果在将来
   任何时候这个页面设为可写，使用sqlite3PagerWrite()调用，它的内容将不被日志记录。
   这节省了IO。
** The acquisition might fail for several reasons.  In all cases,
** an appropriate error code is returned and *ppPage is set to NULL.
** 获得这个可能会因为几个原因失败。任何情况下，一个相应的错误代码被返回并且
   *ppPage被设置为NULL。
** See also sqlite3PagerLookup().  Both this routine and Lookup() attempt
** to find a page in the in-memory cache first.  If the page is not already
** in memory, this routine goes to disk to read it in whereas Lookup()
** just returns 0.  This routine acquires a read-lock the first time it
** has to go to disk, and could also playback an old journal if necessary.
** Since Lookup() never goes to disk, it never has to deal with locks
** or journal files.
   看sqlite3PagerLookup()。这个程序和Lookup()都试图先寻找一个在内存缓存的页面。
   如果这个页面不是已经在内存中，这个程序进入磁盘来读它，而Lookup()仅返回0.
   这个程序在第一次进入磁盘时获得一个读锁，并且如果需要也能回滚一个旧日志。
   因为Lookup()从来不进入磁盘，它从来不必要处理锁或者日志文件。
*/
int sqlite3PagerAcquire(
  Pager *pPager,      /* The pager open on the database file */ //在数据库文件上pager打开
  Pgno pgno,          /* Page number to fetch */ //取页码
  DbPage **ppPage,    /* Write a pointer to the page here */
                      //写一个指向这个页面的指针
  int noContent       /* Do not bother reading content from disk if true */
                      //如果为真，不要打扰从磁盘读内容
){
  int rc;
  PgHdr *pPg;

  assert( pPager->eState>=PAGER_READER );
  assert( assert_pager_state(pPager) );

  if( pgno==0 ){
    return SQLITE_CORRUPT_BKPT;
  }

  /* If the pager is in the error state, return an error immediately. 
  ** Otherwise, request the page from the PCache layer. */
     //如果pager在错误状态，立即返回一个错误。否则，从PCache层请求一个页面。
  if( pPager->errCode!=SQLITE_OK ){
    rc = pPager->errCode;
  }else{
    rc = sqlite3PcacheFetch(pPager->pPCache, pgno, 1, ppPage);
  }

  if( rc!=SQLITE_OK ){
    /* Either the call to sqlite3PcacheFetch() returned an error or the
    ** pager was already in the error-state when this function was called.
    ** Set pPg to 0 and jump to the exception handler.  */
	   //当这个函数被调用，要么sqlite3PcacheFetch()调用返回一个错误或者pager
       //已经在错误状态.设置pPg为0并且跳转到异常处理程序。
    pPg = 0;
    goto pager_acquire_err;
  }
  assert( (*ppPage)->pgno==pgno );
  assert( (*ppPage)->pPager==pPager || (*ppPage)->pPager==0 );

  if( (*ppPage)->pPager && !noContent ){
    /* In this case the pcache already contains an initialized copy of
    ** the page. Return without further ado.  */
	  //在这种情况下,pcache已经包含一个初始化的副本页面。立即返回。
    assert( pgno<=PAGER_MAX_PGNO && pgno!=PAGER_MJ_PGNO(pPager) );
    pPager->aStat[PAGER_STAT_HIT]++;
    return SQLITE_OK;

  }else{
    /* The pager cache has created a new page. Its content needs to 
    ** be initialized.  pager 缓存中创建了一个新页面，它的内容需要被初始化*/

    pPg = *ppPage;
    pPg->pPager = pPager;

    /* The maximum page number is 2^31. Return SQLITE_CORRUPT if a page
    ** number greater than this, or the unused locking-page, is requested. */
	//最大页码是2^31。返回SQLITE_CORRUPT，如果一个页码比这个大，或者未使用的
	 //锁定页面，被请求。
    if( pgno>PAGER_MAX_PGNO || pgno==PAGER_MJ_PGNO(pPager) ){
      rc = SQLITE_CORRUPT_BKPT;
      goto pager_acquire_err;
    }

    if( MEMDB || pPager->dbSize<pgno || noContent || !isOpen(pPager->fd) ){
      if( pgno>pPager->mxPgno ){
        rc = SQLITE_FULL;
        goto pager_acquire_err;
      }
      if( noContent ){
        /* Failure to set the bits in the InJournal bit-vectors is benign.
        ** It merely means that we might do some extra work to journal a 
        ** page that does not need to be journaled.  Nevertheless, be sure 
        ** to test the case where a malloc error occurs while trying to set 
        ** a bit in a bit vector.
		   未能在InJournal中设置位向量是良性的。
		   这仅仅意味着我们可能会做一些额外并不用记录的工作日志。然而，一定要测试这个
           用例当试图在一个位向量里设置一个位时在哪里发生一个分配错误。
        */
        sqlite3BeginBenignMalloc();
        if( pgno<=pPager->dbOrigSize ){
          TESTONLY( rc = ) sqlite3BitvecSet(pPager->pInJournal, pgno);
          testcase( rc==SQLITE_NOMEM );
        }
        TESTONLY( rc = ) addToSavepointBitvecs(pPager, pgno);
        testcase( rc==SQLITE_NOMEM );
        sqlite3EndBenignMalloc();
      }
      memset(pPg->pData, 0, pPager->pageSize);
      IOTRACE(("ZERO %p %d\n", pPager, pgno));
    }else{
      assert( pPg->pPager==pPager );
      pPager->aStat[PAGER_STAT_MISS]++;
      rc = readDbPage(pPg);
      if( rc!=SQLITE_OK ){
        goto pager_acquire_err;
      }
    }
    pager_set_pagehash(pPg);
  }

  return SQLITE_OK;

pager_acquire_err:
  assert( rc!=SQLITE_OK );
  if( pPg ){
    sqlite3PcacheDrop(pPg);
  }
  pagerUnlockIfUnused(pPager);

  *ppPage = 0;
  return rc;
}

/*
** Acquire a page if it is already in the in-memory cache.  Do
** not read the page from disk.  Return a pointer to the page,
** or 0 if the page is not in cache. 
** 获取一个页面如果它已经在内存缓存中。不要从磁盘读取这个页面。如果这个页面不在
   缓存中，返回一个页面指针或者0。
** See also sqlite3PagerGet().  The difference between this routine
** and sqlite3PagerGet() is that _get() will go to the disk and read
** in the page if the page is not already in cache.  This routine
** returns NULL if the page is not in cache or if a disk I/O error 
** has ever happened.
  参见sqlite3PagerGet()。这个程序和sqlite3PagerGet()之间的不同是如果这个页面不是已经在缓存中
  则_get()将到磁盘上并在页面中读取。如果这个页面不在缓存中或者如果一个磁盘I/O错误已经发生，这个程序返回NULL。
*/
DbPage *sqlite3PagerLookup(Pager *pPager, Pgno pgno){
  PgHdr *pPg = 0;
  assert( pPager!=0 );
  assert( pgno!=0 );
  assert( pPager->pPCache!=0 );
  assert( pPager->eState>=PAGER_READER && pPager->eState!=PAGER_ERROR );
  sqlite3PcacheFetch(pPager->pPCache, pgno, 0, &pPg);
  return pPg;
}

/*
** Release a page reference.
** 释放一个页面引用
** If the number of references to the page drop to zero, then the
** page is added to the LRU list.  When all references to all pages
** are released, a rollback occurs and the lock on the database is
** removed.
   如果引用页面的数量下降到零，那么该页面添加到LRU列表。当所有的页面引用被释放，
   发生一个回滚并且撤销数据库上的锁。
*/
void sqlite3PagerUnref(DbPage *pPg){
  if( pPg ){
    Pager *pPager = pPg->pPager;
    sqlite3PcacheRelease(pPg);
    pagerUnlockIfUnused(pPager);
  }
}

/*
** This function is called at the start of every write transaction.
** There must already be a RESERVED or EXCLUSIVE lock on the database 
** file when this routine is called.
** 这个函数在每一个写事务的开始被调用。当这个程序被调用，必须在数据库文件上有
   保留或排它锁。
** Open the journal file for pager pPager and write a journal header
** to the start of it. If there are active savepoints, open the sub-journal
** as well. This function is only used when the journal file is being 
** opened to write a rollback log for a transaction. It is not used 
** when opening a hot journal file to roll it back.
** 打开pager pPager日志文件并且在它的开头写一个日志标题。如果有活跃的保存点，也打
   开子日志。这个函数仅当日志文件被打开来写一个事务的回滚日志时使用。当打开一个
    hot journal文件来回滚该事务时它不被使用。
** If the journal file is already open (as it may be in exclusive mode),
** then this function just writes a journal header to the start of the
** already open file. 
** 如果这个日志文件已经打开（因为它可能在独占模式），那么这个函数仅写一个日志标题
   到已经打开文件的开头
** Whether or not the journal file is opened by this function, the
** Pager.pInJournal bitvec structure is allocated.
** 不论该日志文件被这个函数打开与否，分配Pager.pInJournal bitvec结构体。
** Return SQLITE_OK if everything is successful. Otherwise, return 
** SQLITE_NOMEM if the attempt to allocate Pager.pInJournal fails, or 
** an IO error code if opening or writing the journal file fails.
   如果一切都是成功的返回SQLITE_OK。另外，如果试图分配Pager.pInJournal失败则返回
   SQLITE_NOMEM，或者如果打开或写入日志文件失败则返回一个IO错误代码。
*/
static int pager_open_journal(Pager *pPager){
  int rc = SQLITE_OK;                        /* Return code */ //返回代码
  sqlite3_vfs * const pVfs = pPager->pVfs;   /* Local cache of vfs pointer */ //本地缓存的vfs指针

  assert( pPager->eState==PAGER_WRITER_LOCKED );
  assert( assert_pager_state(pPager) );
  assert( pPager->pInJournal==0 );
  
  /* If already in the error state, this function is a no-op.  But on
  ** the other hand, this routine is never called if we are already in
  ** an error state. */
  //如果已经在错误的状态，这个函数是一个空操作。
  //但是在另一方面，如果我们已经在一个错误的状态，则这个程序从未被调用。
  if( NEVER(pPager->errCode) ) return pPager->errCode;

  if( !pagerUseWal(pPager) && pPager->journalMode!=PAGER_JOURNALMODE_OFF ){
    pPager->pInJournal = sqlite3BitvecCreate(pPager->dbSize);
    if( pPager->pInJournal==0 ){
      return SQLITE_NOMEM;
    }
  
    /* Open the journal file if it is not already open. */
	//打开日志文件，如果它还没有被打开
    if( !isOpen(pPager->jfd) ){
      if( pPager->journalMode==PAGER_JOURNALMODE_MEMORY ){
        sqlite3MemJournalOpen(pPager->jfd);
      }else{
        const int flags =                   /* VFS flags to open journal file */ //打开日志文件的VFS标识符
          SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|
          (pPager->tempFile ? 
            (SQLITE_OPEN_DELETEONCLOSE|SQLITE_OPEN_TEMP_JOURNAL):
            (SQLITE_OPEN_MAIN_JOURNAL)
          );
  #ifdef SQLITE_ENABLE_ATOMIC_WRITE
        rc = sqlite3JournalOpen(
            pVfs, pPager->zJournal, pPager->jfd, flags, jrnlBufferSize(pPager)
        );
  #else
        rc = sqlite3OsOpen(pVfs, pPager->zJournal, pPager->jfd, flags, 0);
  #endif
      }
      assert( rc!=SQLITE_OK || isOpen(pPager->jfd) );
    }
  
  
    /* Write the first journal header to the journal file and open 
    ** the sub-journal if necessary.
	 写第一个日志标题到日志文件并且若有需要打开子日志。
    */
    if( rc==SQLITE_OK ){
      /* TODO: Check if all of these are really required. */  
		//待办事项：检查是否所有这些都真的需要。
      pPager->nRec = 0;
      pPager->journalOff = 0;
      pPager->setMaster = 0;
      pPager->journalHdr = 0;
      rc = writeJournalHdr(pPager);
    }
  }

  if( rc!=SQLITE_OK ){
    sqlite3BitvecDestroy(pPager->pInJournal);
    pPager->pInJournal = 0;
  }else{
    assert( pPager->eState==PAGER_WRITER_LOCKED );
    pPager->eState = PAGER_WRITER_CACHEMOD;
  }

  return rc;
}


/*
** Begin a write-transaction on the specified pager object. If a 
** write-transaction has already been opened, this function is a no-op.
** 在指定的pager对象上开始一个写事务。如果写事务已经被打开，这个功能就是一个空操作。
** If the exFlag argument is false, then acquire at least a RESERVED
** lock on the database file. If exFlag is true, then acquire at least
** an EXCLUSIVE lock. If such a lock is already held, no locking 
** functions need be called.
   如果exFlag论点是错误的，那么在数据库文件需要至少一个保留锁。如果exFlag是正确的，那么至少需要一个排他锁。
   如果像这样的锁已经被保留，那么没有锁函数被调用。
**
** If the subjInMemory argument is non-zero, then any sub-journal opened
** within this transaction will be opened as an in-memory file. This
** has no effect if the sub-journal is already opened (as it may be when
** running in exclusive mode) or if the transaction does not require a
** sub-journal. If the subjInMemory argument is zero, then any required
** sub-journal is implemented in-memory if pPager is an in-memory database, 
** or using a temporary file otherwise.
   如果subInMemory论点不为零，那么任何子日志在此事务中会以内存文件打开。如果子日志已经被打开（因为它可能在独占模式下运行）
   或者此事务不需要一个子日志，那就没有影响了。如果subjInMemory为零，pPager是一个内存数据库或者另外使用一个临时文件，
   那么任何必要sub-journal实现内存。
*/
int sqlite3PagerBegin(Pager *pPager, int exFlag, int subjInMemory){
  int rc = SQLITE_OK;
  

  if( pPager->errCode ) return pPager->errCode;
  assert( pPager->eState>=PAGER_READER && pPager->eState<PAGER_ERROR );
  pPager->subjInMemory = (u8)subjInMemory;

  if( ALWAYS(pPager->eState==PAGER_READER) ){
    assert( pPager->pInJournal==0 );

    if( pagerUseWal(pPager) ){
      /* If the pager is configured to use locking_mode=exclusive, and an
      ** exclusive lock on the database is not already held, obtain it now.
      如果pager配置为使用排它锁模式，并且排它锁在数据库中没有被保留，立即获得它。
      */
      if( pPager->exclusiveMode && sqlite3WalExclusiveMode(pPager->pWal, -1) ){
        rc = pagerLockDb(pPager, EXCLUSIVE_LOCK);
        if( rc!=SQLITE_OK ){
          return rc;
        }
        sqlite3WalExclusiveMode(pPager->pWal, 1);
      }

      /* Grab the write lock on the log file. If successful, upgrade to
      ** PAGER_RESERVED state. Otherwise, return an error code to the caller.
      ** The busy-handler is not invoked if another connection already
      ** holds the write-lock. If possible, the upper layer will call it.
         获取日志文件写锁。如果成功了，升级到PAGER_RESERVED状态。否则，返回一个错误代码来调用。
         如果另外一个连接已经有写锁，那么busy-handlerr不被调用。如果不成功，上层会调用它。
      */
      rc = sqlite3WalBeginWriteTransaction(pPager->pWal);
    }else{
      /* Obtain a RESERVED lock on the database file. If the exFlag parameter
      ** is true, then immediately upgrade this to an EXCLUSIVE lock. The
      ** busy-handler callback can be used when upgrading to the EXCLUSIVE
      ** lock, but not when obtaining the RESERVED lock.
        在数据库文件中获取保留锁。如果exFlag参数正确，那么它立即升级为一个排它锁。
        当升级到排它锁不是获得保留锁，它会使用busy-handler回调。
      */
      rc = pagerLockDb(pPager, RESERVED_LOCK);
      if( rc==SQLITE_OK && exFlag ){
        rc = pager_wait_on_lock(pPager, EXCLUSIVE_LOCK);
      }
    }

    if( rc==SQLITE_OK ){
      /* Change to WRITER_LOCKED state.
      ** 改变WRITER_LOCKED状态。
      ** WAL mode sets Pager.eState to PAGER_WRITER_LOCKED or CACHEMOD
      ** when it has an open transaction, but never to DBMOD or FINISHED.
      ** This is because in those states the code to roll back savepoint 
      ** transactions may copy data from the sub-journal into the database 
      ** file as well as into the page cache. Which would be incorrect in 
      ** WAL mode.
         WAL设置Pager。当有一个打开的事务没有DAMOD、FINISHED，那么eState 变成 PAGER_WRITER_LOCKED or CACHEMOD。
         这是因为在这些状态中代码回滚保存点事务，可能会将sub-journal数据复制到数据库文件以及页面缓存。
         在WAL模式下，这将是不正确的。
      */
      pPager->eState = PAGER_WRITER_LOCKED;
      pPager->dbHintSize = pPager->dbSize;
      pPager->dbFileSize = pPager->dbSize;
      pPager->dbOrigSize = pPager->dbSize;
      pPager->journalOff = 0;
    }

    assert( rc==SQLITE_OK || pPager->eState==PAGER_READER );
    assert( rc!=SQLITE_OK || pPager->eState==PAGER_WRITER_LOCKED );
    assert( assert_pager_state(pPager) );
  }

  PAGERTRACE(("TRANSACTION %d\n", PAGERID(pPager)));
  return rc;
}

/*
** Mark a single data page as writeable. The page is written into the 
** main journal or sub-journal as required. If the page is written into
** one of the journals, the corresponding bit is set in the 
** Pager.pInJournal bitvec and the PagerSavepoint.pInSavepoint bitvecs
** of any open savepoints as appropriate.
    把一个数据页标记为可写。这一页会按照要求被写入主日志或者子日志。如果被写入其中一个日志，在Pager设置相应位。
    pInJournal bitvec 和PagerSavepoint。pInSavepoint bitvecs是适当保存点。
*/
static int pager_write(PgHdr *pPg){
  void *pData = pPg->pData;
  Pager *pPager = pPg->pPager;
  int rc = SQLITE_OK;

  /* This routine is not called unless a write-transaction has already 
  ** been started. The journal file may or may not be open at this point.
  ** It is never called in the ERROR state.
     这个程序不会被调用除非写事务已经开始。此时，这个日志文件或许会被打开也或许不会被打开。
     在异常状态下，它从来没被调用。
  */
  assert( pPager->eState==PAGER_WRITER_LOCKED
       || pPager->eState==PAGER_WRITER_CACHEMOD
       || pPager->eState==PAGER_WRITER_DBMOD
  );
  assert( assert_pager_state(pPager) );

  /* If an error has been previously detected, report the same error
  ** again. This should not happen, but the check provides robustness.
      如果错误先被检测到，再次报告这一样的错误。这是不能发生的，但是检查提供了健壮性。
   */
  if( NEVER(pPager->errCode) )  return pPager->errCode;

  /* Higher-level routines never call this function if database is not
  ** writable.  But check anyway, just for robustness. 
     如果数据库不可写，高级程序不会调用这个功能。但无论如何，检查只是为了健壮性。
  */
  if( NEVER(pPager->readOnly) ) return SQLITE_PERM;

  CHECK_PAGE(pPg);

  /* The journal file needs to be opened. Higher level routines have already
  ** obtained the necessary locks to begin the write-transaction, but the
  ** rollback journal might not yet be open. Open it now if this is the case.
  ** 日志文件需要被打开。高级程序已经获得必要锁开始这个写事务，但是恢复日志可能不会开放。
      如果是这种情况，现在打开它。
  ** This is done before calling sqlite3PcacheMakeDirty() on the page. 
  ** Otherwise, if it were done after calling sqlite3PcacheMakeDirty(), then
  ** an error might occur and the pager would end up in WRITER_LOCKED state
  ** with pages marked as dirty in the cache.
     在调用 sqlite3PcacheMakeDirty()函数之前，这是已完成的。否则，如果在调用 sqlite3PcacheMakeDirty()函数之后完成
     会出现错误，pager会在 WRITER_LOCKED状态下结束，在缓存中pages被标记为dirty。
  */
  if( pPager->eState==PAGER_WRITER_LOCKED ){
    rc = pager_open_journal(pPager);
    if( rc!=SQLITE_OK ) return rc;
  }
  assert( pPager->eState>=PAGER_WRITER_CACHEMOD );
  assert( assert_pager_state(pPager) );

  /* Mark the page as dirty.  If the page has already been written
  ** to the journal then we can return right away.
     页面标记为dirty。如果这个页面已经被写入日志，那么我们可以立即返回。
  */
  sqlite3PcacheMakeDirty(pPg);
  if( pageInJournal(pPg) && !subjRequiresPage(pPg) ){
    assert( !pagerUseWal(pPager) );
  }else{
  
    /* The transaction journal now exists and we have a RESERVED or an
    ** EXCLUSIVE lock on the main database file.  Write the current page to
    ** the transaction journal if it is not there already.
       事务日志已经存在，并且在主数据库文件中有一个保留锁或者一个排它锁。
       如果它已经不存在了，把当前页写入事务日志。
    */
    if( !pageInJournal(pPg) && !pagerUseWal(pPager) ){
      assert( pagerUseWal(pPager)==0 );
      if( pPg->pgno<=pPager->dbOrigSize && isOpen(pPager->jfd) ){
        u32 cksum;
        char *pData2;
        i64 iOff = pPager->journalOff;

        /* We should never write to the journal file the page that
        ** contains the database locks.  The following assert verifies
        ** that we do not.
           我们不应该把包含数据库锁的页面写入事务日志。下面的声明证实我们不能。
        */
        assert( pPg->pgno!=PAGER_MJ_PGNO(pPager) );

        assert( pPager->journalHdr<=pPager->journalOff );
        CODEC2(pPager, pData, pPg->pgno, 7, return SQLITE_NOMEM, pData2);
        cksum = pager_cksum(pPager, (u8*)pData2);

        /* Even if an IO or diskfull error occurs while journalling the
        ** page in the block above, set the need-sync flag for the page.
        ** Otherwise, when the transaction is rolled back, the logic in
        ** playback_one_page() will think that the page needs to be restored
        ** in the database file. And if an IO error occurs while doing so,
        ** then corruption may follow.
           即使IO或者DISKFULL错误在上面块的日志文件中发生，为这个页面设置need-sync标记。
           否则，当日志恢复， playback_one_page() 逻辑将认为这个页面需要在数据库文件中被恢复。
           如果一个io错误同时发生，那么会中断。
        */
        pPg->flags |= PGHDR_NEED_SYNC;

        rc = write32bits(pPager->jfd, iOff, pPg->pgno);
        if( rc!=SQLITE_OK ) return rc;
        rc = sqlite3OsWrite(pPager->jfd, pData2, pPager->pageSize, iOff+4);
        if( rc!=SQLITE_OK ) return rc;
        rc = write32bits(pPager->jfd, iOff+pPager->pageSize+4, cksum);
        if( rc!=SQLITE_OK ) return rc;

        IOTRACE(("JOUT %p %d %lld %d\n", pPager, pPg->pgno, 
                 pPager->journalOff, pPager->pageSize));
        PAGER_INCR(sqlite3_pager_writej_count);
        PAGERTRACE(("JOURNAL %d page %d needSync=%d hash(%08x)\n",
             PAGERID(pPager), pPg->pgno, 
             ((pPg->flags&PGHDR_NEED_SYNC)?1:0), pager_pagehash(pPg)));

        pPager->journalOff += 8 + pPager->pageSize;
        pPager->nRec++;
        assert( pPager->pInJournal!=0 );
        rc = sqlite3BitvecSet(pPager->pInJournal, pPg->pgno);
        testcase( rc==SQLITE_NOMEM );
        assert( rc==SQLITE_OK || rc==SQLITE_NOMEM );
        rc |= addToSavepointBitvecs(pPager, pPg->pgno);
        if( rc!=SQLITE_OK ){
          assert( rc==SQLITE_NOMEM );
          return rc;
        }
      }else{
        if( pPager->eState!=PAGER_WRITER_DBMOD ){
          pPg->flags |= PGHDR_NEED_SYNC;
        }
        PAGERTRACE(("APPEND %d page %d needSync=%d\n",
                PAGERID(pPager), pPg->pgno,
               ((pPg->flags&PGHDR_NEED_SYNC)?1:0)));
      }
    }
  
    /* If the statement journal is open and the page is not in it,
    ** then write the current page to the statement journal.  Note that
    ** the statement journal format differs from the standard journal format
    ** in that it omits the checksums and the header.
       如果打开声明日志，里面没有页面。那么把当前页面写入这个声明日志。
       注意这个声明日志格式不同于标准日志格式，它省去了校验和标题。
    */
    if( subjRequiresPage(pPg) ){
      rc = subjournalPage(pPg);
    }
  }

  /* Update the database size and return.
    更新数据库大小并且返回。
  */
  if( pPager->dbSize<pPg->pgno ){
    pPager->dbSize = pPg->pgno;
  }
  return rc;
}

/*
** Mark a data page as writeable. This routine must be called before 
** making changes to a page. The caller must check the return value 
** of this function and be careful not to change any page data unless 
** this routine returns SQLITE_OK.
   标记一个数据页面可写。在修改页面之前，这个程序必须被调用。调用者必须检查这个功能的返回值并且不能修改任何页面数据，
   除非这个程序返回SQLITE_OK。
** The difference between this function and pager_write() is that this
** function also deals with the special case where 2 or more pages
** fit on a single disk sector. In this case all co-resident pages
** must have been written to the journal file before returning.
** 这个功能和pager_write()之间的区别是指这个功能同时处理2个或更多页面适合单个磁盘扇区这种特殊情况。
   在这种特殊情况下，所有共驻贮存的页面必须在返回前被写入日志文件。
** If an error occurs, SQLITE_NOMEM or an IO error code is returned
** as appropriate. Otherwise, SQLITE_OK.
   如果发生错误，SQLITE_NOMEM 和IO错误代码被适当返回。否则，SQLITE_OK。
*/
int sqlite3PagerWrite(DbPage *pDbPage){
  int rc = SQLITE_OK;

  PgHdr *pPg = pDbPage;
  Pager *pPager = pPg->pPager;
  Pgno nPagePerSector = (pPager->sectorSize/pPager->pageSize);

  assert( pPager->eState>=PAGER_WRITER_LOCKED );
  assert( pPager->eState!=PAGER_ERROR );
  assert( assert_pager_state(pPager) );

  if( nPagePerSector>1 ){
    Pgno nPageCount;          /* Total number of pages in database file 数据库文件中的总页数*/
    Pgno pg1;                 /* First page of the sector pPg is located on.pPg扇区中第一个页面的位置 */
    int nPage = 0;            /* Number of pages starting at pg1 to journal 从pg1到日志的总页数*/
    int ii;                   /* Loop counter 循环计数器*/
    int needSync = 0;         /* True if any page has PGHDR_NEED_SYNC 任何有PGHDR_NEED_SYNC的页面是对的*/

    /* Set the doNotSyncSpill flag to 1. This is because we cannot allow
    ** a journal header to be written between the pages journaled by
    ** this function.
       doNotSyncSpill标记设置为1.这是因为我们不允许日志标题以这种日志功能被写入页面。
    */
    assert( !MEMDB );
    assert( pPager->doNotSyncSpill==0 );
    pPager->doNotSyncSpill++;

    /* This trick assumes that both the page-size and sector-size are
    ** an integer power of 2. It sets variable pg1 to the identifier
    ** of the first page of the sector pPg is located on.
       这个方法假定页面大小和扇区大小都是2的整数幂。
       它设置变量pg1到扇区pPg第一页面标示符的位置。
    */
    pg1 = ((pPg->pgno-1) & ~(nPagePerSector-1)) + 1;

    nPageCount = pPager->dbSize;
    if( pPg->pgno>nPageCount ){
      nPage = (pPg->pgno - pg1)+1;
    }else if( (pg1+nPagePerSector-1)>nPageCount ){
      nPage = nPageCount+1-pg1;
    }else{
      nPage = nPagePerSector;
    }
    assert(nPage>0);
    assert(pg1<=pPg->pgno);
    assert((pg1+nPage)>pPg->pgno);

    for(ii=0; ii<nPage && rc==SQLITE_OK; ii++){
      Pgno pg = pg1+ii;
      PgHdr *pPage;
      if( pg==pPg->pgno || !sqlite3BitvecTest(pPager->pInJournal, pg) ){
        if( pg!=PAGER_MJ_PGNO(pPager) ){
          rc = sqlite3PagerGet(pPager, pg, &pPage);
          if( rc==SQLITE_OK ){
            rc = pager_write(pPage);
            if( pPage->flags&PGHDR_NEED_SYNC ){
              needSync = 1;
            }
            sqlite3PagerUnref(pPage);
          }
        }
      }else if( (pPage = pager_lookup(pPager, pg))!=0 ){
        if( pPage->flags&PGHDR_NEED_SYNC ){
          needSync = 1;
        }
        sqlite3PagerUnref(pPage);
      }
    }

    /* If the PGHDR_NEED_SYNC flag is set for any of the nPage pages 
    ** starting at pg1, then it needs to be set for all of them. Because
    ** writing to any of these nPage pages may damage the others, the
    ** journal file must contain sync()ed copies of all of them
    ** before any of them can be written out to the database file.
       如果PGHDR_NEED_SYNC标记设置为在pg1启动的nPage下的任何页面，那么它需要设置为全部。
       因为写这些nPage页面会影响到其他，在他们被写出数据库文件之前，日志文件必须包含sync()ed的全部副本。
    */
    if( rc==SQLITE_OK && needSync ){
      assert( !MEMDB );
      for(ii=0; ii<nPage; ii++){
        PgHdr *pPage = pager_lookup(pPager, pg1+ii);
        if( pPage ){
          pPage->flags |= PGHDR_NEED_SYNC;
          sqlite3PagerUnref(pPage);
        }
      }
    }

    assert( pPager->doNotSyncSpill==1 );
    pPager->doNotSyncSpill--;
  }else{
    rc = pager_write(pDbPage);
  }
  return rc;
}

/*
** Return TRUE if the page given in the argument was previously passed
** to sqlite3PagerWrite().  In other words, return TRUE if it is ok
** to change the content of the page.
    如果论点给出的页面被优先通过sqlite3PagerWrite()，那么返回TRUE。
    换句话说，如果它成功改变了页面的内容则返回TRUE。
*/
#ifndef NDEBUG
int sqlite3PagerIswriteable(DbPage *pPg){
  return pPg->flags&PGHDR_DIRTY;
}
#endif

/*
** A call to this routine tells the pager that it is not necessary to
** write the information on page pPg back to the disk, even though
** that page might be marked as dirty.  This happens, for example, when
** the page has been added as a leaf of the freelist and so its
** content no longer matters.
** 调用这个程序告诉pager没有必要编写分回磁盘页的信息，即使页面或许会被标记为dirty。
   这一切发生的时候,例如,当页面添加了自由表中的叶,所以它的内容不再重要。
** The overlying software layer calls this routine when all of the data
** on the given page is unused. The pager marks the page as clean so
** that it does not get written to disk.
** 当所提供页面上的所有数据未使用，整个软件层就调用这个程序。pager标记干净页面以至于它不会被写入磁盘。
** Tests show that this optimization can quadruple the speed of large 
** DELETE operations.测试表明，这种优化可以将删除操作的速度提高四倍。
*/
void sqlite3PagerDontWrite(PgHdr *pPg){
  Pager *pPager = pPg->pPager;
  if( (pPg->flags&PGHDR_DIRTY) && pPager->nSavepoint==0 ){
    PAGERTRACE(("DONT_WRITE page %d of %d\n", pPg->pgno, PAGERID(pPager)));
    IOTRACE(("CLEAN %p %d\n", pPager, pPg->pgno))
    pPg->flags |= PGHDR_DONT_WRITE;
    pager_set_pagehash(pPg);
  }
}

/*
** This routine is called to increment the value of the database file 
** change-counter, stored as a 4-byte big-endian integer starting at 
** byte offset 24 of the pager file.  The secondary change counter at
** 92 is also updated, as is the SQLite version number at offset 96.
* 这个例程被调用来增加数据库文件中change-counter的价值，存储为一个4字节的高位优先整数起始于pager文件中24个字节偏移量。
** But this only happens if the pPager->changeCountDone flag is false.
** To avoid excess churning of page 1, the update only happens once.
** See also the pager_write_changecounter() routine that does an 
** unconditional update of the change counters.
**
** If the isDirectMode flag is zero, then this is done by calling 
** sqlite3PagerWrite() on page 1, then modifying the contents of the
** page data. In this case the file will be updated when the current
** transaction is committed.
**
** The isDirectMode flag may only be non-zero if the library was compiled
** with the SQLITE_ENABLE_ATOMIC_WRITE macro defined. In this case,
** if isDirect is non-zero, then the database file is updated directly
** by writing an updated version of page 1 using a call to the 
** sqlite3OsWrite() function.
*/
static int pager_incr_changecounter(Pager *pPager, int isDirectMode){
  int rc = SQLITE_OK;

  assert( pPager->eState==PAGER_WRITER_CACHEMOD
       || pPager->eState==PAGER_WRITER_DBMOD
  );
  assert( assert_pager_state(pPager) );

  /* Declare and initialize constant integer 'isDirect'. If the
  ** atomic-write optimization is enabled in this build, then isDirect
  ** is initialized to the value passed as the isDirectMode parameter
  ** to this function. Otherwise, it is always set to zero.
  **
  ** The idea is that if the atomic-write optimization is not
  ** enabled at compile time, the compiler can omit the tests of
  ** 'isDirect' below, as well as the block enclosed in the
  ** "if( isDirect )" condition.
  */
#ifndef SQLITE_ENABLE_ATOMIC_WRITE
# define DIRECT_MODE 0
  assert( isDirectMode==0 );
  UNUSED_PARAMETER(isDirectMode);
#else
# define DIRECT_MODE isDirectMode
#endif

  if( !pPager->changeCountDone && pPager->dbSize>0 ){
    PgHdr *pPgHdr;                /* Reference to page 1 */

    assert( !pPager->tempFile && isOpen(pPager->fd) );

    /* Open page 1 of the file for writing. */
    rc = sqlite3PagerGet(pPager, 1, &pPgHdr);
    assert( pPgHdr==0 || rc==SQLITE_OK );

    /* If page one was fetched successfully, and this function is not
    ** operating in direct-mode, make page 1 writable.  When not in 
    ** direct mode, page 1 is always held in cache and hence the PagerGet()
    ** above is always successful - hence the ALWAYS on rc==SQLITE_OK.
    */
    if( !DIRECT_MODE && ALWAYS(rc==SQLITE_OK) ){
      rc = sqlite3PagerWrite(pPgHdr);
    }

    if( rc==SQLITE_OK ){
      /* Actually do the update of the change counter */
      pager_write_changecounter(pPgHdr);

      /* If running in direct mode, write the contents of page 1 to the file. */
      if( DIRECT_MODE ){
        const void *zBuf;
        assert( pPager->dbFileSize>0 );
        CODEC2(pPager, pPgHdr->pData, 1, 6, rc=SQLITE_NOMEM, zBuf);
        if( rc==SQLITE_OK ){
          rc = sqlite3OsWrite(pPager->fd, zBuf, pPager->pageSize, 0);
          pPager->aStat[PAGER_STAT_WRITE]++;
        }
        if( rc==SQLITE_OK ){
          pPager->changeCountDone = 1;
        }
      }else{
        pPager->changeCountDone = 1;
      }
    }

    /* Release the page reference. */
    sqlite3PagerUnref(pPgHdr);
  }
  return rc;
}

/*
** Sync the database file to disk. This is a no-op for in-memory databases
** or pages with the Pager.noSync flag set.
**
** If successful, or if called on a pager for which it is a no-op, this
** function returns SQLITE_OK. Otherwise, an IO error code is returned.
*/
int sqlite3PagerSync(Pager *pPager){
  int rc = SQLITE_OK;
  if( !pPager->noSync ){
    assert( !MEMDB );
    rc = sqlite3OsSync(pPager->fd, pPager->syncFlags);
  }else if( isOpen(pPager->fd) ){
    assert( !MEMDB );
    rc = sqlite3OsFileControl(pPager->fd, SQLITE_FCNTL_SYNC_OMITTED, 0);
    if( rc==SQLITE_NOTFOUND ){
      rc = SQLITE_OK;
    }
  }
  return rc;
}

/*
** This function may only be called while a write-transaction is active in
** rollback. If the connection is in WAL mode, this call is a no-op. 
** Otherwise, if the connection does not already have an EXCLUSIVE lock on 
** the database file, an attempt is made to obtain one.
**
** If the EXCLUSIVE lock is already held or the attempt to obtain it is
** successful, or the connection is in WAL mode, SQLITE_OK is returned.
** Otherwise, either SQLITE_BUSY or an SQLITE_IOERR_XXX error code is 
** returned.
*/
int sqlite3PagerExclusiveLock(Pager *pPager){
  int rc = SQLITE_OK;
  assert( pPager->eState==PAGER_WRITER_CACHEMOD 
       || pPager->eState==PAGER_WRITER_DBMOD 
       || pPager->eState==PAGER_WRITER_LOCKED 
  );
  assert( assert_pager_state(pPager) );
  if( 0==pagerUseWal(pPager) ){
    rc = pager_wait_on_lock(pPager, EXCLUSIVE_LOCK);
  }
  return rc;
}

/*
** Sync the database file for the pager pPager. zMaster points to the name
** of a master journal file that should be written into the individual
** journal file. zMaster may be NULL, which is interpreted as no master
** journal (a single database transaction).
**
** This routine ensures that:
**
**   * The database file change-counter is updated,
**   * the journal is synced (unless the atomic-write optimization is used),
**   * all dirty pages are written to the database file, 
**   * the database file is truncated (if required), and
**   * the database file synced. 
**
** The only thing that remains to commit the transaction is to finalize 
** (delete, truncate or zero the first part of) the journal file (or 
** delete the master journal file if specified).
**
** Note that if zMaster==NULL, this does not overwrite a previous value
** passed to an sqlite3PagerCommitPhaseOne() call.
**
** If the final parameter - noSync - is true, then the database file itself
** is not synced. The caller must call sqlite3PagerSync() directly to
** sync the database file before calling CommitPhaseTwo() to delete the
** journal file in this case.
*/
int sqlite3PagerCommitPhaseOne(
  Pager *pPager,                  /* Pager object */
  const char *zMaster,            /* If not NULL, the master journal name */
  int noSync                      /* True to omit the xSync on the db file */
){
  int rc = SQLITE_OK;             /* Return code */

  assert( pPager->eState==PAGER_WRITER_LOCKED
       || pPager->eState==PAGER_WRITER_CACHEMOD
       || pPager->eState==PAGER_WRITER_DBMOD
       || pPager->eState==PAGER_ERROR
  );
  assert( assert_pager_state(pPager) );

  /* If a prior error occurred, report that error again. */
  if( NEVER(pPager->errCode) ) return pPager->errCode;

  PAGERTRACE(("DATABASE SYNC: File=%s zMaster=%s nSize=%d\n", 
      pPager->zFilename, zMaster, pPager->dbSize));

  /* If no database changes have been made, return early. */
  if( pPager->eState<PAGER_WRITER_CACHEMOD ) return SQLITE_OK;

  if( MEMDB ){
    /* If this is an in-memory db, or no pages have been written to, or this
    ** function has already been called, it is mostly a no-op.  However, any
    ** backup in progress needs to be restarted.
    */
    sqlite3BackupRestart(pPager->pBackup);
  }else{
    if( pagerUseWal(pPager) ){
      PgHdr *pList = sqlite3PcacheDirtyList(pPager->pPCache);
      PgHdr *pPageOne = 0;
      if( pList==0 ){
        /* Must have at least one page for the WAL commit flag.
        ** Ticket [2d1a5c67dfc2363e44f29d9bbd57f] 2011-05-18 */
        rc = sqlite3PagerGet(pPager, 1, &pPageOne);
        pList = pPageOne;
        pList->pDirty = 0;
      }
      assert( rc==SQLITE_OK );
      if( ALWAYS(pList) ){
        rc = pagerWalFrames(pPager, pList, pPager->dbSize, 1);
      }
      sqlite3PagerUnref(pPageOne);
      if( rc==SQLITE_OK ){
        sqlite3PcacheCleanAll(pPager->pPCache);
      }
    }else{
      /* The following block updates the change-counter. Exactly how it
      ** does this depends on whether or not the atomic-update optimization
      ** was enabled at compile time, and if this transaction meets the 
      ** runtime criteria to use the operation: 
      **
      **    * The file-system supports the atomic-write property for
      **      blocks of size page-size, and 
      **    * This commit is not part of a multi-file transaction, and
      **    * Exactly one page has been modified and store in the journal file.
      **
      ** If the optimization was not enabled at compile time, then the
      ** pager_incr_changecounter() function is called to update the change
      ** counter in 'indirect-mode'. If the optimization is compiled in but
      ** is not applicable to this transaction, call sqlite3JournalCreate()
      ** to make sure the journal file has actually been created, then call
      ** pager_incr_changecounter() to update the change-counter in indirect
      ** mode. 
      **
      ** Otherwise, if the optimization is both enabled and applicable,
      ** then call pager_incr_changecounter() to update the change-counter
      ** in 'direct' mode. In this case the journal file will never be
      ** created for this transaction.
      */
  #ifdef SQLITE_ENABLE_ATOMIC_WRITE
      PgHdr *pPg;
      assert( isOpen(pPager->jfd) 
           || pPager->journalMode==PAGER_JOURNALMODE_OFF 
           || pPager->journalMode==PAGER_JOURNALMODE_WAL 
      );
      if( !zMaster && isOpen(pPager->jfd) 
       && pPager->journalOff==jrnlBufferSize(pPager) 
       && pPager->dbSize>=pPager->dbOrigSize
       && (0==(pPg = sqlite3PcacheDirtyList(pPager->pPCache)) || 0==pPg->pDirty)
      ){
        /* Update the db file change counter via the direct-write method. The 
        ** following call will modify the in-memory representation of page 1 
        ** to include the updated change counter and then write page 1 
        ** directly to the database file. Because of the atomic-write 
        ** property of the host file-system, this is safe.
        */
        rc = pager_incr_changecounter(pPager, 1);
      }else{
        rc = sqlite3JournalCreate(pPager->jfd);
        if( rc==SQLITE_OK ){
          rc = pager_incr_changecounter(pPager, 0);
        }
      }
  #else
      rc = pager_incr_changecounter(pPager, 0);
  #endif
      if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
  
      /* If this transaction has made the database smaller, then all pages
      ** being discarded by the truncation must be written to the journal
      ** file. This can only happen in auto-vacuum mode.
      **
      ** Before reading the pages with page numbers larger than the 
      ** current value of Pager.dbSize, set dbSize back to the value
      ** that it took at the start of the transaction. Otherwise, the
      ** calls to sqlite3PagerGet() return zeroed pages instead of 
      ** reading data from the database file.
      */
  #ifndef SQLITE_OMIT_AUTOVACUUM
      if( pPager->dbSize<pPager->dbOrigSize 
       && pPager->journalMode!=PAGER_JOURNALMODE_OFF
      ){
        Pgno i;                                   /* Iterator variable */
        const Pgno iSkip = PAGER_MJ_PGNO(pPager); /* Pending lock page */
        const Pgno dbSize = pPager->dbSize;       /* Database image size */ 
        pPager->dbSize = pPager->dbOrigSize;
        for( i=dbSize+1; i<=pPager->dbOrigSize; i++ ){
          if( !sqlite3BitvecTest(pPager->pInJournal, i) && i!=iSkip ){
            PgHdr *pPage;             /* Page to journal */
            rc = sqlite3PagerGet(pPager, i, &pPage);
            if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
            rc = sqlite3PagerWrite(pPage);
            sqlite3PagerUnref(pPage);
            if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
          }
        }
        pPager->dbSize = dbSize;
      } 
  #endif
  
      /* Write the master journal name into the journal file. If a master 
      ** journal file name has already been written to the journal file, 
      ** or if zMaster is NULL (no master journal), then this call is a no-op.
      */
      rc = writeMasterJournal(pPager, zMaster);
      if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
  
      /* Sync the journal file and write all dirty pages to the database.
      ** If the atomic-update optimization is being used, this sync will not 
      ** create the journal file or perform any real IO.
      **
      ** Because the change-counter page was just modified, unless the
      ** atomic-update optimization is used it is almost certain that the
      ** journal requires a sync here. However, in locking_mode=exclusive
      ** on a system under memory pressure it is just possible that this is 
      ** not the case. In this case it is likely enough that the redundant
      ** xSync() call will be changed to a no-op by the OS anyhow. 
      */
      rc = syncJournal(pPager, 0);
      if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
  
      rc = pager_write_pagelist(pPager,sqlite3PcacheDirtyList(pPager->pPCache));
      if( rc!=SQLITE_OK ){
        assert( rc!=SQLITE_IOERR_BLOCKED );
        goto commit_phase_one_exit;
      }
      sqlite3PcacheCleanAll(pPager->pPCache);
  
      /* If the file on disk is not the same size as the database image,
      ** then use pager_truncate to grow or shrink the file here.
      */
      if( pPager->dbSize!=pPager->dbFileSize ){
        Pgno nNew = pPager->dbSize - (pPager->dbSize==PAGER_MJ_PGNO(pPager));
        assert( pPager->eState==PAGER_WRITER_DBMOD );
        rc = pager_truncate(pPager, nNew);
        if( rc!=SQLITE_OK ) goto commit_phase_one_exit;
      }
  
      /* Finally, sync the database file. */
      if( !noSync ){
        rc = sqlite3PagerSync(pPager);
      }
      IOTRACE(("DBSYNC %p\n", pPager))
    }
  }

commit_phase_one_exit:
  if( rc==SQLITE_OK && !pagerUseWal(pPager) ){
    pPager->eState = PAGER_WRITER_FINISHED;
  }
  return rc;
}


/*
** When this function is called, the database file has been completely
** updated to reflect the changes made by the current transaction and
** synced to disk. The journal file still exists in the file-system 
** though, and if a failure occurs at this point it will eventually
** be used as a hot-journal and the current transaction rolled back.
**
** This function finalizes the journal file, either by deleting, 
** truncating or partially zeroing it, so that it cannot be used 
** for hot-journal rollback. Once this is done the transaction is
** irrevocably committed.
**
** If an error occurs, an IO error code is returned and the pager
** moves into the error state. Otherwise, SQLITE_OK is returned.
*/
int sqlite3PagerCommitPhaseTwo(Pager *pPager){
  int rc = SQLITE_OK;                  /* Return code */

  /* This routine should not be called if a prior error has occurred.
  ** But if (due to a coding error elsewhere in the system) it does get
  ** called, just return the same error code without doing anything. */
  if( NEVER(pPager->errCode) ) return pPager->errCode;

  assert( pPager->eState==PAGER_WRITER_LOCKED
       || pPager->eState==PAGER_WRITER_FINISHED
       || (pagerUseWal(pPager) && pPager->eState==PAGER_WRITER_CACHEMOD)
  );
  assert( assert_pager_state(pPager) );

  /* An optimization. If the database was not actually modified during
  ** this transaction, the pager is running in exclusive-mode and is
  ** using persistent journals, then this function is a no-op.
  **
  ** The start of the journal file currently contains a single journal 
  ** header with the nRec field set to 0. If such a journal is used as
  ** a hot-journal during hot-journal rollback, 0 changes will be made
  ** to the database file. So there is no need to zero the journal 
  ** header. Since the pager is in exclusive mode, there is no need
  ** to drop any locks either.
  */
  if( pPager->eState==PAGER_WRITER_LOCKED 
   && pPager->exclusiveMode 
   && pPager->journalMode==PAGER_JOURNALMODE_PERSIST
  ){
    assert( pPager->journalOff==JOURNAL_HDR_SZ(pPager) || !pPager->journalOff );
    pPager->eState = PAGER_READER;
    return SQLITE_OK;
  }

  PAGERTRACE(("COMMIT %d\n", PAGERID(pPager)));
  rc = pager_end_transaction(pPager, pPager->setMaster);
  return pager_error(pPager, rc);
}

/*
** If a write transaction is open, then all changes made within the 
** transaction are reverted and the current write-transaction is closed.
** The pager falls back to PAGER_READER state if successful, or PAGER_ERROR
** state if an error occurs.
**
** If the pager is already in PAGER_ERROR state when this function is called,
** it returns Pager.errCode immediately. No work is performed in this case.
**
** Otherwise, in rollback mode, this function performs two functions:
**
**   1) It rolls back the journal file, restoring all database file and 
**      in-memory cache pages to the state they were in when the transaction
**      was opened, and
**
**   2) It finalizes the journal file, so that it is not used for hot
**      rollback at any point in the future.
**
** Finalization of the journal file (task 2) is only performed if the 
** rollback is successful.
**
** In WAL mode, all cache-entries containing data modified within the
** current transaction are either expelled from the cache or reverted to
** their pre-transaction state by re-reading data from the database or
** WAL files. The WAL transaction is then closed.
*/
int sqlite3PagerRollback(Pager *pPager){
  int rc = SQLITE_OK;                  /* Return code */
  PAGERTRACE(("ROLLBACK %d\n", PAGERID(pPager)));

  /* PagerRollback() is a no-op if called in READER or OPEN state. If
  ** the pager is already in the ERROR state, the rollback is not 
  ** attempted here. Instead, the error code is returned to the caller.
  */
  assert( assert_pager_state(pPager) );
  if( pPager->eState==PAGER_ERROR ) return pPager->errCode;
  if( pPager->eState<=PAGER_READER ) return SQLITE_OK;

  if( pagerUseWal(pPager) ){
    int rc2;
    rc = sqlite3PagerSavepoint(pPager, SAVEPOINT_ROLLBACK, -1);
    rc2 = pager_end_transaction(pPager, pPager->setMaster);
    if( rc==SQLITE_OK ) rc = rc2;
  }else if( !isOpen(pPager->jfd) || pPager->eState==PAGER_WRITER_LOCKED ){
    int eState = pPager->eState;
    rc = pager_end_transaction(pPager, 0);
    if( !MEMDB && eState>PAGER_WRITER_LOCKED ){
      /* This can happen using journal_mode=off. Move the pager to the error 
      ** state to indicate that the contents of the cache may not be trusted.
      ** Any active readers will get SQLITE_ABORT.
      */
      pPager->errCode = SQLITE_ABORT;
      pPager->eState = PAGER_ERROR;
      return rc;
    }
  }else{
    rc = pager_playback(pPager, 0);
  }

  assert( pPager->eState==PAGER_READER || rc!=SQLITE_OK );
  assert( rc==SQLITE_OK || rc==SQLITE_FULL
          || rc==SQLITE_NOMEM || (rc&0xFF)==SQLITE_IOERR );

  /* If an error occurs during a ROLLBACK, we can no longer trust the pager
  ** cache. So call pager_error() on the way out to make any error persistent.
  */
  return pager_error(pPager, rc);
}

/*
** Return TRUE if the database file is opened read-only.  Return FALSE
** if the database is (in theory) writable.
*/
u8 sqlite3PagerIsreadonly(Pager *pPager){
  return pPager->readOnly;
}

/*
** Return the number of references to the pager.
*/
int sqlite3PagerRefcount(Pager *pPager){
  return sqlite3PcacheRefCount(pPager->pPCache);
}

/*
** Return the approximate number of bytes of memory currently
** used by the pager and its associated cache.
*/
int sqlite3PagerMemUsed(Pager *pPager){
  int perPageSize = pPager->pageSize + pPager->nExtra + sizeof(PgHdr)
                                     + 5*sizeof(void*);
  return perPageSize*sqlite3PcachePagecount(pPager->pPCache)
           + sqlite3MallocSize(pPager)
           + pPager->pageSize;
}

/*
** Return the number of references to the specified page.
*/
int sqlite3PagerPageRefcount(DbPage *pPage){
  return sqlite3PcachePageRefcount(pPage);
}

#ifdef SQLITE_TEST
/*
** This routine is used for testing and analysis only.
*/
int *sqlite3PagerStats(Pager *pPager){
  static int a[11];
  a[0] = sqlite3PcacheRefCount(pPager->pPCache);
  a[1] = sqlite3PcachePagecount(pPager->pPCache);
  a[2] = sqlite3PcacheGetCachesize(pPager->pPCache);
  a[3] = pPager->eState==PAGER_OPEN ? -1 : (int) pPager->dbSize;
  a[4] = pPager->eState;
  a[5] = pPager->errCode;
  a[6] = pPager->aStat[PAGER_STAT_HIT];
  a[7] = pPager->aStat[PAGER_STAT_MISS];
  a[8] = 0;  /* Used to be pPager->nOvfl */
  a[9] = pPager->nRead;
  a[10] = pPager->aStat[PAGER_STAT_WRITE];
  return a;
}
#endif

/*
** Parameter eStat must be either SQLITE_DBSTATUS_CACHE_HIT or
** SQLITE_DBSTATUS_CACHE_MISS. Before returning, *pnVal is incremented by the
** current cache hit or miss count, according to the value of eStat. If the 
** reset parameter is non-zero, the cache hit or miss count is zeroed before 
** returning.
*/
void sqlite3PagerCacheStat(Pager *pPager, int eStat, int reset, int *pnVal){

  assert( eStat==SQLITE_DBSTATUS_CACHE_HIT
       || eStat==SQLITE_DBSTATUS_CACHE_MISS
       || eStat==SQLITE_DBSTATUS_CACHE_WRITE
  );

  assert( SQLITE_DBSTATUS_CACHE_HIT+1==SQLITE_DBSTATUS_CACHE_MISS );
  assert( SQLITE_DBSTATUS_CACHE_HIT+2==SQLITE_DBSTATUS_CACHE_WRITE );
  assert( PAGER_STAT_HIT==0 && PAGER_STAT_MISS==1 && PAGER_STAT_WRITE==2 );

  *pnVal += pPager->aStat[eStat - SQLITE_DBSTATUS_CACHE_HIT];
  if( reset ){
    pPager->aStat[eStat - SQLITE_DBSTATUS_CACHE_HIT] = 0;
  }
}

/*
** Return true if this is an in-memory pager.
*/
int sqlite3PagerIsMemdb(Pager *pPager){
  return MEMDB;
}

/*
** Check that there are at least nSavepoint savepoints open. If there are
** currently less than nSavepoints open, then open one or more savepoints
** to make up the difference. If the number of savepoints is already
** equal to nSavepoint, then this function is a no-op.
**
** If a memory allocation fails, SQLITE_NOMEM is returned. If an error 
** occurs while opening the sub-journal file, then an IO error code is
** returned. Otherwise, SQLITE_OK.
*/
int sqlite3PagerOpenSavepoint(Pager *pPager, int nSavepoint){
  int rc = SQLITE_OK;                       /* Return code */
  int nCurrent = pPager->nSavepoint;        /* Current number of savepoints */

  assert( pPager->eState>=PAGER_WRITER_LOCKED );
  assert( assert_pager_state(pPager) );

  if( nSavepoint>nCurrent && pPager->useJournal ){
    int ii;                                 /* Iterator variable */
    PagerSavepoint *aNew;                   /* New Pager.aSavepoint array */

    /* Grow the Pager.aSavepoint array using realloc(). Return SQLITE_NOMEM
    ** if the allocation fails. Otherwise, zero the new portion in case a 
    ** malloc failure occurs while populating it in the for(...) loop below.
    */
    aNew = (PagerSavepoint *)sqlite3Realloc(
        pPager->aSavepoint, sizeof(PagerSavepoint)*nSavepoint
    );
    if( !aNew ){
      return SQLITE_NOMEM;
    }
    memset(&aNew[nCurrent], 0, (nSavepoint-nCurrent) * sizeof(PagerSavepoint));
    pPager->aSavepoint = aNew;

    /* Populate the PagerSavepoint structures just allocated. */
    for(ii=nCurrent; ii<nSavepoint; ii++){
      aNew[ii].nOrig = pPager->dbSize;
      if( isOpen(pPager->jfd) && pPager->journalOff>0 ){
        aNew[ii].iOffset = pPager->journalOff;
      }else{
        aNew[ii].iOffset = JOURNAL_HDR_SZ(pPager);
      }
      aNew[ii].iSubRec = pPager->nSubRec;
      aNew[ii].pInSavepoint = sqlite3BitvecCreate(pPager->dbSize);
      if( !aNew[ii].pInSavepoint ){
        return SQLITE_NOMEM;
      }
      if( pagerUseWal(pPager) ){
        sqlite3WalSavepoint(pPager->pWal, aNew[ii].aWalData);
      }
      pPager->nSavepoint = ii+1;
    }
    assert( pPager->nSavepoint==nSavepoint );
    assertTruncateConstraint(pPager);
  }

  return rc;
}

/*
** This function is called to rollback or release (commit) a savepoint.
** The savepoint to release or rollback need not be the most recently 
** created savepoint.
**
** Parameter op is always either SAVEPOINT_ROLLBACK or SAVEPOINT_RELEASE.
** If it is SAVEPOINT_RELEASE, then release and destroy the savepoint with
** index iSavepoint. If it is SAVEPOINT_ROLLBACK, then rollback all changes
** that have occurred since the specified savepoint was created.
**
** The savepoint to rollback or release is identified by parameter 
** iSavepoint. A value of 0 means to operate on the outermost savepoint
** (the first created). A value of (Pager.nSavepoint-1) means operate
** on the most recently created savepoint. If iSavepoint is greater than
** (Pager.nSavepoint-1), then this function is a no-op.
**
** If a negative value is passed to this function, then the current
** transaction is rolled back. This is different to calling 
** sqlite3PagerRollback() because this function does not terminate
** the transaction or unlock the database, it just restores the 
** contents of the database to its original state. 
**
** In any case, all savepoints with an index greater than iSavepoint 
** are destroyed. If this is a release operation (op==SAVEPOINT_RELEASE),
** then savepoint iSavepoint is also destroyed.
**
** This function may return SQLITE_NOMEM if a memory allocation fails,
** or an IO error code if an IO error occurs while rolling back a 
** savepoint. If no errors occur, SQLITE_OK is returned.
*/ 
int sqlite3PagerSavepoint(Pager *pPager, int op, int iSavepoint){
  int rc = pPager->errCode;       /* Return code */

  assert( op==SAVEPOINT_RELEASE || op==SAVEPOINT_ROLLBACK );
  assert( iSavepoint>=0 || op==SAVEPOINT_ROLLBACK );

  if( rc==SQLITE_OK && iSavepoint<pPager->nSavepoint ){
    int ii;            /* Iterator variable */
    int nNew;          /* Number of remaining savepoints after this op. */

    /* Figure out how many savepoints will still be active after this
    ** operation. Store this value in nNew. Then free resources associated 
    ** with any savepoints that are destroyed by this operation.
    */
    nNew = iSavepoint + (( op==SAVEPOINT_RELEASE ) ? 0 : 1);
    for(ii=nNew; ii<pPager->nSavepoint; ii++){
      sqlite3BitvecDestroy(pPager->aSavepoint[ii].pInSavepoint);
    }
    pPager->nSavepoint = nNew;

    /* If this is a release of the outermost savepoint, truncate 
    ** the sub-journal to zero bytes in size. */
    if( op==SAVEPOINT_RELEASE ){
      if( nNew==0 && isOpen(pPager->sjfd) ){
        /* Only truncate if it is an in-memory sub-journal. */
        if( sqlite3IsMemJournal(pPager->sjfd) ){
          rc = sqlite3OsTruncate(pPager->sjfd, 0);
          assert( rc==SQLITE_OK );
        }
        pPager->nSubRec = 0;
      }
    }
    /* Else this is a rollback operation, playback the specified savepoint.
    ** If this is a temp-file, it is possible that the journal file has
    ** not yet been opened. In this case there have been no changes to
    ** the database file, so the playback operation can be skipped.
    */
    else if( pagerUseWal(pPager) || isOpen(pPager->jfd) ){
      PagerSavepoint *pSavepoint = (nNew==0)?0:&pPager->aSavepoint[nNew-1];
      rc = pagerPlaybackSavepoint(pPager, pSavepoint);
      assert(rc!=SQLITE_DONE);
    }
  }

  return rc;
}

/*
** Return the full pathname of the database file.
**
** Except, if the pager is in-memory only, then return an empty string if
** nullIfMemDb is true.  This routine is called with nullIfMemDb==1 when
** used to report the filename to the user, for compatibility with legacy
** behavior.  But when the Btree needs to know the filename for matching to
** shared cache, it uses nullIfMemDb==0 so that in-memory databases can
** participate in shared-cache.
*/
const char *sqlite3PagerFilename(Pager *pPager, int nullIfMemDb){
  return (nullIfMemDb && pPager->memDb) ? "" : pPager->zFilename;
}

/*
** Return the VFS structure for the pager.
*/
const sqlite3_vfs *sqlite3PagerVfs(Pager *pPager){
  return pPager->pVfs;
}

/*
** Return the file handle for the database file associated
** with the pager.  This might return NULL if the file has
** not yet been opened.
*/
sqlite3_file *sqlite3PagerFile(Pager *pPager){
  return pPager->fd;
}

/*
** Return the full pathname of the journal file.
*/
const char *sqlite3PagerJournalname(Pager *pPager){
  return pPager->zJournal;
}

/*
** Return true if fsync() calls are disabled for this pager.  Return FALSE
** if fsync()s are executed normally.
*/
int sqlite3PagerNosync(Pager *pPager){
  return pPager->noSync;
}

#ifdef SQLITE_HAS_CODEC
/*
** Set or retrieve the codec for this pager
*/
void sqlite3PagerSetCodec(
  Pager *pPager,
  void *(*xCodec)(void*,void*,Pgno,int),
  void (*xCodecSizeChng)(void*,int,int),
  void (*xCodecFree)(void*),
  void *pCodec
){
  if( pPager->xCodecFree ) pPager->xCodecFree(pPager->pCodec);
  pPager->xCodec = pPager->memDb ? 0 : xCodec;
  pPager->xCodecSizeChng = xCodecSizeChng;
  pPager->xCodecFree = xCodecFree;
  pPager->pCodec = pCodec;
  pagerReportSize(pPager);
}
void *sqlite3PagerGetCodec(Pager *pPager){
  return pPager->pCodec;
}
#endif

#ifndef SQLITE_OMIT_AUTOVACUUM
/*
** Move the page pPg to location pgno in the file.
**
** There must be no references to the page previously located at
** pgno (which we call pPgOld) though that page is allowed to be
** in cache.  If the page previously located at pgno is not already
** in the rollback journal, it is not put there by by this routine.
**
** References to the page pPg remain valid. Updating any
** meta-data associated with pPg (i.e. data stored in the nExtra bytes
** allocated along with the page) is the responsibility of the caller.
**
** A transaction must be active when this routine is called. It used to be
** required that a statement transaction was not active, but this restriction
** has been removed (CREATE INDEX needs to move a page when a statement
** transaction is active).
**
** If the fourth argument, isCommit, is non-zero, then this page is being
** moved as part of a database reorganization just before the transaction 
** is being committed. In this case, it is guaranteed that the database page 
** pPg refers to will not be written to again within this transaction.
**
** This function may return SQLITE_NOMEM or an IO error code if an error
** occurs. Otherwise, it returns SQLITE_OK.
*/
int sqlite3PagerMovepage(Pager *pPager, DbPage *pPg, Pgno pgno, int isCommit){
  PgHdr *pPgOld;               /* The page being overwritten. */
  Pgno needSyncPgno = 0;       /* Old value of pPg->pgno, if sync is required */
  int rc;                      /* Return code */
  Pgno origPgno;               /* The original page number */

  assert( pPg->nRef>0 );
  assert( pPager->eState==PAGER_WRITER_CACHEMOD
       || pPager->eState==PAGER_WRITER_DBMOD
  );
  assert( assert_pager_state(pPager) );

  /* In order to be able to rollback, an in-memory database must journal
  ** the page we are moving from.
  */
  if( MEMDB ){
    rc = sqlite3PagerWrite(pPg);
    if( rc ) return rc;
  }

  /* If the page being moved is dirty and has not been saved by the latest
  ** savepoint, then save the current contents of the page into the 
  ** sub-journal now. This is required to handle the following scenario:
  **
  **   BEGIN;
  **     <journal page X, then modify it in memory>
  **     SAVEPOINT one;
  **       <Move page X to location Y>
  **     ROLLBACK TO one;
  **
  ** If page X were not written to the sub-journal here, it would not
  ** be possible to restore its contents when the "ROLLBACK TO one"
  ** statement were is processed.
  **
  ** subjournalPage() may need to allocate space to store pPg->pgno into
  ** one or more savepoint bitvecs. This is the reason this function
  ** may return SQLITE_NOMEM.
  */
  if( pPg->flags&PGHDR_DIRTY
   && subjRequiresPage(pPg)
   && SQLITE_OK!=(rc = subjournalPage(pPg))
  ){
    return rc;
  }

  PAGERTRACE(("MOVE %d page %d (needSync=%d) moves to %d\n", 
      PAGERID(pPager), pPg->pgno, (pPg->flags&PGHDR_NEED_SYNC)?1:0, pgno));
  IOTRACE(("MOVE %p %d %d\n", pPager, pPg->pgno, pgno))

  /* If the journal needs to be sync()ed before page pPg->pgno can
  ** be written to, store pPg->pgno in local variable needSyncPgno.
  **
  ** If the isCommit flag is set, there is no need to remember that
  ** the journal needs to be sync()ed before database page pPg->pgno 
  ** can be written to. The caller has already promised not to write to it.
  */
  if( (pPg->flags&PGHDR_NEED_SYNC) && !isCommit ){
    needSyncPgno = pPg->pgno;
    assert( pageInJournal(pPg) || pPg->pgno>pPager->dbOrigSize );
    assert( pPg->flags&PGHDR_DIRTY );
  }

  /* If the cache contains a page with page-number pgno, remove it
  ** from its hash chain. Also, if the PGHDR_NEED_SYNC flag was set for 
  ** page pgno before the 'move' operation, it needs to be retained 
  ** for the page moved there.
  */
  pPg->flags &= ~PGHDR_NEED_SYNC;
  pPgOld = pager_lookup(pPager, pgno);
  assert( !pPgOld || pPgOld->nRef==1 );
  if( pPgOld ){
    pPg->flags |= (pPgOld->flags&PGHDR_NEED_SYNC);
    if( MEMDB ){
      /* Do not discard pages from an in-memory database since we might
      ** need to rollback later.  Just move the page out of the way. */
      sqlite3PcacheMove(pPgOld, pPager->dbSize+1);
    }else{
      sqlite3PcacheDrop(pPgOld);
    }
  }

  origPgno = pPg->pgno;
  sqlite3PcacheMove(pPg, pgno);
  sqlite3PcacheMakeDirty(pPg);

  /* For an in-memory database, make sure the original page continues
  ** to exist, in case the transaction needs to roll back.  Use pPgOld
  ** as the original page since it has already been allocated.
  */
  if( MEMDB ){
    assert( pPgOld );
    sqlite3PcacheMove(pPgOld, origPgno);
    sqlite3PagerUnref(pPgOld);
  }

  if( needSyncPgno ){
    /* If needSyncPgno is non-zero, then the journal file needs to be 
    ** sync()ed before any data is written to database file page needSyncPgno.
    ** Currently, no such page exists in the page-cache and the 
    ** "is journaled" bitvec flag has been set. This needs to be remedied by
    ** loading the page into the pager-cache and setting the PGHDR_NEED_SYNC
    ** flag.
    **
    ** If the attempt to load the page into the page-cache fails, (due
    ** to a malloc() or IO failure), clear the bit in the pInJournal[]
    ** array. Otherwise, if the page is loaded and written again in
    ** this transaction, it may be written to the database file before
    ** it is synced into the journal file. This way, it may end up in
    ** the journal file twice, but that is not a problem.
    */
    PgHdr *pPgHdr;
    rc = sqlite3PagerGet(pPager, needSyncPgno, &pPgHdr);
    if( rc!=SQLITE_OK ){
      if( needSyncPgno<=pPager->dbOrigSize ){
        assert( pPager->pTmpSpace!=0 );
        sqlite3BitvecClear(pPager->pInJournal, needSyncPgno, pPager->pTmpSpace);
      }
      return rc;
    }
    pPgHdr->flags |= PGHDR_NEED_SYNC;
    sqlite3PcacheMakeDirty(pPgHdr);
    sqlite3PagerUnref(pPgHdr);
  }

  return SQLITE_OK;
}
#endif

/*
** Return a pointer to the data for the specified page.
*/
void *sqlite3PagerGetData(DbPage *pPg){
  assert( pPg->nRef>0 || pPg->pPager->memDb );
  return pPg->pData;
}

/*
** Return a pointer to the Pager.nExtra bytes of "extra" space 
** allocated along with the specified page.
*/
void *sqlite3PagerGetExtra(DbPage *pPg){
  return pPg->pExtra;
}

/*
** Get/set the locking-mode for this pager. Parameter eMode must be one
** of PAGER_LOCKINGMODE_QUERY, PAGER_LOCKINGMODE_NORMAL or 
** PAGER_LOCKINGMODE_EXCLUSIVE. If the parameter is not _QUERY, then
** the locking-mode is set to the value specified.
**
** The returned value is either PAGER_LOCKINGMODE_NORMAL or
** PAGER_LOCKINGMODE_EXCLUSIVE, indicating the current (possibly updated)
** locking-mode.
*/
int sqlite3PagerLockingMode(Pager *pPager, int eMode){
  assert( eMode==PAGER_LOCKINGMODE_QUERY
            || eMode==PAGER_LOCKINGMODE_NORMAL
            || eMode==PAGER_LOCKINGMODE_EXCLUSIVE );
  assert( PAGER_LOCKINGMODE_QUERY<0 );
  assert( PAGER_LOCKINGMODE_NORMAL>=0 && PAGER_LOCKINGMODE_EXCLUSIVE>=0 );
  assert( pPager->exclusiveMode || 0==sqlite3WalHeapMemory(pPager->pWal) );
  if( eMode>=0 && !pPager->tempFile && !sqlite3WalHeapMemory(pPager->pWal) ){
    pPager->exclusiveMode = (u8)eMode;
  }
  return (int)pPager->exclusiveMode;
}

/*
** Set the journal-mode for this pager. Parameter eMode must be one of:
**
**    PAGER_JOURNALMODE_DELETE
**    PAGER_JOURNALMODE_TRUNCATE
**    PAGER_JOURNALMODE_PERSIST
**    PAGER_JOURNALMODE_OFF
**    PAGER_JOURNALMODE_MEMORY
**    PAGER_JOURNALMODE_WAL
**
** The journalmode is set to the value specified if the change is allowed.
** The change may be disallowed for the following reasons:
**
**   *  An in-memory database can only have its journal_mode set to _OFF
**      or _MEMORY.
**
**   *  Temporary databases cannot have _WAL journalmode.
**
** The returned indicate the current (possibly updated) journal-mode.
*/
int sqlite3PagerSetJournalMode(Pager *pPager, int eMode){
  u8 eOld = pPager->journalMode;    /* Prior journalmode */

#ifdef SQLITE_DEBUG
  /* The print_pager_state() routine is intended to be used by the debugger
  ** only.  We invoke it once here to suppress a compiler warning. */
  print_pager_state(pPager);
#endif


  /* The eMode parameter is always valid */
  assert(      eMode==PAGER_JOURNALMODE_DELETE
            || eMode==PAGER_JOURNALMODE_TRUNCATE
            || eMode==PAGER_JOURNALMODE_PERSIST
            || eMode==PAGER_JOURNALMODE_OFF 
            || eMode==PAGER_JOURNALMODE_WAL 
            || eMode==PAGER_JOURNALMODE_MEMORY );

  /* This routine is only called from the OP_JournalMode opcode, and
  ** the logic there will never allow a temporary file to be changed
  ** to WAL mode.
  */
  assert( pPager->tempFile==0 || eMode!=PAGER_JOURNALMODE_WAL );

  /* Do allow the journalmode of an in-memory database to be set to
  ** anything other than MEMORY or OFF
  */
  if( MEMDB ){
    assert( eOld==PAGER_JOURNALMODE_MEMORY || eOld==PAGER_JOURNALMODE_OFF );
    if( eMode!=PAGER_JOURNALMODE_MEMORY && eMode!=PAGER_JOURNALMODE_OFF ){
      eMode = eOld;
    }
  }

  if( eMode!=eOld ){

    /* Change the journal mode. */
    assert( pPager->eState!=PAGER_ERROR );
    pPager->journalMode = (u8)eMode;

    /* When transistioning from TRUNCATE or PERSIST to any other journal
    ** mode except WAL, unless the pager is in locking_mode=exclusive mode,
    ** delete the journal file.
    */
    assert( (PAGER_JOURNALMODE_TRUNCATE & 5)==1 );
    assert( (PAGER_JOURNALMODE_PERSIST & 5)==1 );
    assert( (PAGER_JOURNALMODE_DELETE & 5)==0 );
    assert( (PAGER_JOURNALMODE_MEMORY & 5)==4 );
    assert( (PAGER_JOURNALMODE_OFF & 5)==0 );
    assert( (PAGER_JOURNALMODE_WAL & 5)==5 );

    assert( isOpen(pPager->fd) || pPager->exclusiveMode );
    if( !pPager->exclusiveMode && (eOld & 5)==1 && (eMode & 1)==0 ){

      /* In this case we would like to delete the journal file. If it is
      ** not possible, then that is not a problem. Deleting the journal file
      ** here is an optimization only.
      **
      ** Before deleting the journal file, obtain a RESERVED lock on the
      ** database file. This ensures that the journal file is not deleted
      ** while it is in use by some other client.
      */
      sqlite3OsClose(pPager->jfd);
      if( pPager->eLock>=RESERVED_LOCK ){
        sqlite3OsDelete(pPager->pVfs, pPager->zJournal, 0);
      }else{
        int rc = SQLITE_OK;
        int state = pPager->eState;
        assert( state==PAGER_OPEN || state==PAGER_READER );
        if( state==PAGER_OPEN ){
          rc = sqlite3PagerSharedLock(pPager);
        }
        if( pPager->eState==PAGER_READER ){
          assert( rc==SQLITE_OK );
          rc = pagerLockDb(pPager, RESERVED_LOCK);
        }
        if( rc==SQLITE_OK ){
          sqlite3OsDelete(pPager->pVfs, pPager->zJournal, 0);
        }
        if( rc==SQLITE_OK && state==PAGER_READER ){
          pagerUnlockDb(pPager, SHARED_LOCK);
        }else if( state==PAGER_OPEN ){
          pager_unlock(pPager);
        }
        assert( state==pPager->eState );
      }
    }
  }

  /* Return the new journal mode */
  return (int)pPager->journalMode;
}

/*
** Return the current journal mode.
*/
int sqlite3PagerGetJournalMode(Pager *pPager){
  return (int)pPager->journalMode;
}

/*
** Return TRUE if the pager is in a state where it is OK to change the
** journalmode.  Journalmode changes can only happen when the database
** is unmodified.
*/
int sqlite3PagerOkToChangeJournalMode(Pager *pPager){
  assert( assert_pager_state(pPager) );
  if( pPager->eState>=PAGER_WRITER_CACHEMOD ) return 0;
  if( NEVER(isOpen(pPager->jfd) && pPager->journalOff>0) ) return 0;
  return 1;
}

/*
** Get/set the size-limit used for persistent journal files.
**
** Setting the size limit to -1 means no limit is enforced.
** An attempt to set a limit smaller than -1 is a no-op.
*/
i64 sqlite3PagerJournalSizeLimit(Pager *pPager, i64 iLimit){
  if( iLimit>=-1 ){
    pPager->journalSizeLimit = iLimit;
    sqlite3WalLimit(pPager->pWal, iLimit);
  }
  return pPager->journalSizeLimit;
}

/*
** Return a pointer to the pPager->pBackup variable. The backup module
** in backup.c maintains the content of this variable. This module
** uses it opaquely as an argument to sqlite3BackupRestart() and
** sqlite3BackupUpdate() only.
*/
sqlite3_backup **sqlite3PagerBackupPtr(Pager *pPager){
  return &pPager->pBackup;
}

#ifndef SQLITE_OMIT_VACUUM
/*
** Unless this is an in-memory or temporary database, clear the pager cache.
*/
void sqlite3PagerClearCache(Pager *pPager){
  if( !MEMDB && pPager->tempFile==0 ) pager_reset(pPager);
}
#endif

#ifndef SQLITE_OMIT_WAL
/*
** This function is called when the user invokes "PRAGMA wal_checkpoint",
** "PRAGMA wal_blocking_checkpoint" or calls the sqlite3_wal_checkpoint()
** or wal_blocking_checkpoint() API functions.
**
** Parameter eMode is one of SQLITE_CHECKPOINT_PASSIVE, FULL or RESTART.
*/
int sqlite3PagerCheckpoint(Pager *pPager, int eMode, int *pnLog, int *pnCkpt){
  int rc = SQLITE_OK;
  if( pPager->pWal ){
    rc = sqlite3WalCheckpoint(pPager->pWal, eMode,
        pPager->xBusyHandler, pPager->pBusyHandlerArg,
        pPager->ckptSyncFlags, pPager->pageSize, (u8 *)pPager->pTmpSpace,
        pnLog, pnCkpt
    );
  }
  return rc;
}

int sqlite3PagerWalCallback(Pager *pPager){
  return sqlite3WalCallback(pPager->pWal);
}

/*
** Return true if the underlying VFS for the given pager supports the
** primitives necessary for write-ahead logging.
*/
int sqlite3PagerWalSupported(Pager *pPager){
  const sqlite3_io_methods *pMethods = pPager->fd->pMethods;
  return pPager->exclusiveMode || (pMethods->iVersion>=2 && pMethods->xShmMap);
}

/*
** Attempt to take an exclusive lock on the database file. If a PENDING lock
** is obtained instead, immediately release it.
*/
static int pagerExclusiveLock(Pager *pPager){
  int rc;                         /* Return code */

  assert( pPager->eLock==SHARED_LOCK || pPager->eLock==EXCLUSIVE_LOCK );
  rc = pagerLockDb(pPager, EXCLUSIVE_LOCK);
  if( rc!=SQLITE_OK ){
    /* If the attempt to grab the exclusive lock failed, release the 
    ** pending lock that may have been obtained instead.  */
    pagerUnlockDb(pPager, SHARED_LOCK);
  }

  return rc;
}

/*
** Call sqlite3WalOpen() to open the WAL handle. If the pager is in 
** exclusive-locking mode when this function is called, take an EXCLUSIVE
** lock on the database file and use heap-memory to store the wal-index
** in. Otherwise, use the normal shared-memory.
*/
static int pagerOpenWal(Pager *pPager){
  int rc = SQLITE_OK;

  assert( pPager->pWal==0 && pPager->tempFile==0 );
  assert( pPager->eLock==SHARED_LOCK || pPager->eLock==EXCLUSIVE_LOCK );

  /* If the pager is already in exclusive-mode, the WAL module will use 
  ** heap-memory for the wal-index instead of the VFS shared-memory 
  ** implementation. Take the exclusive lock now, before opening the WAL
  ** file, to make sure this is safe.
  */
  if( pPager->exclusiveMode ){
    rc = pagerExclusiveLock(pPager);
  }

  /* Open the connection to the log file. If this operation fails, 
  ** (e.g. due to malloc() failure), return an error code.
  */
  if( rc==SQLITE_OK ){
    rc = sqlite3WalOpen(pPager->pVfs, 
        pPager->fd, pPager->zWal, pPager->exclusiveMode,
        pPager->journalSizeLimit, &pPager->pWal
    );
  }

  return rc;
}


/*
** The caller must be holding a SHARED lock on the database file to call
** this function.
**
** If the pager passed as the first argument is open on a real database
** file (not a temp file or an in-memory database), and the WAL file
** is not already open, make an attempt to open it now. If successful,
** return SQLITE_OK. If an error occurs or the VFS used by the pager does 
** not support the xShmXXX() methods, return an error code. *pbOpen is
** not modified in either case.
**
** If the pager is open on a temp-file (or in-memory database), or if
** the WAL file is already open, set *pbOpen to 1 and return SQLITE_OK
** without doing anything.
*/
int sqlite3PagerOpenWal(
  Pager *pPager,                  /* Pager object */
  int *pbOpen                     /* OUT: Set to true if call is a no-op */
){
  int rc = SQLITE_OK;             /* Return code */

  assert( assert_pager_state(pPager) );
  assert( pPager->eState==PAGER_OPEN   || pbOpen );
  assert( pPager->eState==PAGER_READER || !pbOpen );
  assert( pbOpen==0 || *pbOpen==0 );
  assert( pbOpen!=0 || (!pPager->tempFile && !pPager->pWal) );

  if( !pPager->tempFile && !pPager->pWal ){
    if( !sqlite3PagerWalSupported(pPager) ) return SQLITE_CANTOPEN;

    /* Close any rollback journal previously open */
    sqlite3OsClose(pPager->jfd);

    rc = pagerOpenWal(pPager);
    if( rc==SQLITE_OK ){
      pPager->journalMode = PAGER_JOURNALMODE_WAL;
      pPager->eState = PAGER_OPEN;
    }
  }else{
    *pbOpen = 1;
  }

  return rc;
}

/*
** This function is called to close the connection to the log file prior
** to switching from WAL to rollback mode.
**
** Before closing the log file, this function attempts to take an 
** EXCLUSIVE lock on the database file. If this cannot be obtained, an
** error (SQLITE_BUSY) is returned and the log connection is not closed.
** If successful, the EXCLUSIVE lock is not released before returning.
*/
int sqlite3PagerCloseWal(Pager *pPager){
  int rc = SQLITE_OK;

  assert( pPager->journalMode==PAGER_JOURNALMODE_WAL );

  /* If the log file is not already open, but does exist in the file-system,
  ** it may need to be checkpointed before the connection can switch to
  ** rollback mode. Open it now so this can happen.
  */
  if( !pPager->pWal ){
    int logexists = 0;
    rc = pagerLockDb(pPager, SHARED_LOCK);
    if( rc==SQLITE_OK ){
      rc = sqlite3OsAccess(
          pPager->pVfs, pPager->zWal, SQLITE_ACCESS_EXISTS, &logexists
      );
    }
    if( rc==SQLITE_OK && logexists ){
      rc = pagerOpenWal(pPager);
    }
  }
    
  /* Checkpoint and close the log. Because an EXCLUSIVE lock is held on
  ** the database file, the log and log-summary files will be deleted.
  */
  if( rc==SQLITE_OK && pPager->pWal ){
    rc = pagerExclusiveLock(pPager);
    if( rc==SQLITE_OK ){
      rc = sqlite3WalClose(pPager->pWal, pPager->ckptSyncFlags,
                           pPager->pageSize, (u8*)pPager->pTmpSpace);
      pPager->pWal = 0;
    }
  }
  return rc;
}

#ifdef SQLITE_ENABLE_ZIPVFS
/*
** A read-lock must be held on the pager when this function is called. If
** the pager is in WAL mode and the WAL file currently contains one or more
** frames, return the size in bytes of the page images stored within the
** WAL frames. Otherwise, if this is not a WAL database or the WAL file
** is empty, return 0.
*/
int sqlite3PagerWalFramesize(Pager *pPager){
  assert( pPager->eState==PAGER_READER );
  return sqlite3WalFramesize(pPager->pWal);
}
#endif

#ifdef SQLITE_HAS_CODEC
/*
** This function is called by the wal module when writing page content
** into the log file.
**
** This function returns a pointer to a buffer containing the encrypted
** page content. If a malloc fails, this function may return NULL.
*/
void *sqlite3PagerCodec(PgHdr *pPg){
  void *aData = 0;
  CODEC2(pPg->pPager, pPg->pData, pPg->pgno, 6, return 0, aData);
  return aData;
}
#endif /* SQLITE_HAS_CODEC */

#endif /* !SQLITE_OMIT_WAL */

#endif /* SQLITE_OMIT_DISKIO */
