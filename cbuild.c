
typedef enum {
    LINUX,
    WINDOWS,
} System;

typedef enum {
    DEBUG,
    RELEASE,
} Mode;

typedef struct Script  Script;
typedef struct Target  Target;
typedef struct Library Library;
typedef void (*TargetFunc)(Target*, Mode, System);
typedef void (*LibraryFunc)(Library*, Mode, System);

void plugTarget(Script *S, const char *name, const char *file, TargetFunc func);
void defaultTarget(Script *S, const char *name);

void targetDesc(Target *T, const char *desc);
void sourceDir(Target *T, const char *dir);
void compileFlags(Target *T, const char *flags);
void plugLibrary(Target *T, LibraryFunc func, const char *dir);

void includeDir(Library *L, const char *dir);
void libraryDir(Library *L, const char *dir);
void linkFlags(Library *L, const char *flags);

void script(Script *S, System OS);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <dirent.h>

#define COUNT_OF(X) (int) (sizeof(X) / sizeof((X)[0]))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

typedef struct {
    char **items;
    int count;
    int capacity;
} StringList;

typedef struct {
    char *data;
    size_t count;
    size_t capacity;
} String;

struct Library {
    StringList incdirs;
    StringList libdirs;
    String      lflags;
};

#define MAX_TARGETS 32

typedef struct {
    char      *name;
    char      *file;
    TargetFunc func;
} PTarget;

struct Script {
    char *default_target;
    PTarget items[MAX_TARGETS];
    int count;
};

typedef struct {
    char       *dir;
    LibraryFunc func;
} PLibrary;

struct Target {

    char desc[256];
    
    StringList srcdirs;
    String      cflags;

    PLibrary *libs;
    int       lib_count;
    int       lib_capacity;
};

static void
initString(String *str)
{
    str->data = NULL;
    str->count = 0;
    str->capacity = 0;
}

static void
freeString(String *str)
{
    free(str->data);
}

static void
initStringList(StringList *list)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void
freeStringList(StringList *list)
{
    for (int i = 0; i < list->count; i++)
        free(list->items[i]);
    free(list->items);
}

static void 
appendStringToList(StringList *list, const char *str)
{
    if (str == NULL)
        return;

    if (list->count == list->capacity) {
        list->capacity = MAX(8, 2 * list->capacity);
        list->items = realloc(list->items, list->capacity * sizeof(char*));
    }
    list->items[list->count++] = strdup(str);
}

static void
appendString(String *dst, const char *src)
{
    if (src == NULL)
        return;

    size_t len = strlen(src);
    if (len == 0)
        return;
    
    if (dst->count + len + 1 > dst->capacity) {
        dst->capacity = MAX(dst->capacity+len+1, 2*dst->capacity);
        dst->data = realloc(dst->data, dst->capacity);
    }
    memcpy(dst->data + dst->count, src, len);
    dst->count += len;
    dst->data[dst->count] = '\0';
}

void includeDir(Library *L, const char *dir)
{
    appendStringToList(&L->incdirs, dir);
}

void libraryDir(Library *L, const char *dir)
{
    appendStringToList(&L->libdirs, dir);
}

void linkFlags(Library *L, const char *flags)
{
    appendString(&L->lflags, " ");
    appendString(&L->lflags, flags);
}

void targetDesc(Target *T, const char *desc)
{
    size_t max = sizeof(T->desc);
    strncpy(T->desc, desc, max);
    T->desc[max-1] = '\0';
}

void sourceDir(Target *T, const char *dir)
{
    appendStringToList(&T->srcdirs, dir);
}

void compileFlags(Target *T, const char *flags)
{
    appendString(&T->cflags, " ");
    appendString(&T->cflags, flags);
}

void plugLibrary(Target *T, LibraryFunc func, const char *dir)
{
    if (T->lib_count == T->lib_capacity) {
        T->lib_capacity = MAX(8, 2 * T->lib_capacity);
        T->libs = realloc(T->libs, T->lib_capacity * sizeof(Library));
    }
    PLibrary plib;
    plib.dir = strdup(dir);
    plib.func = func;
    T->libs[T->lib_count++] = plib;
}

void plugTarget(Script *S, 
                const char *name, 
                const char *file, 
                TargetFunc  func)
{
    PTarget PT;
    PT.name = strdup(name);
    PT.file = strdup(file);
    PT.func = func;
    S->items[S->count++] = PT;
}

System currentSystem(void)
{
#ifdef _WIN32
    return WINDOWS;
#else
    return LINUX;
#endif
}

void defaultTarget(Script *S, const char *name)
{
    free(S->default_target);
    S->default_target = strdup(name);
}

static Script getScript(System OS)
{
    Script S;
    S.count = 0;
    S.default_target = NULL;
    script(&S, OS);
    return S;
}

