#include <gtest/gtest.h>

#include "os.h"
#include "tdb.h"

#include <string>

typedef struct SPoolMem {
  int64_t          size;
  struct SPoolMem *prev;
  struct SPoolMem *next;
} SPoolMem;

static SPoolMem *openPool() {
  SPoolMem *pPool = (SPoolMem *)taosMemoryMalloc(sizeof(*pPool));

  pPool->prev = pPool->next = pPool;
  pPool->size = 0;

  return pPool;
}

static void clearPool(SPoolMem *pPool) {
  SPoolMem *pMem;

  do {
    pMem = pPool->next;

    if (pMem == pPool) break;

    pMem->next->prev = pMem->prev;
    pMem->prev->next = pMem->next;
    pPool->size -= pMem->size;

    taosMemoryFree(pMem);
  } while (1);

  assert(pPool->size == 0);
}

static void closePool(SPoolMem *pPool) {
  clearPool(pPool);
  taosMemoryFree(pPool);
}

static void *poolMalloc(void *arg, size_t size) {
  void     *ptr = NULL;
  SPoolMem *pPool = (SPoolMem *)arg;
  SPoolMem *pMem;

  pMem = (SPoolMem *)taosMemoryMalloc(sizeof(*pMem) + size);
  if (pMem == NULL) {
    assert(0);
  }

  pMem->size = sizeof(*pMem) + size;
  pMem->next = pPool->next;
  pMem->prev = pPool;

  pPool->next->prev = pMem;
  pPool->next = pMem;
  pPool->size += pMem->size;

  ptr = (void *)(&pMem[1]);
  return ptr;
}

static void poolFree(void *arg, void *ptr) {
  SPoolMem *pPool = (SPoolMem *)arg;
  SPoolMem *pMem;

  pMem = &(((SPoolMem *)ptr)[-1]);

  pMem->next->prev = pMem->prev;
  pMem->prev->next = pMem->next;
  pPool->size -= pMem->size;

  taosMemoryFree(pMem);
}

static int tKeyCmpr(const void *pKey1, int kLen1, const void *pKey2, int kLen2) {
  int k1, k2;

  std::string s1((char *)pKey1 + 3, kLen1 - 3);
  std::string s2((char *)pKey2 + 3, kLen2 - 3);
  k1 = stoi(s1);
  k2 = stoi(s2);

  if (k1 < k2) {
    return -1;
  } else if (k1 > k2) {
    return 1;
  } else {
    return 0;
  }
}

static int tDefaultKeyCmpr(const void *pKey1, int keyLen1, const void *pKey2, int keyLen2) {
  int mlen;
  int cret;

  ASSERT(keyLen1 > 0 && keyLen2 > 0 && pKey1 != NULL && pKey2 != NULL);

  mlen = keyLen1 < keyLen2 ? keyLen1 : keyLen2;
  cret = memcmp(pKey1, pKey2, mlen);
  if (cret == 0) {
    if (keyLen1 < keyLen2) {
      cret = -1;
    } else if (keyLen1 > keyLen2) {
      cret = 1;
    } else {
      cret = 0;
    }
  }
  return cret;
}

