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

#ifndef _TD_DND_QNODE_INT_H_
#define _TD_DND_QNODE_INT_H_

#include "dmUtil.h"

#include "qnode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SQnodeMgmt {
  SQnode       *pQnode;
  SMsgCb        msgCb;
  const char   *path;
  const char   *name;
  int32_t       dnodeId;
  SSingleWorker queryWorker;
  SSingleWorker fetchWorker;
  SSingleWorker monitorWorker;
} SQnodeMgmt;

// qmHandle.c
SArray *qmGetMsgHandles();
int32_t qmProcessCreateReq(const SMgmtInputOpt *pInput, SNodeMsg *pMsg);
int32_t qmProcessDropReq(SQnodeMgmt *pMgmt, SNodeMsg *pMsg);
int32_t qmProcessGetMonitorInfoReq(SQnodeMgmt *pMgmt, SNodeMsg *pReq);

// qmWorker.c
int32_t qmPutRpcMsgToQueryQueue(SQnodeMgmt *pMgmt, SRpcMsg *pMsg);
int32_t qmPutRpcMsgToFetchQueue(SQnodeMgmt *pMgmt, SRpcMsg *pMsg);
int32_t qmGetQueueSize(SQnodeMgmt *pMgmt, int32_t vgId, EQueueType qtype);

int32_t qmStartWorker(SQnodeMgmt *pMgmt);
void    qmStopWorker(SQnodeMgmt *pMgmt);
int32_t qmPutNodeMsgToQueryQueue(SQnodeMgmt *pMgmt, SNodeMsg *pMsg);
int32_t qmPutNodeMsgToFetchQueue(SQnodeMgmt *pMgmt, SNodeMsg *pMsg);
int32_t qmPutNodeMsgToMonitorQueue(SQnodeMgmt *pMgmt, SNodeMsg *pMsg);

#ifdef __cplusplus
}
#endif

#endif /*_TD_DND_QNODE_INT_H_*/