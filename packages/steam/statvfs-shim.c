/* steam-statvfs-shim: report free space on read-only filesystems that
 * report zero avail, but ONLY when STEAM_IMG_ROOT is present in environ.
 * Freestanding (-nostdlib): raw syscalls only, no libc calls, so the same
 * .so loads under glibc AND musl, 32-bit AND 64-bit. Intercepts
 * statvfs/statvfs64 (what Valve's bootstrapper uses to gate updates and
 * game installs) and fills the caller's struct directly, so no dlsym
 * chaining is needed. Passthrough is bit-identical to libc. */
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

#ifdef __x86_64__
#define SYS_statfs 137
#define SYS_fstatfs 100
#define SYS_openat 257
#define SYS_read 0
#define SYS_write 1
#define SYS_close 3
#define AT_FDCWD -100
static long raw_statfs(const char *p, void *b) {
	long r;
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_statfs), "D" ((long)p), "S" ((long)b)
		: "rcx", "r11", "memory");
	return r;
}
static long raw_fstatfs(long fd, void *b) {
	long r;
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_fstatfs), "D" (fd), "S" ((long)b)
		: "rcx", "r11", "memory");
	return r;
}
static long raw_open(const char *p) {
	long r;
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_openat), "D" ((long)AT_FDCWD), "S" ((long)p), "d" ((long)0)
		: "rcx", "r11", "memory");
	return r;
}
static long raw_rw(long n, long fd, const char *b, unsigned long c) {
	long r;
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" (n), "D" (fd), "S" ((long)b), "d" ((long)c)
		: "rcx", "r11", "memory");
	return r;
}
static long raw_close(long fd) {
	long r;
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_close), "D" (fd)
		: "rcx", "r11", "memory");
	return r;
}
#else
#define SYS_statfs64 268
#define SYS_fstatfs64 269
#define SYS_open 5
#define SYS_read 3
#define SYS_write 4
#define SYS_close 6
static long raw_statfs(const char *p, void *b) {
	long r;
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_statfs64), "b" ((long)p), "c" ((long)b)
		: "memory");
	return r;
}
static long raw_fstatfs(long fd, void *b) {
	long r;
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_fstatfs64), "b" (fd), "c" ((long)b)
		: "memory");
	return r;
}
static long raw_open(const char *p) {
	long r;
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_open), "b" ((long)p), "c" ((long)0)
		: "memory");
	return r;
}
static long raw_rw(long n, long fd, const char *b, unsigned long c) {
	long r;
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" (n), "b" (fd), "c" ((long)b), "d" ((long)c)
		: "memory");
	return r;
}
static long raw_close(long fd) {
	long r;
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_close), "b" (fd)
		: "memory");
	return r;
}
#endif

static int streq_n(const char *a, const char *b, unsigned long n) {
	unsigned long i;
	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}

/* 1 if STEAM_IMG_ROOT= appears in /proc/self/environ, else 0. */
static int want_fake(void) {
	static int cached = -1;
	static const char key[] = "STEAM_IMG_ROOT=";
	char buf[4096 + 15];
	long fd, r, i;
	unsigned long u, off = 0;
	if (cached != -1)
		return cached;
	cached = 0;
	fd = raw_open("/proc/self/environ");
	if (fd < 0)
		return 0;
	for (;;) {
		r = raw_rw(
#ifdef __x86_64__
			SYS_read,
#else
			SYS_read,
#endif
			fd, buf + off, sizeof(buf) - off);
		if (r <= 0)
			break;
		u = off + (unsigned long)r;
		for (i = 0; i + 15 < u; i++) {
			if (streq_n(buf + i, key, 15)) {
				cached = 1;
				break;
			}
		}
		if (cached)
			break;
		/* Carry up to 15 bytes so keys split across reads still match. */
		off = u > 15 ? 15 : u;
		{
			unsigned long j;
			for (j = 0; j < off; j++)
				buf[j] = buf[u - off + j];
		}
	}
	raw_close(fd);
	return cached;
}

