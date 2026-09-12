/*
 * user/hello_embed.S — OBSOLETE.
 *
 * The user ELFs (hello.elf, log_server.elf) are no longer embedded into
 * the kernel image with .incbin.  GRUB loads them as multiboot modules
 * (config/grub.cfg) and kernel/init.c reads them through the multiboot
 * info (kernel/multiboot.c).  This file is kept only as a record of the
 * old embedding approach; it is not part of the build anymore.
 */
