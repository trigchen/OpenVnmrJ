/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/* 
 */
/*
DESCRIPTION

  This Task  handles the FID Data UpLoad to the Host computer.
  It Waits on a msgQ which is written into via STM interrupt routine
and other functions. Then writes that FID Statblock & Data up to the 
host via channel interface.

*/

#define _POSIX_SOURCE /* defined when source is to be POSIX-compliant */
#include <vxWorks.h>
#include <stdioLib.h>
#include <semLib.h>
#include <msgQLib.h>
#include "hardware.h"
#include "hostAcqStructs.h"
#include "hostMsgChannels.h"
#include "expDoneCodes.h"
#include "logMsgLib.h"
#include "stmObj.h"

#include <vme.h>
#include <drv/vme/vmechip2.h> 
#include "dma.h"

/* criteria for SA, EXP_FID_CMPLT, BS_CMPLT, IL_CMPLT, CT_CMPLT */
extern int SA_Criteria; 
extern unsigned long SA_Mod; /* modulo for SA, ie which fid to stop at 'il' & ct*/
extern unsigned long SA_CTs;  /* Completed Transients for SA */
extern SEM_ID  pSemSAStop; /* Binary  Semaphore used to Stop upLinker for SA */

extern MSG_Q_ID pMsgesToPHandlr;
extern EXCEPTION_MSGE GenericException;
extern STATUS_BLOCK   currentStatBlock;

extern STMOBJ_ID pTheStmObject;	/* STM Object */

static char	msg_norm[ACQ_UPLINK_XFR_MSG_SIZE+4];
static char	msg_term[ACQ_UPLINK_XFR_MSG_SIZE+4];
static char	msg_acqi[ACQ_UPLINK_XFR_MSG_SIZE+4];
static char	msg_noda[ACQ_UPLINK_XFR_MSG_SIZE+4];

/*  This is a buffer which lets the uplinker program transfer the data out of 
    the DTM before sending it to the host computer.  It is declared a long word
    buffer because it was found that 4 byte trandfers from the DTM work best.	*/

static long	dtmXferBuffer[ XFR_SIZE / sizeof( long ) ];

static int 	upLnkrIsCon = 0;

/*  The copyDtm2Buffer could include a for loop to delay transfers even more
    than that resulting from the transfer loop itself.  It is declared to be
    a global instead of a static so as to be accessable from the VxWorks shell.  */

#define  DELAYVAL	1
int	delayval = DELAYVAL;

int  dma_it = 1;


void
copyDtm2Buffer( long *dtmaddr, int count )
{
	volatile int	delayer;
	int		iter;

	for (iter = 0; iter < count; iter++) {
		dtmXferBuffer[ iter ] = *dtmaddr++;
		for (delayer = 0; delayer < delayval; delayer++) ;
	}
}

/* called went communications is lost with Host */
/* restart both the uplinker and the interactive uplinker */

restartUpLnk()
{
   int tid;
   DPRINT(0,"restartUpLnk:");
   if ( upLnkrIsCon == 1)
   {
      DPRINT(0,"taskRestart of tUpLink:");
      upLnkrIsCon = 0;
      if ((tid = taskNameToId("tUpLink")) != ERROR)
        taskRestart(tid);
      if ((tid = taskNameToId("tUpLinkI")) != ERROR)
        taskRestart(tid);
   }
}

startUpLnk(int priority, int taskoptions, int stacksize)
{
    int uplink();
    extern STMOBJ_ID pTheStmObject;/* STM Object */
    extern MSG_Q_ID pUpLinkMsgQ;/* MsgQ used between UpLinker and STM Object */

   /* Start UpLinker */
   if (taskNameToId("tUpLink") == ERROR)
     taskSpawn("tUpLink",priority,taskoptions,stacksize,uplink,pUpLinkMsgQ,
		pTheStmObject, 3,4,5,6,7,8,9,10);
}

killUpLnk()
{
   int tid;
   if ((tid = taskNameToId("tUpLink")) != ERROR)
	taskDelete(tid);
   upLnkrIsCon = 0;
}