/* 1 if STEAM_SHIM_DEBUG= appears in environ. Checked lazily, cached. */
static int debug_on(void) {
	static int cached = -1;
	static const char key[] = "STEAM_SHIM_DEBUG=";
	char buf[1024 + 17];
	long fd, r, i;
	unsigned long u, off = 0;
	if (cached != -1)
		return cached;
	cached = 0;
	fd = raw_open("/proc/self/environ");
	if (fd < 0)
		return 0;
	for (;;) {
		r = raw_rw(
#ifdef __x86_64__
			SYS_read,
#else
			SYS_read,
#endif
			fd, buf + off, sizeof(buf) - off);
		if (r <= 0)
			break;
		u = off + (unsigned long)r;
		for (i = 0; i + 18 < u; i++) {
			if (streq_n(buf + i, key, 17)) {
				cached = 1;
				break;
			}
		}
		if (cached || u < sizeof(buf))
			break;
		off = 17;
		{
			unsigned long j;
			for (j = 0; j < off; j++)
				buf[j] = buf[u - off + j];
		}
	}
	raw_close(fd);
	return cached;
}

static void dbg_fake(void) {
	static const char m[] = "steam-shim: faked statvfs space\n";
	unsigned long n = 0;
	while (m[n])
		n++;
	if (debug_on())
		raw_rw(
#ifdef __x86_64__
			SYS_write,
#else
			SYS_write,
#endif
			2, m, n);
}

/* Verbose: why a call was NOT faked (0 = gate values, 1 = env). */
static void dbg_skip(int which) {
	static const char m0[] = "steam-shim: skip (fs writable or has space)\n";
	static const char m1[] = "steam-shim: skip (no STEAM_IMG_ROOT in environ)\n";
	const char *m = which ? m1 : m0;
	unsigned long n = 0;
	if (!debug_on())
		return;
	while (m[n])
		n++;
	raw_rw(
#ifdef __x86_64__
		SYS_write,
#else
		SYS_write,
#endif
		2, m, n);
}

#define SYS_readlinkat_x64 267
#define SYS_readlink_x86 85
static long raw_readlink_cwd(char *b, unsigned long c) {
	static const char p[] = "/proc/self/cwd";
	long r;
#ifdef __x86_64__
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_readlinkat_x64), "D" ((long)AT_FDCWD), "S" ((long)p), "d" ((long)b), "r" ((long)c)
		: "rcx", "r11", "memory");
#else
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_readlink_x86), "b" ((long)p), "c" ((long)b), "d" ((long)c)
		: "memory");
#endif
	return r;
}

/* Verbose: log every intercepted path (capped) plus caller CWD. */
static void dbg_call(const char *p) {
	static const char pre[] = "steam-shim: statvfs ";
	static const char mid[] = " cwd=";
	char cwd[256];
	unsigned long n = 0;
	long r;
	if (!debug_on())
		return;
	raw_rw(
#ifdef __x86_64__
		SYS_write,
#else
		SYS_write,
#endif
		2, pre, sizeof(pre) - 1);
	while (n < 200 && p[n])
		n++;
	if (n)
		raw_rw(
#ifdef __x86_64__
			SYS_write,
#else
			SYS_write,
#endif
			2, p, n);
	raw_rw(
#ifdef __x86_64__
		SYS_write,
#else
		SYS_write,
#endif
		2, mid, sizeof(mid) - 1);
	r = raw_readlink_cwd(cwd, sizeof(cwd) - 1);
	if (r > 0)
		raw_rw(
#ifdef __x86_64__
			SYS_write,
#else
			SYS_write,
#endif
			2, cwd, (unsigned long)r);
	else
		raw_rw(
#ifdef __x86_64__
			SYS_write,
#else
			SYS_write,
#endif
			2, "(cwd-unreadable)", 16);
	raw_rw(
#ifdef __x86_64__
		SYS_write,
#else
		SYS_write,
#endif
		2, "\n", 1);
}

/* ---- execve router: run ELF binaries whose INTERP is missing via the
 * bundled loaders, WITHOUT touching files on disk (keeps Valve's
 * checksum verification happy and covers update-delivered binaries).
 * Only reroutes when the kernel could not run the file itself. ---- */
extern char **environ;

