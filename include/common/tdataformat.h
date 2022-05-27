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

#ifndef _TD_COMMON_DATA_FORMAT_H_
#define _TD_COMMON_DATA_FORMAT_H_

#include "os.h"
#include "talgo.h"
#include "tencode.h"
#include "ttypes.h"
#include "tutil.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SSchema       SSchema;
typedef struct STColumn      STColumn;
typedef struct STSchema      STSchema;
typedef struct SColVal       SColVal;
typedef struct STSRow2       STSRow2;
typedef struct STSRowBuilder STSRowBuilder;
typedef struct STagVal       STagVal;
typedef struct STag          STag;
typedef struct STagIdx       STagIdx;
typedef struct STagBuilder   STagBuilder;
typedef struct STagIter      STagIter;
typedef struct SColData      SColData;
typedef struct SColBatch     SColBatch;

// STSchema
int32_t tTSchemaCreate(int32_t sver, SSchema *pSchema, int32_t nCols, STSchema **ppTSchema);
void    tTSchemaDestroy(STSchema *pTSchema);

// SColVal
#define ColValNONE               ((SColVal){.type = COL_VAL_NONE, .nData = 0, .pData = NULL})
#define ColValNULL               ((SColVal){.type = COL_VAL_NULL, .nData = 0, .pData = NULL})
#define ColValDATA(nData, pData) ((SColVal){.type = COL_VAL_DATA, .nData = (nData), .pData = (pData)})

// STSRow2
int32_t tPutTSRow(uint8_t *p, STSRow2 *pRow);
int32_t tGetTSRow(uint8_t *p, STSRow2 *pRow);
int32_t tTSRowDup(const STSRow2 *pRow, STSRow2 **ppRow);
void    tTSRowFree(STSRow2 *pRow);
int32_t tTSRowGet(const STSRow2 *pRow, STSchema *pTSchema, int32_t iCol, SColVal *pColVal);

// STSRowBuilder
int32_t tTSRowBuilderInit(STSRowBuilder *pBuilder, int32_t sver, int32_t nCols, SSchema *pSchema);
void    tTSRowBuilderClear(STSRowBuilder *pBuilder);
void    tTSRowBuilderReset(STSRowBuilder *pBuilder);
int32_t tTSRowBuilderPut(STSRowBuilder *pBuilder, int32_t cid, uint8_t *pData, uint32_t nData);
int32_t tTSRowBuilderGetRow(STSRowBuilder *pBuilder, const STSRow2 **ppRow);

// STag
int32_t tTagNew(STagVal *pTagVals, int16_t nTag, STag **ppTag);
void    tTagFree(STag *pTag);
int32_t tTagSet(STag *pTag, SSchema *pSchema, int32_t nCols, int iCol, uint8_t *pData, uint32_t nData, STag **ppTag);
void    tTagGet(STag *pTag, int16_t cid, int8_t type, uint8_t **ppData, uint32_t *nData);
int32_t tEncodeTag(SEncoder *pEncoder, const STag *pTag);
int32_t tDecodeTag(SDecoder *pDecoder, STag **ppTag);

int32_t tInitTagBuilder(STagBuilder *pBuilder, int16_t nTags);
void    tDestroyTagBuilder(STagBuilder *pBuilder);
void    tResetTagBuilder(STagBuilder *pBuilder);
int32_t tAddColToTagBuilder(STagBuilder *pBuilder, col_id_t colId, int8_t type, const void *value, int32_t tlen);
STag   *tGetTagFromBuilder(STagBuilder *pBuilder);

void debugPrintSTag(STag *pTag, const char *tag, int32_t ln);  // TODO: remove when done

struct STagIter{
  int16_t nTag;
  int16_t idx;
  STag   *tag;
};

void FORCE_INLINE STagIterReset(STagIter *pIter, STag *pTag) {
  pIter->tag = pTag;
  pIter->nTag = pIter->nTag;
  pIter->idx = 0;
}
void *STagIterNext(STagIter *pIter, int16_t *cid);

// STRUCT =================
struct STColumn {
  col_id_t colId;
  int8_t   type;
  int8_t   flags;
  int32_t  bytes;
  int32_t  offset;
};

struct STSchema {
  int32_t  numOfCols;
  int32_t  version;
  int32_t  flen;
  int32_t  vlen;
  int32_t  tlen;
  STColumn columns[];
};

