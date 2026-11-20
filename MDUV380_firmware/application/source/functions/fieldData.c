/*
 * Fork: field operations data in the SPI flash (see functions/fieldData.h).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include "functions/fieldData.h"
#include "functions/aprsText.h"

#if defined(HAS_FIELD_TOOLS)

#include "hardware/SPI_Flash.h"

#define SECTOR_SIZE      4096U
#define PAGE_SIZE        256U
#define PACE_ADDRESS     FIELD_DATA_FLASH_ADDRESS
#define PACE_MAGIC       0x45434150UL // "PACE"
#define COVERAGE_MAGIC   0x31564F43UL // "COV1": the coverage sectors were erased by this firmware
#define COVERAGE_ADDRESS (FIELD_DATA_FLASH_ADDRESS + SECTOR_SIZE)
#define REPLIES_ADDRESS  (FIELD_DATA_FLASH_ADDRESS + FIELD_APRS_REPLIES_OFFSET) // sector 0, after the PACE store

typedef struct
{
	uint32_t   magic;
	uint32_t   coverageMagic; // the area may hold old GPS log data until it is erased once
	paceSlot_t slots[PACE_SLOTS];
} paceStore_t;

static uint32_t coverageCount = 0xFFFFFFFFU; // unknown until the first access

__attribute__((section(".ccmram"))) uint32_t fieldToolsScratch[FIELD_SCRATCH_SIZE / sizeof(uint32_t)];

// --- PACE --------------------------------------------------------------------------------
static void paceRead(paceStore_t *store)
{
	SPI_Flash_read(PACE_ADDRESS, (uint8_t *)store, sizeof(paceStore_t));
	if (store->magic != PACE_MAGIC)
	{
		store->magic = PACE_MAGIC;
		store->coverageMagic = 0U;
		for (int i = 0; i < PACE_SLOTS; i++)
		{
			store->slots[i].zoneNumber = -1;
			store->slots[i].channelNumber = 0;
		}
	}
}

bool fieldPaceGet(int slot, paceSlot_t *data)
{
	paceStore_t store;

	if ((slot < 0) || (slot >= PACE_SLOTS))
	{
		return false;
	}

	paceRead(&store);
	*data = store.slots[slot];
	return ((data->zoneNumber >= 0) && (data->channelNumber > 0) && (data->channelNumber <= 1024));
}

_Static_assert(sizeof(paceStore_t) <= FIELD_APRS_REPLIES_OFFSET, "PACE store overlaps the APRS replies");
_Static_assert((FIELD_APRS_REPLIES_OFFSET + (APRS_REPLY_SLOTS * sizeof(aprsReplyRecord_t))) <= SECTOR_SIZE, "APRS replies do not fit in sector 0");

// Sector 0 is shared (PACE store + APRS replies): read-modify-write of the whole
// sector through the SPI flash sector buffer, so the other records are kept.
static void storeWrite(const paceStore_t *store)
{
	SPI_Flash_write(PACE_ADDRESS, (uint8_t *)store, sizeof(paceStore_t));
}

void fieldPaceSet(int slot, const paceSlot_t *data)
{
	paceStore_t storeBuf;
	paceStore_t *store = &storeBuf;

	if ((slot < 0) || (slot >= PACE_SLOTS))
	{
		return;
	}

	paceRead(store);
	if (data != NULL)
	{
		store->slots[slot] = *data;
	}
	else
	{
		store->slots[slot].zoneNumber = -1;
		store->slots[slot].channelNumber = 0;
	}

	storeWrite(store);
}

// --- APRS canned replies ------------------------------------------------------------------
bool fieldAprsReplyGet(int slot, char *text)
{
	aprsReplyRecord_t rec;

	if ((slot < 0) || (slot >= APRS_REPLY_SLOTS))
	{
		text[0] = 0;
		return false;
	}

	SPI_Flash_read((REPLIES_ADDRESS + (slot * sizeof(aprsReplyRecord_t))), (uint8_t *)&rec, sizeof(rec));
	return aprsReplyRecordDecode(slot, &rec, text);
}

void fieldAprsReplySet(int slot, const char *text)
{
	aprsReplyRecord_t rec;

	if ((slot < 0) || (slot >= APRS_REPLY_SLOTS))
	{
		return;
	}

	// No-op saves do not rewrite sector 0 (it also holds the PACE store)
	char current[APRS_TEXT_MSG_MAX + 1];
	bool edited = fieldAprsReplyGet(slot, current);

	if ((text == NULL) || (text[0] == 0))
	{
		if (edited == false)
		{
			return; // already the default
		}
	}
	else if (strncmp(text, current, APRS_TEXT_MSG_MAX) == 0)
	{
		return; // unchanged (or the same as the default)
	}

	aprsReplyRecordEncode(text, &rec);
	SPI_Flash_write((REPLIES_ADDRESS + (slot * sizeof(aprsReplyRecord_t))), (uint8_t *)&rec, sizeof(rec));
}

// --- coverage survey -----------------------------------------------------------------------
static uint32_t pointAddress(uint32_t index)
{
	return (COVERAGE_ADDRESS + (index * sizeof(coveragePoint_t)));
}

static bool pointIsEmpty(uint32_t index)
{
	uint32_t t = 0U;

	SPI_Flash_read(pointAddress(index), (uint8_t *)&t, sizeof(t));
	return (t == 0xFFFFFFFFU);
}

uint32_t fieldCoverageCount(void)
{
	if (coverageCount == 0xFFFFFFFFU)
	{
		paceStore_t store;

		paceRead(&store);
		if (store.coverageMagic != COVERAGE_MAGIC)
		{
			fieldCoverageClear(); // first use: erase what the GPS log may have left there
			return coverageCount;
		}

		// Points are written in order, so the first empty one is found by bisection
		uint32_t lo = 0U, hi = COVERAGE_POINTS_MAX;

		while (lo < hi)
		{
			uint32_t mid = ((lo + hi) / 2U);

			if (pointIsEmpty(mid))
			{
				hi = mid;
			}
			else
			{
				lo = (mid + 1U);
			}
		}
		coverageCount = lo;
	}
	return coverageCount;
}

bool fieldCoverageAdd(const coveragePoint_t *point)
{
	uint8_t page[PAGE_SIZE];
	uint32_t n = fieldCoverageCount();

	if (n >= COVERAGE_POINTS_MAX)
	{
		return false;
	}

	uint32_t addr = pointAddress(n);
	uint32_t pageAddr = (addr & ~(PAGE_SIZE - 1U));

	// Page program only (the sectors are erased by fieldCoverageClear)
	SPI_Flash_read(pageAddr, page, PAGE_SIZE);
	memcpy(&page[addr - pageAddr], point, sizeof(coveragePoint_t));
	SPI_Flash_writePage(pageAddr, page);
	coverageCount = (n + 1U);
	return true;
}

bool fieldCoverageGet(uint32_t index, coveragePoint_t *point)
{
	if (index >= fieldCoverageCount())
	{
		return false;
	}
	SPI_Flash_read(pointAddress(index), (uint8_t *)point, sizeof(coveragePoint_t));
	return true;
}

void fieldCoverageClear(void)
{
	paceStore_t store;

	for (uint32_t a = COVERAGE_ADDRESS; a < (FIELD_DATA_FLASH_ADDRESS + FIELD_DATA_FLASH_SIZE); a += SECTOR_SIZE)
	{
		SPI_Flash_eraseSector(a);
	}
	coverageCount = 0U;

	paceRead(&store);
	if (store.coverageMagic != COVERAGE_MAGIC)
	{
		store.coverageMagic = COVERAGE_MAGIC;
		storeWrite(&store);
	}
}

#endif
