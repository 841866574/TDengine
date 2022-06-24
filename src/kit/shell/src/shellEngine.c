/*
 * Copyright (c) 2022 TAOS Data, Inc. <jhtao@taosdata.com>
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

#define _BSD_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define _DEFAULT_SOURCE

#include "os.h"
#include "shell.h"
#include "shellCommand.h"
#include "tutil.h"
#include "taosdef.h"
#include "taoserror.h"
#include "tglobal.h"
#include "tsclient.h"

#include <regex.h>

/**************** Global variables ****************/
char      CLIENT_VERSION[] = "Welcome to the TDengine shell from %s, Client Version:%s\n"
                             "Copyright (c) 2022 by TAOS Data, Inc. All rights reserved.\n\n";
char      PROMPT_HEADER[] = "taos> ";
char      CONTINUE_PROMPT[] = "   -> ";
int       prompt_size = 6;
const char   *BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char    hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
int64_t result = 0;
SShellHistory   history;

#define DEFAULT_MAX_BINARY_DISPLAY_WIDTH 30
extern int32_t tsMaxBinaryDisplayWidth;
extern TAOS *taos_connect_auth(const char *ip, const char *user, const char *auth, const char *db, uint16_t port);

static int  calcColWidth(TAOS_FIELD *field, int precision);
static void printHeader(TAOS_FIELD *fields, int *width, int num_fields);

/*
 * FUNCTION: Initialize the shell.
 */
void shellInit(SShellArguments *_args) {
  printf("\n");
  if (!_args->is_use_passwd) {
#ifdef WINDOWS
    strcpy(tsOsName, "Windows");
#elif defined(DARWIN)
    strcpy(tsOsName, "Darwin");
#endif
    printf(CLIENT_VERSION, tsOsName, taos_get_client_info());
  }

  fflush(stdout);

  if (!_args->is_use_passwd) {
    _args->password = TSDB_DEFAULT_PASS;
  }

  if (_args->user == NULL) {
    _args->user = TSDB_DEFAULT_USER;
  }

  if (_args->restful || _args->cloud) {
    if (wsclient_handshake(1)) {
      exit(EXIT_FAILURE);
    }
    wsclient.status = TCP_CONNECTED;
    if (wsclient_conn()) {
      exit(EXIT_FAILURE);
    }
  } else {
    // set options before initializing
    if (_args->timezone != NULL) {
      taos_options(TSDB_OPTION_TIMEZONE, _args->timezone);
    }

    if (taos_init()) {
      printf("failed to init taos\n");
      fflush(stdout);
      exit(EXIT_FAILURE);
    }

    // Connect to the database.
    if (_args->auth == NULL) {
      _args->con = taos_connect(_args->host, _args->user, _args->password, _args->database, _args->port);
    } else {
      _args->con = taos_connect_auth(_args->host, _args->user, _args->auth, _args->database, _args->port);
    }

    if (_args->con == NULL) {
      fflush(stdout);
      exit(EXIT_FAILURE);
    }
  }

    /* Read history TODO : release resources here*/
    read_history();

    // Check if it is temperory run
    if (_args->commands != NULL || _args->file[0] != 0) {
      if (_args->commands != NULL) {
        printf("%s%s\n", PROMPT_HEADER, _args->commands);
        shellRunCommand(_args->con, _args->commands);
      }

      if (_args->file[0] != 0) {
        source_file(_args->con, _args->file);
      }

      taos_close(_args->con);
      write_history();
      exit(EXIT_SUCCESS);
    }

#ifndef WINDOWS
    if (_args->dir[0] != 0) {
      source_dir(_args->con, _args);
      taos_close(_args->con);
      exit(EXIT_SUCCESS);
    }

    if (_args->check != 0) {
      shellCheck(_args->con, _args);
      taos_close(_args->con);
      exit(EXIT_SUCCESS);
    }
#endif

  return;
}

static bool isEmptyCommand(const char* cmd) {
  for (char c = *cmd++; c != 0; c = *cmd++) {
    if (c != ' ' && c != '\t' && c != ';') {
      return false;
    }
  }
  return true;
}

static char* wsclient_strerror(WS_STATUS status) {
 switch (status) {
   case CANCELED:
     return "terminated";
   case DISCONNECTED:
     return "disconnected";
   case RECV_ERROR:
     return "recv error";
   case SEND_ERROR:
     return "send error";
   default:
     return "success";
 }
}

static int32_t shellRunSingleCommand(TAOS *con, char *command) {
  /* If command is empty just return */
  if (isEmptyCommand(command)) {
    return 0;
  }

  // Analyse the command.
  if (regex_match(command, "^[ \t]*(quit|q|exit)[ \t;]*$", REG_EXTENDED | REG_ICASE)) {
    if (args.restful || args.cloud) {
      close(args.socket);
    } else {
      taos_close(con);
    }
    write_history();
#ifdef WINDOWS
    exit(EXIT_SUCCESS);
#endif
    return -1;
  }

  if (regex_match(command, "^[\t ]*clear[ \t;]*$", REG_EXTENDED | REG_ICASE)) {
    // If clear the screen.
    system("clear");
    return 0;
  }

  if (regex_match(command, "^[\t ]*set[ \t]+max_binary_display_width[ \t]+(default|[1-9][0-9]*)[ \t;]*$", REG_EXTENDED | REG_ICASE)) {
    strtok(command, " \t");
    strtok(NULL, " \t");
    char* p = strtok(NULL, " \t");
    if (strcasecmp(p, "default") == 0) {
      tsMaxBinaryDisplayWidth = DEFAULT_MAX_BINARY_DISPLAY_WIDTH;
    } else {
      tsMaxBinaryDisplayWidth = atoi(p);
    }
    return 0;
  }

  if (regex_match(command, "^[ \t]*source[\t ]+[^ ]+[ \t;]*$", REG_EXTENDED | REG_ICASE)) {
    /* If source file. */
    char *c_ptr = strtok(command, " ;");
    assert(c_ptr != NULL);
    c_ptr = strtok(NULL, " ;");
    assert(c_ptr != NULL);
    source_file(con, c_ptr);
    return 0;
  }
  if (args.restful || args.cloud) {
    shellRunCommandOnWebsocket(command);
  } else {
    shellRunCommandOnServer(con, command);
  }
  return 0;
}


int32_t shellRunCommand(TAOS* con, char* command) {
  /* If command is empty just return */
  if (isEmptyCommand(command)) {
    return 0;
  }

  /* Update the history vector. */
  if (history.hstart == history.hend ||
      history.hist[(history.hend + MAX_HISTORY_SIZE - 1) % MAX_HISTORY_SIZE] == NULL ||
      strcmp(command, history.hist[(history.hend + MAX_HISTORY_SIZE - 1) % MAX_HISTORY_SIZE]) != 0) {
    if (history.hist[history.hend] != NULL) {
      tfree(history.hist[history.hend]);
    }
    history.hist[history.hend] = strdup(command);

    history.hend = (history.hend + 1) % MAX_HISTORY_SIZE;
    if (history.hend == history.hstart) {
      history.hstart = (history.hstart + 1) % MAX_HISTORY_SIZE;
    }
  }

  char quote = 0, *cmd = command;
  for (char c = *command++; c != 0; c = *command++) {
    if (c == '\\' && (*command == '\'' || *command == '"' || *command == '`')) {
      command ++;
      continue;
    }

    if (quote == c) {
      quote = 0;
    } else if (quote == 0 && (c == '\'' || c == '"' || c == '`')) {
      quote = c;
    } else if (c == ';' && quote == 0) {
      c = *command;
      *command = 0;
      if (shellRunSingleCommand(con, cmd) < 0) {
        return -1;
      }
      *command = c;
      cmd = command;
    }
  }
  return shellRunSingleCommand(con, cmd);
}


void freeResultWithRid(int64_t rid) {
  SSqlObj* pSql = taosAcquireRef(tscObjRef, rid);
  if(pSql){
    taos_free_result(pSql);
    taosReleaseRef(tscObjRef, rid);
  }
}