static unsigned long r_strlen(const char *s) {
	unsigned long n = 0;
	while (s[n])
		n++;
	return n;
}
static int r_strneq(const char *a, const char *b, unsigned long n) {
	unsigned long i;
	for (i = 0; i < n; i++)
		if (a[i] != b[i])
			return 0;
	return 1;
}
/* getenv from the live process environ (no /proc needed). */
static const char *r_getenv(const char *name) {
	char **e;
	unsigned long nl;
	if (!environ)
		return 0;
	nl = r_strlen(name);
	for (e = environ; *e; e++) {
		unsigned long i;
		for (i = 0; i < nl; i++)
			if ((*e)[i] != name[i])
				break;
		if (i == nl && (*e)[nl] == '=')
			return *e + nl + 1;
	}
	return 0;
}
#ifdef __x86_64__
#define SYS_execve 59
#else
#define SYS_execve 11
#endif
static long raw_execve(const char *p, char *const *a, char *const *e) {
	long r;
#ifdef __x86_64__
	__asm__ volatile ("syscall"
		: "=a" (r)
		: "a" ((long)SYS_execve), "D" ((long)p), "S" ((long)a), "d" ((long)e)
		: "rcx", "r11", "memory");
#else
	__asm__ volatile ("int $0x80"
		: "=a" (r)
		: "a" ((long)SYS_execve), "b" ((long)p), "c" ((long)a), "d" ((long)e)
		: "memory");
#endif
	return r;
}
/* Verbose: log a rerouted exec (capped path). */
static void dbg_route(const char *p, const char *ld) {
	static const char pre[] = "steam-shim: routed exec ";
	char buf[200];
	unsigned long n = 0, m = 0;
	if (!debug_on())
		return;
	while (pre[n]) {
		buf[n] = pre[n];
		n++;
	}
	m = 0;
	while (n < 110 && p[m]) {
		buf[n] = p[m];
		n++;
		m++;
	}
	buf[n++] = ' ';
	buf[n++] = 'v';
	buf[n++] = 'i';
	buf[n++] = 'a';
	buf[n++] = ' ';
	m = 0;
	while (n < (unsigned long)(sizeof(buf) - 2) && ld[m])
		buf[n++] = ld[m++];
	buf[n++] = '\n';
	raw_rw(
#ifdef __x86_64__
		SYS_write,
#else
		SYS_write,
#endif
		2, buf, n);
}
/* Read exactly c bytes (short reads looped). Returns 0 on success. */
static int r_read_all(long fd, char *b, unsigned long c) {
	while (c) {
		long r = raw_rw(
#ifdef __x86_64__
			SYS_read,
#else
			SYS_read,
#endif
			fd, b, c);
		if (r <= 0)
			return -1;
		b += (unsigned long)r;
		c -= (unsigned long)r;
	}
	return 0;
}
/* Discard c bytes (chunked: never overflows the scratch buffer). */
static int r_discard(long fd, unsigned long c) {
	static char trash[1024];
	while (c) {
		unsigned long step = c > sizeof(trash) ? sizeof(trash) : c;
		long r = raw_rw(
#ifdef __x86_64__
			SYS_read,
#else
			SYS_read,
#endif
			fd, trash, step);
		if (r <= 0)
			return -1;
		c -= (unsigned long)r;
	}
	return 0;
}
/* If path is an ELF whose INTERP cannot be opened, return 1 and set
 * *is64; else return 0. Never fails the caller: errors mean passthrough. */
