#include <stddef.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define LFC_EXPORT __declspec(dllexport)
#else
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <time.h>
#define LFC_EXPORT
#endif

#include "dyn_fontconfig.h"

#define LITE_XL_PLUGIN_ENTRYPOINT
#include "lite_xl_plugin_api.h"

#define LFC_FONT_CACHE "__LFC_FONT_CACHE__"
#define LFC_FONT "__LFC_FONT_TABLE__"
#define LFC_RENDERER "__LFC_RENDERER_TABLE__"

#define LFC_TYPE_FCFONT "FcFont"

typedef struct FcFont
{
    FcPattern *base_pattern;
    FcPattern **patterns;
    FcCharSet **charsets;
    int n, tab_size;
} FcFont;

#define CLEANUP(L, ...)                  \
    {                                    \
        lua_pushfstring(L, __VA_ARGS__); \
        goto cleanup;                    \
    }

static int f_load(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    FcResult result;
    FcFontSet *set = NULL;
    FcPattern *base_pattern = NULL;
    FcPattern **final_patterns = NULL;
    FcCharSet **final_charsets = NULL;

    base_pattern = FcNameParse((FcChar8 *)name);
    if (!base_pattern)
        CLEANUP(L, "%s: cannot lookup font", name);
    if (!FcConfigSubstitute(NULL, base_pattern, FcMatchPattern))
        CLEANUP(L, "%s: cannot perform config substitution", name);
    FcDefaultSubstitute(base_pattern);

    if (lua_isnumber(L, 2))
    {
        // if a number is specified, override the pixel size in pattern string
        FcPatternDel(base_pattern, FC_PIXEL_SIZE);
        FcPatternAddDouble(base_pattern, FC_PIXEL_SIZE, lua_tonumber(L, 2));
    }

    set = FcFontSort(NULL, base_pattern, 1, NULL, &result);
    if (result != FcResultMatch)
        CLEANUP(L, "%s: cannot match font", name);

    final_patterns = calloc(sizeof(FcPattern *), set->nfont);
    final_charsets = calloc(sizeof(FcCharSet *), set->nfont);
    if (!final_patterns || !final_charsets)
        CLEANUP(L, "%s: cannot allocate memory", name);

    for (int i = 0; i < set->nfont; i++)
    {
        final_patterns[i] = FcFontRenderPrepare(NULL, base_pattern, set->fonts[i]);
        if (!final_patterns[i])
            CLEANUP(L, "%s: cannot create final pattern", name);
        FcCharSet *s;
        if (FcPatternGetCharSet(base_pattern, FC_CHARSET, 0, &s) != FcResultMatch && FcPatternGetCharSet(final_patterns[i], FC_CHARSET, 0, &s) != FcResultMatch)
            CLEANUP(L, "%s: cannot get charset", name);

        final_charsets[i] = FcCharSetCopy(s);
    }

    FcFont *font = lua_newuserdata(L, sizeof(FcFont));
    luaL_setmetatable(L, LFC_TYPE_FCFONT);
    font->base_pattern = base_pattern;
    font->charsets = final_charsets;
    font->patterns = final_patterns;
    font->n = set->nfont;
    font->tab_size = -1;
    FcFontSetDestroy(set);
    return 1;
cleanup:
    if (final_patterns)
    {
        for (FcPattern **pp = final_patterns; *pp; pp++)
            FcPatternDestroy(*pp);
        free(final_patterns);
    }
    if (final_charsets)
    {
        for (FcCharSet **ss = final_charsets; *ss; ss++)
            FcCharSetDestroy(*ss);
        free(final_charsets);
    }
    if (set)
        FcFontSetDestroy(set);
    if (base_pattern)
        FcPatternDestroy(base_pattern);
    return lua_error(L);
}

static inline const char *utf8_to_codepoint(const char *p, unsigned *dst)
{
    const unsigned char *up = (unsigned char *)p;
    unsigned res, n;
    switch (*p & 0xf0)
    {
    case 0xf0:
        res = *up & 0x07;
        n = 3;
        break;
    case 0xe0:
        res = *up & 0x0f;
        n = 2;
        break;
    case 0xd0:
    case 0xc0:
        res = *up & 0x1f;
        n = 1;
        break;
    default:
        res = *up;
        n = 0;
        break;
    }
    while (n--)
    {
        res = (res << 6) | (*(++up) & 0x3f);
    }
    *dst = res;
    return (const char *)up + 1;
}

static int get_function(lua_State *L, const char *table, const char *function)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, table) != LUA_TTABLE)
    { // -> [nil]
        lua_pop(L, 1);
        return -1;
    } // -> [table]
    if (lua_getfield(L, -1, function) != LUA_TFUNCTION)
    { // -> [table, nil]
        lua_pop(L, 2);
        return -1;
    } // -> [table, function]
    lua_remove(L, lua_gettop(L) - 1); // -> [function]
    return 0;
}

