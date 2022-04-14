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

#include "vnodeInt.h"

int32_t tqInit() { return tqPushMgrInit(); }

void tqCleanUp() { tqPushMgrCleanUp(); }

STQ* tqOpen(const char* path, SVnode* pVnode, SWal* pWal, SMeta* pVnodeMeta, STqCfg* tqConfig,
            SMemAllocatorFactory* allocFac) {
  STQ* pTq = taosMemoryMalloc(sizeof(STQ));
  if (pTq == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return NULL;
  }
  pTq->path = strdup(path);
  pTq->tqConfig = tqConfig;
  pTq->pVnode = pVnode;
  pTq->pWal = pWal;
  pTq->pVnodeMeta = pVnodeMeta;
#if 0
  pTq->tqMemRef.pAllocatorFactory = allocFac;
  pTq->tqMemRef.pAllocator = allocFac->create(allocFac);
  if (pTq->tqMemRef.pAllocator == NULL) {
    // TODO: error code of buffer pool
  }
#endif
  pTq->tqMeta = tqStoreOpen(pTq, path, (FTqSerialize)tqSerializeConsumer, (FTqDeserialize)tqDeserializeConsumer,
                            (FTqDelete)taosMemoryFree, 0);
  if (pTq->tqMeta == NULL) {
    taosMemoryFree(pTq);
#if 0
    allocFac->destroy(allocFac, pTq->tqMemRef.pAllocator);
#endif
    return NULL;
  }

#if 0
  pTq->tqPushMgr = tqPushMgrOpen();
  if (pTq->tqPushMgr == NULL) {
    // free store
    taosMemoryFree(pTq);
    return NULL;
  }
#endif

  pTq->pStreamTasks = taosHashInit(64, taosGetDefaultHashFunction(TSDB_DATA_TYPE_INT), true, HASH_NO_LOCK);

  return pTq;
}

void tqClose(STQ* pTq) {
  if (pTq) {
    taosMemoryFreeClear(pTq->path);
    taosMemoryFree(pTq);
  }
  // TODO
}

int tqPushMsg(STQ* pTq, void* msg, int32_t msgLen, tmsg_t msgType, int64_t version) {
  if (msgType != TDMT_VND_SUBMIT) return 0;
  void* data = taosMemoryMalloc(msgLen);
  if (data == NULL) {
    return -1;
  }
  memcpy(data, msg, msgLen);

  if (msgType == TDMT_VND_SUBMIT) {
    if (tsdbUpdateSmaWindow(pTq->pVnode->pTsdb, msg, version) != 0) {
      return -1;
    }
  }

  SRpcMsg req = {
      .msgType = TDMT_VND_STREAM_TRIGGER,
      .pCont = data,
      .contLen = msgLen,
  };
  tmsgPutToQueue(&pTq->pVnode->msgCb, FETCH_QUEUE, &req);

#if 0
  void* pIter = taosHashIterate(pTq->tqPushMgr->pHash, NULL);
  while (pIter != NULL) {
    STqPusher* pusher = *(STqPusher**)pIter;
    if (pusher->type == TQ_PUSHER_TYPE__STREAM) {
      STqStreamPusher* streamPusher = (STqStreamPusher*)pusher;
      // repack
      STqStreamToken* token = taosMemoryMalloc(sizeof(STqStreamToken));
      if (token == NULL) {
        taosHashCancelIterate(pTq->tqPushMgr->pHash, pIter);
        terrno = TSDB_CODE_OUT_OF_MEMORY;
        return -1;
      }
      token->type = TQ_STREAM_TOKEN__DATA;
      token->data = msg;
      // set input
      // exec
    }
    // send msg to ep
  }
  // iterate hash
  // process all msg
  // if waiting
  // memcpy and send msg to fetch thread
  // TODO: add reference
  // if handle waiting, launch query and response to consumer
  //
  // if no waiting handle, return
#endif
  return 0;
}

int tqCommit(STQ* pTq) { return tqStorePersist(pTq->tqMeta); }

int32_t tqGetTopicHandleSize(const STqTopic* pTopic) {
  return strlen(pTopic->topicName) + strlen(pTopic->sql) + strlen(pTopic->physicalPlan) + strlen(pTopic->qmsg) +
         sizeof(int64_t) * 3;
}

int32_t tqGetConsumerHandleSize(const STqConsumer* pConsumer) {
  int     num = taosArrayGetSize(pConsumer->topics);
  int32_t sz = 0;
  for (int i = 0; i < num; i++) {
    STqTopic* pTopic = taosArrayGet(pConsumer->topics, i);
    sz += tqGetTopicHandleSize(pTopic);
  }
  return sz;
}