static void
uplinkNonInteractive(
	STMOBJ_ID pStmId,
	int chan_no,
	long Tag,
	FID_STAT_BLOCK *pStatBlk,
	long *dataAddr,
	int stat
)
{
     int bytes,Stop;
     register unsigned long count,block;
     register char *dataptr;

     Stop = 0;

     if (Tag >= 0L)   /* Tag will be -1 on SA, AA, etc... */
     {

/*
       DPRINT3(-1,
		"=======> FID: %ld, CT: %ld, data Addr: 0x%lx  <==============\n",
		pStatBlk->elemId,pStatBlk->ct,dataAddr);
*/

       DPRINT5(0,
       "Uploading Tag: %ld, FID: %ld, CT: %ld, doneCode: %d, errorCOde: %d\n",
	    Tag,pStatBlk->elemId,pStatBlk->ct,pStatBlk->doneCode,
		pStatBlk->errorCode);
	DPRINT4(3,
       		"FID: %ld, CT: %ld, NP: %ld, fid Size: %ld\n",
                pStatBlk->elemId,pStatBlk->ct, pStatBlk->np,
                pStatBlk->dataSize);
      /* if connection broken will send msge to phandler & suspend task */
       bytes = phandlerWriteChannel(chan_no,msg_norm,ACQ_UPLINK_XFR_MSG_SIZE);
#ifdef XXXX
       /* bytes = writeChannel(chan_no,msg_norm,ACQ_UPLINK_XFR_MSG_SIZE); */
       if (bytes < 0)
       {
         DPRINT(1,"\nupLinker: Host Closed Connection to Console.\n");
         errLogSysRet(LOGIT,debugInfo,
	  "Uplink:FID: %ld, ct: &ld, write of sync Msge failed",
	    pStatBlk->elemId,pStatBlk->ct);
         GenericException.exceptionType = LOST_CONN;
         GenericException.reportEvent = LOST_CONN;
       /* send Lost Connection to exception handler task, it knows what to do */
       msgQSend(pMsgesToPHandlr, (char*) &GenericException, 
				sizeof(EXCEPTION_MSGE), 
					NO_WAIT, MSG_PRI_URGENT);
         return;
       }
#endif


      /* if connection broken will send msge to phandler & suspend task */
       bytes = phandlerWriteChannel(chan_no,pStatBlk, sizeof(FID_STAT_BLOCK));
       /* bytes = rWriteChannel(chan_no,pStatBlk, sizeof(FID_STAT_BLOCK)); */
       if (bytes < 0)
       {
         errLogSysRet(LOGIT,debugInfo,
	  "Uplink:FID: %ld, ct: &ld, write of StatBlock failed",
	    pStatBlk->elemId,pStatBlk->ct);
         return;
       }

       dataptr = (char*) dataAddr;
       count = pStatBlk->dataSize;
       block = XFR_SIZE;

       DPRINT(2,"Data Xfr:\n");
       if (!dma_it || (dataptr < (char*) 0xF0000000 /*STM_MEM_BASE_ADR4*/))  /* i.e. not HS STM/ADC */
       {
	 /* --------------  Copy data PIO for DTM  --------------- */
         while (count > 0)
         {
	  if (count < block)
	    block = count;
          DPRINT5(2,"Address: 0x%lx (%lu), Xfrsize: 0x%lx (%lu) Left: %lu\n",
		dataptr,dataptr,block,block,count);


          copyDtm2Buffer( (long *) dataptr, block / sizeof( long ) );

          /* if connection broken will send msge to phandler & suspend task */
          bytes = phandlerWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block );
          /* bytes = rWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block ); */
          if (bytes < 0)
          {
            errLogSysRet(LOGIT,debugInfo,
	   "Uplink:FID: %ld, ct: &ld, at byte %ld write of Data failed",
	     pStatBlk->elemId,pStatBlk->ct,pStatBlk->dataSize - count);
            return;
          }
	  dataptr += block;
	  count -= block;
	 }
	 /* --------------------------------------------------------------- */
        }
        else
        {
	  /* --------------  Copy data DMA for HS STM/ADC  --------------- */
          while (count > 0)
          {

	   if (count < block)
	     block = count;
           DPRINT5(2,"DMA - Address: 0x%lx (%lu), Xfrsize: 0x%lx (%lu) Left: %lu\n",
		dataptr,dataptr,block,block,count);

           /* DPRINT3(0,"bcopyDMA D32: 0x%lx, 0x%lx, %lu\n",dataptr,dtmXferBuffer,block); */
           stat = bcopyDMA(dataptr,dtmXferBuffer, block, VME_AM_EXT_SUP_DATA, VME_TO_LOCAL, 1);

           /* if connection broken will send msge to phandler & suspend task */
           bytes = phandlerWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block );
           /* bytes = rWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block ); */
           if (bytes < 0)
           {
             errLogSysRet(LOGIT,debugInfo,
	    "Uplink:FID: %ld, ct: &ld, at byte %ld write of Data failed",
	      pStatBlk->elemId,pStatBlk->ct,pStatBlk->dataSize - count);
             return;
           }
	   dataptr += block;
	   count -= block;
          }
	  /* --------------------------------------------------------------- */
        }

       /* If SA criteria meet then do the right thing */
       /* if true 1. send msge to phandler,  */
       /*         2. take a semaphore that will pend this process */  
       if (SA_Criteria)
       {
         if (checkSA(pStatBlk))
         {
	   /* send STOP to exception handler task, it knows what to do */
	   DPRINT(1,"UpLInker: send msge to Phandler to stop\n");
	   msgQSend(pMsgesToPHandlr, (char*) &GenericException, 
			sizeof(EXCEPTION_MSGE), NO_WAIT, MSG_PRI_URGENT);
           /* now stop in my tracks, phandler will release me */
           DPRINT(1,"upLinker: SA pending\n");
           semTake(pSemSAStop,WAIT_FOREVER);  /* stop uplinker */
           DPRINT(1,"upLinker: SA released\n");
         }
       }

       stmFreeFidBlk(pStmId,Tag);
     } 
     else
     {
       /* 
       ** if Tag is -1 we got a SA or AA or Halt
       ** for end of data flag from aa,ra,reset,local reset
       */
       DPRINT2(1,"HALT/SA/AA Tag: %ld, doneCode: %d\n",Tag,pStatBlk->doneCode);
       bytes = phandlerWriteChannel(chan_no,msg_term,ACQ_UPLINK_XFR_MSG_SIZE);
       /* bytes = rWriteChannel(chan_no,msg_term,ACQ_UPLINK_XFR_MSG_SIZE); */
       if (bytes < 0)
       {
          errLogSysRet(LOGIT,debugInfo,
	     "Uplink: write of abort sync Msge failed");
          return;
       }
       /* if connection broken will send msge to phandler & suspend task */
       bytes = phandlerWriteChannel(chan_no,pStatBlk,sizeof(FID_STAT_BLOCK));
       /* bytes = rWriteChannel(chan_no,pStatBlk,sizeof(FID_STAT_BLOCK)); */
       if (bytes < 0)
       {
          errLogSysRet(LOGIT,debugInfo,
	     "Uplink: write of exception StatBlock failed");
          return;
       }
     }
}

