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

#include "trpc.h"
#include "query.h"
#include "tname.h"
#include "catalogInt.h"
#include "systable.h"

extern SCtgDebug gCTGDebug;
SCatalogMgmt gCtgMgmt = {0};

int32_t ctgGetDBVgInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* dbFName, SCtgDBCache** dbCache, SDBVgInfo **pInfo) {
  bool inCache = false;
  int32_t code = 0;

  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, dbFName, dbCache, &inCache));

  if (inCache) {
    return TSDB_CODE_SUCCESS;
  }

  SUseDbOutput DbOut = {0};
  SBuildUseDBInput input = {0};

  tstrncpy(input.db, dbFName, tListLen(input.db));
  input.vgVersion = CTG_DEFAULT_INVALID_VERSION;

  code = ctgGetDBVgInfoFromMnode(pCtg, pRpc, pMgmtEps, &input, &DbOut);
  if (code) {
    if (CTG_DB_NOT_EXIST(code) && input.vgVersion > CTG_DEFAULT_INVALID_VERSION) {
      ctgDebug("db no longer exist, dbFName:%s, dbId:%" PRIx64, input.db, input.dbId);
      ctgPutRmDBToQueue(pCtg, input.db, input.dbId);
    }

    CTG_ERR_RET(code);
  }

  CTG_ERR_JRET(ctgCloneVgInfo(DbOut.dbVgroup, pInfo));

  CTG_ERR_RET(ctgPutUpdateVgToQueue(pCtg, dbFName, DbOut.dbId, DbOut.dbVgroup, false));

  return TSDB_CODE_SUCCESS;

_return:

  taosMemoryFreeClear(*pInfo);
  *pInfo = DbOut.dbVgroup;
  
  CTG_RET(code);
}

int32_t ctgRefreshDBVgInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* dbFName) {
  bool inCache = false;
  int32_t code = 0;
  SCtgDBCache* dbCache = NULL;

  CTG_ERR_RET(ctgAcquireVgInfoFromCache(pCtg, dbFName, &dbCache, &inCache));

  SUseDbOutput DbOut = {0};
  SBuildUseDBInput input = {0};
  tstrncpy(input.db, dbFName, tListLen(input.db));

  if (inCache) {
    input.dbId = dbCache->dbId;

    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }
  
  input.vgVersion = CTG_DEFAULT_INVALID_VERSION;
  input.numOfTable = 0;

  code = ctgGetDBVgInfoFromMnode(pCtg, pRpc, pMgmtEps, &input, &DbOut);
  if (code) {
    if (CTG_DB_NOT_EXIST(code) && inCache) {
      ctgDebug("db no longer exist, dbFName:%s, dbId:%" PRIx64, input.db, input.dbId);
      ctgPutRmDBToQueue(pCtg, input.db, input.dbId);
    }

    CTG_ERR_RET(code);
  }

  CTG_ERR_RET(ctgPutUpdateVgToQueue(pCtg, dbFName, DbOut.dbId, DbOut.dbVgroup, true));

  return TSDB_CODE_SUCCESS;
}



