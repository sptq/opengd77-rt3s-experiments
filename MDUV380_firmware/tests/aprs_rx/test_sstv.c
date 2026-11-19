// Host test for the SSTV decoder: encode a test card in each mode, decode it back.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "functions/sstvRx.h"

#define FS 8000.0
static int16_t *buf; static size_t nbuf, cap;
static double phase;
static unsigned rngState = 1;
static double rate = FS; // sender sample clock (skew test)
static double tAcc;      // fractional samples carried

static double noise(void) { double s = 0; for (int i = 0; i < 12; i++) { rngState = rngState * 1103515245u + 12345u; s += ((rngState >> 8) & 0xFFFF) / 65536.0; } return s - 6.0; }
static double noiseAmp;
static void tone(double f, double ms) {
	tAcc += ms * rate / 1000.0;
	int n = (int)tAcc; tAcc -= n;
	for (int i = 0; i < n; i++) {
		if (nbuf >= cap) { cap = cap ? cap * 2 : 1 << 20; buf = realloc(buf, cap * sizeof(int16_t)); }
		phase += 2 * M_PI * f / rate; buf[nbuf++] = (int16_t)(8000 * sin(phase) + noise() * noiseAmp);
	} }
static double v2f(double v) { return 1500 + v * 800.0 / 255.0; }

// test card: 8 vertical colour bars on the top half, grey ramp on the bottom half
static void card(double x, double y, double *r, double *g, double *b) { // x,y in 0..1
	static const int bars[8][3] = {{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}};
	if (y < 0.5) { int i = (int)(x * 8); if (i > 7) i = 7; *r = bars[i][0]; *g = bars[i][1]; *b = bars[i][2]; }
	else { *r = *g = *b = x * 255; } }
static void yuv(double r, double g, double b, double *y, double *ry, double *by) {
	*y = 16 + (65.738 * r + 129.057 * g + 25.064 * b) / 256; *ry = 128 + (112.439 * r - 94.154 * g - 18.285 * b) / 256; *by = 128 + (-37.945 * r - 74.494 * g + 112.439 * b) / 256; }
static void scan(double ms, int ch, double y, int width, int kind /*0 r,1 g,2 b,3 Y,4 RY,5 BY*/) {
	(void)ch;
	for (int i = 0; i < width; i++) { double r, g, b, Y, RY, BY, v; card((i + 0.5) / width, y, &r, &g, &b); yuv(r, g, b, &Y, &RY, &BY);
		v = (kind == 0) ? r : (kind == 1) ? g : (kind == 2) ? b : (kind == 3) ? Y : (kind == 4) ? RY : BY; tone(v2f(v), ms / width); } }

static void vis(int code) {
	tone(1900, 300); tone(1200, 10); tone(1900, 300); tone(1200, 30);
	int ones = 0; for (int i = 0; i < 7; i++) { int bit = (code >> i) & 1; ones += bit; tone(bit ? 1100 : 1300, 30); }
	tone((ones & 1) ? 1100 : 1300, 30); tone(1200, 30); }

static void encode(const char *mode) {
	if (!strcmp(mode, "Robot 36")) { vis(8); for (int l = 0; l < 240; l++) { double y = (l + 0.5) / 240; tone(1200, 9); tone(1500, 3); scan(88, 0, y, 320, 3); tone((l & 1) ? 2300 : 1500, 4.5); tone(1900, 1.5); scan(44, 0, y, 160, (l & 1) ? 5 : 4); } }
	else if (!strcmp(mode, "Robot 72")) { vis(12); for (int l = 0; l < 240; l++) { double y = (l + 0.5) / 240; tone(1200, 9); tone(1500, 3); scan(138, 0, y, 320, 3); tone(1500, 4.5); tone(1900, 1.5); scan(69, 0, y, 160, 4); tone(2300, 4.5); tone(1500, 1.5); scan(69, 0, y, 160, 5); } }
	else if (!strcmp(mode, "Martin M1")) { vis(44); for (int l = 0; l < 256; l++) { double y = (l + 0.5) / 256; tone(1200, 4.862); tone(1500, 0.572); scan(146.432, 0, y, 320, 1); tone(1500, 0.572); scan(146.432, 0, y, 320, 2); tone(1500, 0.572); scan(146.432, 0, y, 320, 0); tone(1500, 0.572); } }
	else if (!strcmp(mode, "Scottie S1")) { vis(60); tone(1200, 9); for (int l = 0; l < 256; l++) { double y = (l + 0.5) / 256; tone(1500, 1.5); scan(138.24, 0, y, 320, 1); tone(1500, 1.5); scan(138.24, 0, y, 320, 2); tone(1200, 9); tone(1500, 1.5); scan(138.24, 0, y, 320, 0); } }
	else if (!strcmp(mode, "PD 120")) { vis(95); for (int l = 0; l < 496; l += 2) { double y1 = (l + 0.5) / 496, y2 = (l + 1.5) / 496; tone(1200, 20); tone(1500, 2.08); scan(121.6, 0, y1, 640, 3); scan(121.6, 0, (y1 + y2) / 2, 640, 4); scan(121.6, 0, (y1 + y2) / 2, 640, 5); scan(121.6, 0, y2, 640, 3); } }
	tone(1500, 300); }

