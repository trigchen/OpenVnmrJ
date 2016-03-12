/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/* stmObj.c 11.1 07/09/07  - Sum-To-Memory Object Source Modules */
/* 
 */

/* --------------------- stm.c ----------------------------- */

#define _POSIX_SOURCE /* defined when source is to be POSIX-compliant */
#include <vxWorks.h>
#include <stdlib.h>
#include <vme.h>
#include <semLib.h>
#include <rngLib.h>
#include <memLib.h>
#include <msgQLib.h>
#include "commondefs.h"
#include "logMsgLib.h"
#include "rngBlkLib.h"
#include "hashLib.h"
#include "hardware.h"
#include "vmeIntrp.h"
#include "taskPrior.h"
#include "stmObj.h"

/*
modification history
--------------------
7-23-93,gmb  created 
*/

/*
DESCRIPTION

  This module handles the creation, initialization of the STM Object.
The other public interfaces manage the data memory allocation from the
STM and pertinate data parameters.

  There are 3 VME interrupts generated by the STM and handled by this Object.
  In Order of priority, 1 being the highest priority

  1. Data Error, The Number of points acquired does not match
		  that requested.
  2. Max Transients Acquired, Ready for transfer to Host

  3. Data Acquired, Ready for transfer to Host

*/

static char *pStmIDStr ="STM Object";
static int  StmIdCnt = 0;
static char *StmStrtAddr = (char*) STM_MEM_BASE_ADR;
static int  StmSize = STM_MEM_2MB_END - STM_MEM_BASE_ADR;

SEM_ID pSemDataRdy;	/* counting semphore given by ISR to trigger IST */
SEM_ID pSemDataMax;
SEM_ID pSemDataErr;
/* static SEM_ID pSemDataRdy;	/* counting semphore given by ISR to trigger IST */
/*
static SEM_ID pSemDataMax;
static SEM_ID pSemDataErr;
*/
static RING_ID pStmAddr; /* STM address of data put here at ISR read by IST */

static  ITR_MSG itrMaxMsge;
static  ITR_MSG itrErrMsge;

/* just ignore the interrupt routine for now */

/***********************************************************
*
* itpDataRdy - Interrupt Service Routine for CT==NT, data Ready 
*
*/
static VOID
itpDataRdy(STMOBJ_ID pStmId)
{
    char* dataAddr;

    dataAddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
    msgQSend(pStmId->pMsgRdyLst, (char *)(&dataAddr),sizeof(dataAddr),
		NO_WAIT,MSG_PRI_NORMAL);
    /* semGive(pSemDataRdy); */
}

/***********************************************************
*
* itpDataMax - Interrupt Service Routine for Max Transient Accumulated 
*
*/
static VOID
itpDataMax(STMOBJ_ID pStmId)
{
    unsigned long readStmCT();

    itrMaxMsge.memaddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
    itrMaxMsge.count = readStmCT();
    msgQSend(pStmId->pMsgMaxLst, (char *)(&itrMaxMsge),sizeof(ITR_MSG),
		NO_WAIT,MSG_PRI_NORMAL);
    /* semGive(pSemDataMax); */
}

/***********************************************************
*
* itpDataErr - Interrupt Service Routine for FIFO FULL
*
*/
static VOID
itpDataErr(STMOBJ_ID pStmId)
{
    unsigned long readStmNP();

    itrErrMsge.memaddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
    itrErrMsge.count = readStmNP();
    msgQSend(pStmId->pMsgErrLst, (char *)(&itrErrMsge),sizeof(ITR_MSG),
		NO_WAIT,MSG_PRI_NORMAL);
    /* semGive(pSemDataErr); */
}


/*-------------------------------------------------------------
| Interrupt Service Tasks (IST) 
+--------------------------------------------------------------*/
#define COMPLETE 5

/***********************************************************
*
* dataReady - Interrupt Service Task 
*
* The Assumption Here is that FIDs are done in succession via page (or memory)
*     order within STM Memory, this does not necessarily mean by FID order
*     (is this right ?) I Think So 
*
*
* Actions to be taken:
* 1. Read STM Address from ringbuf (contain addr of data ready)
* 2. Translate address to FID elemid and/or STM page number
* 3. Put page number, elemid, and/or STM address onto ready rngbuf 
* 4. Change STM state to  DATA_RDY , unblock all task waiting on
*    this state variable. (semflush())
*
*/
static VOID
dataReady(STMOBJ_ID pStmId)
/* STMOBJ_ID pStmId - stm Object identifier */
{
   char* dataAddr;
   char* stmAddr;
   int index;
   register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */

   FOREVER
   {
      semTake(pSemDataRdy,WAIT_FOREVER);

   /* --- for testing this would normal be in interrupt routine */

      dataAddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
      msgQSend(pStmId->pMsgRdyLst, (char *)(&dataAddr),sizeof(dataAddr),
		NO_WAIT,MSG_PRI_NORMAL);

   /*--------------------------------------------------------- */
      msgQReceive(pStmId->pMsgRdyLst,(char *)&stmAddr,sizeof(char *),WAIT_FOREVER);

      index = (int) hashGet(pStmId->pHashStmAddr, stmAddr);

      fidElem = &(pStmId->pFidEntry[index]);
      fidElem->acqState = DATA_RDY;
      fidElem->pageState = COMPLETE; /* FREE,ACQ,Complete,Xfering */

      rngBufPut(pStmId->pRngRdyLst,(char*) &index,sizeof(index));

      semTake(pStmId->pStmMutex,WAIT_FOREVER);	/* take mutex on stuct */
      pStmId->stmState = DATA_RDY;
      semGive(pStmId->pStmMutex);	/* give mutex back */
      /* we may be preempted above but mutex is inversion safe so its OK */

      semFlush(pStmId->pSemStateChg); /* release any task Block on statchg for STM */
   }
}