int32_t ctgRefreshTbMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SName* pTableName, int32_t flag, STableMetaOutput **pOutput, bool syncReq) {
  if (NULL == pCtg || NULL == pTrans || NULL == pMgmtEps || NULL == pTableName) {
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }

  SVgroupInfo vgroupInfo = {0};
  int32_t code = 0;

  if (!CTG_FLAG_IS_SYS_DB(flag)) {
    CTG_ERR_RET(catalogGetTableHashVgroup(pCtg, pTrans, pMgmtEps, pTableName, &vgroupInfo));
  }

  STableMetaOutput  moutput = {0};
  STableMetaOutput *output = taosMemoryCalloc(1, sizeof(STableMetaOutput));
  if (NULL == output) {
    ctgError("malloc %d failed", (int32_t)sizeof(STableMetaOutput));
    CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
  }

  if (CTG_FLAG_IS_SYS_DB(flag)) {
    ctgDebug("will refresh tbmeta, supposed in information_schema, tbName:%s", tNameGetTableName(pTableName));

    CTG_ERR_JRET(ctgGetTbMetaFromMnodeImpl(pCtg, pTrans, pMgmtEps, (char *)pTableName->dbname, (char *)pTableName->tname, output));
  } else if (CTG_FLAG_IS_STB(flag)) {
    ctgDebug("will refresh tbmeta, supposed to be stb, tbName:%s", tNameGetTableName(pTableName));

    // if get from mnode failed, will not try vnode
    CTG_ERR_JRET(ctgGetTbMetaFromMnode(pCtg, pTrans, pMgmtEps, pTableName, output));

    if (CTG_IS_META_NULL(output->metaType)) {
      CTG_ERR_JRET(ctgGetTbMetaFromVnode(pCtg, pTrans, pMgmtEps, pTableName, &vgroupInfo, output));
    }
  } else {
    ctgDebug("will refresh tbmeta, not supposed to be stb, tbName:%s, flag:%d", tNameGetTableName(pTableName), flag);

    // if get from vnode failed or no table meta, will not try mnode
    CTG_ERR_JRET(ctgGetTbMetaFromVnode(pCtg, pTrans, pMgmtEps, pTableName, &vgroupInfo, output));

    if (CTG_IS_META_TABLE(output->metaType) && TSDB_SUPER_TABLE == output->tbMeta->tableType) {
      ctgDebug("will continue to refresh tbmeta since got stb, tbName:%s", tNameGetTableName(pTableName));

      taosMemoryFreeClear(output->tbMeta);
      
      CTG_ERR_JRET(ctgGetTbMetaFromMnodeImpl(pCtg, pTrans, pMgmtEps, output->dbFName, output->tbName, output));
    } else if (CTG_IS_META_BOTH(output->metaType)) {
      int32_t exist = 0;
      if (!CTG_FLAG_IS_FORCE_UPDATE(flag)) {
        CTG_ERR_JRET(ctgTbMetaExistInCache(pCtg, output->dbFName, output->tbName, &exist));
      }

      if (0 == exist) {
        CTG_ERR_JRET(ctgGetTbMetaFromMnodeImpl(pCtg, pTrans, pMgmtEps, output->dbFName, output->tbName, &moutput));

        if (CTG_IS_META_NULL(moutput.metaType)) {
          SET_META_TYPE_NULL(output->metaType);
        }
        
        taosMemoryFreeClear(output->tbMeta);
        output->tbMeta = moutput.tbMeta;
        moutput.tbMeta = NULL;
      } else {
        taosMemoryFreeClear(output->tbMeta);
        
        SET_META_TYPE_CTABLE(output->metaType); 
      }
    }
  }

  if (CTG_IS_META_NULL(output->metaType)) {
    ctgError("no tbmeta got, tbNmae:%s", tNameGetTableName(pTableName));
    catalogRemoveTableMeta(pCtg, pTableName);
    CTG_ERR_JRET(CTG_ERR_CODE_TABLE_NOT_EXIST);
  }

  if (CTG_IS_META_TABLE(output->metaType)) {
    ctgDebug("tbmeta got, dbFName:%s, tbName:%s, tbType:%d", output->dbFName, output->tbName, output->tbMeta->tableType);
  } else {
    ctgDebug("tbmeta got, dbFName:%s, tbName:%s, tbType:%d, stbMetaGot:%d", output->dbFName, output->ctbName, output->ctbMeta.tableType, CTG_IS_META_BOTH(output->metaType));
  }

  if (pOutput) {
    CTG_ERR_JRET(ctgCloneMetaOutput(output, pOutput));
  }

  CTG_ERR_JRET(ctgPutUpdateTbToQueue(pCtg, output, syncReq));

  return TSDB_CODE_SUCCESS;

_return:

  taosMemoryFreeClear(output->tbMeta);
  taosMemoryFreeClear(output);
  
  CTG_RET(code);
}

int32_t ctgGetTbMeta(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const SName* pTableName, STableMeta** pTableMeta, int32_t flag) {
  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == pTableName || NULL == pTableMeta) {
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }

  bool inCache = false;
  int32_t code = 0;
  uint64_t dbId = 0;
  uint64_t suid = 0;
  STableMetaOutput *output = NULL;

  if (CTG_IS_SYS_DBNAME(pTableName->dbname)) {
    CTG_FLAG_SET_SYS_DB(flag);
  }

  CTG_ERR_RET(ctgGetTbMetaFromCache(pCtg, pTableName, pTableMeta, &inCache, flag, &dbId));

  int32_t tbType = 0;

  if (inCache) {
    if (CTG_FLAG_MATCH_STB(flag, (*pTableMeta)->tableType) && ((!CTG_FLAG_IS_FORCE_UPDATE(flag)) || (CTG_FLAG_IS_SYS_DB(flag)))) {
      goto _return;
    }

    tbType = (*pTableMeta)->tableType;
    suid = (*pTableMeta)->suid;

    taosMemoryFreeClear(*pTableMeta);
  }

  if (CTG_FLAG_IS_UNKNOWN_STB(flag)) {
    CTG_FLAG_SET_STB(flag, tbType);
  }


  while (true) {
    CTG_ERR_JRET(ctgRefreshTbMeta(pCtg, pRpc, pMgmtEps, pTableName, flag, &output, false));

    if (CTG_IS_META_TABLE(output->metaType)) {
      *pTableMeta = output->tbMeta;
      goto _return;
    }

    if (CTG_IS_META_BOTH(output->metaType)) {
      memcpy(output->tbMeta, &output->ctbMeta, sizeof(output->ctbMeta));
      
      *pTableMeta = output->tbMeta;
      goto _return;
    }

    if ((!CTG_IS_META_CTABLE(output->metaType)) || output->tbMeta) {
      ctgError("invalid metaType:%d", output->metaType);
      taosMemoryFreeClear(output->tbMeta);
      CTG_ERR_JRET(TSDB_CODE_CTG_INTERNAL_ERROR);
    }

    // HANDLE ONLY CHILD TABLE META

    SName stbName = *pTableName;
    strcpy(stbName.tname, output->tbName);
    
    CTG_ERR_JRET(ctgGetTbMetaFromCache(pCtg, &stbName, pTableMeta, &inCache, flag, NULL));
    if (!inCache) {
      ctgDebug("stb no longer exist, dbFName:%s, tbName:%s", output->dbFName, pTableName->tname);
      continue;
    }

    memcpy(*pTableMeta, &output->ctbMeta, sizeof(output->ctbMeta));

    break;
  }