static int needs_router(const char *path, int *is64) {
	/* Stack-local: exec can come from any thread. */
	char eb[64];
	char ph[32 * 56];
	char ib[512];
	unsigned long phoff, phesz, phn, i, pos, ppsz;
	long fd, ird;
	int cls;
	fd = raw_open(path);
	if (fd < 0)
		return 0;
	if (r_read_all(fd, eb, 52) != 0) {
		raw_close(fd);
		return 0;
	}
	if (eb[0] != 0x7f || eb[1] != 'E' || eb[2] != 'L' || eb[3] != 'F') {
		raw_close(fd);
		return 0; /* script or data: kernel handles it */
	}
	cls = (unsigned char)eb[4];
	*is64 = (cls == 2);
	if (cls == 2) {
		if (r_read_all(fd, eb + 52, 12) != 0) {
			raw_close(fd);
			return 0;
		}
		phoff = *(u64 *)(eb + 32);
		phesz = *(unsigned short *)(eb + 54);
		phn = *(unsigned short *)(eb + 56);
		ppsz = 56;
	} else if (cls == 1) {
		phoff = *(u32 *)(eb + 28);
		phesz = *(unsigned short *)(eb + 42);
		phn = *(unsigned short *)(eb + 44);
		ppsz = 32;
	} else {
		raw_close(fd);
		return 0;
	}
	if (phesz != ppsz || phn > 32 || phoff < 52 || phoff > 52 + 4096) {
		raw_close(fd);
		return 0;
	}
	pos = (cls == 2) ? 64 : 52;
	if (phoff < pos) {
		raw_close(fd);
		return 0;
	}
	if (r_discard(fd, phoff - pos) != 0) {
		raw_close(fd);
		return 0;
	}
	pos = phoff;
	if (r_read_all(fd, ph, phn * phesz) != 0) {
		raw_close(fd);
		return 0;
	}
	pos += phn * phesz;
	for (i = 0; i < phn; i++) {
		char *e = ph + i * phesz;
		u32 t;
		unsigned long off, fsz;
		if (cls == 2) {
			t = *(u32 *)e;
			off = *(u64 *)(e + 8);
			fsz = *(u64 *)(e + 32);
		} else {
			t = *(u32 *)e;
			off = *(u32 *)(e + 4);
			fsz = *(u32 *)(e + 16);
		}
		if (t != 3) /* PT_INTERP */
			continue;
		if (fsz < 2 || fsz > sizeof(ib) - 1 || off < pos ||
		    off > pos + 64 * 1024) {
			raw_close(fd);
			return 0;
		}
		if (r_discard(fd, off - pos) != 0) {
			raw_close(fd);
			return 0;
		}
		if (r_read_all(fd, ib, fsz) != 0) {
			raw_close(fd);
			return 0;
		}
		raw_close(fd);
		ib[fsz] = 0;
		ird = raw_open(ib);
		if (ird >= 0) {
			raw_close(ird);
			return 0; /* loader present: kernel can run it */
		}
		return 1; /* INTERP missing: reroute */
	}
	raw_close(fd);
	return 0; /* static binary or no INTERP: kernel handles it */
}
/* Reroute path (missing loader) via bundled ld. Returns only on failure. */
static long route_exec(const char *path, char *const *argv, char *const *envp,
		       int is64) {
	/* Stack-local: exec can come from any thread. */
	char *nargv[128];
	char *nenvp[300];
	char ldlp[2048];
	char lpref[2064];
	char ldcand[1024];
	const char *ld, *libs, *oldlp;
	unsigned long i, n = 0, m = 0, l;
	if (!argv || !envp)
		return raw_execve(path, argv, (char *const *)envp);
	ld = r_getenv(is64 ? "STEAM_IMG_LD64" : "STEAM_IMG_LD32");
	libs = r_getenv("STEAM_IMG_LIBS");
	if (!ld || !*ld || !libs || !*libs)
		return raw_execve(path, argv, (char *const *)envp);
	nargv[n++] = (char *)ld;
	nargv[n++] = (char *)"--library-path";
	nargv[n++] = (char *)libs;
	nargv[n++] = (char *)path;
	for (i = 1; argv[i] && n < 127; i++)
		nargv[n++] = argv[i];
	if (argv[i])
		return raw_execve(path, argv, (char *const *)envp);
	nargv[n] = 0;
	oldlp = r_getenv("LD_LIBRARY_PATH");
	l = r_strlen(libs);
	if (l > sizeof(ldlp) - 32)
		return raw_execve(path, argv, (char *const *)envp);
	for (i = 0; i < l; i++)
		ldlp[m++] = libs[i];
	if (oldlp && *oldlp) {
		ldlp[m++] = ':';
		for (i = 0; oldlp[i] && m < sizeof(ldlp) - 1; i++)
			ldlp[m++] = oldlp[i];
	}
	ldlp[m] = 0;
	n = 0;
	for (i = 0; envp[i]; i++) {
		if (r_strneq(envp[i], "LD_LIBRARY_PATH=", 16))
			continue;
		if (n >= 297)
			return raw_execve(path, argv, (char *const *)envp);
		nenvp[n++] = envp[i];
	}
	/* nenvp tail: fresh LD_LIBRARY_PATH entry (replaces any inherited). */
	{
		unsigned long k = 0;
		const char *pre = "LD_LIBRARY_PATH=";
		while (*pre)
			lpref[k++] = *pre++;
		for (i = 0; i <= m; i++)
			lpref[k++] = ldlp[i];
		nenvp[n++] = lpref;
		nenvp[n] = 0;
	}
	/* ld may be colon-separated (tree copy first, image fallback):
	 * try each candidate until one executes. */
	for (;;) {
		unsigned long dl = 0;
		while (ld[dl] && ld[dl] != ':')
			dl++;
		if (dl == 0 || dl >= sizeof(ldcand)) {
			if (!ld[dl])
				break;
			ld += dl + 1;
			continue;
		}
		for (i = 0; i < dl; i++)
			ldcand[i] = ld[i];
		ldcand[dl] = 0;
		nargv[0] = ldcand;
		dbg_route(path, ldcand);
		raw_execve(ldcand, nargv, nenvp);
		if (!ld[dl])
			break;
		ld += dl + 1;
	}
	return raw_execve(path, argv, (char *const *)envp);
}
int execve(const char *path, char *const argv[], char *const envp[]) {
	int is64 = 0;
	if (!envp)
		envp = (char *const *)environ;
	if (!path || !needs_router(path, &is64))
		return raw_execve(path, argv, (char *const *)envp);
	return route_exec(path, argv, envp, is64);
}
/* exec family -> our execve (covers fork+exec users; posix_spawn is rare
 * in this stack and falls through to the kernel error if unresolved). */
