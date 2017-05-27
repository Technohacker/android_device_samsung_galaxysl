
/*
 *  Copyright 2001-2008 Texas Instruments - http://www.ti.com/
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* ====================================================================
*             Texas Instruments OMAP(TM) Platform Software
* (c) Copyright Texas Instruments, Incorporated. All Rights Reserved.
*
* Use of this software is controlled by the terms and conditions found
* in the license agreement under which this software has been supplied.
* ==================================================================== */

/*utilities includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <time.h>
//#include <mcheck.h>
#include <getopt.h>
#include <signal.h>
#include <cutils/log.h>

/* OMX includes */
#include <OMX_Component.h>
#include "OMX_JpegEnc_CustomCmd.h"
#include "JpegEncoder.h"
#include "OMX_JpegEnc_Utils.h"

/* DSP recovery includes */
#include <qosregistry.h>
#include <qosti.h>
#include <dbapi.h>
#include <DSPManager.h>
#include <DSPProcessor.h>
#include <DSPProcessor_OEM.h>

#define STRESS
#define NSTRESSCASES 1
#define STRESSMULTILOAD 1
#define NUM_OF_PORTS  2
#define NUM_OF_BUFFERSJPEG 1

#define LOG_TAG "JpegEncoder"
//#define LOG_NDEBUG 0

OMX_STRING StrJpegEncoder= "OMX.TI.JPEG.encoder";

typedef unsigned char uchar;
/**
 * Pipe used to communicate back to the main thread from the component thread;
**/
int IpBuf_Pipe[2];
int OpBuf_Pipe[2];
int Event_Pipe[2];

/* the FLAG when we need to DeInit the OMX */
int DEINIT_FLAG = 0;

/* Flag set when component is preempted */
int bPreempted=0;

/* Hardware error flag */
OMX_BOOL bError = OMX_FALSE;

/*function prototypes */
/* FIXME: inline int maxint(int a, int b); */
int maxint(int a, int b);

/*Routine to get the maximum of 2 integers */
/* FIXME: inline int maxint(int a, int b)
{
    return(a>b) ? a : b;
} */
int maxint(int a, int b)
{
    return(a>b) ? a : b;
}
static OMX_ERRORTYPE WaitForState(OMX_HANDLETYPE* pHandle,
                                  OMX_STATETYPE DesiredState)
{
	OMX_STATETYPE CurState = OMX_StateInvalid;
	OMX_ERRORTYPE eError = OMX_ErrorNone;
	OMX_COMPONENTTYPE *pComponent = (OMX_COMPONENTTYPE *)pHandle;

	while ( (eError == OMX_ErrorNone) &&
	        (CurState != DesiredState)) {
		if (sem_wait(semaphore))
		{
			ALOGE("\nsem_wait returned the error");
			continue;
		}

		eError = pComponent->GetState(pHandle, &CurState);
		if (CurState == OMX_StateInvalid && DesiredState != OMX_StateInvalid) {
			eError = OMX_ErrorInvalidState;
			break;
		}
	}


	if ( eError != OMX_ErrorNone ) {
		ALOGE("Error: Couldn't get state for component or sent to invalid state because of an error.\n");
		return eError;
	}
	return OMX_ErrorNone;
}