_return:

  if (CTG_TABLE_NOT_EXIST(code) && inCache) {
    char dbFName[TSDB_DB_FNAME_LEN] = {0};
    if (CTG_FLAG_IS_SYS_DB(flag)) {
      strcpy(dbFName, pTableName->dbname);
    } else {
      tNameGetFullDbName(pTableName, dbFName);
    }

    if (TSDB_SUPER_TABLE == tbType) {
      ctgPutRmStbToQueue(pCtg, dbFName, dbId, pTableName->tname, suid, false);
    } else {
      ctgPutRmTbToQueue(pCtg, dbFName, dbId, pTableName->tname, false);
    }
  }

  taosMemoryFreeClear(output);

  if (*pTableMeta) {
    ctgDebug("tbmeta returned, tbName:%s, tbType:%d", pTableName->tname, (*pTableMeta)->tableType);
    ctgdShowTableMeta(pCtg, pTableName->tname, *pTableMeta);
  }

  CTG_RET(code);
}

int32_t ctgChkAuth(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* user, const char* dbFName, AUTH_TYPE type, bool *pass) {
  bool inCache = false;
  int32_t code = 0;
  
  *pass = false;
  
  CTG_ERR_RET(ctgChkAuthFromCache(pCtg, user, dbFName, type, &inCache, pass));

  if (inCache) {
    return TSDB_CODE_SUCCESS;
  }

  SGetUserAuthRsp authRsp = {0};
  CTG_ERR_RET(ctgGetUserDbAuthFromMnode(pCtg, pRpc, pMgmtEps, user, &authRsp));
  
  if (authRsp.superAuth) {
    *pass = true;
    goto _return;
  }

  if (authRsp.createdDbs && taosHashGet(authRsp.createdDbs, dbFName, strlen(dbFName))) {
    *pass = true;
    goto _return;
  }

  if (authRsp.readDbs && taosHashGet(authRsp.readDbs, dbFName, strlen(dbFName)) && type == AUTH_TYPE_READ) {
    *pass = true;
  }

  if (authRsp.writeDbs && taosHashGet(authRsp.writeDbs, dbFName, strlen(dbFName)) && type == AUTH_TYPE_WRITE) {
    *pass = true;
  }

_return:

  ctgPutUpdateUserToQueue(pCtg, &authRsp, false);

  return TSDB_CODE_SUCCESS;
}

int32_t ctgGetTbDistVgInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const SName* pTableName, SArray** pVgList) {
  STableMeta *tbMeta = NULL;
  int32_t code = 0;
  SVgroupInfo vgroupInfo = {0};
  SCtgDBCache* dbCache = NULL;
  SArray *vgList = NULL;
  SDBVgInfo *vgInfo = NULL;

  *pVgList = NULL;
  
  CTG_ERR_JRET(ctgGetTbMeta(pCtg, pRpc, pMgmtEps, pTableName, &tbMeta, CTG_FLAG_UNKNOWN_STB));

  char db[TSDB_DB_FNAME_LEN] = {0};
  tNameGetFullDbName(pTableName, db);

  SHashObj *vgHash = NULL;  
  CTG_ERR_JRET(ctgGetDBVgInfo(pCtg, pRpc, pMgmtEps, db, &dbCache, &vgInfo));

  if (dbCache) {
    vgHash = dbCache->vgInfo->vgHash;
  } else {
    vgHash = vgInfo->vgHash;
  }

  if (tbMeta->tableType == TSDB_SUPER_TABLE) {
    CTG_ERR_JRET(ctgGenerateVgList(pCtg, vgHash, pVgList));
  } else {
    // USE HASH METHOD INSTEAD OF VGID IN TBMETA
    ctgError("invalid method to get none stb vgInfo, tbType:%d", tbMeta->tableType);
    CTG_ERR_JRET(TSDB_CODE_CTG_INVALID_INPUT);
    
#if 0  
    int32_t vgId = tbMeta->vgId;
    if (taosHashGetDup(vgHash, &vgId, sizeof(vgId), &vgroupInfo) != 0) {
      ctgWarn("table's vgId not found in vgroup list, vgId:%d, tbName:%s", vgId, tNameGetTableName(pTableName));
      CTG_ERR_JRET(TSDB_CODE_CTG_VG_META_MISMATCH);
    }

    vgList = taosArrayInit(1, sizeof(SVgroupInfo));
    if (NULL == vgList) {
      ctgError("taosArrayInit %d failed", (int32_t)sizeof(SVgroupInfo));
      CTG_ERR_JRET(TSDB_CODE_CTG_MEM_ERROR);    
    }

    if (NULL == taosArrayPush(vgList, &vgroupInfo)) {
      ctgError("taosArrayPush vgroupInfo to array failed, vgId:%d, tbName:%s", vgId, tNameGetTableName(pTableName));
      CTG_ERR_JRET(TSDB_CODE_CTG_INTERNAL_ERROR);
    }

    *pVgList = vgList;
    vgList = NULL;
#endif    
  }

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  taosMemoryFreeClear(tbMeta);

  if (vgInfo) {
    taosHashCleanup(vgInfo->vgHash);
    taosMemoryFreeClear(vgInfo);
  }

  if (vgList) {
    taosArrayDestroy(vgList);
    vgList = NULL;
  }

  CTG_RET(code);
}

