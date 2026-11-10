/* FONT.BIN, the code-card check (docs/re/protection.md): a separate program the game reads into the
   overlay segment 2424:0000 and calls far once at start-up (Sys_CodeCardCheck 1000:31f0) with AX = 0;
   AX = 0 back is a pass, FFFFh a failure. Ported routine by routine from the file; addresses are
   FONT:offset (the file's offset, the same in segment 2424), names in tools/ghidra/names-font.tsv.
   Every routine works on the registers in R as the original's do (docs/re/porting.md). */
#pragma once

void Card_Check(void);        /* FONT:0000, the far entry */
void Card_Ask(void);          /* FONT:0190 */
void Card_SetMode(void);      /* FONT:033c */
void Card_SetPalette(void);   /* FONT:0352 */
void Card_SetAttr(void);      /* FONT:036c */
void Card_Fill(void);         /* FONT:0385 */
void Card_SetStart(void);     /* FONT:03ac */
void Card_ClearText(void);    /* FONT:03bc */
void Card_FillBack(void);     /* FONT:03c6 */
void Card_ShowQuestion(void); /* FONT:03d0 */
void Card_Sprite(void);       /* FONT:04d4 */
void Card_SpriteFar(void);    /* FONT:04f7 */
void Card_CopyBox(void);      /* FONT:05b3 */
void Card_DrawMask(void);     /* FONT:05ee */
void Card_BuildMask(void);    /* FONT:0640 */
