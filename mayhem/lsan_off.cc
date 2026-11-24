// LeakSanitizer off by default (leaks are not the bug class fuzzed here); ASan/UBSan stay on.
// Default visibility: upstream builds with -fvisibility=hidden, which would otherwise make the
// hook a local symbol.
extern "C" __attribute__((visibility("default"))) int __lsan_is_turned_off() { return 1; }
