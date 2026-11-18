/* SPDX-License-Identifier: Apache-2.0
 * 固件用 EMBED_FILES 嵌入的资源，在主机上用 .incbin 提供同名符号。
 * Host replacement for EMBED_FILES: same symbols via .incbin. */

.macro embed name, file
    .globl _binary_\name\()_start
    .globl _binary_\name\()_end
    .p2align 4
_binary_\name\()_start:
    .incbin "\file"
_binary_\name\()_end:
    .byte 0
.endm

#ifdef __APPLE__
    .section __DATA,__const
#else
    .section .rodata
#endif

    embed lock_4bpp_bin, ASSET_DIR/lock_4bpp.bin
    embed loading_4bpp_bin, ASSET_DIR/loading_4bpp.bin
    embed builtin_ttf, ASSET_DIR/builtin.ttf
    embed reading_md, ASSET_DIR/reading.md

/* 模拟器自己的资源：真机外壳的 3D 网格。/ Simulator's own asset: the device shell mesh. */
    embed read_pico_mesh, SIM_ASSET_DIR/read_pico.mesh
