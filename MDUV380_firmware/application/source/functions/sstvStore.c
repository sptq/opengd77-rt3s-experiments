/*
 * Fork: received SSTV pictures kept in the SPI flash (see functions/sstvStore.h).
 *
 * Use of this source code or binary releases for commercial purposes is
 * strictly forbidden (see license.txt).
 */
#include <string.h>
#include "functions/sstvStore.h"

#if defined(HAS_SSTV_STORE)

#include "functions/sstvRx.h"
#include "hardware/SPI_Flash.h"

#define SECTOR_SIZE       4096U
#define PAGE_SIZE         256U
#define PIXEL_BYTES       (SSTV_RX_ROWS * SSTV_RX_WIDTH * 2U)                 // 38 400
#define PIXEL_PAGES       (PIXEL_BYTES / PAGE_SIZE)                           // 150
#define SLOT_SECTORS      (((SSTV_STORE_PIXELS_OFFSET + PIXEL_BYTES) + (SECTOR_SIZE - 1U)) / SECTOR_SIZE) // 10
#define PAGES_PER_SECTOR  (SECTOR_SIZE / PAGE_SIZE)

#if ((PIXEL_BYTES % PAGE_SIZE) != 0) || ((SLOT_SECTORS * SECTOR_SIZE) > SSTV_STORE_SLOT_SIZE)
#error "SSTV store slot layout"
#endif
_Static_assert(sizeof(sstvStoreHeader_t) == 32U, "SSTV store header size");
// interfaces/gps.c: LOG_FLASH_16MB_START_ADDRESS + LOG_FLASH_16MB_MEM_SIZE, then the 8 KB DMR SMS inbox (functions/dmrSms.h)
_Static_assert(((14U * 1024U * 1024U) + (64U * 1024U) + ((2U * 1024U * 1024U) - (128U * 1024U) - (320U * 1024U) - (8U * 1024U)) + (8U * 1024U)) == SSTV_STORE_FLASH_ADDRESS, "the GPS log and the DMR SMS inbox must end where the SSTV store starts");

static const uint16_t *savePixels = NULL;
static sstvStoreHeader_t saveHeader;
static uint32_t saveSlotAddress;
static uint32_t saveSector;     // next sector to erase and fill
static bool saving = false;
static bool saveOk;          // every erase / program so far succeeded

static uint32_t slotAddress(uint32_t slot)
{
	return (SSTV_STORE_FLASH_ADDRESS + (slot * SSTV_STORE_SLOT_SIZE));
}

static bool readHeader(uint32_t slot, sstvStoreHeader_t *h)
{
	return (SPI_Flash_read(slotAddress(slot), (uint8_t *)h, sizeof(sstvStoreHeader_t)) &&
			(h->magic == SSTV_STORE_MAGIC) && (h->seq != 0xFFFFFFFFUL) && (h->seq != 0U));
}

uint32_t sstvStoreCount(void)
{
	sstvStoreHeader_t h;
	uint32_t n = 0U;

	for (uint32_t s = 0U; s < SSTV_STORE_SLOTS; s++)
	{
		if (readHeader(s, &h))
		{
			n++;
		}
	}
	return n;
}

bool sstvStoreBegin(const uint16_t *pixels, uint16_t rows, const char *mode, uint32_t rxFreq, uint32_t utc)
{
	sstvStoreHeader_t h;
	uint32_t newestSeq = 0U;
	uint32_t newestSlot = (SSTV_STORE_SLOTS - 1U); // empty store: the first picture goes to slot 0

	if (saving || (pixels == NULL))
	{
		return false;
	}

	for (uint32_t s = 0U; s < SSTV_STORE_SLOTS; s++)
	{
		if (readHeader(s, &h) && (h.seq > newestSeq))
		{
			newestSeq = h.seq;
			newestSlot = s;
		}
	}

	memset(&saveHeader, 0, sizeof(saveHeader));
	saveHeader.magic = SSTV_STORE_MAGIC;
	saveHeader.seq = (newestSeq + 1U);
	saveHeader.utc = utc;
	saveHeader.rxFreq = rxFreq;
	saveHeader.rows = rows;
	saveHeader.width = (uint8_t)SSTV_RX_WIDTH;
	saveHeader.height = (uint8_t)SSTV_RX_ROWS;
	if (mode != NULL)
	{
		strncpy(saveHeader.mode, mode, (sizeof(saveHeader.mode) - 1U));
	}

	saveSlotAddress = slotAddress((newestSlot + 1U) % SSTV_STORE_SLOTS);
	savePixels = pixels;
	saveSector = 0U;
	saveOk = true;
	saving = true;
	return true;
}

bool sstvStoreTick(void)
{
	if (saving == false)
	{
		return false;
	}

	if (saveSector < SLOT_SECTORS)
	{
		uint32_t sectorAddress = (saveSlotAddress + (saveSector * SECTOR_SIZE));

		saveOk = (SPI_Flash_eraseSector(sectorAddress) && saveOk);

		for (uint32_t p = 0U; p < PAGES_PER_SECTOR; p++)
		{
			uint32_t page = ((saveSector * PAGES_PER_SECTOR) + p); // page within the slot
			uint32_t pixelPage = (page - (SSTV_STORE_PIXELS_OFFSET / PAGE_SIZE));

			if ((page >= (SSTV_STORE_PIXELS_OFFSET / PAGE_SIZE)) && (pixelPage < PIXEL_PAGES))
			{
				saveOk = (SPI_Flash_writePage((sectorAddress + (p * PAGE_SIZE)), ((uint8_t *)savePixels + (pixelPage * PAGE_SIZE))) && saveOk);
			}
		}
		saveSector++;
		return false;
	}

	// Header last: the slot only becomes valid once the picture is complete (and not at all
	// after a flash error: the slot then stays empty)
	if (saveOk)
	{
		uint8_t page[PAGE_SIZE];

		memset(page, 0xFF, sizeof(page));
		memcpy(page, &saveHeader, sizeof(saveHeader));
		SPI_Flash_writePage(saveSlotAddress, page);
	}

	saving = false;
	savePixels = NULL;
	return true;
}

bool sstvStoreBusy(void)
{
	return saving;
}

void sstvStoreFinish(void)
{
	while (saving)
	{
		sstvStoreTick();
	}
}

#endif