static double get_time()
{
#ifdef _WIN32
    return (double) GetTickCount() / 1000;
#else
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return spec.tv_sec + spec.tv_nsec / 1.0e9;
#endif
}

static int get_font_cache(lua_State *L, FcPattern *pattern)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, LFC_FONT_CACHE) != LUA_TTABLE)
    {                                                       // -> [nil]
        lua_pop(L, 1);                                      // -> []
        lua_newtable(L);                                    // -> [cache]
        lua_pushinteger(L, 0);                              // -> [cache, 0]
        lua_rawseti(L, -2, 1);                              // -> [cache]
        lua_pushvalue(L, -1);                               // -> [cache, cache]
        lua_setfield(L, LUA_REGISTRYINDEX, LFC_FONT_CACHE); // -> [cache]
    } // -> [cache]
    double pixel_size;
    const char *filename;
    if (FcPatternGetString(pattern, FC_FILE, 0, (unsigned char **)&filename) != FcResultMatch)
    {
        lua_pop(L, 1);
        return -1;
    }
    if (FcPatternGetDouble(pattern, FC_PIXEL_SIZE, 0, &pixel_size) != FcResultMatch)
    {
        lua_pop(L, 1);
        return -1;
    }
    if (lua_rawgeti(L, -1, FcPatternHash(pattern)) == LUA_TNIL)
    {                                               // -> [cache, nil]
        lua_pop(L, 1);                              // -> [cache]
        if (get_function(L, LFC_FONT, "load") != 0) // -> [cache]
        {
            return luaL_error(L, "cannot get font.load()");
        } // -> [cache, font.load]
        // TODO: figure out how to pass extra stuff
        lua_pushstring(L, filename);   // -> [cache, font.load, filename]
        lua_pushnumber(L, pixel_size); // -> [cache, font.load, filename, size]
        if (lua_pcall(L, 2, 1, 0) != LUA_OK)
        { // -> [cache, error]
            lua_pop(L, 2);
            return -1;
        } // -> [cache, font]
        lua_createtable(L, 2, 0);                     // -> [cache, font, fonttable]
        lua_rotate(L, lua_gettop(L) - 1, -1);         // -> [cache, fon  ttable, font]
        lua_rawseti(L, -2, 1);                        // -> [cache, fonttable]
        lua_pushinteger(L, FcPatternHash(pattern));   // -> [cache, fonttable, hash]
        lua_pushvalue(L, -2);                         // -> [cache, fonttable, hash, fonttable]
        lua_rawset(L, -4);                            // -> [cache, fonttable]
        lua_rawgeti(L, -2, 1);                        // -> [cache, fonttable, len]
        lua_pushinteger(L, lua_tointeger(L, -1) + 1); // -> [cache, fonttable, len, len+1]
        lua_rawseti(L, -4, 1);                        // -> [cache, fonttable, len]
        lua_pop(L, 1);                                // -> [cache, fonttable]
    } // -> [cache, fonttable]
    lua_remove(L, lua_gettop(L) - 1); // -> [fonttable]
    lua_pushnumber(L, get_time());    // -> [fonttable, ttl]
    lua_rawseti(L, -2, 2);            // -> [fonttable]
    lua_rawgeti(L, -1, 1);            // -> [fonttable, font]
    lua_remove(L, lua_gettop(L) - 1); // -> [font]
    return 0;
}

static double get_width(lua_State *L, FcFont *font, int i, const char *str, size_t len)
{
    if (get_function(L, LFC_FONT, "get_width") != 0)
    {
        return luaL_error(L, "cannot get font.get_width()");
    } // -> [get_width]
    unsigned char *name;
    if (FcPatternGetString(font->patterns[i], FC_FILE, 0, &name) == FcResultTypeMismatch)
    {
        lua_pop(L, 1);
        return 0;
    }
    if (get_font_cache(L, font->patterns[i]) != 0)
    {
        lua_pop(L, 1);
        return 0;
    } // -> [get_width, font]
    lua_pushlstring(L, str, len); // -> [get_width, font, string]
    lua_call(L, 2, 1);            // -> [width]
    double width = lua_tonumber(L, -1);
    lua_pop(L, 1); // -> []
    return width;
}