static FORCE_INLINE int32_t tEncodeSTqTopic(void** buf, const STqTopic* pTopic) {
  int32_t tlen = 0;
  tlen += taosEncodeString(buf, pTopic->topicName);
  /*tlen += taosEncodeString(buf, pTopic->sql);*/
  /*tlen += taosEncodeString(buf, pTopic->physicalPlan);*/
  tlen += taosEncodeString(buf, pTopic->qmsg);
  /*tlen += taosEncodeFixedI64(buf, pTopic->persistedOffset);*/
  /*tlen += taosEncodeFixedI64(buf, pTopic->committedOffset);*/
  /*tlen += taosEncodeFixedI64(buf, pTopic->currentOffset);*/
  return tlen;
}

static FORCE_INLINE const void* tDecodeSTqTopic(const void* buf, STqTopic* pTopic) {
  buf = taosDecodeStringTo(buf, pTopic->topicName);
  /*buf = taosDecodeString(buf, &pTopic->sql);*/
  /*buf = taosDecodeString(buf, &pTopic->physicalPlan);*/
  buf = taosDecodeString(buf, &pTopic->qmsg);
  /*buf = taosDecodeFixedI64(buf, &pTopic->persistedOffset);*/
  /*buf = taosDecodeFixedI64(buf, &pTopic->committedOffset);*/
  /*buf = taosDecodeFixedI64(buf, &pTopic->currentOffset);*/
  return buf;
}

static FORCE_INLINE int32_t tEncodeSTqConsumer(void** buf, const STqConsumer* pConsumer) {
  int32_t sz;

  int32_t tlen = 0;
  tlen += taosEncodeFixedI64(buf, pConsumer->consumerId);
  tlen += taosEncodeFixedI32(buf, pConsumer->epoch);
  tlen += taosEncodeString(buf, pConsumer->cgroup);
  sz = taosArrayGetSize(pConsumer->topics);
  tlen += taosEncodeFixedI32(buf, sz);
  for (int32_t i = 0; i < sz; i++) {
    STqTopic* pTopic = taosArrayGet(pConsumer->topics, i);
    tlen += tEncodeSTqTopic(buf, pTopic);
  }
  return tlen;
}

static FORCE_INLINE const void* tDecodeSTqConsumer(const void* buf, STqConsumer* pConsumer) {
  int32_t sz;

  buf = taosDecodeFixedI64(buf, &pConsumer->consumerId);
  buf = taosDecodeFixedI32(buf, &pConsumer->epoch);
  buf = taosDecodeStringTo(buf, pConsumer->cgroup);
  buf = taosDecodeFixedI32(buf, &sz);
  pConsumer->topics = taosArrayInit(sz, sizeof(STqTopic));
  if (pConsumer->topics == NULL) return NULL;
  for (int32_t i = 0; i < sz; i++) {
    STqTopic pTopic;
    buf = tDecodeSTqTopic(buf, &pTopic);
    taosArrayPush(pConsumer->topics, &pTopic);
  }
  return buf;
}

int tqSerializeConsumer(const STqConsumer* pConsumer, STqSerializedHead** ppHead) {
  int32_t sz = tEncodeSTqConsumer(NULL, pConsumer);

  if (sz > (*ppHead)->ssize) {
    void* tmpPtr = taosMemoryRealloc(*ppHead, sizeof(STqSerializedHead) + sz);
    if (tmpPtr == NULL) {
      taosMemoryFree(*ppHead);
      terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
      return -1;
    }
    *ppHead = tmpPtr;
    (*ppHead)->ssize = sz;
  }

  void* ptr = (*ppHead)->content;
  void* abuf = ptr;
  tEncodeSTqConsumer(&abuf, pConsumer);

  return 0;
}