OMX_ERRORTYPE EventHandler(OMX_HANDLETYPE hComponent,OMX_PTR pAppData,OMX_EVENTTYPE eEvent,
                           OMX_U32 nData1, OMX_U32 data2, OMX_PTR pEventData)
{
    OMX_COMPONENTTYPE *pComponent = (OMX_COMPONENTTYPE *)hComponent;
    OMX_STATETYPE state;
    OMX_ERRORTYPE eError;
    JPEGE_EVENTPRIVATE MyEvent;

    MyEvent.eEvent = eEvent;
    MyEvent.nData1 = nData1;
    MyEvent.nData2 = data2;
    MyEvent.pAppData = pAppData;
    MyEvent.pEventInfo = pEventData;
    eError = pComponent->GetState (hComponent, &state);

    if ( eError != OMX_ErrorNone ) {
        ALOGE("Error: From JPEGENC_GetState\n");
    }
    switch ( eEvent ) {

        case OMX_EventCmdComplete:
			sem_post(semaphore) ;
            break;

        case OMX_EventError:
                if (nData1 == OMX_ErrorHardware){
                    ALOGE("\n\nAPP:: ErrorNotification received: Error Num = %p Severity = %ld String  = %s\n", (OMX_PTR)nData1, data2, (OMX_STRING)pEventData);
                    ALOGE("\nAPP:: OMX_ErrorHardware. Deinitialization of the component....\n\n");
                    if(!bError) {
                        bError = OMX_TRUE;
                        write(Event_Pipe[1], &MyEvent, sizeof(JPEGE_EVENTPRIVATE));
                    }
                }
                else if(nData1 == OMX_ErrorResourcesPreempted) {
                    bPreempted = 1;
                    ALOGE("APP:: OMX_ErrorResourcesPreempted !\n\n");
                }
                else if(nData1 == OMX_ErrorInvalidState) {
                    ALOGE("\n\nAPP:: ErrorNotification received: Error Num = %p Severity = %ld String	= %s\n", (OMX_PTR)nData1, data2, (OMX_STRING)pEventData);
                    ALOGE("\nAPP:: Invalid state\n\n");
                    if(!bError) {
                        bError = OMX_TRUE;
                        write(Event_Pipe[1], &MyEvent, sizeof(JPEGE_EVENTPRIVATE));
                    }
                }
                else if(nData1 == OMX_ErrorPortUnpopulated) {
                    ALOGE("APP:: OMX_ErrorPortUnpopulated\n");
    		    bError = OMX_TRUE;
                }
                else if (nData1 == OMX_ErrorStreamCorrupt) {
                    ALOGE("\n\nAPP:: ErrorNotification received: Error Num = %p Severity = %ld String	= %s\n", (OMX_PTR)nData1, data2, (OMX_STRING)pEventData);
                    ALOGE("%s: Stream Corrupt (%ld %s)\n",__FUNCTION__,data2,(char*)pEventData);
                    if(!bError) {
    			bError = OMX_TRUE;
    			write(Event_Pipe[1], &MyEvent, sizeof(JPEGE_EVENTPRIVATE));
    		    }
                }
                else {
    		    bError = OMX_TRUE;
                    DEINIT_FLAG = 1;
                }
            sem_post(semaphore) ;
            break;

        case OMX_EventResourcesAcquired:
            bPreempted = 0;
            break;

        case OMX_EventPortSettingsChanged:
        case OMX_EventBufferFlag:
            ALOGE("Event Buffer Flag detected\n");
        case OMX_EventMax:
        case OMX_EventMark:
            break;

        default:
                ALOGE("ErrorNotification received: Error Num %p: String :%s\n", (OMX_PTR)nData1, (OMX_STRING)pEventData);
    }

    return OMX_ErrorNone;
}


void FillBufferDone (OMX_HANDLETYPE hComponent, OMX_PTR ptr,
                     OMX_BUFFERHEADERTYPE* pBuffHead)
{
    write(OpBuf_Pipe[1], &pBuffHead, sizeof(pBuffHead));
}


void EmptyBufferDone(OMX_HANDLETYPE hComponent, OMX_PTR ptr,
                     OMX_BUFFERHEADERTYPE* pBuffer)
{
    write(IpBuf_Pipe[1], &pBuffer, sizeof(pBuffer));
}

int fill_data (OMX_BUFFERHEADERTYPE *pBuf, void * inputBuffer, int buffSize)
{
    int nRead;
    OMX_U8 oneByte;

	nRead = buffSize;

    memcpy(pBuf->pBuffer, inputBuffer, nRead );

    ALOGE ("Buffer Size = %d. Read %d bytes from buffer. \n", (int) nRead, (int)nRead);

	pBuf->nFilledLen=nRead;
	pBuf->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;


    return nRead;
}

