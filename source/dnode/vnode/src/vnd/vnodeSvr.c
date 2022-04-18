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

static int vnodeProcessCreateStbReq(SVnode *pVnode, void *pReq);
static int vnodeProcessCreateTbReq(SVnode *pVnode, SRpcMsg *pMsg, void *pReq, SRpcMsg **pRsp);
static int vnodeProcessAlterStbReq(SVnode *pVnode, void *pReq);
static int vnodeProcessSubmitReq(SVnode *pVnode, SSubmitReq *pSubmitReq, SRpcMsg *pRsp);

void vnodePreprocessWriteReqs(SVnode *pVnode, SArray *pMsgs) {
  SNodeMsg *pMsg;
  SRpcMsg  *pRpc;

  for (int i = 0; i < taosArrayGetSize(pMsgs); i++) {
    pMsg = *(SNodeMsg **)taosArrayGet(pMsgs, i);
    pRpc = &pMsg->rpcMsg;

    // set request version
    void   *pBuf = POINTER_SHIFT(pRpc->pCont, sizeof(SMsgHead));
    int64_t ver = pVnode->state.processed++;
    taosEncodeFixedI64(&pBuf, ver);

    if (walWrite(pVnode->pWal, ver, pRpc->msgType, pRpc->pCont, pRpc->contLen) < 0) {
      // TODO: handle error
      /*ASSERT(false);*/
      vError("vnode:%d  write wal error since %s", TD_VID(pVnode), terrstr());
    }
  }

  walFsync(pVnode->pWal, false);
}

int vnodeProcessWriteReq(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp) {
  void *ptr = NULL;
  int   ret;

  if (pVnode->config.streamMode == 0) {
    ptr = vnodeMalloc(pVnode, pMsg->contLen);
    if (ptr == NULL) {
      // TODO: handle error
    }

    // TODO: copy here need to be extended
    memcpy(ptr, pMsg->pCont, pMsg->contLen);
  }

  // todo: change the interface here
  int64_t ver;
  taosDecodeFixedI64(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &ver);
  if (tqPushMsg(pVnode->pTq, pMsg->pCont, pMsg->contLen, pMsg->msgType, ver) < 0) {
    // TODO: handle error
  }

  switch (pMsg->msgType) {
    case TDMT_VND_CREATE_STB:
      ret = vnodeProcessCreateStbReq(pVnode, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)));
      return 0;
    case TDMT_VND_CREATE_TABLE:
      return vnodeProcessCreateTbReq(pVnode, pMsg, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), pRsp);
    case TDMT_VND_ALTER_STB:
      return vnodeProcessAlterStbReq(pVnode, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)));
    case TDMT_VND_DROP_STB:
      vTrace("vgId:%d, process drop stb req", TD_VID(pVnode));
      break;
    case TDMT_VND_DROP_TABLE:
      break;
    case TDMT_VND_SUBMIT:
      /*printf("vnode %d write data %ld\n", TD_VID(pVnode), ver);*/
      if (pVnode->config.streamMode == 0) {
        *pRsp = taosMemoryCalloc(1, sizeof(SRpcMsg));
        (*pRsp)->handle = pMsg->handle;
        (*pRsp)->ahandle = pMsg->ahandle;
        return vnodeProcessSubmitReq(pVnode, ptr, *pRsp);
      }
      break;
    case TDMT_VND_MQ_SET_CONN: {
      if (tqProcessSetConnReq(pVnode->pTq, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
        // TODO: handle error
      }
    } break;
    case TDMT_VND_MQ_REB: {
      if (tqProcessRebReq(pVnode->pTq, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
      }
    } break;
    case TDMT_VND_MQ_CANCEL_CONN: {
      if (tqProcessCancelConnReq(pVnode->pTq, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
      }
    } break;
    case TDMT_VND_TASK_DEPLOY: {
      if (tqProcessTaskDeploy(pVnode->pTq, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)),
                              pMsg->contLen - sizeof(SMsgHead)) < 0) {
      }
    } break;
    case TDMT_VND_TASK_WRITE_EXEC: {
      if (tqProcessTaskExec(pVnode->pTq, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), pMsg->contLen - sizeof(SMsgHead),
                            0) < 0) {
      }
    } break;
    case TDMT_VND_CREATE_SMA: {  // timeRangeSMA

      if (tsdbCreateTSma(pVnode->pTsdb, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
        // TODO
      }
      // } break;
      // case TDMT_VND_CANCEL_SMA: {  // timeRangeSMA
      // } break;
      // case TDMT_VND_DROP_SMA: {  // timeRangeSMA
      //   if (tsdbDropTSma(pVnode->pTsdb, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
      //     // TODO
      //   }

    } break;
    default:
      ASSERT(0);
      break;
  }

  pVnode->state.applied = ver;

  // Check if it needs to commit
  if (vnodeShouldCommit(pVnode)) {
    // tsem_wait(&(pVnode->canCommit));
    if (vnodeAsyncCommit(pVnode) < 0) {
      // TODO: handle error
    }
  }

  return 0;
}