/* check if condition matches the interrupt type, however end-of-scan is */
/* is special since it is used for two type of sa, end-of-scan and  */
/* end-of-phase_cycling by use of the modulo number 'stp_phsmod',   */
/* end-of-scan has a modulo number of 1.                             */
/* The end-of-interleave_cycle may only occur at an end-of-fid       */
/* condition.                                                        */
checkSA(FID_STAT_BLOCK *pStatBlk)
{
  int stop = 0;
  DPRINT3(0,">>> SA_Criteria (%d) == doneCode (%d), SA_mod: %lu <<<<<<\n",
		SA_Criteria,pStatBlk->doneCode,SA_Mod);
   /* if BS or FID CMPLT & !CT CMPLT then SA, or if CT_CMPLT & modulo CT 
      then SA, or BS but realy at FID CMPLT then SA
   */
   if ( (((SA_Criteria == pStatBlk->doneCode) && (SA_Criteria != CT_CMPLT)) ||
        ((SA_Criteria == CT_CMPLT) && (!(SA_CTs % SA_Mod)) ) ||
        ((SA_Criteria == BS_CMPLT) && (pStatBlk->doneCode == EXP_FID_CMPLT)))
	&& (pStatBlk->elemId != 0) )
   {
      /* NOTE: SA Type can be alter to indicate when the stop occurred     */
      /*   e.g., if eos happens at bs then type is change to eob type	*/
     DPRINT(1,"SA criteria for FID, BS or CT meet\n");
     GenericException.exceptionType = STOP_CMPLT;
     if ( (pStatBlk->doneCode == EXP_FID_CMPLT) || 
	  (pStatBlk->doneCode == BS_CMPLT)
        )
     {
       GenericException.reportEvent = pStatBlk->doneCode;
     }
     else
     {
       GenericException.reportEvent = CT_CMPLT;
     }
     stop = 1;
   }

#ifdef XXX
   if (SA_Criteria == EXP_HALTED)
   {
     DPRINT(1,"EXP Halt  criteria meet\n");
     GenericException.exceptionType = EXP_HALTED;
     GenericException.reportEvent = EXP_HALTED;
     stop = 1;
   }
#endif

   if ( ((pStatBlk->doneCode == EXP_FID_CMPLT) || 
	(pStatBlk->doneCode == BS_CMPLT)) &&
	(pStatBlk->elemId != 0) &&
         (SA_Criteria == IL_CMPLT) && ( !(pStatBlk->elemId % SA_Mod) ) )
   {
     DPRINT(0,"SA criteria for IL meet\n");
     GenericException.exceptionType = STOP_CMPLT;
     GenericException.reportEvent = IL_CMPLT;
     stop = 1;
   }

  return(stop);
}

