/*
 * Fork: APRS receive (AFSK 1200) - capture control, frame history, station
 * list and AX.25 / APRS parsing. The demodulator itself is in aprsRxDecoder.c,
 * the parsing (hardware independent) in aprsRxParse.c.
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#ifndef _OPENGD77_APRS_RX_H_
#define _OPENGD77_APRS_RX_H_

#include <stdint.h>
#include <stdbool.h>
#include "functions/aprsRxDecoder.h"

#define APRS_RX_HISTORY_SIZE   8U
#define APRS_RX_STORED_LEN     200U // longer frames are kept truncated (typical APRS frames are < 150 bytes)
#define APRS_RX_CALL_LEN       10U  // "SP5ABC-15" / 9 chars object name + NUL
#define APRS_RX_MAX_PATH        8U
#define APRS_RX_MAX_STATIONS   16U
#define APRS_RX_MSG_TEXT_LEN   68U  // 67 chars max (APRS 1.0.1) + NUL
#define APRS_RX_MSG_ID_LEN     6U   // 5 chars max + NUL
#define APRS_RX_MAX_MESSAGES   5U
#define APRS_RX_TONE_TEXT_LEN  64U  // DTMF / CW decoded text kept for display

typedef enum
{
	APRS_RX_STATE_OFF = 0,
	APRS_RX_STATE_LISTENING,
	APRS_RX_STATE_PAUSED,    // not FM, transmitting, or a sound is playing
	APRS_RX_STATE_NO_CLOCK   // the HR-C6000 does not clock the I2S
} aprsRxState_t;

typedef struct
{
	uint32_t timeMs;
	uint16_t len;            // without FCS, <= APRS_RX_STORED_LEN
	bool     truncated;
	uint8_t  data[APRS_RX_STORED_LEN];
} aprsRxFrame_t;

typedef struct
{
	char           src[APRS_RX_CALL_LEN];
	char           dst[APRS_RX_CALL_LEN];
	char           dstRaw[7];   // 6 raw destination characters (Mic-E)
	char           path[APRS_RX_MAX_PATH][APRS_RX_CALL_LEN + 1]; // + '*' when repeated
	uint8_t        pathCount;
	const uint8_t *info;
	uint16_t       infoLen;
} aprsRxParsed_t;

typedef struct
{
	char    name[APRS_RX_CALL_LEN]; // source callsign, or object / item name
	double  lat;
	double  lon;
	char    symTable;
	char    symCode;
	int16_t courseDeg;  // -1: unknown
	int16_t speedKmh;   // -1: unknown
	bool    killed;     // object / item deleted
} aprsRxPosition_t;

typedef struct
{
	char     name[APRS_RX_CALL_LEN];
	uint32_t lastMs;
	float    lat;
	float    lon;
	bool     hasPos;
	char     symTable;
	char     symCode;
	int16_t  courseDeg;
	int16_t  speedKmh;
	uint16_t count;      // frames heard
} aprsRxStation_t;

typedef struct
{
	char addressee[APRS_RX_CALL_LEN];
	char text[APRS_RX_MSG_TEXT_LEN];
	char msgId[APRS_RX_MSG_ID_LEN];  // empty: no acknowledgement wanted
	bool isAck;                      // text empty, msgId = acknowledged message
	bool isRej;
} aprsRxMessage_t;

typedef enum
{
	APRS_MSG_IN = 0,       // addressed to me
	APRS_MSG_IN_BULLETIN,  // BLNx / ALL / CQ
	APRS_MSG_OUT_PENDING,  // sent by me, waiting for the ack
	APRS_MSG_OUT_ACKED,
	APRS_MSG_OUT_REJECTED,
	APRS_MSG_OUT_FAILED,   // no ack after the retries
	APRS_MSG_OUT_NOT_SENT  // no APRS config / TX not allowed
} aprsRxMsgKind_t;

typedef struct
{
	char     peer[APRS_RX_CALL_LEN]; // from (in) / to (out)
	char     text[APRS_RX_MSG_TEXT_LEN];
	char     msgId[APRS_RX_MSG_ID_LEN];
	uint32_t timeMs;
	uint8_t  kind;                   // aprsRxMsgKind_t
	uint8_t  tries;
	bool     unread;
} aprsRxMsgEntry_t;

typedef struct
{
	aprsRxState_t state;
	uint8_t       levelPercent;  // audio peak over the last second, 0..100
	uint32_t      framesOk;
	uint32_t      crcErrors;
} aprsRxStatus_t;

// Capture runs while the monitor screen or the background mode wants it.
void aprsRxSetMonitor(bool on);
void aprsRxSetBackground(bool on);
bool aprsRxGetBackground(void);
void aprsRxToggleBackground(void); // with a notification
// Main loop. Moves decoded frames to the history / station list.
void aprsRxTick(void);
uint32_t aprsRxGetGeneration(void); // incremented on every new frame
void aprsRxGetStatus(aprsRxStatus_t *status);

uint32_t aprsRxHistoryCount(void);
const aprsRxFrame_t *aprsRxHistoryGet(uint32_t index); // 0 = newest
void aprsRxHistoryClear(void);

uint32_t aprsRxStationCount(void);
const aprsRxStation_t *aprsRxStationGet(uint32_t index); // 0 = last heard
bool aprsRxOwnPosition(double *lat, double *lon);       // GPS fix, else the fixed location from the settings

// Messages (newest first). Incoming ones addressed to me are acknowledged automatically.
uint32_t aprsRxMessageCount(void);
const aprsRxMsgEntry_t *aprsRxMessageGet(uint32_t index);
uint32_t aprsRxMessageUnreadCount(void);
void aprsRxMessagesMarkRead(void);
void aprsRxMessagesClear(void);
void aprsRxSendMessage(const char *to, const char *text); // queued, sent from aprsRxTick(), up to 3 tries
bool aprsRxGetOwnCall(char *buf, uint32_t size);         // false: no APRS config, no TX possible

// DTMF / CW decoded text (oldest .. newest), returns the length; 'serial' changes on new chars
uint32_t aprsRxToneText(bool cw, char *out, uint32_t size, uint32_t *serial);
void aprsRxToneTextClear(void);

// Parsing (aprsRxParse.c)
bool aprsRxParse(const aprsRxFrame_t *frame, aprsRxParsed_t *parsed);
// Full TNC2 monitor line: SRC>DST,PATH*:info (non printable chars as '.')
uint32_t aprsRxFormatTnc2(const aprsRxFrame_t *frame, char *out, uint32_t outSize);
bool aprsRxParsePosition(const aprsRxParsed_t *parsed, aprsRxPosition_t *pos);
bool aprsRxParseMessage(const aprsRxParsed_t *parsed, aprsRxMessage_t *msg);
double aprsRxDistanceKm(double lat1, double lon1, double lat2, double lon2);
double aprsRxBearingDeg(double lat1, double lon1, double lat2, double lon2);
void aprsRxFormatDistance(double km, char *buf, uint32_t size); // "850m", "12.3km", "123km"

#endif
