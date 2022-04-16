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

#define _DEFAULT_SOURCE
#include "mndShow.h"

#define SHOW_STEP_SIZE 100

static SShowObj *mndCreateShowObj(SMnode *pMnode, SShowReq *pReq);
static void      mndFreeShowObj(SShowObj *pShow);
static SShowObj *mndAcquireShowObj(SMnode *pMnode, int64_t showId);
static void      mndReleaseShowObj(SShowObj *pShow, bool forceRemove);
static bool      mndCheckRetrieveFinished(SShowObj *pShow);
static int32_t   mndProcessRetrieveSysTableReq(SNodeMsg *pReq);

int32_t mndInitShow(SMnode *pMnode) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;

  pMgmt->cache = taosCacheInit(TSDB_DATA_TYPE_INT, 5, true, (__cache_free_fn_t)mndFreeShowObj, "show");
  if (pMgmt->cache == NULL) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    mError("failed to alloc show cache since %s", terrstr());
    return -1;
  }

  mndSetMsgHandle(pMnode, TDMT_MND_SYSTABLE_RETRIEVE, mndProcessRetrieveSysTableReq);
  return 0;
}

void mndCleanupShow(SMnode *pMnode) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;
  if (pMgmt->cache != NULL) {
    taosCacheCleanup(pMgmt->cache);
    pMgmt->cache = NULL;
  }
}

static SShowObj *mndCreateShowObj(SMnode *pMnode, SShowReq *pReq) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;

  int64_t showId = atomic_add_fetch_64(&pMgmt->showId, 1);
  if (showId == 0) atomic_add_fetch_64(&pMgmt->showId, 1);

  int32_t  size = sizeof(SShowObj) + pReq->payloadLen;
  SShowObj showObj = {0};
  showObj.id = showId;
  showObj.pMnode = pMnode;
  showObj.type = pReq->type;
  showObj.payloadLen = pReq->payloadLen;
  memcpy(showObj.db, pReq->db, TSDB_DB_FNAME_LEN);

  int32_t   keepTime = tsShellActivityTimer * 6 * 1000;
  SShowObj *pShow = taosCachePut(pMgmt->cache, &showId, sizeof(int64_t), &showObj, size, keepTime);
  if (pShow == NULL) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    mError("show:0x%" PRIx64 ", failed to put into cache since %s", showId, terrstr());
    return NULL;
  }

  mTrace("show:0x%" PRIx64 ", is created, data:%p", showId, pShow);
  return pShow;
}

static void mndFreeShowObj(SShowObj *pShow) {
  SMnode    *pMnode = pShow->pMnode;
  SShowMgmt *pMgmt = &pMnode->showMgmt;

  ShowFreeIterFp freeFp = pMgmt->freeIterFps[pShow->type];
  if (freeFp != NULL) {
    if (pShow->pIter != NULL) {
      (*freeFp)(pMnode, pShow->pIter);
    }
  }

  mTrace("show:0x%" PRIx64 ", is destroyed, data:%p", pShow->id, pShow);
}

static SShowObj *mndAcquireShowObj(SMnode *pMnode, int64_t showId) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;

  SShowObj *pShow = taosCacheAcquireByKey(pMgmt->cache, &showId, sizeof(showId));
  if (pShow == NULL) {
    mError("show:0x%" PRIx64 ", already destroyed", showId);
    return NULL;
  }

  mTrace("show:0x%" PRIx64 ", acquired from cache, data:%p", pShow->id, pShow);
  return pShow;
}

static void mndReleaseShowObj(SShowObj *pShow, bool forceRemove) {
  if (pShow == NULL) return;
  mTrace("show:0x%" PRIx64 ", released from cache, data:%p force:%d", pShow->id, pShow, forceRemove);

  // A bug in tcache.c
  forceRemove = 0;

  SMnode    *pMnode = pShow->pMnode;
  SShowMgmt *pMgmt = &pMnode->showMgmt;
  taosCacheRelease(pMgmt->cache, (void **)(&pShow), forceRemove);
}

