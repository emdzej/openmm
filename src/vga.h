/* The original's waits on the VGA's status register 3DAh (machine.c). */
#pragma once
#include <stdint.h>
/* "wait until bit 3 is set": the retrace's start, a frame boundary unless one just passed */
void vga_wait_retrace_start(void);
/* "wait while bit 3 is set": the retrace's end */
void vga_wait_retrace_end(void);