int32_t tqDeserializeConsumer(STQ* pTq, const STqSerializedHead* pHead, STqConsumer** ppConsumer) {
  const void* str = pHead->content;
  *ppConsumer = taosMemoryCalloc(1, sizeof(STqConsumer));
  if (*ppConsumer == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return -1;
  }
  if (tDecodeSTqConsumer(str, *ppConsumer) == NULL) {
    terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
    return -1;
  }
  STqConsumer* pConsumer = *ppConsumer;
  int32_t      sz = taosArrayGetSize(pConsumer->topics);
  for (int32_t i = 0; i < sz; i++) {
    STqTopic* pTopic = taosArrayGet(pConsumer->topics, i);
    pTopic->pReadhandle = walOpenReadHandle(pTq->pWal);
    if (pTopic->pReadhandle == NULL) {
      ASSERT(false);
    }
    for (int j = 0; j < TQ_BUFFER_SIZE; j++) {
      pTopic->buffer.output[j].status = 0;
      STqReadHandle* pReadHandle = tqInitSubmitMsgScanner(pTq->pVnodeMeta);
      SReadHandle    handle = {
             .reader = pReadHandle,
             .meta = pTq->pVnodeMeta,
      };
      pTopic->buffer.output[j].pReadHandle = pReadHandle;
      pTopic->buffer.output[j].task = qCreateStreamExecTaskInfo(pTopic->qmsg, &handle);
    }
  }

  return 0;
}
#if 0
int32_t tqProcessPollReq(STQ* pTq, SRpcMsg* pMsg, int32_t workerId) {
  SMqPollReq* pReq = pMsg->pCont;
  int64_t     consumerId = pReq->consumerId;
  int64_t     fetchOffset;
  int64_t     blockingTime = pReq->blockingTime;
  int32_t     reqEpoch = pReq->epoch;

  if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__EARLIEAST) {
    fetchOffset = 0;
  } else if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__LATEST) {
    fetchOffset = walGetLastVer(pTq->pWal);
  } else {
    fetchOffset = pReq->currentOffset + 1;
  }

  vDebug("tmq poll: consumer %ld (epoch %d) recv poll req in vg %d, req %ld %ld", consumerId, pReq->epoch,
         pTq->pVnode->vgId, pReq->currentOffset, fetchOffset);

  SMqPollRsp rsp = {
      /*.consumerId = consumerId,*/
      .numOfTopics = 0,
      .pBlockData = NULL,
  };

  STqConsumer* pConsumer = tqHandleGet(pTq->tqMeta, consumerId);
  if (pConsumer == NULL) {
    vWarn("tmq poll: consumer %ld (epoch %d) not found in vg %d", consumerId, pReq->epoch, pTq->pVnode->vgId);
    pMsg->pCont = NULL;
    pMsg->contLen = 0;
    pMsg->code = -1;
    tmsgSendRsp(pMsg);
    return 0;
  }

  int32_t consumerEpoch = atomic_load_32(&pConsumer->epoch);
  while (consumerEpoch < reqEpoch) {
    consumerEpoch = atomic_val_compare_exchange_32(&pConsumer->epoch, consumerEpoch, reqEpoch);
  }

  STqTopic* pTopic = NULL;
  int32_t   sz = taosArrayGetSize(pConsumer->topics);
  for (int32_t i = 0; i < sz; i++) {
    STqTopic* topic = taosArrayGet(pConsumer->topics, i);
    // TODO race condition
    ASSERT(pConsumer->consumerId == consumerId);
    if (strcmp(topic->topicName, pReq->topic) == 0) {
      pTopic = topic;
      break;
    }
  }
  if (pTopic == NULL) {
    vWarn("tmq poll: consumer %ld (epoch %d) topic %s not found in vg %d", consumerId, pReq->epoch, pReq->topic,
          pTq->pVnode->vgId);
    pMsg->pCont = NULL;
    pMsg->contLen = 0;
    pMsg->code = -1;
    tmsgSendRsp(pMsg);
    return 0;
  }

  vDebug("poll topic %s from consumer %ld (epoch %d) vg %d", pTopic->topicName, consumerId, pReq->epoch,
         pTq->pVnode->vgId);

  rsp.reqOffset = pReq->currentOffset;
  rsp.skipLogNum = 0;

  while (1) {
    /*if (fetchOffset > walGetLastVer(pTq->pWal) || walReadWithHandle(pTopic->pReadhandle, fetchOffset) < 0) {*/
    // TODO
    consumerEpoch = atomic_load_32(&pConsumer->epoch);
    if (consumerEpoch > reqEpoch) {
      vDebug("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, found new consumer epoch %d discard req epoch %d",
             consumerId, pReq->epoch, pTq->pVnode->vgId, fetchOffset, consumerEpoch, reqEpoch);
      break;
    }
    SWalReadHead* pHead;
    if (walReadWithHandle_s(pTopic->pReadhandle, fetchOffset, &pHead) < 0) {
      // TODO: no more log, set timer to wait blocking time
      // if data inserted during waiting, launch query and
      // response to user
      vDebug("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, no more log to return", consumerId, pReq->epoch,
             pTq->pVnode->vgId, fetchOffset);
      break;
    }
    vDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d offset %ld msgType %d", consumerId, pReq->epoch,
           pTq->pVnode->vgId, fetchOffset, pHead->msgType);
    /*int8_t pos = fetchOffset % TQ_BUFFER_SIZE;*/
    /*pHead = pTopic->pReadhandle->pHead;*/
    if (pHead->msgType == TDMT_VND_SUBMIT) {
      SSubmitReq* pCont = (SSubmitReq*)&pHead->body;
      qTaskInfo_t task = pTopic->buffer.output[workerId].task;
      ASSERT(task);
      qSetStreamInput(task, pCont, STREAM_DATA_TYPE_SUBMIT_BLOCK);
      SArray* pRes = taosArrayInit(0, sizeof(SSDataBlock));
      while (1) {
        SSDataBlock* pDataBlock = NULL;
        uint64_t     ts;
        if (qExecTask(task, &pDataBlock, &ts) < 0) {
          ASSERT(false);
        }
        if (pDataBlock == NULL) {
          /*pos = fetchOffset % TQ_BUFFER_SIZE;*/
          break;
        }

        taosArrayPush(pRes, pDataBlock);
      }

      if (taosArrayGetSize(pRes) == 0) {
        vDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d skip log %ld since not wanted", consumerId,
               pReq->epoch, pTq->pVnode->vgId, fetchOffset);
        fetchOffset++;
        rsp.skipLogNum++;
        taosArrayDestroy(pRes);
        continue;
      }
      rsp.schema = pTopic->buffer.output[workerId].pReadHandle->pSchemaWrapper;
      rsp.rspOffset = fetchOffset;

      rsp.numOfTopics = 1;
      rsp.pBlockData = pRes;

      int32_t tlen = sizeof(SMqRspHead) + tEncodeSMqPollRsp(NULL, &rsp);
      void*   buf = rpcMallocCont(tlen);
      if (buf == NULL) {
        pMsg->code = -1;
        taosMemoryFree(pHead);
        return -1;
      }
      ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_RSP;
      ((SMqRspHead*)buf)->epoch = pReq->epoch;
      ((SMqRspHead*)buf)->consumerId = consumerId;

      void* abuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));
      tEncodeSMqPollRsp(&abuf, &rsp);
      /*taosArrayDestroyEx(rsp.pBlockData, (void (*)(void*))tDeleteSSDataBlock);*/
      pMsg->pCont = buf;
      pMsg->contLen = tlen;
      pMsg->code = 0;
      vDebug("vg %d offset %ld msgType %d from consumer %ld (epoch %d) actual rsp", pTq->pVnode->vgId, fetchOffset,
             pHead->msgType, consumerId, pReq->epoch);
      tmsgSendRsp(pMsg);
      taosMemoryFree(pHead);
      return 0;
    } else {
      taosMemoryFree(pHead);
      fetchOffset++;
      rsp.skipLogNum++;
    }
  }

  /*if (blockingTime != 0) {*/
  /*tqAddClientPusher(pTq->tqPushMgr, pMsg, consumerId, blockingTime);*/
  /*} else {*/
  int32_t tlen = sizeof(SMqRspHead) + tEncodeSMqPollRsp(NULL, &rsp);
  void*   buf = rpcMallocCont(tlen);
  if (buf == NULL) {
    pMsg->code = -1;
    return -1;
  }
  ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_RSP;
  ((SMqRspHead*)buf)->epoch = pReq->epoch;
  rsp.rspOffset = fetchOffset - 1;

  void* abuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));
  tEncodeSMqPollRsp(&abuf, &rsp);
  rsp.pBlockData = NULL;
  pMsg->pCont = buf;
  pMsg->contLen = tlen;
  pMsg->code = 0;
  tmsgSendRsp(pMsg);
  vDebug("vg %d offset %ld from consumer %ld (epoch %d) not rsp", pTq->pVnode->vgId, fetchOffset, consumerId,
         pReq->epoch);
  /*}*/

  return 0;
}
#endif