static PTarget*
getPTarget(Script *S, const char *name)
{
    for (int i = 0; i < S->count; i++)
        if (!strcmp(S->items[i].name, name))
            return S->items+i;
    return NULL;
}

static bool 
systemNameToID(const char *name, System *id)
{
    static const struct {
        const char *name;
        System        id;
    } ostable[] = {
        {"windows", WINDOWS},
        {"linux",   LINUX},
    };

    for (int i = 0; i < COUNT_OF(ostable); i++)
        if (!strcmp(name, ostable[i].name)) {
            *id = ostable[i].id;
            return true;
        }
    return false;
}

static bool 
modeNameToID(const char *name, Mode *mode)
{
    if (!strcmp(name, "debug"))
        *mode = DEBUG;
    else if (!strcmp(name, "release"))
        *mode = RELEASE;
    else
        return false;
    return true;
}

typedef struct {
    Mode mode;
    System OS;
    const char *target;
    bool verbose;
} Config;

#define FLAG_VERBOSE "--verbose"
#define FLAG_MODE    "--mode"
#define FLAG_OS      "--os"

static bool 
parseConfig(Config *config, 
            int argc, char **argv, 
            char *errmsg, size_t errmax)
{
    config->OS = currentSystem();
    config->mode = DEBUG;
    config->target = NULL;
    config->verbose = false;

    // Look for the verbose flag first of all
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], FLAG_VERBOSE)) {
            config->verbose = true;
            break;
        }

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], FLAG_OS)) {
            if (i+1 < argc) {
                i++;
                if (!systemNameToID(argv[i], &config->OS)) {
                    snprintf(errmsg, errmax, "Unknown system '%s'", argv[i]);
                    return false;
                }
            } else if (config->verbose) {
                fprintf(stderr, "Warning: Missing argument for option '%s'\n", argv[i]);
            }
        } else if (!strcmp(argv[i], FLAG_MODE)) {
            if (i+1 < argc) {
                i++;
                if (!modeNameToID(argv[i], &config->mode)) {
                    fprintf(stderr, "Unexpected mode '%s'. Only 'debug' and 'release' are allowed.\n", argv[i]);
                    return -1;
                }
            } else {
                fprintf(stderr, "Warning: Missing argument for option '%s'\n", argv[i]);
            }
        } else if (!strcmp(argv[i], FLAG_VERBOSE)) {
            // Already handled
        } else {
            if (config->target == NULL)
                config->target = argv[i];
            else {
                if (config->verbose)
                    fprintf(stderr, "Warning: Ignoring option '%s'\n", argv[i]);
            }
        }
    }

    return true;
}

#ifdef _WIN32
#define PATHSEP '\\'
#else
#define PATHSEP '/'
#endif

static bool 
listCFiles(const char *dir, StringList *list)
{
    size_t dlen = strlen(dir);
    if (dlen == 0)
        return false;

    DIR *d = opendir(dir);
    if (d == NULL)
        return false;

    char buffer[1024];
    if (dlen >= sizeof(buffer)) {
        closedir(d);
        return false;
    }
    size_t blen = dlen;
    memcpy(buffer, dir, dlen);
    if (buffer[blen-1] != '\\' && buffer[blen-1] != '/') {
        if (blen+1 >= sizeof(buffer)) {
            closedir(d);
            return false;
        }
        buffer[blen++] = PATHSEP;
    }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        const char *name = ent->d_name;
        size_t      nlen = strlen(name);

        if (nlen+blen >= sizeof(buffer)) {
            closedir(d);
            return false;
        }
        memcpy(buffer + blen, name, nlen);
        buffer[blen+nlen] = '\0';

        if (nlen > 2 && name[nlen-2] == '.' && name[nlen-1] == 'c')
            appendStringToList(list, buffer);
    }
    closedir(d);
    return true;
}

bool targetExists(Script *S, const char *target)
{
    PTarget *PT = getPTarget(S, target);
    return PT != NULL;
}

typedef struct {
    String output;
    StringList files;
    StringList incdirs;
    StringList libdirs;
    StringList srcdirs;
    String cflags;
    String lflags;
} Recipe;

void freeRecipe(Recipe *recipe)
{
    freeString(&recipe->output);
    freeString(&recipe->cflags);
    freeString(&recipe->lflags);
    freeStringList(&recipe->files);
    freeStringList(&recipe->incdirs);
    freeStringList(&recipe->libdirs);
    freeStringList(&recipe->srcdirs);
}