/***********************************************************
*
* dataMaxTrans - Interrupt Service Task 
*
*  Action to take:
* Actions to be taken:
* 1. Read STM Address from ringbuf (contain addr of data ready)
* 2. Translate address to FID elemid and/or STM page number
* 3. Put page number, elemid, and/or STM address onto ready rngbuf 
* 4. Change STM state to  DATA_RDY_MAXTRANS, unblock all task waiting on
*    this state variable. (semflush())
*
*/
static VOID
dataMaxTrans(STMOBJ_ID pStmId)
/* STMOBJ_ID pStmId - stm Object identifier */
{
   unsigned long dataAddr;
   unsigned long stmAddr;
   register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */
   ITR_MSG maxMsg;
   long index;

    unsigned long readStmCT();

   FOREVER
   {
      semTake(pSemDataMax,WAIT_FOREVER);

   /* --- for testing this would normal be in interrupt routine */

/*
    itrMaxMsge.memaddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
    itrMaxMsge.count = readStmCT();
*/
    itrMaxMsge.memaddr = (char*) 0xa0020020;
    itrMaxMsge.count = 14;
    msgQSend(pStmId->pMsgMaxLst, (char *)(&itrMaxMsge),sizeof(ITR_MSG),
		NO_WAIT,MSG_PRI_NORMAL);

   /*--------------------------------------------------------- */

      msgQReceive(pStmId->pMsgMaxLst,(char *)&maxMsg,sizeof(ITR_MSG),WAIT_FOREVER);

      index = (long) hashGet(pStmId->pHashStmAddr, maxMsg.memaddr);

      fidElem = &(pStmId->pFidEntry[index]);

      fidElem->ct = maxMsg.count;	/* update ct count */
      fidElem->acqState = DATA_RDY_MAXTRANS;
      fidElem->pageState = COMPLETE; /* FREE,ACQ,Complete,Xfering */

      rngBufPut(pStmId->pRngRdyLst,(char*) &index,sizeof(index));

      semTake(pStmId->pStmMutex,WAIT_FOREVER);	/* take mutex on stuct */
      pStmId->stmState = DATA_RDY_MAXTRANS;
      semGive(pStmId->pStmMutex);	/* give mutex back */
      /* we may be preempted above but mutex is inversion safe so its OK */

      semFlush(pStmId->pSemStateChg);	/* release any task Block on statchg for STM */
   }
}


/***********************************************************
*
* dataDataError - Interrupt Service Task 
*
*  Action to take:
* 1. Read STM Address from ringbuf (contain addr of data in Error)
* 2. Translate address to FID elemid and/or STM page number
* 3. Put page number, elemid, and/or STM address onto Error parameter 
* 4. Get STM count, to set npOvrRun parameter to CTC under/over ran
* 5. Change STM state to  DATAERROR_NP, unblock all task waiting on
*    this state variable. (semflush())
*
*/
static VOID
dataDataError(STMOBJ_ID pStmId)
/* STMOBJ_ID pStmId - stm Object identifier */
{
   unsigned long dataAddr;
   unsigned long stmAddr;
   register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */
   ITR_MSG maxMsg;
   long index;
   unsigned long readStmNP();

   FOREVER
   {
      semTake(pSemDataErr,WAIT_FOREVER);

   /* --- for testing this would normal be in interrupt routine */

      itrErrMsge.memaddr = (char*) (*STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr));
      itrErrMsge.count = readStmNP();
      msgQSend(pStmId->pMsgErrLst, (char *)(&itrErrMsge),sizeof(ITR_MSG),
		NO_WAIT,MSG_PRI_NORMAL);

   /*--------------------------------------------------------- */

      msgQReceive(pStmId->pMsgErrLst,(char *)&stmAddr,sizeof(char *),WAIT_FOREVER);

      index = (long) hashGet(pStmId->pHashStmAddr, maxMsg.memaddr);

      fidElem = &(pStmId->pFidEntry[index]);
      fidElem->acqState = DATAERROR_NP;
      fidElem->pageState = COMPLETE; /* FREE,ACQ,Complete,Xfering */

      rngBufPut(pStmId->pRngRdyLst,(char*) &index,sizeof(index));

      semTake(pStmId->pStmMutex,WAIT_FOREVER);	/* take mutex on stuct */
      pStmId->stmState = DATAERROR_NP;
      pStmId->npOvrRun = maxMsg.count - fidElem->np;
      semGive(pStmId->pStmMutex);	/* give mutex back */
      /* we may be preempted above but mutex is inversion safe so its OK */

      semFlush(pStmId->pSemStateChg); /* release any task Block on statchg for STM */
   }
}

