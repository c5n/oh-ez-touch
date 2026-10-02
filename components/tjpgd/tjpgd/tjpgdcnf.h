/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/
/* OH-EZ-TOUCH: our own settings, not LVGL's. The panel decodes straight to
 * its RGB565 framebuffer format, and needs the 1/2..1/8 descaling to get a
 * camera snapshot down to something internal RAM can hold. */

#define JD_SZBUF        512
/* Specifies size of stream input buffer */

#define JD_FORMAT       1
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
*/

#define JD_USE_SCALE    1
/* Switches output descaling feature.
/  0: Disable
/  1: Enable
*/

#define JD_TBLCLIP      1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/

#define JD_FASTDECODE   1
/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM)
*/