static int f_get_width(lua_State *L)
{
    size_t len = 0;
    FcFont *fc = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    const char *text = luaL_checklstring(L, 2, &len);
    const char *textp = text, *last_segment = text;

    int current_font = 0;
    int first = 1;

    double width = 0;
    unsigned int codepoint;
    while (textp < (text + len))
    {
        const char *prev_textp = textp;
        textp = utf8_to_codepoint(textp, &codepoint);
        if (!FcCharSetHasChar(fc->charsets[current_font], codepoint))
        {
            // select a new font
            int new_font = 0;
            for (int i = 0; i < fc->n; i++)
            {
                if (FcCharSetHasChar(fc->charsets[i], codepoint))
                {
                    new_font = i;
                    break;
                }
            }
            if (!first)
            {
                // process previous font
                width += get_width(L, fc, current_font, last_segment, textp - last_segment);
            }
            first = 0;
            last_segment = prev_textp;
            current_font = new_font;
        }
        else
        {
            continue;
        }
    }
    if (last_segment <= textp)
        width += get_width(L, fc, current_font, last_segment, textp - last_segment);
    lua_pushnumber(L, width);
    return 1;
}

static double draw_text(lua_State *L, FcFont *font, int i, const char *str, size_t len, double x, double y, int color)
{
    if (get_function(L, LFC_RENDERER, "draw_text") != 0)
    {
        return luaL_error(L, "cannot get renderer.draw_text()");
    } // -> [draw_text]
    unsigned char *name;
    if (FcPatternGetString(font->patterns[i], FC_FILE, 0, &name) == FcResultTypeMismatch)
    {
        lua_pop(L, 1);
        return 0;
    }
    if (get_font_cache(L, font->patterns[i]) != 0)
    {
        lua_pop(L, 1);
        return 0;
    } // -> [draw_text]
    lua_pushlstring(L, str, len); // -> [draw_text, text]
    lua_pushnumber(L, x);         // -> [draw_text, text, x]
    lua_pushnumber(L, y);         // -> [draw_text, text, x, y]
    lua_pushvalue(L, color);      // -> [draw_text, text, x, y, color]
    lua_call(L, 5, 1);            // -> [new_x]
    double new_x = lua_tonumber(L, -1);
    lua_pop(L, 1); // -> []
    return new_x;
}

static int f_draw_text(lua_State *L)
{
    size_t len = 0;

    // check if the input is a regular Font, if yes then pass on
    if (!luaL_testudata(L, 1, LFC_TYPE_FCFONT))
    {
        if (get_function(L, LFC_RENDERER, "draw_text") != 0)
            return luaL_error(L, "cannot get renderer.draw_text()");
        lua_insert(L, 1); // -> [function, ...args]
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }

    FcFont *fc = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    const char *text = luaL_checklstring(L, 2, &len);
    double x = luaL_checknumber(L, 3);
    double y = luaL_checknumber(L, 4);
    luaL_checktype(L, 5, LUA_TTABLE);

    const char *textp = text, *last_segment = text;
    int current_font = 0;
    int first = 1;

    unsigned int codepoint;
    while (textp < (text + len))
    {
        const char *prev_textp = textp;
        textp = utf8_to_codepoint(textp, &codepoint);
        if (!FcCharSetHasChar(fc->charsets[current_font], codepoint))
        {
            // select a new font
            int new_font = 0;
            for (int i = 0; i < fc->n; i++)
            {
                int len;
                const char *name;
                // skip TTC files because lite-xl don't support them
                if (FcCharSetHasChar(fc->charsets[i], codepoint) &&
                    FcPatternGetString(fc->patterns[i], FC_FILE, 0, (unsigned char **) &name) == FcResultMatch &&
                    (len = strlen(name), strcmp(name + len - 4, ".ttc") != 0))
                {
                    new_font = i;
                    break;
                }
            }
            if (!first)
            {
                // process previous font
                x = draw_text(L, fc, current_font, last_segment, textp - last_segment, x, y, 5);
            }
            first = 0;
            last_segment = prev_textp;
            current_font = new_font;
        }
        else
        {
            continue;
        }
    }
    x = draw_text(L, fc, current_font, last_segment, textp - last_segment, x, y, 5);
    lua_pushnumber(L, x);
    return 1;
}

static int f_copy(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    luaL_checknumber(L, 2);
    // push the base pattern serialized into a string, and call f_load
    FcChar8 *pattern_str = FcNameUnparse(font->patterns[0]);
    if (!pattern_str)
        return luaL_error(L, "cannot unparse base pattern");
    lua_pushstring(L, (const char *) pattern_str); // -> [font, size, ..., pattern]
    free(pattern_str);
    lua_replace(L, 1); // -> [pattern, size, ...]
    return f_load(L);
}

static int f_set_tab_size(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    font->tab_size = luaL_checkinteger(L, 2);
    return 0;
}

static int f_get_height(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    if (get_function(L, LFC_FONT, "get_height") != 0)
        return luaL_error(L, "cannot get renderer.font.get_height()");
    // push the font
    get_font_cache(L, font->patterns[0]);
    lua_call(L, 1, 1);
    return 1;
}