int32_t catalogInit(SCatalogCfg *cfg) {
  if (gCtgMgmt.pCluster) {
    qError("catalog already initialized");
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }

  atomic_store_8((int8_t*)&gCtgMgmt.exit, false);

  if (cfg) {
    memcpy(&gCtgMgmt.cfg, cfg, sizeof(*cfg));

    if (gCtgMgmt.cfg.maxDBCacheNum == 0) {
      gCtgMgmt.cfg.maxDBCacheNum = CTG_DEFAULT_CACHE_DB_NUMBER;
    }

    if (gCtgMgmt.cfg.maxTblCacheNum == 0) {
      gCtgMgmt.cfg.maxTblCacheNum = CTG_DEFAULT_CACHE_TBLMETA_NUMBER;
    }

    if (gCtgMgmt.cfg.dbRentSec == 0) {
      gCtgMgmt.cfg.dbRentSec = CTG_DEFAULT_RENT_SECOND;
    }

    if (gCtgMgmt.cfg.stbRentSec == 0) {
      gCtgMgmt.cfg.stbRentSec = CTG_DEFAULT_RENT_SECOND;
    }
  } else {
    gCtgMgmt.cfg.maxDBCacheNum = CTG_DEFAULT_CACHE_DB_NUMBER;
    gCtgMgmt.cfg.maxTblCacheNum = CTG_DEFAULT_CACHE_TBLMETA_NUMBER;
    gCtgMgmt.cfg.dbRentSec = CTG_DEFAULT_RENT_SECOND;
    gCtgMgmt.cfg.stbRentSec = CTG_DEFAULT_RENT_SECOND;
  }

  gCtgMgmt.pCluster = taosHashInit(CTG_DEFAULT_CACHE_CLUSTER_NUMBER, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BIGINT), false, HASH_ENTRY_LOCK);
  if (NULL == gCtgMgmt.pCluster) {
    qError("taosHashInit %d cluster cache failed", CTG_DEFAULT_CACHE_CLUSTER_NUMBER);
    CTG_ERR_RET(TSDB_CODE_CTG_INTERNAL_ERROR);
  }

  if (tsem_init(&gCtgMgmt.queue.reqSem, 0, 0)) {
    qError("tsem_init failed, error:%s", tstrerror(TAOS_SYSTEM_ERROR(errno)));
    CTG_ERR_RET(TSDB_CODE_CTG_SYS_ERROR);
  }
  
  if (tsem_init(&gCtgMgmt.queue.rspSem, 0, 0)) {
    qError("tsem_init failed, error:%s", tstrerror(TAOS_SYSTEM_ERROR(errno)));
    CTG_ERR_RET(TSDB_CODE_CTG_SYS_ERROR);
  }

  gCtgMgmt.queue.head = taosMemoryCalloc(1, sizeof(SCtgQNode));
  if (NULL == gCtgMgmt.queue.head) {
    qError("calloc %d failed", (int32_t)sizeof(SCtgQNode));
    CTG_ERR_RET(TSDB_CODE_QRY_OUT_OF_MEMORY);
  }
  gCtgMgmt.queue.tail = gCtgMgmt.queue.head;

  CTG_ERR_RET(ctgStartUpdateThread());

  qDebug("catalog initialized, maxDb:%u, maxTbl:%u, dbRentSec:%u, stbRentSec:%u", gCtgMgmt.cfg.maxDBCacheNum, gCtgMgmt.cfg.maxTblCacheNum, gCtgMgmt.cfg.dbRentSec, gCtgMgmt.cfg.stbRentSec);

  return TSDB_CODE_SUCCESS;
}

int32_t catalogGetHandle(uint64_t clusterId, SCatalog** catalogHandle) {
  if (NULL == catalogHandle) {
    CTG_ERR_RET(TSDB_CODE_CTG_INVALID_INPUT);
  }

  if (NULL == gCtgMgmt.pCluster) {
    qError("catalog cluster cache are not ready, clusterId:%"PRIx64, clusterId);
    CTG_ERR_RET(TSDB_CODE_CTG_NOT_READY);
  }

  int32_t code = 0;
  SCatalog *clusterCtg = NULL;

  while (true) {
    SCatalog **ctg = (SCatalog **)taosHashGet(gCtgMgmt.pCluster, (char*)&clusterId, sizeof(clusterId));

    if (ctg && (*ctg)) {
      *catalogHandle = *ctg;
      qDebug("got catalog handle from cache, clusterId:%"PRIx64", CTG:%p", clusterId, *ctg);
      return TSDB_CODE_SUCCESS;
    }

    clusterCtg = taosMemoryCalloc(1, sizeof(SCatalog));
    if (NULL == clusterCtg) {
      qError("calloc %d failed", (int32_t)sizeof(SCatalog));
      CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
    }

    clusterCtg->clusterId = clusterId;

    CTG_ERR_JRET(ctgMetaRentInit(&clusterCtg->dbRent, gCtgMgmt.cfg.dbRentSec, CTG_RENT_DB));
    CTG_ERR_JRET(ctgMetaRentInit(&clusterCtg->stbRent, gCtgMgmt.cfg.stbRentSec, CTG_RENT_STABLE));

    clusterCtg->dbCache = taosHashInit(gCtgMgmt.cfg.maxDBCacheNum, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BINARY), false, HASH_ENTRY_LOCK);
    if (NULL == clusterCtg->dbCache) {
      qError("taosHashInit %d dbCache failed", CTG_DEFAULT_CACHE_DB_NUMBER);
      CTG_ERR_JRET(TSDB_CODE_CTG_MEM_ERROR);
    }

    SHashObj *metaCache = taosHashInit(gCtgMgmt.cfg.maxTblCacheNum, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BINARY), true, HASH_ENTRY_LOCK);
    if (NULL == metaCache) {
      qError("taosHashInit failed, num:%d", gCtgMgmt.cfg.maxTblCacheNum);
      CTG_ERR_RET(TSDB_CODE_CTG_MEM_ERROR);
    }
    
    code = taosHashPut(gCtgMgmt.pCluster, &clusterId, sizeof(clusterId), &clusterCtg, POINTER_BYTES);
    if (code) {
      if (HASH_NODE_EXIST(code)) {
        ctgFreeHandle(clusterCtg);
        continue;
      }
      
      qError("taosHashPut CTG to cache failed, clusterId:%"PRIx64, clusterId);
      CTG_ERR_JRET(TSDB_CODE_CTG_INTERNAL_ERROR);
    }

    qDebug("add CTG to cache, clusterId:%"PRIx64", CTG:%p", clusterId, clusterCtg);

    break;
  }

  *catalogHandle = clusterCtg;

  CTG_CACHE_STAT_ADD(clusterNum, 1);
  
  return TSDB_CODE_SUCCESS;