void shellRunCommandOnWebsocket(char command[]) {
 int64_t et;
 wordexp_t full_path;
 char *    sptr = NULL;
 char *    cptr = NULL;
 char *    fname = NULL;
 bool      printMode = false;
 if ((sptr = tstrstr(command, ">>", true)) != NULL) {
   cptr = tstrstr(command, ";", true);
   if (cptr != NULL) {
     *cptr = '\0';
   }

   if (wordexp(sptr + 2, &full_path, 0) != 0) {
     fprintf(stderr, "ERROR: invalid filename: %s\n", sptr + 2);
     return;
   }
   *sptr = '\0';
   fname = full_path.we_wordv[0];
 }

 if ((sptr = tstrstr(command, "\\G", true)) != NULL) {
   cptr = tstrstr(command, ";", true);
   if (cptr != NULL) {
     *cptr = '\0';
   }

   *sptr = '\0';
   printMode = true;  // When output to a file, the switch does not work.
 }

 uint64_t limit = DEFAULT_RES_SHOW_NUM;
 if (regex_match(command, "^(.*)\\s+limit\\s+[1-9][0-9]*;?$", REG_EXTENDED | REG_ICASE)) {
   char*limit_buf = strstr(command, "limit");
   limit_buf += strlen("limit");
   if (limit_buf != NULL) {
     if (command[strlen(command) -1] == ';') {
       command[strlen(command) -1] = '\0';
     }
     int index = 0;
     while (limit_buf[index] == ' ' && index < strlen(limit_buf)) {
       index++;
     }
     if (limit_buf[index] != '\0') {
       limit = atoll(limit_buf + index);
     }
   }
 }

 args.st = taosGetTimestampUs();

 cJSON* query = wsclient_query(command);

 if (query == NULL || wsclient_check(query)) {
   cJSON_Delete(query);
   return;
 }

 if (regex_match(command, "^\\s*use\\s+[a-zA-Z0-9_]+\\s*;\\s*$", REG_EXTENDED | REG_ICASE)) {
   fprintf(stdout, "Database changed.\n\n");
   fflush(stdout);
   cJSON_Delete(query);
   return;
 }

 cJSON *is_update = cJSON_GetObjectItem(query, "is_update");
 cJSON *fields_count = cJSON_GetObjectItem(query, "fields_count");
 cJSON *precisionObj = cJSON_GetObjectItem(query, "precision");
 cJSON *id = cJSON_GetObjectItem(query, "id");
 if (!cJSON_IsBool(is_update) ||
     !cJSON_IsNumber(fields_count) ||
     !cJSON_IsNumber(precisionObj) ||
     !cJSON_IsNumber(id)) {
   fprintf(stderr, "Invalid or miss 'is_update'/'fields_count'/'precision'/'id' in query response.\n");
   cJSON_Delete(query);
   return;
 }
 args.id = id->valueint;

 if (is_update->valueint) {
   cJSON *affected_rows = cJSON_GetObjectItem(query, "affected_rows");
   if (cJSON_IsNumber(affected_rows)) {
     et = taosGetTimestampUs();
     printf("Update OK, %d row(s) in set (%.6fs)\n\n", (int)affected_rows->valueint, (et - args.st) / 1E6);
   } else {
     fprintf(stderr, "Invalid or miss 'affected_rows' key in response\n");
   }
   cJSON_Delete(query);
   return;
 }

 int error_no = 0;

 int numOfRows = wsclientDumpResult(query, fname, &error_no, printMode, limit);
 if (numOfRows < 0) {
   cJSON_Delete(query);
   return;
 }
 et = taosGetTimestampUs();
 if (error_no == 0) {
   printf("Query OK, %d row(s) in set (%.6fs)\n", numOfRows, (et - args.st) / 1E6);
 } else {
   printf("Query interrupted, %d row(s) in set (%.6fs)\n", numOfRows, (et - args.st) / 1E6);
 }

 printf("\n");
 if (fname != NULL) {
   wordfree(&full_path);
 }
 cJSON_Delete(query);
}

void shellRunCommandOnServer(TAOS *con, char command[]) {
  int64_t   st, et;
  wordexp_t full_path;
  char *    sptr = NULL;
  char *    cptr = NULL;
  char *    fname = NULL;
  bool      printMode = false;

  if ((sptr = tstrstr(command, ">>", true)) != NULL) {
    cptr = tstrstr(command, ";", true);
    if (cptr != NULL) {
      *cptr = '\0';
    }

    if (wordexp(sptr + 2, &full_path, 0) != 0) {
      fprintf(stderr, "ERROR: invalid filename: %s\n", sptr + 2);
      return;
    }
    *sptr = '\0';
    fname = full_path.we_wordv[0];
  }

  if ((sptr = tstrstr(command, "\\G", true)) != NULL) {
    cptr = tstrstr(command, ";", true);
    if (cptr != NULL) {
      *cptr = '\0';
    }

    *sptr = '\0';
    printMode = true;  // When output to a file, the switch does not work.
  }

 st = taosGetTimestampUs();

  TAOS_RES* pSql = taos_query_h(con, command, &result);
  if (taos_errno(pSql)) {
    taos_error(pSql, st);
    return;
  }

  int64_t oresult = atomic_load_64(&result);

  if (regex_match(command, "^\\s*use\\s+[a-zA-Z0-9_]+\\s*;\\s*$", REG_EXTENDED | REG_ICASE)) {
    fprintf(stdout, "Database changed.\n\n");
    fflush(stdout);

    atomic_store_64(&result, 0);
    freeResultWithRid(oresult);
    return;
  }

  if (tscIsDeleteQuery(pSql)) {
    // delete
    int numOfRows   = taos_affected_rows(pSql);
    int numOfTables = taos_affected_tables(pSql);
    int error_no    = taos_errno(pSql);

    et = taosGetTimestampUs();
    if (error_no == TSDB_CODE_SUCCESS) {
      printf("Deleted %d row(s) from %d table(s) (%.6fs)\n", numOfRows, numOfTables, (et - st) / 1E6);
    } else {
      printf("Deleted interrupted (%s), %d row(s) from %d tables (%.6fs)\n", taos_errstr(pSql), numOfRows, numOfTables, (et - st) / 1E6);
    }
  }
  else if (!tscIsUpdateQuery(pSql)) {  // select and show kinds of commands
    int error_no = 0;

    int numOfRows = shellDumpResult(pSql, fname, &error_no, printMode);
    if (numOfRows < 0) {
      atomic_store_64(&result, 0);
      freeResultWithRid(oresult);
      return;
    }

    et = taosGetTimestampUs();
    if (error_no == 0) {
      printf("Query OK, %d row(s) in set (%.6fs)\n", numOfRows, (et - st) / 1E6);
    } else {
      printf("Query interrupted (%s), %d row(s) in set (%.6fs)\n", taos_errstr(pSql), numOfRows, (et - st) / 1E6);
    }
  } else {
    int num_rows_affacted = taos_affected_rows(pSql);
    et = taosGetTimestampUs();
    printf("Query OK, %d of %d row(s) in database (%.6fs)\n", num_rows_affacted, num_rows_affacted, (et - st) / 1E6);
  }

  printf("\n");

  if (fname != NULL) {
    wordfree(&full_path);
  }

  atomic_store_64(&result, 0);
  freeResultWithRid(oresult);
}

/* Function to do regular expression check */
int regex_match(const char *s, const char *reg, int cflags) {
  regex_t regex;
  char    msgbuf[100] = {0};

  /* Compile regular expression */
  if (regcomp(&regex, reg, cflags) != 0) {
    fprintf(stderr, "Fail to compile regex");
    exitShell();
  }

  /* Execute regular expression */
  int reti = regexec(&regex, s, 0, NULL, 0);
  if (!reti) {
    regfree(&regex);
    return 1;
  } else if (reti == REG_NOMATCH) {
    regfree(&regex);
    return 0;
  }
#ifdef DARWIN
  else if (reti == REG_ILLSEQ){
    regfree(&regex);
    return 0;
  }
#endif
  else {
    regerror(reti, &regex, msgbuf, sizeof(msgbuf));
    fprintf(stderr, "Regex match failed: %s\n", msgbuf);
    regfree(&regex);
    exitShell();
  }

  return 0;
}


