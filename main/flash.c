#include <spi_flash.h>
#include "megawifi.h"
#include "util.h"
#include "flash.h"

int flash_write(uint32_t addr, uint16_t len, const char *data)
{
	int err = 1;

	// Compute effective flash address
	addr += MW_FLASH_USER_BASE_ADDR;
	// Check for overflows (avoid malevolous attempts to write to
	// protected area)
	if ((addr + len) > FLASH_LENGTH) {
		LOGE("Address/length combination overflows!");
	} else if (spi_flash_write(addr, &len, len) != ESP_OK) {
		LOGE("Write to flash failed!");
	} else {
		err = 0;
	}

	return err;
}

int flash_read(uint32_t addr, uint16_t len, char *data)
{
	int err = 1;

	// Compute effective flash address
	addr += MW_FLASH_USER_BASE_ADDR;

	// Check requested length fits a transfer and there's no overflow
	// on address and length (maybe a malicious attempt to read
	// protected area
	if (len > (MW_MSG_MAX_BUFLEN - MW_CMD_HEADLEN) ||
			((addr + len) > FLASH_LENGTH)) {
		LOGE("Invalid address/length combination.");
	// Perform read and check result
	} else if (spi_flash_read(addr, data, len) != ESP_OK) {
		LOGE("Flash read failed!");
	} else {
		err = 0;
	}

	return err;
}

int flash_erase(uint16_t sect)
{
	int err = 1;

	// Check for sector overflow
	if (sect >= ((FLASH_LENGTH - MW_FLASH_USER_BASE_SECT)>>12)) {
		LOGW("Wrong sector number.");
		goto out;
	}

	sect += MW_FLASH_USER_BASE_SECT;
	if (spi_flash_erase_sector(sect) != ESP_OK) {
		LOGE("Sector erase failed!");
	} else {
		LOGE("Sector erase OK!");
		err = 0;
	}

out:
	return err;
}