uplink(MSG_Q_ID pMsgsFromStm,STMOBJ_ID pStmId)
{
   ITR_MSG itrmsge;
   int	bytes;
   int 	chan_no;
   long    Tag;
   FID_STAT_BLOCK *pStatBlk;
   long *dataAddr;
   int stat;
   ulong_t count,block;
   
   chan_no = RECVPROC_CHANNEL;

   strcpy(msg_norm,ACQ_UPLINK_MSG);
   strcpy(msg_term,ACQ_UPLINK_ENDDATA_MSG);
   DPRINT(1,"Uplink: Establish Connection to Host.\n");
   EstablishChannelCon(chan_no);
   upLnkrIsCon = 1;
   DPRINT(1,"Uplink: Connection Established.\n");

/*  Task lets msgQReceive block it until a message is delivered.
    The message represents its cue.  Each message is expected to
    have a tag, an index into the STM bufs.  A tag of -1 implies
    the message was delivered as the result of an abort, halt or
    stop operation.  No data is delivered in this situation,
    just the prologue and the FID stat block.  The latter should
    tell the program in the host computer not to expect any data.	*/

   DPRINT(1,"Uplink:Server LOOP Ready & Waiting.\n");
   FOREVER
   {
     msgQReceive(pMsgsFromStm,(char*) &itrmsge,sizeof(ITR_MSG), WAIT_FOREVER);
     /* Tag = stmGetNxtFid(pStmId,&itrmsge,&pStatBlk,&dataAddr,&stat); */
     Tag = stmGetNxtFid(pTheStmObject,&itrmsge,&pStatBlk,&dataAddr,&stat);

/* The console takes a scan at the start of each experiment to establish
   the noise level.  The stat block associated with this scan has its
   element ID set to 0.							*/

     switch (itrmsge.msgType) {
	case INTERRUPT_OCCURRED:
		  currentStatBlock.stb.AcqFidCnt = (pStatBlk->elemId > 0) ?
							pStatBlk->elemId : 1;
		  /* uplinkNonInteractive( pStmId, chan_no, Tag, pStatBlk, dataAddr, stat ); */
		  uplinkNonInteractive( pTheStmObject, chan_no, Tag, pStatBlk, dataAddr, stat );
		break;

	default:
		  errLogSysRet(LOGIT,debugInfo,
		      "Uplink received unknown message: %d\n", itrmsge.msgType
		  );
		break;
     }
   } 
   closeChannel( chan_no );
}


/*
  This Task handles interactive Data UpLoad to the Host computer.
*/

static struct {
	FID_STAT_BLOCK	fidStatBlk;
	long		npParam;
	long		lastNpCnt;
	int		bytesPerPt;
	int		lastTag;
} uliData;				/* UpLink Interactive Data */

