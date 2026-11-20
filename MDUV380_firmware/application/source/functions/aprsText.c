/*
 * Fork: APRS texts typed on the radio (see functions/aprsText.h).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include "functions/aprsText.h"

static const char *const defaultReplies[APRS_REPLY_SLOTS] =
{
		"QSL", "OK", "73", "Roger", "Thanks!", "Yes", "No", "Call me", "QRV?", "QRX 5 min", "On my way", "QRT"
};

char aprsTextNormaliseChar(char c, bool callsign)
{
	if (callsign)
	{
		if ((c >= 'a') && (c <= 'z'))
		{
			return (char)(c - ('a' - 'A'));
		}
		return ((((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '-')) ? c : 0);
	}

	if ((c < ' ') || (c > '}') || (c == '|') || (c == '{')) // '~' (0x7E) is above '}'
	{
		return 0;
	}
	return c;
}

bool aprsTextCallsignIsValid(const char *call)
{
	uint32_t len = strlen(call);
	const char *dash = strchr(call, '-');

	if ((len == 0U) || (len > APRS_TEXT_CALL_MAX))
	{
		return false;
	}

	for (uint32_t i = 0U; i < len; i++)
	{
		if ((aprsTextNormaliseChar(call[i], true) != call[i]))
		{
			return false; // lower case or not allowed
		}
	}

	if (dash != NULL)
	{
		uint32_t ssidLen = (len - (uint32_t)(dash - call) - 1U);

		if ((dash == call) || (strchr(dash + 1, '-') != NULL) || (ssidLen == 0U) || (ssidLen > 2U))
		{
			return false;
		}
	}
	return true;
}

bool aprsTextIsValid(const char *text, uint32_t maxLen)
{
	uint32_t len = strlen(text);
	bool hasChar = false;

	if ((len == 0U) || (len > maxLen))
	{
		return false;
	}

	for (uint32_t i = 0U; i < len; i++)
	{
		if (aprsTextNormaliseChar(text[i], false) == 0)
		{
			return false;
		}
		if (text[i] != ' ')
		{
			hasChar = true;
		}
	}
	return hasChar;
}

const char *aprsTextDefaultReply(int slot)
{
	return (((slot >= 0) && (slot < APRS_REPLY_SLOTS)) ? defaultReplies[slot] : "");
}

bool aprsReplyRecordDecode(int slot, const aprsReplyRecord_t *rec, char *out)
{
	if ((rec->magic == APRS_REPLY_MAGIC) && (memchr(rec->text, 0, sizeof(rec->text)) != NULL) &&
			aprsTextIsValid(rec->text, APRS_TEXT_MSG_MAX))
	{
		strcpy(out, rec->text);
		return true;
	}

	strcpy(out, aprsTextDefaultReply(slot));
	return false;
}

void aprsReplyRecordEncode(const char *text, aprsReplyRecord_t *rec)
{
	memset(rec, 0xFF, sizeof(aprsReplyRecord_t));

	if ((text != NULL) && (text[0] != 0))
	{
		size_t n = strlen(text);

		if (n > APRS_TEXT_MSG_MAX)
		{
			n = APRS_TEXT_MSG_MAX;
		}
		rec->magic = APRS_REPLY_MAGIC;
		memset(rec->text, 0, sizeof(rec->text));
		memcpy(rec->text, text, n);
	}
}
