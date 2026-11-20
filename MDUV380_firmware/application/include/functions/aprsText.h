/*
 * Fork: APRS texts typed on the radio (SP7TEAM firmware, Marcin SP7TQ): character
 * and callsign checks, the default canned replies and the flash record format of
 * the edited ones (stored by functions/fieldData.c). Hardware independent, host
 * tested in tests/aprs_rx/test_text.c.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_APRS_TEXT_H_
#define _OPENGD77_APRS_TEXT_H_

#include <stdint.h>
#include <stdbool.h>

#define APRS_TEXT_MSG_MAX       67U  // APRS 1.0.1 message text
#define APRS_TEXT_CALL_MAX       9U  // addressee field
#define APRS_REPLY_SLOTS        12
#define APRS_REPLY_MAGIC        0x31504C52UL // "RLP1"

// One canned reply in the flash. magic != APRS_REPLY_MAGIC: the default text.
typedef struct
{
	uint32_t magic;
	char     text[APRS_TEXT_MSG_MAX + 1];
} aprsReplyRecord_t;

// Character as stored, or 0 when it is not allowed. Text: printable ASCII without
// '|', '~' and '{' (reserved in APRS messages / comments). Callsign: A-Z, 0-9 and '-',
// lower case is turned to upper case.
char aprsTextNormaliseChar(char c, bool callsign);
// Callsign / addressee: 1..9 chars, letters, digits, at most one '-' followed by 1..2 chars.
bool aprsTextCallsignIsValid(const char *call);
// Not empty, not only spaces, every char allowed, at most maxLen chars.
bool aprsTextIsValid(const char *text, uint32_t maxLen);

const char *aprsTextDefaultReply(int slot);
// Text of a slot from its flash record; returns false (and the default) when not edited.
bool aprsReplyRecordDecode(int slot, const aprsReplyRecord_t *rec, char *out);
// Record for a text; NULL or empty text: the default marker (erased record).
void aprsReplyRecordEncode(const char *text, aprsReplyRecord_t *rec);

#endif
