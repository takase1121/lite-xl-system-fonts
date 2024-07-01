#ifndef DYN_FONTCONFIG_H
#define DYN_FONTCONFIG_H

#ifdef FONTCONFIG_DYNAMIC

#include <dlfcn.h>

/* ------------- SYMBOLS FROM fontconfig/fontconfig.h -------------*/

#define FC_FILE "file"            /* String */
#define FC_CHARSET "charset"      /* CharSet */
#define FC_PIXEL_SIZE "pixelsize" /* Double */
#define FC_FAMILY "family"        /* string */

typedef int FcBool;
typedef unsigned char FcChar8;
typedef unsigned short FcChar16;
typedef unsigned int FcChar32;
typedef struct _FcCharSet FcCharSet;
typedef struct _FcConfig FcConfig;
typedef struct _FcPatern FcPattern;
typedef struct _FcCharSet FcCharSet;

typedef enum _FcResult
{
    FcResultMatch,
    FcResultNoMatch,
    FcResultTypeMismatch,
    FcResultNoId,
    FcResultOutOfMemory
} FcResult;

typedef struct _FcFontSet
{
    int nfont;
    int sfont;
    FcPattern **fonts;
} FcFontSet;

typedef enum _FcMatchKind
{
    FcMatchPattern,
    FcMatchFont,
    FcMatchScan,
    FcMatchKindEnd,
    FcMatchKindBegin = FcMatchPattern
} FcMatchKind;

#define DEFSYM(R, N, ...)              \
    typedef R (*PFN_##N)(__VA_ARGS__); \
    static PFN_##N N = NULL;

#define LOADSYM(L, N)               \
    if ((N = dlsym(L, #N)) == NULL) \
        return (const char *const)dlerror();

DEFSYM(void, FcInit, void);
DEFSYM(FcPattern *, FcNameParse, FcChar8 *name);
DEFSYM(FcChar8 *, FcNameUnparse, FcPattern *p);
DEFSYM(FcBool, FcConfigSubstitute, FcConfig *config, FcPattern *p, FcMatchKind kind);
DEFSYM(void, FcDefaultSubstitute, FcPattern *p);
DEFSYM(FcFontSet *, FcFontSort, FcConfig *config, FcPattern *p, FcBool trim, FcCharSet **csp, FcResult *r);
DEFSYM(FcResult, FcPatternGetCharSet, FcPattern *p, const char *object, int n, FcCharSet **c);
DEFSYM(FcResult, FcPatternGetDouble, FcPattern *p, const char *object, int n, double *v);
DEFSYM(FcResult, FcPatternGetString, FcPattern *p, const char *object, int n, FcChar8 **s);
DEFSYM(FcResult, FcPatternAddDouble, FcPattern *p, const char *object, double d);
DEFSYM(FcResult, FcPatternDel, FcPattern *p, const char *object);
DEFSYM(FcChar32, FcPatternHash, const FcPattern *p);
DEFSYM(void, FcPatternPrint, const FcPattern *p);
DEFSYM(FcCharSet *, FcCharSetCopy, FcCharSet *src);
DEFSYM(FcBool, FcCharSetHasChar, const FcCharSet *fcs, FcChar32 ucs4);
DEFSYM(FcPattern *, FcFontRenderPrepare, FcConfig *config, FcPattern *p, FcPattern *font);
DEFSYM(void, FcPatternDestroy, FcPattern *p);
DEFSYM(void, FcCharSetDestroy, FcCharSet *c);
DEFSYM(void, FcFontSetDestroy, FcFontSet *s);

/* ------------- SYMBOLS FROM fontconfig/fontconfig.h -------------*/

static const char *const load_fontconfig()
{
    void *lib = NULL;
    static const char *dlnames[] = {
        "libfontconfig.so",
        "libfontconfig.so.1",
    };
    for (int i = 0; i < (sizeof(dlnames) / sizeof(*dlnames)) && lib == NULL; i++)
        lib = dlopen(dlnames[i], RTLD_LAZY | RTLD_GLOBAL);
    if (lib == NULL)
        return (const char *const)dlerror();
    LOADSYM(lib, FcInit);
    LOADSYM(lib, FcNameParse);
    LOADSYM(lib, FcNameUnparse);
    LOADSYM(lib, FcConfigSubstitute);
    LOADSYM(lib, FcDefaultSubstitute);
    LOADSYM(lib, FcFontSort);
    LOADSYM(lib, FcPatternGetCharSet);
    LOADSYM(lib, FcPatternGetDouble);
    LOADSYM(lib, FcPatternGetString);
    LOADSYM(lib, FcPatternDel);
    LOADSYM(lib, FcPatternPrint);
    LOADSYM(lib, FcPatternAddDouble);
    LOADSYM(lib, FcPatternHash);
    LOADSYM(lib, FcFontRenderPrepare);
    LOADSYM(lib, FcCharSetCopy);
    LOADSYM(lib, FcCharSetHasChar);
    LOADSYM(lib, FcPatternDestroy);
    LOADSYM(lib, FcCharSetDestroy);
    LOADSYM(lib, FcFontSetDestroy);
    FcInit();
    return NULL;
}

#else

#include <fontconfig/fontconfig.h>

#endif

#endif