TEST(tdb_test, simple_insert1) {
  int           ret;
  TENV         *pEnv;
  TDB          *pDb;
  tdb_cmpr_fn_t compFunc;
  int           nData = 1000000;
  TXN           txn;

  taosRemoveDir("tdb");

  // Open Env
  ret = tdbEnvOpen("tdb", 4096, 64, &pEnv);
  GTEST_ASSERT_EQ(ret, 0);

  // Create a database
  compFunc = tKeyCmpr;
  ret = tdbDbOpen("db.db", -1, -1, compFunc, pEnv, &pDb);
  GTEST_ASSERT_EQ(ret, 0);

  {
    char      key[64];
    char      val[64];
    int64_t   poolLimit = 4096;  // 1M pool limit
    int64_t   txnid = 0;
    SPoolMem *pPool;

    // open the pool
    pPool = openPool();

    // start a transaction
    txnid++;
    tdbTxnOpen(&txn, txnid, poolMalloc, poolFree, pPool, TDB_TXN_WRITE | TDB_TXN_READ_UNCOMMITTED);
    tdbBegin(pEnv, &txn);

    for (int iData = 1; iData <= nData; iData++) {
      sprintf(key, "key%d", iData);
      sprintf(val, "value%d", iData);
      ret = tdbDbInsert(pDb, key, strlen(key), val, strlen(val), &txn);
      GTEST_ASSERT_EQ(ret, 0);

      // if pool is full, commit the transaction and start a new one
      if (pPool->size >= poolLimit) {
        // commit current transaction
        tdbCommit(pEnv, &txn);
        tdbTxnClose(&txn);

        // start a new transaction
        clearPool(pPool);
        txnid++;
        tdbTxnOpen(&txn, txnid, poolMalloc, poolFree, pPool, TDB_TXN_WRITE | TDB_TXN_READ_UNCOMMITTED);
        tdbBegin(pEnv, &txn);
      }
    }

    // commit the transaction
    tdbCommit(pEnv, &txn);
    tdbTxnClose(&txn);

    {  // Query the data
      void *pVal = NULL;
      int   vLen;

      for (int i = 1; i <= nData; i++) {
        sprintf(key, "key%d", i);
        sprintf(val, "value%d", i);

        ret = tdbDbGet(pDb, key, strlen(key), &pVal, &vLen);
        ASSERT(ret == 0);
        GTEST_ASSERT_EQ(ret, 0);

        GTEST_ASSERT_EQ(vLen, strlen(val));
        GTEST_ASSERT_EQ(memcmp(val, pVal, vLen), 0);
      }

      tdbFree(pVal);
    }

    {  // Iterate to query the DB data
      TDBC *pDBC;
      void *pKey = NULL;
      void *pVal = NULL;
      int   vLen, kLen;
      int   count = 0;

      ret = tdbDbcOpen(pDb, &pDBC, NULL);
      GTEST_ASSERT_EQ(ret, 0);

      tdbDbcMoveToFirst(pDBC);

      for (;;) {
        ret = tdbDbcNext(pDBC, &pKey, &kLen, &pVal, &vLen);
        if (ret < 0) break;

        // std::cout.write((char *)pKey, kLen) /* << " " << kLen */ << " ";
        // std::cout.write((char *)pVal, vLen) /* << " " << vLen */;
        // std::cout << std::endl;

        count++;
      }

      GTEST_ASSERT_EQ(count, nData);

      tdbDbcClose(pDBC);

      tdbFree(pKey);
      tdbFree(pVal);
    }
  }

  ret = tdbDbDrop(pDb);
  GTEST_ASSERT_EQ(ret, 0);

  // Close a database
  tdbDbClose(pDb);

  // Close Env
  ret = tdbEnvClose(pEnv);
  GTEST_ASSERT_EQ(ret, 0);
}

TEST(tdb_test, simple_insert2) {
  int           ret;
  TENV         *pEnv;
  TDB          *pDb;
  tdb_cmpr_fn_t compFunc;
  int           nData = 1000000;
  TXN           txn;

  taosRemoveDir("tdb");

  // Open Env
  ret = tdbEnvOpen("tdb", 1024, 10, &pEnv);
  GTEST_ASSERT_EQ(ret, 0);

  // Create a database
  compFunc = tDefaultKeyCmpr;
  ret = tdbDbOpen("db.db", -1, -1, compFunc, pEnv, &pDb);
  GTEST_ASSERT_EQ(ret, 0);

  {
    char      key[64];
    char      val[64];
    int64_t   txnid = 0;
    SPoolMem *pPool;

    // open the pool
    pPool = openPool();

    // start a transaction
    txnid++;
    tdbTxnOpen(&txn, txnid, poolMalloc, poolFree, pPool, TDB_TXN_WRITE | TDB_TXN_READ_UNCOMMITTED);
    tdbBegin(pEnv, &txn);

    for (int iData = 1; iData <= nData; iData++) {
      sprintf(key, "key%d", iData);
      sprintf(val, "value%d", iData);
      ret = tdbDbInsert(pDb, key, strlen(key), val, strlen(val), &txn);
      GTEST_ASSERT_EQ(ret, 0);
    }

    {  // Iterate to query the DB data
      TDBC *pDBC;
      void *pKey = NULL;
      void *pVal = NULL;
      int   vLen, kLen;
      int   count = 0;

      ret = tdbDbcOpen(pDb, &pDBC, NULL);
      GTEST_ASSERT_EQ(ret, 0);

      tdbDbcMoveToFirst(pDBC);

      for (;;) {
        ret = tdbDbcNext(pDBC, &pKey, &kLen, &pVal, &vLen);
        if (ret < 0) break;

        // std::cout.write((char *)pKey, kLen) /* << " " << kLen */ << " ";
        // std::cout.write((char *)pVal, vLen) /* << " " << vLen */;
        // std::cout << std::endl;

        count++;
      }

      GTEST_ASSERT_EQ(count, nData);

      tdbDbcClose(pDBC);

      tdbFree(pKey);
      tdbFree(pVal);
    }
  }

  // commit the transaction
  tdbCommit(pEnv, &txn);
  tdbTxnClose(&txn);

  ret = tdbDbDrop(pDb);
  GTEST_ASSERT_EQ(ret, 0);

  // Close a database
  tdbDbClose(pDb);

  // Close Env
  ret = tdbEnvClose(pEnv);
  GTEST_ASSERT_EQ(ret, 0);
}

