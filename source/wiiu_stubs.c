#ifdef __WIIU__
// Stubs for POSIX functions not provided by WUT/newlib-nano on Wii U.
// These are required by statically linked portlibs (libmpg123 compat.c uses sigaction).

int sigaction(int sig, const void* act, void* oact) {
    (void)sig; (void)act; (void)oact;
    return 0;
}
#endif