startUpLnkI(int priority, int taskoptions, int stacksize)
{
	int uplinkI();

	extern STMOBJ_ID	pTheStmObject;			 /* STM Object */
	extern MSG_Q_ID		pUpLinkIMsgQ;		  /* MsgQ used between */
					/* interactive UpLinker and STM Object */
   /* Start UpLinker */

	if (taskNameToId("tUpLinkI") == ERROR)
	  taskSpawn("tUpLinkI", priority, taskoptions, stacksize, uplinkI,
		     pUpLinkIMsgQ, pTheStmObject, RECVPROC_CHANNEL, 4, 5, 6, 7, 8, 9, 10);
}

killUpLnkI()
{
   int tid;
   if ((tid = taskNameToId("tUpLinkI")) != ERROR)
	taskDelete(tid);
}

static void
setupUplinkerI( STMOBJ_ID pStmId, int curTag )
{
	FID_STAT_BLOCK	*pStatBlk;

	if (curTag < 0)		/* This is not supposed to happen */
	  return;			    /* but let's be sure. */

	pStatBlk = stmTag2StatBlk( pStmId, curTag );
	uliData.npParam = pStatBlk->np;
	uliData.bytesPerPt = pStatBlk->dataSize / pStatBlk->np;
}

void
resetUplinkerI()
{
	uliData.lastTag = NO_TAG;
	uliData.lastNpCnt = 0;		/* np count of 0 ==> completed scan */
	uliData.npParam = -1;
	uliData.bytesPerPt = -1;
	strcpy(msg_acqi,ACQ_UPLINK_INTERA_MSG);
	strcpy(msg_noda,ACQ_UPLINK_NODATA_MSG);
}

static long
uplinkIfidStatBlk( STMOBJ_ID pStmId, ITR_MSG *pItrMsge, long curNpCnt )
{
	char		*dataAddr;
	int		 dataOffset;
	FID_STAT_BLOCK	*pStatBlk;

	uliData.fidStatBlk.doneCode = pItrMsge->donecode;
	uliData.fidStatBlk.errorCode = pItrMsge->errorcode;

	if (pItrMsge->tag < 0) {
		uliData.fidStatBlk.elemId = 0;
		uliData.fidStatBlk.np = 0;
		uliData.fidStatBlk.dataSize = 0;
		uliData.fidStatBlk.fidAddr = (long *) NULL;
	}
	else {
		if (uliData.lastTag != pItrMsge->tag) {
			pStatBlk = stmTag2StatBlk( pStmId, pItrMsge->tag );
			uliData.fidStatBlk.elemId = pStatBlk->elemId;
			uliData.lastNpCnt = uliData.npParam;
			pStatBlk->ct = 0;
			if ((pItrMsge->donecode == CT_CMPLT) || 
				(pItrMsge->donecode == EXP_FID_CMPLT))
			{
			  pStatBlk->ct = pStatBlk->nt - pItrMsge->count;
			}
			uliData.fidStatBlk.ct = pStatBlk->ct;
		}

		uliData.fidStatBlk.np = uliData.lastNpCnt - curNpCnt;
		uliData.fidStatBlk.dataSize = uliData.fidStatBlk.np * uliData.bytesPerPt;
		dataAddr = (char *) stmTag2DataAddr( pStmId, pItrMsge->tag );
		dataOffset = (uliData.npParam - uliData.lastNpCnt) * uliData.bytesPerPt;
		dataAddr += dataOffset;
		uliData.fidStatBlk.fidAddr = (long *) dataAddr;
	}

	return( uliData.fidStatBlk.np );
}