int execv(const char *p, char *const a[]) {
	return execve(p, a, (char *const *)environ);
}
int execvpe(const char *f, char *const a[], char *const e[]) {
	unsigned long i;
	if (!f)
		return raw_execve(f, a, (char *const *)e);
	for (i = 0; f[i]; i++)
		if (f[i] == '/')
			return execve(f, a, e);
	/* PATH search (bare filename). */
	{
		const char *pe = 0;
		unsigned long k;
		static char cand[1024];
		if (e) {
			for (k = 0; e[k]; k++) {
				if (r_strneq(e[k], "PATH=", 5)) {
					pe = e[k] + 5;
					break;
				}
			}
		}
		if (!pe)
			pe = r_getenv("PATH");
		if (!pe)
			pe = "/usr/bin:/bin";
		for (;;) {
			unsigned long dl = 0, fl = 0, c = 0;
			while (pe[dl] && pe[dl] != ':')
				dl++;
			while (f[fl])
				fl++;
			if (dl + 1 + fl >= sizeof(cand))
				return raw_execve(f, a, (char *const *)e);
			for (k = 0; k < dl; k++)
				cand[c++] = pe[k];
			cand[c++] = '/';
			for (k = 0; k <= fl; k++)
				cand[c++] = f[k];
			/* try it: needs_router succeeds only on accessible ELF */
			{
				int dummy = 0;
				long fd = raw_open(cand);
				if (fd >= 0) {
					raw_close(fd);
					return execve(cand, a, e);
				}
				(void)dummy;
			}
			if (!pe[dl])
				break;
			pe += dl + 1;
		}
	}
	return raw_execve(f, a, (char *const *)e);
}
int execvp(const char *f, char *const a[]) {
	return execvpe(f, a, (char *const *)environ);
}
/* execl family via stdarg (compiler-provided, freestanding-safe). */
#include <stdarg.h>
int execl(const char *p, const char *a0, ...) {
	char *av[64];
	unsigned long n = 0;
	va_list ap;
	va_start(ap, a0);
	av[n++] = (char *)a0;
	while (n < 63) {
		char *a = va_arg(ap, char *);
		av[n++] = a;
		if (!a)
			break;
	}
	va_end(ap);
	av[63] = 0;
	return execve(p, av, (char *const *)environ);
}
int execle(const char *p, const char *a0, ...) {
	char *av[64];
	char *const *e = 0;
	unsigned long n = 0;
	va_list ap;
	va_start(ap, a0);
	av[n++] = (char *)a0;
	while (n < 63) {
		char *a = va_arg(ap, char *);
		av[n++] = a;
		if (!a)
			break;
	}
	e = va_arg(ap, char *const *);
	va_end(ap);
	av[63] = 0;
	return execve(p, av, (char *const *)e);
}
int execlp(const char *f, const char *a0, ...) {
	char *av[64];
	unsigned long n = 0;
	va_list ap;
	va_start(ap, a0);
	av[n++] = (char *)a0;
	while (n < 63) {
		char *a = va_arg(ap, char *);
		av[n++] = a;
		if (!a)
			break;
	}
	va_end(ap);
	av[63] = 0;
	return execvpe(f, av, (char *const *)environ);
}

