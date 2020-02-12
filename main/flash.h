#ifndef _FLASH_H_
#define _FLASH_H_

#include <stdint.h>

int flash_write(uint32_t addr, uint16_t len, const char *data);
int flash_read(uint32_t addr, uint16_t len, char *data);
int flash_erase(uint16_t sect);

#endif /*_FLASH_H_*/