static void
uplinkInteractive(
	STMOBJ_ID pStmId,
	int chan_no,
	ITR_MSG *pItrmsge
)
{
	int		 bytes;
	int		 stat;
	int		 Tag;
	long		*dataAddr;
	ulong_t		 count,block;
	register char	*dataptr;
	FID_STAT_BLOCK	*pStatBlk;

	Tag = pItrmsge->tag;
	dataAddr = uliData.fidStatBlk.fidAddr;
	pStatBlk = &uliData.fidStatBlk;

        DPRINT3(1,"uplinkInteractive Tag: %ld, doneCode: %d, Fid Addr: 0x%lx\n",
		Tag,pStatBlk->doneCode,dataAddr);

	if (Tag < 0) 
	{
             /* 
             ** if Tag is -1 we got a SA or AA or Halt
             */
             /* if connection broken will send msge to phandler & suspend task */
             bytes = phandlerWriteChannel(chan_no,msg_term,ACQ_UPLINK_XFR_MSG_SIZE);
             /* bytes = rWriteChannel(chan_no,msg_term,ACQ_UPLINK_XFR_MSG_SIZE); */
             if (bytes < 0)
             {
                errLogSysRet(LOGIT,debugInfo,
	           "Uplink: write of abort sync Msge failed");
                return;
             }
             /* if connection broken will send msge to phandler & suspend task */
             bytes = phandlerWriteChannel(chan_no,pStatBlk,sizeof(FID_STAT_BLOCK));
             /* bytes = rWriteChannel(chan_no,pStatBlk,sizeof(FID_STAT_BLOCK)); */
             if (bytes < 0)
             {
                errLogSysRet(LOGIT,debugInfo,
	           "Uplink: write of exception StatBlock failed");
                return;
             }
	    DPRINT1(0, "uplink interactive data send received tag of %d\n", Tag );
	    return;
	}

        /* if connection broken will send msge to phandler & suspend task */
	bytes = phandlerWriteChannel( chan_no, msg_acqi, ACQ_UPLINK_XFR_MSG_SIZE );
	/* bytes = rWriteChannel( chan_no, msg_acqi, ACQ_UPLINK_XFR_MSG_SIZE ); */
	if (bytes < 0) {
		errLogSysRet(LOGIT,debugInfo,
	   "Uplink:FID: %ld, ct: &ld, write of sync Msge failed",
	    pStatBlk->elemId,pStatBlk->ct);
		return;
	}

        /* if connection broken will send msge to phandler & suspend task */
	bytes = phandlerWriteChannel(chan_no,pStatBlk, sizeof(FID_STAT_BLOCK));
	/* bytes = rWriteChannel(chan_no,pStatBlk, sizeof(FID_STAT_BLOCK)); */
	if (bytes < 0) {
		errLogSysRet(LOGIT,debugInfo,
	   "Uplink:FID: %ld, ct: &ld, write of StatBlock failed",
	    pStatBlk->elemId,pStatBlk->ct);
		return;
	}

	dataptr = (char*) dataAddr;
	count = pStatBlk->dataSize;
	block = XFR_SIZE;
        DPRINT2(1,"uplinkInteractive: data addr: 0x%lx, size: %d (bytes)\n",dataptr,count);
	while (count > 0) {
		if (count < block)
		  block = count;
		DPRINT5(2,"Address: 0x%lx (%lu), Xfrsize: 0x%lx (%lu) Left: %lu\n",
			   dataptr,dataptr,block,block,count);
		copyDtm2Buffer( (long *) dataptr, block / sizeof( long ) );
      	        /* if connection broken will send msge to phandler & suspend task */
		bytes = phandlerWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block );
		/* bytes = rWriteChannel( chan_no, &dtmXferBuffer[ 0 ], block ); */
		if (bytes < 0) {
			errLogSysRet(LOGIT,debugInfo,
		   "Uplink:FID: %ld, ct: &ld, at byte %ld write of Data failed",
		    pStatBlk->elemId,pStatBlk->ct,pStatBlk->dataSize - count);
			return;
		}
		dataptr += block;
		count -= block;
	}


    if ((stat | RTZ_DATA_CMPLT) == 0)
      DPRINT(0, "Warning:  RTZ_DATA_CMPLT bit NOT SET in interactive uplinker status\n" );
}

static
send_nodata( int chan_no )
{
	int	bytes;

        /* if connection broken will send msge to phandler & suspend task */
	bytes = phandlerWriteChannel( chan_no, msg_noda, ACQ_UPLINK_XFR_MSG_SIZE );
	/* bytes = rWriteChannel( chan_no, msg_noda, ACQ_UPLINK_XFR_MSG_SIZE ); */
}