int encodeImage(void* outputBuffer, void *inputBuffer, int width, int height, int quality, unsigned char* pExifBuf, int ExifSize, int ThumbWidth, int ThumbHeight)
{

    OMX_HANDLETYPE pHandle;
    OMX_U32 AppData = 100;

    OMX_CALLBACKTYPE JPEGCaBa = {(void *)EventHandler,
                     (void*) EmptyBufferDone,
                     (void*)FillBufferDone};
    int retval;
    int nWidth;
    int nHeight;
    int framesent = 0;
    int nRepeated = 0;
    int maxRepeat = 1;
    int inputformat;
    int qualityfactor;
    int buffertype;
    sigset_t set;
	int fileSize;

	semaphore = NULL;
	semaphore = (sem_t*)malloc(sizeof(sem_t)) ;
    sem_init(semaphore, 0x00, 0x00);

    OMX_STATETYPE state;
    OMX_COMPONENTTYPE *pComponent;
    IMAGE_INFO* imageinfo = NULL;
    OMX_PORT_PARAM_TYPE* pPortParamType = NULL;
    OMX_IMAGE_PARAM_QFACTORTYPE* pQfactorType = NULL;
    JPEGENC_CUSTOM_HUFFMANTTABLETYPE *pHuffmanTable = NULL;
    OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE *pQuantizationTable = NULL;
    OMX_PARAM_PORTDEFINITIONTYPE* pInPortDef = NULL;
    OMX_PARAM_PORTDEFINITIONTYPE* pOutPortDef = NULL;
    OMX_CONFIG_RECTTYPE sCrop;

    OMX_BOOL bConvertion_420pTo422i = OMX_FALSE;

    OMX_BUFFERHEADERTYPE* pInBuff[NUM_OF_BUFFERSJPEG];
    OMX_BUFFERHEADERTYPE* pOutBuff[NUM_OF_BUFFERSJPEG];
    int nCounter = 0;
    int fdmax;
    OMX_U8* pTemp;
    OMX_U8* pInBuffer[NUM_OF_BUFFERSJPEG];
    OMX_U8* pOutBuffer[NUM_OF_BUFFERSJPEG];
    OMX_BUFFERHEADERTYPE* pBuffer;
    OMX_BUFFERHEADERTYPE* pBuf;
    int nRead;
    int done;
    OMX_S32 sJPEGEnc_CompID = 300;
    int nIndex1= 0;
    int nIndex2 = 0;
    int nframerecieved = 0;
    int nMultFactor = 0;
    int nHeightNew, nWidthNew;
    OMX_INDEXTYPE nCustomIndex = OMX_IndexMax;
    OMX_ERRORTYPE error = OMX_ErrorNone;
    OMX_ERRORTYPE eError = OMX_ErrorNone;

    MALLOC(pPortParamType, OMX_PORT_PARAM_TYPE);
    MALLOC(pQfactorType, OMX_IMAGE_PARAM_QFACTORTYPE);
    MALLOC(pQuantizationTable,OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE);
    MALLOC(pHuffmanTable, JPEGENC_CUSTOM_HUFFMANTTABLETYPE);
    MALLOC(pInPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    MALLOC(pOutPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    MALLOC(imageinfo, IMAGE_INFO);

    OMX_CONF_INIT_STRUCT(pPortParamType, OMX_PORT_PARAM_TYPE);
    OMX_CONF_INIT_STRUCT(pQfactorType, OMX_IMAGE_PARAM_QFACTORTYPE);
    OMX_CONF_INIT_STRUCT(pQuantizationTable,OMX_IMAGE_PARAM_QUANTIZATIONTABLETYPE);
    OMX_CONF_INIT_STRUCT(pHuffmanTable, JPEGENC_CUSTOM_HUFFMANTTABLETYPE);
    OMX_CONF_INIT_STRUCT(pInPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    OMX_CONF_INIT_STRUCT(pOutPortDef, OMX_PARAM_PORTDEFINITIONTYPE);

    /* Setting up default parameters */
    nWidth=width;
    nHeight=height;
    inputformat=2;
    qualityfactor = quality;
    buffertype=1;

    imageinfo->nThumbnailWidth_app0 = 0;
    imageinfo->nThumbnailHeight_app0 = 0;
    imageinfo->nThumbnailWidth_app1 = ThumbWidth;
    imageinfo->nThumbnailHeight_app1 = ThumbHeight;
    imageinfo->nThumbnailWidth_app5 = 0;
    imageinfo->nThumbnailHeight_app5 = 0;
    imageinfo->nThumbnailWidth_app13 = 0;
    imageinfo->nThumbnailHeight_app13 = 0;

    imageinfo->bAPP0 = OMX_FALSE;
    if (ThumbWidth > 0 && ThumbHeight > 0)
        imageinfo->bAPP1 = OMX_TRUE;
    else
        imageinfo->bAPP1 = OMX_FALSE;
    imageinfo->bAPP5 = OMX_FALSE;
    imageinfo->bAPP13 = OMX_FALSE;
    imageinfo->nComment = OMX_FALSE;
    imageinfo->pCommentString = NULL;

    sCrop.nTop = 0;
    sCrop.nLeft = 0;
    sCrop.nWidth = 0;
    sCrop.nHeight = 0;


	error = TIOMX_Init();
	if ( error != OMX_ErrorNone ) {
	    ALOGE("Error returned by OMX_Init()\n");
	    goto EXIT;
	}

	ALOGD("OMX_Init Successful!\n");

	error = TIOMX_GetHandle(&pHandle,StrJpegEncoder,(void *)&AppData, &JPEGCaBa);
	if ( (error != OMX_ErrorNone) || (pHandle == NULL) ) {
	    ALOGE ("Error in Get Handle function\n");
	    goto EXIT;
	}

	/* Create a pipe used to queue data from the callback. */
	retval = pipe(IpBuf_Pipe);
	if ( retval != 0 ) {
	    ALOGE("Error:Fill Data Pipe failed to open\n");
	    goto EXIT;
	}

	retval = pipe(OpBuf_Pipe);
	if ( retval != 0 ) {
	    ALOGE("Error:Empty Data Pipe failed to open\n");
	    goto EXIT;
	}

	retval = pipe(Event_Pipe);
	if ( retval != 0 ) {
	    ALOGE("Error:Empty Data Pipe failed to open\n");
	    goto EXIT;
	}

	/* save off the "max" of the handles for the selct statement */
	fdmax = maxint(IpBuf_Pipe[0], OpBuf_Pipe[0]);
	fdmax = maxint(Event_Pipe[0], fdmax);

	error = OMX_GetParameter(pHandle, OMX_IndexParamImageInit, pPortParamType);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    error = OMX_ErrorBadParameter;
	    goto EXIT;
	}

	nIndex1 = pPortParamType->nStartPortNumber;
	nIndex2 = nIndex1 + 1;
	pInPortDef->nPortIndex = nIndex1;
	error = OMX_GetParameter (pHandle, OMX_IndexParamPortDefinition, pInPortDef);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    error = OMX_ErrorBadParameter;
	    goto EXIT;
	}

	if (pInPortDef->eDir == nIndex1 ) {
	    pInPortDef->nPortIndex = nIndex1;
	}
	else {
	    pInPortDef->nPortIndex = nIndex2;
	}

	/* Set the component's OMX_PARAM_PORTDEFINITIONTYPE structure (input) */
	pInPortDef->nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	pInPortDef->nVersion.s.nVersionMajor = 0x1;
	pInPortDef->nVersion.s.nVersionMinor = 0x0;
	pInPortDef->nVersion.s.nRevision = 0x0;
	pInPortDef->nVersion.s.nStep = 0x0;
	pInPortDef->nPortIndex = 0x0;
	pInPortDef->eDir = OMX_DirInput;
	pInPortDef->nBufferCountActual = NUM_OF_BUFFERSJPEG;
	pInPortDef->nBufferCountMin = 1;
	pInPortDef->bEnabled = OMX_TRUE;
	pInPortDef->bPopulated = OMX_FALSE;
	pInPortDef->eDomain = OMX_PortDomainImage;

	/* OMX_IMAGE_PORTDEFINITION values for input port */
	pInPortDef->format.image.cMIMEType = "JPEGENC";
	pInPortDef->format.image.pNativeRender = NULL;
	pInPortDef->format.image.nFrameWidth = nWidth;
	pInPortDef->format.image.nFrameHeight = nHeight;
	pInPortDef->format.image.nSliceHeight = -1;
	pInPortDef->format.image.bFlagErrorConcealment = OMX_FALSE;
	pInPortDef->format.image.eColorFormat =  OMX_COLOR_FormatCbYCrY;
	pInPortDef->format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;

	nMultFactor = (nWidth + 16 - 1)/16;
	nWidthNew = (int)(nMultFactor) * 16;

	nMultFactor = (nHeight + 16 - 1)/16;
	nHeightNew = (int)(nMultFactor) * 16;

	pInPortDef->nBufferSize = (nWidthNew * nHeightNew * 2);
	if (pInPortDef->nBufferSize < 400) {
	    pInPortDef->nBufferSize = 400;
	}

	error = OMX_SetParameter (pHandle, OMX_IndexParamPortDefinition, pInPortDef);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    error = OMX_ErrorBadParameter;
	    goto EXIT;
	}

	pOutPortDef->nPortIndex = nIndex2;
	error = OMX_GetParameter(pHandle, OMX_IndexParamPortDefinition, pOutPortDef);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    error = OMX_ErrorBadParameter;
	    goto EXIT;
	}
	if (pOutPortDef->eDir == nIndex1 ) {
	    pOutPortDef->nPortIndex = nIndex1;
	}
	else {
	    pOutPortDef->nPortIndex = nIndex2;
	}
	/* Set the component's OMX_PARAM_PORTDEFINITIONTYPE structure (input) */

	pOutPortDef->nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
	pOutPortDef->nVersion.s.nVersionMajor = 0x1;
	pOutPortDef->nVersion.s.nVersionMinor = 0x0;
	pOutPortDef->nVersion.s.nRevision = 0x0;
	pOutPortDef->nVersion.s.nStep = 0x0;
	pOutPortDef->nPortIndex = 0x1;
	pOutPortDef->eDir = OMX_DirInput;
	pOutPortDef->nBufferCountActual = NUM_OF_BUFFERSJPEG;
	pOutPortDef->nBufferCountMin = 1;
	pOutPortDef->bEnabled = OMX_TRUE;
	pOutPortDef->bPopulated = OMX_FALSE;
	pOutPortDef->eDomain = OMX_PortDomainImage;

	/* OMX_IMAGE_PORTDEFINITION values for input port */
	pOutPortDef->format.image.cMIMEType = "JPEGENC";
	pOutPortDef->format.image.pNativeRender = NULL;
	pOutPortDef->format.image.nFrameWidth = nWidth;
	pOutPortDef->format.image.nFrameHeight = nHeight;
	pOutPortDef->format.image.nStride = -1;
	pOutPortDef->format.image.nSliceHeight = -1;
	pOutPortDef->format.image.bFlagErrorConcealment = OMX_FALSE;

	/*Minimum buffer size requirement */
	pOutPortDef->nBufferSize = (nWidth*nHeight);
	if( qualityfactor < 10){
	    pOutPortDef->nBufferSize /=10;
	}
	else if (qualityfactor <100){
	    pOutPortDef->nBufferSize /= (100/qualityfactor);
	}

	/*Adding memory to include Thumbnail, comments & markers information and header (depends on the app)*/
	pOutPortDef->nBufferSize += 12288;


	pOutPortDef->format.image.eColorFormat =  OMX_COLOR_FormatCbYCrY;
	pOutPortDef->format.image.eCompressionFormat = OMX_IMAGE_CodingEXIF;

	error = OMX_SetParameter (pHandle, OMX_IndexParamPortDefinition, pOutPortDef);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    error = OMX_ErrorBadParameter;
	    goto EXIT;
	}

	pComponent = (OMX_COMPONENTTYPE *)pHandle;

	error = OMX_SetConfig(pHandle, OMX_IndexConfigCommonInputCrop, &sCrop);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	   error = OMX_ErrorBadParameter;
	   goto EXIT;
	}

	pComponent = (OMX_COMPONENTTYPE *)pHandle;
	error = OMX_SendCommand(pHandle, OMX_CommandStateSet, OMX_StateIdle ,NULL);
	if ( error != OMX_ErrorNone ) {
	    ALOGE ("Error from SendCommand-Idle(Init) State function\n");
	    goto EXIT;
	}


	for (nCounter = 0; nCounter < NUM_OF_BUFFERSJPEG; nCounter++) {
        OMX_MALLOC_SIZE_DSPALIGN ( pTemp, pInPortDef->nBufferSize,OMX_U8 );
	    if(pTemp == NULL){
	        error = OMX_ErrorInsufficientResources;
	        goto EXIT;
	    }
	    pInBuffer[nCounter] = pTemp;
	    pTemp = NULL;

            OMX_MALLOC_SIZE_DSPALIGN ( pTemp, pOutPortDef->nBufferSize, OMX_U8 );
	    if(pTemp == NULL){
	        error = OMX_ErrorInsufficientResources;
	        goto EXIT;
	    }
	    pOutBuffer[nCounter] = pTemp;
	}

	for (nCounter = 0; nCounter < NUM_OF_BUFFERSJPEG; nCounter++) {
	    error = OMX_UseBuffer(pHandle, &pInBuff[nCounter], nIndex1, (void*)&sJPEGEnc_CompID, pInPortDef->nBufferSize,pInBuffer[nCounter]);
	}
	for (nCounter = 0; nCounter < NUM_OF_BUFFERSJPEG; nCounter++) {
	    error = OMX_UseBuffer(pHandle, &pOutBuff[nCounter], nIndex2, (void*)&sJPEGEnc_CompID, pOutPortDef->nBufferSize,pOutBuffer[nCounter]);
	}

	if (imageinfo->bAPP1) {
		JPEG_APPTHUMB_MARKER sAPP1;
		sAPP1.bMarkerEnabled = OMX_TRUE;

		/* set JFIF marker buffer */
		sAPP1.nThumbnailWidth = imageinfo->nThumbnailWidth_app1;
		sAPP1.nThumbnailHeight = imageinfo->nThumbnailHeight_app1;

		sAPP1.nMarkerSize = ExifSize + 4;
		sAPP1.pMarkerBuffer = (OMX_U8*)pExifBuf;

		eError = OMX_GetExtensionIndex(pHandle, "OMX.TI.JPEG.encoder.Config.APP1", (OMX_INDEXTYPE*)&nCustomIndex);
		if ( eError != OMX_ErrorNone ) {
			printf("%d::APP_Error at function call: %x\n", __LINE__, eError);
			eError = OMX_ErrorUndefined;
			goto EXIT;
		}

		eError = OMX_SetConfig(pHandle, nCustomIndex, &sAPP1);
		if ( eError != OMX_ErrorNone ) {
			printf("%d::APP_Error at function call: %x\n", __LINE__, eError);
			eError = OMX_ErrorUndefined;
			goto EXIT;
		}
	}

	ALOGD("Waiting for OMX_StateIdle\n");
	error = WaitForState(pHandle, OMX_StateIdle);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("Error: JpegEncoder->WaitForState has timed out or failed - Idle %X\n",
	          error);
	    goto EXIT;
	}

	pQfactorType->nSize = sizeof(OMX_IMAGE_PARAM_QFACTORTYPE);
	pQfactorType->nQFactor = (OMX_U32) qualityfactor;
	pQfactorType->nVersion.s.nVersionMajor = 0x1;
	pQfactorType->nVersion.s.nVersionMinor = 0x0;
	pQfactorType->nVersion.s.nRevision = 0x0;
	pQfactorType->nVersion.s.nStep = 0x0;
	pQfactorType->nPortIndex = 0x0;

	error = OMX_GetExtensionIndex(pHandle, "OMX.TI.JPEG.encoder.Config.QFactor", (OMX_INDEXTYPE*)&nCustomIndex);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    goto EXIT;
	}
	error = OMX_SetConfig (pHandle, nCustomIndex, pQfactorType);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("%d::APP_Error at function call: %x\n", __LINE__, error);
	    goto EXIT;
	}

	error = OMX_SendCommand(pHandle,OMX_CommandStateSet, OMX_StateExecuting, NULL);
	if ( error != OMX_ErrorNone ) {
	    ALOGE (stderr,"Error from SendCommand-Executing State function\n");
	    goto EXIT;
	}
	pComponent = (OMX_COMPONENTTYPE *)pHandle;

	ALOGD("Waiting for OMX_StateExcecuting\n");
	error = WaitForState(pHandle, OMX_StateExecuting);
	if ( error != OMX_ErrorNone ) {
	    ALOGE("Error:  JpegEncoder->WaitForState has timed out or failed  - Executing %X\n", error);
	    goto EXIT;
	}

	done = 0;
	pComponent->GetState(pHandle, &state);

	for (nCounter =0; nCounter<1 /*NUM_OF_BUFFERSJPEG*/; nCounter++) {
		nRead = fill_data(pInBuff[nCounter], inputBuffer,pInPortDef->nBufferSize);
		pComponent->FillThisBuffer(pHandle, pOutBuff[nCounter]);
		pComponent->EmptyThisBuffer(pHandle, pInBuff[nCounter]);
		framesent++;
		if (pInBuff[nCounter]->nFlags == OMX_BUFFERFLAG_ENDOFFRAME)
		    break;
	}

	while ((error == OMX_ErrorNone) &&
	        (state != OMX_StateIdle)) {

	        if (bPreempted)
	        {
			ALOGD("Preempted - Forced tp Idle State - Waiting for OMX_StateIdle\n");
			error = WaitForState(pHandle, OMX_StateIdle);
			if ( error != OMX_ErrorNone ) {
				ALOGE("Error:  JpegEncoder->WaitForState has timed out or failed %X",  error);
				goto EXIT;
			}
			break;
	        }

	        fd_set rfds;
		sigemptyset(&set);
		sigaddset(&set,SIGALRM);

	        FD_ZERO(&rfds);
	        FD_SET(IpBuf_Pipe[0], &rfds);
	        FD_SET(OpBuf_Pipe[0], &rfds);
	        FD_SET(Event_Pipe[0], &rfds);
	        retval = pselect(fdmax+1, &rfds, NULL, NULL, NULL,&set);
	        if ( retval == -1 ) {
			ALOGE ( " : Error \n");
			break;
	        }
	        else if ( retval == 0 ) {
			ALOGE("App Timeout !!!!!!!!!!!\n");
	        }

	        if ( FD_ISSET(Event_Pipe[0], &rfds)) {

	            JPEGE_EVENTPRIVATE EventPrivate;
	            read(Event_Pipe[0], &EventPrivate, sizeof(JPEGE_EVENTPRIVATE));

	            switch(EventPrivate.eEvent) {
	                case OMX_EventError:
				if(bError) {
					error = WaitForState(pHandle, OMX_StateInvalid);
					if (error != OMX_ErrorNone) {
						ALOGE("APP:: Error:  JpegEncoder->WaitForState has timed out or failed %X\n", error);
						goto EXIT;
					}
					ALOGE("APP:: Component is in Invalid state now.\n");
					goto EXIT;
				}
				break;

	                case OMX_EventBufferFlag:
		                ALOGE("APP:: EOS flag received\n");
		                break;

	                default:
		                ALOGE("APP:: Non-error event rised. Event -> 0x%x\n", EventPrivate.eEvent);
		                break;
	            }
	        }

		if ( FD_ISSET(IpBuf_Pipe[0], &rfds) && !DEINIT_FLAG ) {

			/*read buffer */
			read(IpBuf_Pipe[0], &pBuffer, sizeof(pBuffer));

			/* re-fill this buffer with data from JPEG file */
			nRead = fill_data(pBuffer, inputBuffer,pInPortDef->nBufferSize);

			/* call EmptyThisBuffer (send buffer back to component */
			OMX_EmptyThisBuffer(pHandle, pBuffer);

			/*increment count */
			framesent++;
	        }

	        if (FD_ISSET(OpBuf_Pipe[0], &rfds)) {

			/* read buffer */
			read(OpBuf_Pipe[0], &pBuf, sizeof(pBuf));

			/* write data to a file, buffer is assumed to be emptied after this*/
			ALOGD("JPEG Encoded Length (%d)\n",pBuf->nFilledLen, nframerecieved);
			fileSize=(int)pBuf->nFilledLen;
			memcpy(outputBuffer,(void *)pBuf->pBuffer,fileSize);
			/*increment count and validate for limits; call FillThisBuffer */
			nframerecieved++;
			nRepeated++;
			if (nRepeated >= maxRepeat) {
				DEINIT_FLAG = 1;
			}
			else {
				ALOGD("Sending another output buffer\n");
				pComponent->FillThisBuffer(pHandle,pBuf);
			}
	        }

	        if (DEINIT_FLAG == 1) {
                    done = 1;
                    pHandle = (OMX_HANDLETYPE *) pComponent;
                    error = OMX_SendCommand(pHandle,OMX_CommandStateSet, OMX_StateIdle, NULL);
                    if ( error != OMX_ErrorNone ) {
                        fprintf (stderr,"APP:: Error from SendCommand-Idle(Stop) State function\n");
                        goto EXIT;
                    }

                    ALOGD("Waiting for OMX_StateIdle\n");
                    error = WaitForState(pHandle, OMX_StateIdle);
                    if ( error != OMX_ErrorNone ) {
                        ALOGE("Error:  JpegEncoder->WaitForState has timed out %X", error);
                        goto EXIT;
                    }

                    error = OMX_SendCommand(pHandle, OMX_CommandPortDisable, 0, NULL);
                    if ( error != OMX_ErrorNone ) {
                        ALOGE("Error from SendCommand-PortDisable function\n");
                        goto EXIT;
                    }
	        }

	        if (done == 1) {
                    error = pComponent->GetState(pHandle, &state);
                    if ( error != OMX_ErrorNone ){
                        ALOGE("Warning:  JpegEncoder->JPEGENC_GetState has returned status %X\n", error);
                        goto EXIT;
                    }
	        }
		ALOGD("JPEG Encoding Complete. Freeing OMX Handle.\n");
	}