static char* formatTimestamp(char* buf, int64_t val, int precision) {
  if (args.is_raw_time) {
    sprintf(buf, "%" PRId64, val);
    return buf;
  }

  time_t tt;
  int32_t ms = 0;
  if (precision == TSDB_TIME_PRECISION_NANO) {
    tt = (time_t)(val / 1000000000);
    ms = val % 1000000000;
  } else if (precision == TSDB_TIME_PRECISION_MICRO) {
    tt = (time_t)(val / 1000000);
    ms = val % 1000000;
  } else {
    tt = (time_t)(val / 1000);
    ms = val % 1000;
  }

/* comment out as it make testcases like select_with_tags.sim fail.
  but in windows, this may cause the call to localtime crash if tt < 0,
  need to find a better solution.
  if (tt < 0) {
    tt = 0;
  }
  */
#ifdef WINDOWS
  if (tt < 0) {
    SYSTEMTIME a={1970,1,5,1,0,0,0,0}; // SYSTEMTIME struct support 1601-01-01. set 1970 to compatible with Epoch time.
    FILETIME b; // unit is 100ns
    ULARGE_INTEGER c;
    SystemTimeToFileTime(&a,&b);
    c.LowPart = b.dwLowDateTime;
    c.HighPart = b.dwHighDateTime;
    c.QuadPart+=tt*10000000;
    b.dwLowDateTime=c.LowPart;
    b.dwHighDateTime=c.HighPart;
    FileTimeToLocalFileTime(&b,&b);
    FileTimeToSystemTime(&b,&a);
    int pos = sprintf(buf,"%02d-%02d-%02d %02d:%02d:%02d", a.wYear, a.wMonth,a.wDay, a.wHour, a.wMinute, a.wSecond);
    if (precision == TSDB_TIME_PRECISION_NANO) {
      sprintf(buf + pos, ".%09d", ms);
    } else if (precision == TSDB_TIME_PRECISION_MICRO) {
      sprintf(buf + pos, ".%06d", ms);
    } else {
      sprintf(buf + pos, ".%03d", ms);
    }
    return buf;
  }
#endif
  if (tt <= 0 && ms < 0) {
    tt--;
    if (precision == TSDB_TIME_PRECISION_NANO) {
      ms += 1000000000;
    } else if (precision == TSDB_TIME_PRECISION_MICRO) {
      ms += 1000000;
    } else {
      ms += 1000;
    }
  }

  struct tm* ptm = localtime(&tt);
  size_t pos = strftime(buf, 35, "%Y-%m-%d %H:%M:%S", ptm);

  if (precision == TSDB_TIME_PRECISION_NANO) {
    sprintf(buf + pos, ".%09d", ms);
  } else if (precision == TSDB_TIME_PRECISION_MICRO) {
    sprintf(buf + pos, ".%06d", ms);
  } else {
    sprintf(buf + pos, ".%03d", ms);
  }

  return buf;
}


static void dumpFieldToFile(FILE* fp, const char* val, TAOS_FIELD* field, int32_t length, int precision) {
  if (val == NULL) {
    fprintf(fp, "%s", TSDB_DATA_NULL_STR);
    return;
  }

  int  n;
  char buf[TSDB_MAX_BYTES_PER_ROW];
  switch (field->type) {
    case TSDB_DATA_TYPE_BOOL:
      fprintf(fp, "%d", ((((int32_t)(*((char *)val))) == 1) ? 1 : 0));
      break;
    case TSDB_DATA_TYPE_TINYINT:
      fprintf(fp, "%d", *((int8_t *)val));
      break;
    case TSDB_DATA_TYPE_UTINYINT:
      fprintf(fp, "%u", *((uint8_t *)val));
      break;
    case TSDB_DATA_TYPE_SMALLINT:
      fprintf(fp, "%d", *((int16_t *)val));
      break;
    case TSDB_DATA_TYPE_USMALLINT:
      fprintf(fp, "%u", *((uint16_t *)val));
      break;
    case TSDB_DATA_TYPE_INT:
      fprintf(fp, "%d", *((int32_t *)val));
      break;
    case TSDB_DATA_TYPE_UINT:
      fprintf(fp, "%u", *((uint32_t *)val));
      break;
    case TSDB_DATA_TYPE_BIGINT:
      fprintf(fp, "%" PRId64, *((int64_t *)val));
      break;
    case TSDB_DATA_TYPE_UBIGINT:
      fprintf(fp, "%" PRIu64, *((uint64_t *)val));
      break;
    case TSDB_DATA_TYPE_FLOAT:
      fprintf(fp, "%.5f", GET_FLOAT_VAL(val));
      break;
    case TSDB_DATA_TYPE_DOUBLE:
      n = snprintf(buf, TSDB_MAX_BYTES_PER_ROW, "%*.9f", length, GET_DOUBLE_VAL(val));
      if (n > MAX(25, length)) {
        fprintf(fp, "%*.15e", length, GET_DOUBLE_VAL(val));
      } else {
        fprintf(fp, "%s", buf);
      }
      break;
    case TSDB_DATA_TYPE_BINARY:
    case TSDB_DATA_TYPE_NCHAR:
    case TSDB_DATA_TYPE_JSON:
      memcpy(buf, val, length);
      buf[length] = 0;
      fprintf(fp, "\'%s\'", buf);
      break;
    case TSDB_DATA_TYPE_TIMESTAMP:
      formatTimestamp(buf, *(int64_t*)val, precision);
      fprintf(fp, "'%s'", buf);
      break;
    default:
      break;
  }
}

static int dumpResultToFile(const char* fname, TAOS_RES* tres) {
  TAOS_ROW row = taos_fetch_row(tres);
  if (row == NULL) {
    return 0;
  }

  wordexp_t full_path;

  if (wordexp((char *)fname, &full_path, 0) != 0) {
    fprintf(stderr, "ERROR: invalid file name: %s\n", fname);
    return -1;
  }

  FILE* fp = fopen(full_path.we_wordv[0], "w");
  if (fp == NULL) {
    fprintf(stderr, "ERROR: failed to open file: %s\n", full_path.we_wordv[0]);
    wordfree(&full_path);
    return -1;
  }

  wordfree(&full_path);

  int num_fields = taos_num_fields(tres);
  TAOS_FIELD *fields = taos_fetch_fields(tres);
  int precision = taos_result_precision(tres);

  for (int col = 0; col < num_fields; col++) {
    if (col > 0) {
      fprintf(fp, ",");
    }
    fprintf(fp, "%s", fields[col].name);
  }
  fputc('\n', fp);

  int numOfRows = 0;
  do {
    int32_t* length = taos_fetch_lengths(tres);
    for (int i = 0; i < num_fields; i++) {
      if (i > 0) {
        fputc(',', fp);
      }
      dumpFieldToFile(fp, (const char*)row[i], fields +i, length[i], precision);
    }
    fputc('\n', fp);

    numOfRows++;
    row = taos_fetch_row(tres);
  } while( row != NULL);

  result = 0;
  fclose(fp);

  return numOfRows;
}


static void shellPrintNChar(const char *str, int length, int width) {
  wchar_t tail[3];
  int pos = 0, cols = 0, totalCols = 0, tailLen = 0;

  while (pos < length) {
    wchar_t wc;
    int bytes = mbtowc(&wc, str + pos, MB_CUR_MAX);
    if (bytes <= 0) {
      break;
    }
    if (pos + bytes > length) {
      break;
    }
    int w = 0;
#ifdef WINDOWS
    w = bytes;
#else
    if(*(str + pos) == '\t' || *(str + pos) == '\n' || *(str + pos) == '\r'){
      w = bytes;
    }else{
      w = wcwidth(wc);
    }
#endif
    pos += bytes;
    if (w <= 0) {
      continue;
    }

    if (width <= 0) {
      printf("%lc", wc);
      continue;
    }

    totalCols += w;
    if (totalCols > width) {
      break;
    }
    if (totalCols <= (width - 3)) {
      printf("%lc", wc);
      cols += w;
    } else {
      tail[tailLen] = wc;
      tailLen++;
    }
  }

  if (totalCols > width) {
    // width could be 1 or 2, so printf("...") cannot be used
    for (int i = 0; i < 3; i++) {
      if (cols >= width) {
        break;
      }
      putchar('.');
      ++cols;
    }
  } else {
    for (int i = 0; i < tailLen; i++) {
      printf("%lc", tail[i]);
    }
    cols = totalCols;
  }

  for (; cols < width; cols++) {
    putchar(' ');
  }
}