/**************************************************************
*
*  maxBufSize - determine the maximum system buffering that can be 
*  tolurated.  
*
*
* RETURNS:
* maxBufSIze  - if no error,
*
*/ 
static long maxBufSize(long np,long totalFidBlks)
{
   long fidBytes,stmSize;
   long maxLimit;
   long bytesPerMallocFidBlk;
   long maxFidBlksInSTM;
   long bytesPerFidBlk,  bytesPerMsgQ,  bytesPerRng,  bytesPerHash; 
   long maxMallocSize, maxStructSize;

   /* ----- What is the maximum number of FID blocks that can 
      fit into the STM memory?
      Note that there is an initial overhead of 16 bytes and 
      then 8 bytes for each allocation.
   */
   bytesPerMallocFidBlk = (np * BYTES_PER_NP) + ALLOC_OVERHEAD;
   stmSize = STM_MEM_2MB_END  - STM_MEM_BASE_ADR;
   maxFidBlksInSTM = (stmSize - PART_OVRHD) / bytesPerMallocFidBlk;

   /* ----- What is the maximum number of FID blocks stuctures 
      (FIDBLK_ENTRYs, MSG_Qs, RINGs, RINGBLKs, HASHs)
      that can be malloced with the self imposed malloc limit
      of the structures.
     Note: 3 MSG_Qs, 2 RINGs
   */
   bytesPerFidBlk = sizeof(FIDBLK_ELEMENT);
   bytesPerMsgQ = sizeof(long);
   bytesPerRng = sizeof(long);
   bytesPerHash = sizeof(HASH_ELEMENT);
   maxMallocSize = 1024 * 32; /* 32K */

   maxStructSize = maxMallocSize / ( bytesPerFidBlk + (bytesPerMsgQ * 3) + 
		   ( bytesPerRng * 2) + bytesPerHash );
  
   /* --- who's the limiting size maxStructSize or maxFidBlksInSTM ? --- */
   maxLimit = (maxStructSize < maxFidBlksInSTM) ? maxStructSize : maxFidBlksInSTM;

   /* --- who's the limiting size maxLimit or totalFidBlks ? --- */
   return( (maxLimit < totalFidBlks) ? maxLimit : totalFidBlks );
}


/*-------------------------------------------------------------
| STM Object Public Interfaces
+-------------------------------------------------------------*/
/**************************************************************
*
*  stmCreate - create the STM Object Data Structure & Semiphores, etc..
*
*
* RETURNS:
* STMOBJ_ID  - if no error, NULL - if mallocing or semiphore creation failed
*
*/ 

