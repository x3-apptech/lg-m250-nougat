/*! \file cardparser.c
    \brief Implements a vCard/vCal parser
*/

/*! \mainpage CCARD
\section intro Purpose
Card parser is for the general parsing of card (doh!) objects, aka vCards & vCals.
It is a event orientated parser designed to be used in a similar manner to expat (a XML
parser).

\section license License
All code is under the Mozilla Public License 1.1 (MPL 1.1) as specified in 
\link license license.txt \endlink

\subsection features Features
- generic for any card sytax, e.g vCard & vCal
- reports errors in sytax
- Event orientated (ie reentrant & stream orientated)
- Handles line folding
- Automatic decoding of params
- Handles quoted-printable and base64 decoding

\subsection limitations Does NOT
- Generate cards (parser *only*)
- verify overall card structure - ie if required properties are missing - tough.

\subsection usage Usage

<ol>
<li>Create a card parser
   - CARD_ParserCreate()

<li>Init the parser:
   - CARD_SetUserData()
   - CARD_SetPropHandler()
   - CARD_SetDataHandler()

<li>Feed it Data
   - CARD_Parse()
   - Handle the parse events in the callbacks specified in 2.
   - Keep feeding it data in as large and small chunks as you like

<li>Free the card parser
   - CARD_ParserFree()
</ol>

\section reference Reference
\code
- const char *CARD_ParserVersion()
- CARD_Parser CARD_ParserCreate(CARD_Char *encoding)
- void CARD_ParserFree(CARD_Parser p)
- int CARD_Parse(CARD_Parser p, const char *s, int len, int isFinal)
- void CARD_SetUserData(CARD_Parser p, void *userData)
- void *CARD_GetUserData(CARD_Parser p)
- void CARD_SetPropHandler(CARD_Parser p, CARD_PropHandler cardProp)
- void CARD_SetDataHandler(CARD_Parser p, CARD_DataHandler cardData)
- typedef void (*CARD_PropHandler)(void *userData, const CARD_Char *propname, const CARD_Char **params);
- typedef void (*CARD_DataHandler)(void *userData, const CARD_Char *data, int len);
\endcode

*/ 

/*! \page license ccard License
\include license.txt
*/


/*
Licensing : All code is under the Mozilla Public License 1.1 (MPL 1.1) as specified in 
license.txt
*/

//#include "..\..\..\Api\BasicApiLib\include\include.h"
#include "../../Adapt/_OemDS_Dal.h"

#include "vLibCardParser.h"

#include "vLibEncode.h" //for VOBJ_MAX_B64LINE

#include "../../Adapt/_OemDS_Common.h"


#ifndef TRUE
#define TRUE -1
#endif

#ifndef FALSE
#define FALSE 0
#endif


/* version */
const char *CARD_ParserVersion(void)
{
    return CARD_PARSER_VER;
}

/*
 * 20110401 soungkyun.park@lge.com
 * VBODY 처리를 위한 STATE 추가  >> cp_BodyMessage
 * 현재 VMESSAGE_VBODY_PARSING 로 분기 처리
 */
#if (VMESSAGE_VBODY_PARSING == TRUE)
	typedef enum {cp_PropName, cp_ParamBN,cp_ParamIN, cp_ValueBV, cp_ValueIV, cp_PropData,cp_BodyMessage} TCARDParserState;
#else
	typedef enum {cp_PropName, cp_ParamBN,cp_ParamIN, cp_ValueBV, cp_ValueIV, cp_PropData} TCARDParserState;
#endif

typedef enum {cp_EncodingNone, cp_EncodingQP, cp_EncodingBase64, cp_Encoding8Bit} TCARDParserEncoding;

typedef struct
{
	/* folding stuff */
	int startOfLine;
	int folding;

	/* inital buffer */
	char *lineBuf;
	int lbSize;
	int lbLen;

	/* line parsing state */
	TCARDParserState	state;
	int startOfData;
	CARD_Char **params;
	int nParams;
	int valueEscape;
	TCARDParserEncoding	encoding;
	int		decoding;
	///////////////////////
	// kyt : 7월 19일
	// 127을 넘어가는 값에 대해서는 처리를 못해준다. 
	// char	decodeBuf[4];
	unsigned char	decodeBuf[4];

    int     dbLen;


	/* user data (duh!) */
	void *userData;

	/* handlers */
	CARD_PropHandler	cardProp;
	CARD_DataHandler	cardData;

	//20111123 soungkyun.park@lge.com
	int customFlag;
	enVobjectVer_t vObjectType;
} VP;

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
<<<		Util Functions	>>>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