static void printField(const char* val, TAOS_FIELD* field, int width, int32_t length, int precision) {
  if (val == NULL) {
    int w = width;
    if (field->type == TSDB_DATA_TYPE_BINARY || field->type == TSDB_DATA_TYPE_NCHAR || field->type == TSDB_DATA_TYPE_TIMESTAMP) {
      w = 0;
    }
    w = printf("%*s", w, TSDB_DATA_NULL_STR);
    for (; w < width; w++) {
      putchar(' ');
    }
    return;
  }

  int  n;
  char buf[TSDB_MAX_BYTES_PER_ROW];
  switch (field->type) {
    case TSDB_DATA_TYPE_BOOL:
      printf("%*s", width, ((((int32_t)(*((char *)val))) == 1) ? "true" : "false"));
      break;
    case TSDB_DATA_TYPE_TINYINT:
      printf("%*d", width, *((int8_t *)val));
      break;
    case TSDB_DATA_TYPE_UTINYINT:
      printf("%*u", width, *((uint8_t *)val));
      break;
    case TSDB_DATA_TYPE_SMALLINT:
      printf("%*d", width, *((int16_t *)val));
      break;
    case TSDB_DATA_TYPE_USMALLINT:
      printf("%*u", width, *((uint16_t *)val));
      break;
    case TSDB_DATA_TYPE_INT:
      printf("%*d", width, *((int32_t *)val));
      break;
    case TSDB_DATA_TYPE_UINT:
      printf("%*u", width, *((uint32_t *)val));
      break;
    case TSDB_DATA_TYPE_BIGINT:
      printf("%*" PRId64, width, *((int64_t *)val));
      break;
    case TSDB_DATA_TYPE_UBIGINT:
      printf("%*" PRIu64, width, *((uint64_t *)val));
      break;
    case TSDB_DATA_TYPE_FLOAT:
      printf("%*.5f", width, GET_FLOAT_VAL(val));
      break;
    case TSDB_DATA_TYPE_DOUBLE:
      n = snprintf(buf, TSDB_MAX_BYTES_PER_ROW, "%*.9f", width, GET_DOUBLE_VAL(val));
      if (n > MAX(25, width)) {
        printf("%*.15e", width, GET_DOUBLE_VAL(val));
      } else {
        printf("%s", buf);
      }
      break;
    case TSDB_DATA_TYPE_BINARY:
    case TSDB_DATA_TYPE_NCHAR:
    case TSDB_DATA_TYPE_JSON:
      shellPrintNChar(val, length, width);
      break;
    case TSDB_DATA_TYPE_TIMESTAMP:
      formatTimestamp(buf, *(int64_t*)val, precision);
      printf("%s", buf);
      break;
    default:
      break;
  }
}


bool isSelectQuery(TAOS_RES* tres) {
  char *sql = tscGetSqlStr(tres);

  if (regex_match(sql, "^[\t ]*select[ \t]*", REG_EXTENDED | REG_ICASE)) {
    return true;
  }

  return false;
}


static int verticalPrintResult(TAOS_RES* tres) {
  TAOS_ROW row = taos_fetch_row(tres);
  if (row == NULL) {
    return 0;
  }

  int num_fields = taos_num_fields(tres);
  TAOS_FIELD *fields = taos_fetch_fields(tres);
  int precision = taos_result_precision(tres);

  int maxColNameLen = 0;
  for (int col = 0; col < num_fields; col++) {
    int len = (int)strlen(fields[col].name);
    if (len > maxColNameLen) {
      maxColNameLen = len;
    }
  }

  uint64_t resShowMaxNum = UINT64_MAX;

  if (args.commands == NULL && args.file[0] == 0 && isSelectQuery(tres) && !tscIsQueryWithLimit(tres)) {
    resShowMaxNum = DEFAULT_RES_SHOW_NUM;
  }

  int numOfRows = 0;
  int showMore = 1;
  do {
    if (numOfRows < resShowMaxNum) {
      printf("*************************** %d.row ***************************\n", numOfRows + 1);

      int32_t* length = taos_fetch_lengths(tres);

      for (int i = 0; i < num_fields; i++) {
        TAOS_FIELD* field = fields + i;

        int padding = (int)(maxColNameLen - strlen(field->name));
        printf("%*.s%s: ", padding, " ", field->name);

        printField((const char*)row[i], field, 0, length[i], precision);
        putchar('\n');
      }
    } else if (showMore) {
        printf("\n");
        printf(" Notice: The result shows only the first %d rows.\n", DEFAULT_RES_SHOW_NUM);
        printf("         You can use the `LIMIT` clause to get fewer result to show.\n");
        printf("           Or use '>>' to redirect the whole set of the result to a specified file.\n");
        printf("\n");
        printf("         You can use Ctrl+C to stop the underway fetching.\n");
        printf("\n");
        showMore = 0;
    }

    numOfRows++;
    row = taos_fetch_row(tres);
  } while(row != NULL);

  return numOfRows;
}

static int calcColWidth(TAOS_FIELD* field, int precision) {
  int width = (int)strlen(field->name);

  switch (field->type) {
    case TSDB_DATA_TYPE_BOOL:
      return MAX(5, width); // 'false'

    case TSDB_DATA_TYPE_TINYINT:
    case TSDB_DATA_TYPE_UTINYINT:
      return MAX(4, width); // '-127'

    case TSDB_DATA_TYPE_SMALLINT:
    case TSDB_DATA_TYPE_USMALLINT:
      return MAX(6, width); // '-32767'

    case TSDB_DATA_TYPE_INT:
    case TSDB_DATA_TYPE_UINT:
      return MAX(11, width); // '-2147483648'

    case TSDB_DATA_TYPE_BIGINT:
    case TSDB_DATA_TYPE_UBIGINT:
      return MAX(21, width); // '-9223372036854775807'

    case TSDB_DATA_TYPE_FLOAT:
      return MAX(20, width);

    case TSDB_DATA_TYPE_DOUBLE:
      return MAX(25, width);

    case TSDB_DATA_TYPE_BINARY:
      if (field->bytes > tsMaxBinaryDisplayWidth) {
        return MAX(tsMaxBinaryDisplayWidth, width);
      } else {
        return MAX(field->bytes, width);
      }

    case TSDB_DATA_TYPE_NCHAR:
    case TSDB_DATA_TYPE_JSON:{
      int16_t bytes = field->bytes * TSDB_NCHAR_SIZE;
      if (bytes > tsMaxBinaryDisplayWidth) {
        return MAX(tsMaxBinaryDisplayWidth, width);
      } else {
        return MAX(bytes, width);
      }
    }

    case TSDB_DATA_TYPE_TIMESTAMP:
      if (args.is_raw_time) {
        return MAX(14, width);
      } if (precision == TSDB_TIME_PRECISION_NANO) {
        return MAX(29, width);
      } else if (precision == TSDB_TIME_PRECISION_MICRO) {
        return MAX(26, width); // '2020-01-01 00:00:00.000000'
      } else {
        return MAX(23, width); // '2020-01-01 00:00:00.000'
      }

    default:
      assert(false);
  }

  return 0;
}


static void printHeader(TAOS_FIELD* fields, int* width, int num_fields) {
  int rowWidth = 0;
  for (int col = 0; col < num_fields; col++) {
    TAOS_FIELD* field = fields + col;
    int padding = (int)(width[col] - strlen(field->name));
    int left = padding / 2;
    printf(" %*.s%s%*.s |", left, " ", field->name, padding - left, " ");
    rowWidth += width[col] + 3;
  }

  putchar('\n');
  for (int i = 0; i < rowWidth; i++) {
    putchar('=');
  }
  putchar('\n');
}


static int horizontalPrintResult(TAOS_RES* tres) {
  TAOS_ROW row = taos_fetch_row(tres);
  if (row == NULL) {
    return 0;
  }

  int num_fields = taos_num_fields(tres);
  TAOS_FIELD *fields = taos_fetch_fields(tres);
  int precision = taos_result_precision(tres);

  int width[TSDB_MAX_COLUMNS];
  for (int col = 0; col < num_fields; col++) {
    width[col] = calcColWidth(fields + col, precision);
  }

  printHeader(fields, width, num_fields);

  uint64_t resShowMaxNum = UINT64_MAX;

  if (args.commands == NULL && args.file[0] == 0 && isSelectQuery(tres) && !tscIsQueryWithLimit(tres)) {
    resShowMaxNum = DEFAULT_RES_SHOW_NUM;
  }

  int numOfRows = 0;
  int showMore = 1;

  do {
    int32_t* length = taos_fetch_lengths(tres);
    if (numOfRows < resShowMaxNum) {
      for (int i = 0; i < num_fields; i++) {
        putchar(' ');
        printField((const char*)row[i], fields + i, width[i], length[i], precision);
        putchar(' ');
        putchar('|');
      }
      putchar('\n');
    } else if (showMore) {
        printf("\n");
        printf(" Notice: The result shows only the first %d rows.\n", DEFAULT_RES_SHOW_NUM);
        printf("         You can use the `LIMIT` clause to get fewer result to show.\n");
        printf("           Or use '>>' to redirect the whole set of the result to a specified file.\n");
        printf("\n");
        printf("         You can use Ctrl+C to stop the underway fetching.\n");
        printf("\n");
        showMore = 0;
    }

    numOfRows++;
    row = taos_fetch_row(tres);
  } while(row != NULL);

  return numOfRows;
}