int vnodeProcessQueryMsg(SVnode *pVnode, SRpcMsg *pMsg) {
  vTrace("message in query queue is processing");
  SReadHandle handle = {.reader = pVnode->pTsdb, .meta = pVnode->pMeta, .config = &pVnode->config};

  switch (pMsg->msgType) {
    case TDMT_VND_QUERY:
      return qWorkerProcessQueryMsg(&handle, pVnode->pQuery, pMsg);
    case TDMT_VND_QUERY_CONTINUE:
      return qWorkerProcessCQueryMsg(&handle, pVnode->pQuery, pMsg);
    default:
      vError("unknown msg type:%d in query queue", pMsg->msgType);
      return TSDB_CODE_VND_APP_ERROR;
  }
}

int vnodeProcessFetchMsg(SVnode *pVnode, SRpcMsg *pMsg, SQueueInfo *pInfo) {
  vTrace("message in fetch queue is processing");
  char   *msgstr = POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead));
  int32_t msgLen = pMsg->contLen - sizeof(SMsgHead);
  switch (pMsg->msgType) {
    case TDMT_VND_FETCH:
      return qWorkerProcessFetchMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_FETCH_RSP:
      return qWorkerProcessFetchRsp(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_RES_READY:
      return qWorkerProcessReadyMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_TASKS_STATUS:
      return qWorkerProcessStatusMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_CANCEL_TASK:
      return qWorkerProcessCancelMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_DROP_TASK:
      return qWorkerProcessDropMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_TABLE_META:
      return vnodeGetTableMeta(pVnode, pMsg);
    case TDMT_VND_CONSUME:
      return tqProcessPollReq(pVnode->pTq, pMsg, pInfo->workerId);
    case TDMT_VND_TASK_PIPE_EXEC:
    case TDMT_VND_TASK_MERGE_EXEC:
      return tqProcessTaskExec(pVnode->pTq, msgstr, msgLen, 0);
    case TDMT_VND_STREAM_TRIGGER:
      return tqProcessStreamTrigger(pVnode->pTq, pMsg->pCont, pMsg->contLen, 0);
    case TDMT_VND_QUERY_HEARTBEAT:
      return qWorkerProcessHbMsg(pVnode, pVnode->pQuery, pMsg);
    default:
      vError("unknown msg type:%d in fetch queue", pMsg->msgType);
      return TSDB_CODE_VND_APP_ERROR;
  }
}

// TODO: remove the function
void smaHandleRes(void *pVnode, int64_t smaId, const SArray *data) {
  // TODO

  // blockDebugShowData(data);
  tsdbInsertTSmaData(((SVnode *)pVnode)->pTsdb, smaId, (const char *)data);
}

int vnodeProcessSyncReq(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp) {
  /*vInfo("sync message is processed");*/
  return 0;
}

static int vnodeProcessCreateStbReq(SVnode *pVnode, void *pReq) {
  SVCreateTbReq vCreateTbReq = {0};
  tDeserializeSVCreateTbReq(pReq, &vCreateTbReq);
  if (metaCreateTable(pVnode->pMeta, &(vCreateTbReq)) < 0) {
    // TODO
    return -1;
  }

  taosMemoryFree(vCreateTbReq.stbCfg.pSchema);
  taosMemoryFree(vCreateTbReq.stbCfg.pTagSchema);
  if (vCreateTbReq.stbCfg.pRSmaParam) {
    taosMemoryFree(vCreateTbReq.stbCfg.pRSmaParam->pFuncIds);
    taosMemoryFree(vCreateTbReq.stbCfg.pRSmaParam);
  }
  taosMemoryFree(vCreateTbReq.dbFName);
  taosMemoryFree(vCreateTbReq.name);

  return 0;
}