int util_isspace( int c )
{
	if( c == 0x20 || c == 0x09 ) return 1;
	else return 0;
}

//20100929, jechan.han@lge.com, not used at the present
/*
int util_EncodeQP(char* szInput, char* szOutput, int nOutLength)
{
	char szTemp[4];
	int i,count;
	int nInLength = (int)DS_strlen(szInput);

	if( !szOutput || nOutLength < 1 ) return -1;

	count = 0;
	for( i=0 ; i < nInLength ; i++ )
	{
		if( count >= nOutLength ) return -1;

		if( (BYTE)szInput[i] > 0x7F )
		{
			DS_sprintf(szTemp,"%02X",(BYTE)szInput[i]);
			szOutput[count++] = '=';
			szOutput[count++] = szTemp[0];
			szOutput[count++] = szTemp[1];
		}
		else szOutput[count++] = szInput[i];
	}
	szOutput[count] = '\0';

	return count;
}
*/

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
<<<		Main Functions	>>>
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void vcard_clear_line(VP *vp)
{
	//OEMDS_LOG_DEBUG("vcard_clear_line IN\n",0,0,0);
	vp->state = cp_PropName;
	vp->lbLen = 0;
	if (vp->lbSize > 1024)
	{
		DS_free((vp->lineBuf));
		vp->lbSize = 0;
	};
	vp->startOfData = TRUE;
	vp->nParams = 0;
	vp->valueEscape = FALSE;
	vp->encoding = cp_EncodingNone;
	vp->decoding = FALSE;
	DS_memset(vp->decodeBuf, 0, sizeof(vp->decodeBuf));

	// HS Yoon 
	// 이 부분이 없으면 UTC Format를 읽어오는 부분에서 오류가 생김
	// 현재 UTC Format 검사는 : if( (*(data+15) == 'Z')) 이런식으로 되어있지만
	// DTSTART:20051108200000Z
	// DTEND:20051108230000Z
	// AALARM:20051108220000
	// 이렇게 들어올 가능성이 있음.
	// DTEND부분에서 15번째가 Z이고 연속으로 ALARM이 들어온다면 ALARM이 강제적으로 UTC Format으로 바뀜
	if(vp->lineBuf)
	{
		DS_memset(vp->lineBuf, 0, vp->lbSize);
		//OEMDS_LOG_DEBUG("vcard_clear_line DS_memset lineBuf\n",0,0,0);
	}
	
    vp->dbLen = 0;
	vp->customFlag = FALSE;
	//OEMDS_LOG_DEBUG("vcard_clear_line OUT\n",0,0,0);
}



/** \fn CARD_Parser CARD_ParserCreate(CARD_Char *encoding)
    \param encoding encoding of the card, currentl not used
    \return CARD_Parser handle or NULL on error
    \retval NULL An error ocurred
    \brief Creates a card parser handle.
    Allocates a handle for a card parser. Free with CARD_ParserFree()
*/
CARD_Parser CARD_ParserCreate(CARD_Char *encoding, enVobjectVer_t vObjectType)
{
	//OEMDS_LOG_DEBUG("CARD_ParserCreate IN\n",  0,0,0);
	VP *vp;

	vp = DS_malloc(sizeof(VP));
	if (!vp)
		return NULL;

	DS_memset(vp, 0, sizeof(VP));

	/* folding stuff */
	vp->startOfLine = TRUE;
	vp->folding = FALSE;
	vp->vObjectType = vObjectType;	

	/* init line stuff */
	
	vcard_clear_line(vp);


	return vp;
}

/** \fn void CARD_ParserFree(CARD_Parser p)
    \param p Card parser object
    \return nothing
    \brief Deallocates the card parser object.
    Frees the memory associated with a card parser objetc created by CARD_ParserCreate()
*/
void CARD_ParserFree(CARD_Parser p)
{
	VP *vp = (VP *) p;

	if(p == NULL) return;

	DS_free(vp->lineBuf);
	vp->lineBuf = NULL;
	
	DS_free(*(vp->params));
	*(vp->params) = NULL;

	DS_free(vp->params);
	vp->params = NULL;
	
	DS_free(p);
	p = NULL;
}

