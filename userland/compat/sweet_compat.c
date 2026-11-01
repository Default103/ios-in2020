/* IN2020 iOS 16 compatibility repairs with bounded crash evidence.
 * Only duplicate first-time PosterBoard initialization is guarded. Exceptions
 * remain enabled; assetsd's unknown exception is recorded, never swallowed.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

typedef void (*setup_fn)(id, SEL);
typedef id (*preprocessor_fn)(id);
typedef preprocessor_fn (*set_preprocessor_fn)(preprocessor_fn);
static setup_fn original_setup;
static Ivar switcher_ivar;
static preprocessor_fn original_preprocessor;
static _Thread_local int logging_exception;
static unsigned exceptions_seen, setup_skips;

static void
setup_once(id self, SEL command)
{
    /* The original caller holds the datastore state lock. */
    if (object_getIvar(self, switcher_ivar) != nil) {
        if (__atomic_fetch_add(&setup_skips, 1, __ATOMIC_RELAXED) < 8) {
            fprintf(stderr, "sweet-compat: PosterBoard: existing switcher configuration retained; duplicate first-time setup skipped\n");
        }
        return;
    }
    original_setup(self, command);
}

static id
record_exception(id exception)
{
    if (logging_exception) return exception;
    logging_exception = 1;
    if (original_preprocessor) exception = original_preprocessor(exception);
    if (__atomic_fetch_add(&exceptions_seen, 1, __ATOMIC_RELAXED) < 16) {
        char text[2048] = "(description unavailable)";
        CFStringRef description = CFCopyDescription((CFTypeRef)exception);
        if (description) {
            (void)CFStringGetCString(description, text, sizeof(text), kCFStringEncodingUTF8);
            CFRelease(description);
            text[sizeof(text)-1] = 0;
        }
        fprintf(stderr, "sweet-compat: %s original ObjC throw: %s\n", getprogname(), text);
        void *frames[32];
        int n = backtrace(frames, 32);
        fprintf(stderr, "sweet-compat: throw backtrace");
        for (int i = 0; i < n; i++) fprintf(stderr, " %p", frames[i]);
        fprintf(stderr, "\n");
    }
    logging_exception = 0;
    return exception;
}

__attribute__((constructor)) static void
install_compatibility(void)
{
    const char *name = getprogname();
    char bootargs[1024] = {0}; size_t size = sizeof(bootargs)-1;
    (void)sysctlbyname("kern.bootargs", bootargs, &size, NULL, 0);
    if (strstr(bootargs, "sweetcompat=0")) return;
    if (!name || (strcmp(name,"PosterBoard") && strcmp(name,"assetsd"))) return;
    set_preprocessor_fn set = (set_preprocessor_fn)dlsym(RTLD_DEFAULT, "objc_setExceptionPreprocessor");
    if (set) {
        original_preprocessor = set(record_exception);
        fprintf(stderr, "sweet-compat: %s exception evidence enabled (exceptions are not suppressed)\n", name);
    }
    if (strcmp(name,"PosterBoard")) return;
    Class cls = objc_getClass("PBFPosterExtensionDataStore");
    SEL sel = sel_registerName("_stateLock_setupSwitcherConfigurationForFirstTime");
    Method method = cls ? class_getInstanceMethod(cls, sel) : NULL;
    switcher_ivar = cls ? class_getInstanceVariable(cls,"_stateLock_switcherConfiguration") : NULL;
    const char *types = method ? method_getTypeEncoding(method) : NULL;
    if (!method || !switcher_ivar || !types || types[0]!='v' || method_getNumberOfArguments(method)!=2) {
        fprintf(stderr, "sweet-compat: PosterBoard method/layout mismatch; no setup hook installed\n");
        return;
    }
    original_setup = (setup_fn)method_setImplementation(method, (IMP)setup_once);
    fprintf(stderr, "sweet-compat: PosterBoard first-time setup idempotence guard installed\n");
}
