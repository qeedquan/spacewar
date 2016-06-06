#include "u.h"
#include "libc.h"
#include "dat.h"
#include "fns.h"

static Word mask = 0777777;
static Word sign = 0400000;

extern const char *spacewar_rom[];

void
savestate(Mach *m, void *buf)
{
	size_t i;

	u8 *p;

	p = buf;
	p += put4(p, m->ac);
	p += put4(p, m->io);
	p += put4(p, m->pc);
	p += put4(p, m->ov);
	for (i = 0; i < nelem(m->mem); i++)
		p += put4(p, m->mem[i]);
	p += putm(p, m->flag, sizeof(m->flag));
	p += putm(p, m->sense, sizeof(m->sense));
	p += put1(p, m->halt);
	for (i = 0; i < nelem(m->sym); i++)
		p += put4(p, m->sym[i]);
}

void
loadstate(Mach *m, void *buf)
{
	size_t i;

	u8 *p;

	p = buf;
	p += get4(p, &m->ac);
	p += get4(p, &m->io);
	p += get4(p, &m->pc);
	p += get4(p, &m->ov);
	for (i = 0; i < nelem(m->mem); i++)
		p += get4(p, &m->mem[i]);
	p += getm(m->flag, p, sizeof(m->flag));
	p += getm(m->sense, p, sizeof(m->sense));
	p += get1(p, &m->halt);
	for (i = 0; i < nelem(m->sym); i++)
		p += get4(p, &m->sym[i]);
}

int
savestate_f(Mach *m, uint slot)
{
	FILE *fp;
	int   ret;

	if (slot >= nelem(m->state))
		return -EINVAL;

	fp = xfopen("%u.sav", "wb", slot);
	if (!fp)
		return -errno;

	ret = 0;
	memset(m->state[slot], 0, sizeof(m->state[slot]));
	savestate(m, m->state[slot]);
	if (fwrite(m->state[slot], 1, sizeof(m->state[slot]), fp) != sizeof(m->state[slot]))
		ret = -errno;

	fclose(fp);

	return ret;
}

int
loadstate_f(Mach *m, uint slot)
{
	FILE *fp;

	if (slot >= nelem(m->state))
		return -EINVAL;

	fp = xfopen("%u.sav", "rb", slot);
	if (!fp)
		return -errno;

	if (fread(m->state[slot], 1, sizeof(m->state[slot]), fp) != sizeof(m->state[slot]))
		return -errno;

	loadstate(m, m->state[slot]);
	fclose(fp);

	return 0;
}

void
loadrom(Mach *m)
{
	const char *line;
	size_t      len, i, j, n;
	Word        a, v;

	memset(m->mem, 0, sizeof(m->mem));
	for (n = 0; n < nelem(m->sym); n++) {
		m->sym[n] = 0xffffffff;
	}

	for (n = 0; (line = spacewar_rom[n]); n++) {
		if (line[0] != ' ' && line[0] != '+')
			continue;
		len = strlen(line);

		a = 0;
		for (i = 1; i < len && '0' <= line[i] && line[i] <= '7'; i++)
			a = a * 8 + line[i] - '0';
		if (i >= len || line[i] != '\t' || i == 1)
			continue;

		v = 0;
		j = i;
		for (i++; i < len && '0' <= line[i] && line[i] <= '7'; i++)
			v = v * 8 + line[i] - '0';

		if (i == j)
			continue;

		m->mem[a] = v;
		m->sym[a] = n;
	}
}

void
initmach(Mach *m, SDL_Renderer *re, Config *conf)
{
	size_t i;
	u8     r, g, b;

	for (i = 0; i < nelem(m->cmap); i++) {
		r = 0;
		g = min(i * 2, 255);
		b = 0;

		if (conf->white) {
			r = min(i * 2, 255);
			g = min(i * 2, 255);
			b = min(i * 2, 255);
		}
		m->cmap[i] = r | g << 8 | b << 16 | 0xff000000;
	}

	m->dx = m->dy = 512;
	m->tex        = SDL_CreateTexture(re, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, m->dx, m->dy);
	if (!m->tex)
		fatal("Failed to create texture for display: %s", SDL_GetError());
}

void
reset(Mach *m)
{
	loadrom(m);
	m->ac = m->io = m->ov = m->halt = 0;
	m->pc                           = 4;
	memset(m->flag, 0, sizeof(m->flag));
	memset(m->sense, 0, sizeof(m->sense));
	m->frametime = SDL_GetTicks();
}

Word
memread(Mach *m, Word a)
{
	return m->mem[a & 07777];
}

void
memwrite(Mach *m, Word a, Word v)
{
	m->mem[a & 07777] = v;
}

void
step(Mach *m)
{
	Word inst;
	Inst ip;

	disasm(&ip, m, m->pc);
	if (m->halt)
		return;

	if (ip.mode & AMEM)
		m->sym[ip.addr] = 0xffffffff;

	/* printf("%04x %s", m->pc, ip.str); */
	inst = memread(m, m->pc++);
	if (exec(m, inst))
		m->halt |= 0x1;
}