///////////////////////
// kyt : 7월 19일
// 127을 넘어가는 값에 대해서는 처리를 못해준다. 
//int vcard_add_char_to_line(VP *vp, char c) 
int vcard_add_char_to_line(VP *vp, unsigned char c)// 20050719
{
	
	/* expand buf if neccessary */

	if (vp->lbLen >= vp->lbSize)
	{
		vp->lbSize = vp->lbLen + 128;
		vp->lineBuf = (char *) DS_realloc(vp->lineBuf, (DS_ULONG)vp->lbSize);
		if (vp->lineBuf == NULL)
		{
			/* memory failure, crash & burn */
			vp->lbSize = 0;
			vp->lbLen = 0;
			return FALSE;
		};

	};

	/* place in linebuf */
	vp->lineBuf[vp->lbLen] = c;
	vp->lbLen++;
	////////////////////////
	//vp->lineBuf[vp->lbLen] = '\0';
	//OEMDS_LOG_DEBUG("[libDS] %s() - %d Line lineBuff = %s", OEMDS_LOG_FUNC, OEMDS_LOG_LINE, vp->lineBuf);	
	
	return TRUE;
}

int card_add_param(VP *vp, int paramOff)
{
	vp->params = (CARD_Char **) DS_realloc(vp->params, sizeof(CARD_Char *) * (vp->nParams + 1) * 2);

	if (! vp->params)
		return FALSE;

	vp->params[vp->nParams * 2] = (CARD_Char *) paramOff;
	vp->params[vp->nParams * 2 + 1] = NULL;
	vp->nParams++;

	return TRUE;
}

int card_set_paramvalue(VP *vp, int valueOff)
{
	if (! vp->params)
		return FALSE;

	vp->params[(vp->nParams - 1) * 2 + 1] = (CARD_Char *) valueOff;

	return TRUE;
}

int vcard_end_prop(VP *vp)
{
	char *b;
	const CARD_Char *propName;
	int i;
	CARD_Char *encoding;
	DS_CHAR* custom_type = "CUSTOM";

	if (vp->cardProp)
	{
		/* null terminate the line buf */
		if (! vcard_add_char_to_line(vp, 0))
			return FALSE;

		/* propname */
		b = vp->lineBuf;
		propName = b;

		if(DS_strstr(vp->lineBuf, custom_type) !=NULL)
		{
			vp->customFlag = TRUE;	// For custom type support ex) X-TELCUSTOM, X-ADDRCUSTOM..
			vp->valueEscape = TRUE;	
		}
		else
		{
			vp->customFlag = FALSE;
			vp->valueEscape = FALSE;
		}
		/* fix up params & values */
		for (i = 0; i < vp->nParams; i++)
		{
			vp->params[i * 2] = vp->lineBuf + (int) vp->params[i * 2];
			if (vp->params[i * 2 + 1])
				vp->params[i * 2 + 1] = vp->lineBuf + (int) vp->params[i * 2 + 1];

			/* check for encodings, needlessly complicated by solo value string
			   dumb spec choice */
			encoding = (char*)DS_strupr(vp->params[i * 2]);
			if (DS_strcmp(encoding, "ENCODING") == 0)
				encoding = (char*)DS_strupr(vp->params[i * 2 + 1]);
//			encoding = vp->params[i * 2];
//			if (strcmp(encoding, "ENCODING") == 0)
//				encoding = vp->params[i * 2 + 1];

			if (encoding)
			{
				if (DS_strcmp(encoding, "QUOTED-PRINTABLE") == 0)
					vp->encoding = cp_EncodingQP;
				else if ( (DS_strcmp(encoding, "BASE64") == 0) || (DS_strcmp(encoding, "B") == 0) )
					vp->encoding = cp_EncodingBase64;
				else if (DS_strcmp(encoding, "8BIT") == 0)
					vp->encoding = cp_Encoding8Bit;
			};
		};


		/* null terminate */
		card_add_param(vp, 0);

		/* do callback */
		vp->cardProp(vp->userData, propName, (const CARD_Char **) vp->params);
	};

	vp->lbLen = 0;
	//20110403 soungkyun.park@lge.com Memory set vp->LineBuf
	//DS_memset(vp->lineBuf,0x00,vp->lbSize);
	
	vp->state = cp_PropData;

	// HS Yoon 
	
	return TRUE;
}

/*  Base64 Chars 
    ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/ 
    Folling func translates a base64 char to its ordinal val
*/

