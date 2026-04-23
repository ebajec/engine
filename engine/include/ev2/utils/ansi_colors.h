#ifndef ANSI_COLORS_H
#define ANSI_COLORS_H

#define ANSI_PINK(s) "\x1b[93m"#s"\x1b[0m"
#define ANSI_BLUE(s) "\x1b[34m"#s"\x1b[0m"
#define ANSI_CYAN(s) "\x1b[36m"#s"\x1b[0m"
#define ANSI_YELLOW(s) "\x1b[33m"#s"\x1b[0m"
#define ANSI_RED(s) "\x1b[31m"#s"\x1b[0m"

#define COLORIZE_PATH(s) ANSI_PINK(s)

#endif
