/*  Copyright 2003-2006 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/
/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file scu.c
    \brief SCU emulation functions.
*/

#include <stdlib.h>
#include "scu.h"
#include "debug.h"
#include "memory.h"
#include "sh2core.h"
#include "yabause.h"
#include <inttypes.h>

#ifdef OPTIMIZED_DMA
# include "cs2.h"
# include "scsp.h"
# include "vdp1.h"
# include "vdp2.h"
#endif

Scu * ScuRegs;
scudspregs_struct * ScuDsp;
scubp_struct * ScuBP;
static int incFlg[4] = { 0 };
static void ScuTestInterruptMask(void);

void ScuRemoveInterruptByCPU(u32 pre, u32 after);

//#define DSPLOG

#ifdef DSPLOG
static FILE * slogp = NULL;
#endif

//#define LOG
#define OLD_DMA 0
//////////////////////////////////////////////////////////////////////////////

int ScuInit(void) {
   int i;

   if ((ScuRegs = (Scu *) calloc(1, sizeof(Scu))) == NULL)
      return -1;

   memset(&ScuRegs->dma0, 0, sizeof(ScuRegs->dma0));
   memset(&ScuRegs->dma1, 0, sizeof(ScuRegs->dma1));
   memset(&ScuRegs->dma2, 0, sizeof(ScuRegs->dma2));
   
   if ((ScuDsp = (scudspregs_struct *) calloc(1, sizeof(scudspregs_struct))) == NULL)
      return -1;

   if ((ScuBP = (scubp_struct *) calloc(1, sizeof(scubp_struct))) == NULL)
      return -1;

   for (i = 0; i < MAX_BREAKPOINTS; i++)
      ScuBP->codebreakpoint[i].addr = 0xFFFFFFFF;
   ScuBP->numcodebreakpoints = 0;
   ScuBP->BreakpointCallBack=NULL;
   ScuBP->inbreakpoint=0;
   
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void ScuDeInit(void) {
   if (ScuRegs)
      free(ScuRegs);
   ScuRegs = NULL;

   if (ScuDsp)
      free(ScuDsp);
   ScuDsp = NULL;

   if (ScuBP)
      free(ScuBP);
   ScuBP = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void ScuReset(void) {
   ScuRegs->D0AD = ScuRegs->D1AD = ScuRegs->D2AD = 0x101;
   ScuRegs->D0EN = ScuRegs->D1EN = ScuRegs->D2EN = 0x0;
   ScuRegs->D0MD = ScuRegs->D1MD = ScuRegs->D2MD = 0x7;
   ScuRegs->DSTP = 0x0;
   ScuRegs->DSTA = 0x0;

   ScuDsp->ProgControlPort.all = 0;
   ScuRegs->PDA = 0x0;

   ScuRegs->T1MD = 0x0;

   ScuRegs->IMS = 0xBFFF;
   ScuRegs->IST = 0x0;

   ScuRegs->AIACK = 0x0;
   ScuRegs->ASR0 = ScuRegs->ASR1 = 0x0;
   ScuRegs->AREF = 0x0;

   ScuRegs->RSEL = 0x0;
   ScuRegs->VER = 0x04; // Looks like all consumer saturn's used at least version 4

   ScuRegs->timer0 = 0;
   ScuRegs->timer1 = 0;

   ScuRegs->dma0_time = 0;
   ScuRegs->dma1_time = 0;
   ScuRegs->dma2_time = 0;


   memset((void *)ScuRegs->interrupts, 0, sizeof(scuinterrupt_struct) * 30);
   ScuRegs->NumberOfInterrupts = 0;

   memset(&ScuRegs->dma0, 0, sizeof(ScuRegs->dma0));
   memset(&ScuRegs->dma1, 0, sizeof(ScuRegs->dma1));
   memset(&ScuRegs->dma2, 0, sizeof(ScuRegs->dma2));

}

//////////////////////////////////////////////////////////////////////////////

#ifdef OPTIMIZED_DMA

// Table of memory types for DMA optimization, in 512k (1<<19 byte) units:
//    0x00 = no special handling
//    0x12 = VDP1/2 RAM (8-bit organized, 16-bit copy unit)
//    0x22 = M68K RAM (16-bit organized, 16-bit copy unit)
//    0x23 = VDP2 color RAM (16-bit organized, 16-bit copy unit)
//    0x24 = SH-2 RAM (16-bit organized, 32-bit copy unit)
static const u8 DMAMemoryType[0x20000000>>19] = {
   [0x00200000>>19] = 0x24,
   [0x00280000>>19] = 0x24,
   [0x05A00000>>19] = 0x22,
   [0x05A80000>>19] = 0x22,
   [0x05C00000>>19] = 0x12,
   [0x05C00000>>19] = 0x12,
   [0x05E00000>>19] = 0x12,
   [0x05E80000>>19] = 0x12,
   [0x05F00000>>19] = 0x23,
   [0x06000000>>19] = 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
                      0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
};

// Function to return the native pointer for an optimized address
#ifdef __GNUC__
__attribute__((always_inline))  // Force it inline for better performance
#endif
static INLINE void *DMAMemoryPointer(u32 address) {
   u32 page = (address & 0x1FF80000) >> 19;
   switch (DMAMemoryType[page]) {
      case 0x12:
         switch (page) {
            case 0x05C00000>>19: return &Vdp1Ram[address & 0x7FFFF];
            case 0x05E00000>>19: // fall through
            case 0x05E80000>>19: return &Vdp2Ram[address & 0x7FFFF];
            default: return NULL;
         }
      case 0x22:
         return &SoundRam[address & 0x7FFFF];
      case 0x23:
         return &Vdp2ColorRam[address & 0xFFF];
      case 0x24:
         if (page == 0x00200000>>19) {
            return &LowWram[address & 0xFFFFF];
         } else {
            return &HighWram[address & 0xFFFFF];
         }
      default:
         return NULL;
   }
}

#endif  // OPTIMIZED_DMA

static void DoDMA(u32 ReadAddress, unsigned int ReadAdd,
                  u32 WriteAddress, unsigned int WriteAdd,
                  u32 TransferSize)
{
  LOG("DoDMA src=%08X,dst=%08X,size=%d, ra:%d/wa:%d flame=%d:%d\n", ReadAddress, WriteAddress, TransferSize, ReadAdd, WriteAdd, yabsys.frame_count,yabsys.LineCount );
   if (ReadAdd == 0) {
      // DMA fill
      // Is it a constant source or a register whose value can change from
      // read to read?
      int constant_source = ((ReadAddress & 0x1FF00000) == 0x00200000)
                         || ((ReadAddress & 0x1E000000) == 0x06000000)
                         || ((ReadAddress & 0x1FF00000) == 0x05A00000)
                         || ((ReadAddress & 0x1DF00000) == 0x05C00000);

      if ((WriteAddress & 0x1FFFFFFF) >= 0x5A00000
            && (WriteAddress & 0x1FFFFFFF) < 0x5FF0000) {
         // Fill a 32-bit value in 16-bit units.  We have to be careful to
         // avoid misaligned 32-bit accesses, because some hardware (e.g.
         // PSP) crashes on such accesses.
         if (constant_source) {
            u32 counter = 0;
            u32 val;
            if (ReadAddress & 2) {  // Avoid misaligned access
               val = MappedMemoryReadWord(ReadAddress,NULL) << 16
                   | MappedMemoryReadWord(ReadAddress+2, NULL);
            } else {
               val = MappedMemoryReadLong(ReadAddress, NULL);
            }
            while (counter < TransferSize) {
               MappedMemoryWriteWord(WriteAddress, (u16)(val >> 16), NULL);
               WriteAddress += WriteAdd;
               MappedMemoryWriteWord(WriteAddress, (u16)val, NULL);
               WriteAddress += WriteAdd;
               counter += 4;
            }
         } else {
            u32 counter = 0;
            while (counter < TransferSize) {
               u32 tmp = MappedMemoryReadLong(ReadAddress, NULL);
               MappedMemoryWriteWord(WriteAddress, (u16)(tmp >> 16), NULL);
               WriteAddress += WriteAdd;
               MappedMemoryWriteWord(WriteAddress, (u16)tmp, NULL);
               WriteAddress += WriteAdd;
               ReadAddress += ReadAdd;
               counter += 4;
            }
         }
      }
      else {
         // Fill in 32-bit units (always aligned).
         u32 start = WriteAddress;
         if (constant_source) {
            u32 val = MappedMemoryReadLong(ReadAddress, NULL);
            u32 counter = 0;
            while (counter < TransferSize) {
               MappedMemoryWriteLong(WriteAddress, val, NULL);
               ReadAddress += ReadAdd;
               WriteAddress += WriteAdd;
               counter += 4;
            }
         } else {
            u32 counter = 0;
            while (counter < TransferSize) {
               MappedMemoryWriteLong(WriteAddress,
                                     MappedMemoryReadLong(ReadAddress, NULL), NULL);
               ReadAddress += ReadAdd;
               WriteAddress += WriteAdd;
               counter += 4;
            }
         }
         // Inform the SH-2 core in case it was a write to main RAM.
         SH2WriteNotify(start, WriteAddress - start);
      }

   }

   else {
      // DMA copy
      // Access to B-BUS?
      if ( ((WriteAddress & 0x1FFFFFFF) >= 0x5A00000  && (WriteAddress & 0x1FFFFFFF) < 0x5FF0000) ) {
         // Copy in 16-bit units, avoiding misaligned accesses.
         u32 counter = 0;
         if (ReadAddress & 2) {  // Avoid misaligned access
            u16 tmp = MappedMemoryReadWord(ReadAddress, NULL);
            MappedMemoryWriteWord(WriteAddress, tmp, NULL);
            WriteAddress += WriteAdd;
            ReadAddress += 2;
            counter += 2;
         }
         if (TransferSize >= 3)
         {
            while (counter < TransferSize-2) {
               u32 tmp = MappedMemoryReadLong(ReadAddress, NULL);
               MappedMemoryWriteWord(WriteAddress, (u16)(tmp >> 16), NULL);
               WriteAddress += WriteAdd;
               MappedMemoryWriteWord(WriteAddress, (u16)tmp, NULL);
               WriteAddress += WriteAdd;
               ReadAddress += 4;
               counter += 4;
            }
         }
         if (counter < TransferSize) {
            u16 tmp = MappedMemoryReadWord(ReadAddress, NULL);
            MappedMemoryWriteWord(WriteAddress, tmp, NULL);
            WriteAddress += WriteAdd;
            ReadAddress += 2;
            counter += 2;
         }
      }
      else if (((ReadAddress & 0x1FFFFFFF) >= 0x5A00000 && (ReadAddress & 0x1FFFFFFF) < 0x5FF0000)) {
        u32 counter = 0;
        while (counter < TransferSize) {
          u16 tmp = MappedMemoryReadWord(ReadAddress, NULL);
          MappedMemoryWriteWord(WriteAddress, tmp, NULL);
          WriteAddress += (WriteAdd>>1);
          ReadAddress += 2;
          counter += 2;
        }
      }
      else {
         u32 counter = 0;
         u32 start = WriteAddress;
         while (counter < TransferSize) {
            MappedMemoryWriteLong(WriteAddress, MappedMemoryReadLong(ReadAddress, NULL), NULL);
            ReadAddress += 4;
            WriteAddress += WriteAdd;
            counter += 4;
         }
         /* Inform the SH-2 core in case it was a write to main RAM */
         SH2WriteNotify(start, WriteAddress - start);
      }

   }  // Fill / copy
}

//////////////////////////////////////


static void FASTCALL ScuDMA(scudmainfo_struct *dmainfo) {
   u8 ReadAdd, WriteAdd;
   u32 trans_size = 0;

   if (dmainfo->AddValue & 0x100)
      ReadAdd = 4;
   else
      ReadAdd = 0;

   switch(dmainfo->AddValue & 0x7) {
      case 0x0:
         WriteAdd = 0;
         break;
      case 0x1:
         WriteAdd = 2;
         break;
      case 0x2:
         WriteAdd = 4;
         break;
      case 0x3:
         WriteAdd = 8;
         break;
      case 0x4:
         WriteAdd = 16;
         break;
      case 0x5:
         WriteAdd = 32;
         break;
      case 0x6:
         WriteAdd = 64;
         break;
      case 0x7:
         WriteAdd = 128;
         break;
      default:
         WriteAdd = 0;
         break;
   }

   if (dmainfo->ModeAddressUpdate & 0x1000000) {
      // Indirect DMA

      for (;;) {
         u32 ThisTransferSize = MappedMemoryReadLong(dmainfo->WriteAddress, NULL);
         u32 ThisWriteAddress = MappedMemoryReadLong(dmainfo->WriteAddress+4, NULL);
         u32 ThisReadAddress  = MappedMemoryReadLong(dmainfo->WriteAddress+8, NULL);

         //LOG("SCU Indirect DMA: src %08x, dst %08x, size = %08x\n", ThisReadAddress, ThisWriteAddress, ThisTransferSize);
         DoDMA(ThisReadAddress & 0x7FFFFFFF, ReadAdd, ThisWriteAddress,
               WriteAdd, ThisTransferSize);

         if (ThisReadAddress & 0x80000000)
            break;

         dmainfo->WriteAddress+= 0xC;
         trans_size += ThisTransferSize;
      }

      switch(dmainfo->mode) {
         case 0:
           if (trans_size > 1024) {
             ScuRegs->dma0_time = trans_size;
           }
           else {
             ScuSendLevel0DMAEnd();
           }
           break;
         case 1:
          if (trans_size > 1024) {
             ScuRegs->dma1_time = trans_size;
           }
           else {
             ScuSendLevel1DMAEnd();
           }
            break;
         case 2:
           if (trans_size > 1024) {
             ScuRegs->dma2_time = trans_size;
           }
           else {
             ScuSendLevel2DMAEnd();
           }
            break;
      }
   }
   else {
      // Direct DMA

      if (dmainfo->mode > 0) {
         dmainfo->TransferNumber &= 0xFFF;

         if (dmainfo->TransferNumber == 0)
            dmainfo->TransferNumber = 0x1000;
      }
      else {
         if (dmainfo->TransferNumber == 0)
            dmainfo->TransferNumber = 0x100000;
      }

      DoDMA(dmainfo->ReadAddress, ReadAdd, dmainfo->WriteAddress, WriteAdd,
            dmainfo->TransferNumber);

      switch(dmainfo->mode) {
         case 0:
           
           if (dmainfo->TransferNumber > 1024) {
             ScuRegs->dma0_time = dmainfo->TransferNumber;
           }
           else {
             ScuSendLevel0DMAEnd();
           }
            break;
         case 1:
           if (dmainfo->TransferNumber > 1024) {
             ScuRegs->dma1_time = dmainfo->TransferNumber;
           }
           else {
             ScuSendLevel1DMAEnd();
           }
            break;
         case 2:
           if (dmainfo->TransferNumber > 1024) {
             ScuRegs->dma2_time = dmainfo->TransferNumber;
           }
           else {
             ScuSendLevel2DMAEnd();
           }
            break;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

static u32 readgensrc(u8 num)
{
   u32 val;

   if( num <= 7  ){
     incFlg[(num & 0x3)] |= ((num >> 2) & 0x01);
     return ScuDsp->MD[(num & 0x3)][ScuDsp->CT[(num & 0x3)]&0x3F];
   }else{
     if (num == 0x9)  // ALL
       return (u32)ScuDsp->ALU.part.L;
     else if (num == 0xA) // ALH
       return (u32)(ScuDsp->ALU.all >> 16); ////(u32)((ScuDsp->ALU.all & (u64)(0x0000ffffffff0000))  >> 16);
   }
#if 0
   switch(num) {
      case 0x0: // M0
         return ScuDsp->MD[0][ScuDsp->CT[0]];
      case 0x1: // M1
         return ScuDsp->MD[1][ScuDsp->CT[1]];
      case 0x2: // M2
         return ScuDsp->MD[2][ScuDsp->CT[2]];
      case 0x3: // M3
         return ScuDsp->MD[3][ScuDsp->CT[3]];
      case 0x4: // MC0
         val = ScuDsp->MD[0][ScuDsp->CT[0]];
         incFlg[0] = 1;
         return val;
      case 0x5: // MC1
         val = ScuDsp->MD[1][ScuDsp->CT[1]];
         incFlg[1] = 1;
         return val;
      case 0x6: // MC2
         val = ScuDsp->MD[2][ScuDsp->CT[2]];
         incFlg[2] = 1;
         return val;
      case 0x7: // MC3
         val = ScuDsp->MD[3][ScuDsp->CT[3]];
         incFlg[3] = 1;
         return val;
      case 0x9: // ALL
         return (u32)ScuDsp->ALU.part.L;
      case 0xA: // ALH
         return (u32)((ScuDsp->ALU.all & (u64)(0x0000ffffffff0000))  >> 16);
      default: break;
   }
#endif
   return 0xFFFFFFFF;
}

//////////////////////////////////////////////////////////////////////////////

static void writed1busdest(u8 num, u32 val)
{
   switch(num) { 
      case 0x0:
          ScuDsp->MD[0][ScuDsp->CT[0]&0x3F] = val;
          incFlg[0] = 1;
          return;
      case 0x1:
        ScuDsp->MD[1][ScuDsp->CT[1] & 0x3F] = val;
          incFlg[1] = 1;
          return;
      case 0x2:
        ScuDsp->MD[2][ScuDsp->CT[2] & 0x3F] = val;
          incFlg[2] = 1;
          return;
      case 0x3:
        ScuDsp->MD[3][ScuDsp->CT[3] & 0x3F] = val;
          incFlg[3] = 1;
          return;
      case 0x4:
          ScuDsp->RX = val;
          return;
      case 0x5:
          ScuDsp->P.all = (signed)val;
          return;
      case 0x6:
          ScuDsp->RA0 = val;
          return;
      case 0x7:
          ScuDsp->WA0 = val;
          return;
      case 0xA:
          ScuDsp->LOP = (u16)val;
          return;
      case 0xB:
          ScuDsp->TOP = (u8)val;
          return;
      case 0xC:
          ScuDsp->CT[0] = (u8)val;
          incFlg[0] = 0;
          return;
      case 0xD:
          ScuDsp->CT[1] = (u8)val;
          incFlg[1] = 0;
          return;
      case 0xE:
          ScuDsp->CT[2] = (u8)val;
          incFlg[2] = 0;
          return;
      case 0xF:
          ScuDsp->CT[3] = (u8)val;
          incFlg[3] = 0;
          return;
      default: break;
   }
}

//////////////////////////////////////////////////////////////////////////////

static void writeloadimdest(u8 num, u32 val)
{
   switch(num) { 
      case 0x0: // MC0
        ScuDsp->MD[0][ScuDsp->CT[0] & 0x3F] = val;
          incFlg[0] = 1;
          return;
      case 0x1: // MC1
        ScuDsp->MD[1][ScuDsp->CT[1] & 0x3F] = val;
        incFlg[1] = 1;
        return;
      case 0x2: // MC2
        ScuDsp->MD[2][ScuDsp->CT[2] & 0x3F] = val;
          incFlg[2] = 1;
          return;
      case 0x3: // MC3
        ScuDsp->MD[3][ScuDsp->CT[3] & 0x3F] = val;
          incFlg[3] = 1;
          return;
      case 0x4: // RX
          ScuDsp->RX = val;
          return;
      case 0x5: // PL
          ScuDsp->P.all = (s32)val;
          return;
      case 0x6: // RA0
          val = (val & 0x1FFFFFF);
          ScuDsp->RA0 = val;
          return;
      case 0x7: // WA0
          val = (val & 0x1FFFFFF);
          ScuDsp->WA0 = val;
          return;
      case 0xA: // LOP
          ScuDsp->LOP = (u16)(val & 0x0FFF);
          return;
      case 0xC: // PC->TOP, PC
          ScuDsp->TOP = ScuDsp->PC+1;
          ScuDsp->jmpaddr = val;
          ScuDsp->delayed = 0;
          return;
      default: 
        LOG("writeloadimdest BAD NUM %d,%d",num,val);
        break;
   }
}


void dsp_dma01(scudspregs_struct *sc, u32 inst)
{
    u32 imm = ((inst & 0xFF));
    u8  sel = ((inst >> 8) & 0x03);
    u8  addr = sc->CT[sel];
    u32 i;

    const u32 mode = (inst >> 15) & 0x7;
    const u32 add = (1 << (mode & 0x2)) &~1;

  //LOG("DSP DMA01 read addr=%08X cnt= %d add = %d\n", (sc->RA0 << 2), imm, add );

  // is A-Bus?
  u32 abus_check = ((sc->RA0 << 2) & 0x0FF00000);
  if (abus_check >= 0x02000000 && abus_check < 0x05900000){
    for (i = 0; i < imm; i++)
    {
      sc->MD[sel][sc->CT[sel] & 0x3F] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
      //LOG("read from %08X to [%d][%d] val %08X", (sc->RA0 << 2), sel, sc->CT[sel] & 0x3F, sc->MD[sel][sc->CT[sel] & 0x3F] );
      sc->CT[sel]++;
      sc->CT[sel] &= 0x3F;
      sc->RA0 += (add >> 2);
    }
  }
  else{
    for (i = 0; i < imm ; i++)
    {
      sc->MD[sel][sc->CT[sel] & 0x3F] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
      //LOG("read from %08X to [%d][%d] val %08X", (sc->RA0 << 2), sel, sc->CT[sel] & 0x3F, sc->MD[sel][sc->CT[sel] & 0x3F]);
      sc->CT[sel]++;
      sc->CT[sel] &= 0x3F;
      sc->RA0 += (add >>2);
    }
  }

    sc->ProgControlPort.part.T0 = 0;
}

extern u8 * HighWram;

void dsp_dma_write_d0bus(scudspregs_struct *sc, int sel, int add, int count){

  int i;
  u32 Adr = (sc->WA0 << 2) & 0x0FFFFFFF;

  // A-BUS?
  if (Adr >= 0x02000000 && Adr < 0x05A00000){

    if (add > 1) add = 1;

    for (i = 0; i < count; i++)
    {
      u32 Val = sc->MD[sel][sc->CT[sel] & 0x3F];
      Adr = (sc->WA0 << 2);
      MappedMemoryWriteLong(Adr, Val, NULL);
      sc->CT[sel]++;
      sc->WA0 += add;
      sc->CT[sel] &= 0x3F;
    }
  }
  else
    // B-BUS?
    if (Adr >= 0x05A00000 && Adr < 0x06000000){

      if (add == 0) add = 1;

      for (i = 0; i < count; i++)
      { 
        u32 Val = sc->MD[sel][sc->CT[sel] & 0x3F];
        MappedMemoryWriteWord(Adr, (Val>>16), NULL);
        MappedMemoryWriteWord(Adr+2, Val, NULL);
        sc->CT[sel]++;
        sc->CT[sel] &= 0x3F;
        Adr += (add << 2);
      }
      sc->WA0 = sc->WA0 + ((add*count));
    }
  // CPU-BUS
    else{

      if (add == 0) add = 1;

      if (add == 1){
        for (i = 0; i < count; i++)
        {
          u32 Val = sc->MD[sel][sc->CT[sel] & 0x3F];
          Adr = (sc->WA0 << 2);
          T2WriteLong(HighWram, Adr & 0xFFFFC, Val);
          sc->CT[sel]++;
          sc->CT[sel] &= 0x3F;
          sc->WA0 += 1;
        }
      }
      else
      {
        for (i = 0; i < count; i++)
        {
          u32 Val = sc->MD[sel][sc->CT[sel] & 0x3F];
          Adr = (sc->WA0 << 2);
          T2WriteLong(HighWram, Adr & 0xFFFFC, Val);
          sc->CT[sel]++;
          sc->CT[sel] &= 0x3F;
          sc->WA0 += (add >> 1);
        }
      }

    }

    sc->ProgControlPort.part.T0 = 0;

}

void dsp_dma02(scudspregs_struct *sc, u32 inst)
{
    u32 imm = ((inst & 0xFF));      
    u8  sel = ((inst >> 8) & 0x03); 
    u8  addr = sc->CT[sel];             
    u8  add;
    u32 i;

    switch (((inst >> 15) & 0x07))
    {
    case 0: add = 0; break;
    case 1: add = 1; break;
    case 2: add = 2; break;
    case 3: add = 4; break;
    case 4: add = 8; break;
    case 5: add = 16; break;
    case 6: add = 32; break;
    case 7: add = 64; break;
    }

  //LOG("DSP DMA02 write addr=%08X cnt= %d add = %d\n", (sc->WA0 << 2), imm, add);
  dsp_dma_write_d0bus(sc, sel, add, imm);
}

void dsp_dma03(scudspregs_struct *sc, u32 inst)
{
  u32 Counter = 0;
  u32 i;
  int sel;

  switch ((inst & 0x7))
  {
  case 0x00: Counter = sc->MD[0][sc->CT[0] & 0x3F]; break;
  case 0x01: Counter = sc->MD[1][sc->CT[1] & 0x3F]; break;
  case 0x02: Counter = sc->MD[2][sc->CT[2] & 0x3F]; break;
  case 0x03: Counter = sc->MD[3][sc->CT[3] & 0x3F]; break;
  case 0x04: Counter = sc->MD[0][sc->CT[0] & 0x3F]; ScuDsp->CT[0]++; ScuDsp->CT[0] &= 0x3F; break;
  case 0x05: Counter = sc->MD[1][sc->CT[1] & 0x3F]; ScuDsp->CT[1]++; ScuDsp->CT[1] &= 0x3F; break;
  case 0x06: Counter = sc->MD[2][sc->CT[2] & 0x3F]; ScuDsp->CT[2]++; ScuDsp->CT[2] &= 0x3F; break;
  case 0x07: Counter = sc->MD[3][sc->CT[3] & 0x3F]; ScuDsp->CT[3]++; ScuDsp->CT[3] &= 0x3F; break;
  }

  sel = (inst >> 8) & 0x7;
  int index = 0;

  const u32 mode = (inst >> 15) & 0x7;
  const u32 add = (1 << (mode & 0x2)) &~1;

  //LOG("DSP DMA03 read addr=%08X cnt= %d add = %d\n", (sc->RA0 << 2), Counter, add);

  u32 abus_check = ((sc->RA0 << 2) & 0x0FF00000);
  if (abus_check >= 0x02000000 && abus_check < 0x05900000){
    for (i = 0; i < Counter; i++)
    {
      if (sel == 0x04){
        sc->ProgramRam[index] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
        //LOG("read from %08X to P[%d] val %08X", (sc->RA0 << 2), index, sc->ProgramRam[index]);
        index++;
      }
      else{
        sc->MD[sel][sc->CT[sel]&0x3F] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
        //LOG("read from %08X to [%d][%d] val %08X", (sc->RA0 << 2), sel, sc->CT[sel] & 0x3F, sc->MD[sel][sc->CT[sel] & 0x3F]);
        sc->CT[sel]++;
        sc->CT[sel] &= 0x3F;
      }
      sc->RA0 += (add >> 2);

    }
  }
  else{
    for (i = 0; i < Counter; i++)
    {

      if (sel == 0x04){
        sc->ProgramRam[index] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
        //LOG("read from %08X to P[%d] val %08X", (sc->RA0 << 2), index, sc->ProgramRam[index]);
        index++;
      }else{
        sc->MD[sel][sc->CT[sel]&0x3F] = MappedMemoryReadLong((sc->RA0 << 2), NULL);
        //LOG("read from %08X to [%d][%d] val %08X", (sc->RA0 << 2), sel, sc->CT[sel] & 0x3F, sc->MD[sel][sc->CT[sel] & 0x3F]);
        sc->CT[sel]++;
        sc->CT[sel] &= 0x3F;
      }
      sc->RA0 += (add >> 2);
    }
  }


#if 0
    DestinationId = (inst >> 8) & 0x7;

    if (DestinationId > 3)
    {
        int incl = 1; //((sc->inst >> 15) & 0x01);
        for (i = 0; i < Counter; i++)
        {
            u32 Adr = (sc->RA0 << 2);
            sc->ProgramRam[i] = MappedMemoryReadLong(Adr);
            sc->RA0 += incl;
        }
    }
    else{

        int incl = 1; //((sc->inst >> 15) & 0x01);
        for (i = 0; i < Counter; i++)
        {
            u32 Adr = (sc->RA0 << 2);

            sc->MD[DestinationId][sc->CT[DestinationId]] = MappedMemoryReadLong(Adr);
            sc->CT[DestinationId]++;
            sc->CT[DestinationId] &= 0x3F;
            sc->RA0 += incl;
        }
    }
#endif
    sc->ProgControlPort.part.T0 = 0;
}

void dsp_dma04(scudspregs_struct *sc, u32 inst)
{
    u32 Counter = 0;
    u32 add = 0;
    u32 sel = ((inst >> 8) & 0x03);
    u32 i;

    switch ((inst & 0x7))
    {
    case 0x00: Counter = sc->MD[0][sc->CT[0] & 0x3F]; break;
    case 0x01: Counter = sc->MD[1][sc->CT[1] & 0x3F]; break;
    case 0x02: Counter = sc->MD[2][sc->CT[2] & 0x3F]; break;
    case 0x03: Counter = sc->MD[3][sc->CT[3] & 0x3F]; break;
    case 0x04: Counter = sc->MD[0][sc->CT[0] & 0x3F]; ScuDsp->CT[0]++; ScuDsp->CT[0] &= 0x3F; break;
    case 0x05: Counter = sc->MD[1][sc->CT[1] & 0x3F]; ScuDsp->CT[1]++; ScuDsp->CT[1] &= 0x3F; break;
    case 0x06: Counter = sc->MD[2][sc->CT[2] & 0x3F]; ScuDsp->CT[2]++; ScuDsp->CT[2] &= 0x3F; break;
    case 0x07: Counter = sc->MD[3][sc->CT[3] & 0x3F]; ScuDsp->CT[3]++; ScuDsp->CT[3] &= 0x3F; break;
    }
    
    switch (((inst >> 15) & 0x07))
    {
    case 0: add = 0; break;
    case 1: add = 1; break;
    case 2: add = 2; break;
    case 3: add = 4; break;
    case 4: add = 8; break;
    case 5: add = 16; break;
    case 6: add = 32; break;
    case 7: add = 64; break;
    }

  //LOG("DSP DMA04 write addr=%08X cnt= %d add = %d sel=%d\n", (sc->WA0 << 2), Counter, add,sel );
  dsp_dma_write_d0bus(sc, sel, add, Counter);


}

void dsp_dma05(scudspregs_struct *sc, u32 inst)
{
    u32 saveRa0 = sc->RA0;
    dsp_dma01(sc, inst);
    sc->RA0 = saveRa0;
}

void dsp_dma06(scudspregs_struct *sc, u32 inst)
{
    u32 saveWa0 = sc->WA0;
    dsp_dma02(sc, inst);
    sc->WA0 = saveWa0;
}

void dsp_dma07(scudspregs_struct *sc, u32 inst)
{
    u32 saveRa0 = sc->RA0;
    dsp_dma03(sc, inst);
    sc->RA0 = saveRa0;

}

void dsp_dma08(scudspregs_struct *sc, u32 inst)
{
    u32 saveWa0 = sc->WA0;
    dsp_dma04(sc, inst);
    sc->WA0 = saveWa0;
}

INLINE void ScuTimer1Exec( u32 timing ) {
  if (ScuRegs->timer1_counter > 0) {
    ScuRegs->timer1_counter = (ScuRegs->timer1_counter - (timing >> 1));
    if (ScuRegs->timer1_counter <= 0) {
      ScuRegs->timer1_set = 1;
      if ((ScuRegs->T1MD & 0x80) == 0) {
        ScuSendTimer1();
      }
      else if (ScuRegs->timer0_set == 1) {
        ScuSendTimer1();
      }
    }
  }
}

void ScuSetAddValue(scudmainfo_struct * dmainfo) {

  if (dmainfo->AddValue & 0x100)
    dmainfo->ReadAdd = 4;
  else
    dmainfo->ReadAdd = 0;

  switch (dmainfo->AddValue & 0x7) {
  case 0x0:
    dmainfo->WriteAdd = 0;
    break;
  case 0x1:
    dmainfo->WriteAdd = 2;
    break;
  case 0x2:
    dmainfo->WriteAdd = 4;
    break;
  case 0x3:
    dmainfo->WriteAdd = 8;
    break;
  case 0x4:
    dmainfo->WriteAdd = 16;
    break;
  case 0x5:
    dmainfo->WriteAdd = 32;
    break;
  case 0x6:
    dmainfo->WriteAdd = 64;
    break;
  case 0x7:
    dmainfo->WriteAdd = 128;
    break;
  default:
    dmainfo->WriteAdd = 0;
    break;
  }
  if (dmainfo->ModeAddressUpdate & 0x1000000) {
    dmainfo->InDirectAdress = dmainfo->WriteAddress;
    dmainfo->TransferNumber = MappedMemoryReadLong(dmainfo->InDirectAdress, NULL);
    dmainfo->WriteAddress = MappedMemoryReadLong(dmainfo->InDirectAdress + 4, NULL);
    dmainfo->ReadAddress = MappedMemoryReadLong(dmainfo->InDirectAdress + 8, NULL);
    dmainfo->InDirectAdress += 0xC;
  }
  else {

    if (dmainfo->mode > 0) {
      dmainfo->TransferNumber &= 0xFFF;
      if (dmainfo->TransferNumber == 0)
        dmainfo->TransferNumber = 0x1000;
    }
    else {
      if (dmainfo->TransferNumber == 0)
        dmainfo->TransferNumber = 0x100000;
    }
  }

  LOG("DoDMA src=%08X,dst=%08X,size=%d, ra:%d/wa:%d flame=%d:%d\n",
    dmainfo->ReadAddress, dmainfo->WriteAddress, dmainfo->TransferNumber,
    dmainfo->ReadAdd, dmainfo->WriteAdd, yabsys.frame_count, yabsys.LineCount);

}

void SucDmaExec(scudmainfo_struct * dma, int * time ) {
  //LOG("DoDMA src=%08X,dst=%08X,size=%d, ra:%d/wa:%d flame=%d:%d\n",
  //  dma->ReadAddress, dma->WriteAddress, dma->TransferNumber, dma->ReadAdd, dma->WriteAdd, yabsys.frame_count, yabsys.LineCount);
  u32 cycle = 0;
  if (dma->ReadAdd == 0) {
    // DMA fill
    // Is it a constant source or a register whose value can change from
    // read to read?
    int constant_source = ((dma->ReadAddress & 0x1FF00000) == 0x00200000)
      || ((dma->ReadAddress & 0x1E000000) == 0x06000000)
      || ((dma->ReadAddress & 0x1FF00000) == 0x05A00000)
      || ((dma->ReadAddress & 0x1DF00000) == 0x05C00000);

    if ((dma->WriteAddress & 0x1FFFFFFF) >= 0x5A00000
      && (dma->WriteAddress & 0x1FFFFFFF) < 0x5FF0000) {
      // Fill a 32-bit value in 16-bit units.  We have to be careful to
      // avoid misaligned 32-bit accesses, because some hardware (e.g.
      // PSP) crashes on such accesses.
      if (constant_source) {
        u32 val;
        if (dma->ReadAddress & 2) {  // Avoid misaligned access
          val = MappedMemoryReadWord( (dma->ReadAddress&0x0FFFFFFF) , NULL) << 16
            | MappedMemoryReadWord( (dma->ReadAddress&0x0FFFFFFF) + 2, NULL);
        }
        else {
          val = MappedMemoryReadLong((dma->ReadAddress & 0x0FFFFFFF), NULL);
        }

        u32 start = dma->WriteAddress;
        while ( *time > 0 ) {
          *time -= 1;
          MappedMemoryWriteWord(dma->WriteAddress, (u16)(val >> 16), &cycle);
          dma->WriteAddress += dma->WriteAdd;
          MappedMemoryWriteWord(dma->WriteAddress, (u16)val, &cycle);
          dma->WriteAddress += dma->WriteAdd;
          dma->TransferNumber -= 4;
          if (dma->TransferNumber <= 0 ) {
            SH2WriteNotify(start, dma->WriteAddress - start);
            return;
          }
        }
        SH2WriteNotify(start, dma->WriteAddress - start);
      }
      else {
        u32 start = dma->WriteAddress;
        while ( *time > 0) {
          *time -= 1;
          u32 tmp = MappedMemoryReadLong((dma->ReadAddress & 0x0FFFFFFF), &cycle);
          MappedMemoryWriteWord(dma->WriteAddress, (u16)(tmp >> 16), &cycle);
          dma->WriteAddress += dma->WriteAdd;
          MappedMemoryWriteWord(dma->WriteAddress, (u16)tmp, &cycle);
          dma->WriteAddress += dma->WriteAdd;
          dma->ReadAddress += dma->ReadAdd;
          dma->TransferNumber -= 4;
          if (dma->TransferNumber <= 0) {
            SH2WriteNotify(start, dma->WriteAddress - start);
            return;
          }
        }
        SH2WriteNotify(start, dma->WriteAddress - start);
      }
    }
    else {
      // Fill in 32-bit units (always aligned).
      u32 start = dma->WriteAddress;
      if (constant_source) {
        u32 val = MappedMemoryReadLong((dma->ReadAddress & 0x0FFFFFFF), &cycle);
        while ( *time > 0) {
          *time -= 1;
          MappedMemoryWriteLong(dma->WriteAddress, val, &cycle);
          dma->ReadAddress += dma->ReadAdd;
          dma->WriteAddress += dma->WriteAdd;
          dma->TransferNumber -= 4;
          if (dma->TransferNumber <= 0) {
            SH2WriteNotify(start, dma->WriteAddress - start);
            return;
          }
        }
      }
      else {
        while (*time > 0) {
          *time -= 1;
          u32 val = MappedMemoryReadLong((dma->ReadAddress & 0x0FFFFFFF), &cycle);
          MappedMemoryWriteLong(dma->WriteAddress, val, &cycle);
          dma->ReadAddress += dma->ReadAdd;
          dma->WriteAddress += dma->WriteAdd;
          dma->TransferNumber -= 4;
          if (dma->TransferNumber <= 0) {
            SH2WriteNotify(start, dma->WriteAddress - start);
            return;
          }
        }
      }
      // Inform the SH-2 core in case it was a write to main RAM.
      SH2WriteNotify(start, dma->WriteAddress - start);
    }

  }

  else {
    // DMA copy
    // Access to B-BUS?
    if (((dma->WriteAddress & 0x1FFFFFFF) >= 0x5A00000 && (dma->WriteAddress & 0x1FFFFFFF) < 0x5FF0000)) {
      // Copy in 16-bit units, avoiding misaligned accesses.
      u32 counter = 0;
      u32 start = dma->WriteAddress;
      while (*time > 0) {
        *time -= 1;
        u16 tmp = MappedMemoryReadWord((dma->ReadAddress & 0x0FFFFFFF), &cycle);
        MappedMemoryWriteWord(dma->WriteAddress, tmp, &cycle);
        dma->WriteAddress += dma->WriteAdd;
        dma->ReadAddress += 2;
        dma->TransferNumber -= 2;
        if (dma->TransferNumber <= 0) {
          SH2WriteNotify(start, dma->WriteAddress - start);
          return;
        }
      }
      SH2WriteNotify(start, dma->WriteAddress - start);
    }
    else if (((dma->ReadAddress & 0x1FFFFFFF) >= 0x5A00000 && (dma->ReadAddress & 0x1FFFFFFF) < 0x5FF0000)) {
      u32 start = dma->WriteAddress;
      while ( *time > 0) {
        *time -= 1;
        u16 tmp = MappedMemoryReadWord((dma->ReadAddress & 0x0FFFFFFF), &cycle);
        MappedMemoryWriteWord(dma->WriteAddress, tmp, &cycle);
        dma->WriteAddress += (dma->WriteAdd >> 1);
        dma->ReadAddress += 2;
        dma->TransferNumber -= 2;
        if (dma->TransferNumber <= 0) {
          SH2WriteNotify(start, dma->WriteAddress - start);
          return;
        }
      }
      SH2WriteNotify(start, dma->WriteAddress - start);
    }
    else {
      u32 counter = 0;
      u32 start = dma->WriteAddress;
      while (*time > 0) {
        *time -= 1;
        u32 val = MappedMemoryReadLong((dma->ReadAddress & 0x0FFFFFFF), &cycle);
        MappedMemoryWriteLong(dma->WriteAddress, val , &cycle);
        dma->ReadAddress += 4;
        dma->WriteAddress += dma->WriteAdd;
        dma->TransferNumber -= 4;
        if (dma->TransferNumber <= 0) {
          SH2WriteNotify(start, dma->WriteAddress - start);
          return;
        }
      }
      /* Inform the SH-2 core in case it was a write to main RAM */
      SH2WriteNotify(start, dma->WriteAddress - start);
    }

  }  // Fill / copy


}


void SucDmaCheck(scudmainfo_struct * dma, int time) {
  int atime = time;
  if (dma->TransferNumber > 0) {
    if (dma->ModeAddressUpdate & 0x1000000) {
      while (atime > 0) {
        SucDmaExec(dma, &atime);
        if (dma->TransferNumber <= 0) {
          if (dma->ReadAddress & 0x80000000) {
            switch (dma->mode) {
            case 0:
              //LOG("DMA0 Finished!");
              ScuSendLevel0DMAEnd();
              break;
            case 1:
              //LOG("DMA1 Finished!");
              ScuSendLevel1DMAEnd();
              break;
            case 2:
              //LOG("DMA2 Finished!");
              ScuSendLevel2DMAEnd();
              break;
            }
            dma->TransferNumber = 0;
            return;
          }
          else {
            dma->TransferNumber = MappedMemoryReadLong(dma->InDirectAdress, NULL);
            dma->WriteAddress = MappedMemoryReadLong(dma->InDirectAdress + 4, NULL);
            dma->ReadAddress = MappedMemoryReadLong(dma->InDirectAdress + 8, NULL);
            dma->InDirectAdress += 0xC;
          }
        }
      }

    }
    else {
      SucDmaExec(dma, &atime);
      if (dma->TransferNumber <= 0) {
        switch (dma->mode) {
        case 0:
          //LOG("DMA0 Finished!");
          ScuSendLevel0DMAEnd();
          break;
        case 1:
          //LOG("DMA1 Finished!");
          ScuSendLevel1DMAEnd();
          break;
        case 2:
          //LOG("DMA2 Finished!");
          ScuSendLevel2DMAEnd();
          break;
        }
      }
    }
  }
  return;
}


void ScuDmaProc(Scu * scu, int time) {
#if OLD_DMA
  return;
#endif
  SucDmaCheck(&scu->dma0, time);
  SucDmaCheck(&scu->dma1, time);
  SucDmaCheck(&scu->dma2, time);
}

//////////////////////////////////////////////////////////////////////////////
void ScuExec(u32 timing) {
   int i;

   if ( ScuRegs->T1MD & 0x1 ){
     if (ScuRegs->T1MD & 0x80 == 0) {
       ScuTimer1Exec(timing);
     }
     else {
       if (yabsys.LineCount == ScuRegs->T0C) {
         ScuTimer1Exec(timing);
       }
     }
   }

#if OLD_DMA
   if (ScuRegs->dma0_time > 0) {
     //ScuRegs->dma0_time -= (timing << 4); // ToDo: memory clock
     //if (ScuRegs->dma0_time < 0) {
     ScuSendLevel0DMAEnd();
     ScuRegs->dma0_time = 0;
     //}
   }

   else if (ScuRegs->dma1_time > 0) {
     //ScuRegs->dma1_time -= (timing << 4); // ToDo: memory clock
     //if (ScuRegs->dma1_time < 0) {
     ScuSendLevel1DMAEnd();
     ScuRegs->dma1_time = 0;
     //}
   }

   else if (ScuRegs->dma2_time > 0) {
     //ScuRegs->dma0_time -= (timing << 4); // ToDo: memory clock
     //if (ScuRegs->dma0_time < 0) {
     ScuSendLevel2DMAEnd();
     ScuRegs->dma2_time = 0;
     //}
   }
#else
  ScuDmaProc(ScuRegs, (int)timing<<4);
#endif

   // is dsp executing?
   if (ScuDsp->ProgControlPort.part.EX) {

#ifdef DSPLOG
     if (slogp == NULL){
#if defined(ANDROID)
       slogp = fopen("/mnt/sdcard/slog.txt", "w");
#else
       slogp = fopen("slog.txt", "w");
#endif
     }
     if (slogp){
       fprintf(slogp, "*********************************************\n");
     }
#endif
     s32 dsp_counter = (s32)timing;
      while (dsp_counter > 0) {
         u32 instruction;

         // Make sure it isn't one of our breakpoints
         for (i=0; i < ScuBP->numcodebreakpoints; i++) {
            if ((ScuDsp->PC == ScuBP->codebreakpoint[i].addr) && ScuBP->inbreakpoint == 0) {
               ScuBP->inbreakpoint = 1;
               if (ScuBP->BreakpointCallBack) ScuBP->BreakpointCallBack(ScuBP->codebreakpoint[i].addr);
                 ScuBP->inbreakpoint = 0;
            }
         }

         instruction = ScuDsp->ProgramRam[ScuDsp->PC];
         //LOG("scu: dsp %08X @ %08X", instruction, ScuDsp->PC);
         incFlg[0] = 0;
         incFlg[1] = 0;
         incFlg[2] = 0;
         incFlg[3] = 0;

         ScuDsp->ALU.all = ScuDsp->AC.all;
#ifdef DSPLOG
         if (slogp){
           char buf[128];
           ScuDspDisasm(ScuDsp->PC, buf);
           fprintf(slogp, "%s ALU=%" PRId64 "P = %" PRId64 "\n", buf, ScuDsp->ALU.all, ScuDsp->P.all);
         }
#endif

         // ALU commands
         switch (instruction >> 26)
         {
            case 0x0: // NOP
               //AC is moved as-is to the ALU
              //ScuDsp->ALU.all = ScuDsp->AC.all;
               break;
            case 0x1: // AND
               //the upper 16 bits of AC are not modified for and, or, add, sub, rr and rl8
              ScuDsp->ALU.part.L = (s64)((u32)ScuDsp->AC.part.L & (u32)ScuDsp->P.part.L);

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if ((s64)ScuDsp->ALU.part.L < 0)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               ScuDsp->ProgControlPort.part.C = 0;
               break;
            case 0x2: // OR
              ScuDsp->ALU.part.L = (u64)((u32)ScuDsp->AC.part.L | (u32)ScuDsp->P.part.L);

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if ((s64)ScuDsp->ALU.part.L < 0)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               ScuDsp->ProgControlPort.part.C = 0;
               break;
            case 0x3: // XOR
              ScuDsp->ALU.part.L = (u64)((u32)ScuDsp->AC.part.L ^ (u32)ScuDsp->P.part.L);

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if ((s64)ScuDsp->ALU.part.L < 0)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               ScuDsp->ProgControlPort.part.C = 0;
               break;
            case 0x4: // ADD
               ScuDsp->ALU.part.L = (s32)ScuDsp->AC.part.L + (s32)ScuDsp->P.part.L;
#ifdef DSPLOG
               if (slogp){
                 fprintf(slogp, "%02X: %d + %d = %d\n", ScuDsp->PC, (s32)ScuDsp->AC.part.L, (s32)ScuDsp->P.part.L, (s32)ScuDsp->ALU.part.L);
               }
#endif
               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if ((s32)ScuDsp->ALU.part.L < 0)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               //0x00000001 + 0xFFFFFFFF will set the carry bit, needs to be unsigned math
               if (((u64)(u32)ScuDsp->P.part.L + (u64)(u32)ScuDsp->AC.part.L) & 0x100000000){
                 ScuDsp->ProgControlPort.part.C = 1;
               }
               else{
                 ScuDsp->ProgControlPort.part.C = 0;
               }

 
               //if (ScuDsp->ALU.part.L ??) // set overflow flag
               //    ScuDsp->ProgControlPort.part.V = 1;
               //else
               //   ScuDsp->ProgControlPort.part.V = 0;
               break;
            case 0x5: // SUB
               ScuDsp->ALU.part.L = (s32)ScuDsp->AC.part.L - (s32)ScuDsp->P.part.L;

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if ((s64)ScuDsp->ALU.part.L < 0)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               //0x00000001 - 0xFFFFFFFF will set the carry bit, needs to be unsigned math
               if ((((u64)(u32)ScuDsp->AC.part.L - (u64)(u32)ScuDsp->P.part.L)) & 0x100000000)
                  ScuDsp->ProgControlPort.part.C = 1;
               else
                  ScuDsp->ProgControlPort.part.C = 0;


//               if (ScuDsp->ALU.part.L ??) // set overflow flag
//                  ScuDsp->ProgControlPort.part.V = 1;
//               else
//                  ScuDsp->ProgControlPort.part.V = 0;
               break;
            case 0x6: // AD2
              ScuDsp->ALU.all = (s64)ScuDsp->AC.all +(s64)ScuDsp->P.all;
#ifdef DSPLOG
              if (slogp){
                fprintf(slogp, "%02X: %" PRId64 "+ %" PRId64 "= %" PRId64 "\n", ScuDsp->PC, ScuDsp->AC.all, ScuDsp->P.all, ScuDsp->ALU.all);
              }
#endif
               if (ScuDsp->ALU.all == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               //0x500000000000 + 0xd00000000000 will set the sign bit
               if (ScuDsp->ALU.all & 0x800000000000)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               //AC.all and P.all are sign-extended so we need to mask it off and check for a carry
               if (((ScuDsp->AC.all & 0xffffffffffff) + (ScuDsp->P.all & 0xffffffffffff)) & (0x1000000000000))
                  ScuDsp->ProgControlPort.part.C = 1;
               else
                  ScuDsp->ProgControlPort.part.C = 0;

//               if (ScuDsp->ALU.part.unused != 0)
//                  ScuDsp->ProgControlPort.part.V = 1;
//               else
//                  ScuDsp->ProgControlPort.part.V = 0;

               break;
            case 0x8: // SR
              ScuDsp->ProgControlPort.part.C = ScuDsp->AC.part.L & 0x1;
               ScuDsp->ALU.part.L = (ScuDsp->AC.part.L & 0x80000000) | (ScuDsp->AC.part.L >> 1);

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if (ScuDsp->ALU.part.L & 0x80000000)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               //0x00000001 >> 1 will set the carry bit
               //ScuDsp->ProgControlPort.part.C = ScuDsp->ALU.part.L >> 31; would not handle this case
               break;
            case 0x9: // RR
              ScuDsp->ProgControlPort.part.C = ScuDsp->AC.part.L & 0x1;
               ScuDsp->ALU.part.L = ((u32)(ScuDsp->ProgControlPort.part.C) << 31) | ((u32)(ScuDsp->AC.part.L) >> 1) ;
               
               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               //rotating 0x00000001 right will produce 0x80000000 and set 
               //the sign bit.
               if (ScuDsp->ALU.part.L & 0x80000000)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;
               break;
            case 0xA: // SL
              ScuDsp->ProgControlPort.part.C = (ScuDsp->AC.part.L >> 31) & 0x01;

               ScuDsp->ALU.part.L = (u32)(ScuDsp->AC.part.L << 1);

               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               if (ScuDsp->ALU.part.L & 0x80000000)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;
               break;
            case 0xB: // RL

              ScuDsp->ProgControlPort.part.C = (ScuDsp->AC.part.L >> 31) & 0x01;

               ScuDsp->ALU.part.L = (((u32)ScuDsp->AC.part.L << 1) | ScuDsp->ProgControlPort.part.C);
               
               if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;
         
               if (ScuDsp->ALU.part.L & 0x80000000)
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;
               
               //ScuDsp->AC.part.L = ScuDsp->ALU.part.L;
               break;
            case 0xF: // RL8

              ScuDsp->ProgControlPort.part.C = (ScuDsp->AC.part.L >> 24) & 0x01;
              ScuDsp->ALU.part.L  = ((u32)(ScuDsp->AC.part.L << 8) | ((ScuDsp->AC.part.L >> 24) & 0xFF)) ;

              if (ScuDsp->ALU.part.L == 0)
                  ScuDsp->ProgControlPort.part.Z = 1;
               else
                  ScuDsp->ProgControlPort.part.Z = 0;

               //rotating 0x00ffffff left 8 will produce 0xffffff00 and
               //set the sign bit
               if ( ScuDsp->ALU.part.L & 0x80000000 )
                  ScuDsp->ProgControlPort.part.S = 1;
               else
                  ScuDsp->ProgControlPort.part.S = 0;

               //rotating 0xff000000 left 8 will produce 0x000000ff and set the
               //carry bit
               //ScuDsp->ProgControlPort.part.C = (ScuDsp->AC.part.L >> 24) & 0x01;
               break;
            default: break;
         }

         
         switch (instruction >> 30) {
         case 0x00: // Operation Commands
               switch ((instruction >> 23) & 0x3)
               {
                  case 2: // MOV MUL, P
                    ScuDsp->P.all = (s64)ScuDsp->RX * (s32)ScuDsp->RY; // ScuDsp->MUL.all;
                     break;
                  case 3: // MOV [s], P
                     //s32 cast to sign extend
                    ScuDsp->P.all = (s64)(s32)readgensrc((instruction >> 20) & 0x7);
                     break;
                  default: break;
               }
               // X-bus
               if ((instruction >> 23) & 0x4)
               {
                 // MOV [s], X
                 ScuDsp->RX = readgensrc((instruction >> 20) & 0x7);
               }

               // Y-bus
               if ((instruction >> 17) & 0x4) 
               {
                  // MOV [s], Y
                  ScuDsp->RY = readgensrc((instruction >> 14) & 0x7);
               }
               switch ((instruction >> 17) & 0x3)
               {
                  case 1: // CLR A
                     ScuDsp->AC.all = 0;
                     break;
                  case 2: // MOV ALU,A
                     ScuDsp->AC.all = ScuDsp->ALU.all;
                     break;
                  case 3: // MOV [s],A
                     //s32 cast to sign extend
                     ScuDsp->AC.all = (s64)(s32)readgensrc((instruction >> 14) & 0x7);
                     break;
                  default: break;
               }

   
               // D1-bus
               switch ((instruction >> 12) & 0x3)
               {
                  case 1: // MOV SImm,[d]
                    if (incFlg[0] != 0){ ScuDsp->CT[0]++; ScuDsp->CT[0] &= 0x3f; incFlg[0] = 0; };
                    if (incFlg[1] != 0){ ScuDsp->CT[1]++; ScuDsp->CT[1] &= 0x3f; incFlg[1] = 0; };
                    if (incFlg[2] != 0){ ScuDsp->CT[2]++; ScuDsp->CT[2] &= 0x3f; incFlg[2] = 0; };
                    if (incFlg[3] != 0){ ScuDsp->CT[3]++; ScuDsp->CT[3] &= 0x3f; incFlg[3] = 0; };
                     writed1busdest((instruction >> 8) & 0xF, (u32)(signed char)(instruction & 0xFF));
                     break;
                  case 3: // MOV [s],[d]
                     writed1busdest((instruction >> 8) & 0xF, readgensrc(instruction & 0xF));
                     break;
                  default: break;
               }

               break;
            case 0x02: // Load Immediate Commands
               if ((instruction >> 25) & 1)
               {
                  switch ((instruction >> 19) & 0x3F) {
                     case 0x01: // MVI Imm,[d]NZ
                        if (!ScuDsp->ProgControlPort.part.Z)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x02: // MVI Imm,[d]NS
                        if (!ScuDsp->ProgControlPort.part.S)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x03: // MVI Imm,[d]NZS
                        if (!ScuDsp->ProgControlPort.part.Z || !ScuDsp->ProgControlPort.part.S)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x04: // MVI Imm,[d]NC
                        if (!ScuDsp->ProgControlPort.part.C)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x08: // MVI Imm,[d]NT0
                        if (!ScuDsp->ProgControlPort.part.T0)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x21: // MVI Imm,[d]Z
                        if (ScuDsp->ProgControlPort.part.Z)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x22: // MVI Imm,[d]S
                        if (ScuDsp->ProgControlPort.part.S)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x23: // MVI Imm,[d]ZS
                        if (ScuDsp->ProgControlPort.part.Z || ScuDsp->ProgControlPort.part.S)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x24: // MVI Imm,[d]C
                        if (ScuDsp->ProgControlPort.part.C)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     case 0x28: // MVI Imm,[d]T0
                        if (ScuDsp->ProgControlPort.part.T0)
                           writeloadimdest((instruction >> 26) & 0xF, (instruction & 0x7FFFF) | ((instruction & 0x40000) ? 0xFFF80000 : 0x00000000));
                        break;
                     default: break;
                  }
               }
               else
               {
                  // MVI Imm,[d]
                  int value = (instruction & 0x1FFFFFF);
                  if (value & 0x1000000) value |= 0xfe000000;
                  writeloadimdest((instruction >> 26) & 0xF, value);
                }
               break;
            case 0x03: // Other
            {
               switch((instruction >> 28) & 0xF) {
                 case 0x0C: // DMA Commands
                 {
                   if (((instruction >> 10) & 0x1F) == 0x00/*0x08*/)
                   {
                       dsp_dma01(ScuDsp, instruction);
                   }
                   else if (((instruction >> 10) & 0x1F) == 0x04)
                   {
                       dsp_dma02(ScuDsp, instruction);
                   }
                   else if (((instruction >> 11) & 0x0F) == 0x04)
                   {
                       dsp_dma03(ScuDsp, instruction);
                   }
                   else if (((instruction >> 10) & 0x1F) == 0x0C)
                   {
                       dsp_dma04(ScuDsp, instruction);
                   }
                   else if (((instruction >> 11) & 0x0F) == 0x08)
                   {
                       dsp_dma05(ScuDsp, instruction);
                   }
                   else if (((instruction >> 10) & 0x1F) == 0x14)
                   {
                       dsp_dma06(ScuDsp, instruction);
                   }
                   else if (((instruction >> 11) & 0x0F) == 0x0C)
                   {
                       dsp_dma07(ScuDsp, instruction);
                   }
                   else if (((instruction >> 10) & 0x1F) == 0x1C)
                   {
                       dsp_dma08(ScuDsp, instruction);
                   }
                     break;
                  }
                  case 0x0D: // Jump Commands
                     switch ((instruction >> 19) & 0x7F) {
                        case 0x00: // JMP Imm
                           ScuDsp->jmpaddr = instruction & 0xFF;
                           ScuDsp->delayed = 0;
                           break;
                        case 0x41: // JMP NZ, Imm
                           if (!ScuDsp->ProgControlPort.part.Z)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }
                           break;
                        case 0x42: // JMP NS, Imm
                           if (!ScuDsp->ProgControlPort.part.S)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }

                           //LOG("scu\t: JMP NS: S = %d, jmpaddr = %08X\n", (unsigned int)ScuDsp->ProgControlPort.part.S, (unsigned int)ScuDsp->jmpaddr);
                           break;
                        case 0x43: // JMP NZS, Imm
                           if (!ScuDsp->ProgControlPort.part.Z || !ScuDsp->ProgControlPort.part.S)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }

                           //LOG("scu\t: JMP NZS: Z = %d, S = %d, jmpaddr = %08X\n", (unsigned int)ScuDsp->ProgControlPort.part.Z, (unsigned int)ScuDsp->ProgControlPort.part.S, (unsigned int)ScuDsp->jmpaddr);
                           break;
                        case 0x44: // JMP NC, Imm
                           if (!ScuDsp->ProgControlPort.part.C)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }
                           break;
                        case 0x48: // JMP NT0, Imm
                           if (!ScuDsp->ProgControlPort.part.T0)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }

                           //LOG("scu\t: JMP NT0: T0 = %d, jmpaddr = %08X\n", (unsigned int)ScuDsp->ProgControlPort.part.T0, (unsigned int)ScuDsp->jmpaddr);
                           break;
                        case 0x61: // JMP Z,Imm
                           if (ScuDsp->ProgControlPort.part.Z)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }
                           break;
                        case 0x62: // JMP S, Imm
                           if (ScuDsp->ProgControlPort.part.S)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }

                           //LOG("scu\t: JMP S: S = %d, jmpaddr = %08X\n", (unsigned int)ScuDsp->ProgControlPort.part.S, (unsigned int)ScuDsp->jmpaddr);
                           break;
                        case 0x63: // JMP ZS, Imm
                           if (ScuDsp->ProgControlPort.part.Z || ScuDsp->ProgControlPort.part.S)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }

                           //LOG("scu\t: JMP ZS: Z = %d, S = %d, jmpaddr = %08X\n", ScuDsp->ProgControlPort.part.Z, (unsigned int)ScuDsp->ProgControlPort.part.S, (unsigned int)ScuDsp->jmpaddr);
                           break;
                        case 0x64: // JMP C, Imm
                           if (ScuDsp->ProgControlPort.part.C)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }
                           break;
                        case 0x68: // JMP T0,Imm
                           if (ScuDsp->ProgControlPort.part.T0)
                           {
                              ScuDsp->jmpaddr = instruction & 0xFF;
                              ScuDsp->delayed = 0; 
                           }
                           break;
                        default:
                           LOG("scu\t: Unknown JMP instruction not implemented\n");
                           break;
                     }
                     break;
                  case 0x0E: // Loop bottom Commands
                     if (instruction & 0x8000000)
                     {
                        // LPS
                        if (ScuDsp->LOP != 0)
                        {
                           ScuDsp->jmpaddr = ScuDsp->PC;
                           ScuDsp->delayed = 0;
                           ScuDsp->LOP--;
                        }
                     }
                     else
                     {
                        // BTM
                        if (ScuDsp->LOP != 0)
                        {
                           ScuDsp->jmpaddr = ScuDsp->TOP;
                           ScuDsp->delayed = 0;
                           ScuDsp->LOP--;
                        }
                     }

                     break;
                  case 0x0F: // End Commands
                     ScuDsp->ProgControlPort.part.EX = 0;

                     if (instruction & 0x8000000) {
                        // End with Interrupt
                        ScuDsp->ProgControlPort.part.E = 1;
                        ScuSendDSPEnd();
                     }

                     LOG("dsp has ended\n");
                     ScuDsp->ProgControlPort.part.P = ScuDsp->PC+1;
                     dsp_counter = 1;
                     break;
                  default: break;
               }
               break;
            }
            default: 
               LOG("scu\t: Invalid DSP opcode %08X at offset %02X\n", instruction, ScuDsp->PC);
               break;
         }

         //ScuDsp->MUL.all = (s64)ScuDsp->RX * (s32)ScuDsp->RY;
         
         if (incFlg[0] != 0){ ScuDsp->CT[0]++; ScuDsp->CT[0] &= 0x3f; incFlg[0] = 0; };
         if (incFlg[1] != 0){ ScuDsp->CT[1]++; ScuDsp->CT[1] &= 0x3f; incFlg[1] = 0; };
         if (incFlg[2] != 0){ ScuDsp->CT[2]++; ScuDsp->CT[2] &= 0x3f; incFlg[2] = 0; };
         if (incFlg[3] != 0){ ScuDsp->CT[3]++; ScuDsp->CT[3] &= 0x3f; incFlg[3] = 0; };

         ScuDsp->PC++;

         // Handle delayed jumps
         if (ScuDsp->jmpaddr != 0xFFFFFFFF)
         {
            if (ScuDsp->delayed)
            {
               ScuDsp->PC = (unsigned char)ScuDsp->jmpaddr;
               ScuDsp->jmpaddr = 0xFFFFFFFF;
               dsp_counter += 1; // hold clock
            }
            else
               ScuDsp->delayed = 1;
         }
         dsp_counter--;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

