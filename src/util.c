#include "u.h"
#include "libc.h"
#include "dat.h"
#include "fns.h"

size_t
put1(u8 *b, u8 v)
{
	b[0] = v;
	return 1;
}

size_t
put4(u8 *b, u32 v)
{
	b[0] = v & 0xff;
	b[1] = (v >> 8) & 0xff;
	b[2] = (v >> 16) & 0xff;
	b[3] = (v >> 24) & 0xff;
	return 4;
}

size_t
putm(u8 *dst, u8 *src, size_t size)
{
	memmove(dst, src, size);
	return size;
}

size_t
get1(u8 *b, u8 *v)
{
	b[0] = *v;
	return 1;
}

size_t
get4(u8 *b, u32 *v)
{
	*v = (u32)b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
	return 4;
}

size_t
getm(u8 *dst, u8 *src, size_t n)
{
	memmove(dst, src, n);
	return n;
}

void
fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

FILE *
xfopen(const char *fmt, const char *mode, ...)
{
	FILE *  fp;
	char    buf[80], *path;
	va_list ap;

	va_start(ap, mode);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	path = fpath(buf);
	fp   = fopen(path, mode);
	free(path);

	return fp;
}

char *
estrdup(const char *s)
{
	char *p;

	p = strdup(s);
	if (!p)
		fatal("Failed to allocate memory: %s", strerror(errno));
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *ptr;

	if (nmemb == 0)
		nmemb++;
	ptr = calloc(nmemb, size);
	if (!ptr)
		fatal("Failed to allocate memory: %s", strerror(errno));
	return ptr;
}

#define MUL_NO_OVERFLOW ((size_t)1 << (sizeof(size_t) * 4))

void *
reallocarray(void *optr, size_t nmemb, size_t size)
{
	if ((nmemb >= MUL_NO_OVERFLOW || size >= MUL_NO_OVERFLOW) &&
	    nmemb > 0 && SIZE_MAX / nmemb < size) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(optr, size * nmemb);
}

void *
ereallocarray(void *optr, size_t nmemb, size_t size)
{
	void *ptr;

	ptr = reallocarray(optr, nmemb, size);
	if (!ptr)
		fatal("Failed to reallocate memory: %s", strerror(errno));
	return ptr;
}

static char *
trim(char *s)
{
	size_t n;

	if (!s)
		return s;

	while (isspace(*s))
		s++;

	n = strlen(s);
	while (n > 0 && isspace(s[n - 1]))
		n--;
	s[n] = '\0';

	return s;
}

static struct {
	const char *str;
	const char *key;
	const char *pad;
} dc[] = {
    {"p1fire", "f", "dpup"},
    {"p1acc", "d", "dpdown"},
    {"p1rot", "a", "dpleft"},
    {"p1cwrot", "s", "dpright"},
    {"p2fire", "'", "a"},
    {"p2acc", ";", "b"},
    {"p2rot", "k", "y"},
    {"p2cwrot", "l", "x"},
    {"psavestate", "f2", "leftshoulder"},
    {"ploadstate", "f4", "rightshoulder"},
    {"pincstate", "f3", "leftx"},
    {"pdecstate", "f1", "rightx"},
    {"preset", "r", "guide"},
    {"ppause", "Space", "leftstick"},
    {"pesc", "Escape", "rightstick"},
    {"pframeskip", "`", "back"},
};

int
loadconfig(Controller *ctl, Config *conf, const char *name)
{
	char   line[1024], buf[80], *key, *value, *saveptr;
	FILE * fp;
	size_t i;
	int    n, v;

	for (i = 0; i < nelem(dc); i++) {
		ctl->key[i]    = SDL_GetKeyFromName(dc[i].key);
		ctl->button[i] = SDL_GameControllerGetButtonFromString(dc[i].pad);
		ctl->axis[i]   = -1;
		if (ctl->button[i] < 0)
			ctl->axis[i] = SDL_GameControllerGetAxisFromString(dc[i].pad);
	}
	ctl->axis_threshold = 10000;
	conf->fps           = 35;
	conf->white         = 0;
	conf->frameskip     = 1;

	fp = xfopen(name, "rt");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		key   = trim(strtok_r(line, "=", &saveptr));
		value = trim(strtok_r(NULL, "=", &saveptr));
		if (!key || !value)
			continue;

		if (!strcasecmp(key, "fps")) {
			conf->fps = atof(value);
			continue;
		} else if (!strcasecmp(key, "white")) {
			conf->white = atoi(value);
			continue;
		} else if (!strcasecmp(key, "axis_threshold")) {
			ctl->axis_threshold = atof(value);
			continue;
		}

		for (i = 0; i < nelem(dc); i++) {
			snprintf(buf, sizeof(buf), "%s_key", dc[i].str);
			if (!strcasecmp(buf, key)) {
				if ((v = SDL_GetKeyFromName(value)) >= 0) {
					ctl->key[i] = v;
				}
				break;
			}

			snprintf(buf, sizeof(buf), "%s_pad", dc[i].str);
			if (!strcasecmp(buf, key) && sscanf(value, "%d, %32s", &n, buf) == 2) {
				if ((v = SDL_GameControllerGetButtonFromString(buf)) >= 0) {
					ctl->button[i] = v | (n << 16);
				} else if ((v = SDL_GameControllerGetButtonFromString(buf)) >= 0) {
					ctl->axis[i] = v | (n << 16);
				}
				break;
			}
		}
	}

	fclose(fp);
	return 0;
}

int
saveconfig(Controller *ctl, Config *conf, const char *name)
{
	FILE *      fp;
	const char *str;
	size_t      i;

	fp = xfopen(name, "wt");
	if (!fp)
		return -1;

	for (i = 0; i < nelem(dc); i++) {
		str = SDL_GetKeyName(ctl->key[i]);
		if (str)
			fprintf(fp, "%s_key = %s\n", dc[i].str, str);
	}

	for (i = 0; i < nelem(dc); i++) {
		str = SDL_GameControllerGetStringForButton(ctl->button[i]);
		if (!str)
			str = SDL_GameControllerGetStringForAxis(ctl->axis[i]);
		if (str)
			fprintf(fp, "%s_pad = %s\n", dc[i].str, str);
	}
	fprintf(fp, "axis_threshold = %d\n", ctl->axis_threshold);

	fprintf(fp, "fps = %lf\n", conf->fps);
	fprintf(fp, "white = %d\n", conf->white);

	fclose(fp);
	return 0;
}

static char *rdir;

void
setrootdir(const char *dir)
{
	if (!dir)
		dir = SDL_GetPrefPath("", "spacewar");

	if (!dir)
		dir = "";

	free(rdir);
	rdir = estrdup(dir);
}

const char *
rootdir(void)
{
	return rdir;
}

#if defined(__WINDOWS__) || defined(__WINRT__)
#define SEP '\\'
#else
#define SEP '/'
#endif

char *
fpath(const char *fmt, ...)
{
	const char *dir;
	char        buf[1024], *path, *str;
	va_list     ap;
	int         n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	dir  = rootdir();
	path = ecalloc(1, strlen(dir) + n + 8);
	sprintf(path, "%s/%s", dir, buf);

	str = path;
	while ((str = strchr(str, '/')))
		*str++ = SEP;

	return path;
}