#ifdef __x86_64__
/* x86_64 libc struct statvfs == statvfs64 layout. */
struct vfs {
	u64 bsize, frsize, blocks, bfree, bavail, files, ffree, favail;
	u32 fsid;
	u32 __pad;
	u64 flag, namemax;
	u32 spare[6];
};
/* Fill caller's struct from raw kernel statfs buffer in kb. */
static void fill_vfs(struct vfs *o, const u64 *kb) {
	/* kernel statfs x86_64: type,bsize,blocks,bfree,bavail,files,ffree,
	 * fsid[2],namelen,frsize,flags,spare[4] */
	o->bsize = kb[1];
	o->frsize = kb[9];
	o->blocks = kb[2];
	o->bfree = kb[3];
	o->bavail = kb[4];
	o->files = kb[5];
	o->ffree = kb[6];
	o->favail = kb[6];
	o->fsid = (u32)kb[7];
	o->__pad = 0;
	o->flag = kb[10];
	o->namemax = kb[8];
	o->spare[0] = 0; o->spare[1] = 0; o->spare[2] = 0;
	o->spare[3] = 0; o->spare[4] = 0; o->spare[5] = 0;
}
static void maybe_fake(struct vfs *o) {
	u64 want;
	if ((o->flag & 1u) != 1u || o->bavail != 0) {
		dbg_skip(0);
		return;
	}
	if (!want_fake()) {
		dbg_skip(1);
		return;
	}
	want = (8ull * 1024 * 1024 * 1024) / (o->frsize ? o->frsize : 4096u);
	if (o->blocks < want)
		o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	dbg_fake();
}
/* Last resort: kernel refused statfs AND fstatfs (broken compat), but the
 * path exists (open worked) and we run inside the image: report 8GB. */
static void fill_synth(struct vfs *o) {
	u64 want = (8ull * 1024 * 1024 * 1024) / 4096u;
	o->bsize = 4096;
	o->frsize = 4096;
	o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	o->files = 1000000;
	o->ffree = 1000000;
	o->favail = 1000000;
	o->fsid = 0;
	o->__pad = 0;
	o->flag = 0;
	o->namemax = 255;
	o->spare[0] = 0; o->spare[1] = 0; o->spare[2] = 0;
	o->spare[3] = 0; o->spare[4] = 0; o->spare[5] = 0;
}
int statvfs(const char *p, struct vfs *o) {
	struct kbuf {
		u64 w[15];
	} kb;
	long r, fd;
	dbg_call(p);
	r = raw_statfs(p, &kb);
	if (r == 0) {
		fill_vfs(o, kb.w);
		maybe_fake(o);
		return 0;
	}
	/* statfs failed: missing path (open fails) -> -1 as before; kernel
	 * quirk (open ok, statfs broken) -> real data via fd or synthesize. */
	fd = raw_open(p);
	if (fd < 0)
		return -1;
	r = raw_fstatfs(fd, &kb);
	raw_close(fd);
	if (r == 0) {
		fill_vfs(o, kb.w);
		maybe_fake(o);
		return 0;
	}
	if (!want_fake())
		return -1;
	fill_synth(o);
	dbg_fake();
	return 0;
}
int statvfs64(const char *p, struct vfs *o) {
	return statvfs(p, o);
}
#else
/* i386 libc struct statvfs64 layout. */
struct vfs64 {
	u32 bsize, frsize;
	u64 blocks, bfree, bavail, files, ffree, favail;
	u32 fsid, flag, namemax, spare[6];
};
static void fill_vfs64(struct vfs64 *o, const char *kb) {
	u32 bsize_, namelen_, frsize_, flags_, fsid0_;
	u64 blocks_, bfree_, bavail_, files_, ffree_;
	bsize_ = *(const u32 *)(kb + 4);
	blocks_ = *(const u64 *)(kb + 8);
	bfree_ = *(const u64 *)(kb + 16);
	bavail_ = *(const u64 *)(kb + 24);
	files_ = *(const u64 *)(kb + 32);
	ffree_ = *(const u64 *)(kb + 40);
	fsid0_ = *(const u32 *)(kb + 48);
	namelen_ = *(const u32 *)(kb + 56);
	frsize_ = *(const u32 *)(kb + 60);
	flags_ = *(const u32 *)(kb + 64);
	o->bsize = bsize_;
	o->frsize = frsize_;
	o->blocks = blocks_;
	o->bfree = bfree_;
	o->bavail = bavail_;
	o->files = files_;
	o->ffree = ffree_;
	o->favail = ffree_;
	o->fsid = fsid0_;
	o->flag = flags_;
	o->namemax = namelen_;
	o->spare[0] = 0; o->spare[1] = 0; o->spare[2] = 0;
	o->spare[3] = 0; o->spare[4] = 0; o->spare[5] = 0;
}
static void maybe_fake64(struct vfs64 *o) {
	/* 8GB in fs-blocks, without 64-bit division (no libgcc freestanding). */
	u64 want;
	u32 fs = o->frsize ? o->frsize : 4096u;
	if ((o->flag & 1u) != 1u || o->bavail != 0) {
		dbg_skip(0);
		return;
	}
	if (!want_fake()) {
		dbg_skip(1);
		return;
	}
	if (fs == 1024)
		want = 8388608ull;
	else if (fs == 2048)
		want = 4194304ull;
	else if (fs == 8192)
		want = 1048576ull;
	else if (fs == 512)
		want = 16777216ull;
	else
		want = 2097152ull; /* 4096 and oddballs */
	if (o->blocks < want)
		o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	dbg_fake();
}
/* Last resort: kernel refused statfs64 AND fstatfs64 (broken compat), but
 * the path exists (open worked) and we run inside the image: report 8GB. */
