/* Minimal db 1.85 recno API, implemented by dbshim/recno.c */
#ifndef _DBSHIM_DB_H_
#define _DBSHIM_DB_H_

#include <sys/types.h>
#include <stdint.h>

typedef uint32_t recno_t;
#define MAX_REC_NUMBER 0xffffffff

typedef struct {
  void *data;
  size_t size;
} DBT;

#define R_CURSOR 1
#define R_FIRST 3
#define R_IAFTER 4
#define R_IBEFORE 5
#define R_LAST 6
#define R_NEXT 7
#define R_NOOVERWRITE 8
#define R_PREV 9
#define R_SETCURSOR 10
#define R_RECNOSYNC 11

#define RET_ERROR -1
#define RET_SUCCESS 0
#define RET_SPECIAL 1

typedef enum { DB_BTREE, DB_HASH, DB_RECNO } DBTYPE;

typedef struct __db {
  DBTYPE type;
  int (*close)(struct __db *);
  int (*del)(const struct __db *, const DBT *, u_int);
  int (*get)(const struct __db *, const DBT *, DBT *, u_int);
  int (*put)(const struct __db *, DBT *, const DBT *, u_int);
  int (*seq)(const struct __db *, DBT *, DBT *, u_int);
  int (*sync)(const struct __db *, u_int);
  void *internal;
  int (*fd)(const struct __db *);
} DB;

#define R_FIXEDLEN 0x01
#define R_NOKEY 0x02
#define R_SNAPSHOT 0x04

typedef struct {
  u_long flags;
  u_int cachesize;
  u_int psize;
  int lorder;
  size_t reclen;
  u_char bval;
  char *bfname;
} RECNOINFO;

DB *dbopen(const char *, int, int, DBTYPE, const void *);

#endif