static char *disd1bussrc(u8 num)
{
   switch(num) { 
      case 0x0:
         return "M0";
      case 0x1:
         return "M1";
      case 0x2:
         return "M2";
      case 0x3:
         return "M3";
      case 0x4:
         return "MC0";
      case 0x5:
         return "MC1";
      case 0x6:
         return "MC2";
      case 0x7:
         return "MC3";
      case 0x9:
         return "ALL";
      case 0xA:
         return "ALH";
      default: break;
   }

   return "??";
}

//////////////////////////////////////////////////////////////////////////////

static char *disd1busdest(u8 num)
{
   switch(num) { 
      case 0x0:
         return "MC0";
      case 0x1:
         return "MC1";
      case 0x2:
         return "MC2";
      case 0x3:
         return "MC3";
      case 0x4:
         return "RX";
      case 0x5:
         return "PL";
      case 0x6:
         return "RA0";
      case 0x7:
         return "WA0";
      case 0xA:
         return "LOP";
      case 0xB:
         return "TOP";
      case 0xC:
         return "CT0";
      case 0xD:
         return "CT1";
      case 0xE:
         return "CT2";
      case 0xF:
         return "CT3";
      default: break;
   }

   return "??";
}

//////////////////////////////////////////////////////////////////////////////

static char *disloadimdest(u8 num)
{
   switch(num) { 
      case 0x0:
         return "MC0";
      case 0x1:
         return "MC1";
      case 0x2:
         return "MC2";
      case 0x3:
         return "MC3";
      case 0x4:
         return "RX";
      case 0x5:
         return "PL";
      case 0x6:
         return "RA0";
      case 0x7:
         return "WA0";
      case 0xA:
         return "LOP";
      case 0xC:
         return "PC";
      default: break;
   }

   return "??";
}