unsigned char Base64ToOrd(unsigned char ch)
{
    if (ch >= 'A'&& ch <= 'Z')
        return (unsigned char)(ch - 'A');
    
    else if (ch >= 'a'&& ch <= 'z')
            return (unsigned char)(26 + ch - 'a');
    
    else if (ch >= '0' && ch <= '9')
            return (unsigned char)(52 + ch - '0');
    
    else if (ch == '+')
        return (unsigned char)62;
    
    else if (ch == '/')
        return (unsigned char)63;

    return 0xFF;
}

//int vcard_process_char(VP *vp, char c)
int vcard_process_char(VP *vp, unsigned char c)
{
	//OEMDS_LOG_DEBUG("vcard_process_char IN : %c, state : %d" ,c,vp->state,0);
	// for qp decoding
	int x;
    // for base64 decoding
	unsigned char ch;
	unsigned char u0;
	unsigned char u1;
	unsigned char u2;
	unsigned char u3;
	
	/* 20110401 soungkyun.park@lge.com
	 * VBODY 처리를 위한 변수
	 */	
	#if (VMESSAGE_VBODY_PARSING == TRUE)

	// 20110401 soungkyun.park@lge.com
	// szResultString : 검색 결과  의 시작번지를 저장합니다.
	DS_CHAR * szResultString;
	//searchStr : VBODY 의 TEXT 메세지 영역의 끝을 구분짓는 구분자 역활
	DS_CONST DS_CHAR * searchStr = "END:VBODY";
	// cp_PropData state 에서 VBODY 시작을 비교
	DS_CONST DS_CHAR * szVBodyTag = "VBODY";
	//VBODY 의 문자열 저장 SMS,MMS 공통 사용이므로 MMS 의 메시지 사이즈
	//에 대해서 확인필요
	DS_CHAR tempBuffer[MAX_MESSAGE_SIZE] = {0,};
	DS_INT	iVBodyPropLen = 0;

	iVBodyPropLen = DS_strlen((DS_PUCHAR)szVBodyTag);			//"VBODY" length will be 5
	/////////////////////////////////////////
	#endif

	switch (vp->state)
	{
	/* --------------------------------------- */
	/* Property Names */
	case cp_PropName:
		/* looking for eof propName delim */
		//OEMDS_LOG_DEBUG("vcard_process_char cp_PropName IN : %c" ,c,0,0);
		switch(c)
		{
		case ':':
			vcard_end_prop(vp);
			return TRUE;

		case ';':
			/* we have parameters - null terminate name */
			vp->state = cp_ParamBN;
			return vcard_add_char_to_line(vp, 0);
		
		case '\r':
			return TRUE;

		case '\n':
			/* throw it all away */
			vcard_clear_line(vp);
			return TRUE;

		default:
			/* just add */
			if (! util_isspace(c))
				return vcard_add_char_to_line(vp, c);
			else
				return TRUE;
		};

	/* --------------------------------------- */
	/* Param Names */
	case cp_ParamBN: /* before name */
		//OEMDS_LOG_DEBUG("vcard_process_char cp_ParamBN IN : %c" ,c,0,0);
		switch(c)
		{
		case ':':
			/* error */
			vcard_clear_line(vp); 
			return FALSE;

		default:
			if (util_isspace(c))
				return TRUE; /* ignore */

			/* start name - store as offset into lineBuf, as lineBuf may move when reallocated */
			card_add_param(vp, vp->lbLen);

			/* now in name */
			vp->state = cp_ParamIN;
			return vcard_add_char_to_line(vp, c);
		};

	case cp_ParamIN: /* in name */
		//OEMDS_LOG_DEBUG("vcard_process_char cp_ParamIN IN : %c" ,c,0,0);
		switch(c)
		{
		case '=':
			/* eof param name, value follows */
			vp->state = cp_ValueBV;
			return vcard_add_char_to_line(vp, 0); /* terminate param name */

		case ';':
			/* eof param, start new param */
			vp->state = cp_ParamBN;
			return vcard_add_char_to_line(vp, 0); /* terminate param name */

		case ':':
			/* eof param, into data */
			vcard_end_prop(vp);
			return TRUE;

		case '\n':
			/* throw it all away,  wry:add:20040602:param중간에 줄바꿈있을경우 그 줄 무시*/
			vcard_clear_line(vp);
			return TRUE;
		default:
			if (! util_isspace(c))
				return vcard_add_char_to_line(vp, c);
			else
				return TRUE;
		}

	/* ----------------------------------------------- */
	/* Param Values */
	case cp_ValueBV: /* before value */
		//OEMDS_LOG_DEBUG("vcard_process_char cp_ValueBV IN : %c" ,c,0,0);
		switch(c)
		{
		case ';':
		case ':':
			/* error ? can we have empty values (ie. "name=;" or "name=:") */
			vcard_clear_line(vp); 
			return FALSE;

		default:
			if (util_isspace(c))
				return TRUE; /* ignore */

			/* start value - store as offset into lineBuf, as lineBuf may move */
			card_set_paramvalue(vp, vp->lbLen);

			/* now in Value */
			vp->state = cp_ValueIV;
			return vcard_add_char_to_line(vp, c);
		};

	case cp_ValueIV: /* in value */
		//OEMDS_LOG_DEBUG("vcard_process_char cp_ValueIV IN : %c" ,c,0,0);
		switch(c)
		{
		case ';':
			if (vp->valueEscape)
			{
				/* just add to value */
				vp->valueEscape = FALSE;
				return vcard_add_char_to_line(vp, c);
			};

			/* eof value, start new param */
			vp->state = cp_ParamBN;
			return vcard_add_char_to_line(vp, 0); /* terminate value */

		case ':':
			/* eof value, into data */
			vcard_end_prop(vp);
			return TRUE;

		case '\n':
			/* throw it all away,  wry:add:20040602:param중간에 줄바꿈있을경우 그 줄 무시*/
			vcard_clear_line(vp);
			return TRUE;

		default:
			if (! util_isspace(c))
			{
				if (c == '\\')
				{
					
					if (vp->valueEscape)
						vp->valueEscape = FALSE; /* unescaping now */
					else
					{
						vp->valueEscape = TRUE; /* escaping now */
						return TRUE;
					};
				};
				return vcard_add_char_to_line(vp, c);
			}
			else
				return TRUE;
		}
		/* ----------------------------------------------- */
		/* 20110401  soungkyun.park@lge.com
		 * VBODY text message parsing
		 * */
	#if (VMESSAGE_VBODY_PARSING == TRUE)
		
	case cp_BodyMessage:
			//OEMDS_LOG_DEBUG("vcard_process_char cp_BodyMessage IN : %c" ,c,0,0);
			// 이전단계는 VBODY 의 cp_PropData state 에서 VBODY 로 비교 되어서
			// cp_PropName 으로 변경되어야 할 state 를 cp_PropData 로 변경한 상황이다.
			// vp->lineBuf 에 char 를 담는다.
			vcard_add_char_to_line(vp,c);
			//vp->lineBuf 에 저장된 문자열 에  searchStr 값과 같은 문자 가 있는 지 검색한다.
			szResultString = DS_strstr((DS_CHAR*)DS_strupr(vp->lineBuf), searchStr);
			// 결과가 참이라면 tempBuffer 에 libBuf 에 있는 문자열중 searchStr 길이 만큼 뺀 길이를 복사해 넣는다
			if(szResultString)
			{
				DS_strncpy(tempBuffer,vp->lineBuf,(vp->lbLen) - DS_strlen((DS_UCHAR *)searchStr));
				if(vp->cardData)
				{	// 이후 DataHandler 를 호출한다.
					if(vp->lbLen > 0)
						vp->cardData(vp->userData, tempBuffer, DS_strlen((DS_UCHAR *)tempBuffer));
					vcard_clear_line(vp);
				}
			}
		break;
		
	#endif
		/* ----------------------------------------------- */

	/* ----------------------------------------------- */
	/* Property Data */
	case cp_PropData:
		switch(c)
		{
		case '\r':
			//OEMDS_LOG_DEBUG("vcard_process_char cp_PropData \r character" ,0,0,0);
			return TRUE;

		case '\n':

			/* terminate data */
			if (vp->cardData)
			{
                /* check for improperly terminated base64 encoding */
                if (vp->encoding == cp_EncodingBase64)
                {
                    if (vp->dbLen > 0)
                    {
                        while (vp->dbLen != 0)
						{
							// 081021 Fix endless loop problem report from BAEG.
                            if (vcard_process_char(vp, '=') == FALSE)
							{
								vcard_clear_line(vp);
								return TRUE;
							}
						}
                    };
                };

				/* report any data */
				//if (vp->lbLen > 0)
				//20110726 soungkyun.park@lge.com Property name "TITLE" is a string of zero length, even if you are allowed
				
				vp->cardData(vp->userData, vp->lineBuf, vp->lbLen);
				
				/* terminate data */
				//vp->cardData(vp->userData, NULL, 0);		// wry, 굳이 null이 또한번 올 필요가?
			};

			vcard_clear_line(vp);
			/* ----------------------------------------------- */
			/* 20110401  soungkyun.park@lge.com
			 * VBODY state hook
			 * Normal flow cp_propData -> cp_propName in vcard_clear_line function
			 * VBODY strcmp Hook flow cp_propData -> cp_propName in vcard_clear_line function -> cp_BodyMessage
			 * */	
			
#if (VMESSAGE_VBODY_PARSING == TRUE)			
			if ((vp->lbLen >= iVBodyPropLen) && (vp->lbLen < MAX_MESSAGE_SIZE))
			{
				DS_memset((DS_CHAR *)tempBuffer, 0x00, sizeof(tempBuffer)*sizeof(DS_CHAR));
				DS_strncpy((DS_CHAR *)tempBuffer, (DS_CHAR *)vp->lineBuf, iVBodyPropLen);
			
				if(!DS_strcmp((DS_CHAR *)tempBuffer, (DS_CHAR *)szVBodyTag))
				{				
					vp->state = cp_BodyMessage;
				}
			}
#endif			
			return TRUE;

		//KYT, 2006-09-18, Escape문자 처리
		case ';':
			if(vp->customFlag == TRUE)
				vp->valueEscape = TRUE;
			if (vp->valueEscape)
			{
				/* just add to value */
				vp->valueEscape = FALSE;
				return vcard_add_char_to_line(vp, c);
			}
			else
			{
				// delimiters
				return vcard_add_char_to_line(vp, 0x1F);
			}

		
		case '\\' :
			//Must valueEscape is true in CUSTOM TYPE
			if(vp->customFlag == TRUE)
				vp->valueEscape = TRUE;

			if (vp->valueEscape)
			{
				vp->valueEscape = FALSE; // unescaping now
			}
			else
			{
				vp->valueEscape = TRUE; // escaping now 
				return TRUE;
			};
			
			return vcard_add_char_to_line(vp, c);			
		
		//HS Yoon : 지원여부에 대해서 고려중!!
/*		case '/' :
			if (vp->valueEscape)
				vp->valueEscape = FALSE; // unescaping now 
			
			return vcard_add_char_to_line(vp, c);			

		case '>' :
			if (vp->valueEscape)
				vp->valueEscape = FALSE; // unescaping now 
			
			return vcard_add_char_to_line(vp, c);			

		case '<' :
			if (vp->valueEscape)
				vp->valueEscape = FALSE; // unescaping now 
			
			return vcard_add_char_to_line(vp, c);			


		case '&' :
			if (vp->valueEscape)
				vp->valueEscape = FALSE; // unescaping now 
			
			return vcard_add_char_to_line(vp, c);		
*/


		// HS Yoon : Escape char handling

		case ',':
			if(vp->vObjectType == DSYNC_DEVINFO_VCARD_30)
			{
				if(vp->customFlag == TRUE)
					vp->valueEscape = TRUE;
				if (vp->valueEscape)
				{
					/* just add to value */
					vp->valueEscape = FALSE;
					return vcard_add_char_to_line(vp, c);
				}
				else
				{
					// delimiters
					return vcard_add_char_to_line(vp, 0x1F);
				}
			}
			else
			{
				//It's default case.
			}

		default:
			//OEMDS_LOG_DEBUG("vcard_process_char default IN : %c" ,c,0,0);
			// HS Yoon : Escape char handling
			if(vp->encoding != cp_EncodingQP) // for processing \(=5C) in QP
			{
				if (vp->valueEscape)  // single '\' : exception 
				{
					/* just add to value */
					vp->valueEscape = FALSE;
					vcard_add_char_to_line(vp, '\\');

				}
			}
			// HS Yoon : 20060427
			// Property data의 시작이 space가 들어올면 무조건 버림.
			// condition of space folding : Encoding None
			// statu : folding, startofdata, and space
			if (util_isspace(c)&&(vp->encoding == cp_EncodingNone)&&vp->folding)
			{
				return TRUE;
			}
			// a CRLF immediately followed by AT LEAST one LWSP-char may instead be inserted
			else if((vp->encoding == cp_EncodingNone)&&vp->folding)
			{
				vp->folding = FALSE;
				vcard_add_char_to_line(vp, ' ');
			}
			else
			{
				vp->startOfData = FALSE;
			}


			/* only store data if we bother reporting it */
			if (vp->cardData)
			{
				switch (vp->encoding)
				{
                case cp_EncodingBase64:
                    if (vp->dbLen < 4)
                    {
                        // add to decode buf
                        // may have trailing ws on lines, so ignore
                        if (! util_isspace(c))
                        {
                            vp->decodeBuf[vp->dbLen] = c;
                            vp->dbLen++;
                        };
                    };
                    if (vp->dbLen < 4)
                        return TRUE;

                    // we have a quartet
    		        u0 = Base64ToOrd(vp->decodeBuf[0]);
		            u1 = Base64ToOrd(vp->decodeBuf[1]);
		            u2 = Base64ToOrd(vp->decodeBuf[2]);
		            u3 = Base64ToOrd(vp->decodeBuf[3]);
                	
                    if (u0 == 0xFF || u1 == 0xFF)
                        return FALSE;

                    ch = (unsigned char)( ((u0 << 2) | (u1 >> 4)) );
                    vcard_add_char_to_line(vp, ch);

					if (vp->decodeBuf[2] != '=')
					{
                        if (u1 == 0xFF || u2 == 0xFF)
                            return FALSE;
			            ch = (unsigned char)( (((u1 & 0xF) << 4) | (u2 >> 2)) );
			            vcard_add_char_to_line(vp, ch);

                        if (vp->decodeBuf[3] != '=')
						{
                            if (u2 == 0xFF || u3 == 0xFF)
                                return FALSE;
			                ch = (unsigned char)( (((u2 & 0x3) << 6) | u3) );
			                vcard_add_char_to_line(vp, ch);
		                }
		            }

		      		DS_memset(vp->decodeBuf, 0, sizeof(vp->decodeBuf));

                    vp->dbLen = 0;
                    return TRUE;

				case cp_EncodingQP:
					if (vp->decoding)
					{
						if (vp->decodeBuf[0] == 0)
						{
							vp->decodeBuf[0] = c;
							return TRUE;
						}
						else
						{
							/* decoding
							   finished */
							x = 0;
							vp->decodeBuf[1] = c;
							vp->decodeBuf[2] = 0;
							sscanf(vp->decodeBuf, "%X", &x);
							c = (unsigned char)x;

							if(c == '\\')  // for processing \(=5C) in QP
							{

								if (vp->valueEscape)
								{
									vp->valueEscape = FALSE; /* unescaping now */
									vp->decoding = FALSE;
									vcard_add_char_to_line(vp, '\\');

									return TRUE;
								}
								else
								{
									vp->valueEscape = TRUE; /* escaping now */
									vp->decoding = FALSE;
									return TRUE;
								};
							}
							
							vp->decoding = FALSE;
							break; /* drops down to add char */
						}
					}
					else if (c == '=')
					{
						vp->decoding = TRUE;
						DS_memset(vp->decodeBuf, 0, sizeof(vp->decodeBuf));
                        vp->dbLen = 0;
						return TRUE;
					};
					break;
				};

				vcard_add_char_to_line(vp, c); 
			};
			return TRUE;
		};
	};

	return TRUE;
}

