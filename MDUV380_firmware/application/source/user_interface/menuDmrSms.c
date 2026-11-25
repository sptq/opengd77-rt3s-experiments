/*
 * Fork: DMR SMS inbox (main menu > DMR SMS), by Marcin SP7TQ. The messages received and sent by
 * functions/dmrSms.c (RX and TX paths ported from dondch/OpenGD77-AES256, cleartext only).
 *
 * List, newest first, received and sent mixed: age, peer, the first words. Marks: '*' unread
 * (bold), '>' sent, '!' a sent one that did not go out. UP / DOWN select, GREEN opens the
 * message (wrapped text, marked read), '#' writes a new one, '0' deletes the selected one, a long
 * '0' (then GREEN) deletes all, RED goes back / leaves. In the message UP / DOWN go to the next /
 * previous one, GREEN replies (to the sender, or to the talkgroup of a group message).
 *
 * Writing (HAS_DMR_SMS_TX): the recipient (DMR ID or talkgroup typed on the keypad, LEFT
 * deletes, '*' private / group, '#' the contact of the channel, GREEN next), then the text: one
 * typed with the text editor (user_interface/uiTextEdit.c) or a quick one (the BrandMeister
 * service words WX / RPT / INBOX, then the APRS canned replies of functions/fieldData.c); GREEN
 * sends it as it is, RIGHT edits it first. Sent on the current DMR channel, as a PTT press would
 * transmit: "SENDING..." then "SENT", or the reason it was not.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/sound.h"
#include "functions/dmrSms.h"
#include "user_interface/uiEmcom.h"
#if defined(HAS_DMR_SMS_TX)
#include "functions/trx.h"
#include "functions/aprsText.h"
#include "functions/fieldData.h"
#include "hardware/HR-C6000.h"
#include "user_interface/uiTextEdit.h"
#endif

#if defined(HAS_DMR_SMS)

#define SMS_TITLE      "DMR SMS"
#define ROW_H          (FONT_SIZE_1_HEIGHT + 2)
#define LIST_Y         (MENU_HEADER_HEIGHT + 2)
#define FOOTER_H       20
#define ROWS           ((DISPLAY_SIZE_Y - LIST_Y - FOOTER_H) / ROW_H)
#define EM_ROW_H       11
#define NAME_X         28
#define TEXT_X         82
#define TEXT_CHARS     13      // (160 - 82) / 6
#define WRAP_CHARS_1   26      // 6 px font
#define WRAP_CHARS_3   20      // 8 px font

typedef enum
{
	VIEW_LIST = 0,
	VIEW_READ,
#if defined(HAS_DMR_SMS_TX)
	VIEW_TO,     // recipient
	VIEW_PICK,   // typed / quick text
	VIEW_EDIT,   // text editor
	VIEW_SEND    // SENDING... / the result
#endif
} smsView_t;

static uint32_t selected = 0;
static uint32_t selectedSeq = 0;   // the message selected, found again when the list changes
static uint8_t view = VIEW_LIST;
static bool confirmAll = false;
static bool showDiag = false;    // '*' on the list: receive diagnostics

#if defined(HAS_DMR_SMS_TX)
#define PICK_QUICK         3
#define PICK_ROWS          (1 + PICK_QUICK + APRS_REPLY_SLOTS)
#define RESULT_SHOW_MS     3000U
#define TO_MAX_DIGITS      8

static const char *const quickTexts[PICK_QUICK] = { "WX", "RPT", "INBOX" }; // BrandMeister 262993 / 262994 / 262995

static uint8_t returnView;         // where RED leaves the recipient screen to
static uint8_t pickIndex;
static uint8_t sendResult;         // dmrSmsTxResult_t
static bool    toGroup;
static uint32_t toId;
static uint32_t resultMs;          // when the result was first shown
static char composeText[DMR_SMS_TEXT_MAX + 1]; // kept until a message is sent (a refused one can be edited again)
#endif

static void ageString(uint32_t t, char *buf, size_t size)
{
	uint32_t now = (uint32_t)uiDataGlobal.dateTimeSecs;
	uint32_t s;

	if ((t == 0U) || (now < t))
	{
		snprintf(buf, size, "--");
		return;
	}

	s = (now - t);
	if (s < 60U)
	{
		snprintf(buf, size, "%us", (unsigned)s);
	}
	else if (s < 3600U)
	{
		snprintf(buf, size, "%um", (unsigned)(s / 60U));
	}
	else if (s < 86400U)
	{
		snprintf(buf, size, "%uh", (unsigned)(s / 3600U));
	}
	else
	{
		snprintf(buf, size, "%ud", (unsigned)(s / 86400U));
	}
}

static void timeString(uint32_t t, char *buf, size_t size)
{
	struct tm tmv;
	time_t_custom secs = t;

	if (t == 0U)
	{
		snprintf(buf, size, "no clock");
		return;
	}
	gmtime_r_Custom(&secs, &tmv);
	snprintf(buf, size, "%02d-%02d %02d:%02dZ", (tmv.tm_mon + 1), tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
}

// "TG 260" for a group message, else "PRIV"; "OUT ..." for one we sent
static void destString(const dmrSmsMessage_t *m, char *buf, size_t size)
{
	const char *dir = (DMR_SMS_IS_OUTGOING(m->flags) ? "OUT " : "");

	if (m->flags & DMR_SMS_FLAG_GROUP)
	{
		snprintf(buf, size, "%sTG %u", dir, (unsigned)(m->dst & 0x00FFFFFFU));
	}
	else
	{
		snprintf(buf, size, "%sPRIV", dir);
	}
}

// The other end: the sender, or for a message we sent its recipient ("TG 260" for a group one)
static void peerString(const dmrSmsMessage_t *m, char *buf, size_t size)
{
	if (DMR_SMS_IS_OUTGOING(m->flags) == false)
	{
		dmrSmsPeerName(m->src, buf, size);
	}
	else if (m->flags & DMR_SMS_FLAG_GROUP)
	{
		snprintf(buf, size, "TG %u", (unsigned)(m->dst & 0x00FFFFFFU));
	}
	else
	{
		dmrSmsPeerName(m->dst, buf, size);
	}
}

// Word wrap at x 2 from y; returns the number of lines (drawn when 'draw')
static uint32_t wrapText(const char *str, uint32_t perLine, uint32_t maxLines, int16_t y, int16_t lineH, ucFont_t font, bool draw)
{
	char line[WRAP_CHARS_1 + 1];
	uint32_t lines = 0U;

	while (*str)
	{
		uint32_t len = strlen(str);
		uint32_t cut = len;

		if (len > perLine)
		{
			cut = perLine;
			while ((cut > 0U) && (str[cut] != ' '))
			{
				cut--;
			}
			if (cut == 0U)
			{
				cut = perLine;
			}
		}

		if (draw && (lines < maxLines))
		{
			memcpy(line, str, cut);
			line[cut] = 0;
			displayPrintCore(2, (int16_t)(y + (lines * lineH)), line, font, TEXT_ALIGN_LEFT, false);
		}
		lines++;
		str += cut;
		while (*str == ' ')
		{
			str++;
		}
	}
	return lines;
}

static bool emcom(void)
{
#if defined(HAS_EMCOM_UI)
	return emcomStyleEnabled();
#else
	return false;
#endif
}

static void drawEmpty(int16_t y)
{
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintCentered((y + 16), "No messages", FONT_SIZE_3);
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCentered((y + 40), "Private DMR SMS to your", FONT_SIZE_1);
	displayPrintCentered((y + 50), "ID and group SMS show here", FONT_SIZE_1);
	displayThemeResetToDefault();
}

// Classic style footer: two lines on the header colour
static void classicFooter(const char *line1, const char *line2, themeItem_t line1Colour)
{
	int16_t fy = (DISPLAY_SIZE_Y - FOOTER_H);

	displayThemeApply(THEME_ITEM_BG_HEADER_TEXT, THEME_ITEM_BG);
	displayFillRect(0, fy, DISPLAY_SIZE_X, FOOTER_H, false);
	displayThemeApply(line1Colour, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 1), line1, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
	displayThemeApply(THEME_ITEM_FG_HEADER_TEXT, THEME_ITEM_BG_HEADER_TEXT);
	displayPrintCore(3, (fy + 11), line2, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayThemeResetToDefault();
}

// The key hints: one bar in the EMCOM style, two lines in the classic one
static void footer(const char *emKeys, const char *line1, const char *line2)
{
#if defined(HAS_EMCOM_UI)
	if (emcom())
	{
		emcomUiKeyBar(emKeys);
		return;
	}
#endif
	classicFooter(line1, line2, THEME_ITEM_FG_HEADER_TEXT);
}

static void drawDiag(void)
{
	char buf[32];
	volatile dmrSmsDiag_t *d = &dmrSmsDiag;
	int16_t y = (MENU_HEADER_HEIGHT + 2);

	displayClearBuf();
	menuDisplayTitle("SMS RX diag");
	snprintf(buf, sizeof(buf), "DATA %u CRCX %u", d->dataInt, d->crcBad);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "HDR %u R12 %u R34 %u", d->hdr, d->r12, d->r34);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "OTHER %u SKIP %u", d->other, d->skipped);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "PDU %u DECX %u", d->pduDone, d->decodeFail);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "NOTUS %u DUP %u OK %u", d->notUs, d->dup, d->stored);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "INFO %u 90:%02X 51:%02X", d->infoInt, d->last90, d->last51);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 11;
	displayPrintCore(2, y, "LAST HEADER:", FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "%02X%02X%02X%02X %02X%02X%02X%02X", d->lastHdr[0], d->lastHdr[1], d->lastHdr[2], d->lastHdr[3],
			d->lastHdr[4], d->lastHdr[5], d->lastHdr[6], d->lastHdr[7]);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false); y += 9;
	snprintf(buf, sizeof(buf), "%02X%02X%02X%02X", d->lastHdr[8], d->lastHdr[9], d->lastHdr[10], d->lastHdr[11]);
	displayPrintCore(2, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayPrintCore(2, (DISPLAY_SIZE_Y - 10), "* BACK  0 RESET", FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayRender();
}

static void drawList(void)
{
	if (showDiag)
	{
		drawDiag();
		return;
	}

	char buf[32];
	char name[SCREEN_LINE_BUFFER_SIZE];
	dmrSmsMessage_t m;
	uint32_t count = dmrSmsCount();
	bool em = emcom();
	int16_t listY = (em ? EMCOM_UI_BODY_Y : LIST_Y);
	int16_t rowH = (em ? EM_ROW_H : ROW_H);
	uint32_t rows = (em ? (uint32_t)((EMCOM_UI_KEYBAR_Y - 1 - EMCOM_UI_BODY_Y) / EM_ROW_H) : ROWS);

	displayClearBuf();
	if (selected >= count)
	{
		selected = ((count > 0U) ? (count - 1U) : 0U);
	}
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)((count > 0U) ? (selected + 1U) : 0U), (unsigned)count);

#if defined(HAS_EMCOM_UI)
	if (em)
	{
		emcomUiTitle(SMS_TITLE, buf);
		emcomUiStrip();
		if ((count > 0U) && dmrSmsGet(selected, &m))
		{
			char dest[16];

			timeString(m.time, buf, sizeof(buf));
			emcomUiText(3, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
			destString(&m, dest, sizeof(dest));
			emcomUiChip((DISPLAY_SIZE_X - 2), (EMCOM_UI_STRIP_Y + 2), dest,
					((m.flags & DMR_SMS_FLAG_GROUP) ? THEME_ITEM_FG_RX_FREQ : THEME_ITEM_FG_RSSI_BAR), EMCOM_CHIP_SOLID, true);
		}
		else
		{
			emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), "EMPTY", THEME_ITEM_FG_DECORATION, EMCOM_CHIP_OUTLINE, false);
		}
	}
	else
#endif
	{
		menuDisplayTitle(SMS_TITLE);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG_MENU_NAME);
		displayPrintCore((DISPLAY_SIZE_X - 3 - (int16_t)(strlen(buf) * 6)), 4, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		displayThemeResetToDefault();
	}

	if (count == 0U)
	{
		selectedSeq = 0U;
		drawEmpty(listY);
#if defined(HAS_DMR_SMS_TX)
		footer("# NEW  RED EXIT", "# new message", "RED back");
#else
		footer("RED EXIT", "", "RED back");
#endif
		displayRender();
		return;
	}

	uint32_t first = ((selected >= rows) ? (selected - rows + 1U) : 0U);

	for (uint32_t r = 0U; r < rows; r++)
	{
		uint32_t idx = (first + r);
		int16_t y = (int16_t)(listY + (r * rowH));
		bool sel = (idx == selected);

		if ((idx >= count) || (dmrSmsGet(idx, &m) == false))
		{
			break;
		}

		bool outgoing = DMR_SMS_IS_OUTGOING(m.flags);
		bool failed = (outgoing && ((m.flags & DMR_SMS_FLAG_UNSENT) != 0U));
		bool unread = ((outgoing == false) && ((m.flags & DMR_SMS_FLAG_UNREAD) != 0U));
		ucFont_t font = (unread ? FONT_SIZE_1_BOLD : FONT_SIZE_1);

		if (sel)
		{
			selectedSeq = m.seq;
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect(0, (y - (em ? 2 : 1)), DISPLAY_SIZE_X, rowH, false);
		}

		// em style selected row: text in the background colour on the selection bar; classic: inverted print
		themeItem_t fg = ((sel && em) ? THEME_ITEM_BG : THEME_ITEM_FG_DEFAULT);
		themeItem_t dim = ((sel && em) ? THEME_ITEM_BG : THEME_ITEM_FG_DECORATION);
		themeItem_t markColour = (failed ? THEME_ITEM_FG_TX_COUNTER : ((unread || outgoing) ? THEME_ITEM_FG_RSSI_BAR : dim));
		bool invert = (sel && (em == false));

		ageString(m.time, buf, sizeof(buf));
		displayThemeApply(((sel && em) ? THEME_ITEM_BG : markColour), THEME_ITEM_BG);
		displayPrintCore(2, y, (failed ? "!" : (outgoing ? ">" : (unread ? "*" : " "))), FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, invert);
		displayThemeApply(dim, THEME_ITEM_BG);
		displayPrintCore(8, y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, invert);

		peerString(&m, name, sizeof(name));
		snprintf(buf, sizeof(buf), "%.8s", name);
		displayThemeApply(fg, THEME_ITEM_BG);
		displayPrintCore(NAME_X, y, buf, font, TEXT_ALIGN_LEFT, invert);

		snprintf(buf, sizeof(buf), "%.*s", TEXT_CHARS, m.text);
		displayPrintCore(TEXT_X, y, buf, font, TEXT_ALIGN_LEFT, invert);
		displayThemeResetToDefault();
	}

#if defined(HAS_EMCOM_UI)
	if (em)
	{
		if (confirmAll)
		{
			emcomUiConfirmBar("DELETE ALL?");
		}
		else
		{
#if defined(HAS_DMR_SMS_TX)
			emcomUiKeyBar("GRN READ  # NEW  0 DEL");
#else
			emcomUiKeyBar("GRN READ  0 DEL  RED EXIT");
#endif
		}
		displayRender();
		return;
	}
#endif

	if (confirmAll)
	{
		classicFooter("DELETE ALL MESSAGES?", "GREEN yes, other key no", THEME_ITEM_FG_TX_COUNTER);
	}
	else
	{
#if defined(HAS_DMR_SMS_TX)
		classicFooter("GREEN read # new 0 del", "long 0 delete all", THEME_ITEM_FG_HEADER_TEXT);
#else
		classicFooter("GREEN read  0 delete", "long 0 delete all", THEME_ITEM_FG_HEADER_TEXT);
#endif
	}
	displayRender();
}

static void drawMessage(void)
{
	char buf[32];
	char name[SCREEN_LINE_BUFFER_SIZE];
	char dest[16];
	dmrSmsMessage_t m;
	bool em = emcom();

	if (dmrSmsGet(selected, &m) == false)
	{
		view = VIEW_LIST;
		drawList();
		return;
	}

	bool outgoing = DMR_SMS_IS_OUTGOING(m.flags);

	selectedSeq = m.seq;
	displayClearBuf();
	peerString(&m, name, sizeof(name));
	if (outgoing)
	{
		char peer[SCREEN_LINE_BUFFER_SIZE];

		snprintf(peer, sizeof(peer), "%s", name);
		snprintf(name, sizeof(name), "%s%.14s", ((m.flags & DMR_SMS_FLAG_UNSENT) ? "! " : "> "), peer);
	}
	destString(&m, dest, sizeof(dest));
	timeString(m.time, buf, sizeof(buf));

	int16_t bodyY;
	int16_t bodyBottom;

#if defined(HAS_EMCOM_UI)
	if (em)
	{
		emcomUiTitle(name, dest);
		emcomUiStrip();
		emcomUiText(3, (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, false);
		if (outgoing && (m.flags & DMR_SMS_FLAG_UNSENT))
		{
			snprintf(buf, sizeof(buf), "NOT SENT");
		}
		else
		{
			snprintf(buf, sizeof(buf), "%u", (unsigned)((outgoing ? m.dst : m.src) & 0x00FFFFFFU));
		}
		emcomUiText((DISPLAY_SIZE_X - 3), (EMCOM_UI_STRIP_Y + 2), buf, FONT_SIZE_1, THEME_ITEM_FG_HEADER_TEXT, true);
		bodyY = EMCOM_UI_BODY_Y;
		bodyBottom = (EMCOM_UI_KEYBAR_Y - 1);
	}
	else
#endif
	{
		menuDisplayTitle(name);
		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		displayPrintCore(2, LIST_Y, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		displayPrintCore((DISPLAY_SIZE_X - 2 - (int16_t)(strlen(dest) * 6)), LIST_Y, dest, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
		bodyY = (LIST_Y + FONT_SIZE_1_HEIGHT + 4);
		bodyBottom = (DISPLAY_SIZE_Y - FOOTER_H);
	}

	// 8x16 when the whole text fits, else the small font
	uint32_t room3 = (uint32_t)((bodyBottom - bodyY) / FONT_SIZE_3_HEIGHT);
	uint32_t room1 = (uint32_t)((bodyBottom - bodyY) / (FONT_SIZE_1_HEIGHT + 1));
	bool big = (wrapText(m.text, WRAP_CHARS_3, 0U, 0, 0, FONT_SIZE_3, false) <= room3);

	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	if (big)
	{
		wrapText(m.text, WRAP_CHARS_3, room3, bodyY, FONT_SIZE_3_HEIGHT, FONT_SIZE_3, true);
	}
	else
	{
		wrapText(m.text, WRAP_CHARS_1, room1, bodyY, (FONT_SIZE_1_HEIGHT + 1), FONT_SIZE_1, true);
	}
	displayThemeResetToDefault();

#if defined(HAS_EMCOM_UI)
	if (em)
	{
#if defined(HAS_DMR_SMS_TX)
		emcomUiKeyBar("GRN REPLY 0 DEL RED BACK");
#else
		emcomUiKeyBar("UP/DN  0 DEL  RED BACK");
#endif
		displayRender();
		return;
	}
#endif
	snprintf(buf, sizeof(buf), "%u/%u", (unsigned)(selected + 1U), (unsigned)dmrSmsCount());
#if defined(HAS_DMR_SMS_TX)
	classicFooter(buf, "GREEN reply 0 del RED back", THEME_ITEM_FG_HEADER_TEXT);
#else
	classicFooter(buf, "UP/DN next  0 delete  RED back", THEME_ITEM_FG_HEADER_TEXT);
#endif
	displayRender();
}

#if defined(HAS_DMR_SMS_TX)
// Title, and the status strip (EMCOM style) / a first line (classic) with 'info'; returns the body y
static int16_t composeFrame(const char *title, const char *info, themeItem_t infoColour)
{
	displayClearBuf();
#if defined(HAS_EMCOM_UI)
	if (emcom())
	{
		emcomUiTitle(title, NULL);
		emcomUiStrip();
		if (info)
		{
			emcomUiChip(2, (EMCOM_UI_STRIP_Y + 2), info, infoColour, EMCOM_CHIP_SOLID, false);
		}
		return EMCOM_UI_BODY_Y;
	}
#endif
	menuDisplayTitle(title);
	if (info)
	{
		displayThemeApply(infoColour, THEME_ITEM_BG);
		displayPrintCore(2, LIST_Y, info, FONT_SIZE_1_BOLD, TEXT_ALIGN_LEFT, false);
		displayThemeResetToDefault();
	}
	return (int16_t)(LIST_Y + FONT_SIZE_1_HEIGHT + 3);
}

// "TO SP5ABC" / "TO TG 260"
static void recipientString(char *buf, size_t size)
{
	char name[SCREEN_LINE_BUFFER_SIZE];

	if (toGroup)
	{
		snprintf(buf, size, "TO TG %u", (unsigned)toId);
	}
	else
	{
		dmrSmsPeerName(toId, name, sizeof(name));
		snprintf(buf, size, "TO %s", name);
	}
}

static void drawTo(void)
{
	char buf[SCREEN_LINE_BUFFER_SIZE];
	int16_t y = composeFrame("SMS to", (toGroup ? "GROUP (TG)" : "PRIVATE ID"), (toGroup ? THEME_ITEM_FG_RX_FREQ : THEME_ITEM_FG_RSSI_BAR));

	if (toId > 0U)
	{
		snprintf(buf, sizeof(buf), "%u_", (unsigned)toId);
	}
	else
	{
		snprintf(buf, sizeof(buf), "_");
	}
	displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
	displayPrintCentered((uint16_t)(y + 4), buf, FONT_SIZE_3);

	displayThemeApply(THEME_ITEM_FG_RX_FREQ, THEME_ITEM_BG);
	if ((toId > 0U) && (toGroup == false))
	{
		dmrSmsPeerName(toId, buf, sizeof(buf));
		displayPrintCentered((uint16_t)(y + 24), buf, FONT_SIZE_1);
	}
	else if (toId > 0U)
	{
		displayPrintCentered((uint16_t)(y + 24), "talkgroup", FONT_SIZE_1);
	}
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayPrintCentered((uint16_t)(y + 36), "BM: 262993 WX  262994 RPT", FONT_SIZE_1);
	displayPrintCentered((uint16_t)(y + 45), "262995 SMSC (INBOX)", FONT_SIZE_1);
	displayThemeResetToDefault();

	footer("GRN OK *P/G #CH RED BACK", "GREEN next  * priv/group", "# channel contact RED back");
	displayRender();
}

// Row text of the text choice (0: the typed text)
static void pickText(uint32_t i, char *text)
{
	if (i == 0U)
	{
		snprintf(text, (DMR_SMS_TEXT_MAX + 1U), "%s", composeText);
	}
	else if (i <= PICK_QUICK)
	{
		snprintf(text, (DMR_SMS_TEXT_MAX + 1U), "%s", quickTexts[i - 1U]);
	}
	else
	{
		fieldAprsReplyGet((int)(i - 1U - PICK_QUICK), text); // APRS_TEXT_MSG_MAX + 1 bytes
	}
}

static void drawPick(void)
{
	char buf[32];
	char text[DMR_SMS_TEXT_MAX + 1];
	bool em = emcom();

	recipientString(buf, sizeof(buf));
	int16_t y = composeFrame("SMS text", buf, THEME_ITEM_FG_RX_FREQ);
	int16_t rowH = (em ? EM_ROW_H : ROW_H);
	int16_t bottom = (em ? (EMCOM_UI_KEYBAR_Y - 1) : (DISPLAY_SIZE_Y - FOOTER_H));
	uint32_t rows = (uint32_t)((bottom - y) / rowH);
	uint32_t first = ((pickIndex >= rows) ? (pickIndex - rows + 1U) : 0U);

	for (uint32_t r = 0U; (r < rows) && ((first + r) < PICK_ROWS); r++)
	{
		uint32_t i = (first + r);
		int16_t ry = (int16_t)(y + (r * rowH));
		bool sel = (i == pickIndex);

		pickText(i, text);
		if (i == 0U)
		{
			snprintf(buf, sizeof(buf), "%s%.20s", "> ", ((text[0] != 0) ? text : "Type a message..."));
		}
		else
		{
			snprintf(buf, sizeof(buf), "%.25s", text);
		}

		if (sel)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRect(0, (ry - 1), DISPLAY_SIZE_X, rowH, false);
		}
		displayThemeApply((((i > 0U) && (i <= PICK_QUICK)) ? THEME_ITEM_FG_RX_FREQ : THEME_ITEM_FG_DEFAULT), THEME_ITEM_BG);
		if (sel && em)
		{
			displayThemeApply(THEME_ITEM_BG, THEME_ITEM_BG);
		}
		displayPrintCore(4, ry, buf, FONT_SIZE_1, TEXT_ALIGN_LEFT, (sel && (em == false)));
		displayThemeResetToDefault();
	}

	footer("GRN SEND >EDIT *TO RED", "GREEN send  RIGHT edit", "* recipient  RED back");
	displayRender();
}

static void drawSend(void)
{
	char buf[32];
	dmrSmsTxResult_t r = (dmrSmsTxResult_t)sendResult;
	const char *big;
	themeItem_t colour;

	if (r == DMR_SMS_TX_SENDING)
	{
		big = "SENDING...";
		colour = THEME_ITEM_FG_TX_COUNTER;
	}
	else if (r == DMR_SMS_TX_OK)
	{
		big = "SENT";
		colour = THEME_ITEM_FG_RSSI_BAR;
	}
	else if ((r == DMR_SMS_TX_NO_REPEATER) || (r == DMR_SMS_TX_TIMEOUT))
	{
		big = "FAILED";
		colour = THEME_ITEM_FG_TX_COUNTER;
	}
	else
	{
		big = (((r == DMR_SMS_TX_BUSY) || (r == DMR_SMS_TX_BAD_TEXT)) ? "NOT SENT" : "NOT ALLOWED");
		colour = THEME_ITEM_FG_TX_COUNTER;
	}

	recipientString(buf, sizeof(buf));
	int16_t y = composeFrame(SMS_TITLE, buf, THEME_ITEM_FG_RX_FREQ);

	displayThemeApply(colour, THEME_ITEM_BG);
	displayPrintCentered((uint16_t)(y + 10), big, FONT_SIZE_3);
	if ((r != DMR_SMS_TX_SENDING) && (r != DMR_SMS_TX_OK))
	{
		displayThemeApply(THEME_ITEM_FG_DEFAULT, THEME_ITEM_BG);
		displayPrintCentered((uint16_t)(y + 32), dmrSmsTxResultText(r), FONT_SIZE_1);
	}
	displayThemeResetToDefault();

	if (r == DMR_SMS_TX_SENDING)
	{
		footer("TRANSMITTING", "transmitting", "on the current channel");
	}
	else
	{
		footer("ANY KEY", "any key", "");
	}
	displayRender();
}
#endif

static void updateScreen(void)
{
	switch (view)
	{
		case VIEW_READ:
			dmrSmsMarkRead(selected);
			drawMessage();
			break;
#if defined(HAS_DMR_SMS_TX)
		case VIEW_TO:
			drawTo();
			break;
		case VIEW_PICK:
			drawPick();
			break;
		case VIEW_EDIT:
			{
				char label[32];

				recipientString(label, sizeof(label));
				uiTextEditDraw(SMS_TITLE, label);
			}
			break;
		case VIEW_SEND:
			drawSend();
			break;
#endif
		default:
			drawList();
			break;
	}
}

#if defined(HAS_DMR_SMS_TX)
static void composeStart(uint32_t id, bool group, uint8_t from)
{
	toId = (id & 0x00FFFFFFU);
	toGroup = group;
	returnView = from;
	pickIndex = 0U;
	view = VIEW_TO;
}

// SENDING... is on the screen while the message is filed and the call keyed
static void sendText(const char *text)
{
	sendResult = DMR_SMS_TX_SENDING;
	resultMs = 0U;
	view = VIEW_SEND;
	drawSend();

	sendResult = dmrSmsSend(text, toId, toGroup);
	if (sendResult == DMR_SMS_TX_OK)
	{
		sendResult = DMR_SMS_TX_SENDING; // keyed: dmrSmsTxResult() tells the end
		composeText[0] = 0;
	}
	else
	{
		soundSetMelody(MELODY_ERROR_BEEP);
		resultMs = (ticksGetMillis() | 1U);
	}
	drawSend();
}

static void sendDone(void)
{
	bool keyed = ((sendResult == DMR_SMS_TX_OK) || (sendResult == DMR_SMS_TX_NO_REPEATER) || (sendResult == DMR_SMS_TX_TIMEOUT));

	if (keyed)
	{
		view = VIEW_LIST; // the message is the newest one
		selected = 0U;
	}
	else
	{
		view = VIEW_PICK; // refused before it was filed: the text can be sent again or edited
	}
	updateScreen();
}

static void handleCompose(uiEvent_t *ev)
{
	if (view == VIEW_EDIT)
	{
		textEditResult_t res = uiTextEditHandleEvent(ev);

		if (res == TEXT_EDIT_DONE)
		{
			uint32_t len = strlen(composeText);

			while ((len > 0U) && (composeText[len - 1U] == ' '))
			{
				composeText[--len] = 0;
			}
			if (len == 0U)
			{
				soundSetMelody(MELODY_ERROR_BEEP);
				uiTextEditStart(composeText, DMR_SMS_TEXT_MAX, false);
			}
			else
			{
				sendText(composeText);
				return;
			}
		}
		else if (res == TEXT_EDIT_CANCEL)
		{
			view = VIEW_PICK;
		}
		else if (res == TEXT_EDIT_NONE)
		{
			return;
		}
		updateScreen();
		return;
	}

	if ((ev->events & KEY_EVENT) == 0)
	{
		return;
	}

	if (view == VIEW_SEND)
	{
		if ((sendResult != DMR_SMS_TX_SENDING) && (ev->keys.event & KEY_MOD_UP))
		{
			sendDone();
		}
		return;
	}

	if (view == VIEW_TO)
	{
		if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2) == false))
		{
			uint32_t d = (uint32_t)(ev->keys.key - '0');
			uint32_t v = ((toId * 10U) + d);

			if ((v <= 0x00FFFFFFU) && (toId < 10000000U))
			{
				toId = v;
			}
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
		{
			toId /= 10U;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
		{
			toGroup = !toGroup;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
		{
			// the contact / talkgroup the channel would call
			toId = (trxTalkGroupOrPcId & 0x00FFFFFFU);
			toGroup = ((trxTalkGroupOrPcId >> 24) != PC_CALL_FLAG);
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			if (toId == 0U)
			{
				soundSetMelody(MELODY_ERROR_BEEP);
				return;
			}
			view = VIEW_PICK;
		}
		else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			view = returnView;
		}
		else
		{
			return;
		}
		updateScreen();
		return;
	}

	// VIEW_PICK
	if (KEYCHECK_PRESS(ev->keys, KEY_UP))
	{
		pickIndex = ((pickIndex > 0U) ? (pickIndex - 1U) : (PICK_ROWS - 1U));
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
	{
		pickIndex = (((pickIndex + 1U) < PICK_ROWS) ? (pickIndex + 1U) : 0U);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		char text[DMR_SMS_TEXT_MAX + 1];

		pickText(pickIndex, text);
		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (text[0] != 0))
		{
			snprintf(composeText, sizeof(composeText), "%s", text);
			sendText(composeText);
			return;
		}
		snprintf(composeText, sizeof(composeText), "%s", text); // RIGHT, or GREEN on an empty typed text
		uiTextEditStart(composeText, DMR_SMS_TEXT_MAX, false);
		view = VIEW_EDIT;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		view = VIEW_TO;
	}
	else
	{
		return;
	}
	updateScreen();
}
#endif

// The list changed (a message came, went or was sent): keep the same one selected
static void followSelection(void)
{
	dmrSmsMessage_t m;
	uint32_t count = dmrSmsCount();

	for (uint32_t i = 0U; (selectedSeq != 0U) && (i < count); i++)
	{
		if (dmrSmsGet(i, &m) && (m.seq == selectedSeq))
		{
			selected = i;
			return;
		}
	}
}

menuStatus_t menuDmrSms(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t lastSerial = 0U;

	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		selected = 0U;
		selectedSeq = 0U;
		view = VIEW_LIST;
		confirmAll = false;
#if defined(HAS_DMR_SMS_TX)
		uiTextEditStop();
		composeText[0] = 0;
#endif
		lastSerial = dmrSmsSerial();
		updateScreen();
		return MENU_STATUS_SUCCESS;
	}

#if defined(HAS_DMR_SMS_TX)
	if (view == VIEW_SEND)
	{
		if ((sendResult == DMR_SMS_TX_SENDING) && (dmrSmsTxResult() != DMR_SMS_TX_SENDING))
		{
			sendResult = dmrSmsTxResult();
			resultMs = (ticksGetMillis() | 1U);
			soundSetMelody((sendResult == DMR_SMS_TX_OK) ? MELODY_ACK_BEEP : MELODY_ERROR_BEEP);
			drawSend();
		}
		else if ((resultMs != 0U) && ((ticksGetMillis() - resultMs) > RESULT_SHOW_MS))
		{
			sendDone();
		}
	}
#endif

	if (ev->hasEvent)
	{
		if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
		{
			updateScreen();
		}
#if defined(HAS_DMR_SMS_TX)
		else if (view >= VIEW_TO)
		{
			handleCompose(ev);
		}
#endif
		else if (ev->events & KEY_EVENT)
		{
			if (confirmAll)
			{
				// decided on a key release (not the release of the long '0' that asked)
				if ((ev->keys.event & KEY_MOD_UP) && ((ev->keys.key != KEY_0) || ((ev->keys.event & KEY_MOD_LONG) == 0)))
				{
					if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
					{
						dmrSmsDeleteAll();
						selected = 0U;
						soundSetMelody(MELODY_ACK_BEEP);
					}
					confirmAll = false;
					lastSerial = dmrSmsSerial();
					updateScreen();
				}
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				if (view == VIEW_READ)
				{
					view = VIEW_LIST;
					updateScreen();
				}
				else
				{
					menuSystemPopPreviousMenu();
					return MENU_STATUS_SUCCESS;
				}
			}
			else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && BUTTONCHECK_DOWN(ev, BUTTON_SK2))
			{
				saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
			}
#if defined(HAS_DMR_SMS_TX)
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH) && (view == VIEW_LIST))
			{
				composeText[0] = 0;
				composeStart(0U, false, VIEW_LIST);
				updateScreen();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) && (view == VIEW_READ))
			{
				dmrSmsMessage_t m;

				if (dmrSmsGet(selected, &m))
				{
					bool group = ((m.flags & DMR_SMS_FLAG_GROUP) != 0U);

					// to the sender, or to the talkgroup / the recipient of a message we sent
					composeText[0] = 0;
					composeStart(((group || DMR_SMS_IS_OUTGOING(m.flags)) ? m.dst : m.src), group, VIEW_READ);
				}
				updateScreen();
			}
#endif
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				if (view == VIEW_READ)
				{
					view = VIEW_LIST;
				}
				else if (dmrSmsCount() > 0U)
				{
					view = VIEW_READ;
				}
				updateScreen();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) && (view == VIEW_LIST))
			{
				showDiag = !showDiag;
				drawList();
			}
			else if (showDiag && (view == VIEW_LIST) && KEYCHECK_SHORTUP(ev->keys, KEY_0))
			{
				memset((void *)&dmrSmsDiag, 0, sizeof(dmrSmsDiag));
				drawList();
			}
			else if (KEYCHECK_LONGDOWN(ev->keys, KEY_0) && (view == VIEW_LIST) && (dmrSmsCount() > 0U))
			{
				confirmAll = true;
				updateScreen();
			}
			else if (KEYCHECK_SHORTUP(ev->keys, KEY_0) && (dmrSmsCount() > 0U))
			{
				dmrSmsDelete(selected);
				lastSerial = dmrSmsSerial();
				soundSetMelody(MELODY_ACK_BEEP);
				if (dmrSmsCount() == 0U)
				{
					view = VIEW_LIST;
				}
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_DOWN))
			{
				if ((selected + 1U) < dmrSmsCount())
				{
					selected++;
				}
				updateScreen();
			}
			else if (KEYCHECK_PRESS(ev->keys, KEY_UP))
			{
				if (selected > 0U)
				{
					selected--;
				}
				updateScreen();
			}
		}
	}

	// The list changed while the screen is shown: keep the same message selected
	uint32_t s = dmrSmsSerial();
	if (s != lastSerial)
	{
		lastSerial = s;
		if (view <= VIEW_READ)
		{
			if ((view == VIEW_READ) || (selected != 0U))
			{
				followSelection(); // the top of the list stays on the newest message
			}
			updateScreen();
		}
	}

	return MENU_STATUS_SUCCESS;
}

#else
menuStatus_t menuDmrSms(uiEvent_t *ev, bool isFirstRun)
{
	UNUSED_PARAMETER(ev);
	UNUSED_PARAMETER(isFirstRun);
	menuSystemPopPreviousMenu();
	return MENU_STATUS_SUCCESS;
}
#endif
