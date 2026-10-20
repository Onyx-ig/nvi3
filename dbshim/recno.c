/* In-memory emulation of the db 1.85 recno interface, as used by nvi2. */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "db.h"

struct rec { char *p; size_t n; };

struct priv {
struct rec *r;
  size_t nrecs, cap;
  int bfd;
  int bval;
  recno_t seqkey;
};

static int
rec_insert(struct priv *t, size_t idx, const void *p, size_t n)
{
char *c;

if (t->nrecs == t->cap) {
size_t ncap = t->cap ? t->cap * 2 : 1024;
struct rec *nr = realloc(t->r, ncap * sizeof(*nr));
if (nr == NULL)
return -1;
t->r = nr;
t->cap = ncap;
}
if ((c = malloc(n + 1)) == NULL)
return -1;
if (n)
memcpy(c, p, n);
c[n] = '\0';
memmove(&t->r[idx + 1], &t->r[idx], (t->nrecs - idx) * sizeof(*t->r));
t->r[idx].p = c;
t->r[idx].n = n;
t->nrecs++;
return 0;
}

static int
extend(struct priv *t, size_t upto)
{
while (t->nrecs < upto)
if (rec_insert(t, t->nrecs, "", 0))
return -1;
return 0;
}

static int
getkey(const DBT *k, recno_t *np)
{
if (k->size != sizeof(recno_t)) {
errno = EINVAL;
return -1;
}
memcpy(np, k->data, sizeof(recno_t));
return 0;
}

static int
load(struct priv *t, int fd)
{
size_t cap = 65536, len = 0, n;
char *buf, *tmp, *s, *e, *nl;
ssize_t r;

if ((buf = malloc(cap)) == NULL)
return -1;
for (;;) {
if (len == cap) {
if ((tmp = realloc(buf, cap * 2)) == NULL) {
free(buf);
return -1;
}
buf = tmp;
cap *= 2;
}
r = read(fd, buf + len, cap - len);
if (r < 0) {
if (errno == EINTR)
continue;
free(buf);
return -1;
}
if (r == 0)
break;
len += r;
}
for (s = buf, e = buf + len; s < e;) {
nl = memchr(s, t->bval, e - s);
n = nl ? (size_t)(nl - s) : (size_t)(e - s);
if (rec_insert(t, t->nrecs, s, n)) {
free(buf);
return -1;
}
s += n + (nl ? 1 : 0);
}
free(buf);
return 0;
}

static int
rec_close(DB *dbp)
{
struct priv *t = dbp->internal;
size_t i;

for (i = 0; i < t->nrecs; i++)
free(t->r[i].p);
free(t->r);
if (t->bfd >= 0)
close(t->bfd);
free(t);
free(dbp);
return 0;
}

static int
rec_get(const DB *dbp, const DBT *key, DBT *data, u_int flags)
{
struct priv *t = dbp->internal;
recno_t n;

if (flags != 0) {
errno = EINVAL;
return RET_ERROR;
}
if (getkey(key, &n))
return RET_ERROR;
if (n == 0) {
errno = EINVAL;
return RET_ERROR;
}
if (n > t->nrecs)
return RET_SPECIAL;
data->data = t->r[n - 1].p;
data->size = t->r[n - 1].n;
return RET_SUCCESS;
}

static int
rec_put(const DB *dbp, DBT *key, const DBT *data, u_int flags)
{
struct priv *t = dbp->internal;
recno_t n, newn;
char *c;

if (getkey(key, &n))
return RET_ERROR;
switch (flags) {
case 0:
if (n == 0) {
errno = EINVAL;
return RET_ERROR;
}
if (n <= t->nrecs) { /* replace */
if ((c = malloc(data->size + 1)) == NULL)
return RET_ERROR;
if (data->size)
memcpy(c, data->data, data->size);
c[data->size] = '\0';
free(t->r[n - 1].p);
t->r[n - 1].p = c;
t->r[n - 1].n = data->size;
return RET_SUCCESS;
}
if (extend(t, n - 1) ||
    rec_insert(t, n - 1, data->data, data->size))
return RET_ERROR;
return RET_SUCCESS;
case R_IAFTER:/* n may be 0 */
if (extend(t, n) || rec_insert(t, n, data->data, data->size))
return RET_ERROR;
newn = n + 1;
break;
case R_IBEFORE:
if (n == 0) {
errno = EINVAL;
return RET_ERROR;
}
if (extend(t, n - 1) ||
    rec_insert(t, n - 1, data->data, data->size))
return RET_ERROR;
newn = n;
break;
default:
errno = EINVAL;
return RET_ERROR;
}
memcpy(key->data, &newn, sizeof(newn));
return RET_SUCCESS;
}