static int f_get_size(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    double size;
    if (FcPatternGetDouble(font->patterns[0], FC_PIXEL_SIZE, 0, &size) != FcResultMatch)
        return luaL_error(L, "cannot get font size");
    lua_pushnumber(L, size);
    return 1;
}

static int f_set_size(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    double size = luaL_checknumber(L, 2);
    FcPatternDel(font->base_pattern, FC_PIXEL_SIZE);
    FcPatternAddDouble(font->base_pattern, FC_PIXEL_SIZE, size);
    for (int i = 0; i < font->n; i++)
    {
        FcPatternDel(font->patterns[i], FC_PIXEL_SIZE);
        FcPatternAddDouble(font->patterns[i], FC_PIXEL_SIZE, size);
    }
    return 0;
}

static int f_get_path(lua_State *L)
{
    FcFont *font = luaL_checkudata(L, 1, LFC_TYPE_FCFONT);
    FcChar8 *pattern_str = FcNameUnparse(font->base_pattern);
    lua_pushstring(L, (const char *) pattern_str);
    free(pattern_str);
    return 1;
}

static int f_clean_font_cache(lua_State *L)
{
    // TODO: fix this
    int younger_than = luaL_checkinteger(L, 1);
    int n = luaL_optinteger(L, 2, 1000);
    if (n > 1000)
        n = 1000;
    if (n < 0)
        n = 0;

    if (lua_getfield(L, LUA_REGISTRYINDEX, LFC_FONT_CACHE) != LUA_TTABLE)
        return 0;
    int font_cache = lua_gettop(L);
    // iterate the cache (do not modify mid iteration)
    int len = 0;
    FcChar32 ids[1000];
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        int age = lua_tointeger(L, -1);
        if (age < younger_than)
        {
            ids[len++] = lua_tointeger(L, -2);
            if (len >= n)
                break;
        }
        lua_pop(L, 1);
    }
    // iterate over drop ids and drop the entries
    for (int i = 0; i < len; i++)
    {
        lua_pushnil(L);
        lua_rawseti(L, font_cache, ids[i]);
    }
    lua_rawgeti(L, font_cache, 1);
    lua_pushinteger(L, lua_tointeger(L, -1) - len);
    lua_rawseti(L, font_cache, 1);
    return 0;
}

static int f_get_cache_metrics(lua_State *L)
{
    if (lua_getfield(L, LUA_REGISTRYINDEX, LFC_FONT_CACHE) != LUA_TTABLE)
    {
        return 0;
    } // -> [cache]
    lua_newtable(L); // -> [cache, tbl]
    lua_pushnil(L);  // -> [cache, tbl, nil]
    while (lua_next(L, -3) != 0)
    {                         // -> [cache, tbl, key, value]
        lua_pushvalue(L, -2); // -> [cache, tbl, key, value, key]
        lua_pushvalue(L, -2); // -> [cache, tbl, key, value, key, value]
        lua_rawset(L, -5);    // -> [cache, tbl, key, value]
        lua_pop(L, 1);        // -> [cache, tbl, key]
    } // -> [cache, tbl]
    return 1;
}

static int f_setup(lua_State *L)
{
#ifdef FONTCONFIG_DYNAMIC
    const char *const msg = load_fontconfig();
    if (msg)
        return luaL_error(L, "%s", msg);
#endif
    luaL_checktype(L, 1, LUA_TTABLE); // renderer metatable
    luaL_checktype(L, 2, LUA_TTABLE); // font metatable
    lua_settop(L, 2);
    lua_setfield(L, LUA_REGISTRYINDEX, LFC_FONT);
    lua_setfield(L, LUA_REGISTRYINDEX, LFC_RENDERER);
    return 0;
}

static luaL_Reg fc_meta[] = {
    {"get_width", f_get_width},
    {"get_height", f_get_height},
    {"get_size", f_get_size},
    {"set_size", f_set_size},
    {"get_path", f_get_path},
    {"copy", f_copy},
    {"set_tab_size", f_set_tab_size},
    {NULL, NULL},
};

static luaL_Reg lib[] = {
    {"setup", f_setup},
    {"load", f_load},
    {"draw_text", f_draw_text},
    {"clean_font_cache", f_clean_font_cache},
    {"get_cache_metrics", f_get_cache_metrics},
    {NULL, NULL},
};

LFC_EXPORT int luaopen_systemfonts(lua_State *L)
{
    luaL_newmetatable(L, LFC_TYPE_FCFONT);
    luaL_setfuncs(L, fc_meta, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_newlib(L, lib);
    return 1;
}

LFC_EXPORT int luaopen_lite_xl_systemfonts(lua_State *L, void *XL)
{
    lite_xl_plugin_init(XL);
    return luaopen_systemfonts(L);
}