int32_t tqProcessPollReq(STQ* pTq, SRpcMsg* pMsg, int32_t workerId) {
  SMqPollReq* pReq = pMsg->pCont;
  int64_t     consumerId = pReq->consumerId;
  int64_t     fetchOffset;
  int64_t     blockingTime = pReq->blockingTime;
  int32_t     reqEpoch = pReq->epoch;

  if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__EARLIEAST) {
    fetchOffset = 0;
  } else if (pReq->currentOffset == TMQ_CONF__RESET_OFFSET__LATEST) {
    fetchOffset = walGetLastVer(pTq->pWal);
  } else {
    fetchOffset = pReq->currentOffset + 1;
  }

  vDebug("tmq poll: consumer %ld (epoch %d) recv poll req in vg %d, req %ld %ld", consumerId, pReq->epoch,
         pTq->pVnode->vgId, pReq->currentOffset, fetchOffset);

  SMqPollRspV2 rspV2 = {0};
  rspV2.dataLen = 0;

  STqConsumer* pConsumer = tqHandleGet(pTq->tqMeta, consumerId);
  if (pConsumer == NULL) {
    vWarn("tmq poll: consumer %ld (epoch %d) not found in vg %d", consumerId, pReq->epoch, pTq->pVnode->vgId);
    pMsg->pCont = NULL;
    pMsg->contLen = 0;
    pMsg->code = -1;
    tmsgSendRsp(pMsg);
    return 0;
  }

  int32_t consumerEpoch = atomic_load_32(&pConsumer->epoch);
  while (consumerEpoch < reqEpoch) {
    consumerEpoch = atomic_val_compare_exchange_32(&pConsumer->epoch, consumerEpoch, reqEpoch);
  }

  STqTopic* pTopic = NULL;
  int32_t   topicSz = taosArrayGetSize(pConsumer->topics);
  for (int32_t i = 0; i < topicSz; i++) {
    STqTopic* topic = taosArrayGet(pConsumer->topics, i);
    // TODO race condition
    ASSERT(pConsumer->consumerId == consumerId);
    if (strcmp(topic->topicName, pReq->topic) == 0) {
      pTopic = topic;
      break;
    }
  }
  if (pTopic == NULL) {
    vWarn("tmq poll: consumer %ld (epoch %d) topic %s not found in vg %d", consumerId, pReq->epoch, pReq->topic,
          pTq->pVnode->vgId);
    pMsg->pCont = NULL;
    pMsg->contLen = 0;
    pMsg->code = -1;
    tmsgSendRsp(pMsg);
    return 0;
  }

  vDebug("poll topic %s from consumer %ld (epoch %d) vg %d", pTopic->topicName, consumerId, pReq->epoch,
         pTq->pVnode->vgId);

  rspV2.reqOffset = pReq->currentOffset;
  rspV2.skipLogNum = 0;

  while (1) {
    /*if (fetchOffset > walGetLastVer(pTq->pWal) || walReadWithHandle(pTopic->pReadhandle, fetchOffset) < 0) {*/
    // TODO
    consumerEpoch = atomic_load_32(&pConsumer->epoch);
    if (consumerEpoch > reqEpoch) {
      vDebug("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, found new consumer epoch %d discard req epoch %d",
             consumerId, pReq->epoch, pTq->pVnode->vgId, fetchOffset, consumerEpoch, reqEpoch);
      break;
    }
    SWalReadHead* pHead;
    if (walReadWithHandle_s(pTopic->pReadhandle, fetchOffset, &pHead) < 0) {
      // TODO: no more log, set timer to wait blocking time
      // if data inserted during waiting, launch query and
      // response to user
      vDebug("tmq poll: consumer %ld (epoch %d) vg %d offset %ld, no more log to return", consumerId, pReq->epoch,
             pTq->pVnode->vgId, fetchOffset);
      break;
    }
    vDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d offset %ld msgType %d", consumerId, pReq->epoch,
           pTq->pVnode->vgId, fetchOffset, pHead->msgType);
    /*int8_t pos = fetchOffset % TQ_BUFFER_SIZE;*/
    /*pHead = pTopic->pReadhandle->pHead;*/
    if (pHead->msgType == TDMT_VND_SUBMIT) {
      SSubmitReq* pCont = (SSubmitReq*)&pHead->body;
      qTaskInfo_t task = pTopic->buffer.output[workerId].task;
      ASSERT(task);
      qSetStreamInput(task, pCont, STREAM_DATA_TYPE_SUBMIT_BLOCK);
      SArray* pRes = taosArrayInit(0, sizeof(SSDataBlock));
      while (1) {
        SSDataBlock* pDataBlock = NULL;
        uint64_t     ts;
        if (qExecTask(task, &pDataBlock, &ts) < 0) {
          ASSERT(false);
        }
        if (pDataBlock == NULL) {
          /*pos = fetchOffset % TQ_BUFFER_SIZE;*/
          break;
        }

        taosArrayPush(pRes, pDataBlock);
      }

      if (taosArrayGetSize(pRes) == 0) {
        vDebug("tmq poll: consumer %ld (epoch %d) iter log, vg %d skip log %ld since not wanted", consumerId,
               pReq->epoch, pTq->pVnode->vgId, fetchOffset);
        fetchOffset++;
        rspV2.skipLogNum++;
        taosArrayDestroy(pRes);
        continue;
      }
      rspV2.rspOffset = fetchOffset;

      int32_t blockSz = taosArrayGetSize(pRes);
      int32_t dataBlockStrLen = 0;
      for (int32_t i = 0; i < blockSz; i++) {
        SSDataBlock* pBlock = taosArrayGet(pRes, i);
        dataBlockStrLen += sizeof(SRetrieveTableRsp) + blockGetEncodeSize(pBlock);
      }

      void* dataBlockBuf = taosMemoryMalloc(dataBlockStrLen);
      if (dataBlockBuf == NULL) {
        pMsg->code = -1;
        taosMemoryFree(pHead);
      }

      rspV2.blockData = dataBlockBuf;

      int32_t pos;
      rspV2.blockPos = taosArrayInit(blockSz, sizeof(int32_t));
      for (int32_t i = 0; i < blockSz; i++) {
        pos = 0;
        SSDataBlock*       pBlock = taosArrayGet(pRes, i);
        SRetrieveTableRsp* pRetrieve = (SRetrieveTableRsp*)dataBlockBuf;
        pRetrieve->useconds = 0;
        pRetrieve->precision = 0;
        pRetrieve->compressed = 0;
        pRetrieve->completed = 1;
        pRetrieve->numOfRows = htonl(pBlock->info.rows);
        blockCompressEncode(pBlock, pRetrieve->data, &pos, pBlock->info.numOfCols, false);
        taosArrayPush(rspV2.blockPos, &rspV2.dataLen);

        int32_t totLen = sizeof(SRetrieveTableRsp) + pos;
        pRetrieve->compLen = htonl(totLen);
        rspV2.dataLen += totLen;
        dataBlockBuf = POINTER_SHIFT(dataBlockBuf, totLen);
      }
      ASSERT(POINTER_DISTANCE(dataBlockBuf, rspV2.blockData) <= dataBlockStrLen);

      int32_t msgLen = sizeof(SMqRspHead) + tEncodeSMqPollRspV2(NULL, &rspV2);
      void*   buf = rpcMallocCont(msgLen);

      ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_RSP;
      ((SMqRspHead*)buf)->epoch = pReq->epoch;
      ((SMqRspHead*)buf)->consumerId = consumerId;

      void* msgBodyBuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));
      tEncodeSMqPollRspV2(&msgBodyBuf, &rspV2);

      /*rsp.pBlockData = pRes;*/

      /*taosArrayDestroyEx(rsp.pBlockData, (void (*)(void*))tDeleteSSDataBlock);*/
      pMsg->pCont = buf;
      pMsg->contLen = msgLen;
      pMsg->code = 0;
      vDebug("vg %d offset %ld msgType %d from consumer %ld (epoch %d) actual rsp", pTq->pVnode->vgId, fetchOffset,
             pHead->msgType, consumerId, pReq->epoch);
      tmsgSendRsp(pMsg);
      taosMemoryFree(pHead);
      return 0;
    } else {
      taosMemoryFree(pHead);
      fetchOffset++;
      rspV2.skipLogNum++;
    }
  }

  /*if (blockingTime != 0) {*/
  /*tqAddClientPusher(pTq->tqPushMgr, pMsg, consumerId, blockingTime);*/
  /*} else {*/

  rspV2.rspOffset = fetchOffset - 1;

  int32_t tlen = sizeof(SMqRspHead) + tEncodeSMqPollRspV2(NULL, &rspV2);
  void*   buf = rpcMallocCont(tlen);
  if (buf == NULL) {
    pMsg->code = -1;
    return -1;
  }
  ((SMqRspHead*)buf)->mqMsgType = TMQ_MSG_TYPE__POLL_RSP;
  ((SMqRspHead*)buf)->epoch = pReq->epoch;
  ((SMqRspHead*)buf)->consumerId = consumerId;

  void* abuf = POINTER_SHIFT(buf, sizeof(SMqRspHead));
  tEncodeSMqPollRspV2(&abuf, &rspV2);
  pMsg->pCont = buf;
  pMsg->contLen = tlen;
  pMsg->code = 0;
  tmsgSendRsp(pMsg);
  vDebug("vg %d offset %ld from consumer %ld (epoch %d) not rsp", pTq->pVnode->vgId, fetchOffset, consumerId,
         pReq->epoch);
  /*}*/

  return 0;
}

