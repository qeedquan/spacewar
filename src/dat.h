typedef u32 Word;

typedef struct {
	char str[64];
	Word enc;
	int  op;
	int  mode;
	u32  addr;
} Inst;

typedef struct {
	Word ac, io, pc, ov;
	Word mem[010000];
	u32  sym[010000];
	u8   flag[7];
	u8   sense[7];
	u8   halt;

	Word ctl;
	u32  frametime;

	SDL_Texture *tex;
	u32          nframe;
	u32          cmap[256];
	u8           pix[512][512];
	int          dx, dy;

	u8   state[10][64 * 1024];
	uint statepos;
} Mach;

enum {
	AREG  = 1 << 0,
	AMEM  = 1 << 1,
	AJMP  = 1 << 2,
	AXEC  = 1 << 3,
	ATRAP = 1 << 4
};

enum {
	AND    = 01,
	IOR    = 02,
	XOR    = 03,
	XCT    = 04,
	CALJDA = 07,

	LAC = 010,
	LIO = 011,
	DAC = 012,
	DAP = 013,
	DIO = 015,
	DZM = 016,

	ADD = 020,
	SUB = 021,
	IDX = 022,
	ISP = 023,
	SAD = 024,
	SAS = 025,
	MUS = 026,
	DIS = 027,

	JMP = 030,
	JSP = 031,
	SKP = 032,
	SFT = 033,
	LAW = 034,
	IOT = 035,
	OPR = 037
};

enum {
	EINST = 0x12345,
	EHLT
};

enum {
	B1F,
	B1D,
	B1A,
	B1S,
	B2B,
	B2S,
	B2K,
	B2L,
	BST,
	BLT,
	BIS,
	BDS,
	BRS,
	BPU,
	BES,
	BFS,
	BMAX
};

typedef struct {
	s64 key[BMAX];
	s64 button[BMAX];
	s64 axis[BMAX];
	s32 axis_threshold;

	SDL_GameController **ctx;
	int                  nctx;
} Controller;

typedef struct {
	double fps;
	double frameskip;
	u8     white;
} Config;