cJSON* wsclient_fetch(bool fetch_block) {
 if (wsclient_send_sql(NULL, WS_FETCH)) {
   return NULL;
 }
 pthread_create(&rpid, NULL, recvHandler, NULL);
 pthread_join(rpid, NULL);
 if (wsclient.status != TCP_CONNECTED && wsclient.status != WS_CONNECTED) {
   fprintf(stderr, "websocket receive failed, reason: %s\n", wsclient_strerror(wsclient.status));
   return NULL;
 }
 cJSON *fetch = cJSON_Parse(args.response_buffer);
 if (fetch == NULL) {
   fprintf(stderr, "failed to parse response into json: %s\n", args.response_buffer);
   return NULL;
 }
 tfree(args.response_buffer);
 if (wsclient_check(fetch)) {
   cJSON_Delete(fetch);
   return NULL;
 }
 cJSON* rowsObj = cJSON_GetObjectItem(fetch, "rows");
 cJSON *completedObj = cJSON_GetObjectItem(fetch, "completed");
 if (!cJSON_IsBool(completedObj) || !cJSON_IsNumber(rowsObj)) {
   fprintf(stderr, "Invalid or miss 'completed'/'rows' in fetch response\n");
   cJSON_Delete(fetch);
   return NULL;
 }
 if (completedObj->valueint || !fetch_block) {
   return fetch;
 } else {
   cJSON* lengths = cJSON_GetObjectItem(fetch, "lengths");
   if (!cJSON_IsArray(lengths)) {
     fprintf(stderr, "Invalid or miss 'lengths' key in fetch response\n");
     cJSON_Delete(fetch);
     return NULL;
   }
   cJSON* current_child = lengths->child;
   int index = 0;
   while (current_child != NULL) {
     args.fields[index].bytes = (int16_t)current_child->valueint;
     current_child = current_child->next;
     index++;
   }

   if (wsclient_send_sql(NULL, WS_FETCH_BLOCK)) {
     cJSON_Delete(fetch);
     return NULL;
   }
   pthread_create(&rpid, NULL, recvHandler, NULL);
   pthread_join(rpid, NULL);
   if (wsclient.status != TCP_CONNECTED && wsclient.status != WS_CONNECTED) {
     fprintf(stderr, "websocket receive failed, reason: %s\n", wsclient_strerror(wsclient.status));
     cJSON_Delete(fetch);
     return NULL;
   }

   if (*(int64_t *)args.response_buffer != args.id) {
     fprintf(stderr, "Mismatch id with %"PRId64" expect %"PRId64"\n", *(int64_t *)args.response_buffer, args.id);
     cJSON_Delete(fetch);
     return NULL;
   }
   return fetch;
 }
}

int wsclient_fetch_fields(cJSON *query, TAOS_FIELD * fields, int cols) {
 cJSON *fields_names = cJSON_GetObjectItem(query, "fields_names");
 cJSON *fields_types = cJSON_GetObjectItem(query, "fields_types");
 cJSON *fields_lengths = cJSON_GetObjectItem(query, "fields_lengths");
 if (!cJSON_IsArray(fields_names) || !cJSON_IsArray(fields_types) || !cJSON_IsArray(fields_lengths)) {
   fprintf(stderr, "Invalid or miss 'fields_names'/'fields_types'/'fields_lengths' key in response\n");
   return -1;
 }
 for (int i = 0; i < cols; i++) {
   cJSON* field_name = cJSON_GetArrayItem(fields_names, i);
   cJSON* field_type = cJSON_GetArrayItem(fields_types, i);
   cJSON* field_length = cJSON_GetArrayItem(fields_lengths, i);
   if (!cJSON_IsString(field_name) || !cJSON_IsNumber(field_type) || !cJSON_IsNumber(field_length)) {
     fprintf(stderr, "Invalid or miss 'field_name'/'field_type'/'field_length' in query response");
     return -1;
   }
   strncpy(fields[i].name, field_name->valuestring, TSDB_COL_NAME_LEN);
   fields[i].type = (uint8_t)field_type->valueint;
   fields[i].bytes = (int16_t)field_length->valueint;
 }
 return 0;
}

int wsHorizontalPrintRes(cJSON* query, uint64_t limit) {
 bool fetch_block = true;

 int num_fields = (int)cJSON_GetObjectItem(query, "fields_count")->valueint;
 args.fields = calloc(num_fields, sizeof(TAOS_FIELD));
 if (wsclient_fetch_fields(query, args.fields, num_fields)) {
   return 0;
 }
 cJSON* precisionObj = cJSON_GetObjectItem(query, "precision");
 if (!cJSON_IsNumber(precisionObj)) {
   fprintf(stderr, "Invalid or miss precision key in query response\n");
   return 0;
 }
 int precision = (int)precisionObj->valueint;

 cJSON * fetch = wsclient_fetch(fetch_block);
 if (fetch == NULL) {
   return 0;
 }

 int width[TSDB_MAX_COLUMNS];
 for (int col = 0; col < num_fields; col++) {
   width[col] = calcColWidth(args.fields + col, precision);
 }

 printHeader(args.fields, width, num_fields);

 int numOfRows = 0;
 int showMore = 1;
 int64_t rows;
 int64_t pos;

 do {
   cJSON* rowsObj = cJSON_GetObjectItem(fetch, "rows");
   cJSON *completedObj = cJSON_GetObjectItem(fetch, "completed");
   rows = rowsObj->valueint;
   if (completedObj->valueint) {
     break;
   }

   if (numOfRows < limit) {
     for (int64_t i = 0; i < rows; i++) {
       if (numOfRows >= limit) {
         numOfRows = numOfRows - limit + rows;
         fetch_block = false;
         break;
       }
       for (int j = 0; j < num_fields; ++j) {
         pos = 8;
         pos += i * args.fields[j].bytes;
         for (int k = 0; k < j; ++k) {
           pos += args.fields[k].bytes * rows;
         }
         putchar(' ');
         int16_t length = 0;
         if (args.fields[j].type == TSDB_DATA_TYPE_NCHAR || args.fields[j].type == TSDB_DATA_TYPE_BINARY ||
             args.fields[j].type == TSDB_DATA_TYPE_JSON) {
           length = *(int16_t *)(args.response_buffer + pos);
           pos += 2;
         }
         printField((const char *)(args.response_buffer + pos), args.fields + j, width[j], (int32_t)length, precision);
         putchar(' ');
         putchar('|');
       }
       putchar('\n');
       numOfRows += 1;
     }
   } else if (showMore) {
     printf("\n");
     printf(" Notice: The result shows only the first %d rows.\n", DEFAULT_RES_SHOW_NUM);
     printf("         You can use the `LIMIT` clause to get fewer result to show.\n");
     printf("           Or use '>>' to redirect the whole set of the result to a specified file.\n");
     printf("\n");
     printf("         You can use Ctrl+C to stop the underway fetching.\n");
     printf("\n");
     showMore = 0;
   } else {
     numOfRows += rows;
   }
   cJSON_Delete(fetch);
   fetch = wsclient_fetch(fetch_block);
 } while (fetch != NULL);

 cJSON_Delete(fetch);
 return numOfRows;
}