int32_t tqProcessRebReq(STQ* pTq, char* msg) {
  SMqMVRebReq req = {0};
  terrno = TSDB_CODE_SUCCESS;
  tDecodeSMqMVRebReq(msg, &req);

  vDebug("vg %d set from consumer %ld to consumer %ld", req.vgId, req.oldConsumerId, req.newConsumerId);
  STqConsumer* pConsumer = tqHandleGet(pTq->tqMeta, req.oldConsumerId);
  ASSERT(pConsumer);
  ASSERT(pConsumer->consumerId == req.oldConsumerId);
  int32_t numOfTopics = taosArrayGetSize(pConsumer->topics);
  if (numOfTopics == 1) {
    STqTopic* pTopic = taosArrayGet(pConsumer->topics, 0);
    ASSERT(strcmp(pTopic->topicName, req.topic) == 0);
    STqConsumer* pNewConsumer = tqHandleGet(pTq->tqMeta, req.newConsumerId);
    if (pNewConsumer == NULL) {
      pConsumer->consumerId = req.newConsumerId;
      tqHandleMovePut(pTq->tqMeta, req.newConsumerId, pConsumer);
      tqHandleCommit(pTq->tqMeta, req.newConsumerId);
      tqHandlePurge(pTq->tqMeta, req.oldConsumerId);
      return 0;
    } else {
      taosArrayPush(pNewConsumer->topics, pTopic);
    }
  } else {
    for (int32_t i = 0; i < numOfTopics; i++) {
      STqTopic* pTopic = taosArrayGet(pConsumer->topics, i);
      if (strcmp(pTopic->topicName, req.topic) == 0) {
        STqConsumer* pNewConsumer = tqHandleGet(pTq->tqMeta, req.newConsumerId);
        if (pNewConsumer == NULL) {
          pNewConsumer = taosMemoryCalloc(1, sizeof(STqConsumer));
          if (pNewConsumer == NULL) {
            terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
            return -1;
          }
          strcpy(pNewConsumer->cgroup, pConsumer->cgroup);
          pNewConsumer->topics = taosArrayInit(0, sizeof(STqTopic));
          pNewConsumer->consumerId = req.newConsumerId;
          pNewConsumer->epoch = 0;

          taosArrayPush(pNewConsumer->topics, pTopic);
          tqHandleMovePut(pTq->tqMeta, req.newConsumerId, pConsumer);
          tqHandleCommit(pTq->tqMeta, req.newConsumerId);
          return 0;
        }
        ASSERT(pNewConsumer->consumerId == req.newConsumerId);
        taosArrayPush(pNewConsumer->topics, pTopic);
        break;
      }
    }
    //
  }
  return 0;
}