//////////////////////////////////////////////////////////////////////////////

static char *disdmaram(u8 num)
{
   switch(num)
   {
      case 0x0: // MC0
         return "MC0";
      case 0x1: // MC1
         return "MC1";
      case 0x2: // MC2
         return "MC2";
      case 0x3: // MC3
         return "MC3";
      case 0x4: // Program Ram
         return "PRG";
      default: break;
   }

   return "??";
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspDisasm(u8 addr, char *outstring) {
   u32 instruction;
   u8 counter=0;
   u8 filllength=0;

   instruction = ScuDsp->ProgramRam[addr];

   sprintf(outstring, "%02X: ", addr);
   outstring+=strlen(outstring);

   if (instruction == 0)
   {
      sprintf(outstring, "NOP");
      return;
   }

   // Handle ALU commands
   switch (instruction >> 26)
   {
      case 0x0: // NOP
         break;
      case 0x1: // AND
         sprintf(outstring, "AND");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x2: // OR
         sprintf(outstring, "OR");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x3: // XOR
         sprintf(outstring, "XOR");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x4: // ADD
         sprintf(outstring, "ADD");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x5: // SUB
         sprintf(outstring, "SUB");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x6: // AD2
         sprintf(outstring, "AD2");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x8: // SR
         sprintf(outstring, "SR");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0x9: // RR
         sprintf(outstring, "RR");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0xA: // SL
         sprintf(outstring, "SL");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0xB: // RL
         sprintf(outstring, "RL");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      case 0xF: // RL8
         sprintf(outstring, "RL8");
         counter = (u8)strlen(outstring);
         outstring+=(u8)strlen(outstring);
         break;
      default: break;
   }

   switch (instruction >> 30) {
      case 0x00: // Operation Commands
         filllength = 5 - counter;
         memset((void  *)outstring, 0x20, filllength);
         counter += filllength;
         outstring += filllength;

         if ((instruction >> 23) & 0x4)
         {
            sprintf(outstring, "MOV %s, X", disd1bussrc((instruction >> 20) & 0x7));
            counter+=(u8)strlen(outstring);
            outstring+=(u8)strlen(outstring);
         }

         filllength = 16 - counter;
         memset((void  *)outstring, 0x20, filllength);
         counter += filllength;
         outstring += filllength;

         switch ((instruction >> 23) & 0x3)
         {
            case 2:
               sprintf(outstring, "MOV MUL, P");
               counter+=(u8)strlen(outstring);
               outstring+=(u8)strlen(outstring);
               break;
            case 3:
               sprintf(outstring, "MOV %s, P", disd1bussrc((instruction >> 20) & 0x7));
               counter+=(u8)strlen(outstring);
               outstring+=(u8)strlen(outstring);
               break;
            default: break;
         }

         filllength = 27 - counter;
         memset((void  *)outstring, 0x20, filllength);
         counter += filllength;
         outstring += filllength;

         // Y-bus
         if ((instruction >> 17) & 0x4)
         {
            sprintf(outstring, "MOV %s, Y", disd1bussrc((instruction >> 14) & 0x7));
            counter+=(u8)strlen(outstring);
            outstring+=(u8)strlen(outstring);
         }

         filllength = 38 - counter;
         memset((void  *)outstring, 0x20, filllength);
         counter += filllength;
         outstring += filllength;

         switch ((instruction >> 17) & 0x3)
         {
            case 1:
               sprintf(outstring, "CLR A");
               counter+=(u8)strlen(outstring);
               outstring+=(u8)strlen(outstring);
               break;
            case 2:
               sprintf(outstring, "MOV ALU, A");
               counter+=(u8)strlen(outstring);
               outstring+=(u8)strlen(outstring);
               break;
            case 3:
               sprintf(outstring, "MOV %s, A", disd1bussrc((instruction >> 14) & 0x7));
               counter+=(u8)strlen(outstring);
               outstring+=(u8)strlen(outstring);
               break;
            default: break;
         }

         filllength = 50 - counter;
         memset((void  *)outstring, 0x20, filllength);
         counter += filllength;
         outstring += filllength;

         // D1-bus
         switch ((instruction >> 12) & 0x3)
         {
            case 1:
               sprintf(outstring, "MOV #$%02X, %s", (unsigned int)instruction & 0xFF, disd1busdest((instruction >> 8) & 0xF));
               outstring+=(u8)strlen(outstring);
               break;
            case 3:
               sprintf(outstring, "MOV %s, %s", disd1bussrc(instruction & 0xF), disd1busdest((instruction >> 8) & 0xF));
               outstring+=(u8)strlen(outstring);
               break;
            default:
               outstring[0] = 0x00;
               break;
         }

         break;
      case 0x02: // Load Immediate Commands
         if ((instruction >> 25) & 1)
         {
            switch ((instruction >> 19) & 0x3F) {
               case 0x01:
                  sprintf(outstring, "MVI #$%05X,%s,NZ", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x02:
                  sprintf(outstring, "MVI #$%05X,%s,NS", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x03:
                  sprintf(outstring, "MVI #$%05X,%s,NZS", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x04:
                  sprintf(outstring, "MVI #$%05X,%s,NC", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x08:
                  sprintf(outstring, "MVI #$%05X,%s,NT0", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x21:
                  sprintf(outstring, "MVI #$%05X,%s,Z", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x22:
                  sprintf(outstring, "MVI #$%05X,%s,S", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x23:
                  sprintf(outstring, "MVI #$%05X,%s,ZS", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x24:
                  sprintf(outstring, "MVI #$%05X,%s,C", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               case 0x28:
                  sprintf(outstring, "MVI #$%05X,%s,T0", (unsigned int)instruction & 0x7FFFF, disloadimdest((instruction >> 26) & 0xF));
                  break;
               default: break;
            }
         }
         else
         {
           //sprintf(outstring, "MVI #$%08X,%s", (instruction & 0xFFFFFF) | ((instruction & 0x1000000) ? 0xFF000000 : 0x00000000), disloadimdest((instruction >> 26) & 0xF));
           sprintf(outstring, "MVI #$%08X,%s", (instruction & 0x1FFFFFF) << 2,disloadimdest((instruction >> 26) & 0xF));
         }

         break;
      case 0x03: // Other
         switch((instruction >> 28) & 0x3) {
            case 0x00: // DMA Commands
            {
               int addressAdd;

               if (instruction & 0x1000)
                  addressAdd = (instruction >> 15) & 0x7;
               else
                  addressAdd = (instruction >> 15) & 0x1;

               switch(addressAdd)
               {
                  case 0: // Add 0
                     addressAdd = 0;
                     break;
                  case 1: // Add 1
                     addressAdd = 1;
                     break;
                  case 2: // Add 2
                     addressAdd = 2;
                     break;
                  case 3: // Add 4
                     addressAdd = 4;
                     break;
                  case 4: // Add 8
                     addressAdd = 8;
                     break;
                  case 5: // Add 16
                     addressAdd = 16;
                     break;
                  case 6: // Add 32
                     addressAdd = 32;
                     break;
                  case 7: // Add 64
                     addressAdd = 64;
                     break;
                  default:
                     addressAdd = 0;
                     break;
               }

               LOG("DMA Add = %X, addressAdd = %d", (instruction >> 15) & 0x7, addressAdd);

               // Write Command name
               sprintf(outstring, "DMA");
               outstring+=(u8)strlen(outstring);

               // Is h bit set?
               if (instruction & 0x4000)
               {
                  outstring[0] = 'H';
                  outstring++;
               }

               sprintf(outstring, "%d ", addressAdd);
               outstring+=(u8)strlen(outstring);

               if (instruction & 0x2000)
               {
                  // Command Format 2                 
                  if (instruction & 0x1000)
                     sprintf(outstring, "%s, D0, %s", disdmaram((instruction >> 8) & 0x7), disd1bussrc(instruction & 0x7));
                  else
                     sprintf(outstring, "D0, %s, %s", disdmaram((instruction >> 8) & 0x7), disd1bussrc(instruction & 0x7));
               }
               else
               {
                  // Command Format 1
                  if (instruction & 0x1000)
                     sprintf(outstring, "%s, D0, #$%02X", disdmaram((instruction >> 8) & 0x7), (int)(instruction & 0xFF));
                  else
                     sprintf(outstring, "D0, %s, #$%02X", disdmaram((instruction >> 8) & 0x7), (int)(instruction & 0xFF));
               }
               
               break;
            }
            case 0x01: // Jump Commands
               switch ((instruction >> 19) & 0x7F) {
                  case 0x00:
                     sprintf(outstring, "JMP $%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x41:
                     sprintf(outstring, "JMP NZ,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x42:
                     sprintf(outstring, "JMP NS,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x43:
                     sprintf(outstring, "JMP NZS,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x44:
                     sprintf(outstring, "JMP NC,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x48:
                     sprintf(outstring, "JMP NT0,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x61:
                     sprintf(outstring, "JMP Z,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x62:
                     sprintf(outstring, "JMP S,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x63:
                     sprintf(outstring, "JMP ZS,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x64:
                     sprintf(outstring, "JMP C,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  case 0x68:
                     sprintf(outstring, "JMP T0,$%02X", (unsigned int)instruction & 0xFF);
                     break;
                  default:
                     sprintf(outstring, "Unknown JMP");
                     break;
               }
               break;
            case 0x02: // Loop bottom Commands
               if (instruction & 0x8000000)
                  sprintf(outstring, "LPS");
               else
                  sprintf(outstring, "BTM");

               break;
            case 0x03: // End Commands
               if (instruction & 0x8000000)
                  sprintf(outstring, "ENDI");
               else
                  sprintf(outstring, "END");

               break;
            default: break;
         }
         break;
      default: 
         sprintf(outstring, "Invalid opcode");
         break;
   }
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspStep(void) {
   if (ScuDsp)
      ScuExec(1);
}

//////////////////////////////////////////////////////////////////////////////

int ScuDspSaveProgram(const char *filename) {
   FILE *fp;
   u32 i;
   u8 *buffer;

   if (!filename)
      return -1;

   if ((fp = fopen(filename, "wb")) == NULL)
      return -1;

   if ((buffer = (u8 *)malloc(sizeof(ScuDsp->ProgramRam))) == NULL)
   {
      fclose(fp);
      return -2;
   }

   for (i = 0; i < 256; i++)
   {
      buffer[i * 4] = (u8)(ScuDsp->ProgramRam[i] >> 24);
      buffer[(i * 4)+1] = (u8)(ScuDsp->ProgramRam[i] >> 16);
      buffer[(i * 4)+2] = (u8)(ScuDsp->ProgramRam[i] >> 8);
      buffer[(i * 4)+3] = (u8)ScuDsp->ProgramRam[i];
   }

   fwrite((void *)buffer, 1, sizeof(ScuDsp->ProgramRam), fp);
   fclose(fp);
   free(buffer);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

int ScuDspSaveMD(const char *filename, int num) {
   FILE *fp;
   u32 i;
   u8 *buffer;

   if (!filename)
      return -1;

   if ((fp = fopen(filename, "wb")) == NULL)
      return -1;

   if ((buffer = (u8 *)malloc(sizeof(ScuDsp->MD[num]))) == NULL)
   {
      fclose(fp);
      return -2;
   }

   for (i = 0; i < 64; i++)
   {
      buffer[i * 4] = (u8)(ScuDsp->MD[num][i] >> 24);
      buffer[(i * 4)+1] = (u8)(ScuDsp->MD[num][i] >> 16);
      buffer[(i * 4)+2] = (u8)(ScuDsp->MD[num][i] >> 8);
      buffer[(i * 4)+3] = (u8)ScuDsp->MD[num][i];
   }

   fwrite((void *)buffer, 1, sizeof(ScuDsp->MD[num]), fp);
   fclose(fp);
   free(buffer);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspGetRegisters(scudspregs_struct *regs) {
   if (regs != NULL) {
      memcpy(regs->ProgramRam, ScuDsp->ProgramRam, sizeof(u32) * 256);
      memcpy(regs->MD, ScuDsp->MD, sizeof(u32) * 64 * 4);

      regs->ProgControlPort.all = ScuDsp->ProgControlPort.all;
      regs->ProgControlPort.part.P = regs->PC = ScuDsp->PC;
      regs->TOP = ScuDsp->TOP;
      regs->LOP = ScuDsp->LOP;
      regs->jmpaddr = ScuDsp->jmpaddr;
      regs->delayed = ScuDsp->delayed;
      regs->DataRamPage = ScuDsp->DataRamPage;
      regs->DataRamReadAddress = ScuDsp->DataRamReadAddress;
      memcpy(regs->CT, ScuDsp->CT, sizeof(u8) * 4);
      regs->RX = ScuDsp->RX;
      regs->RY = ScuDsp->RY;
      regs->RA0 = ScuDsp->RA0;
      regs->WA0 = ScuDsp->WA0;

      regs->AC.all = ScuDsp->AC.all;
      regs->P.all = ScuDsp->P.all;
      regs->ALU.all = ScuDsp->ALU.all;
      regs->MUL.all = ScuDsp->MUL.all;
   }
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspSetRegisters(scudspregs_struct *regs) {
   if (regs != NULL) {
      memcpy(ScuDsp->ProgramRam, regs->ProgramRam, sizeof(u32) * 256);
      memcpy(ScuDsp->MD, regs->MD, sizeof(u32) * 64 * 4);

      ScuDsp->ProgControlPort.all = regs->ProgControlPort.all;
      ScuDsp->PC = regs->ProgControlPort.part.P;
      ScuDsp->TOP = regs->TOP;
      ScuDsp->LOP = regs->LOP;
      ScuDsp->jmpaddr = regs->jmpaddr;
      ScuDsp->delayed = regs->delayed;
      ScuDsp->DataRamPage = regs->DataRamPage;
      ScuDsp->DataRamReadAddress = regs->DataRamReadAddress;
      memcpy(ScuDsp->CT, regs->CT, sizeof(u8) * 4);
      ScuDsp->RX = regs->RX;
      ScuDsp->RY = regs->RY;
      ScuDsp->RA0 = regs->RA0;
      ScuDsp->WA0 = regs->WA0;

      ScuDsp->AC.all = regs->AC.all;
      ScuDsp->P.all = regs->P.all;
      ScuDsp->ALU.all = regs->ALU.all;
      ScuDsp->MUL.all = regs->MUL.all;
   }
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspSetBreakpointCallBack(void (*func)(u32)) {
   ScuBP->BreakpointCallBack = func;
}

//////////////////////////////////////////////////////////////////////////////

int ScuDspAddCodeBreakpoint(u32 addr) {
   int i;

   if (ScuBP->numcodebreakpoints < MAX_BREAKPOINTS) {
      // Make sure it isn't already on the list
      for (i = 0; i < ScuBP->numcodebreakpoints; i++)
      {
         if (addr == ScuBP->codebreakpoint[i].addr)
            return -1;
      }

      ScuBP->codebreakpoint[ScuBP->numcodebreakpoints].addr = addr;
      ScuBP->numcodebreakpoints++;

      return 0;
   }

   return -1;
}

//////////////////////////////////////////////////////////////////////////////

static void ScuDspSortCodeBreakpoints(void) {
   int i, i2;
   u32 tmp;

   for (i = 0; i < (MAX_BREAKPOINTS-1); i++)
   {
      for (i2 = i+1; i2 < MAX_BREAKPOINTS; i2++)
      {
         if (ScuBP->codebreakpoint[i].addr == 0xFFFFFFFF &&
            ScuBP->codebreakpoint[i2].addr != 0xFFFFFFFF)
         {
            tmp = ScuBP->codebreakpoint[i].addr;
            ScuBP->codebreakpoint[i].addr = ScuBP->codebreakpoint[i2].addr;
            ScuBP->codebreakpoint[i2].addr = tmp;
         }
      }
   } 
}

//////////////////////////////////////////////////////////////////////////////

int ScuDspDelCodeBreakpoint(u32 addr) {
   int i;

   if (ScuBP->numcodebreakpoints > 0) {
      for (i = 0; i < ScuBP->numcodebreakpoints; i++) {
         if (ScuBP->codebreakpoint[i].addr == addr)
         {
            ScuBP->codebreakpoint[i].addr = 0xFFFFFFFF;
            ScuDspSortCodeBreakpoints();
            ScuBP->numcodebreakpoints--;
            return 0;
         }
      }
   }
   
   return -1;
}

//////////////////////////////////////////////////////////////////////////////

scucodebreakpoint_struct *ScuDspGetBreakpointList(void) {
   return ScuBP->codebreakpoint;
}

//////////////////////////////////////////////////////////////////////////////

void ScuDspClearCodeBreakpoints(void) {
   int i;
   for (i = 0; i < MAX_BREAKPOINTS; i++)
      ScuBP->codebreakpoint[i].addr = 0xFFFFFFFF;

   ScuBP->numcodebreakpoints = 0;
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL ScuReadByte(u32 addr) {
   addr &= 0xFF;

   switch(addr) {
      case 0xA7:
         return (ScuRegs->IST & 0xFF);
      default:
         LOG("Unhandled SCU Register byte read %08X\n", addr);
         return 0;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL ScuReadWord(u32 addr) {
   addr &= 0xFF;
   LOG("Unhandled SCU Register word read %08X\n", addr);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL ScuReadLong(u32 addr) {
   addr &= 0xFF;
   //LOG("scu: read  %08X @ %08X", addr, CurrentSH2->regs.PC);
   switch(addr) {
      case 0:
         return ScuRegs->D0R;
      case 4:
         return ScuRegs->D0W;
      case 8:
         return ScuRegs->D0C;
      case 0x20:
         return ScuRegs->D1R;
      case 0x24:
         return ScuRegs->D1W;
      case 0x28:
         return ScuRegs->D1C;
      case 0x40:
         return ScuRegs->D2R;
      case 0x44:
         return ScuRegs->D2W;
      case 0x48:
         return ScuRegs->D2C;
      case 0x7C: {
        if (ScuRegs->dma0.TransferNumber > 0) { ScuRegs->DSTA |= 0x10; }else{ ScuRegs->DSTA &= ~0x10;  }
        if (ScuRegs->dma1.TransferNumber > 0) { ScuRegs->DSTA |= 0x100; }else{ ScuRegs->DSTA &= ~0x100;  }
        if (ScuRegs->dma2.TransferNumber > 0) { ScuRegs->DSTA |= 0x1000; }else{ ScuRegs->DSTA &= ~0x1000; }
        return ScuRegs->DSTA;
      }
      case 0x80: // DSP Program Control Port
         return (ScuDsp->ProgControlPort.all & 0x00FD00FF);
      case 0x8C: // DSP Data Ram Data Port
         if (!ScuDsp->ProgControlPort.part.EX)
            return ScuDsp->MD[ScuDsp->DataRamPage][ScuDsp->DataRamReadAddress++];
         else
            return 0;
      case 0xA4:
         //LOG("Read IST %08X", ScuRegs->IST);
         return ScuRegs->IST;
      case 0xA8:
         return ScuRegs->AIACK;
      case 0xC4:
         return ScuRegs->RSEL;
      case 0xC8:
         return ScuRegs->VER;
      default:
         LOG("Unhandled SCU Register long read %08X\n", addr);
         return 0;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL ScuWriteByte(u32 addr, u8 val) {
   addr &= 0xFF;
   switch(addr) {
      case 0xA7:
      {
        u32 after = ScuRegs->IST & (0xFFFFFF00 | val);
        ScuRemoveInterruptByCPU(ScuRegs->IST, after);
        ScuRegs->IST = after; // double check this
        ScuTestInterruptMask();
      }
         return;
      default:
         LOG("Unhandled SCU Register byte write %08X\n", addr);
         return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL ScuWriteWord(u32 addr, UNUSED u16 val) {
   addr &= 0xFF;
   LOG("Unhandled SCU Register word write %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL ScuWriteLong(u32 addr, u32 val) {
   addr &= 0xFF;
  //if (addr!= 0xA0)
  //LOG("scu: write %08X:%08X @ %08X", addr, val, CurrentSH2->regs.PC);
   switch(addr) {
      case 0:
         ScuRegs->D0R = val;
         break;
      case 4:
         ScuRegs->D0W = val;
         break;
      case 8:
         ScuRegs->D0C = val;
         break;
      case 0xC:
         ScuRegs->D0AD = val;
         break;
      case 0x10:
      if ((val & 0x1) && ((ScuRegs->D0MD&0x7)==0x7) )
         {
            if (ScuRegs->dma0.TransferNumber != 0) {
              ScuDmaProc(ScuRegs, 0x7FFFFFFF);
            }
            ScuRegs->dma0.mode = 0;
            ScuRegs->dma0.ReadAddress = ScuRegs->D0R;
            ScuRegs->dma0.WriteAddress = ScuRegs->D0W;
            ScuRegs->dma0.TransferNumber = ScuRegs->D0C;
            ScuRegs->dma0.AddValue = ScuRegs->D0AD;
            ScuRegs->dma0.ModeAddressUpdate = ScuRegs->D0MD;
#if OLD_DMA
            ScuDMA(&ScuRegs->dma0);
#else
            ScuSetAddValue(&ScuRegs->dma0);
            ScuDmaProc(ScuRegs, 128);
#endif
         }
         ScuRegs->D0EN = val;
         break;
      case 0x14:
         ScuRegs->D0MD = val;
         break;
      case 0x20:
         ScuRegs->D1R = val;
         break;
      case 0x24:
         ScuRegs->D1W = val;
         break;
      case 0x28:
         ScuRegs->D1C = val;
         break;
      case 0x2C:
         ScuRegs->D1AD = val;
         break;
      case 0x30:
      if ((val & 0x1) && ((ScuRegs->D1MD&0x07) == 0x7))
         {
            if (ScuRegs->dma1.TransferNumber != 0) {
              ScuDmaProc(ScuRegs, 0x7FFFFFFF);
            }

            ScuRegs->dma1.mode = 1;
            ScuRegs->dma1.ReadAddress = ScuRegs->D1R;
            ScuRegs->dma1.WriteAddress = ScuRegs->D1W;
            ScuRegs->dma1.TransferNumber = ScuRegs->D1C;
            ScuRegs->dma1.AddValue = ScuRegs->D1AD;
            ScuRegs->dma1.ModeAddressUpdate = ScuRegs->D1MD;
#if OLD_DMA
            ScuDMA(&ScuRegs->dma1);
#else
            ScuSetAddValue(&ScuRegs->dma1);
            ScuDmaProc(ScuRegs,128);
#endif
      

         }
         ScuRegs->D1EN = val;
         break;
      case 0x34:
         ScuRegs->D1MD = val;
         break;
      case 0x40:
         ScuRegs->D2R = val;
         break;
      case 0x44:
         ScuRegs->D2W = val;
         break;
      case 0x48:
         ScuRegs->D2C = val;
         break;
      case 0x4C:
         ScuRegs->D2AD = val;
         break;
      case 0x50:
      if ((val & 0x1) && ((ScuRegs->D2MD & 0x7) == 0x7))
         {

            if (ScuRegs->dma2.TransferNumber != 0) {
              ScuDmaProc(ScuRegs, 0x7FFFFFFF);
            }

            ScuRegs->dma2.mode = 2;
            ScuRegs->dma2.ReadAddress = ScuRegs->D2R;
            ScuRegs->dma2.WriteAddress = ScuRegs->D2W;
            ScuRegs->dma2.TransferNumber = ScuRegs->D2C;
            ScuRegs->dma2.AddValue = ScuRegs->D2AD;
            ScuRegs->dma2.ModeAddressUpdate = ScuRegs->D2MD;
#if OLD_DMA
            ScuDMA(&ScuRegs->dma2);
#else
            ScuSetAddValue(&ScuRegs->dma2);
            ScuDmaProc(ScuRegs, 128);
#endif
            
         }
         ScuRegs->D2EN = val;
         break;
      case 0x54:
         ScuRegs->D2MD = val;
         break;
      case 0x60:
         ScuRegs->DSTP = val;
         break;
      case 0x7C:
        ScuRegs->DSTA = val;
        break;
      case 0x80: // DSP Program Control Port
         LOG("scu: wrote %08X to DSP Program Control Port", val);
         ScuDsp->ProgControlPort.all = (ScuDsp->ProgControlPort.all & 0x00FC0000) | (val & 0x060380FF);

         if (ScuDsp->ProgControlPort.part.LE) {
            // set pc
            ScuDsp->PC = (u8)ScuDsp->ProgControlPort.part.P;
            LOG("scu: DSP set pc = %02X", ScuDsp->PC);
         }

         // Execution is rquested
         if (val & 0x10000) {
           // clear internal values
           ScuDsp->jmpaddr = 0xffffffff;
         }

#ifdef DEBUG
         if (ScuDsp->ProgControlPort.part.EX)
            LOG("scu: DSP executing: PC = %02X", ScuDsp->PC);
#endif
         break;
      case 0x84: // DSP Program Ram Data Port
         //LOG("scu: wrote %08X to DSP Program ram offset %02X", val, ScuDsp->PC);
         ScuDsp->ProgramRam[ScuDsp->PC] = val;
         ScuDsp->PC++;
         ScuDsp->ProgControlPort.part.P = ScuDsp->PC;
         break;
      case 0x88: // DSP Data Ram Address Port
         //LOG("scu: wrote %08X to DSP Data Ram ", val);
         ScuDsp->DataRamPage = (val >> 6) & 3;
         ScuDsp->DataRamReadAddress = val & 0x3F;
         break;
      case 0x8C: // DSP Data Ram Data Port
         //LOG("scu: wrote %08X to DSP Data Ram Data Port Page %d offset %02X", val, ScuDsp->DataRamPage, ScuDsp->DataRamReadAddress);
         if (!ScuDsp->ProgControlPort.part.EX) {
            ScuDsp->MD[ScuDsp->DataRamPage][ScuDsp->DataRamReadAddress] = val;
            ScuDsp->DataRamReadAddress++;
         }
         break;
      case 0x90:
         ScuRegs->T0C = val;
         break;
      case 0x94:
         ScuRegs->T1S = val;
         ScuRegs->timer1_set = 1;
         ScuRegs->timer1_preset = val;
         break;
      case 0x98:
         ScuRegs->T1MD = val;
         break;
      case 0xA0:
         ScuRegs->IMS = val;
         //LOG("scu\t: IMS = %X PC=%X frame=%d:%d", val, CurrentSH2->regs.PC, yabsys.frame_count,yabsys.LineCount);
         ScuTestInterruptMask();
         break;
      case 0xA4: {
        u32 after = ScuRegs->IST & val;
        ScuRemoveInterruptByCPU(ScuRegs->IST, after);
        ScuRegs->IST = after;
        //LOG("scu\t: IST = %X PC=%X frame=%d:%d", val, CurrentSH2->regs.PC, yabsys.frame_count, yabsys.LineCount);
        ScuTestInterruptMask();
      }
         break;
      case 0xA8:
         ScuRegs->AIACK = val;
         ScuTestInterruptMask();
         break;
      case 0xB0:
         ScuRegs->ASR0 = val;
         break;
      case 0xB4:
         ScuRegs->ASR1 = val;
         break;
      case 0xB8:
         ScuRegs->AREF = val;
         break;
      case 0xC4:
         ScuRegs->RSEL = val;
         break;
      default:
         LOG("Unhandled SCU Register long write %08X\n", addr);
         break;
   }
}

//////////////////////////////////////////////////////////////////////////////

void ScuRemoveInterruptByCPU(u32 pre, u32 after) {
  for (int i = 0; i < 16; i++) {
    if (((pre >> i) & 0x01) && ((after >> i) & 0x01 == 0)) {
      u32 ii, i2;
      int hit = -1;
      for (ii = 0; ii < ScuRegs->NumberOfInterrupts; ii++) {
        if (ScuRegs->interrupts[i].statusbit == (1<<i)) {
          hit = ii;
          LOG("%s(%0X) is removed at frame %d:%d", ScuGetVectorString(ScuRegs->interrupts[i].vector), ScuRegs->interrupts[i].vector, yabsys.frame_count, yabsys.LineCount);
          break;
        }
      }
      if (hit != -1) {
        i2 = 0;
        for (ii = 0; ii < ScuRegs->NumberOfInterrupts; ii++) {
          if (ii != hit) {
            memcpy(&ScuRegs->interrupts[i2], &ScuRegs->interrupts[ii], sizeof(scuinterrupt_struct));
            i2++;
          }
        }
        ScuRegs->NumberOfInterrupts--;
      }
    }
  }
}

void ScuTestInterruptMask()
{
   unsigned int i, i2;

   // Handle SCU interrupts
   for (i = 0; i < ScuRegs->NumberOfInterrupts; i++)
   {
     u32 mask = ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].mask;

     // A-BUS?
     if (mask & 0x8000){
       if (ScuRegs->AIACK){
         ScuRegs->AIACK = 0;
         if (!(ScuRegs->IMS & 0x8000)) {
           SH2SendInterrupt(MSH2, ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].vector, ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].level);
           ScuRegs->IST &= ~ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].statusbit;

           // Shorten list
           for (i2 = ScuRegs->NumberOfInterrupts - 1 - i; i2 < (ScuRegs->NumberOfInterrupts - 1); i2++)
             memcpy(&ScuRegs->interrupts[i2], &ScuRegs->interrupts[i2 + 1], sizeof(scuinterrupt_struct));

           ScuRegs->NumberOfInterrupts--;
           ScuRegs->AIACK = 0;
         }
       }
     }else if (!(ScuRegs->IMS & mask)) {

       // removed manually
       if ( (ScuRegs->IST & ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].statusbit) == 0) {

         //LOG("removed");

       }
       else {
         u8 vector = ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].vector;
         LOG("%s(%0X) delay at frame %d:%d", ScuGetVectorString(vector), vector, yabsys.frame_count, yabsys.LineCount);

         SH2SendInterrupt(MSH2, ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].vector, ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].level);
         ScuRegs->IST &= ~ScuRegs->interrupts[ScuRegs->NumberOfInterrupts - 1 - i].statusbit;

         // Shorten list
         for (i2 = ScuRegs->NumberOfInterrupts - 1 - i; i2 < (ScuRegs->NumberOfInterrupts - 1); i2++)
           memcpy(&ScuRegs->interrupts[i2], &ScuRegs->interrupts[i2 + 1], sizeof(scuinterrupt_struct));

         ScuRegs->NumberOfInterrupts--;
         break;
       }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////
static void ScuQueueInterrupt(u8 vector, u8 level, u16 mask, u32 statusbit)
{
   u32 i, i2;
   scuinterrupt_struct tmp;

   // Make sure interrupt doesn't already exist
   for (i = 0; i < ScuRegs->NumberOfInterrupts; i++)
   {
      if (ScuRegs->interrupts[i].vector == vector)
         return;
   }

   ScuRegs->interrupts[ScuRegs->NumberOfInterrupts].vector = vector;
   ScuRegs->interrupts[ScuRegs->NumberOfInterrupts].level = level;
   ScuRegs->interrupts[ScuRegs->NumberOfInterrupts].mask = mask;
   ScuRegs->interrupts[ScuRegs->NumberOfInterrupts].statusbit = statusbit;
   ScuRegs->NumberOfInterrupts++;

   // Sort interrupts
   for (i = 0; i < (ScuRegs->NumberOfInterrupts-1); i++)
   {
      for (i2 = i+1; i2 < ScuRegs->NumberOfInterrupts; i2++)
      {
         if (ScuRegs->interrupts[i].level > ScuRegs->interrupts[i2].level)
         {
            memcpy(&tmp, &ScuRegs->interrupts[i], sizeof(scuinterrupt_struct));
            memcpy(&ScuRegs->interrupts[i], &ScuRegs->interrupts[i2], sizeof(scuinterrupt_struct));
            memcpy(&ScuRegs->interrupts[i2], &tmp, sizeof(scuinterrupt_struct));
         }
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

static INLINE void SendInterrupt(u8 vector, u8 level, u16 mask, u32 statusbit) {

  // A-BUS?
  if ((mask & 0x8000) ){
    if (ScuRegs->AIACK){
      ScuRegs->AIACK = 0;
      if (!(ScuRegs->IMS & 0x8000)){
        SH2SendInterrupt(MSH2, vector, level);
      }
    }
  }else if (!(ScuRegs->IMS & mask)){
    //if (vector != 0x41) LOG("INT %d", vector);
    //LOG("%s(%x) at frame %d:%d", ScuGetVectorString(vector), vector, yabsys.frame_count, yabsys.LineCount);
    SH2SendInterrupt(MSH2, vector, level);
  }
  else
   {
      //LOG("%s(%x) is Queued %d:%d", ScuGetVectorString(vector), vector, yabsys.frame_count, yabsys.LineCount);
      ScuQueueInterrupt(vector, level, mask, statusbit);
      ScuRegs->IST |= statusbit;
   }
   if (yabsys.IsSSH2Running) {
     if( vector == 0x42 ) 
       SH2SendInterrupt(SSH2, 0x41, 1);
     if( vector == 0x40 ) 
       SH2SendInterrupt(SSH2, 0x43, 2);
  }
}

// 3.2 DMA control register
static INLINE void ScuChekIntrruptDMA(int id){

  if ((ScuRegs->D0EN & 0x100) && (ScuRegs->D0MD & 0x07) == id){
    if (ScuRegs->dma0.TransferNumber > 0) {
      ScuDmaProc(ScuRegs, 0x7FFFFFFF);
    }
    ScuRegs->dma0.mode = 0;
    ScuRegs->dma0.ReadAddress = ScuRegs->D0R;
    ScuRegs->dma0.WriteAddress = ScuRegs->D0W;
    ScuRegs->dma0.TransferNumber = ScuRegs->D0C;
    ScuRegs->dma0.AddValue = ScuRegs->D0AD;
    ScuRegs->dma0.ModeAddressUpdate = ScuRegs->D0MD;
#if OLD_DMA
    ScuDMA(&ScuRegs->dma0);
#else
    ScuSetAddValue(&ScuRegs->dma0);
    ScuDmaProc(ScuRegs, 128);
#endif
    ScuRegs->D0EN = 0;
  }
  if ((ScuRegs->D1EN & 0x100) && (ScuRegs->D1MD & 0x07) == id){
    if (ScuRegs->dma1.TransferNumber > 0) {
      ScuDmaProc(ScuRegs, 0x7FFFFFFF);
    }
    scudmainfo_struct dmainfo;
    ScuRegs->dma1.mode = 1;
    ScuRegs->dma1.ReadAddress = ScuRegs->D1R;
    ScuRegs->dma1.WriteAddress = ScuRegs->D1W;
    ScuRegs->dma1.TransferNumber = ScuRegs->D1C;
    ScuRegs->dma1.AddValue = ScuRegs->D1AD;
    ScuRegs->dma1.ModeAddressUpdate = ScuRegs->D1MD;
#if OLD_DMA
    ScuDMA(&ScuRegs->dma1);
#else
    ScuSetAddValue(&ScuRegs->dma1);
    ScuDmaProc(ScuRegs, 128);
#endif    
    ScuRegs->D1EN = 0;
  }
  if ((ScuRegs->D2EN & 0x100) && (ScuRegs->D2MD & 0x07) == id){
    if (ScuRegs->dma2.TransferNumber > 0) {
      ScuDmaProc(ScuRegs, 0x7FFFFFFF);
    }
    ScuRegs->dma2.mode = 2;
    ScuRegs->dma2.ReadAddress = ScuRegs->D2R;
    ScuRegs->dma2.WriteAddress = ScuRegs->D2W;
    ScuRegs->dma2.TransferNumber = ScuRegs->D2C;
    ScuRegs->dma2.AddValue = ScuRegs->D2AD;
    ScuRegs->dma2.ModeAddressUpdate = ScuRegs->D2MD;
#if OLD_DMA
    ScuDMA(&ScuRegs->dma2);
#else
    ScuSetAddValue(&ScuRegs->dma2);
    ScuDmaProc(ScuRegs, 128);
#endif
    ScuRegs->D2EN = 0;
  }
}

void ScuRemoveInterrupt(u8 vector, u8 level); 
void ScuRemoveVBlankOut();
void ScuRemoveHBlankIN();
void ScuRemoveVBlankIN();
void ScuRemoveTimer0();
void ScuRemoveTimer1();


const char * ScuGetVectorString(u32 vec) {
  switch (vec) {
  case 0x40:
    return "VBlankIN";
    break;
  case 0x41:
    return "VBlankOUT";
    break;
  case 0x42:
    return "HBlankIN";
    break;
  case 0x43:
    return "Timer0";
    break;
  case 0x44:
    return "Timer1";
    break;
  case 0x45:
    return "DSP End";
    break;
  case 0x47:
    return "SmpcINTBACK";
    break;
  case 0x49:
    return "DMA2 End";
  case 0x4A:
    return "DMA1 End";
  case 0x4B:
    return "DMA0 End";
  case 0x4d:
    return "DrawEnd";
    break;

  }
  return "Unknown";
}
//////////////////////////////////////////////////////////////////////////////

void ScuSendVBlankIN(void) {
   //ScuRemoveVBlankOut();
   //ScuRemoveHBlankIN();
   SendInterrupt(0x40, 0xF, 0x0001, 0x0001);
   ScuChekIntrruptDMA(0);
}

void ScuRemoveVBlankIN() {
  //ScuRemoveInterrupt(0x40, 0x0F);
  //SH2RemoveInterrupt(MSH2, 0x40, 0x0F);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendVBlankOUT(void) {
   //ScuRemoveVBlankIN();
   SendInterrupt(0x41, 0xE, 0x0002, 0x0002);
   ScuRegs->timer0 = 0;
   if (ScuRegs->T1MD & 0x1)
   {
     if (ScuRegs->timer0 == ScuRegs->T0C) {
       ScuRegs->timer0_set = 1;
       ScuSendTimer0();
     }
     else {
       ScuRegs->timer0_set = 0;
       //ScuRemoveTimer0();
     }
   }
   ScuChekIntrruptDMA(1);
}

void ScuRemoveVBlankOut() {
  //ScuRemoveInterrupt(0x41, 0x0E);
  //SH2RemoveInterrupt(MSH2, 0x41, 0x0E);
}

//////////////////////////////////////////////////////////////////////////////

void ScuRemoveHBlankIN() {
  //ScuRemoveInterrupt(0x42, 0x0D);
  //SH2RemoveInterrupt(MSH2, 0x42, 0x0D);
}


void ScuSendHBlankIN(void) {
   //if(yabsys.LineCount == 0) ScuRemoveVBlankOut();
   SendInterrupt(0x42, 0xD, 0x0004, 0x0004);
   ScuRegs->timer0++;
   if (ScuRegs->T1MD & 0x1)
   {
      // if timer0 equals timer 0 compare register, do an interrupt
     if (ScuRegs->timer0 == ScuRegs->T0C) {
        ScuSendTimer0();
        ScuRegs->timer0_set = 1;
     }
     else {
       ScuRegs->timer0_set = 0;
       //ScuRemoveTimer0();
     }

     if (ScuRegs->timer1_set == 1) {
        ScuRegs->timer1_set = 0;
        ScuRegs->timer1_counter = ScuRegs->timer1_preset;
        //ScuRemoveTimer1();
      }
   }
   ScuChekIntrruptDMA(2);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendTimer0(void) {
   SendInterrupt(0x43, 0xC, 0x0008, 0x00000008);
   ScuChekIntrruptDMA(3);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendTimer1(void) {
   SendInterrupt(0x44, 0xB, 0x0010, 0x00000010);
   ScuChekIntrruptDMA(4);
}

void ScuRemoveTimer0(void) {
  //ScuRemoveInterrupt(0x43, 0x0C);
  //SH2RemoveInterrupt(MSH2, 0x43, 0x0C);
}


void ScuRemoveTimer1(void) {
  //ScuRemoveInterrupt(0x44, 0x0B);
  //SH2RemoveInterrupt(MSH2, 0x44, 0xB);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendDSPEnd(void) {
   SendInterrupt(0x45, 0xA, 0x0020, 0x00000020);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendSoundRequest(void) {
   SendInterrupt(0x46, 0x9, 0x0040, 0x00000040);
   ScuChekIntrruptDMA(5);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendSystemManager(void) {
   SendInterrupt(0x47, 0x8, 0x0080, 0x00000080);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendPadInterrupt(void) {
   SendInterrupt(0x48, 0x8, 0x0100, 0x00000100);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendLevel2DMAEnd(void) {
   SendInterrupt(0x49, 0x6, 0x0200, 0x00000200);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendLevel1DMAEnd(void) {
   SendInterrupt(0x4A, 0x6, 0x0400, 0x00000400);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendLevel0DMAEnd(void) {
   SendInterrupt(0x4B, 0x5, 0x0800, 0x00000800);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendDMAIllegal(void) {
   SendInterrupt(0x4C, 0x3, 0x1000, 0x00001000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendDrawEnd(void) {
   SendInterrupt(0x4D, 0x2, 0x2000, 0x00002000);
   ScuChekIntrruptDMA(6);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt00(void) {
   SendInterrupt(0x50, 0x7, 0x8000, 0x00010000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt01(void) {
   SendInterrupt(0x51, 0x7, 0x8000, 0x00020000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt02(void) {
   SendInterrupt(0x52, 0x7, 0x8000, 0x00040000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt03(void) {
   SendInterrupt(0x53, 0x7, 0x8000, 0x00080000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt04(void) {
   SendInterrupt(0x54, 0x4, 0x8000, 0x00100000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt05(void) {
   SendInterrupt(0x55, 0x4, 0x8000, 0x00200000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt06(void) {
   SendInterrupt(0x56, 0x4, 0x8000, 0x00400000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt07(void) {
   SendInterrupt(0x57, 0x4, 0x8000, 0x00800000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt08(void) {
   SendInterrupt(0x58, 0x1, 0x8000, 0x01000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt09(void) {
   SendInterrupt(0x59, 0x1, 0x8000, 0x02000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt10(void) {
   SendInterrupt(0x5A, 0x1, 0x8000, 0x04000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt11(void) {
   SendInterrupt(0x5B, 0x1, 0x8000, 0x08000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt12(void) {
   SendInterrupt(0x5C, 0x1, 0x8000, 0x10000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt13(void) {
   SendInterrupt(0x5D, 0x1, 0x8000, 0x20000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt14(void) {
   SendInterrupt(0x5E, 0x1, 0x8000, 0x40000000);
}

//////////////////////////////////////////////////////////////////////////////

void ScuSendExternalInterrupt15(void) {
   SendInterrupt(0x5F, 0x1, 0x8000, 0x80000000);
}

//////////////////////////////////////////////////////////////////////////////

int ScuSaveState(FILE *fp)
{
   int offset;
   IOCheck_struct check = { 0, 0 };

   offset = StateWriteHeader(fp, "SCU ", 3);

   // Write registers and internal variables
   ywrite(&check, (void *)ScuRegs, sizeof(Scu), 1, fp);

   // Write DSP area
   ywrite(&check, (void *)ScuDsp, sizeof(scudspregs_struct), 1, fp);

   ywrite(&check, incFlg, sizeof(int), 4, fp);


   return StateFinishHeader(fp, offset);
}

//////////////////////////////////////////////////////////////////////////////

int ScuLoadState(FILE *fp, UNUSED int version, int size)
{
   IOCheck_struct check = { 0, 0 };

   // Read registers and internal variables
   if (version < 3) {
     yread(&check, (void *)ScuRegs, sizeof(Scu)-sizeof(scudmainfo_struct)*3, 1, fp);
     ScuRegs->dma0.TransferNumber = 0;
     ScuRegs->dma1.TransferNumber = 0;
     ScuRegs->dma2.TransferNumber = 0;
   }
   else {
     yread(&check, (void *)ScuRegs, sizeof(Scu), 1, fp);
   }

   // Read DSP area
   yread(&check, (void *)ScuDsp, sizeof(scudspregs_struct), 1, fp);

   if (version >= 2) {
     yread(&check, incFlg, sizeof(int), 4, fp);
   }
   return size;
}

//////////////////////////////////////////////////////////////////////////////