#define TSROW_HAS_NONE ((uint8_t)0x1)
#define TSROW_HAS_NULL ((uint8_t)0x2U)
#define TSROW_HAS_VAL  ((uint8_t)0x4U)
#define TSROW_KV_ROW   ((uint8_t)0x10U)
struct STSRow2 {
  TSKEY    ts;
  uint8_t  flags;
  int32_t  sver;
  uint32_t nData;
  uint8_t *pData;
};

struct STSRowBuilder {
  STSchema *pTSchema;
  int32_t   szBitMap1;
  int32_t   szBitMap2;
  int32_t   szKVBuf;
  uint8_t  *pKVBuf;
  int32_t   szTPBuf;
  uint8_t  *pTPBuf;
  int32_t   iCol;
  int32_t   vlenKV;
  int32_t   vlenTP;
  STSRow2   row;
};

typedef enum { COL_VAL_NONE = 0, COL_VAL_NULL = 1, COL_VAL_DATA = 2 } EColValT;
struct SColVal {
  EColValT type;
  uint32_t nData;
  uint8_t *pData;
};

struct STagIdx {
  int16_t  cid;
  uint16_t offset;
};

#pragma pack(push, 1)
struct STag {
  uint16_t len;
  uint16_t nTag;
  int32_t  sver;
  STagIdx  idx[];
};
#pragma pack(pop)

#define TD_TAG_LEN(t) (((STag*)(t))->len)

struct STagVal {
  int16_t  cid;
  int8_t   type;
  uint32_t nData;
  int32_t  offset;
  uint8_t *pData;
};

struct STagBuilder {
  int16_t  tCols;
  int16_t  nCols;
  int32_t  alloc;
  int32_t  size;
  STagVal *pTagVal;
  void    *buf;
}

struct SColData {
  int8_t   type;
  int8_t   flags;
  int16_t  cid;
  uint32_t nData;
  uint8_t *pData;
};

struct SColBatch {
  tb_uid_t  uid;
  uint16_t  nCols;
  SColData *aColData;
};

#if 1  //================================================================================================================================================
// Imported since 3.0 and use bitmap to demonstrate None/Null/Norm, while use Null/Norm below 3.0 without of bitmap.
#define TD_SUPPORT_BITMAP
#define TD_SUPPORT_READ2
#define TD_SUPPORT_BACK2  // suppport back compatibility of 2.0

#define TASSERT(x) ASSERT(x)

#define STR_TO_VARSTR(x, str)                     \
  do {                                            \
    VarDataLenT __len = (VarDataLenT)strlen(str); \
    *(VarDataLenT *)(x) = __len;                  \
    memcpy(varDataVal(x), (str), __len);          \
  } while (0);

#define STR_TO_NET_VARSTR(x, str)                 \
  do {                                            \
    VarDataLenT __len = (VarDataLenT)strlen(str); \
    *(VarDataLenT *)(x) = htons(__len);           \
    memcpy(varDataVal(x), (str), __len);          \
  } while (0);

#define STR_WITH_MAXSIZE_TO_VARSTR(x, str, _maxs)                         \
  do {                                                                    \
    char *_e = stpncpy(varDataVal(x), (str), (_maxs)-VARSTR_HEADER_SIZE); \
    varDataSetLen(x, (_e - (x)-VARSTR_HEADER_SIZE));                      \
  } while (0)

#define STR_WITH_SIZE_TO_VARSTR(x, str, _size)  \
  do {                                          \
    *(VarDataLenT *)(x) = (VarDataLenT)(_size); \
    memcpy(varDataVal(x), (str), (_size));      \
  } while (0);

// ----------------- TSDB COLUMN DEFINITION

#define colType(col)   ((col)->type)
#define colFlags(col)  ((col)->flags)
#define colColId(col)  ((col)->colId)
#define colBytes(col)  ((col)->bytes)
#define colOffset(col) ((col)->offset)

#define colSetType(col, t)   (colType(col) = (t))
#define colSetFlags(col, f)  (colFlags(col) = (f))
#define colSetColId(col, id) (colColId(col) = (id))
#define colSetBytes(col, b)  (colBytes(col) = (b))
#define colSetOffset(col, o) (colOffset(col) = (o))

// ----------------- TSDB SCHEMA DEFINITION

