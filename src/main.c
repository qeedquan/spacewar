#include "u.h"
#include "libc.h"
#include "dat.h"
#include "fns.h"

Mach mach;

SDL_Window *window;

SDL_Renderer *renderer;

Controller ctl;

Config conf;

static void
usage(void)
{
	fprintf(stderr, "usage: [options]\n\n");
	fprintf(stderr, "-d <spacewar_dir>\n");
	fprintf(stderr, "    location to load/save spacewars data\n");
	fprintf(stderr, "-h  show this help message\n");
	exit(2);
}

static void
parseargs(int argc, char *argv[])
{
	char *dir;
	int   i, args;

	dir = NULL;
	while (argc > 1) {
		if (argv[1][0] != '-')
			break;

		args = 1;
		for (i = 1; argv[1][i] != '\0'; i++) {
			switch (argv[1][i]) {
			case 'd':
				if (!argv[2])
					usage();
				dir = argv[2];
				break;

			case 'h':
			default:
				usage();
			}

			if (argv[1][i] == 'd') {
				args++;
				break;
			}
		}

		argc -= args;
		argv += args;
	}

	setrootdir(dir);
	loadconfig(&ctl, &conf, "config");
	saveconfig(&ctl, &conf, "config");
}

static void
remapctl(void)
{
	int i, n;

	for (i = 0; i < ctl.nctx; i++)
		SDL_GameControllerClose(ctl.ctx[i]);
	free(ctl.ctx);

	n        = SDL_NumJoysticks();
	ctl.ctx  = ecalloc(n, sizeof(*ctl.ctx));
	ctl.nctx = 0;
	for (i = 0; i < n; i++) {
		if (SDL_IsGameController(i)) {
			ctl.ctx[i] = SDL_GameControllerOpen(i);
			if (!ctl.ctx[i])
				fprintf(stderr, "Failed to open controller %d: %s\n", i + 1, SDL_GetError());
			else
				ctl.nctx++;
		}
	}
}

static void
initsdl(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

	if (SDL_Init(SDL_INIT_EVERYTHING & ~SDL_INIT_AUDIO) < 0)
		fatal("Failed to init SDL: %s", SDL_GetError());

	if (SDL_CreateWindowAndRenderer(512, 512, SDL_WINDOW_RESIZABLE, &window, &renderer) < 0)
		fatal("Failed to create SDL window: %s", SDL_GetError());

	SDL_SetWindowTitle(window, "Spacewar");
	SDL_RenderSetLogicalSize(renderer, 512, 512);

	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	remapctl();
}

static void
input(Mach *m, s64 *map, s64 button, bool clear)
{
	static Word bits[] = {
	    0000001, 0000002, 0000004, 0000010,
	    0040000, 0100000, 0200000, 0400000,
	};
	size_t i;

	for (i = 0; i <= B2L; i++) {
		if (map[i] == button) {
			if (clear)
				m->ctl &= ~bits[i];
			else
				m->ctl |= bits[i];
		}
	}

	if (!clear) {
		if (map[BST] == button)
			savestate_f(m, m->statepos);
		else if (map[BLT] == button)
			loadstate_f(m, m->statepos);
		else if (map[BIS] == button) {
			if (++m->statepos >= nelem(m->state))
				m->statepos = 0;
		} else if (map[BDS] == button) {
			if (m->statepos == 0)
				m->statepos = nelem(m->state) - 1;
			else
				m->statepos--;
		} else if (map[BRS] == button)
			reset(m);
		else if (map[BPU] == button)
			m->halt ^= 0x2;
		else if (map[BES] == button)
			exit(0);
		else if (map[BFS] == button)
			conf.frameskip = 8;
	} else {
		if (map[BFS] == button)
			conf.frameskip = 1;
	}
}

static s64
j2d(int which)
{
	SDL_GameController *c;
	int                 i;

	c = SDL_GameControllerFromInstanceID(which);
	if (!c)
		return -1;

	for (i = 0; i < ctl.nctx; i++) {
		if (ctl.ctx[i] == c)
			return i;
	}
	return -1;
}

static void
event(void)
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			exit(0);

		case SDL_KEYDOWN:
			input(&mach, ctl.key, ev.key.keysym.sym, false);
			break;

		case SDL_KEYUP:
			input(&mach, ctl.key, ev.key.keysym.sym, true);
			break;

		case SDL_CONTROLLERAXISMOTION:
			input(&mach, ctl.button, ev.caxis.axis | (j2d(ev.caxis.which) << 16),
			      abs(ev.caxis.value) >= ctl.axis_threshold);
			break;

		case SDL_CONTROLLERBUTTONDOWN:
			input(&mach, ctl.button, ev.cbutton.button | (j2d(ev.cbutton.which) << 16), false);
			break;

		case SDL_CONTROLLERBUTTONUP:
			input(&mach, ctl.button, ev.cbutton.button | (j2d(ev.cbutton.which) << 16), true);
			break;

		case SDL_CONTROLLERDEVICEADDED:
			remapctl();
			break;
		}
	}
}

static void
flush(Mach *m)
{
	int   x, y, pitch;
	void *frame;
	u32 * pix;

	SDL_LockTexture(m->tex, NULL, &frame, &pitch);
	pix = frame;
	for (y = 0; y < m->dy; y++) {
		for (x = 0; x < m->dx; x++) {
			pix[x] = m->cmap[m->pix[y][x]];
			m->pix[y][x] >>= 1;
		}
		pix += pitch / 4;
	}
	SDL_UnlockTexture(m->tex);
}

static void
draw(void)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	SDL_RenderCopy(renderer, mach.tex, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static void
emulate(Mach *m)
{
	u32    frame, maxframe, t;
	double speed;

	t        = SDL_GetTicks();
	speed    = conf.fps * conf.frameskip;
	frame    = 0;
	maxframe = 1 + ceil((t - m->frametime) / (1000.0 / speed));
	for (;;) {
		if (frame < maxframe) {
			step(m);
			if (m->pc == 02051 && !m->halt) {
				flush(m);
				frame++;
			}
		}

		if (SDL_GetTicks() - t > 1000.0 / speed && frame >= maxframe) {
			break;
		}
	}

	m->frametime = SDL_GetTicks();
}

static void
loop(void)
{
	Mach *m;

	m = &mach;
	for (;;) {
		event();
		emulate(m);
		draw();
	}
}

int
main(int argc, char *argv[])
{
	parseargs(argc, argv);
	initsdl();
	initmach(&mach, renderer, &conf);
	reset(&mach);
	loop();
	return 0;
}
