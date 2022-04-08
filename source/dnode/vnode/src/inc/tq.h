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

#ifndef _TD_VNODE_TQ_H_
#define _TD_VNODE_TQ_H_

#ifdef __cplusplus
extern "C" {
#endif

// tqInt.h
// clang-format off
#define tqFatal(...) do { if (tqDebugFlag & DEBUG_FATAL) { taosPrintLog("TQ FATAL ", DEBUG_FATAL, 255, __VA_ARGS__); }}     while(0)
#define tqError(...) do { if (tqDebugFlag & DEBUG_ERROR) { taosPrintLog("TQ ERROR ", DEBUG_ERROR, 255, __VA_ARGS__); }}     while(0)
#define tqWarn(...)  do { if (tqDebugFlag & DEBUG_WARN)  { taosPrintLog("TQ WARN ", DEBUG_WARN, 255, __VA_ARGS__); }}       while(0)
#define tqInfo(...)  do { if (tqDebugFlag & DEBUG_INFO)  { taosPrintLog("TQ ", DEBUG_INFO, 255, __VA_ARGS__); }}            while(0)
#define tqDebug(...) do { if (tqDebugFlag & DEBUG_DEBUG) { taosPrintLog("TQ ", DEBUG_DEBUG, tqDebugFlag, __VA_ARGS__); }} while(0)
#define tqTrace(...) do { if (tqDebugFlag & DEBUG_TRACE) { taosPrintLog("TQ ", DEBUG_TRACE, tqDebugFlag, __VA_ARGS__); }} while(0)
// clang-format on

int  tqInit();
void tqCleanUp();

// open in each vnode
STQ* tqOpen(const char* path, SVnode* pVnode, SWal* pWal, SMeta* pMeta, STqCfg* tqConfig,
            SMemAllocatorFactory* allocFac);
void tqClose(STQ*);

// required by vnode
int tqPushMsg(STQ*, void* msg, int32_t msgLen, tmsg_t msgType, int64_t version);
int tqCommit(STQ*);

int32_t tqProcessPollReq(STQ* pTq, SRpcMsg* pMsg, int32_t workerId);
int32_t tqProcessSetConnReq(STQ* pTq, char* msg);
int32_t tqProcessRebReq(STQ* pTq, char* msg);
int32_t tqProcessCancelConnReq(STQ* pTq, char* msg);
int32_t tqProcessTaskExec(STQ* pTq, char* msg, int32_t msgLen, int32_t workerId);
int32_t tqProcessTaskDeploy(STQ* pTq, char* msg, int32_t msgLen);
int32_t tqProcessStreamTrigger(STQ* pTq, void* data, int32_t dataLen, int32_t workerId);

#define TQ_BUFFER_SIZE 4

#define TQ_BUCKET_MASK 0xFF
#define TQ_BUCKET_SIZE 256

#define TQ_PAGE_SIZE 4096
// key + offset + size
#define TQ_IDX_SIZE 24
// 4096 / 24
#define TQ_MAX_IDX_ONE_PAGE 170
// 24 * 170
#define TQ_IDX_PAGE_BODY_SIZE 4080
// 4096 - 4080
#define TQ_IDX_PAGE_HEAD_SIZE 16

#define TQ_ACTION_CONST      0
#define TQ_ACTION_INUSE      1
#define TQ_ACTION_INUSE_CONT 2
#define TQ_ACTION_INTXN      3

#define TQ_SVER 0

// TODO: inplace mode is not implemented
#define TQ_UPDATE_INPLACE 0
#define TQ_UPDATE_APPEND  1

#define TQ_DUP_INTXN_REWRITE 0
#define TQ_DUP_INTXN_REJECT  2

static inline bool tqUpdateAppend(int32_t tqConfigFlag) { return tqConfigFlag & TQ_UPDATE_APPEND; }

static inline bool tqDupIntxnReject(int32_t tqConfigFlag) { return tqConfigFlag & TQ_DUP_INTXN_REJECT; }

static const int8_t TQ_CONST_DELETE = TQ_ACTION_CONST;

#define TQ_DELETE_TOKEN (void*)&TQ_CONST_DELETE

typedef enum { TQ_ITEM_READY, TQ_ITEM_PROCESS, TQ_ITEM_EMPTY } STqItemStatus;

typedef struct STqOffsetCfg   STqOffsetCfg;
typedef struct STqOffsetStore STqOffsetStore;

typedef struct {
  int16_t ver;
  int16_t action;
  int32_t checksum;
  int64_t ssize;
  char    content[];
} STqSerializedHead;

typedef int32_t (*FTqSerialize)(const void* pObj, STqSerializedHead** ppHead);
typedef int32_t (*FTqDeserialize)(void* self, const STqSerializedHead* pHead, void** ppObj);
typedef void (*FTqDelete)(void*);

typedef struct {
  int64_t key;
  int64_t offset;
  int64_t serializedSize;
  void*   valueInUse;
  void*   valueInTxn;
} STqMetaHandle;

typedef struct STqMetaList {
  STqMetaHandle       handle;
  struct STqMetaList* next;
  // struct STqMetaList* inTxnPrev;
  // struct STqMetaList* inTxnNext;
  struct STqMetaList* unpersistPrev;
  struct STqMetaList* unpersistNext;
} STqMetaList;

typedef struct {
  STQ*         pTq;
  STqMetaList* bucket[TQ_BUCKET_SIZE];
  // a table head
  STqMetaList* unpersistHead;
  // topics that are not connectted
  STqMetaList* unconnectTopic;

  TdFilePtr pFile;
  TdFilePtr pIdxFile;

  char*          dirPath;
  int32_t        tqConfigFlag;
  FTqSerialize   pSerializer;
  FTqDeserialize pDeserializer;
  FTqDelete      pDeleter;
} STqMetaStore;

typedef struct {
  SMemAllocatorFactory* pAllocatorFactory;
  SMemAllocator*        pAllocator;
} STqMemRef;

struct STQ {
  // the collection of groups
  // the handle of meta kvstore
  bool          writeTrigger;
  char*         path;
  STqCfg*       tqConfig;
  STqMemRef     tqMemRef;
  STqMetaStore* tqMeta;
  // STqPushMgr*   tqPushMgr;
  SHashObj* pStreamTasks;
  SVnode*   pVnode;
  SWal*     pWal;
  SMeta*    pVnodeMeta;
};

typedef struct {
  int8_t inited;
  tmr_h  timer;
} STqMgmt;

static STqMgmt tqMgmt;

typedef struct {
  int8_t         status;
  int64_t        offset;
  qTaskInfo_t    task;
  STqReadHandle* pReadHandle;
} STqTaskItem;

// new version
typedef struct {
  int64_t     firstOffset;
  int64_t     lastOffset;
  STqTaskItem output[TQ_BUFFER_SIZE];
} STqBuffer;

typedef struct {
  char            topicName[TSDB_TOPIC_FNAME_LEN];
  char*           sql;
  char*           logicalPlan;
  char*           physicalPlan;
  char*           qmsg;
  STqBuffer       buffer;
  SWalReadHandle* pReadhandle;
} STqTopic;

typedef struct {
  int64_t consumerId;
  int32_t epoch;
  char    cgroup[TSDB_TOPIC_FNAME_LEN];
  SArray* topics;  // SArray<STqTopic>
} STqConsumer;

enum {
  TQ_PUSHER_TYPE__CLIENT = 1,
  TQ_PUSHER_TYPE__STREAM,
};

typedef struct {
  int8_t   type;
  int8_t   reserved[3];
  int32_t  ttl;
  int64_t  consumerId;
  SRpcMsg* pMsg;
  // SMqPollRsp* rsp;
} STqClientPusher;

typedef struct {
  int8_t      type;
  int8_t      nodeType;
  int8_t      reserved[6];
  int64_t     streamId;
  qTaskInfo_t task;
  // TODO sync function
} STqStreamPusher;

typedef struct {
  int8_t type;  // mq or stream
} STqPusher;

typedef struct {
  SHashObj* pHash;  // <id, STqPush*>
} STqPushMgr;

typedef struct {
  int8_t inited;
  tmr_h  timer;
} STqPushMgmt;

static STqPushMgmt tqPushMgmt;

int32_t tqSerializeConsumer(const STqConsumer*, STqSerializedHead**);
int32_t tqDeserializeConsumer(STQ*, const STqSerializedHead*, STqConsumer**);

static int FORCE_INLINE tqQueryExecuting(int32_t status) { return status; }

// tqMetaStore.h
STqMetaStore* tqStoreOpen(STQ* pTq, const char* path, FTqSerialize pSerializer, FTqDeserialize pDeserializer,
                          FTqDelete pDeleter, int32_t tqConfigFlag);
int32_t       tqStoreClose(STqMetaStore*);
// int32_t       tqStoreDelete(TqMetaStore*);
// int32_t       tqStoreCommitAll(TqMetaStore*);
int32_t tqStorePersist(STqMetaStore*);
// clean deleted idx and data from persistent file
int32_t tqStoreCompact(STqMetaStore*);

void* tqHandleGet(STqMetaStore*, int64_t key);
// make it unpersist
void*   tqHandleTouchGet(STqMetaStore*, int64_t key);
int32_t tqHandleMovePut(STqMetaStore*, int64_t key, void* value);
int32_t tqHandleCopyPut(STqMetaStore*, int64_t key, void* value, size_t vsize);
// delete committed kv pair
// notice that a delete action still needs to be committed
int32_t tqHandleDel(STqMetaStore*, int64_t key);
int32_t tqHandlePurge(STqMetaStore*, int64_t key);
int32_t tqHandleCommit(STqMetaStore*, int64_t key);
int32_t tqHandleAbort(STqMetaStore*, int64_t key);

// tqOffset
STqOffsetStore* STqOffsetOpen(STqOffsetCfg*);
void            STqOffsetClose(STqOffsetStore*);

int64_t tqOffsetFetch(STqOffsetStore* pStore, const char* subscribeKey);
int32_t tqOffsetCommit(STqOffsetStore* pStore, const char* subscribeKey, int64_t offset);
int32_t tqOffsetPersist(STqOffsetStore* pStore, const char* subscribeKey);
int32_t tqOffsetPersistAll(STqOffsetStore* pStore);

// tqPush
int32_t tqPushMgrInit();
void    tqPushMgrCleanUp();

STqPushMgr* tqPushMgrOpen();
void        tqPushMgrClose(STqPushMgr* pushMgr);

STqClientPusher* tqAddClientPusher(STqPushMgr* pushMgr, SRpcMsg* pMsg, int64_t consumerId, int64_t ttl);
STqStreamPusher* tqAddStreamPusher(STqPushMgr* pushMgr, int64_t streamId, SEpSet* pEpSet);

#ifdef __cplusplus
}
#endif

#endif /*_TD_VNODE_TQ_H_*/