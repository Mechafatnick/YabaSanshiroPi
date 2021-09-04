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

#ifndef _OPCODES_H
#define _OPCODES_H

void FASTCALL sh2_STC_SR(tagSH2*){};
void FASTCALL sh2_STC_GBR(tagSH2*){};
void FASTCALL sh2_STC_VBR(tagSH2*){};
void FASTCALL sh2_BSRF(tagSH2*){};
void FASTCALL sh2_BRAF(tagSH2*){};
void FASTCALL sh2_MOVB_R0(tagSH2*){};
void FASTCALL sh2_MOVW_R0(tagSH2*){};
void FASTCALL sh2_MOVL_R0(tagSH2*){};
void FASTCALL sh2_MUL_L(tagSH2*){};
void FASTCALL sh2_CLRT(tagSH2*){};
void FASTCALL sh2_SETT(tagSH2*){}; 
void FASTCALL sh2_CLRMAC(tagSH2*){}; 
void FASTCALL sh2_NOP(tagSH2*){};
void FASTCALL sh2_DIV0U(tagSH2*){}; 
void FASTCALL sh2_MOVT(tagSH2*){}; 
void FASTCALL sh2_STS_MACH(tagSH2*){}; 
void FASTCALL sh2_STS_MACL(tagSH2*){};
void FASTCALL sh2_STS_PR(tagSH2*){}; 
void FASTCALL sh2_RTS(tagSH2*){}; 
void FASTCALL sh2_SLEEP(tagSH2*){}; 
void FASTCALL sh2_RTE(tagSH2*){};
void FASTCALL sh2_MOVB_R0_MEM(tagSH2*){};
void FASTCALL sh2_MOVW_R0_MEM(tagSH2*){};
void FASTCALL sh2_MOVL_R0_MEM(tagSH2*){};
void FASTCALL sh2_MAC_L(tagSH2*){};
void FASTCALL sh2_MOVL_DISP_MEM(tagSH2*){};
void FASTCALL sh2_MOVB(tagSH2*){};
void FASTCALL sh2_MOVW(tagSH2*){};
void FASTCALL sh2_MOVL(tagSH2*){};
void FASTCALL sh2_MOVB_DEC(tagSH2*){};
void FASTCALL sh2_MOVW_DEC(tagSH2*){};
void FASTCALL sh2_MOVL_DEC(tagSH2*){};
void FASTCALL sh2_DIV0S(tagSH2*){};
void FASTCALL sh2_TST(tagSH2*){};
void FASTCALL sh2_AND(tagSH2*){};
void FASTCALL sh2_XOR(tagSH2*){};
void FASTCALL sh2_OR(tagSH2*){}; 
void FASTCALL sh2_CMP_STR(tagSH2*){};
void FASTCALL sh2_XTRCT(tagSH2*){};	
void FASTCALL sh2_MULU(tagSH2*){}; 
void FASTCALL sh2_MULS(tagSH2*){}; 
void FASTCALL sh2_CMP_EQ(tagSH2*){}; 
void FASTCALL sh2_CMP_HS(tagSH2*){}; 
void FASTCALL sh2_CMP_GE(tagSH2*){};
void FASTCALL sh2_DIV1(tagSH2*){};
void FASTCALL sh2_DMULU_L(tagSH2*){};
void FASTCALL sh2_CMP_HI(tagSH2*){};
void FASTCALL sh2_CMP_GT(tagSH2*){};
void FASTCALL sh2_SUB(tagSH2*){};
void FASTCALL sh2_SUBC(tagSH2*){};
void FASTCALL sh2_SUBV(tagSH2*){};
void FASTCALL sh2_ADD(tagSH2*){};
void FASTCALL sh2_DMULS_L(tagSH2*){};
void FASTCALL sh2_ADDC(tagSH2*){};
void FASTCALL sh2_ADDV(tagSH2*){};
void FASTCALL sh2_SHL(tagSH2*){};
void FASTCALL sh2_SHLR(tagSH2*){};
void FASTCALL sh2_STS_MACH_DEC(tagSH2*){};
void FASTCALL sh2_STC_SR_DEC(tagSH2*){};
void FASTCALL sh2_STC_GBR_DEC(tagSH2*){};
void FASTCALL sh2_STC_VBR_DEC(tagSH2*){};
void FASTCALL sh2_ROTL(tagSH2*){};
void FASTCALL sh2_ROTR(tagSH2*){};
void FASTCALL sh2_LDS_MACH_INC(tagSH2*){};
void FASTCALL sh2_LDC_SR_INC(tagSH2*){};
void FASTCALL sh2_SHLL2(tagSH2*){};
void FASTCALL sh2_SHLR2(tagSH2*){};
void FASTCALL sh2_LDS_MACH(tagSH2*){};
void FASTCALL sh2_JSR(tagSH2*){};
void FASTCALL sh2_LDC_SR(tagSH2*){};
void FASTCALL sh2_MAC_W(tagSH2*){};
void FASTCALL sh2_DT(tagSH2*){};
void FASTCALL sh2_CMP_PZ(tagSH2*){};
void FASTCALL sh2_STS_MACL_DEC(tagSH2*){};
void FASTCALL sh2_CMP_PL(tagSH2*){};
void FASTCALL sh2_LDS_MACL_INC(tagSH2*){};
void FASTCALL sh2_LDC_GBR_INC(tagSH2*){};
void FASTCALL sh2_SHLL8(tagSH2*){};
void FASTCALL sh2_SHLR8(tagSH2*){};
void FASTCALL sh2_LDS_MACL(tagSH2*){};
void FASTCALL sh2_TAS_B(tagSH2*){};
void FASTCALL sh2_LDC_GBR(tagSH2*){};
void FASTCALL sh2_SHAR(tagSH2*){};
void FASTCALL sh2_STS_PR_DEC(tagSH2*){};
void FASTCALL sh2_ROTCL(tagSH2*){};
void FASTCALL sh2_ROTCR(tagSH2*){};
void FASTCALL sh2_LDS_PR_INC(tagSH2*){};
void FASTCALL sh2_LDC_VBR_INC(tagSH2*){};
void FASTCALL sh2_SHLL16(tagSH2*){};
void FASTCALL sh2_SHLR16(tagSH2*){};
void FASTCALL sh2_LDS_PR(tagSH2*){};
void FASTCALL sh2_JMP(tagSH2*){};
void FASTCALL sh2_LDC_VBR(tagSH2*){};
void FASTCALL sh2_MOVL_MEM_DISP(tagSH2*){};
void FASTCALL sh2_MOVB_MEM(tagSH2*){};
void FASTCALL sh2_MOVW_MEM(tagSH2*){};
void FASTCALL sh2_MOVL_MEM(tagSH2*){};
void FASTCALL sh2_MOVR(tagSH2*){};
void FASTCALL sh2_MOVB_INC(tagSH2*){};
void FASTCALL sh2_MOVW_INC(tagSH2*){};
void FASTCALL sh2_MOVL_INC(tagSH2*){};
void FASTCALL sh2_NOT(tagSH2*){};
void FASTCALL sh2_SWAP_B(tagSH2*){};
void FASTCALL sh2_SWAP_W(tagSH2*){};
void FASTCALL sh2_NEGC(tagSH2*){};
void FASTCALL sh2_NEG(tagSH2*){};
void FASTCALL sh2_EXTU_B(tagSH2*){};
void FASTCALL sh2_EXTU_W(tagSH2*){};
void FASTCALL sh2_EXTS_B(tagSH2*){};
void FASTCALL sh2_EXTS_W(tagSH2*){};
void FASTCALL sh2_ADDI(tagSH2*){};
void FASTCALL sh2_MOVB_R0_DISP(tagSH2*){};
void FASTCALL sh2_MOVW_R0_DISP(tagSH2*){};
void FASTCALL sh2_MOVB_DISP_R0(tagSH2*){};
void FASTCALL sh2_MOVW_DISP_R0(tagSH2*){};
void FASTCALL sh2_CMP_EQ_IMM(tagSH2*){};
void FASTCALL sh2_BT(tagSH2*){};
void FASTCALL sh2_BF(tagSH2*){};
void FASTCALL sh2_BT_S(tagSH2*){};
void FASTCALL sh2_BF_S(tagSH2*){};
void FASTCALL sh2_MOV_DISP_W(tagSH2*){};
void FASTCALL sh2_BRA(tagSH2*){};
void FASTCALL sh2_BSR(tagSH2*){};
void FASTCALL sh2_MOVB_R0_GBR(tagSH2*){};
void FASTCALL sh2_MOVW_R0_GBR(tagSH2*){};
void FASTCALL sh2_MOVL_R0_GBR(tagSH2*){};
void FASTCALL sh2_TRAPA(tagSH2*){};
void FASTCALL sh2_MOVB_GBR_R0(tagSH2*){};
void FASTCALL sh2_MOVW_GBR_R0(tagSH2*){};
void FASTCALL sh2_MOVL_GBR_R0(tagSH2*){};
void FASTCALL sh2_MOVA(tagSH2*){};
void FASTCALL sh2_TST_R0(tagSH2*){};
void FASTCALL sh2_ANDI(tagSH2*){};
void FASTCALL sh2_XORI(tagSH2*){};
void FASTCALL sh2_ORI(tagSH2*){};
void FASTCALL sh2_TST_B(tagSH2*){};
void FASTCALL sh2_AND_B(tagSH2*){};
void FASTCALL sh2_XOR_B(tagSH2*){}; 
void FASTCALL sh2_OR_B(tagSH2*){};
void FASTCALL sh2_MOV_DISP_L(tagSH2*){};
void FASTCALL sh2_MOVI(tagSH2*){};
void FASTCALL MOVLL4(tagSH2*){};

#endif
