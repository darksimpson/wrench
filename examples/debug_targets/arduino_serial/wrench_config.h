// picked up automatically by wrench.h when it sits next to it

#define WRENCH_INCLUDE_DEBUG_CODE

#if defined(__AVR__)
// AVR boards have very little RAM (an ATmega2560 at least, an Uno is too
// small for the debugger): shrink everything the debug server allocates
#define WRENCH_COMPACT
#define WRENCH_DEBUG_MAX_PAYLOAD 128
#define WRENCH_DEBUG_MAX_BREAKPOINTS 8
#define WRENCH_DEBUG_MAX_HANDLES 16
#define WRENCH_DEFAULT_STACK_SIZE 48
#else
#define WRENCH_DEBUG_MAX_PAYLOAD 512
#endif
