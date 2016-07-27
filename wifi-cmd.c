/************************************************************************//**
 * \brief  WiFi command parser. Parses commands directed to the WiFi module
 *         and executes actions related to the commands and the module
 *         state.
 *
 * \author Jesus Alonso (doragasu)
 * \date   2015
 ****************************************************************************/

/* Note: This module handles frame and command coding/decoding */

#include "wifi-cmd.h"

/// Module initialization
void WiCmdInit(void) {

}

int WiCmdParse(uint8_t frame[], uint16_t length) {
	return 0;
}

int WiCmdSendReply(uint8_t data[], uint16_t length) {
	return 0;
}