static Word
norm(Word i)
{
	i += i >> 18;
	i &= mask;
	if (i == mask)
		i = 0;
	return i;
}

static void
trap(Mach *m, Word a)
{
	int x, y;

	switch (a & 077) {
	case 7:
		x = (m->ac + 0400000) & 0777777;
		y = (m->io + 0400000) & 0777777;
		x = x * m->dx / 0777777;
		y = y * m->dy / 0777777;
		if (0 <= x && x < m->dx && 0 <= y && y < m->dy)
			m->pix[y][x] = min(m->pix[y][x] + 128, 255);
		break;
	case 011:
		m->io = m->ctl;
		break;
	}
}

int
exec(Mach *m, Word inst)
{
	Word ib, y, op, n, a, diffSigns, ac, io, count, i;
	u8   cond, f;
	u64  w;

	ib = (inst >> 12) & 1;
	y  = inst & 07777;
	op = inst >> 13;
	if (op < SKP && op != CALJDA) {
		for (n = 0; ib != 0; n++) {
			if (n > 07777)
				return -ELOOP;
			ib = (m->mem[y] >> 12) & 1;
			y  = m->mem[y] & 07777;
		}
	}

	switch (op) {
	case AND:
		m->ac &= m->mem[y];
		break;
	case IOR:
		m->ac |= m->mem[y];
		break;
	case XOR:
		m->ac ^= m->mem[y];
		break;
	case XCT:
		exec(m, m->mem[y]);
		break;
	case CALJDA:
		a = y;
		if (ib == 0)
			a = 64;
		m->mem[a] = m->ac;
		m->ac     = (m->ov << 17) + m->pc;
		m->pc     = a + 1;
		break;
	case LAC:
		m->ac = m->mem[y];
		break;
	case LIO:
		m->io = m->mem[y];
		break;
	case DAC:
		m->mem[y] = m->ac;
		break;
	case DAP:
		m->mem[y] = (m->mem[y] & 0770000) | (m->ac & 07777);
		break;
	case DIO:
		m->mem[y] = m->io;
		break;
	case DZM:
		m->mem[y] = 0;
		break;
	case ADD:
		m->ac += m->mem[y];
		m->ov = m->ac >> 18;
		m->ac = norm(m->ac);
		break;
	case SUB:
		diffSigns = (m->ac ^ m->mem[y]) >> 17 == 1;
		m->ac += m->mem[y] ^ mask;
		m->ac = norm(m->ac);
		if (diffSigns && m->mem[y] >> 17 == m->ac >> 17)
			m->ov = 1;
		break;
	case IDX:
		m->ac     = norm(m->mem[y] + 1);
		m->mem[y] = m->ac;
		break;
	case ISP:
		m->ac     = norm(m->mem[y] + 1);
		m->mem[y] = m->ac;
		if ((m->ac & sign) == 0)
			m->pc++;
		break;
	case SAD:
		if (m->ac != m->mem[y])
			m->pc++;
		break;
	case SAS:
		if (m->ac == m->mem[y])
			m->pc++;
		break;
	case MUS:
		if ((m->io & 1) == 1) {
			m->ac += m->mem[y];
			m->ac = norm(m->ac);
		}
		m->io = (m->io >> 1 | m->ac << 17) & mask;
		m->ac >>= 1;
		break;
	case DIS:
		ac    = (m->ac << 1 | m->io >> 17) & mask;
		io    = ((m->io << 1 | m->ac >> 17) & mask) ^ 1;
		m->ac = ac;
		m->io = io;

		if ((m->io & 1) == 1)
			m->ac = m->ac + (m->mem[y] ^ mask);
		else
			m->ac = m->ac + 1 + m->mem[y];

		m->ac = norm(m->ac);
		break;
	case JMP:
		m->pc = y;
		break;
	case JSP:
		m->ac = (m->ov << 17) + m->pc;
		m->pc = y;
		break;
	case SKP:
		cond = (((y & 0100) == 0100) && m->ac == 0) ||
		       (((y & 0200) == 0200) && m->ac >> 17 == 0) ||
		       (((y & 0400) == 0400) && m->ac >> 17 == 1) ||
		       (((y & 01000) == 01000) && m->ov == 0) ||
		       (((y & 02000) == 02000) && (m->io >> 17 == 0)) ||
		       (((y & 7) != 0) && !m->flag[y & 7]) ||
		       (((y & 070) != 0) && !m->sense[(y & 070) >> 3]) ||
		       ((y & 070) == 010);
		if ((ib == 0) == cond)
			m->pc++;
		if ((y & 01000) == 01000)
			m->ov = 0;
		break;
	case SFT:
		for (count = inst & 0777; count != 0; count >>= 1) {
			if ((count & 1) == 0)
				continue;
			switch ((inst >> 9) & 017) {
			case 001: /* rotate AC left */
				m->ac = (m->ac << 1 | m->ac >> 17) & mask;
				break;
			case 002: /* rotate IO left */
				m->io = (m->io << 1 | m->io >> 17) & mask;
				break;
			case 003: /* rotate AC and IO left. */
				w     = ((u64)(m->ac) << 18) | (u64)m->io;
				w     = w << 1 | w >> 35;
				m->ac = (w >> 18) & mask;
				m->io = w & mask;
				break;
			case 005: /* shift AC left (excluding sign bit) */
				m->ac = ((m->ac << 1 | m->ac >> 17) & mask & ~sign) | (m->ac & sign);
				break;
			case 006: /* shift IO left (excluding sign bit) */
				m->io = ((m->io << 1 | m->io >> 17) & mask & ~sign) | (m->io & sign);
				break;
			case 007: /* shift AC and IO left (excluding AC's sign bit) */
				w     = ((u64)m->ac << 18) | (u64)m->io;
				w     = w << 1 | w >> 35;
				m->ac = ((w >> 18) & mask & ~sign) | (m->ac & sign);
				m->io = (w & mask & ~sign) | (m->ac & sign);
				break;
			case 011: /* rotate AC right */
				m->ac = (m->ac >> 1 | m->ac << 17) & mask;
				break;
			case 012: /* rotate IO right */
				m->io = (m->io >> 1 | m->io << 17) & mask;
				break;
			case 013: /* rotate AC and IO right */
				w     = ((u64)m->ac << 18) | (u64)m->io;
				w     = w >> 1 | w << 35;
				m->ac = (w >> 18) & mask;
				m->io = w & mask;
				break;
			case 015: /* shift AC right (excluding sign bit) */
				m->ac = (m->ac >> 1) | (m->ac & sign);
				break;
			case 016: /* shift IO right (excluding sign bit) */
				m->io = (m->io >> 1) | (m->io & sign);
				break;
			case 017: /* shift AC and IO right (excluding AC's sign bit) */
				w     = ((u64)m->ac << 18) | (u64)m->io;
				w     = w >> 1;
				m->ac = (w >> 18) | (m->ac & sign);
				m->io = w & mask;
				break;
			default:
				return -EINST;
			}
		}
		break;
	case LAW:
		if (ib == 0)
			m->ac = y;
		else
			m->ac = y ^ mask;
		break;
	case IOT:
		trap(m, y);
		break;
	case OPR:
		if ((y & 0200) == 0200)
			m->ac = 0;
		if ((y & 04000) == 04000)
			m->io = 0;
		if ((y & 01000) == 01000)
			m->ac ^= mask;
		if ((y & 0400) == 0400) {
			m->pc--;
			return -EHLT;
		}
		i = y & 7;
		f = (y & 010) == 010;
		if (i == 7) {
			for (i = 2; i < 7; i++) {
				m->flag[i] = f;
			}
		} else if (i >= 2)
			m->flag[i] = f;
		break;
	default:
		return -EINST;
	}

	return 0;
}