static int32_t mndProcessRetrieveSysTableReq(SNodeMsg *pReq) {
  SMnode    *pMnode = pReq->pNode;
  SShowMgmt *pMgmt = &pMnode->showMgmt;
  SShowObj  *pShow = NULL;
  int32_t    rowsToRead = SHOW_STEP_SIZE;
  int32_t    size = 0;
  int32_t    rowsRead = 0;

  SRetrieveTableReq retrieveReq = {0};
  if (tDeserializeSRetrieveTableReq(pReq->rpcMsg.pCont, pReq->rpcMsg.contLen, &retrieveReq) != 0) {
    terrno = TSDB_CODE_INVALID_MSG;
    return -1;
  }

  if (retrieveReq.showId == 0) {
    SShowReq req = {0};
    req.type = retrieveReq.type;
    strncpy(req.db, retrieveReq.db, tListLen(req.db));

    STableMetaRsp *pMeta = (STableMetaRsp *)taosHashGet(pMnode->infosMeta, retrieveReq.tb, strlen(retrieveReq.tb) + 1);
    if (pMeta == NULL) {
      terrno = TSDB_CODE_MND_INVALID_INFOS_TBL;
      mError("failed to process show-retrieve req:%p since %s", pShow, terrstr());
      return -1;
    }

    pShow = mndCreateShowObj(pMnode, &req);
    if (pShow == NULL) {
      terrno = TSDB_CODE_OUT_OF_MEMORY;
      mError("failed to process show-meta req since %s", terrstr());
      return -1;
    }

    pShow->pMeta = pMeta;
    pShow->numOfColumns = pShow->pMeta->numOfColumns;
    int32_t offset = 0;

    for (int32_t i = 0; i < pShow->pMeta->numOfColumns; ++i) {
      pShow->offset[i] = offset;

      int32_t bytes = pShow->pMeta->pSchemas[i].bytes;
      pShow->rowSize += bytes;
      pShow->bytes[i] = bytes;
      offset += bytes;
    }
  } else {
    pShow = mndAcquireShowObj(pMnode, retrieveReq.showId);
    if (pShow == NULL) {
      terrno = TSDB_CODE_MND_INVALID_SHOWOBJ;
      mError("failed to process show-retrieve req:%p since %s", pShow, terrstr());
      return -1;
    }
  }

  ShowRetrieveFp retrieveFp = pMgmt->retrieveFps[pShow->type];
  if (retrieveFp == NULL) {
    mndReleaseShowObj(pShow, false);
    terrno = TSDB_CODE_MSG_NOT_PROCESSED;
    mError("show:0x%" PRIx64 ", failed to retrieve data since %s", pShow->id, terrstr());
    return -1;
  }

  mDebug("show:0x%" PRIx64 ", start retrieve data, type:%d", pShow->id, pShow->type);

  int32_t      numOfCols = pShow->pMeta->numOfColumns;
  SSDataBlock *pBlock = taosMemoryCalloc(1, sizeof(SSDataBlock));
  pBlock->pDataBlock = taosArrayInit(numOfCols, sizeof(SColumnInfoData));
  pBlock->info.numOfCols = numOfCols;

  for (int32_t i = 0; i < numOfCols; ++i) {
    SColumnInfoData idata = {0};
    SSchema        *p = &pShow->pMeta->pSchemas[i];

    idata.info.bytes = p->bytes;
    idata.info.type = p->type;
    idata.info.colId = p->colId;

    taosArrayPush(pBlock->pDataBlock, &idata);
    if (IS_VAR_DATA_TYPE(p->type)) {
      pBlock->info.hasVarCol = true;
    }
  }

  blockDataEnsureCapacity(pBlock, rowsToRead);
  if (mndCheckRetrieveFinished(pShow)) {
    mDebug("show:0x%" PRIx64 ", read finished, numOfRows:%d", pShow->id, pShow->numOfRows);
    rowsRead = 0;
  } else {
    rowsRead = (*retrieveFp)(pReq, pShow, pBlock, rowsToRead);
    if (rowsRead < 0) {
      terrno = rowsRead;
      mDebug("show:0x%" PRIx64 ", retrieve completed", pShow->id);
      mndReleaseShowObj(pShow, true);
      return -1;
    }

    pBlock->info.rows = rowsRead;
    mDebug("show:0x%" PRIx64 ", stop retrieve data, rowsRead:%d numOfRows:%d", pShow->id, rowsRead, pShow->numOfRows);
  }

  size = sizeof(SRetrieveMetaTableRsp) + sizeof(int32_t) + sizeof(SSysTableSchema) * pShow->pMeta->numOfColumns +
         blockDataGetSize(pBlock) + blockDataGetSerialMetaSize(pBlock);

  SRetrieveMetaTableRsp *pRsp = rpcMallocCont(size);
  if (pRsp == NULL) {
    mndReleaseShowObj(pShow, false);
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    mError("show:0x%" PRIx64 ", failed to retrieve data since %s", pShow->id, terrstr());
    blockDataDestroy(pBlock);
    return -1;
  }

  pRsp->handle = htobe64(pShow->id);

  if (rowsRead > 0) {
    char    *pStart = pRsp->data;
    SSchema *ps = pShow->pMeta->pSchemas;

    *(int32_t *)pStart = htonl(pShow->pMeta->numOfColumns);
    pStart += sizeof(int32_t);  // number of columns

    for (int32_t i = 0; i < pShow->pMeta->numOfColumns; ++i) {
      SSysTableSchema *pSchema = (SSysTableSchema *)pStart;
      pSchema->bytes = htonl(ps[i].bytes);
      pSchema->colId = htons(ps[i].colId);
      pSchema->type = ps[i].type;

      pStart += sizeof(SSysTableSchema);
    }

    int32_t len = 0;
    blockCompressEncode(pBlock, pStart, &len, pShow->pMeta->numOfColumns, false);
  }

  pRsp->numOfRows = htonl(rowsRead);
  pRsp->precision = TSDB_TIME_PRECISION_MILLI;  // millisecond time precision
  pReq->pRsp = pRsp;
  pReq->rspLen = size;

  if (rowsRead == 0 || rowsRead < rowsToRead) {
    pRsp->completed = 1;
    mDebug("show:0x%" PRIx64 ", retrieve completed", pShow->id);
    mndReleaseShowObj(pShow, true);
  } else {
    mDebug("show:0x%" PRIx64 ", retrieve not completed yet", pShow->id);
    mndReleaseShowObj(pShow, false);
  }

  blockDataDestroy(pBlock);
  return TSDB_CODE_SUCCESS;
}

static bool mndCheckRetrieveFinished(SShowObj *pShow) {
  if (pShow->pIter == NULL && pShow->numOfRows != 0) {
    return true;
  }
  return false;
}

void mndAddShowRetrieveHandle(SMnode *pMnode, EShowType showType, ShowRetrieveFp fp) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;
  pMgmt->retrieveFps[showType] = fp;
}

void mndAddShowFreeIterHandle(SMnode *pMnode, EShowType showType, ShowFreeIterFp fp) {
  SShowMgmt *pMgmt = &pMnode->showMgmt;
  pMgmt->freeIterFps[showType] = fp;
}
