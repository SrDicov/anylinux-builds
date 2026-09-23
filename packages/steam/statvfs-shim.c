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

/* Verbose: log every intercepted path (capped). */
static void dbg_path(const char *p) {
	static const char pre[] = "steam-shim: statvfs ";
	unsigned long n = 0;
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
		2, "\n", 1);
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
	if ((o->flag & 1u) != 1u || o->bavail != 0)
		return;
	if (!want_fake())
		return;
	want = (8ull * 1024 * 1024 * 1024) / (o->frsize ? o->frsize : 4096u);
	if (o->blocks < want)
		o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	dbg_fake();
}
int statvfs(const char *p, struct vfs *o) {
	struct kbuf {
		u64 w[15];
	} kb;
	long r;
	dbg_call(p);
	r = raw_statfs(p, &kb);
	if (r < 0)
		return -1;
	fill_vfs(o, kb.w);
	maybe_fake(o);
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
	u64 want;
	if ((o->flag & 1u) != 1u || o->bavail != 0)
		return;
	if (!want_fake())
		return;
	want = (8ull * 1024 * 1024 * 1024) / (o->frsize ? o->frsize : 4096u);
	if (o->blocks < want)
		o->blocks = want;
	o->bfree = want;
	o->bavail = want;
	dbg_fake();
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
	long r;
	dbg_call(p);
	r = raw_statfs(p, kb);
	if (r < 0)
		return -1;
	fill_vfs64(o, kb);
	maybe_fake64(o);
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
