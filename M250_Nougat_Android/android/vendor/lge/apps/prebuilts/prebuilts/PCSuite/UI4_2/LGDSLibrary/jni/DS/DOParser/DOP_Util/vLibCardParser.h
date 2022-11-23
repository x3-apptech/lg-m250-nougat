/*! \file cardparser.h
    \brief Defines a vCard/vCal parser
*/
/*
Licensing : All code is under the Mozilla Public License 1.1 (MPL 1.1) as specified in 
license.txt
*/

#ifndef CARD_PARSER_H
#define CARD_PARSER_H

#include "../../Include/DSApi_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define CARD_PARSER_VER_MAJOR 1
#define CARD_PARSER_VER_MINOR 0
#define CARD_PARSER_VER "1.0"

/*
 * 20110401 soungkyun.park@lge.com
 * vMessage VBODY Parsing  작업을 적용 하냐 안하냐 Flag Define 입니다.
 * 1 work
 * 0 not work
 *
 * MAX_MESSAGE_SIZE 600  SMS/MMS 공용 메세지 최대 크기 .. MMS 의 경우 더 커질수
 */

#define VMESSAGE_VBODY_PARSING	TRUE
#define MAX_MESSAGE_SIZE	600


typedef  char CARD_Char;
typedef void *CARD_Parser;

/* version */
const char *CARD_ParserVersion(void);

/* setup & parsing */
CARD_Parser CARD_ParserCreate(CARD_Char *encoding);
void CARD_ParserFree(CARD_Parser p);
int CARD_Parse(CARD_Parser p, const char *s, int len, int isFinal);

/* user data */
void CARD_SetUserData(CARD_Parser p, void *userData);
void *CARD_GetUserData(CARD_Parser p);

/* handlers */
/** \typedef typedef void (*CARD_PropHandler)(void *userData, const CARD_Char *propname, const CARD_Char **params);
    \param userData user data specified for the card parser (CARD_SetUserData())
    \param propname name of property for this event
    \param params null terminated list of param name/value pairs
            - params[0] = param name
            - params[1] = param value (can be null)
            - params[n] = 0
    \return nothing
    \brief Called when a property & its params is fully parsed
*/
typedef void (*CARD_PropHandler)(void *userData, const CARD_Char *propname, const CARD_Char **params);

/** \typedef typedef void (*CARD_DataHandler)(void *userData, const CARD_Char *data, int len);
    \param userData user data specified for the card parser (CARD_SetUserData())
    \param data pointer to decoded data for a parameter (data = NULL = eod)
    \param len length of data (len = 0 indicates eod)
    \return nothing
    \brief Called when a data chunk for a property is decoded
*/
typedef void (*CARD_DataHandler)(void *userData, const CARD_Char *data, int len);

//grandjoy
typedef void (*CARD_DataHandler_MYF)(void *userData, const CARD_Char *data, int len);
/* Set Handlers */
void CARD_SetPropHandler(CARD_Parser p, CARD_PropHandler cardProp);
void CARD_SetDataHandler(CARD_Parser p, CARD_DataHandler cardData);

/* util, wry added */ 
//extern int util_EncodeQP(char* szInput, char* szOutput, int nOutLength);	//0929

#ifdef __cplusplus
}
#endif


#endif