#define schemaNCols(s)    ((s)->numOfCols)
#define schemaVersion(s)  ((s)->version)
#define schemaTLen(s)     ((s)->tlen)
#define schemaFLen(s)     ((s)->flen)
#define schemaVLen(s)     ((s)->vlen)
#define schemaColAt(s, i) ((s)->columns + i)
#define tdFreeSchema(s)   taosMemoryFreeClear((s))

STSchema *tdDupSchema(const STSchema *pSchema);
int32_t   tdEncodeSchema(void **buf, STSchema *pSchema);
void     *tdDecodeSchema(void *buf, STSchema **pRSchema);

static FORCE_INLINE int32_t comparColId(const void *key1, const void *key2) {
  if (*(int16_t *)key1 > ((STColumn *)key2)->colId) {
    return 1;
  } else if (*(int16_t *)key1 < ((STColumn *)key2)->colId) {
    return -1;
  } else {
    return 0;
  }
}

static FORCE_INLINE STColumn *tdGetColOfID(STSchema *pSchema, int16_t colId) {
  void *ptr = bsearch(&colId, (void *)pSchema->columns, schemaNCols(pSchema), sizeof(STColumn), comparColId);
  if (ptr == NULL) return NULL;
  return (STColumn *)ptr;
}

// ----------------- SCHEMA BUILDER DEFINITION
typedef struct {
  int32_t      tCols;
  int32_t      nCols;
  schema_ver_t version;
  uint16_t     flen;
  int32_t      vlen;
  int32_t      tlen;
  STColumn    *columns;
} STSchemaBuilder;

// use 2 bits for bitmap(default: STSRow/sub block)
#define TD_VTYPE_BITS        2
#define TD_VTYPE_PARTS       4  // PARTITIONS: 1 byte / 2 bits
#define TD_VTYPE_OPTR        3  // OPERATOR: 4 - 1, utilize to get remainder
#define TD_BITMAP_BYTES(cnt) (((cnt) + TD_VTYPE_OPTR) >> 2)

// use 1 bit for bitmap(super block)
#define TD_VTYPE_BITS_I        1
#define TD_VTYPE_PARTS_I       8  // PARTITIONS: 1 byte / 1 bit
#define TD_VTYPE_OPTR_I        7  // OPERATOR: 8 - 1, utilize to get remainder
#define TD_BITMAP_BYTES_I(cnt) (((cnt) + TD_VTYPE_OPTR_I) >> 3)

int32_t   tdInitTSchemaBuilder(STSchemaBuilder *pBuilder, schema_ver_t version);
void      tdDestroyTSchemaBuilder(STSchemaBuilder *pBuilder);
void      tdResetTSchemaBuilder(STSchemaBuilder *pBuilder, schema_ver_t version);
int32_t   tdAddColToSchema(STSchemaBuilder *pBuilder, int8_t type, int8_t flags, col_id_t colId, col_bytes_t bytes);
STSchema *tdGetSchemaFromBuilder(STSchemaBuilder *pBuilder);

// ----------------- Semantic timestamp key definition
// typedef uint64_t TKEY;
#define TKEY TSKEY

#define TKEY_INVALID       UINT64_MAX
#define TKEY_NULL          TKEY_INVALID
#define TKEY_NEGATIVE_FLAG (((TKEY)1) << 63)
#define TKEY_VALUE_FILTER  (~(TKEY_NEGATIVE_FLAG))

#define TKEY_IS_NEGATIVE(tkey) (((tkey)&TKEY_NEGATIVE_FLAG) != 0)
#define TKEY_IS_DELETED(tkey)  (false)

#define tdGetTKEY(key)  (key)
#define tdGetKey(tskey) (tskey)

#define MIN_TS_KEY ((TSKEY)0x8000000000000001)
#define MAX_TS_KEY ((TSKEY)0x7fffffffffffffff)

#define TD_TO_TKEY(key) tdGetTKEY(((key) < MIN_TS_KEY) ? MIN_TS_KEY : (((key) > MAX_TS_KEY) ? MAX_TS_KEY : key))

static FORCE_INLINE TKEY keyToTkey(TSKEY key) {
  TSKEY lkey = key;
  if (key > MAX_TS_KEY) {
    lkey = MAX_TS_KEY;
  } else if (key < MIN_TS_KEY) {
    lkey = MIN_TS_KEY;
  }

  return tdGetTKEY(lkey);
}