int32_t tqProcessSetConnReq(STQ* pTq, char* msg) {
  SMqSetCVgReq req = {0};
  tDecodeSMqSetCVgReq(msg, &req);
  bool create = false;

  vDebug("vg %d set to consumer %ld", req.vgId, req.consumerId);
  STqConsumer* pConsumer = tqHandleGet(pTq->tqMeta, req.consumerId);
  if (pConsumer == NULL) {
    pConsumer = taosMemoryCalloc(1, sizeof(STqConsumer));
    if (pConsumer == NULL) {
      terrno = TSDB_CODE_TQ_OUT_OF_MEMORY;
      return -1;
    }
    strcpy(pConsumer->cgroup, req.cgroup);
    pConsumer->topics = taosArrayInit(0, sizeof(STqTopic));
    pConsumer->consumerId = req.consumerId;
    pConsumer->epoch = 0;
    create = true;
  }

  STqTopic* pTopic = taosMemoryCalloc(1, sizeof(STqTopic));
  if (pTopic == NULL) {
    taosArrayDestroy(pConsumer->topics);
    taosMemoryFree(pConsumer);
    return -1;
  }
  strcpy(pTopic->topicName, req.topicName);
  pTopic->sql = req.sql;
  pTopic->physicalPlan = req.physicalPlan;
  pTopic->qmsg = req.qmsg;
  /*pTopic->committedOffset = -1;*/
  /*pTopic->currentOffset = -1;*/

  pTopic->buffer.firstOffset = -1;
  pTopic->buffer.lastOffset = -1;
  pTopic->pReadhandle = walOpenReadHandle(pTq->pWal);
  if (pTopic->pReadhandle == NULL) {
    ASSERT(false);
  }
  for (int i = 0; i < TQ_BUFFER_SIZE; i++) {
    pTopic->buffer.output[i].status = 0;
    STqReadHandle* pReadHandle = tqInitSubmitMsgScanner(pTq->pVnodeMeta);
    SReadHandle    handle = {
           .reader = pReadHandle,
           .meta = pTq->pVnodeMeta,
    };
    pTopic->buffer.output[i].pReadHandle = pReadHandle;
    pTopic->buffer.output[i].task = qCreateStreamExecTaskInfo(req.qmsg, &handle);
    ASSERT(pTopic->buffer.output[i].task);
  }
  vDebug("set topic %s to consumer %ld on vg %d", pTopic->topicName, req.consumerId, pTq->pVnode->vgId);
  taosArrayPush(pConsumer->topics, pTopic);
  if (create) {
    tqHandleMovePut(pTq->tqMeta, req.consumerId, pConsumer);
    tqHandleCommit(pTq->tqMeta, req.consumerId);
  }
  terrno = TSDB_CODE_SUCCESS;
  return 0;
}