EXIT:
	FREE(pPortParamType);
	FREE(pQfactorType);
	FREE(pInPortDef);
	FREE(pOutPortDef);
	FREE(imageinfo);
	FREE(pQuantizationTable);
	FREE(pHuffmanTable);

	sem_destroy(semaphore);
	free(semaphore) ;
    semaphore=NULL;

	if( error != OMX_ErrorNone){
		if (buffertype == 1) {
			for (nCounter = 0; nCounter < NUM_OF_BUFFERSJPEG; nCounter++) {
				FREE(pOutBuffer[nCounter]);
				FREE(pInBuffer[nCounter]);
			}
		}
		for (nCounter = 0; nCounter < NUM_OF_BUFFERSJPEG; nCounter++) {
		    error = OMX_FreeBuffer(pHandle, nIndex1, pInBuff[nCounter]);
		    if ( (error != OMX_ErrorNone) ) {
		        ALOGE ("Error in OMX_FreeBuffer: %d\n", __LINE__);
		    }
		    error = OMX_FreeBuffer(pHandle, nIndex2, pOutBuff[nCounter]);
		    if ( (error != OMX_ErrorNone) ) {
		        ALOGE ("Error in OMX_FreeBuffer: %d\n", __LINE__);
		    }
		}
	}

	error = TIOMX_FreeHandle(pHandle);
	if ( (error != OMX_ErrorNone) ) {
	    ALOGE ("Error in Free Handle function\n");
	}

	error = TIOMX_Deinit();
	if ( error != OMX_ErrorNone ) {
	    ALOGE("Error returned by OMX_DeInit()\n");
	}
    return fileSize;
}