/*  The interactive uplinker gets its channel number from its argument list.
    Currently it shares the recvproc channel; later it may get its own channel
    so data can be sent simultaneously interactively and non-interactively.  The
    application is "FIDmonitor", where data from a non-interactive acquisition
    is to be shown in ACQI.

    A separate application, FIDscope, requires that the interactive uplinker
    track the current tag in the STM, the current NP parameter, and the last
    NP count.

    When asked to start FIDscope (START_INTERVALS), the task stops waiting
    forever for a message and allows the message receive program to time out.
    If this time-out occurs, then it sends up any available data.  The system
    needs to be prepared for the case that no data is available.  It gets the
    current tag and the current NP count from the STM.  It compares the current
    tag with the one it saved, so it can determine if this is a new buffer or
    a continuation of the buffer accessed the last time it sent data.  In the
    latter case it uses the last NP count to establish how much data may be
    sent and where to start the transfer.  Finally it needs the NP parameter
    value to help it determine where to start, since the NP count register in
    the STM is a countdown register; that is, it holds 0 when a scan is complete.

    The interactive uplinker uses setupUplinkerI to establish the current np
    parameter value.  This program is called if its own value for the current
    np is negative.  The latter situation implies the interactive uplinker has
    been reset, something which is done by resetUplinkerI.  The latter program
    is called when the done code in the message the interactive uplinker is
    EXP_ABORTED.

           *** Thus an interactive uplink stops when and only when ***
           ***       the interactive experiment is aborted         ***

    January 22, 1996								*/