STMOBJ_ID stmCreate(char* baseAddr, int vector, int level, char* idstr)
/* char* baseAddr - base address of STM */
/* int   vector  - VME Interrupt vector number */
/* int   level   - VME Interrupt level */
/* char* idstr - user indentifier string */
{
  char tmpstr[80];
  STMOBJ_ID pStmObj;
  int tDRid, tMTid, tDEid;
  short sr;

  /* ------- malloc space for STM Object --------- */
  if ( (pStmObj = (STM_OBJ *) malloc( sizeof(STM_OBJ)) ) == NULL )
  {
    LOGMSG(ALL_PORTS,ERROR,"stmCreate: ");
    return(NULL);
  }

  /* zero out structure so we don't free something by mistake */
  memset(pStmObj,0,sizeof(STM_OBJ));

  /* ------ Translate Bus address to CPU Board local address ----- */
  if (sysBusToLocalAdrs(STM_VME_ACCESS_TYPE,
                         baseAddr,&(pStmObj->stmBaseAddr)) == -1)
  {
    LOGMSG(ALL_PORTS,ERROR,"stmCreate: ");
    free(pStmObj);
    return(NULL);
  }

  /* ------ Test for Boards Presents ---------- */
  if ( vxMemProbe((char*) (pStmObj->stmBaseAddr + STM_SR), 
		     VX_READ, WORD, &sr) == ERROR)
  { 
    LOGMSG(ALL_PORTS,OK,"stmCreate: Could not read STM's Status register\n");
    free(pStmObj);
    return(NULL);
  }
  else
  {
     pStmObj->stmBrdVersion = (sr >> 12) & 0x000f;
     LOGMSG1(ALL_PORTS,OK,"stmCreate: STM Board Version %d present.\n",
		pStmObj->stmBrdVersion);
  }

  /* ---- Write to DRAM Control Register to activated STM Memory access --- */
    *STM_REG(DRAMCNTL_CR,pStmObj->stmBaseAddr) = 0x01;

  if ( (vector < MIN_VME_ITRP_VEC) || (vector > MAX_VME_ITRP_VEC) )
  {
     LOGMSG3(ALL_PORTS,OK,"stmCreate: vector: 0x%x out of bounds (0x%x-0x%x)\n",
		vector,MIN_VME_ITRP_VEC,MAX_VME_ITRP_VEC);
     free(pStmObj);
     return(NULL);
  }
  else
  {
     pStmObj->vmeItrVector = vector;
  }

  if ( (level >= MIN_VME_ITRP_LEVEL) && (level <= MAX_VME_ITRP_LEVEL) )
  {
    pStmObj->vmeItrLevel = level;
  }
  else
  {
     LOGMSG3(ALL_PORTS,OK,"stmCreate: vme level: %d out of bounds (%d-%d)\n",
	level,MIN_VME_ITRP_LEVEL,MAX_VME_ITRP_LEVEL);
     free(pStmObj);
     return(NULL);
  }


  /* --------------  setup given or default ID string ---------------- */
  if (idstr == NULL) 
  {
     sprintf(tmpstr,"%s %d\n",pStmIDStr,++StmIdCnt);
     pStmObj->pStmIdStr = (char *) malloc(strlen(tmpstr)+2);
  }
  else
  {
     pStmObj->pStmIdStr = (char *) malloc(strlen(idstr)+2);
  }

  if (pStmObj->pStmIdStr == NULL)
  {
     stmDelete(pStmObj);
     return(NULL);
  }

  if (idstr == NULL) 
  {
     strcpy(pStmObj->pStmIdStr,tmpstr);
  }
  else
  {
     strcpy(pStmObj->pStmIdStr,idstr);
  }
  /* -------------------------------------------------------------------*/

  pStmObj->stmState = OK;
  pStmObj->npOvrRun = 0;
  pStmObj->pStmLocAdr = NULL;
  pStmObj->lastPage = 0;
  pStmObj->pFidEntry = NULL;
  pStmObj->pFidBlkFreeLst = NULL;
  pStmObj->pStmMemPool = NULL;
  pStmObj->pmemPoolAdr = NULL;
  pStmObj->memPoolSize = (unsigned long) NULL;
  pStmObj->pMsgRdyLst = NULL;
  pStmObj->pMsgMaxLst = NULL;
  pStmObj->pMsgErrLst = NULL;
  pStmObj->pFidBlkFreeLst = NULL;
  pStmObj->pRngRdyLst = NULL;
  pStmObj->pHashStmAddr = NULL;

  /* create the STM State sync semaphore */
  pStmObj->pSemStateChg = semBCreate(SEM_Q_FIFO,SEM_EMPTY);

  /* create the STM Object Mutual Exclusion semaphore */
  pStmObj->pStmMutex = semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE |
                                  SEM_DELETE_SAFE);

  if ( (pStmObj->pSemStateChg == NULL) ||
       (pStmObj->pStmMutex == NULL) )
  {
     LOGMSG(ALL_PORTS,ERROR,"stmCreate: Could not create semiphores ");
     stmDelete(pStmObj);
     return(NULL);
  }

  pSemDataRdy = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemDataMax = semCCreate(SEM_Q_FIFO,SEM_EMPTY);
  pSemDataErr = semCCreate(SEM_Q_FIFO,SEM_EMPTY);

  /* Hookup interrupt Routines */
/*
   if ( intConnect( INUM_TO_IVEC( STM_DATA_READY_ITRP_VEC ), 
		    itpDataRdy, pStmObj) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"stmCreate: Could not connect Data Ready itrp vector: ");
     stmDelete(pStmObj);
     return(NULL);
   }
   if ( intConnect( INUM_TO_IVEC( STM_MAX_TRANS_ITRP_VEC ), 
		    itpDataMax, pStmObj) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"stmCreate: Could not connect Max Trans itrp vector: ");
     stmDelete(pStmObj);
     return(NULL);
   }
   if ( intConnect( INUM_TO_IVEC( STM_DATA_ERROR_ITRP_VEC ), 
		    itpDataErr, pStmObj) == ERROR)
   {
     LOGMSG(ALL_PORTS,ERROR,"stmCreate: Could not connect Data Error itrp vector: ");
     stmDelete(pStmObj);
     return(NULL);
   }
*/

     /* testing the proper opration of the scheme */
/*
     intConnect( INUM_TO_IVEC( STM_DATA_READY_ITRP_VEC ), semGive, pSemDataRdy);
     intConnect( INUM_TO_IVEC( STM_MAX_TRANS_ITRP_VEC ), semGive, pSemDataMax);
     intConnect( INUM_TO_IVEC( STM_DATA_ERROR_ITRP_VEC ), semGive, pSemDataErr);
     sysIntEnable(4);	/* enable VME level 4 interrupts */

     /* Spawn the Interrupt Service Tasks */


/*
     tDRid = taskSpawn("tDatRdy",STM_DATRDY_IST_PRIORTY,STM_IST_TASK_OPTIONS,
		STM_IST_STACK_SIZE,dataReady,pStmObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);

     if ( tDRid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"stmCreate: could not spawn tDatRdy:");
        stmDelete(pStmObj);
        return(NULL);
     }

     tMTid = taskSpawn("tMaxtrns",STM_MAXTRN_IST_PRIORTY,STM_IST_TASK_OPTIONS,
		STM_IST_STACK_SIZE,dataMaxTrans,pStmObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);

     if ( tMTid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"stmCreate: could not spawn tMaxtrns:");
        stmDelete(pStmObj);
        return(NULL);
     }

     tDEid = taskSpawn("tDatErr",STM_DATERR_IST_PRIORTY,STM_IST_TASK_OPTIONS,
		STM_IST_STACK_SIZE,dataDataError,pStmObj,ARG2,
		ARG3,ARG4,ARG5,ARG6,ARG7,ARG8,ARG9,ARG10);

     if ( tDEid == ERROR)
     {
        LOGMSG(ALL_PORTS,ERROR,"stmCreate: could not spawn tDatErr:");
        stmDelete(pStmObj);
        return(NULL);
     }
*/

   return( pStmObj );
}