int wsVericalPrintRes(cJSON* query, uint64_t limit) {
 bool fetch_block = true;

 int num_fields = (int)cJSON_GetObjectItem(query, "fields_count")->valueint;
 args.fields = calloc(num_fields, sizeof(TAOS_FIELD));
 if (wsclient_fetch_fields(query, args.fields, num_fields)) {
   return 0;
 }
 cJSON* precisionObj = cJSON_GetObjectItem(query, "precision");
 if (!cJSON_IsNumber(precisionObj)) {
   fprintf(stderr, "Invalid or miss precision key in query response\n");
   return 0;
 }
 int precision = (int)precisionObj->valueint;

 cJSON* fetch = wsclient_fetch(fetch_block);
 if (fetch == NULL) {
   return 0;
 }

 int maxColNameLen = 0;
 for (int col = 0; col < num_fields; col++) {
   int len = (int)strlen(args.fields[col].name);
   if (len > maxColNameLen) {
     maxColNameLen = len;
   }
 }

 int numOfRows = 0;
 int showMore = 1;
 int64_t rows;
 int64_t pos;

 do {
   rows = cJSON_GetObjectItem(fetch, "rows")->valueint;
   cJSON *completedObj = cJSON_GetObjectItem(fetch, "completed");
   if (completedObj->valueint) {
     break;
   }
   if (numOfRows < limit) {
     for (int i = 0; i < rows; ++i) {
       if (numOfRows > limit) {
         fetch_block = false;
         break;
       }
       printf("*************************** %d.row ***************************\n", numOfRows + 1);
       for (int j = 0; j < num_fields; ++j) {
         pos = 8;
         pos += i * args.fields[j].bytes;
         for (int k = 0; k < j; ++k) {
           pos += args.fields[k].bytes * rows;
         }
         int16_t length = 0;
         if (args.fields[j].type == TSDB_DATA_TYPE_NCHAR || args.fields[j].type == TSDB_DATA_TYPE_BINARY ||
             args.fields[j].type == TSDB_DATA_TYPE_JSON) {
           length = *(int16_t *)(args.response_buffer + pos);
           pos += 2;
         }
         int padding = (int)(maxColNameLen - strlen(args.fields[j].name));
         printf("%*.s%s: ", padding, " ", args.fields[j].name);
         printField((const char *)(args.response_buffer + pos), args.fields + j, 0, (int32_t)length, precision);
         putchar('\n');
       }
       putchar('\n');
       numOfRows ++;
     }
   } else if (showMore) {
     printf("\n");
     printf(" Notice: The result shows only the first %d rows.\n", DEFAULT_RES_SHOW_NUM);
     printf("         You can use the `LIMIT` clause to get fewer result to show.\n");
     printf("           Or use '>>' to redirect the whole set of the result to a specified file.\n");
     printf("\n");
     printf("         You can use Ctrl+C to stop the underway fetching.\n");
     printf("\n");
     showMore = 0;
   } else {
     numOfRows += rows;
   }
   cJSON_Delete(fetch);
   fetch = wsclient_fetch(fetch_block);
 } while (fetch != NULL);
 cJSON_Delete(fetch);
 return numOfRows;
}

int wsDumpResultToFile(char* fname, cJSON* query, uint64_t limit) {
 wordexp_t full_path;

 if (wordexp((char *)fname, &full_path, 0) != 0) {
   fprintf(stderr, "ERROR: invalid file name: %s\n", fname);
   return -1;
 }

 FILE* fp = fopen(full_path.we_wordv[0], "w");
 if (fp == NULL) {
   fprintf(stderr, "ERROR: failed to open file: %s\n", full_path.we_wordv[0]);
   wordfree(&full_path);
   return -1;
 }

 wordfree(&full_path);

 int num_fields = (int)cJSON_GetObjectItem(query, "fields_count")->valueint;
 args.fields = calloc(num_fields, sizeof(TAOS_FIELD));
 if (wsclient_fetch_fields(query, args.fields, num_fields)) {
   return 0;
 }
 cJSON* precisionObj = cJSON_GetObjectItem(query, "precision");
 if (!cJSON_IsNumber(precisionObj)) {
   fprintf(stderr, "Invalid or miss precision key in query response\n");
   return 0;
 }
 int precision = (int)precisionObj->valueint;

 cJSON* fetch = wsclient_fetch(true);
 if (fetch == NULL) {
   return 0;
 }

 for (int i = 0; i < num_fields; ++i) {
   if (i > 0) {
     fprintf(fp, ",");
   }
   fprintf(fp, "%s", args.fields[i].name);
 }
 fputc('\n', fp);

 int numOfRows = 0;
 int64_t rows;
 int64_t pos;

 do {
   rows = cJSON_GetObjectItem(fetch, "rows")->valueint;
   cJSON *completedObj = cJSON_GetObjectItem(fetch, "completed");
   if (completedObj->valueint) {
     break;
   }
   for (int i = 0; i < rows; ++i) {
     for (int j = 0; j < num_fields; ++j) {
       pos = 8;
       pos += i * args.fields[j].bytes;
       for (int k = 0; k < j; ++k) {
         pos += args.fields[k].bytes * rows;
       }
       int16_t length = 0;
       if (args.fields[j].type == TSDB_DATA_TYPE_NCHAR || args.fields[j].type == TSDB_DATA_TYPE_BINARY ||
           args.fields[j].type == TSDB_DATA_TYPE_JSON) {
         length = *(int16_t *)(args.response_buffer + pos);
         pos += 2;
       }
       if (j > 0) {
         fputc(',', fp);
       }
       dumpFieldToFile(fp, (const char *)(args.response_buffer + pos), args.fields + j, (int32_t)length, precision);
     }
     fputc('\n', fp);
   }
   fetch = wsclient_fetch(fetch);
 } while (fetch != NULL);

 fclose(fp);
 cJSON_Delete(fetch);
 return numOfRows;
}

int wsclientDumpResult(cJSON* query, char *fname, int *error_no, bool vertical, uint64_t limit) {
 int numOfRows = 0;
 if (fname != NULL) {
   numOfRows = wsDumpResultToFile(fname, query, limit);
 } else if (vertical) {
   numOfRows = wsVericalPrintRes(query, limit);
 } else {
   numOfRows = wsHorizontalPrintRes(query, limit);
 }
 tfree(args.fields);
 return numOfRows;
}

int shellDumpResult(TAOS_RES *tres, char *fname, int *error_no, bool vertical) {
  int numOfRows = 0;
  if (fname != NULL) {
    numOfRows = dumpResultToFile(fname, tres);
  } else if(vertical) {
    numOfRows = verticalPrintResult(tres);
  } else {
    numOfRows = horizontalPrintResult(tres);
  }

  *error_no = taos_errno(tres);
  return numOfRows;
}


void read_history() {
  // Initialize history
  memset(history.hist, 0, sizeof(char *) * MAX_HISTORY_SIZE);
  history.hstart = 0;
  history.hend = 0;
  char * line = NULL;
  size_t line_size = 0;
  int    read_size = 0;

  char f_history[TSDB_FILENAME_LEN];
  get_history_path(f_history);

  FILE *f = fopen(f_history, "r");
  if (f == NULL) {
#ifndef WINDOWS
    if (errno != ENOENT) {
      fprintf(stderr, "Failed to open file %s, reason:%s\n", f_history, strerror(errno));
    }
#endif
    return;
  }

  while ((read_size = tgetline(&line, &line_size, f)) != -1) {
    line[read_size - 1] = '\0';
    history.hist[history.hend] = strdup(line);

    history.hend = (history.hend + 1) % MAX_HISTORY_SIZE;

    if (history.hend == history.hstart) {
      history.hstart = (history.hstart + 1) % MAX_HISTORY_SIZE;
    }
  }

  free(line);
  fclose(f);
}

void write_history() {
  char f_history[TSDB_FILENAME_LEN];
  get_history_path(f_history);

  FILE *f = fopen(f_history, "w");
  if (f == NULL) {
#ifndef WINDOWS
    fprintf(stderr, "Failed to open file %s for write, reason:%s\n", f_history, strerror(errno));
#endif
    return;
  }

  for (int i = history.hstart; i != history.hend;) {
    if (history.hist[i] != NULL) {
      fprintf(f, "%s\n", history.hist[i]);
      tfree(history.hist[i]);
    }
    i = (i + 1) % MAX_HISTORY_SIZE;
  }
  fclose(f);
}

void taos_error(TAOS_RES *tres, int64_t st) {
  int64_t et = taosGetTimestampUs();
  atomic_store_ptr(&result, 0);
  fprintf(stderr, "\nDB error: %s (%.6fs)\n", taos_errstr(tres), (et - st) / 1E6);
  taos_free_result(tres);
}

int isCommentLine(char *line) {
  if (line == NULL) return 1;

  return regex_match(line, "^\\s*#.*", REG_EXTENDED);
}