_return:

  ctgFreeHandle(clusterCtg);
  
  CTG_RET(code);
}

void catalogFreeHandle(SCatalog* pCtg) {
  if (NULL == pCtg) {
    return;
  }

  if (taosHashRemove(gCtgMgmt.pCluster, &pCtg->clusterId, sizeof(pCtg->clusterId))) {
    ctgWarn("taosHashRemove from cluster failed, may already be freed, clusterId:%"PRIx64, pCtg->clusterId);
    return;
  }

  CTG_CACHE_STAT_SUB(clusterNum, 1);

  uint64_t clusterId = pCtg->clusterId;
  
  ctgFreeHandle(pCtg);
  
  ctgInfo("handle freed, culsterId:%"PRIx64, clusterId);
}

int32_t catalogGetDBVgVersion(SCatalog* pCtg, const char* dbFName, int32_t* version, int64_t* dbId, int32_t *tableNum) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == dbFName || NULL == version || NULL == dbId) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  SCtgDBCache *dbCache = NULL;
  bool inCache = false;
  int32_t code = 0;

  CTG_ERR_JRET(ctgAcquireVgInfoFromCache(pCtg, dbFName, &dbCache, &inCache));
  if (!inCache) {
    *version = CTG_DEFAULT_INVALID_VERSION;
    CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  }

  *version = dbCache->vgInfo->vgVersion;
  *dbId = dbCache->dbId;
  *tableNum = dbCache->vgInfo->numOfTable;

  ctgReleaseVgInfo(dbCache);
  ctgReleaseDBCache(pCtg, dbCache);

  ctgDebug("Got db vgVersion from cache, dbFName:%s, vgVersion:%d", dbFName, *version);

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);

_return:

  CTG_API_LEAVE(code);
}

int32_t catalogGetDBVgInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* dbFName, SArray** vgroupList) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == dbFName || NULL == pRpc || NULL == pMgmtEps || NULL == vgroupList) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  SCtgDBCache* dbCache = NULL;
  int32_t code = 0;
  SArray *vgList = NULL;
  SHashObj *vgHash = NULL;
  SDBVgInfo *vgInfo = NULL;
  CTG_ERR_JRET(ctgGetDBVgInfo(pCtg, pRpc, pMgmtEps, dbFName, &dbCache, &vgInfo));
  if (dbCache) {
    vgHash = dbCache->vgInfo->vgHash;
  } else {
    vgHash = vgInfo->vgHash;
  }

  CTG_ERR_JRET(ctgGenerateVgList(pCtg, vgHash, &vgList));

  *vgroupList = vgList;
  vgList = NULL;

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  if (vgInfo) {
    taosHashCleanup(vgInfo->vgHash);
    taosMemoryFreeClear(vgInfo);
  }

  CTG_API_LEAVE(code);  
}


int32_t catalogUpdateDBVgInfo(SCatalog* pCtg, const char* dbFName, uint64_t dbId, SDBVgInfo* dbInfo) {
  CTG_API_ENTER();

  int32_t code = 0;
  
  if (NULL == pCtg || NULL == dbFName || NULL == dbInfo) {
    ctgFreeVgInfo(dbInfo);
    CTG_ERR_JRET(TSDB_CODE_CTG_INVALID_INPUT);
  }

  code = ctgPutUpdateVgToQueue(pCtg, dbFName, dbId, dbInfo, false);

_return:

  CTG_API_LEAVE(code);
}


int32_t catalogRemoveDB(SCatalog* pCtg, const char* dbFName, uint64_t dbId) {
  CTG_API_ENTER();

  int32_t code = 0;
  
  if (NULL == pCtg || NULL == dbFName) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  if (NULL == pCtg->dbCache) {
    CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  }

  CTG_ERR_JRET(ctgPutRmDBToQueue(pCtg, dbFName, dbId));

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  
_return:

  CTG_API_LEAVE(code);
}

