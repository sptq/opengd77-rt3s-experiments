// Host test for aprsText.c (texts typed on the radio: chars, callsigns, canned reply records)
#include <stdio.h>
#include <string.h>
#include "functions/aprsText.h"

static int fails;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void)
{
	char out[APRS_TEXT_MSG_MAX + 1];
	aprsReplyRecord_t rec;

	// characters
	CHECK(aprsTextNormaliseChar('a', false) == 'a');
	CHECK(aprsTextNormaliseChar(' ', false) == ' ');
	CHECK(aprsTextNormaliseChar('}', false) == '}');
	CHECK(aprsTextNormaliseChar('|', false) == 0);
	CHECK(aprsTextNormaliseChar('~', false) == 0);
	CHECK(aprsTextNormaliseChar('{', false) == 0);
	CHECK(aprsTextNormaliseChar('\n', false) == 0);
	CHECK(aprsTextNormaliseChar((char)0x88, false) == 0); // Polish font code, not ASCII
	CHECK(aprsTextNormaliseChar('q', true) == 'Q');
	CHECK(aprsTextNormaliseChar('7', true) == '7');
	CHECK(aprsTextNormaliseChar('-', true) == '-');
	CHECK(aprsTextNormaliseChar('.', true) == 0);
	CHECK(aprsTextNormaliseChar(' ', true) == 0);

	// callsigns
	CHECK(aprsTextCallsignIsValid("SP7TQ"));
	CHECK(aprsTextCallsignIsValid("SP7TQ-9"));
	CHECK(aprsTextCallsignIsValid("SP5ABC-15"));
	CHECK(aprsTextCallsignIsValid("BLN1"));
	CHECK(!aprsTextCallsignIsValid(""));
	CHECK(!aprsTextCallsignIsValid("sp7tq"));
	CHECK(!aprsTextCallsignIsValid("SP7TQ-"));
	CHECK(!aprsTextCallsignIsValid("-9"));
	CHECK(!aprsTextCallsignIsValid("SP7TQ-123"));
	CHECK(!aprsTextCallsignIsValid("SP-7TQ-9"));
	CHECK(!aprsTextCallsignIsValid("SP5ABCD-15"));
	CHECK(!aprsTextCallsignIsValid("SP7 TQ"));

	// texts
	CHECK(aprsTextIsValid("QRV 145.500", APRS_TEXT_MSG_MAX));
	CHECK(!aprsTextIsValid("", APRS_TEXT_MSG_MAX));
	CHECK(!aprsTextIsValid("   ", APRS_TEXT_MSG_MAX));
	CHECK(!aprsTextIsValid("a|b", APRS_TEXT_MSG_MAX));
	CHECK(aprsTextIsValid("0123456789012345678901234567890123456789012345678901234567890123456", APRS_TEXT_MSG_MAX)); // 67
	CHECK(!aprsTextIsValid("01234567890123456789012345678901234567890123456789012345678901234567", APRS_TEXT_MSG_MAX)); // 68
	CHECK(!aprsTextIsValid("123456789012345678901234", 23));

	// defaults
	CHECK(strcmp(aprsTextDefaultReply(0), "QSL") == 0);
	CHECK(strcmp(aprsTextDefaultReply(APRS_REPLY_SLOTS - 1), "QRT") == 0);
	CHECK(strcmp(aprsTextDefaultReply(APRS_REPLY_SLOTS), "") == 0);

	// records: erased flash, junk, edited, back to default
	memset(&rec, 0xFF, sizeof(rec));
	CHECK(!aprsReplyRecordDecode(2, &rec, out) && (strcmp(out, "73") == 0));
	memset(&rec, 'A', sizeof(rec)); // old GPS log data: no magic, no NUL
	CHECK(!aprsReplyRecordDecode(3, &rec, out) && (strcmp(out, "Roger") == 0));
	rec.magic = APRS_REPLY_MAGIC; // magic but no NUL in the text
	CHECK(!aprsReplyRecordDecode(3, &rec, out) && (strcmp(out, "Roger") == 0));

	aprsReplyRecordEncode("At the CP in 10 min", &rec);
	CHECK(rec.magic == APRS_REPLY_MAGIC);
	CHECK(aprsReplyRecordDecode(4, &rec, out) && (strcmp(out, "At the CP in 10 min") == 0));

	aprsReplyRecordEncode("01234567890123456789012345678901234567890123456789012345678901234567890", &rec); // too long: cut to 67
	CHECK(aprsReplyRecordDecode(5, &rec, out) && (strlen(out) == APRS_TEXT_MSG_MAX));

	aprsReplyRecordEncode("", &rec);
	CHECK(!aprsReplyRecordDecode(0, &rec, out) && (strcmp(out, "QSL") == 0));
	aprsReplyRecordEncode(NULL, &rec);
	CHECK(rec.magic == 0xFFFFFFFFUL);

	CHECK(sizeof(aprsReplyRecord_t) == 72);

	printf("%s: %d failure(s)\n", __FILE__, fails);
	return (fails ? 1 : 0);
}