void source_file(TAOS *con, char *fptr) {
  wordexp_t full_path;
  int       read_len = 0;
  char *    cmd = calloc(1, tsMaxSQLStringLen+1);
  size_t    cmd_len = 0;
  char *    line = NULL;
  size_t    line_len = 0;

  if (wordexp(fptr, &full_path, 0) != 0) {
    fprintf(stderr, "ERROR: illegal file name\n");
    free(cmd);
    return;
  }

  char *fname = full_path.we_wordv[0];

  /*
  if (access(fname, F_OK) != 0) {
    fprintf(stderr, "ERROR: file %s is not exist\n", fptr);

    wordfree(&full_path);
    free(cmd);
    return;
  }
  */

  FILE *f = fopen(fname, "r");
  if (f == NULL) {
    fprintf(stderr, "ERROR: failed to open file %s\n", fname);
    wordfree(&full_path);
    free(cmd);
    return;
  }

  while ((read_len = tgetline(&line, &line_len, f)) != -1) {
    if (read_len >= tsMaxSQLStringLen) continue;
    line[--read_len] = '\0';

    if (read_len == 0 || isCommentLine(line)) {  // line starts with #
      continue;
    }

    if (line[read_len - 1] == '\\') {
      line[read_len - 1] = ' ';
      memcpy(cmd + cmd_len, line, read_len);
      cmd_len += read_len;
      continue;
    }

    memcpy(cmd + cmd_len, line, read_len);
    printf("%s%s\n", PROMPT_HEADER, cmd);
    shellRunCommand(con, cmd);
    memset(cmd, 0, tsMaxSQLStringLen);
    cmd_len = 0;
  }

  free(cmd);
  if (line) free(line);
  wordfree(&full_path);
  fclose(f);
}

void shellGetGrantInfo(void *con) {
  return;
}

void _base64_encode_triple(unsigned char triple[3], char res[4]) {
  int tripleValue, i;

  tripleValue = triple[0];
  tripleValue *= 256;
  tripleValue += triple[1];
  tripleValue *= 256;
  tripleValue += triple[2];

  for (i = 0; i < 4; i++) {
    res[3 - i] = BASE64_CHARS[tripleValue % 64];
    tripleValue /= 64;
  }
}

int taos_base64_encode(unsigned char *source, size_t sourcelen, char *target, size_t targetlen) {
  /* check if the result will fit in the target buffer */
  if ((sourcelen + 2) / 3 * 4 > targetlen - 1) return 0;

  /* encode all full triples */
  while (sourcelen >= 3) {
    _base64_encode_triple(source, target);
    sourcelen -= 3;
    source += 3;
    target += 4;
  }

  /* encode the last one or two characters */
  if (sourcelen > 0) {
    unsigned char temp[3];
    memset(temp, 0, sizeof(temp));
    memcpy(temp, source, sourcelen);
    _base64_encode_triple(temp, target);
    target[3] = '=';
    if (sourcelen == 1) target[2] = '=';

    target += 4;
  }

  /* terminate the string */
  target[0] = 0;

  return 1;
}

int parse_cloud_dsn() {
    if (args.cloudDsn == NULL) {
        fprintf(stderr, "Cannot read cloud service info\n");
        return -1;
    } else {
        char *start = strstr(args.cloudDsn, "http://");
        if (start != NULL) {
            args.cloudHost = start + strlen("http://");
        } else {
            start = strstr(args.cloudDsn, "https://");
            if (start != NULL) {
                args.cloudHost = start + strlen("https://");
            } else {
                args.cloudHost = args.cloudDsn;
            }
        }
        char *port = strstr(args.cloudHost, ":");
        if ((port == NULL) || (port + strlen(":")) == NULL) {
            fprintf(stderr, "Invalid format in TDengine cloud dsn: %s\n", args.cloudDsn);
            return -1;
        }
        char *token = strstr(port + strlen(":"), "?token=");
        if ((token == NULL) || (token + strlen("?token=")) == NULL ||
            (strlen(token + strlen("?token=")) == 0)) {
            fprintf(stderr, "Invalid format in TDengine cloud dsn: %s\n", args.cloudDsn);
            return -1;
        }
        port[0] = '\0';
        args.cloudPort = port + strlen(":");
        token[0] = '\0';
        args.cloudToken = token + strlen("?token=");
    }
    return 0;
}

void releaseHttpResponse(HttpResponse *httpResponse) {
 if (httpResponse == NULL) {
   return;
 }
 tfree(httpResponse->version);
 tfree(httpResponse->code);
 tfree(httpResponse->desc);
 tfree(httpResponse->body);
 free(httpResponse);
 httpResponse = NULL;
}

HttpResponse *parseHttpResponse(char *response) {
 HttpResponse *_httpResponseTemp = (HttpResponse *) malloc(sizeof(HttpResponse));
 memset(_httpResponseTemp, 0, sizeof(HttpResponse));
 HttpResponse *_httpResponse = (HttpResponse *) malloc(sizeof(HttpResponse));
 memset(_httpResponse, 0, sizeof(HttpResponse));

 char *_start = response;
 for (; *_start && *_start != '\r'; _start++) {
   if (_httpResponseTemp->version == NULL) {
     _httpResponseTemp->version = _start;
   }

   if (*_start == ' ') {
     if (_httpResponseTemp->code == NULL) {
       _httpResponseTemp->code = _start + 1;
       *_start = '\0';
     } else if (_httpResponseTemp->desc == NULL) {
       _httpResponseTemp->desc = _start + 1;
       *_start = '\0';
     }
   }
 }
 *_start = '\0';
 _start++;

 _start++;
 char *_line = _start;
 while (*_line != '\r' && *_line != '\0') {
   char *_key;
   char *_value;
   while (*(_start++) != ':');
   *(_start - 1) = '\0';
   _key = _line;
   _value = _start + 1;
   while(_start++, *_start != '\0' && *_start != '\r');
   *_start = '\0';
   _start++;

   _start++;
   _line = _start;

   if (!strcasecmp(_key, "Content-Length")) {
     _httpResponseTemp->bodySize = atoi(_value);
   }
 }

 if (*_line == '\r') {
   _line += 2;
   _httpResponseTemp->body = _line;
 }

 if (_httpResponseTemp->version != NULL) {
   _httpResponse->version = strdup(_httpResponseTemp->version);
 }
 if (_httpResponseTemp->code != NULL) {
   _httpResponse->code = strdup(_httpResponseTemp->code);
 }
 if (_httpResponseTemp->desc != NULL) {
   _httpResponse->desc = strdup(_httpResponseTemp->desc);
 }
 if (_httpResponseTemp->body != NULL) {
   _httpResponse->body = strdup(_httpResponseTemp->body);
 }
 _httpResponse->bodySize = _httpResponseTemp->bodySize;

 free(_httpResponseTemp);
 _httpResponseTemp = NULL;

 return _httpResponse;
}

int wsclient_handshake(bool printMsg) {
 int code = -1;
 char          request_header[TEMP_RECV_BUF];
 char          recv_buf[TEMP_RECV_BUF];
 unsigned char key_nonce[16];
 char          websocket_key[256];
 srand(time(NULL));
 int i;
 for (i = 0; i < 16; i++) {
   key_nonce[i] = rand() & 0xff;
 }
 taos_base64_encode(key_nonce, 16, websocket_key, 256);
 if (args.cloud) {
   snprintf(request_header, TEMP_RECV_BUF,
            "GET /rest/ws?token=%s HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nHost: "
            "%s:%s\r\nSec-WebSocket-Key: "
            "%s\r\nSec-WebSocket-Version: 13\r\n\r\n",
            args.cloudToken, args.cloudHost, args.cloudPort, websocket_key);
 } else {
   snprintf(request_header, TEMP_RECV_BUF,
            "GET /rest/ws HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nHost: %s:%d\r\nSec-WebSocket-Key: "
            "%s\r\nSec-WebSocket-Version: 13\r\n\r\n",
            args.host, args.port, websocket_key);
 }

 int n = send(args.socket, request_header, strlen(request_header), 0);
 if (n <= 0) {
#ifdef WINDOWS
   fprintf(stderr, "send failed with error: %d\n", WSAGetLastError());
#else
   fprintf(stderr, "websocket handshake error\n");
#endif
   return code;
 }
 n = recv(args.socket, recv_buf, TEMP_RECV_BUF - 1, 0);
 if (n <= 0) {
   fprintf(stderr, "websocket handshake socket error\n");
   return code;
 }
 HttpResponse * response = parseHttpResponse(recv_buf);
 if (atoi(response->code) != 101) {
   fprintf(stderr, "websocket handshake failed, reason: %s, code: %s\n", response->desc, response->code);
   int received_body = strlen(response->body);
   while (received_body < response->bodySize) {
     memset(recv_buf, 0, TEMP_RECV_BUF);
     received_body += recv(args.socket, recv_buf, TEMP_RECV_BUF - 1, 0);
   }
   goto OVER;
 }
 code = 0;
 if (printMsg) {
   if (args.cloud) {
     fprintf(stdout, "Successfully connect to %s:%s in restful mode\n\n", args.cloudHost, args.cloudPort);
   } else {
     fprintf(stdout, "Successfully connect to %s:%d in restful mode\n\n", args.host, args.port);
   }
 }
OVER:
 releaseHttpResponse(response);
 return code;
}

