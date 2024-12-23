// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __WS_H__
#define __WS_H__

#include "http.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WS_ERROR=0,   // an error has occurred
    WS_CLOSE,     // the remote endpoint has closed the connection; use ws_status to get the status code
	WS_MSG_TXT,   // text message has been received
	WS_MSG_BIN,   // binary message has been received
} WS_Msg_Type;

typedef enum {
	WS_STATUS_NORMAL=1000,
	WS_STATUS_GOING_AWAY=1001,
	WS_STATUS_PROTOCOL_ERROR=1002,
    WS_STATUS_CANT_ACCEPT=1003
} WS_Status_Code;

typedef struct Websocket_S * Websocket;

/*! \brief Determine if the given HTTP headers indicates a request
*          to upgrade an HTTP connection to the Websocket protcol.
 */
bool ws_is_upgradable(const Http_Headers headers);

/*! \brief Determine if the given HTTP headers indicates a request
*          to upgrade an HTTP connection to the Websocket protcol.
 */
Websocket ws_upgrade(FILE * f_in, FILE * f_out, const Http_Headers headers, const char * uri, bool masked_client);

/*! \brief Determine if the websocket is open
 */

bool ws_is_open(Websocket ws);

/*! 
 * \brief Close the websocket connection
 * \param status_code The Status code to send to the remote endpoint.
 */
void ws_close(Websocket ws, WS_Status_Code code);

/*! \brief Free resources with the websocket, closing the websocket if it
 *         is open.
 */
void ws_free(Websocket ws);

/*! \brief Wait for a message on the websocket.
 */
WS_Msg_Type ws_wait(Websocket ws);

const unsigned char * ws_get_msg(Websocket ws, size_t * msg_len);

bool ws_send_msg(Websocket ws, WS_Msg_Type type, const unsigned char * msg, size_t msg_len);

/*! \brief The status code sent from the remote 
 *         endpoint when the connection was closed.
 *         This is only meaningful after receiving
 *         a WS_CLOSED event.
 */
WS_Status_Code ws_status(Websocket ws);

#endif // __WS_H__