int32_t tqProcessCancelConnReq(STQ* pTq, char* msg) {
  terrno = TSDB_CODE_SUCCESS;
  return 0;
}

int32_t tqExpandTask(STQ* pTq, SStreamTask* pTask, int32_t parallel) {
  if (pTask->execType == TASK_EXEC__NONE) return 0;

  pTask->exec.numOfRunners = parallel;
  pTask->exec.runners = taosMemoryCalloc(parallel, sizeof(SStreamRunner));
  if (pTask->exec.runners == NULL) {
    return -1;
  }
  for (int32_t i = 0; i < parallel; i++) {
    STqReadHandle* pReadHandle = tqInitSubmitMsgScanner(pTq->pVnodeMeta);
    SReadHandle    handle = {
           .reader = pReadHandle,
           .meta = pTq->pVnodeMeta,
    };
    pTask->exec.runners[i].inputHandle = pReadHandle;
    pTask->exec.runners[i].executor = qCreateStreamExecTaskInfo(pTask->exec.qmsg, &handle);
    ASSERT(pTask->exec.runners[i].executor);
  }
  return 0;
}

int32_t tqProcessTaskDeploy(STQ* pTq, char* msg, int32_t msgLen) {
  SStreamTask* pTask = taosMemoryMalloc(sizeof(SStreamTask));
  if (pTask == NULL) {
    return -1;
  }
  SCoder decoder;
  tCoderInit(&decoder, TD_LITTLE_ENDIAN, (uint8_t*)msg, msgLen, TD_DECODER);
  if (tDecodeSStreamTask(&decoder, pTask) < 0) {
    ASSERT(0);
  }
  tCoderClear(&decoder);

  // exec
  if (tqExpandTask(pTq, pTask, 4) < 0) {
    ASSERT(0);
  }

  // sink
  pTask->ahandle = pTq->pVnode;
  if (pTask->sinkType == TASK_SINK__SMA) {
    pTask->smaSink.smaHandle = smaHandleRes;
  }

  taosHashPut(pTq->pStreamTasks, &pTask->taskId, sizeof(int32_t), pTask, sizeof(SStreamTask));

  return 0;
}

int32_t tqProcessStreamTrigger(STQ* pTq, void* data, int32_t dataLen, int32_t workerId) {
  void* pIter = NULL;

  while (1) {
    pIter = taosHashIterate(pTq->pStreamTasks, pIter);
    if (pIter == NULL) break;
    SStreamTask* pTask = (SStreamTask*)pIter;

    if (streamExecTask(pTask, &pTq->pVnode->msgCb, data, STREAM_DATA_TYPE_SUBMIT_BLOCK, workerId) < 0) {
      // TODO
    }
  }
  return 0;
}

int32_t tqProcessTaskExec(STQ* pTq, char* msg, int32_t msgLen, int32_t workerId) {
  SStreamTaskExecReq req;
  tDecodeSStreamTaskExecReq(msg, &req);

  int32_t taskId = req.taskId;
  ASSERT(taskId);

  SStreamTask* pTask = taosHashGet(pTq->pStreamTasks, &taskId, sizeof(int32_t));
  ASSERT(pTask);

  if (streamExecTask(pTask, &pTq->pVnode->msgCb, req.data, STREAM_DATA_TYPE_SSDATA_BLOCK, workerId) < 0) {
    // TODO
  }
  return 0;
}