static int vnodeProcessCreateTbReq(SVnode *pVnode, SRpcMsg *pMsg, void *pReq, SRpcMsg **pRsp) {
  SVCreateTbBatchReq vCreateTbBatchReq = {0};
  SVCreateTbBatchRsp vCreateTbBatchRsp = {0};
  tDeserializeSVCreateTbBatchReq(pReq, &vCreateTbBatchReq);
  int reqNum = taosArrayGetSize(vCreateTbBatchReq.pArray);
  for (int i = 0; i < reqNum; i++) {
    SVCreateTbReq *pCreateTbReq = taosArrayGet(vCreateTbBatchReq.pArray, i);

    char      tableFName[TSDB_TABLE_FNAME_LEN];
    SMsgHead *pHead = (SMsgHead *)pMsg->pCont;
    sprintf(tableFName, "%s.%s", pCreateTbReq->dbFName, pCreateTbReq->name);

    int32_t code = vnodeValidateTableHash(&pVnode->config, tableFName);
    if (code) {
      SVCreateTbRsp rsp;
      rsp.code = code;

      taosArrayPush(vCreateTbBatchRsp.rspList, &rsp);
    }

    if (metaCreateTable(pVnode->pMeta, pCreateTbReq) < 0) {
      // TODO: handle error
      vError("vgId:%d, failed to create table: %s", TD_VID(pVnode), pCreateTbReq->name);
    }
    // TODO: to encapsule a free API
    taosMemoryFree(pCreateTbReq->name);
    taosMemoryFree(pCreateTbReq->dbFName);
    if (pCreateTbReq->type == TD_SUPER_TABLE) {
      taosMemoryFree(pCreateTbReq->stbCfg.pSchema);
      taosMemoryFree(pCreateTbReq->stbCfg.pTagSchema);
      if (pCreateTbReq->stbCfg.pRSmaParam) {
        taosMemoryFree(pCreateTbReq->stbCfg.pRSmaParam->pFuncIds);
        taosMemoryFree(pCreateTbReq->stbCfg.pRSmaParam);
      }
    } else if (pCreateTbReq->type == TD_CHILD_TABLE) {
      taosMemoryFree(pCreateTbReq->ctbCfg.pTag);
    } else {
      taosMemoryFree(pCreateTbReq->ntbCfg.pSchema);
      if (pCreateTbReq->ntbCfg.pRSmaParam) {
        taosMemoryFree(pCreateTbReq->ntbCfg.pRSmaParam->pFuncIds);
        taosMemoryFree(pCreateTbReq->ntbCfg.pRSmaParam);
      }
    }
  }

  vTrace("vgId:%d process create %" PRIzu " tables", TD_VID(pVnode), taosArrayGetSize(vCreateTbBatchReq.pArray));
  taosArrayDestroy(vCreateTbBatchReq.pArray);
  if (vCreateTbBatchRsp.rspList) {
    int32_t contLen = tSerializeSVCreateTbBatchRsp(NULL, 0, &vCreateTbBatchRsp);
    void   *msg = rpcMallocCont(contLen);
    tSerializeSVCreateTbBatchRsp(msg, contLen, &vCreateTbBatchRsp);
    taosArrayDestroy(vCreateTbBatchRsp.rspList);

    *pRsp = taosMemoryCalloc(1, sizeof(SRpcMsg));
    (*pRsp)->msgType = TDMT_VND_CREATE_TABLE_RSP;
    (*pRsp)->pCont = msg;
    (*pRsp)->contLen = contLen;
    (*pRsp)->handle = pMsg->handle;
    (*pRsp)->ahandle = pMsg->ahandle;
  }

  return 0;
}

static int vnodeProcessAlterStbReq(SVnode *pVnode, void *pReq) {
  SVCreateTbReq vAlterTbReq = {0};
  vTrace("vgId:%d, process alter stb req", TD_VID(pVnode));
  tDeserializeSVCreateTbReq(pReq, &vAlterTbReq);
  // TODO: to encapsule a free API
  taosMemoryFree(vAlterTbReq.stbCfg.pSchema);
  taosMemoryFree(vAlterTbReq.stbCfg.pTagSchema);
  if (vAlterTbReq.stbCfg.pRSmaParam) {
    taosMemoryFree(vAlterTbReq.stbCfg.pRSmaParam->pFuncIds);
    taosMemoryFree(vAlterTbReq.stbCfg.pRSmaParam);
  }
  taosMemoryFree(vAlterTbReq.dbFName);
  taosMemoryFree(vAlterTbReq.name);
  return 0;
}

static int vnodeProcessSubmitReq(SVnode *pVnode, SSubmitReq *pSubmitReq, SRpcMsg *pRsp) {
  SSubmitRsp rsp = {0};

  pRsp->code = 0;

  // handle the request
  if (tsdbInsertData(pVnode->pTsdb, pSubmitReq, &rsp) < 0) {
    pRsp->code = terrno;
    return -1;
  }

  // encode the response (TODO)
  pRsp->msgType = TDMT_VND_SUBMIT_RSP;
  pRsp->pCont = rpcMallocCont(sizeof(SSubmitRsp));
  memcpy(pRsp->pCont, &rsp, sizeof(rsp));
  pRsp->contLen = sizeof(SSubmitRsp);

  return 0;
}