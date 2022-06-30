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

#include "sma.h"

typedef struct SSmaStat SSmaStat;

#define RSMA_TASK_INFO_HASH_SLOT 8
#define SMA_MGMT_REF_NUM         1024

extern SSmaMgmt smaMgmt;

// declaration of static functions

static int32_t  tdInitSmaStat(SSmaStat **pSmaStat, int8_t smaType, const SSma *pSma);
static SSmaEnv *tdNewSmaEnv(const SSma *pSma, int8_t smaType, const char *path);
static int32_t  tdInitSmaEnv(SSma *pSma, int8_t smaType, const char *path, SSmaEnv **pEnv);
static void    *tdFreeTSmaStat(STSmaStat *pStat);
static void     tdDestroyRSmaStat(void *pRSmaStat);

// implementation

static SSmaEnv *tdNewSmaEnv(const SSma *pSma, int8_t smaType, const char *path) {
  SSmaEnv *pEnv = NULL;

  pEnv = (SSmaEnv *)taosMemoryCalloc(1, sizeof(SSmaEnv));
  if (!pEnv) {
    terrno = TSDB_CODE_OUT_OF_MEMORY;
    return NULL;
  }

  SMA_ENV_TYPE(pEnv) = smaType;

  int code = taosThreadRwlockInit(&(pEnv->lock), NULL);
  if (code) {
    terrno = TAOS_SYSTEM_ERROR(code);
    taosMemoryFree(pEnv);
    return NULL;
  }

  if (tdInitSmaStat(&SMA_ENV_STAT(pEnv), smaType, pSma) != TSDB_CODE_SUCCESS) {
    tdFreeSmaEnv(pEnv);
    return NULL;
  }

  return pEnv;
}

static int32_t tdInitSmaEnv(SSma *pSma, int8_t smaType, const char *path, SSmaEnv **pEnv) {
  if (!pEnv) {
    terrno = TSDB_CODE_INVALID_PTR;
    return TSDB_CODE_FAILED;
  }

  if (!(*pEnv)) {
    if (!(*pEnv = tdNewSmaEnv(pSma, smaType, path))) {
      return TSDB_CODE_FAILED;
    }
  }

  return TSDB_CODE_SUCCESS;
}

/**
 * @brief Release resources allocated for its member fields, not including itself.
 *
 * @param pSmaEnv
 * @return int32_t
 */
void tdDestroySmaEnv(SSmaEnv *pSmaEnv) {
  if (pSmaEnv) {
    pSmaEnv->pStat = tdFreeSmaState(pSmaEnv->pStat, SMA_ENV_TYPE(pSmaEnv));
    taosThreadRwlockDestroy(&(pSmaEnv->lock));
  }
}

void *tdFreeSmaEnv(SSmaEnv *pSmaEnv) {
  if (pSmaEnv) {
    tdDestroySmaEnv(pSmaEnv);
    taosMemoryFreeClear(pSmaEnv);
  }
  return NULL;
}

int32_t tdRefSmaStat(SSma *pSma, SSmaStat *pStat) {
  if (!pStat) return 0;

  int ref = T_REF_INC(pStat);
  smaDebug("vgId:%d, ref sma stat:%p, val:%d", SMA_VID(pSma), pStat, ref);
  return 0;
}

int32_t tdUnRefSmaStat(SSma *pSma, SSmaStat *pStat) {
  if (!pStat) return 0;

  int ref = T_REF_DEC(pStat);
  smaDebug("vgId:%d, unref sma stat:%p, val:%d", SMA_VID(pSma), pStat, ref);
  return 0;
}

