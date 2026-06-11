#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void quick_exit(int);
int at_quick_exit(void(*)(void));
#ifdef _WIN32
const char* inet_ntop(int af, const void* src, char* dst, size_t size);
int inet_pton(int af, const char* src, void* dst);
#endif
#ifdef __cplusplus
}
#endif
