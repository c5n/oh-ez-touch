# lodepng, vendored (see lodepng/lodepng.h for its zlib licence). Used by
# openhab_ui.cpp to decode the PNG icons openHAB serves.
idf_component_register(SRCS "lodepng/lodepng.cpp" "lodepng/lodepng_malloc.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES "")

# Only the decoder is used, and the icon buffers are allocated by the caller.
# Dropping the rest saves roughly 40 KB of flash.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS=1
    LODEPNG_NO_COMPILE_DISK=1
    LODEPNG_NO_COMPILE_ENCODER=1
    LODEPNG_NO_COMPILE_ALLOCATORS=1)
