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

#ifndef _TDB_ENV_H_
#define _TDB_ENV_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct STEnv {
  char    *rootDir;
  char    *jfname;
  int      jfd;
  SPCache *pCache;
  SPager  *pgrList;
  int      nPager;
  int      nPgrHash;
  SPager **pgrHash;
} TENV;

int tdbEnvOpen(const char *rootDir, int pageSize, int cacheSize, TENV **ppEnv);
int tdbEnvClose(TENV *pEnv);
int tdbBegin(TENV *pEnv, TXN *pTxn);
int tdbCommit(TENV *pEnv, TXN *pTxn);
int tdbRollback(TENV *pEnv, TXN *pTxn);

void    tdbEnvAddPager(TENV *pEnv, SPager *pPager);
void    tdbEnvRemovePager(TENV *pEnv, SPager *pPager);
SPager *tdbEnvGetPager(TENV *pEnv, const char *fname);

#ifdef __cplusplus
}
#endif

#endif /*_TDB_ENV_H_*/