int32_t catalogUpdateVgEpSet(SCatalog* pCtg, const char* dbFName, int32_t vgId, SEpSet *epSet) {
  return 0;
}

int32_t catalogRemoveTableMeta(SCatalog* pCtg, const SName* pTableName) {
  CTG_API_ENTER();

  int32_t code = 0;
  
  if (NULL == pCtg || NULL == pTableName) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  if (NULL == pCtg->dbCache) {
    CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  }

  STableMeta *tblMeta = NULL;
  bool inCache = false;
  uint64_t dbId = 0;
  CTG_ERR_JRET(ctgGetTbMetaFromCache(pCtg, pTableName, &tblMeta, &inCache, 0, &dbId));

  if (!inCache) {
    ctgDebug("table already not in cache, db:%s, tblName:%s", pTableName->dbname, pTableName->tname);
    goto _return;
  }

  char dbFName[TSDB_DB_FNAME_LEN];
  tNameGetFullDbName(pTableName, dbFName);
  
  if (TSDB_SUPER_TABLE == tblMeta->tableType) {
    CTG_ERR_JRET(ctgPutRmStbToQueue(pCtg, dbFName, dbId, pTableName->tname, tblMeta->suid, true));
  } else {
    CTG_ERR_JRET(ctgPutRmTbToQueue(pCtg, dbFName, dbId, pTableName->tname, true));
  }

 
_return:

  taosMemoryFreeClear(tblMeta);

  CTG_API_LEAVE(code);
}


int32_t catalogRemoveStbMeta(SCatalog* pCtg, const char* dbFName, uint64_t dbId, const char* stbName, uint64_t suid) {
  CTG_API_ENTER();

  int32_t code = 0;
  
  if (NULL == pCtg || NULL == dbFName || NULL == stbName) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  if (NULL == pCtg->dbCache) {
    CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  }

  CTG_ERR_JRET(ctgPutRmStbToQueue(pCtg, dbFName, dbId, stbName, suid, true));

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);
  
_return:

  CTG_API_LEAVE(code);
}

int32_t catalogGetIndexMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SName* pTableName, const char *pIndexName, SIndexMeta** pIndexMeta) {
  return 0;
}

int32_t catalogGetTableMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SName* pTableName, STableMeta** pTableMeta) {
  CTG_API_ENTER();

  CTG_API_LEAVE(ctgGetTbMeta(pCtg, pTrans, pMgmtEps, pTableName, pTableMeta, CTG_FLAG_UNKNOWN_STB));
}

int32_t catalogGetSTableMeta(SCatalog* pCtg, void * pTrans, const SEpSet* pMgmtEps, const SName* pTableName, STableMeta** pTableMeta) {
  CTG_API_ENTER();

  CTG_API_LEAVE(ctgGetTbMeta(pCtg, pTrans, pMgmtEps, pTableName, pTableMeta, CTG_FLAG_STB));
}

int32_t catalogUpdateSTableMeta(SCatalog* pCtg, STableMetaRsp *rspMsg) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == rspMsg) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  STableMetaOutput *output = taosMemoryCalloc(1, sizeof(STableMetaOutput));
  if (NULL == output) {
    ctgError("malloc %d failed", (int32_t)sizeof(STableMetaOutput));
    CTG_API_LEAVE(TSDB_CODE_CTG_MEM_ERROR);
  }
  
  int32_t code = 0;

  strcpy(output->dbFName, rspMsg->dbFName);
  strcpy(output->tbName, rspMsg->tbName);

  output->dbId = rspMsg->dbId;
  
  SET_META_TYPE_TABLE(output->metaType);
  
  CTG_ERR_JRET(queryCreateTableMetaFromMsg(rspMsg, true, &output->tbMeta));

  CTG_ERR_JRET(ctgPutUpdateTbToQueue(pCtg, output, false));

  CTG_API_LEAVE(code);
  
_return:

  taosMemoryFreeClear(output->tbMeta);
  taosMemoryFreeClear(output);
  
  CTG_API_LEAVE(code);
}

int32_t catalogRefreshDBVgInfo(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const char* dbFName) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == pTrans || NULL == pMgmtEps || NULL == dbFName) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgRefreshDBVgInfo(pCtg, pTrans, pMgmtEps, dbFName));
}

int32_t catalogRefreshTableMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SName* pTableName, int32_t isSTable) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == pTrans || NULL == pMgmtEps || NULL == pTableName) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgRefreshTbMeta(pCtg, pTrans, pMgmtEps, pTableName, CTG_FLAG_FORCE_UPDATE | CTG_FLAG_MAKE_STB(isSTable), NULL, true));
}

int32_t catalogRefreshGetTableMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SName* pTableName, STableMeta** pTableMeta, int32_t isSTable) {
  CTG_API_ENTER();

  CTG_API_LEAVE(ctgGetTbMeta(pCtg, pTrans, pMgmtEps, pTableName, pTableMeta, CTG_FLAG_FORCE_UPDATE | CTG_FLAG_MAKE_STB(isSTable)));
}