/**************************************************************
*
*  stmInitial - Initializes the STM Object, based on the experiment
*
*
*  This routine initializes the STM Object base on the experimental
*parameters. The parameters <np> is the smallest np for the
*experiment and <totalFidBlks> are the number of fid blocks to acquire. 
*Note: <totalFidBlks> is NOT the number of FID Elements but instead
*the number of FID elements times the number of blocksizes (nt/bs). 
*E.G. arraydim=10,nt=32 & bs=8 then <totalFidBlks> = 10 * (32/8) = 40
*
*
* RETURNS:
*  OK or ERROR
*
*	Author Greg Brissey 8/5/93
*/
int stmInitial(STMOBJ_ID pStmId,long np, long totalFidBlks, PFL hashFunc, PFI cmpFunc)
/* STMOBJ_ID 	pStmId - stm Object identifier */
/* np	 	Number of Data points */
/* totalFidBlks Number of FID BLocks to be acquired */
/* PFL hashFunc Hashing function for STM address hash table */
/* PFI cmpFunc  Comparison function for STM address hash table */
{
   long fidBytes;
   long maxNumFidBlkToAlloc;
   int  msgQSize;
   long i;
   FIDBLK_ENTRY *blkarray; /* array of FID Block Entries */
   register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */

   extern long maxBufSize();

   fidBytes = (np * BYTES_PER_NP) + ALLOC_OVERHEAD;

   maxNumFidBlkToAlloc = maxBufSize(np, totalFidBlks);

   pStmId->maxFidBlkBuffered = maxNumFidBlkToAlloc;

   msgQSize = maxNumFidBlkToAlloc * sizeof(long);

   if (sysBusToLocalAdrs(STM_MEM_VME_ACCESS_TYPE,
		         StmStrtAddr,&(pStmId->pStmLocAdr)) == -1)
   {
      LOGMSG1(ALL_PORTS,ERROR,"stmInitial: STM Memory at 0x%lx",StmStrtAddr);
      return(ERROR);
   }

   /* Note to myself: since blocking is done by freelist then maybe a single
      partition can be create outside this object and all use this partition ?
      Since there is no memPartDestroy function ..
   */
   /* 
      Create a memory pool consisting of the STM memory so that VxWorks 
      can handle the memory management, Note that there is an initial over
      head of 16 bytes and then 8 bytes for each allocation.
   */
   pStmId->pStmMemPool = (PART_ID) memPartCreate(pStmId->pStmLocAdr,
			(maxNumFidBlkToAlloc * fidBytes) + PART_OVRHD);

   pStmId->pmemPoolAdr = pStmId->pStmLocAdr;	
   pStmId->memPoolSize = ((maxNumFidBlkToAlloc * fidBytes) + PART_OVRHD);	

   /* create space for all FID Block Entries */
   pStmId->pFidEntry = (FIDBLK_ENTRY *) 
		      malloc( (maxNumFidBlkToAlloc * sizeof(FIDBLK_ENTRY)) );  

   /*
      create the msg Q for the Addresses read from STM that are the
      FID Blocks that are READY for Transfer back to the Host
   */
   pStmId->pMsgRdyLst = msgQCreate(maxNumFidBlkToAlloc,sizeof(char *),MSG_Q_FIFO);
   pStmId->pMsgMaxLst = msgQCreate(maxNumFidBlkToAlloc,sizeof(ITR_MSG),MSG_Q_FIFO);
   pStmId->pMsgErrLst = msgQCreate(5,sizeof(ITR_MSG),MSG_Q_FIFO);

   /*
      Create the hash table that will map the STM address read to the
      index into the array of FIDBLK_ENTRY data for that FID Block
   */
   pStmId->pHashStmAddr = 
	hashCreate(maxNumFidBlkToAlloc,hashFunc,cmpFunc,"STM Object");

   /*
      Create the Ring Buffer of Fid Blk Entries In the Free List 
   */
   pStmId->pFidBlkFreeLst = 
	rngBlkCreate(msgQSize,"STM FIDBLK Entry Index Free List");

   
   if ( (pStmId->pStmMemPool == NULL) || 
        (pStmId->pMsgRdyLst == NULL) || 
        (pStmId->pFidBlkFreeLst == NULL) || 
        (pStmId->pHashStmAddr == NULL) )
   {
     stmFreeAllRes(pStmId);
     LOGMSG(ALL_PORTS,ERROR,"stmInitial: A resource could not be Allocated");
     return(ERROR);
   }
   else
   {
     /*
	Initialize the FID Block Structures
     */
     blkarray = pStmId->pFidEntry;
     fidElem = &(blkarray[0]);
     for(i=0;i<maxNumFidBlkToAlloc;i++,fidElem++)
     {
       rngBlkPut(pStmId->pFidBlkFreeLst,&i,sizeof(i));
       fidElem->pageState = FREE; /* FREE,ACQ,Complete,Xfering */
       fidElem->elemAddr = NULL;	/* FID # */
       fidElem->elemId = 0L;	/* FID # */
       fidElem->startct = 0L;	
       fidElem->np = 0L;
       fidElem->nt = 0L;
       fidElem->ct = 0L;
       fidElem->acqState = NO_DATA;
     }
     return( OK );
   }
}