uplinkI( MSG_Q_ID pMsgsFromStm, STMOBJ_ID pStmId, int chan_no )
{
	ITR_MSG itrmsge;
	int	curTag;
	int	ival, waitArg;
	long	curNpCnt;
	long	pts2Send;
	long	pts2Offset;

	uliData.lastTag = NO_TAG;
	uliData.lastNpCnt = 0;			/* np count of 0 ==> completed scan */
	uliData.npParam = -1;
	uliData.bytesPerPt = -1;
	strcpy(msg_acqi,ACQ_UPLINK_INTERA_MSG);
	strcpy(msg_noda,ACQ_UPLINK_NODATA_MSG);

/*  This is a mess, but what can you do... */

	if (chan_no != RECVPROC_CHANNEL) {
		DPRINT(1,"UplinkI: Establish Connection to Host.\n");
		EstablishChannelCon(chan_no);
		DPRINT(1,"UplinkI: Connection Established.\n");
	}
	DPRINT1(2,"UplinkI: Server LOOP Ready & Waiting for message on 0x%x\n",
		   pMsgsFromStm);

	waitArg = WAIT_FOREVER;
	FOREVER {
		ival = msgQReceive(pMsgsFromStm,(char*) &itrmsge,sizeof(ITR_MSG), waitArg);
		if (ival == ERROR) {
			if (errno == S_objLib_OBJ_TIMEOUT)
			  itrmsge.msgType = SEND_DATA_NOW;
			else
			  continue;
		}

		switch (itrmsge.msgType) {
		  case INTERRUPT_OCCURRED:
			  if (uliData.npParam < 0 && itrmsge.tag >= 0) 
      			  {
				/* setupUplinkerI( pStmId, itrmsge.tag ); */
				setupUplinkerI( pTheStmObject, itrmsge.tag );
			  }

			  if (itrmsge.tag < 0) {
			  	pts2Send = 0;
				pts2Offset = 0;
			  }
			  else if (uliData.lastTag != itrmsge.tag) {
			  	pts2Send = uliData.npParam;
				pts2Offset = 0;
			  }
			  else {
				pts2Send = uliData.lastNpCnt;
				pts2Offset = uliData.npParam - uliData.lastNpCnt;
			  }
			  DPRINT2( 1, "interrupt occurred: %d, %d\n",
				       pts2Send, pts2Offset );
			  /* uplinkIfidStatBlk( pStmId, &itrmsge, 0 ); */
			  /* uplinkInteractive( pStmId, chan_no, &itrmsge ); */
			  uplinkIfidStatBlk( pTheStmObject, &itrmsge, 0 );
			  uplinkInteractive( pTheStmObject, chan_no, &itrmsge );
			  /* stmFreeFidBlk(pStmId,itrmsge.tag); */
			  stmFreeFidBlk(pTheStmObject,itrmsge.tag);
			  if (itrmsge.donecode == EXP_ABORTED) {
				resetUplinkerI();
				waitArg = WAIT_FOREVER;
			  }
			  else {
			  	uliData.lastTag = NO_TAG;
			  	uliData.lastNpCnt = 0;
			  }
			break;

		  case SEND_DATA_NOW:
			  /* itrmsge.tag = stmTagReg( pStmId ); */
			  itrmsge.tag = stmTagReg( pTheStmObject );
			  if (itrmsge.tag < 0) {		/* I don't think this can */
				break;			    /* happen, but let's be sure. */
			  }
			  /* curNpCnt = stmNpCntReg( pStmId ); */
			  curNpCnt = stmNpCntReg( pTheStmObject );

		/*  Next test guards against a misread NP register.  For obscure
		    reasons, the Mercury/VX STM receives 2 additions CTC.  Thus
		    the NP register can go down to -2.  Unfortunately, this
		    register can't be read by an atomic instruction; only one
		    byte at a time, with three bytes total.  If it changes in the
		    midst of this, from 0 to -1, the value cound be (for example)
		    0xFFFE or 65534.  Now this erronous value will be larger than
		    the previous value, which is the essential cue, since it is a
		    countdown register.  Thus we can make a reliable test for
		    this situation and deal with it accordingly.  January 1998	*/

			  if (itrmsge.tag == uliData.lastTag && curNpCnt > uliData.lastNpCnt)
			    curNpCnt = 0;
			  if (uliData.npParam < 0) {
				setupUplinkerI( pTheStmObject, itrmsge.tag );
			  }

		/*  Following branch happens if, on the previous iteration, the scan
		    "wrapped around", or, if the system was caught after that scan
		    completed but before the next one started.  On this iteration,
		    we start transferring data at the beginning of the STM data
		    buffer.  Recall lastNpCnt is a count-down value.			*/

			  else if (uliData.lastNpCnt <= 0)
			    uliData.lastNpCnt = uliData.npParam;

		/*  Next branch is taken if a new scan is starting ...  */

			  if (uliData.lastTag != itrmsge.tag) {
				pts2Send = uliData.npParam - curNpCnt;
				pts2Offset = 0;
			  }

		/*  Send an update from the current scan.

		    Now the scan may "wrap around", that is, the scan completes
		    and then starts again in the same STM buffer.  Currently
		    this only happens during signal averaging with FID scope on.
		    In this case the current NP count will be larger (recall
		    this is a count-down value) than the last NP count.  If this
		    occurs, the rest of the just-completed scan will now be
		    transferred.  None of the new scan will be transferred
		    will be transferred this time - that will happen next time.  */

			  else {
				pts2Offset = uliData.npParam - uliData.lastNpCnt;
				if (uliData.lastNpCnt < curNpCnt) {
				    curNpCnt = 0;
				    pts2Send = uliData.lastNpCnt;
				}
				else {
				    pts2Send = uliData.lastNpCnt - curNpCnt;
				}
			  }
			  DPRINT2( 1, "send data now: %d, %d\n",
				       pts2Send, pts2Offset );
			  /* uplinkIfidStatBlk( pStmId, &itrmsge, curNpCnt ); */
			  uplinkIfidStatBlk( pTheStmObject, &itrmsge, curNpCnt );
			  if (pts2Send > 0)
			    uplinkInteractive( pTheStmObject, chan_no, &itrmsge );
			  uliData.lastTag = itrmsge.tag;

			  uliData.lastNpCnt = curNpCnt;

			break;

		  case DTM_COMPLETE:
			  resetUplinkerI();
			break;

		  case START_INTERVALS:
			  waitArg = 1;
			break;

		  case STOP_INTERVALS:
			  waitArg = WAIT_FOREVER;
			break;

		  default:
			  errLogSysRet(LOGIT,debugInfo,
		      "UplinkI received unknown message: %d\n", itrmsge.msgType
			  );
			break;
		}
	}
}

resetSA()
{
     SA_Criteria = 0;
     SA_Mod = 0L;

     /* give semi thus unblocking stuffing */
     semGive(pSemSAStop);	

     /* Now Attempt to take it when, when it would block that
        is the state we want it in.
     */
     while (semTake(pSemSAStop,10) != ERROR);	
}