int32_t catalogGetTableDistVgInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const SName* pTableName, SArray** pVgList) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == pTableName || NULL == pVgList) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  if (CTG_IS_SYS_DBNAME(pTableName->dbname)) {
    ctgError("no valid vgInfo for db, dbname:%s", pTableName->dbname);
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  int32_t code = 0;

  while (true) {
    code = ctgGetTbDistVgInfo(pCtg, pRpc, pMgmtEps, pTableName, pVgList);
    if (code) {
      if (TSDB_CODE_CTG_VG_META_MISMATCH == code) {
        CTG_ERR_JRET(ctgRefreshTbMeta(pCtg, pRpc, pMgmtEps, pTableName, CTG_FLAG_FORCE_UPDATE | CTG_FLAG_MAKE_STB(CTG_FLAG_UNKNOWN_STB), NULL, true));

        char dbFName[TSDB_DB_FNAME_LEN] = {0};
        tNameGetFullDbName(pTableName, dbFName);        
        CTG_ERR_JRET(ctgRefreshDBVgInfo(pCtg, pRpc, pMgmtEps, dbFName));
        
        continue;
      }
    }

    break;
  }

_return:

  CTG_API_LEAVE(code);
}


int32_t catalogGetTableHashVgroup(SCatalog *pCtg, void *pTrans, const SEpSet *pMgmtEps, const SName *pTableName, SVgroupInfo *pVgroup) {
  CTG_API_ENTER();

  if (CTG_IS_SYS_DBNAME(pTableName->dbname)) {
    ctgError("no valid vgInfo for db, dbname:%s", pTableName->dbname);
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  SCtgDBCache* dbCache = NULL;
  int32_t code = 0;
  char db[TSDB_DB_FNAME_LEN] = {0};
  tNameGetFullDbName(pTableName, db);

  SDBVgInfo *vgInfo = NULL;
  CTG_ERR_JRET(ctgGetDBVgInfo(pCtg, pTrans, pMgmtEps, db, &dbCache, &vgInfo));

  CTG_ERR_JRET(ctgGetVgInfoFromHashValue(pCtg, vgInfo ? vgInfo : dbCache->vgInfo, pTableName, pVgroup));

_return:

  if (dbCache) {
    ctgReleaseVgInfo(dbCache);
    ctgReleaseDBCache(pCtg, dbCache);
  }

  if (vgInfo) {
    taosHashCleanup(vgInfo->vgHash);
    taosMemoryFreeClear(vgInfo);
  }

  CTG_API_LEAVE(code);
}


int32_t catalogGetAllMeta(SCatalog* pCtg, void *pTrans, const SEpSet* pMgmtEps, const SCatalogReq* pReq, SMetaData* pRsp) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == pTrans || NULL == pMgmtEps || NULL == pReq || NULL == pRsp) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  int32_t code = 0;
  pRsp->pTableMeta = NULL;

  if (pReq->pTableName) {
    int32_t tbNum = (int32_t)taosArrayGetSize(pReq->pTableName);
    if (tbNum <= 0) {
      ctgError("empty table name list, tbNum:%d", tbNum);
      CTG_ERR_JRET(TSDB_CODE_CTG_INVALID_INPUT);
    }

    pRsp->pTableMeta = taosArrayInit(tbNum, POINTER_BYTES);
    if (NULL == pRsp->pTableMeta) {
      ctgError("taosArrayInit %d failed", tbNum);
      CTG_ERR_JRET(TSDB_CODE_CTG_MEM_ERROR);
    }
    
    for (int32_t i = 0; i < tbNum; ++i) {
      SName *name = taosArrayGet(pReq->pTableName, i);
      STableMeta *pTableMeta = NULL;
      
      CTG_ERR_JRET(ctgGetTbMeta(pCtg, pTrans, pMgmtEps, name, &pTableMeta, CTG_FLAG_UNKNOWN_STB));

      if (NULL == taosArrayPush(pRsp->pTableMeta, &pTableMeta)) {
        ctgError("taosArrayPush failed, idx:%d", i);
        taosMemoryFreeClear(pTableMeta);
        CTG_ERR_JRET(TSDB_CODE_CTG_MEM_ERROR);
      }
    }
  }

  if (pReq->qNodeRequired) {
    pRsp->pQnodeList = taosArrayInit(10, sizeof(SQueryNodeAddr));
    CTG_ERR_JRET(ctgGetQnodeListFromMnode(pCtg, pTrans, pMgmtEps, pRsp->pQnodeList));
  }

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);

_return:  

  if (pRsp->pTableMeta) {
    int32_t aSize = taosArrayGetSize(pRsp->pTableMeta);
    for (int32_t i = 0; i < aSize; ++i) {
      STableMeta *pMeta = taosArrayGetP(pRsp->pTableMeta, i);
      taosMemoryFreeClear(pMeta);
    }
    
    taosArrayDestroy(pRsp->pTableMeta);
    pRsp->pTableMeta = NULL;
  }
  
  CTG_API_LEAVE(code);
}

int32_t catalogGetQnodeList(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, SArray* pQnodeList) {
  CTG_API_ENTER();
  
  int32_t code = 0;
  if (NULL == pCtg || NULL == pRpc  || NULL == pMgmtEps || NULL == pQnodeList) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_ERR_JRET(ctgGetQnodeListFromMnode(pCtg, pRpc, pMgmtEps, pQnodeList));

_return:

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);
}