static int
rec_del(const DB *dbp, const DBT *key, u_int flags)
{
struct priv *t = dbp->internal;
recno_t n;

if (flags != 0 || getkey(key, &n) || n == 0) {
errno = EINVAL;
return RET_ERROR;
}
if (n > t->nrecs)
return RET_SPECIAL;
free(t->r[n - 1].p);
memmove(&t->r[n - 1], &t->r[n], (t->nrecs - n) * sizeof(*t->r));
t->nrecs--;
return RET_SUCCESS;
}

static int
rec_seq(const DB *dbp, DBT *key, DBT *data, u_int flags)
{
struct priv *t = dbp->internal;

if (flags != R_LAST && flags != R_FIRST) {
errno = EINVAL;
return RET_ERROR;
}
if (t->nrecs == 0)
return RET_SPECIAL;
t->seqkey = (flags == R_LAST) ? t->nrecs : 1;
key->data = &t->seqkey;
key->size = sizeof(recno_t);
data->data = t->r[t->seqkey - 1].p;
data->size = t->r[t->seqkey - 1].n;
return RET_SUCCESS;
}

/* Write all lines, plain text, to the backing file. */
static int
rec_sync(const DB *dbp, u_int flags)
{
struct priv *t = dbp->internal;
size_t i, total = 0, off = 0;
char *buf, *p;
ssize_t w;

if (flags != R_RECNOSYNC) {
errno = EINVAL;
return RET_ERROR;
}
if (t->bfd < 0)
return RET_SUCCESS;
for (i = 0; i < t->nrecs; i++)
total += t->r[i].n + 1;
if ((buf = malloc(total ? total : 1)) == NULL)
return RET_ERROR;
for (p = buf, i = 0; i < t->nrecs; i++) {
memcpy(p, t->r[i].p, t->r[i].n);
p += t->r[i].n;
*p++ = t->bval;
}
while (off < total) {
w = pwrite(t->bfd, buf + off, total - off, off);
if (w < 0) {
if (errno == EINTR)
continue;
free(buf);
return RET_ERROR;
}
off += w;
}
free(buf);
if (ftruncate(t->bfd, total))
return RET_ERROR;
return RET_SUCCESS;
}

static int
rec_fd(const DB *dbp)
{
struct priv *t = dbp->internal;

if (t->bfd < 0)
errno = ENOENT;
return t->bfd;
}

DB *
dbopen(const char *fname, int flags, int mode, DBTYPE type,
    const void *openinfo)
{
const RECNOINFO *ri = openinfo;
struct priv *t;
DB *dbp;
int fd, serrno;

(void)flags;
(void)mode;
if (type != DB_RECNO) {
errno = EINVAL;
return NULL;
}
if ((dbp = calloc(1, sizeof(*dbp))) == NULL)
return NULL;
if ((t = calloc(1, sizeof(*t))) == NULL) {
free(dbp);
return NULL;
}
t->bfd = -1;
t->bval = (ri != NULL && ri->bval) ? ri->bval : '\n';
dbp->type = DB_RECNO;
dbp->internal = t;
dbp->close = rec_close;
dbp->del = rec_del;
dbp->get = rec_get;
dbp->put = rec_put;
dbp->seq = rec_seq;
dbp->sync = rec_sync;
dbp->fd = rec_fd;

if (ri != NULL && ri->bfname != NULL) {
t->bfd = open(ri->bfname, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
if (t->bfd < 0)
goto err;
}
if (fname != NULL) {
if ((fd = open(fname, O_RDONLY | O_CLOEXEC)) < 0)
goto err;
if (load(t, fd)) {
serrno = errno;
close(fd);
errno = serrno;
goto err;
}
close(fd);
} else if (t->bfd >= 0 && load(t, t->bfd))/* recovery */
goto err;
return dbp;

err:serrno = errno;
rec_close(dbp);
errno = serrno;
return NULL;
}