TEST(tdb_test, simple_delete1) {
  int       ret;
  TDB      *pDb;
  char      key[128];
  char      data[128];
  TXN       txn;
  TENV     *pEnv;
  SPoolMem *pPool;
  void     *pKey = NULL;
  void     *pData = NULL;
  int       nKey;
  TDBC     *pDbc;
  int       nData;
  int       nKV = 69;

  taosRemoveDir("tdb");

  pPool = openPool();

  // open env
  ret = tdbEnvOpen("tdb", 1024, 256, &pEnv);
  GTEST_ASSERT_EQ(ret, 0);

  // open database
  ret = tdbDbOpen("db.db", -1, -1, tKeyCmpr, pEnv, &pDb);
  GTEST_ASSERT_EQ(ret, 0);

  tdbTxnOpen(&txn, 0, poolMalloc, poolFree, pPool, TDB_TXN_WRITE | TDB_TXN_READ_UNCOMMITTED);
  tdbBegin(pEnv, &txn);

  // loop to insert batch data
  for (int iData = 0; iData < nKV; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d", iData);
    ret = tdbDbInsert(pDb, key, strlen(key), data, strlen(data), &txn);
    GTEST_ASSERT_EQ(ret, 0);
  }

  // query the data
  for (int iData = 0; iData < nKV; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d", iData);

    ret = tdbDbGet(pDb, key, strlen(key), &pData, &nData);
    GTEST_ASSERT_EQ(ret, 0);
    GTEST_ASSERT_EQ(memcmp(data, pData, nData), 0);
  }

  // loop to delete some data
  for (int iData = nKV - 1; iData > 30; iData--) {
    sprintf(key, "key%d", iData);

    ret = tdbDbDelete(pDb, key, strlen(key), &txn);
    GTEST_ASSERT_EQ(ret, 0);
  }

  // query the data
  for (int iData = 0; iData < nKV; iData++) {
    sprintf(key, "key%d", iData);

    ret = tdbDbGet(pDb, key, strlen(key), &pData, &nData);
    if (iData <= 30) {
      GTEST_ASSERT_EQ(ret, 0);
    } else {
      GTEST_ASSERT_EQ(ret, -1);
    }
  }

  // loop to iterate the data
  tdbDbcOpen(pDb, &pDbc, NULL);

  ret = tdbDbcMoveToFirst(pDbc);
  GTEST_ASSERT_EQ(ret, 0);

  pKey = NULL;
  pData = NULL;
  for (;;) {
    ret = tdbDbcNext(pDbc, &pKey, &nKey, &pData, &nData);
    if (ret < 0) break;

    std::cout.write((char *)pKey, nKey) /* << " " << kLen */ << " ";
    std::cout.write((char *)pData, nData) /* << " " << vLen */;
    std::cout << std::endl;
  }

  tdbDbcClose(pDbc);

  tdbCommit(pEnv, &txn);

  closePool(pPool);

  tdbDbClose(pDb);
  tdbEnvClose(pEnv);
}

TEST(tdb_test, simple_upsert1) {
  int       ret;
  TENV     *pEnv;
  TDB      *pDb;
  int       nData = 100000;
  char      key[64];
  char      data[64];
  void     *pData = NULL;
  SPoolMem *pPool;
  TXN       txn;

  taosRemoveDir("tdb");

  // open env
  ret = tdbEnvOpen("tdb", 4096, 64, &pEnv);
  GTEST_ASSERT_EQ(ret, 0);

  // open database
  ret = tdbDbOpen("db.db", -1, -1, NULL, pEnv, &pDb);
  GTEST_ASSERT_EQ(ret, 0);

  pPool = openPool();
  // insert some data
  tdbTxnOpen(&txn, 0, poolMalloc, poolFree, pPool, TDB_TXN_WRITE | TDB_TXN_READ_UNCOMMITTED);
  tdbBegin(pEnv, &txn);

  for (int iData = 0; iData < nData; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d", iData);
    ret = tdbDbInsert(pDb, key, strlen(key), data, strlen(data), &txn);
    GTEST_ASSERT_EQ(ret, 0);
  }

  // query the data
  for (int iData = 0; iData < nData; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d", iData);
    ret = tdbDbGet(pDb, key, strlen(key), &pData, &nData);
    GTEST_ASSERT_EQ(ret, 0);
    GTEST_ASSERT_EQ(memcmp(pData, data, nData), 0);
  }

  // upsert some data
  for (int iData = 0; iData < nData; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d-u", iData);
    ret = tdbDbUpsert(pDb, key, strlen(key), data, strlen(data), &txn);
    GTEST_ASSERT_EQ(ret, 0);
  }

  tdbCommit(pEnv, &txn);

  // query the data
  for (int iData = 0; iData < nData; iData++) {
    sprintf(key, "key%d", iData);
    sprintf(data, "data%d-u", iData);
    ret = tdbDbGet(pDb, key, strlen(key), &pData, &nData);
    GTEST_ASSERT_EQ(ret, 0);
    GTEST_ASSERT_EQ(memcmp(pData, data, nData), 0);
  }

  tdbDbClose(pDb);
  tdbEnvClose(pEnv);
}