/** \fn int CARD_Parse(CARD_Parser p, const char *s, int len, int isFinal)
    \param p Card parser object
    \param s pointer to binary data, can be null, does not need to be null terminated
    \param len length of data
    \param isFinal wether this is the final chunk of data
    \return success/failure
    \retval 0 a parse error occurred
    \retval non-zero success
    \brief Parses a block of memory.
    Parses the block of memory, the memory does not need to be zero-terminated. isFinal should be set to 
    TRUE (non-zero) for the final call so that internal structures can be flushed to the output.
    \note \code CARD_Parse(p, NULL, 0, TRUE);\endcode is an acceptable way of terminating input.
*/
int CARD_Parse(CARD_Parser p, const char *s, int len, int isFinal)
{
	
	
	int i;
	char c;
	VP *vp = (VP *) p;
	
	
	/* add to lineBuf */
	for (i = 0; i < len; i++)
	{
		c = s[i];
		
		/* check start of line whitespace/folding */
		if (vp->startOfLine)
		{
    		if (c == '\n')
                continue; // empty line

			if (! vp->folding && util_isspace(c))
			{
				/* we are folding, carry on */
				vp->folding = TRUE;
				continue;
			}

			//20110428, gaeul.jeong, Exception Routine for Snoopy Companion
			//They send a line with ' ' after the =\n of QP Encoded soruce. Ignore the space
			if((vp->folding) && (vp->encoding == cp_EncodingQP) && (util_isspace(c)))
			{
				continue;
			}

			/* no longer start of line */
			vp->startOfLine = FALSE;

			/* either are folding or can terminate last line */
			if (vp->folding)
			{
				/* cancel any qp encoding as it does not continue across folding breaks */
				if (vp->encoding == cp_EncodingQP)
				{
					vp->decoding = FALSE;
				}
			//	vcard_process_char(vp, ' ');  //folding문제 부분...2005-06-09류상문
			}
			else
				vcard_process_char(vp, '\n'); 
		}
			

		if (c == '\n')
		{
			vp->startOfLine = TRUE;
			vp->folding = FALSE;

			// 2005-08-16 Folding(QP)에 대한 처리 : 라인끝에 "=" [Softline Break]
			// 참고로 B64의 경우 LWSP를 사용하므로 이 루틴의 영향을 받지 않는다.
			if (! vp->folding && s[i-2]== '=' && (vp->encoding == cp_EncodingQP ))// || vp->encoding == cp_EncodingBase64) )//util_isspace(c))
			{
				/* we are folding, carry on */
				vp->folding = TRUE;
				continue;
			}			
			else if(vp->encoding == cp_EncodingBase64) //Importing PHOTO data가 folding 안되어 있는 경우 처리(MOBICAL)
			{
				if(util_isspace(s[i+1])) //folding이 되어 있으면, 위에서 처리  "if (! vp->folding && util_isspace(c))"
				{
					continue;
				}
				else //folding이 안되어 있는 경우, folding으로 간주해야 할지 판단하는 루틴
				{				
					int j;
					for( j=i+1; (j<=i+VOBJ_MAX_B64LINE+6+1) && (j<len); j++)
					{
					
						if( (s[j] == ':') || (s[j] == ';') ) //folding 아님 (새로운 property의 시작)	
						{
							break;
						}
						else if( (s[j] == '\0') || (s[j] == '\r') || (s[j] == '\n') ) //folding으로 판단
						{
							vp->folding = TRUE;
							break;
						}
						
					}
				}
			}

		}
		else
		{
			/* add to lineBuf */
			if (! vcard_process_char(vp, c))
				return 0;
		};
	};

	if (isFinal)
	{
		/* terminate  */
		vcard_process_char(vp, '\n');
	};
	//OEMDS_LOG_DEBUG("CARD PARSE END\n",0,0,0);
	return TRUE;
}