/**************************************************************
*
*  stmFreeAllRes - Frees all resources allocated in stmInitial
*
*
* RETURNS:
*  OK or ERROR
*
*	Author Greg Brissey 9/9/93
*/
int stmFreeAllRes(STMOBJ_ID pStmId)
/* STMOBJ_ID 	pStmId - stm Object identifier */
{
  if ( pStmId != NULL)
  {
   if (pStmId->pStmMemPool != NULL)
      memPartDestroy(pStmId->pStmMemPool);

   if (pStmId->pFidEntry != NULL)
      free(pStmId->pFidEntry);

   if (pStmId->pMsgRdyLst != NULL)
      msgQDelete(pStmId->pMsgRdyLst);

   if (pStmId->pMsgMaxLst != NULL)
      msgQDelete(pStmId->pMsgMaxLst);

   if (pStmId->pMsgErrLst != NULL)
      msgQDelete(pStmId->pMsgErrLst);

   if (pStmId->pHashStmAddr != NULL)
      hashDelete(pStmId->pHashStmAddr);

   if (pStmId->pFidBlkFreeLst != NULL)
      rngBlkDelete(pStmId->pFidBlkFreeLst);

   pStmId->stmState = OK;
   pStmId->npOvrRun = 0;
   pStmId->pStmLocAdr = NULL;
   pStmId->lastPage = 0;
   pStmId->pFidEntry = NULL;
   pStmId->pFidBlkFreeLst = NULL;
   pStmId->pStmMemPool = NULL;
   pStmId->pmemPoolAdr = NULL;
   pStmId->memPoolSize = (unsigned long) NULL;
   pStmId->pMsgRdyLst = NULL;
   pStmId->pMsgMaxLst = NULL;
   pStmId->pMsgErrLst = NULL;
   pStmId->pFidBlkFreeLst = NULL;
   pStmId->pRngRdyLst = NULL;
   pStmId->pHashStmAddr = NULL;
  }

  return(OK);
}
/**************************************************************
*
*  stmDelete - Deletes STM Object and  all resources
*
*
* RETURNS:
*  OK or ERROR
*
*	Author Greg Brissey 9/9/93
*/
int stmDelete(STMOBJ_ID pStmId)
/* STMOBJ_ID 	pStmId - stm Object identifier */
{
   if (pStmId != NULL)
   {
      stmFreeAllRes(pStmId);
      if (pStmId->pMsgErrLst != NULL)
         free(pStmId->pStmIdStr);
      if (pStmId->pSemStateChg != NULL)
         semDelete(pStmId->pSemStateChg);
      if (pStmId->pStmMutex != NULL)
         semDelete(pStmId->pStmMutex);
/*
      if (pSemDataRdy != NULL)
         semDelete(pSemDataRdy);
      if (pSemDataMax != NULL)
         semDelete(pSemDataMax);
      if (pSemDataErr != NULL)
         semDelete(pSemDataErr);
*/
      free(pStmId);
   }
}
/**************************************************************
*
*  stmGetState - Obtains the current STM Status
*
*  This routines Obtains the status of the STM via 3 different modes.
*
*   NO_WAIT - return the present value immediately.
*   WAIT_FOREVER - waits till the STM Status has changed 
*			and and returns this new value.
*   TIME_OUT - waits till the STM Status has changed or 
*		    the number of <secounds> has elasped 
*		    (timed out) before returning.
*
*  NOTE: The Task that calls this routine with 
*	 STM_WAIT_FOREVER or STM_TIME_OUT will block !!
*     
*
*
* RETURNS:
* STM state - if no error, TIME_OUT - if in STM_TIME_OUT mode call timed out
*	      or ERROR if called with invalid mode;
*
*/ 

int stmGetState(STMOBJ_ID pStmId, int mode, int secounds)
{
   int state;

   if (pStmId == NULL)
     return(ERROR);

   switch(mode)
   {
     case NO_WAIT:
          state = pStmId->stmState;
	  break;

     case WAIT_FOREVER: /* block if state has not changed */
	  semTake(pStmId->pSemStateChg, WAIT_FOREVER);  
          state = pStmId->stmState;
	  break;

     case TIME_OUT:     /* block if state has not changed, until timeout */
          if ( semTake(pStmId->pSemStateChg, (sysClkRateGet() * secounds) ) != OK )
	     state = TIME_OUT;
          else 
             state = pStmId->stmState;
          break;

     default:
	  state = ERROR;
	  break;
   }
   return(state);
}

#define ACQ 2