int32_t catalogGetExpiredSTables(SCatalog* pCtg, SSTableMetaVersion **stables, uint32_t *num) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == stables || NULL == num) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgMetaRentGet(&pCtg->stbRent, (void **)stables, num, sizeof(SSTableMetaVersion)));
}

int32_t catalogGetExpiredDBs(SCatalog* pCtg, SDbVgVersion **dbs, uint32_t *num) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == dbs || NULL == num) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgMetaRentGet(&pCtg->dbRent, (void **)dbs, num, sizeof(SDbVgVersion)));
}

int32_t catalogGetExpiredUsers(SCatalog* pCtg, SUserAuthVersion **users, uint32_t *num) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == users || NULL == num) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  *num = taosHashGetSize(pCtg->userCache);
  if (*num > 0) {
    *users = taosMemoryCalloc(*num, sizeof(SUserAuthVersion));
    if (NULL == *users) {
      ctgError("calloc %d userAuthVersion failed", *num);
      CTG_API_LEAVE(TSDB_CODE_OUT_OF_MEMORY);
    }
  }

  uint32_t i = 0;
  SCtgUserAuth *pAuth = taosHashIterate(pCtg->userCache, NULL);
  while (pAuth != NULL) {
    void *key = taosHashGetKey(pAuth, NULL);
    strncpy((*users)[i].user, key, sizeof((*users)[i].user));
    (*users)[i].version = pAuth->version;
    pAuth = taosHashIterate(pCtg->userCache, pAuth);
  }

  CTG_API_LEAVE(TSDB_CODE_SUCCESS);
}


int32_t catalogGetDBCfg(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* dbFName, SDbCfgInfo* pDbCfg) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == dbFName || NULL == pDbCfg) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgGetDBCfgFromMnode(pCtg, pRpc, pMgmtEps, dbFName, pDbCfg));
}

int32_t catalogGetIndexInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* indexName, SIndexInfo* pInfo) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == indexName || NULL == pInfo) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgGetIndexInfoFromMnode(pCtg, pRpc, pMgmtEps, indexName, pInfo));
}

int32_t catalogGetUdfInfo(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* funcName, SFuncInfo** pInfo) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == funcName || NULL == pInfo) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  int32_t code = 0;
  *pInfo = taosMemoryMalloc(sizeof(SFuncInfo));
  if (NULL == *pInfo) {
    CTG_API_LEAVE(TSDB_CODE_OUT_OF_MEMORY);
  }

  CTG_ERR_JRET(ctgGetUdfInfoFromMnode(pCtg, pRpc, pMgmtEps, funcName, pInfo));
  
_return:

  if (code) {
    taosMemoryFreeClear(*pInfo);    
  }
  
  CTG_API_LEAVE(code);
}

int32_t catalogChkAuth(SCatalog* pCtg, void *pRpc, const SEpSet* pMgmtEps, const char* user, const char* dbFName, AUTH_TYPE type, bool *pass) {
  CTG_API_ENTER();
  
  if (NULL == pCtg || NULL == pRpc || NULL == pMgmtEps || NULL == user || NULL == dbFName || NULL == pass) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  int32_t code = 0;
  CTG_ERR_JRET(ctgChkAuth(pCtg, pRpc, pMgmtEps, user, dbFName, type, pass));
  
_return:

  CTG_API_LEAVE(code);
}

int32_t catalogUpdateUserAuthInfo(SCatalog* pCtg, SGetUserAuthRsp* pAuth) {
  CTG_API_ENTER();

  if (NULL == pCtg || NULL == pAuth) {
    CTG_API_LEAVE(TSDB_CODE_CTG_INVALID_INPUT);
  }

  CTG_API_LEAVE(ctgPutUpdateUserToQueue(pCtg, pAuth, false));
}


void catalogDestroy(void) {
  qInfo("start to destroy catalog");
  
  if (NULL == gCtgMgmt.pCluster || atomic_load_8((int8_t*)&gCtgMgmt.exit)) {
    return;
  }

  atomic_store_8((int8_t*)&gCtgMgmt.exit, true);

  if (tsem_post(&gCtgMgmt.queue.reqSem)) {
    qError("tsem_post failed, error:%s", tstrerror(TAOS_SYSTEM_ERROR(errno)));
  }
  
  if (tsem_post(&gCtgMgmt.queue.rspSem)) {
    qError("tsem_post failed, error:%s", tstrerror(TAOS_SYSTEM_ERROR(errno)));
  }

  while (CTG_IS_LOCKED(&gCtgMgmt.lock)) {
    taosUsleep(1);
  }
  
  CTG_LOCK(CTG_WRITE, &gCtgMgmt.lock);

  SCatalog *pCtg = NULL;
  void *pIter = taosHashIterate(gCtgMgmt.pCluster, NULL);
  while (pIter) {
    pCtg = *(SCatalog **)pIter;

    if (pCtg) {
      catalogFreeHandle(pCtg);
    }
    
    pIter = taosHashIterate(gCtgMgmt.pCluster, pIter);
  }
  
  taosHashCleanup(gCtgMgmt.pCluster);
  gCtgMgmt.pCluster = NULL;

  CTG_UNLOCK(CTG_WRITE, &gCtgMgmt.lock);

  qInfo("catalog destroyed");
}



