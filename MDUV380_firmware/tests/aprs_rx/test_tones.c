// Host test for the DTMF and CW decoders (toneRxDecoders.c)
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "functions/toneRxDecoders.h"

#define FS 8000
static int16_t buf[FS * 60];
static int nbuf;
static char out[256];
static int nout;
static unsigned rngState = 12345;

static void cb(char c) { if (nout < 255) out[nout++] = c; out[nout] = 0; }
static double noise(void) { // ~ gaussian, unit variance
	double s = 0; for (int i = 0; i < 12; i++) { rngState = rngState * 1103515245u + 12345u; s += ((rngState >> 8) & 0xFFFF) / 65536.0; } return s - 6.0; }
static void put(double v) { if (v > 32767) v = 32767; if (v < -32768) v = -32768; buf[nbuf++] = (int16_t)v; }
static void silence(int ms, double nAmp) { for (int i = 0; i < ms * FS / 1000; i++) put(noise() * nAmp); }
static void tone2(double f1, double a1, double f2, double a2, int ms, double nAmp) {
	static double ph1, ph2;
	for (int i = 0; i < ms * FS / 1000; i++) { ph1 += 2 * M_PI * f1 / FS; ph2 += 2 * M_PI * f2 / FS; put(a1 * sin(ph1) + a2 * sin(ph2) + noise() * nAmp); } }

static void dtmfSeq(const char *s, int toneMs, int gapMs, double amp, double twistDb, double nAmp) {
	static const char *keys = "123A456B789C*0#D";
	static const double r[4] = {697, 770, 852, 941}, c[4] = {1209, 1336, 1477, 1633};
	for (; *s; s++) {
		if (*s == ' ') { silence(2000, nAmp); continue; }
		int k = (int)(strchr(keys, *s) - keys);
		tone2(r[k / 4], amp, c[k % 4], amp * pow(10, twistDb / 20), toneMs, nAmp);
		silence(gapMs, nAmp);
	} }

static const char *morse(char ch) {
	static const char *t[][2] = {{"A",".-"},{"B","-..."},{"C","-.-."},{"D","-.."},{"E","."},{"F","..-."},{"G","--."},{"H","...."},{"I",".."},{"J",".---"},{"K","-.-"},{"L",".-.."},{"M","--"},{"N","-."},{"O","---"},{"P",".--."},{"Q","--.-"},{"R",".-."},{"S","..."},{"T","-"},{"U","..-"},{"V","...-"},{"W",".--"},{"X","-..-"},{"Y","-.--"},{"Z","--.."},{"0","-----"},{"1",".----"},{"2","..---"},{"3","...--"},{"4","....-"},{"5","....."},{"6","-...."},{"7","--..."},{"8","---.."},{"9","----."},{"/","-..-."}};
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) if (t[i][0][0] == ch) return t[i][1];
	return ""; }
static void cwSeq(const char *s, int wpm, double f, double amp, double nAmp) {
	int dot = 1200 / wpm;
	for (; *s; s++) {
		if (*s == ' ') { silence(dot * 4, nAmp); continue; } // + 3 after the previous char = 7
		for (const char *p = morse(*s); *p; p++) { tone2(f, amp, 0, 0, (*p == '-') ? 3 * dot : dot, nAmp); silence(dot, nAmp); }
		silence(dot * 2, nAmp);
	} }

static int fails;
static void runDtmf(const char *name, const char *seq, int toneMs, int gapMs, double amp, double twist, double nAmp, const char *expect) {
	nbuf = 0; nout = 0; out[0] = 0;
	silence(300, nAmp); dtmfSeq(seq, toneMs, gapMs, amp, twist, nAmp); silence(2000, nAmp);
	dtmfRxInit(cb);
	for (int i = 0; i < nbuf; i += 160) dtmfRxProcess(buf + i, (nbuf - i) < 160 ? (nbuf - i) : 160, 1);
	int ok = strcmp(out, expect) == 0;
	printf("%-4s dtmf %-26s got '%s'\n", ok ? "OK" : "FAIL", name, out); if (!ok) fails++; }
static void runCw(const char *name, const char *text, int wpm, double f, double amp, double nAmp, const char *expect) {
	nbuf = 0; nout = 0; out[0] = 0;
	silence(1000, nAmp); cwSeq(text, wpm, f, amp, nAmp); silence(1500, nAmp);
	cwRxInit(cb);
	for (int i = 0; i < nbuf; i += 160) cwRxProcess(buf + i, (nbuf - i) < 160 ? (nbuf - i) : 160, 1);
	int ok = (expect == NULL) ? (nout == 0) : (strstr(out, expect) != NULL);
	printf("%-4s cw   %-26s got '%s' (%u wpm %u Hz)\n", ok ? "OK" : "FAIL", name, out, cwRxGetWpm(), cwRxGetToneHz()); if (!ok) fails++; }

int main(void) {
	runDtmf("clean", "123A456B789C*0#D", 80, 60, 4000, 0, 50, "123A456B789C*0#D ");
	runDtmf("fast 70/50 ms", "5551234", 70, 50, 4000, 0, 50, "5551234 ");
	runDtmf("twist -6dB", "908", 80, 60, 4000, -6, 50, "908 ");
	runDtmf("noisy (SNR ~10dB)", "4711", 80, 60, 3000, 0, 700, "4711 ");
	runDtmf("two sequences", "12 34", 80, 60, 4000, 0, 50, "12 34 ");
	runDtmf("weak", "0", 100, 60, 300, 0, 10, "0 ");
	runDtmf("noise only", "", 80, 60, 0, 0, 3000, "");
	runCw("20wpm 800Hz", "CQ CQ DE SP7TQ K", 20, 800, 5000, 100, "CQ CQ DE SP7TQ K");
	runCw("12wpm 1000Hz", "SR7L", 12, 1000, 5000, 100, "SR7L");
	runCw("30wpm 600Hz", "TEST 123", 30, 600, 5000, 100, "TEST 123");
	runCw("noisy 18wpm", "SP7TQ", 18, 850, 4000, 900, "SP7TQ");
	runCw("noise only", "", 20, 800, 0, 3000, NULL);
	printf(fails ? "FAILED: %d\n" : "ALL PASSED\n", fails);
	return fails; }
