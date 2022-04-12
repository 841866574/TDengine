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

int vnodePreProcessWriteMsgs(SVnode *pVnode, SArray *pMsgs, int64_t *pVer) {
  SRpcMsg *pMsg;
  int64_t  version;

  version = ++pVnode->state.processed;

  for (int i = 0; i < taosArrayGetSize(pMsgs); i++) {
    pMsg = &(*(SNodeMsg **)taosArrayGet(pMsgs, i))[0].rpcMsg;

    if (walWrite(pVnode->pWal, version, pMsg->msgType, pMsg->pCont, pMsg->contLen) < 0) {
      vError("vgId: %d failed to pre-process write message, version %" PRId64 " since: %s", TD_VNODE_ID(pVnode),
             version, tstrerror(terrno));
      return -1;
    }
  }

  walFsync(pVnode->pWal, false);

  *pVer = version;
  return 0;
}

int vnodeProcessWriteMsg(SVnode *pVnode, SRpcMsg *pMsg, int64_t version, SRpcMsg **pRsp) {
  ASSERT(pVnode->state.applied <= version);

  // check commit
  if (version > pVnode->state.applied && pVnode->pPool->size >= pVnode->config.szBuf / 3) {
    // async commit
    if (vnodeAsyncCommit(pVnode) < 0) {
      vError("vgId: %d failed to async commit", TD_VNODE_ID(pVnode));
      ASSERT(0);
    }

    // start a new write session
    if (vnodeBegin(pVnode) < 0) {
      vError("vgId: %d failed to begin vnode since %s", TD_VNODE_ID(pVnode), tstrerror(terrno));
      ASSERT(0);
    }
  }

  pVnode->state.applied = version;

#if 0
  void *ptr = NULL;

  if (pVnode->config.streamMode == 0) {
    ptr = vnodeBufPoolMalloc(pVnode->inUse, pMsg->contLen);
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
    case TDMT_VND_CREATE_STB: {
      SVCreateTbReq vCreateTbReq = {0};
      tDeserializeSVCreateTbReq(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &vCreateTbReq);
      if (metaCreateTable(pVnode->pMeta, &(vCreateTbReq)) < 0) {
        // TODO: handle error
      }

      // TODO: to encapsule a free API
      taosMemoryFree(vCreateTbReq.stbCfg.pSchema);
      taosMemoryFree(vCreateTbReq.stbCfg.pTagSchema);
      if (vCreateTbReq.stbCfg.pRSmaParam) {
        taosMemoryFree(vCreateTbReq.stbCfg.pRSmaParam->pFuncIds);
        taosMemoryFree(vCreateTbReq.stbCfg.pRSmaParam);
      }
      taosMemoryFree(vCreateTbReq.dbFName);
      taosMemoryFree(vCreateTbReq.name);
      break;
    }
    case TDMT_VND_CREATE_TABLE: {
      SVCreateTbBatchReq vCreateTbBatchReq = {0};
      SVCreateTbBatchRsp vCreateTbBatchRsp = {0};
      tDeserializeSVCreateTbBatchReq(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &vCreateTbBatchReq);
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
          vError("vgId:%d, failed to create table: %s", pVnode->vgId, pCreateTbReq->name);
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

      vTrace("vgId:%d process create %" PRIzu " tables", pVnode->vgId, taosArrayGetSize(vCreateTbBatchReq.pArray));
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
      break;
    }
    case TDMT_VND_ALTER_STB: {
      SVCreateTbReq vAlterTbReq = {0};
      vTrace("vgId:%d, process alter stb req", pVnode->vgId);
      tDeserializeSVCreateTbReq(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &vAlterTbReq);
      // TODO: to encapsule a free API
      taosMemoryFree(vAlterTbReq.stbCfg.pSchema);
      taosMemoryFree(vAlterTbReq.stbCfg.pTagSchema);
      if (vAlterTbReq.stbCfg.pRSmaParam) {
        taosMemoryFree(vAlterTbReq.stbCfg.pRSmaParam->pFuncIds);
        taosMemoryFree(vAlterTbReq.stbCfg.pRSmaParam);
      }
      taosMemoryFree(vAlterTbReq.dbFName);
      taosMemoryFree(vAlterTbReq.name);
      break;
    }
    case TDMT_VND_DROP_STB:
      vTrace("vgId:%d, process drop stb req", pVnode->vgId);
      break;
    case TDMT_VND_DROP_TABLE:
      // if (metaDropTable(pVnode->pMeta, vReq.dtReq.uid) < 0) {
      //   // TODO: handle error
      // }
      break;
    case TDMT_VND_SUBMIT:
      /*printf("vnode %d write data %ld\n", pVnode->vgId, ver);*/
      if (pVnode->config.streamMode == 0) {
        if (tsdbInsertData(pVnode->pTsdb, (SSubmitReq *)ptr, NULL) < 0) {
          // TODO: handle error
        }
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
#if 0

      SSmaCfg vCreateSmaReq = {0};
      if (tDeserializeSVCreateTSmaReq(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &vCreateSmaReq) == NULL) {
        terrno = TSDB_CODE_OUT_OF_MEMORY;
        vWarn("vgId:%d TDMT_VND_CREATE_SMA received but deserialize failed since %s", pVnode->config.vgId,
              terrstr(terrno));
        return -1;
      }
      vDebug("vgId:%d TDMT_VND_CREATE_SMA msg received for %s:%" PRIi64, pVnode->config.vgId,
             vCreateSmaReq.tSma.indexName, vCreateSmaReq.tSma.indexUid);

      // record current timezone of server side
      vCreateSmaReq.tSma.timezoneInt = tsTimezone;

      if (metaCreateTSma(pVnode->pMeta, &vCreateSmaReq) < 0) {
        // TODO: handle error
        tdDestroyTSma(&vCreateSmaReq.tSma);
        return -1;
      }

      tsdbTSmaAdd(pVnode->pTsdb, 1);

      tdDestroyTSma(&vCreateSmaReq.tSma);
      // TODO: return directly or go on follow steps?
#endif
      //   if (tsdbCreateTSma(pVnode->pTsdb, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
      //     // TODO
      //   }
      // } break;
      // case TDMT_VND_CANCEL_SMA: {  // timeRangeSMA
      // } break;
      // case TDMT_VND_DROP_SMA: {  // timeRangeSMA
      //   if (tsdbDropTSma(pVnode->pTsdb, POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead))) < 0) {
      //     // TODO
      //   }
#if 0    
      tsdbTSmaSub(pVnode->pTsdb, 1);
      SVDropTSmaReq vDropSmaReq = {0};
      if (tDeserializeSVDropTSmaReq(POINTER_SHIFT(pMsg->pCont, sizeof(SMsgHead)), &vDropSmaReq) == NULL) {
        terrno = TSDB_CODE_OUT_OF_MEMORY;
        return -1;
      }

      // TODO: send msg to stream computing to drop tSma
      // if ((send msg to stream computing) < 0) {
      //   tdDestroyTSma(&vCreateSmaReq);
      //   return -1;
      // }
      // 

      if (metaDropTSma(pVnode->pMeta, vDropSmaReq.indexUid) < 0) {
        // TODO: handle error
        return -1;
      }

      if(tsdbDropTSmaData(pVnode->pTsdb, vDropSmaReq.indexUid) < 0) {
        // TODO: handle error
        return -1;
      }

      // TODO: return directly or go on follow steps?
#endif
    } break;
    default:
      ASSERT(0);
      break;
  }

  pVnode->state.applied = ver;

  // Check if it needs to commit
  if (0) {
    // tsem_wait(&(pVnode->canCommit));
    if (vnodeAsyncCommit(pVnode) < 0) {
      // TODO: handle error
    }
  }

#endif
  return 0;
}

int vnodeProcessQueryMsg(SVnode *pVnode, SRpcMsg *pMsg) {
#if 0
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
#endif
  return 0;
}

int vnodeProcessFetchMsg(SVnode *pVnode, SRpcMsg *pMsg, SQueueInfo *pInfo) {
#if 0
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
    case TDMT_VND_SHOW_TABLES:
      return qWorkerProcessShowMsg(pVnode, pVnode->pQuery, pMsg);
    case TDMT_VND_SHOW_TABLES_FETCH:
      return vnodeGetTableList(pVnode, pMsg);
      //      return qWorkerProcessShowFetchMsg(pVnode->pMeta, pVnode->pQuery, pMsg);
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
#endif
  return 0;
}

int vnodeProcessSyncReq(SVnode *pVnode, SRpcMsg *pMsg, SRpcMsg **pRsp) {
  /*vInfo("sync message is processed");*/
  return 0;
}