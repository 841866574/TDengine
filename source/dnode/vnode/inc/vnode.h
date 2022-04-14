/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TD_VNODE_H_
#define _TD_VNODE_H_

#include "os.h"
#include "tmsgcb.h"
#include "tqueue.h"
#include "trpc.h"

#include "tarray.h"
#include "tfs.h"
#include "wal.h"

#include "tcommon.h"
#include "tfs.h"
#include "tmallocator.h"
#include "tmsg.h"
#include "trow.h"

#ifdef __cplusplus
extern "C" {
#endif

// vnode
typedef struct SVnode    SVnode;
typedef struct SMetaCfg  SMetaCfg;  // todo: remove
typedef struct STsdbCfg  STsdbCfg;  // todo: remove
typedef struct STqCfg    STqCfg;    // todo: remove
typedef struct SVnodeCfg SVnodeCfg;

int     vnodeInit();
void    vnodeCleanup();
SVnode *vnodeOpen(const char *path, const SVnodeCfg *pVnodeCfg);
void    vnodeClose(SVnode *pVnode);
void    vnodeDestroy(const char *path);
void    vnodeProcessWMsgs(SVnode *pVnode, SArray *pMsgs);
int     vnodeApplyWMsg(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp);
int     vnodeProcessCMsg(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp);
int     vnodeProcessSyncReq(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp);
int     vnodeProcessQueryMsg(SVnode *pVnode, SRpcMsg *pMsg);
int     vnodeProcessFetchMsg(SVnode *pVnode, SRpcMsg *pMsg, SQueueInfo *pInfo);
int32_t vnodeAlter(SVnode *pVnode, const SVnodeCfg *pCfg);
int32_t vnodeCompact(SVnode *pVnode);
int32_t vnodeSync(SVnode *pVnode);
int32_t vnodeGetLoad(SVnode *pVnode, SVnodeLoad *pLoad);
int     vnodeValidateTableHash(SVnodeCfg *pVnodeOptions, char *tableFName);

// meta
typedef struct SMeta       SMeta;        // todo: remove
typedef struct SMTbCursor  SMTbCursor;   // todo: remove
typedef struct SMCtbCursor SMCtbCursor;  // todo: remove
typedef struct SMSmaCursor SMSmaCursor;  // todo: remove

#define META_SUPER_TABLE  TD_SUPER_TABLE
#define META_CHILD_TABLE  TD_CHILD_TABLE
#define META_NORMAL_TABLE TD_NORMAL_TABLE

typedef SVCreateTbReq   STbCfg;
typedef SVCreateTSmaReq SSmaCfg;

SSchemaWrapper *metaGetTableSchema(SMeta *pMeta, tb_uid_t uid, int32_t sver, bool isinline);
STSchema       *metaGetTbTSchema(SMeta *pMeta, tb_uid_t uid, int32_t sver);
void           *metaGetSmaInfoByIndex(SMeta *pMeta, int64_t indexUid, bool isDecode);
STSmaWrapper   *metaGetSmaInfoByTable(SMeta *pMeta, tb_uid_t uid);
SArray         *metaGetSmaTbUids(SMeta *pMeta, bool isDup);
int             metaGetTbNum(SMeta *pMeta);
SMTbCursor     *metaOpenTbCursor(SMeta *pMeta);
void            metaCloseTbCursor(SMTbCursor *pTbCur);
char           *metaTbCursorNext(SMTbCursor *pTbCur);
SMCtbCursor    *metaOpenCtbCursor(SMeta *pMeta, tb_uid_t uid);
void            metaCloseCtbCurosr(SMCtbCursor *pCtbCur);
tb_uid_t        metaCtbCursorNext(SMCtbCursor *pCtbCur);

SMSmaCursor *metaOpenSmaCursor(SMeta *pMeta, tb_uid_t uid);
void         metaCloseSmaCursor(SMSmaCursor *pSmaCur);
int64_t      metaSmaCursorNext(SMSmaCursor *pSmaCur);

// tsdb
typedef struct STsdb          STsdb;
typedef struct SDataStatis    SDataStatis;
typedef struct STsdbQueryCond STsdbQueryCond;
typedef void                 *tsdbReaderT;

#define BLOCK_LOAD_OFFSET_SEQ_ORDER 1
#define BLOCK_LOAD_TABLE_SEQ_ORDER  2
#define BLOCK_LOAD_TABLE_RR_ORDER   3
#define TABLE_TID(t)                (t)->tid
#define TABLE_UID(t)                (t)->uid
STsdb  *tsdbOpen(const char *path, int32_t vgId, const STsdbCfg *pTsdbCfg, SMemAllocatorFactory *pMAF, SMeta *pMeta,
                 STfs *pTfs);
void    tsdbClose(STsdb *);
void    tsdbRemove(const char *path);
int     tsdbInsertData(STsdb *pTsdb, SSubmitReq *pMsg, SSubmitRsp *pRsp);
int     tsdbPrepareCommit(STsdb *pTsdb);
int     tsdbCommit(STsdb *pTsdb);
int32_t tsdbInitSma(STsdb *pTsdb);
int32_t tsdbCreateTSma(STsdb *pTsdb, char *pMsg);
int32_t tsdbDropTSma(STsdb *pTsdb, char *pMsg);
tsdbReaderT *tsdbQueryTables(STsdb *tsdb, STsdbQueryCond *pCond, STableGroupInfo *tableInfoGroup, uint64_t qId,
                             uint64_t taskId);
tsdbReaderT  tsdbQueryCacheLast(STsdb *tsdb, STsdbQueryCond *pCond, STableGroupInfo *groupList, uint64_t qId,
                                void *pMemRef);
int32_t      tsdbGetFileBlocksDistInfo(tsdbReaderT *pReader, STableBlockDistInfo *pTableBlockInfo);
bool         isTsdbCacheLastRow(tsdbReaderT *pReader);
int32_t      tsdbQuerySTableByTagCond(void *pMeta, uint64_t uid, TSKEY skey, const char *pTagCond, size_t len,
                                      int16_t tagNameRelType, const char *tbnameCond, STableGroupInfo *pGroupInfo,
                                      SColIndex *pColIndex, int32_t numOfCols, uint64_t reqId, uint64_t taskId);
int64_t      tsdbGetNumOfRowsInMemTable(tsdbReaderT *pHandle);
bool         tsdbNextDataBlock(tsdbReaderT pTsdbReadHandle);
void         tsdbRetrieveDataBlockInfo(tsdbReaderT *pTsdbReadHandle, SDataBlockInfo *pBlockInfo);
int32_t      tsdbRetrieveDataBlockStatisInfo(tsdbReaderT *pTsdbReadHandle, SDataStatis **pBlockStatis);
SArray      *tsdbRetrieveDataBlock(tsdbReaderT *pTsdbReadHandle, SArray *pColumnIdList);
void         tsdbDestroyTableGroup(STableGroupInfo *pGroupList);
int32_t      tsdbGetOneTableGroup(void *pMeta, uint64_t uid, TSKEY startKey, STableGroupInfo *pGroupInfo);
int32_t      tsdbGetTableGroupFromIdList(STsdb *tsdb, SArray *pTableIdList, STableGroupInfo *pGroupInfo);
void         tsdbCleanupReadHandle(tsdbReaderT queryHandle);
int32_t      tsdbUpdateSmaWindow(STsdb *pTsdb, SSubmitReq *pMsg, int64_t version);
int32_t      tsdbInsertTSmaData(STsdb *pTsdb, int64_t indexUid, const char *msg);
int32_t      tsdbDropTSmaData(STsdb *pTsdb, int64_t indexUid);
int32_t      tsdbInsertRSmaData(STsdb *pTsdb, char *msg);

// tq
enum {
  TQ_STREAM_TOKEN__DATA = 1,
  TQ_STREAM_TOKEN__WATERMARK,
  TQ_STREAM_TOKEN__CHECKPOINT,
};

typedef struct STqReadHandle STqReadHandle;

STqReadHandle *tqInitSubmitMsgScanner(SMeta *pMeta);

void    tqReadHandleSetColIdList(STqReadHandle *pReadHandle, SArray *pColIdList);
int     tqReadHandleSetTbUidList(STqReadHandle *pHandle, const SArray *tbUidList);
int     tqReadHandleAddTbUidList(STqReadHandle *pHandle, const SArray *tbUidList);
int32_t tqReadHandleSetMsg(STqReadHandle *pHandle, SSubmitReq *pMsg, int64_t ver);
bool    tqNextDataBlock(STqReadHandle *pHandle);
int     tqRetrieveDataBlockInfo(STqReadHandle *pHandle, SDataBlockInfo *pBlockInfo);
SArray *tqRetrieveDataBlock(STqReadHandle *pHandle);

// need to reposition
typedef struct SMgmtWrapper SMgmtWrapper;

int32_t tdScanAndConvertSubmitMsg(SSubmitReq *pMsg);

// structs
struct SMetaCfg {
  uint64_t lruSize;
};

struct STsdbCfg {
  int8_t   precision;
  int8_t   update;
  int8_t   compression;
  int32_t  daysPerFile;
  int32_t  minRowsPerFileBlock;
  int32_t  maxRowsPerFileBlock;
  int32_t  keep;
  int32_t  keep1;
  int32_t  keep2;
  uint64_t lruCacheSize;
  SArray  *retentions;
};

struct STqCfg {
  int32_t reserved;
};

struct SVnodeCfg {
  int32_t  vgId;
  uint64_t dbId;
  STfs    *pTfs;
  uint64_t wsize;
  uint64_t ssize;
  uint64_t lsize;
  bool     isHeapAllocator;
  uint32_t ttl;
  uint32_t keep;
  int8_t   streamMode;
  bool     isWeak;
  STsdbCfg tsdbCfg;
  SMetaCfg metaCfg;
  STqCfg   tqCfg;
  SWalCfg  walCfg;
  SMsgCb   msgCb;
  uint32_t hashBegin;
  uint32_t hashEnd;
  int8_t   hashMethod;
};

struct STqReadHandle {
  int64_t           ver;
  int64_t           tbUid;
  SHashObj         *tbIdHash;
  const SSubmitReq *pMsg;
  SSubmitBlk       *pBlock;
  SSubmitMsgIter    msgIter;
  SSubmitBlkIter    blkIter;
  SMeta            *pVnodeMeta;
  SArray           *pColIdList;  // SArray<int32_t>
  int32_t           sver;
  SSchemaWrapper   *pSchemaWrapper;
  STSchema         *pSchema;
};

struct SDataStatis {
  int16_t colId;
  int16_t maxIndex;
  int16_t minIndex;
  int16_t numOfNull;
  int64_t sum;
  int64_t max;
  int64_t min;
};

struct STsdbQueryCond {
  STimeWindow  twindow;
  int32_t      order;  // desc|asc order to iterate the data block
  int32_t      numOfCols;
  SColumnInfo *colList;
  bool         loadExternalRows;  // load external rows or not
  int32_t      type;              // data block load type:
};

typedef struct {
  TSKEY    lastKey;
  uint64_t uid;
} STableKeyInfo;

typedef struct STable {
  uint64_t  tid;
  uint64_t  uid;
  STSchema *pSchema;
} STable;

typedef struct {
  int8_t type;
  int8_t reserved[7];
  union {
    void   *data;
    int64_t wmTs;
    int64_t checkpointId;
  };
} STqStreamToken;

#ifdef __cplusplus
}
#endif

#endif /*_TD_VNODE_H_*/