static int32_t tdInitSmaStat(SSmaStat **pSmaStat, int8_t smaType, const SSma *pSma) {
  ASSERT(pSmaStat != NULL);

  if (*pSmaStat) {  // no lock
    return TSDB_CODE_SUCCESS;
  }

  /**
   *  1. Lazy mode utilized when init SSmaStat to update expire window(or hungry mode when tdNew).
   *  2. Currently, there is mutex lock when init SSmaEnv, thus no need add lock on SSmaStat, and please add lock if
   * tdInitSmaStat invoked in other multithread environment later.
   */
  if (!(*pSmaStat)) {
    *pSmaStat = (SSmaStat *)taosMemoryCalloc(1, sizeof(SSmaStat));
    if (!(*pSmaStat)) {
      terrno = TSDB_CODE_OUT_OF_MEMORY;
      return TSDB_CODE_FAILED;
    }

    if (smaType == TSDB_SMA_TYPE_ROLLUP) {
      SRSmaStat *pRSmaStat = (SRSmaStat *)(*pSmaStat);
      pRSmaStat->pSma = (SSma *)pSma;

      // init smaMgmt
      smaMgmt.smaRef = taosOpenRef(SMA_MGMT_REF_NUM, tdDestroyRSmaStat);
      if (smaMgmt.smaRef < 0) {
        smaError("init smaRef failed, num:%d", SMA_MGMT_REF_NUM);
        terrno = TSDB_CODE_OUT_OF_MEMORY;
        return TSDB_CODE_FAILED;
      }

      int64_t refId = taosAddRef(smaMgmt.smaRef, pRSmaStat);
      if (refId < 0) {
        smaError("taosAddRef smaRef failed, since:%s", tstrerror(terrno));
        return TSDB_CODE_FAILED;
      }
      pRSmaStat->refId = refId;

      // init timer
      RSMA_TMR_HANDLE(pRSmaStat) = taosTmrInit(10000, 100, 10000, "RSMA");
      if (!RSMA_TMR_HANDLE(pRSmaStat)) {
        taosMemoryFreeClear(*pSmaStat);
        return TSDB_CODE_FAILED;
      }

      // init hash
      RSMA_INFO_HASH(pRSmaStat) = taosHashInit(
          RSMA_TASK_INFO_HASH_SLOT, taosGetDefaultHashFunction(TSDB_DATA_TYPE_BIGINT), true, HASH_ENTRY_LOCK);
      if (!RSMA_INFO_HASH(pRSmaStat)) {
        if (RSMA_TMR_HANDLE(pRSmaStat)) {
          taosTmrCleanUp(RSMA_TMR_HANDLE(pRSmaStat));
        }
        taosMemoryFreeClear(*pSmaStat);
        return TSDB_CODE_FAILED;
      }
    } else if (smaType == TSDB_SMA_TYPE_TIME_RANGE) {
      // TODO
    } else {
      ASSERT(0);
    }
  }
  return TSDB_CODE_SUCCESS;
}

static void tdDestroyTSmaStat(STSmaStat *pStat) {
  if (pStat) {
    smaDebug("destroy tsma stat");
    tDestroyTSma(pStat->pTSma);
    taosMemoryFreeClear(pStat->pTSma);
    taosMemoryFreeClear(pStat->pTSchema);
  }
}

static void *tdFreeTSmaStat(STSmaStat *pStat) {
  tdDestroyTSmaStat(pStat);
  taosMemoryFreeClear(pStat);
  return NULL;
}

static void tdDestroyRSmaStat(void *pRSmaStat) {
  if (pRSmaStat) {
    SRSmaStat *pStat = (SRSmaStat *)pRSmaStat;
    smaDebug("vgId:%d %s:%d destroy rsma stat %p", SMA_VID(pStat->pSma), __func__, __LINE__, pRSmaStat);
    // step 1: set persistence task cancelled
    atomic_store_8(RSMA_TRIGGER_STAT(pStat), TASK_TRIGGER_STAT_CANCELLED);

    // step 2: stop the persistence timer
    taosTmrStopA(&RSMA_TMR_ID(pStat));

    // step 3: wait the persistence thread to finish
    int32_t nLoops = 0;
    if (atomic_load_8(RSMA_RUNNING_STAT(pStat)) == 1) {
      while (1) {
        if (atomic_load_8(RSMA_TRIGGER_STAT(pStat)) == TASK_TRIGGER_STAT_FINISHED) {
          smaDebug("rsma, persist task finished already");
          break;
        } else {
          smaDebug("rsma, persist task not finished yet since rsma stat in %" PRIi8,
                   atomic_load_8(RSMA_TRIGGER_STAT(pStat)));
        }
        ++nLoops;
        if (nLoops > 1000) {
          sched_yield();
          nLoops = 0;
        }
      }
    }

    // step 4: destroy the rsma info and associated fetch tasks
    // TODO: use taosHashSetFreeFp when taosHashSetFreeFp is ready.
    void *infoHash = taosHashIterate(RSMA_INFO_HASH(pStat), NULL);
    while (infoHash) {
      SRSmaInfo *pSmaInfo = *(SRSmaInfo **)infoHash;
      tdFreeRSmaInfo(pSmaInfo);
      infoHash = taosHashIterate(RSMA_INFO_HASH(pStat), infoHash);
    }
    taosHashCleanup(RSMA_INFO_HASH(pStat));

    // step 5: wait all triggered fetch tasks finished
    nLoops = 0;
    while (1) {
      if (T_REF_VAL_GET((SSmaStat *)pStat) == 0) {
        smaDebug("rsma, all fetch task finished already");
        break;
      } else {
        smaDebug("rsma, fetch tasks not all finished yet");
      }
      ++nLoops;
      if (nLoops > 1000) {
        sched_yield();
        nLoops = 0;
      }
    }

    // step 6: cleanup the timer handle
    if (RSMA_TMR_HANDLE(pStat)) {
      taosTmrCleanUp(RSMA_TMR_HANDLE(pStat));
    }
  }
}

