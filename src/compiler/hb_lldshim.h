/* C-callable wrapper around the LLD MinGW linker driver. */
#ifndef HB_LLDSHIM_H_
#define HB_LLDSHIM_H_

#if defined( __cplusplus )
extern "C" {
#endif

/* Link with the LLD MinGW driver.
 * argv mirrors a `ld`-style invocation, argv[0] is the program name,
 * e.g. { "ld.lld", "-o", "hello.exe", "hello.o", "-Lc:/.../lib", "-lhbvm", ... }.
 * Returns 0 on success, non-zero on failure. */
int hb_lld_link_mingw( int argc, const char ** argv );

/* Link a Mach-O executable from the given argv. Returns 0 on success,
 * non-zero on failure. Only meaningful when LLD was built with the
 * macho driver, i.e. on macOS hosts. */
extern int hb_lld_link_macho( int argc, const char ** argv );

/* Link an ELF executable from the given argv. Returns 0 on success,
 * non-zero on failure. Only meaningful when LLD was built with the
 * elf driver, i.e. on Linux/BSD hosts. */
extern int hb_lld_link_elf( int argc, const char ** argv );

#if defined( __cplusplus )
}
#endif

#endif