// ----------------- Data column structure
// SDataCol arrangement: data => bitmap => dataOffset
typedef struct SDataCol {
  int8_t          type;        // column type
  uint8_t         bitmap : 1;  // 0: no bitmap if all rows are NORM, 1: has bitmap if has NULL/NORM rows
  uint8_t         reserve : 7;
  int16_t         colId;      // column ID
  int32_t         bytes;      // column data bytes defined
  int32_t         offset;     // data offset in a SDataRow (including the header size)
  int32_t         spaceSize;  // Total space size for this column
  int32_t         len;        // column data length
  VarDataOffsetT *dataOff;    // For binary and nchar data, the offset in the data column
  void           *pData;      // Actual data pointer
  void           *pBitmap;    // Bitmap pointer
  TSKEY           ts;         // only used in last NULL column
} SDataCol;

#define isAllRowsNull(pCol) ((pCol)->len == 0)
#define isAllRowsNone(pCol) ((pCol)->len == 0)
static FORCE_INLINE void dataColReset(SDataCol *pDataCol) { pDataCol->len = 0; }

int32_t tdAllocMemForCol(SDataCol *pCol, int32_t maxPoints);

void    dataColInit(SDataCol *pDataCol, STColumn *pCol, int32_t maxPoints);
int32_t dataColAppendVal(SDataCol *pCol, const void *value, int32_t numOfRows, int32_t maxPoints);
void   *dataColSetOffset(SDataCol *pCol, int32_t nEle);

bool isNEleNull(SDataCol *pCol, int32_t nEle);

typedef struct {
  col_id_t  maxCols;    // max number of columns
  col_id_t  numOfCols;  // Total number of cols
  int32_t   maxPoints;  // max number of points
  int32_t   numOfRows;
  int32_t   bitmapMode : 1;  // default is 0(2 bits), otherwise 1(1 bit)
  int32_t   sversion : 31;   // TODO: set sversion(not used yet)
  SDataCol *cols;
} SDataCols;

static FORCE_INLINE bool tdDataColsIsBitmapI(SDataCols *pCols) { return pCols->bitmapMode != TSDB_BITMODE_DEFAULT; }
static FORCE_INLINE void tdDataColsSetBitmapI(SDataCols *pCols) { pCols->bitmapMode = TSDB_BITMODE_ONE_BIT; }
static FORCE_INLINE bool tdIsBitmapModeI(int8_t bitmapMode) { return bitmapMode != TSDB_BITMODE_DEFAULT; }

#define keyCol(pCols)              (&((pCols)->cols[0]))                    // Key column
#define dataColsTKeyAt(pCols, idx) ((TKEY *)(keyCol(pCols)->pData))[(idx)]  // the idx row of column-wised data
#define dataColsKeyAt(pCols, idx)  tdGetKey(dataColsTKeyAt(pCols, idx))
static FORCE_INLINE TKEY dataColsTKeyFirst(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsTKeyAt(pCols, 0);
  } else {
    return TKEY_INVALID;
  }
}

static FORCE_INLINE TSKEY dataColsKeyAtRow(SDataCols *pCols, int32_t row) {
  assert(row < pCols->numOfRows);
  return dataColsKeyAt(pCols, row);
}

static FORCE_INLINE TSKEY dataColsKeyFirst(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsKeyAt(pCols, 0);
  } else {
    return TSDB_DATA_TIMESTAMP_NULL;
  }
}

static FORCE_INLINE TKEY dataColsTKeyLast(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsTKeyAt(pCols, pCols->numOfRows - 1);
  } else {
    return TKEY_INVALID;
  }
}

static FORCE_INLINE TSKEY dataColsKeyLast(SDataCols *pCols) {
  if (pCols->numOfRows) {
    return dataColsKeyAt(pCols, pCols->numOfRows - 1);
  } else {
    return TSDB_DATA_TIMESTAMP_NULL;
  }
}

SDataCols *tdNewDataCols(int32_t maxCols, int32_t maxRows);
void       tdResetDataCols(SDataCols *pCols);
int32_t    tdInitDataCols(SDataCols *pCols, STSchema *pSchema);
SDataCols *tdDupDataCols(SDataCols *pCols, bool keepData);
SDataCols *tdFreeDataCols(SDataCols *pCols);
int32_t    tdMergeDataCols(SDataCols *target, SDataCols *source, int32_t rowsToMerge, int32_t *pOffset, bool update,
                           TDRowVerT maxVer);



#endif

#ifdef __cplusplus
}
#endif

#endif /*_TD_COMMON_DATA_FORMAT_H_*/

