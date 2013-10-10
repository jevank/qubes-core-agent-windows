#pragma once

// fixme: ENAMETOOLONG is defined as 38 in mingw headers, which is different than the value here. Is this significant?
// Also: there is no way to disable macro redefinition warnings selectively in GCC, even in 4.6 and above.
#undef ENAMETOOLONG

#define EIO              5      /* I/O error */
#define EACCES          13      /* Permission denied */
#define EEXIST          17      /* File exists */
#define ENOTDIR         20      /* Not a directory */
#define EINVAL          22      /* Invalid argument */
#define ENAMETOOLONG    36      /* File name too long */
#define EDQUOT          122     /* Quota exceeded */


#define S_IFMT  00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define S_ISLNK(m)      (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)      (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)      (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)      (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)      (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m)     (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m)     (((m) & S_IFMT) == S_IFSOCK)