void *tdFreeSmaState(SSmaStat *pSmaStat, int8_t smaType) {
  tdDestroySmaState(pSmaStat, smaType);
  if (smaType == TSDB_SMA_TYPE_TIME_RANGE) {
    taosMemoryFreeClear(pSmaStat);
  }
  // tref used to free rsma stat

  return NULL;
}

/**
 * @brief Release resources allocated for its member fields, not including itself.
 *
 * @param pSmaStat
 * @return int32_t
 */

int32_t tdDestroySmaState(SSmaStat *pSmaStat, int8_t smaType) {
  if (pSmaStat) {
    if (smaType == TSDB_SMA_TYPE_TIME_RANGE) {
      tdDestroyTSmaStat(SMA_TSMA_STAT(pSmaStat));
    } else if (smaType == TSDB_SMA_TYPE_ROLLUP) {
      SRSmaStat *pRSmaStat = SMA_RSMA_STAT(pSmaStat);
      if (taosRemoveRef(smaMgmt.smaRef, RSMA_REF_ID(pRSmaStat)) < 0) {
        smaError("remove refId from smaRef failed, refId:0x%" PRIx64, RSMA_REF_ID(pRSmaStat));
      }
    } else {
      ASSERT(0);
    }
  }
  return 0;
}

int32_t tdLockSma(SSma *pSma) {
  int code = taosThreadMutexLock(&pSma->mutex);
  if (code != 0) {
    smaError("vgId:%d, failed to lock td since %s", SMA_VID(pSma), strerror(errno));
    terrno = TAOS_SYSTEM_ERROR(code);
    return -1;
  }
  pSma->locked = true;
  return 0;
}

int32_t tdUnLockSma(SSma *pSma) {
  ASSERT(SMA_LOCKED(pSma));
  pSma->locked = false;
  int code = taosThreadMutexUnlock(&pSma->mutex);
  if (code != 0) {
    smaError("vgId:%d, failed to unlock td since %s", SMA_VID(pSma), strerror(errno));
    terrno = TAOS_SYSTEM_ERROR(code);
    return -1;
  }
  return 0;
}

int32_t tdCheckAndInitSmaEnv(SSma *pSma, int8_t smaType) {
  SSmaEnv *pEnv = NULL;

  switch (smaType) {
    case TSDB_SMA_TYPE_TIME_RANGE:
      if ((pEnv = (SSmaEnv *)atomic_load_ptr(&SMA_TSMA_ENV(pSma)))) {
        return TSDB_CODE_SUCCESS;
      }
      break;
    case TSDB_SMA_TYPE_ROLLUP:
      if ((pEnv = (SSmaEnv *)atomic_load_ptr(&SMA_RSMA_ENV(pSma)))) {
        return TSDB_CODE_SUCCESS;
      }
      break;
    default:
      TASSERT(0);
      return TSDB_CODE_FAILED;
  }

  // init sma env
  tdLockSma(pSma);
  pEnv = (smaType == TSDB_SMA_TYPE_TIME_RANGE) ? atomic_load_ptr(&SMA_TSMA_ENV(pSma))
                                               : atomic_load_ptr(&SMA_RSMA_ENV(pSma));
  if (!pEnv) {
    char rname[TSDB_FILENAME_LEN] = {0};

    if (tdInitSmaEnv(pSma, smaType, rname, &pEnv) < 0) {
      tdUnLockSma(pSma);
      return TSDB_CODE_FAILED;
    }

    (smaType == TSDB_SMA_TYPE_TIME_RANGE) ? atomic_store_ptr(&SMA_TSMA_ENV(pSma), pEnv)
                                          : atomic_store_ptr(&SMA_RSMA_ENV(pSma), pEnv);
  }
  tdUnLockSma(pSma);

  return TSDB_CODE_SUCCESS;
};