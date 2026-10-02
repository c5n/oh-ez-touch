# TJpgDec R0.03 by ChaN, vendored unmodified from
# https://elm-chan.org/fsw/tjpgd/ (tjpgd3.zip; the licence is at the top of
# tjpgd/tjpgd.c). Used by ui/image_decode.cpp to decode a doorbell's JPEG
# snapshot as it streams in.
#
# Not the copy inside LVGL: that one is compiled only with LV_USE_TJPGD, which
# brings LVGL's file-system image decoder along, and it is patched for LVGL's
# own use -- pixels in B,G,R order and the 1/8 descaling path removed. Only
# tjpgd/tjpgdcnf.h, the settings, is ours.
idf_component_register(SRCS "tjpgd/tjpgd.c"
                       INCLUDE_DIRS "tjpgd"
                       REQUIRES "")