static void
printRecipeInfo(Recipe *recipe)
{
    fprintf(stdout, "Compiler Flags:\n\t%s\n", recipe->cflags.data);
    fprintf(stdout, "Linker Flags:\n\t%s\n", recipe->lflags.data);

    fprintf(stdout, "Include Directories:\n");
    for (int i = 0; i < recipe->incdirs.count; i++)
        fprintf(stdout, "\t%s\n", recipe->incdirs.items[i]);

    fprintf(stdout, "Library Directories:\n");
    for (int i = 0; i < recipe->libdirs.count; i++)
        fprintf(stdout, "\t%s\n", recipe->libdirs.items[i]);

    fprintf(stdout, "Source Directories:\n");
    for (int i = 0; i < recipe->srcdirs.count; i++)
        fprintf(stdout, "\t%s\n", recipe->srcdirs.items[i]);

    fprintf(stdout, "Source Files:\n");
    for (int i = 0; i < recipe->files.count; i++)
        fprintf(stdout, "\t%s\n", recipe->files.items[i]);
}

static void
composeCommand(Recipe *recipe, String *dst)
{
    initString(dst);
    appendString(dst, "gcc -o ");
    appendString(dst, recipe->output.data);

    for (int i = 0; i < recipe->files.count; i++) {
        appendString(dst, " ");
        appendString(dst, recipe->files.items[i]);
    }

    appendString(dst, " ");
    appendString(dst, recipe->cflags.data);
    appendString(dst, " ");
    appendString(dst, recipe->lflags.data);
    
    for (int i = 0; i < recipe->incdirs.count; i++) {
        appendString(dst, " -I");
        appendString(dst, recipe->incdirs.items[i]);
    }

    for (int i = 0; i < recipe->libdirs.count; i++) {
        appendString(dst, " -L");
        appendString(dst, recipe->libdirs.items[i]);
    }
}

static bool 
getRecipe(Script *S, const char *target, Mode mode, 
          System OS, Recipe *recipe)
{
    initString(&recipe->output);
    initString(&recipe->cflags);
    initString(&recipe->lflags);
    initStringList(&recipe->files);
    initStringList(&recipe->incdirs);
    initStringList(&recipe->libdirs);
    initStringList(&recipe->srcdirs);

    PTarget *PT = getPTarget(S, target);
    assert(PT != NULL);

    appendString(&recipe->output, PT->file);    

    Target T;
    initString(&T.cflags);
    initStringList(&T.srcdirs);
    T.libs = NULL;
    T.lib_count = 0;
    T.lib_capacity = 0;

    PT->func(&T, mode, OS);

    for (int i = 0; i < T.srcdirs.count; i++) {
        const char *dir = T.srcdirs.items[i];
        appendStringToList(&recipe->srcdirs, dir);
        listCFiles(dir, &recipe->files);
    }

    appendString(&recipe->cflags, T.cflags.data);

    for (int i = 0; i < T.lib_count; i++) {

        PLibrary PL = T.libs[i];

        Library L;
        initStringList(&L.incdirs);
        initStringList(&L.libdirs);
        initString(&L.lflags);

        PL.func(&L, mode, OS);

        appendString(&recipe->lflags, L.lflags.data);

        for (int j = 0; j < L.incdirs.count; j++) {
            String str;
            initString(&str);
            appendString(&str, PL.dir);
            appendString(&str, L.incdirs.items[j]);
            appendStringToList(&recipe->incdirs, str.data);
            freeString(&str);
        }

        for (int j = 0; j < L.libdirs.count; j++) {
            String str;
            initString(&str);
            appendString(&str, PL.dir);
            appendString(&str, L.libdirs.items[j]);
            appendStringToList(&recipe->libdirs, str.data);
            freeString(&str);
        }

        freeString(&L.lflags);
        freeStringList(&L.libdirs);
        freeStringList(&L.incdirs);
    }
    freeStringList(&T.srcdirs);
    freeString(&T.cflags);
    return true;
}

int main(int argc, char **argv)
{
    char msg[256];
    Config config;
    if (!parseConfig(&config, argc, argv, msg, sizeof(msg)))
        return -1;

    Script S = getScript(config.OS);
    
    const char *target = config.target;
    if (target == NULL)
        target = S.default_target;
    if (target == NULL) {
        fprintf(stderr, "No target specified\n");
        return -1;
    }
    if (!targetExists(&S, target)) {
        fprintf(stderr, "No such target '%s'\n", config.target);
        return -1;
    }

    Recipe recipe;
    if (!getRecipe(&S, target, config.mode, config.OS, &recipe)) {
        fprintf(stderr, "Failed to build target '%s' recipe\n", target);
        return -1;
    }
    
    String cmd;
    composeCommand(&recipe, &cmd);

    if (config.verbose) {
        printRecipeInfo(&recipe);
        fprintf(stdout, "Command:\n\t%s\n", cmd.data);
    }

    FILE *stream = popen(cmd.data, "r");
    if (stream == NULL) {
        freeRecipe(&recipe);
        return -1;
    }

    char buffer[1024];
    while (1) {
        size_t num = fread(buffer, 1, sizeof(buffer), stream);
        if (feof(stream) || ferror(stream))
            break;
        fwrite(buffer, 1, num, stdout);
    }

    pclose(stream);
    freeRecipe(&recipe);
    return 0;
}