static struct {
	char *str;
	u32   mode;
} optab[] = {
        [AND]    = {"and", AREG},
        [IOR]    = {"ior", AREG},
        [XOR]    = {"xor", AREG},
        [XCT]    = {"xct", AXEC},
        [CALJDA] = {"caljda", AREG | AMEM},

        [LAC] = {"lac", AREG},
        [LIO] = {"lio", AREG},
        [DAC] = {"dac", AMEM},
        [DAP] = {"dap", AMEM},
        [DIO] = {"dio", AMEM},
        [DZM] = {"dzm", AMEM},

        [ADD] = {"add", AREG},
        [SUB] = {"sub", AREG},
        [IDX] = {"idx", AREG | AMEM},
        [ISP] = {"isp", AREG | AMEM},
        [SAD] = {"sad", AJMP},
        [SAS] = {"sas", AJMP},
        [MUS] = {"mus", AREG},
        [DIS] = {"dis", AREG},

        [JMP] = {"jmp", AJMP},
        [JSP] = {"jsp", AREG | AJMP},
        [SKP] = {"skp", AREG | AJMP},
        [SFT] = {"sft", AREG},
        [LAW] = {"law", AREG},
        [IOT] = {"iot", ATRAP},
        [OPR] = {"opr", AREG},
};

void
disasm(Inst *ip, Mach *m, u32 a)
{
	Word y, ib;

	a &= 07777;
	ip->enc  = memread(m, a);
	ip->op   = ip->enc >> 13;
	ip->mode = 0;
	ip->addr = a;
	y        = ip->op & 07777;
	ib       = (ip->op >> 12) & 1;

	if (m->sym[a] != 0xffffffff) {
		snprintf(ip->str, sizeof(ip->str), "%s", spacewar_rom[m->sym[a]]);
		ip->mode = optab[ip->op].mode;
	} else {
		if (ip->op < (int)nelem(optab) && optab[ip->op].str) {
			snprintf(ip->str, sizeof(ip->str), "D %s ib=%#o y=%#o mem[y]=%#o\n",
			         optab[ip->op].str, ib, y, m->mem[y]);
			ip->mode = optab[ip->op].mode;
		} else
			snprintf(ip->str, sizeof(ip->str), "D unk\n");
	}
}