/* user data */
/** \fn void CARD_SetUserData(CARD_Parser p, void *userData)
    \param p Card parser object
    \param userData value to be set
    \return nothing
    \brief sets the user data value for the callbacks (::CARD_PropHandler, ::CARD_DataHandler)
    This value will be passed to the specified callbacks
*/
void CARD_SetUserData(CARD_Parser p, void *userData)
{
	VP *vp = (VP *) p;

	// WBT_TD: #2395 20120612 Chunghan Lee	
	if(NULL != vp)
		vp->userData = userData;
}

/** \fn void *CARD_GetUserData(CARD_Parser p)
    \param p Card parser object
    \return current useData value
    \brief returns the user data value for the callbacks (::CARD_PropHandler, ::CARD_DataHandler)
*/
void *CARD_GetUserData(CARD_Parser p)
{
	VP *vp = (VP *) p;

	return vp->userData;
}

/** \fn void CARD_SetPropHandler(CARD_Parser p, CARD_PropHandler cardProp)
    \param p Card parser object
    \param cardProp	function pointer for prop event handler
    \return nothing
    \brief Set the callback for vcard properties
*/
void CARD_SetPropHandler(CARD_Parser p, CARD_PropHandler cardProp)
{
	VP *vp = (VP *) p;

	// WBT_TD: #2899 20120622 Chunghan Lee	
	if(NULL != vp)
		vp->cardProp = cardProp;
}

/** \fn void CARD_SetDataHandler(CARD_Parser p, CARD_DataHandler cardData)
    \param p Card parser object
    \param cardData	function pointer for data event handler
    \return nothing
    \brief Set the callback for vcard data
*/
void CARD_SetDataHandler(CARD_Parser p, CARD_DataHandler cardData)
{
	VP *vp = (VP *) p;

	// WBT_TD: #2899 20120622 Chunghan Lee
	if(NULL != vp)
		vp->cardData = cardData;
}

