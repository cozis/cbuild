
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