static uint8_t img[SSTV_RX_ROWS][SSTV_RX_WIDTH][3];
static int rowsSeen[SSTV_RX_ROWS];
static void px(uint16_t x, uint16_t row, uint8_t r, uint8_t g, uint8_t b) { img[row][x][0] = r; img[row][x][1] = g; img[row][x][2] = b; rowsSeen[row] = 1; }

static int fails;
static void run(const char *mode, double skew, double nAmp, double maxErr) {
	static uint8_t scratch[SSTV_RX_SCRATCH_SIZE];
	nbuf = 0; phase = 0; tAcc = 0; rate = FS * (1.0 + skew); noiseAmp = nAmp;
	memset(img, 0, sizeof(img)); memset(rowsSeen, 0, sizeof(rowsSeen));
	tone(1500, 500); encode(mode);
	sstvRxInit(scratch, px);
	for (size_t i = 0; i < nbuf; i += 160) sstvRxProcess(buf + i, (nbuf - i) < 160 ? (uint32_t)(nbuf - i) : 160, 1);
	sstvRxStatus_t st; sstvRxGetStatus(&st);
	int rows = 0; double err = 0; int n = 0;
	for (int r = 0; r < (int)SSTV_RX_ROWS; r++) { rows += rowsSeen[r];
		for (int x = 4; x < (int)SSTV_RX_WIDTH - 4; x++) { double cr, cg, cb; card((x + 0.5) / SSTV_RX_WIDTH, (r + 0.5) / SSTV_RX_ROWS, &cr, &cg, &cb);
			// skip pixels next to a bar edge / the half-way line (filter smear)
			double fx = (x + 0.5) / SSTV_RX_WIDTH * 8; if (fabs(fx - floor(fx + 0.5)) < 0.12) continue; if (abs(r - 60) < 3) continue;
			err += fabs(img[r][x][0] - cr) + fabs(img[r][x][1] - cg) + fabs(img[r][x][2] - cb); n += 3; } }
	err /= n;
	int ok = (rows >= (int)SSTV_RX_ROWS - 1) && (err < maxErr) && !strcmp(st.modeName, mode);
	printf("%-4s sstv %-10s skew %+.1f%% noise %4.0f: mode '%s' rows %d mean err %.1f\n", ok ? "OK" : "FAIL", mode, skew * 100, nAmp, st.modeName, rows, err);
	if (!ok) fails++;
	if (getenv("SSTV_PPM")) { char fn[64]; snprintf(fn, sizeof(fn), "sstv_%c%c.ppm", mode[0], mode[strlen(mode) - 1]); FILE *f = fopen(fn, "wb"); fprintf(f, "P6 %u %u 255\n", SSTV_RX_WIDTH, SSTV_RX_ROWS); fwrite(img, 1, sizeof(img), f); fclose(f); }
}

int main(void) {
	const char *m[] = { "Robot 36", "Robot 72", "Martin M1", "Scottie S1", "PD 120" };
	for (unsigned i = 0; i < 5; i++) run(m[i], 0, 0, 12);
	for (unsigned i = 0; i < 5; i++) run(m[i], 0.002, 300, 20);
	run("Robot 36", -0.003, 600, 25);
	printf(fails ? "FAILED: %d\n" : "ALL PASSED\n", fails);
	return fails; }