int wsclient_reconnect() {
 if (args.restful && tcpConnect(args.host, args.port)) {
   return -1;
 }
 if (args.cloud && tcpConnect(args.cloudHost, atoi(args.cloudPort))) {
   return -1;
 }
 if (wsclient_handshake(0)) {
   close(args.socket);
   return -1;
 }
 wsclient.status = TCP_CONNECTED;
 return 0;
}

int wsclient_send(char *strdata, WebSocketFrameType frame) {
  struct timeval     tv;
  unsigned char      mask[4];
  unsigned int       mask_int;
  unsigned long long payload_len;
  unsigned int       payload_len_small;
  unsigned int       payload_offset = 6;
  unsigned int       len_size;
  // unsigned long long be_payload_len;
  unsigned int sent = 0;
  int          i;
  unsigned int frame_size;
  char        *data;
  gettimeofday(&tv, NULL);
  srand(tv.tv_usec * tv.tv_sec);
  mask_int = rand();
  memcpy(mask, &mask_int, 4);
  payload_len = strlen(strdata);
  if (payload_len <= 125) {
    frame_size = 6 + payload_len;
    payload_len_small = payload_len;
  } else if (payload_len > 125 && payload_len <= 0xffff) {
    frame_size = 8 + payload_len;
    payload_len_small = 126;
    payload_offset += 2;
  } else if (payload_len > 0xffff && payload_len <= 0xffffffffffffffffLL) {
    frame_size = 14 + payload_len;
    payload_len_small = 127;
    payload_offset += 8;
  } else {
    fprintf(stderr, "websocket send too large data\n");
    return -1;
  }
  data = (char *)malloc(frame_size);
  memset(data, 0, frame_size);
  *data = frame;
  *(data + 1) = payload_len_small | 0x80;
  if (payload_len_small == 126) {
    payload_len &= 0xffff;
    len_size = 2;
    for (i = 0; i < len_size; i++) {
      *(data + 2 + i) = *((char *)&payload_len + (len_size - i - 1));
    }
  }
  if (payload_len_small == 127) {
    payload_len &= 0xffffffffffffffffLL;
    len_size = 8;
    for (i = 0; i < len_size; i++) {
      *(data + 2 + i) = *((char *)&payload_len + (len_size - i - 1));
    }
  }
  for (i = 0; i < 4; i++) *(data + (payload_offset - 4) + i) = mask[i];

  memcpy(data + payload_offset, strdata, strlen(strdata));
  for (i = 0; i < strlen(strdata); i++) *(data + payload_offset + i) ^= mask[i % 4] & 0xff;
  sent = 0;
  i = 0;
  while (sent < frame_size && i >= 0) {
    i = send(args.socket, data + sent, frame_size - sent, 0);
    sent += i;
  }
  if (i < 0) {
    wsclient.status = SEND_ERROR;
    free(data);
    return -1;
  }
  free(data);
  return 0;
}

int wsclient_send_sql(char *command, WS_ACTION_TYPE type) {
 int code = 1;
 cJSON *json = cJSON_CreateObject();
 cJSON *_args = cJSON_CreateObject();
 cJSON_AddNumberToObject(_args, "req_id", 1);
 switch (type) {
   case WS_CONN:
     cJSON_AddStringToObject(json, "action", "conn");
     cJSON_AddStringToObject(_args, "user", args.user);
     cJSON_AddStringToObject(_args, "password", args.password);
     cJSON_AddStringToObject(_args, "db", args.database);

     break;
   case WS_QUERY:
     cJSON_AddStringToObject(json, "action", "query");
     cJSON_AddStringToObject(_args, "sql", command);
     break;
   case WS_FETCH:
     cJSON_AddStringToObject(json, "action", "fetch");
     cJSON_AddNumberToObject(_args, "id", args.id);
     break;
   case WS_FETCH_BLOCK:
     cJSON_AddStringToObject(json, "action", "fetch_block");
     cJSON_AddNumberToObject(_args, "id", args.id);
     break;
   case WS_CLOSE:
     cJSON_AddStringToObject(json, "action", "close");
     cJSON_AddNumberToObject(_args, "id", args.id);
     break;
 }
 cJSON_AddItemToObject(json, "args", _args);
 char *strdata = NULL;
 strdata = cJSON_Print(json);
 if (wsclient_send(strdata, TEXT_FRAME)) {
   if (type == WS_CLOSE) {
     return 0;
   }
   goto OVER;
 }
 code = 0;
OVER:
 free(strdata);
 cJSON_Delete(json);
 return code;
}

int wsclient_conn() {
 if (wsclient_send_sql(NULL, WS_CONN)) {
   return -1;
 }
 pthread_create(&rpid, NULL, recvHandler, NULL);
 pthread_join(rpid, NULL);
 if (wsclient.status != TCP_CONNECTED && wsclient.status != WS_CONNECTED) {
   fprintf(stderr, "websocket receive failed, reason: %s\n", wsclient_strerror(wsclient.status));
   return -1;
 }

 cJSON *conn = cJSON_Parse(args.response_buffer);
 if (conn == NULL) {
   fprintf(stderr, "fail to parse response into json: %s\n", args.response_buffer);
   tfree(args.response_buffer);
   return -1;
 }
 tfree(args.response_buffer);
 if (wsclient_check(conn)) {
   cJSON_Delete(conn);
   return -1;
 }
 cJSON_Delete(conn);
 wsclient.status = WS_CONNECTED;
 return 0;
}

void wsclient_parse_frame(SWSParser * parser, uint8_t * recv_buffer) {
  unsigned char msg_opcode = recv_buffer[0] & 0x0F;
  unsigned char msg_masked = (recv_buffer[1] >> 7) & 0x01;
  int payload_length = 0;
  int pos = 2;
  int length_field = recv_buffer[1] &(~0x80);
  unsigned int mask = 0;
  if (length_field <= 125) {
    payload_length = length_field;
  } else if (length_field == 126) {
    payload_length = recv_buffer[2];
    for (int i = 0; i < 1; i++) {
      payload_length = (payload_length << 8) + recv_buffer[3 + i];
    }
    pos += 2;
  } else if (length_field == 127) {
    payload_length = recv_buffer[2];
    for (int i = 0; i < 7; i++) {
      payload_length = (payload_length << 8) + recv_buffer[3 + i];
    }
    pos += 8;
  }
  if (msg_masked) {
    mask = *((unsigned int *) (recv_buffer + pos));
    pos += 4;
    const uint8_t *c = recv_buffer + pos;
    for (int i = 0; i < payload_length; i++) {
      recv_buffer[i] = c[i] ^ ((unsigned char *) (&mask))[i % 4];
    }
  }
  if (msg_opcode == 0x9) {
    parser->frame = PING_FRAME;
  }
  parser->offset = pos;
  parser->payload_length = payload_length;
}

int wsclient_check(cJSON *root) {
 int64_t et = taosGetTimestampUs();
 cJSON *code = cJSON_GetObjectItem(root, "code");
 cJSON *message = cJSON_GetObjectItem(root, "message");
 if (!cJSON_IsNumber(code) || !cJSON_IsString(message)) {
   fprintf(stderr, "Invalid or miss 'code'/'message' in response\n");
   return -1;
 }
 if (code->valueint != 0) {
   fprintf(stderr, "\nDB error: %s, code: %"PRId64" (%.6fs)\n", message->valuestring, code->valueint, (et - args.st) / 1E6);
   return -1;
 }
 return 0;
}

cJSON* wsclient_query(char *command) {
 if (wsclient.status != WS_CONNECTED) {
   if (wsclient.status != TCP_CONNECTED) {
     if (wsclient_reconnect()) {
       return NULL;
     }
   }
   if (wsclient_conn()) {
     return NULL;
   }
 }

 if (wsclient_send_sql(command, WS_QUERY)) {
   return NULL;
 }
 pthread_create(&rpid, NULL, recvHandler, NULL);
 pthread_join(rpid, NULL);
 if (wsclient.status != TCP_CONNECTED && wsclient.status != WS_CONNECTED) {
   fprintf(stderr, "websocket receive failed, reason: %s\n", wsclient_strerror(wsclient.status));
   return NULL;
 }
 return cJSON_Parse(args.response_buffer);
}