/**************************************************************
*
*  stmAllocAcqBlk - Obtains the next free STM memory block 
*
*  This routines Obtains the next free STM memory block for
*data acquisition. The space is malloc from the STM area, a hashed
*database of important FID block parameters is maintained. Which is
*used later for data transfer back to host.
*  The <fid_element> is that FID that this acquisition data block belongs.
*since there will be more than one block per FID if blocksize (bs) is not zero.
*  The <strtCt> is the logical starting ct for this data block.  Although each
*data block starts at zero completed transients the logical start transient may 
*not be zero. When bs is not zero this will be the case. For an experiment
*where bs=8 nt=32, each FID has 4 data blocks to acquire (nt/bs), thus the logical
*starting transient for the data blocks are: 1st - 0, 2nd - 9, 3rd - 17, 4th - 25.
*This allows the host to blocksize average properly.
*
*  RETURNS
*    STM Memory Block Address, or ERROR
*/
char* stmAllocAcqBlk(STMOBJ_ID pStmId, long fid_element, long np, long strtCt, long nt)
/* pStmId 	- stm Object Id */
/* fid_element - FID that this acquisition data block belongs */
/* np	       - Number of data points of data */
/* strtCt      - The logical starting transient of this data block */
/* nt	       - The number of transients to acquire for this block */
{
  long index;
  FIDBLK_ENTRY *fidarray; /* array of FID Entries */
  register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */
  char*	mem;


     /* 
       Get the first avialiable free FIDBLK_ELEMENT 
	Note: if no free FIDBLK_ELEMENT then this call will block.
     */
     rngBlkGet(pStmId->pFidBlkFreeLst,&index, sizeof(index));

     fidarray = pStmId->pFidEntry;
     fidElem = &(fidarray[index]);

     fidElem->pageState = ACQ; /* FREE,ACQ,Complete,Xfering */
     fidElem->elemId = fid_element;	/* FID # */
     fidElem->startct = strtCt;	
     fidElem->np = np;
     fidElem->nt = nt;
     fidElem->ct = nt;
     fidElem->acqState = NO_DATA;

     mem = (char*) memPartAlignedAlloc(pStmId->pStmMemPool,(np * BYTES_PER_NP));
     fidElem->elemAddr = mem ; /* STM address of FID Data */
     if ( mem == NULL ) /* got a big problem */
     {
        LOGMSG(ALL_PORTS,ERROR,
	   "stmAllocAcqBlk: Could not malloc space for given Free Block.\n");
        LOGMSG(ALL_PORTS,ERROR,
	   "                Should NEVER Happen: Free List Error or freeing Error");
        LOGMSG(ALL_PORTS,ERROR,
	   "                Complete System Crash Eminent !!!!!");
        return( NULL );
     }

     hashPut((HASH_ID) pStmId->pHashStmAddr,(char*) mem,(char*) index);

   return( fidElem->elemAddr );
}



/**************************************************************
*
*  stmGetNxtFid - gets the next ready data block  entry information 
*
*  This routines Obtains the next completed data block. Copies it into
*the pointer to a FIDBLK_ENTRY strcuture.
*
*  RETURNS
*    OK  or ERROR
*/
int stmGetNxtFid(STMOBJ_ID pStmId,FIDBLK_ENTRY *pFidBlk)
/* pStmId - stm Object Id */ 
/* pFidBlk - pointer to fid entry struct to filled in */
{
   int index;
   char* stmMemAdr;
   FIDBLK_ENTRY *blkarray; /* array of FID Entries */

   if (rngBufGet(pStmId->pRngRdyLst,(char*) &index,sizeof(index)) == 0)
     return(ERROR);

   /* msgQReceive(pStmId->pStmRdyList,&stmMemAdr,sizeof(stmMemAdr)); */
   /* index = hashGet(pStmId->pHashStmAddr, stmMemAdr); */

   blkarray = pStmId->pFidEntry;
   memcpy(pFidBlk,&(blkarray[index]),sizeof(FIDBLK_ENTRY));
   return( OK );
}

/**************************************************************
*
*  stmFreeFidBlk - Frees all STM resources associated with the FID Block
*
*  This routines Frees the STM resources associated with a FID Block
* reference thought its index. This includes:
*  1. removing the FID BLock reference from the Hash Table
*  2. Freeing the memory that was allocated from the STM for the FID Block
*  3. Clearing the FID BLock element structure
*  4. Place the index back into the FID Block Free List
*
* NOTE: A Task calling this routine could block if the FID 
*	Block Free List is full.
*
*  RETURNS
*    OK  or ERROR
*/
int stmFreeFidBlk(STMOBJ_ID pStmId,char* memAdr)
/* pStmId - stm Object Id */ 
/* index - index of the FID Block */
{
  int index;
  FIDBLK_ENTRY *fidarray; /* array of FID Entries */
  register FIDBLK_ENTRY *fidBlk; /* pointer to FID Entry */

  index = (int) hashGet(pStmId->pHashStmAddr, memAdr);
  if ( index == ERROR )
  {
     return(ERROR);
  }

  fidarray = pStmId->pFidEntry;
  fidBlk = &(fidarray[index]);

  hashRemove((HASH_ID) pStmId->pHashStmAddr,(char*) fidBlk->elemAddr);
  memPartFree(pStmId->pStmMemPool,fidBlk->elemAddr);  /* Free STM memory for FID BLock */
  fidBlk->pageState = FREE; /* FREE,ACQ,Complete,Xfering */
  fidBlk->elemAddr = NULL;	/* FID # */
  fidBlk->elemId = 0L;	/* FID # */
  fidBlk->startct = 0L;	
  fidBlk->np = 0L;
  fidBlk->nt = 0L;
  fidBlk->ct = 0L;
  fidBlk->acqState = NO_DATA;
  /* put index back into Free List */
  rngBlkPut(pStmId->pFidBlkFreeLst,&index,sizeof(index));
  return( OK );
}

