#ifndef _UPGRADE_H_
#define _UPGRADE_H_

#include <esp_err.h>

esp_err_t upgrade_firmware(const char *server, const char *name);

#endif /*_UPGRADE_H_*/

