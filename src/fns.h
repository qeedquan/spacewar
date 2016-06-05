#define nelem(x) (sizeof(x) / sizeof(x[0]))
#define min(a, b) (((a) < (b)) ? (a) : (b))

void loadrom(Mach *);
void savestate(Mach *, void *);
void loadstate(Mach *, void *);
void initmach(Mach *, SDL_Renderer *, Config *);
void reset(Mach *);
int  step(Mach *);
int  exec(Mach *, Word);
Word memread(Mach *, Word);
void memwrite(Mach *, Word, Word);
void disasm(Inst *, Mach *, Word);

void *ecalloc(size_t, size_t);
void  fatal(const char *, ...);

FILE *xfopen(const char *, const char *, ...);

size_t put1(u8 *, u8);
size_t put4(u8 *, u32);
size_t putm(u8 *, u8 *, size_t);
size_t get1(u8 *, u8 *);
size_t get4(u8 *, u32 *);
size_t getm(u8 *, u8 *, size_t);

int loadconfig(Controller *, Config *, const char *);
int saveconfig(Controller *, Config *, const char *);
int loadstate_f(Mach *, uint);
int savestate_f(Mach *, uint);

void        setrootdir(const char *);
const char *rootdir(void);
char *      fpath(const char *, ...);