/********************************************************************
* stmShow - display the status information on the STM Object
*
*  This routine display the status information of the STM Object
*
*
*  RETURN
*   VOID
*
*/
VOID stmShow(STMOBJ_ID pStmId, int level)
/* STMOBJ_ID pStmId - STM Object ID */
/* int level 	   - level of information */
{
   FIDBLK_ENTRY *blkarray; /* array of FID Entries */
   register FIDBLK_ENTRY *fidElem; /* pointer to FID Entry */
   int i;

   printf("\n\n-------------------------------------------------------------\n\n");
   printf("STM Object (0x%lx: %s\n",pStmId->stmBaseAddr, pStmId->pStmIdStr);

   printf("\nFID Block Free List:\n");
   rngBlkShow(pStmId->pFidBlkFreeLst, 0);

   printf("\nFID Block STM MEMORY allocation:\n");
   memPartShow(pStmId->pStmMemPool,level);

   printf("\nHash STM Memory to Block index table: \n");
   hashShow(pStmId->pHashStmAddr,level);

   printf("\nSTM State Semiphore: \n");
   semShow(pStmId->pSemStateChg,level);

   printf("\nSTM Mutex Semiphore: \n");
   semShow(pStmId->pStmMutex,level);

   printf("\nSTM VME interrupt Message Queues: \n");
   printf("\nDATA ACQUIRED OK: \n");
   msgQShow(pStmId->pMsgRdyLst,level);
   printf("Max Transients: \n");
   msgQShow(pStmId->pMsgMaxLst,level);
   printf("Data Error: \n");
   msgQShow(pStmId->pMsgErrLst, level);

   blkarray = pStmId->pFidEntry;
   printf("\nFID Blocks allocated for Acquisition: \n");
   for( i=0; i < pStmId->maxFidBlkBuffered; i++)
   {
     if (blkarray[i].elemId != 0L)
     {
       printf("Elem: %ld, Addr: 0x%lx, PageState: %d, AcState: %d \n", blkarray[i].elemId,
			blkarray[i].elemAddr,blkarray[i].pageState,blkarray[i].acqState);
       printf("Start CT: %ld, np: %ld, NT: %ld, CT: %ld \n",blkarray[i].startct,
			blkarray[i].np, blkarray[i].nt, blkarray[i].ct);
     }
   }
}
/*****************************************************
*
* memPartDestroy - Delete a memory partition created w/ memPartCreate
*
*  1. from start address & size bzero memory
*  2. free the pointer to PART_ID
*  3. Hope that this works.
*
*/
memPartDestroy(STMOBJ_ID pStmId)
/* STMOBJ_ID pStmId */
{
   memset(pStmId->pmemPoolAdr,0,pStmId->memPoolSize); /* clear memory */
   free(pStmId->pStmMemPool);	/* PART_ID structure */
}

/*******************************************
*
* readStmAddr - Reads the value of the STM address register 
*
* RETURNS:
*  char* Address 
*
* NOMANUAL
*/
char* readStmAddr(STMOBJ_ID pStmId)
{
   register char* addr;

    addr = (char*) *STM_REGL(STM_ADDR_LONG,pStmId->stmBaseAddr);
    return(addr);
}
/*******************************************
*
* readStmCT - Reads the value of the STM CT count register 
*
* RETURNS:
*  unsigned long count
*
* NOMANUAL
*/
unsigned long readStmCT(STMOBJ_ID pStmId)
{
   register unsigned long cnt;
    cnt = ( *STM_REG(STM_CT_BYTE3,pStmId->stmBaseAddr) ) << 16;
    cnt += ( *STM_REG(STM_CT_BYTE2,pStmId->stmBaseAddr) ) << 8;
    cnt += *STM_REG(STM_CT_BYTE1,pStmId->stmBaseAddr);
    return(cnt);
}
/*******************************************
*
* readStmNP - Reads the value of the STM NP count register 
*
* RETURNS:
*  unsigned long count
*
* NOMANUAL
*/
unsigned long readStmNP(STMOBJ_ID pStmId)
{
   register unsigned long cnt;
    cnt = ( *STM_REG(STM_NP_BYTE3,pStmId->stmBaseAddr) ) << 16;
    cnt += ( *STM_REG(STM_NP_BYTE2,pStmId->stmBaseAddr) ) << 8;
    cnt += *STM_REG(STM_NP_BYTE1,pStmId->stmBaseAddr);
    return(cnt);
}
/*******************************************
*
* readStmSS - Reads the value of the STM SS count register 
*
* RETURNS:
*  unsigned long count
*
* NOMANUAL
*/
unsigned long readStmSS(STMOBJ_ID pStmId)
{
   register unsigned long cnt;
    cnt += ( *STM_REG(STM_SS_BYTE2,pStmId->stmBaseAddr) ) << 8;
    cnt += *STM_REG(STM_SS_BYTE1,pStmId->stmBaseAddr);
    return(cnt);
}