static void fill_synth64(struct vfs64 *o) {
	u64 want = (8ull * 1024 * 1024 * 1024) / 4096u;
	o->bsize = 4096;
	o->frsize = 4096;
	o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	o->files = 1000000;
	o->ffree = 1000000;
	o->favail = 1000000;
	o->fsid = 0;
	o->flag = 0;
	o->namemax = 255;
	o->spare[0] = 0; o->spare[1] = 0; o->spare[2] = 0;
	o->spare[3] = 0; o->spare[4] = 0; o->spare[5] = 0;
}
/* i386 libc struct statvfs (32-bit fields, may truncate): convert. */
struct vfs32 {
	u32 bsize, frsize, blocks, bfree, bavail, files, ffree, favail;
	u32 fsid, flag, namemax, spare[6];
};
static void fill_vfs32(struct vfs32 *o, const struct vfs64 *t) {
	o->bsize = t->bsize; o->frsize = t->frsize;
	o->blocks = (u32)t->blocks; o->bfree = (u32)t->bfree;
	o->bavail = (u32)t->bavail; o->files = (u32)t->files;
	o->ffree = (u32)t->ffree; o->favail = (u32)t->favail;
	o->fsid = t->fsid; o->flag = t->flag; o->namemax = t->namemax;
	o->spare[0] = 0; o->spare[1] = 0; o->spare[2] = 0;
	o->spare[3] = 0; o->spare[4] = 0; o->spare[5] = 0;
}
int statvfs64(const char *p, struct vfs64 *o) {
	char kb[84];
	long r, fd;
	dbg_call(p);
	r = raw_statfs(p, kb);
	if (r == 0) {
		fill_vfs64(o, kb);
		maybe_fake64(o);
		return 0;
	}
	/* statfs64 failed: missing path (open fails) -> -1 as before; kernel
	 * quirk (open ok, statfs broken) -> real data via fd or synthesize. */
	fd = raw_open(p);
	if (fd < 0)
		return -1;
	r = raw_fstatfs(fd, kb);
	raw_close(fd);
	if (r == 0) {
		fill_vfs64(o, kb);
		maybe_fake64(o);
		return 0;
	}
	if (!want_fake())
		return -1;
	fill_synth64(o);
	dbg_fake();
	return 0;
}
int statvfs(const char *p, struct vfs32 *o) {
	struct vfs64 t;
	if (statvfs64(p, &t) < 0)
		return -1;
	fill_vfs32(o, &t);
	return 0;
}
#endif
