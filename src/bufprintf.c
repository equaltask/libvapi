#include <string.h>
#include "bufprintf.h"
#include <libvapi/utility.h>

typedef int (*string_formatter_t)(char *, size_t, const char *, va_list);

static int __bufprintf(buffer_t *buf, const char *format, va_list args, string_formatter_t formatter)
{
    unsigned int nc = formatter(buf->data, buf->available, format, args);

    // if output was truncated
    if (nc >= buf->available)
        nc = buf->available - 1; // number of characters actually printed to buf (excluding terminating null byte)

    buf->data += nc;
    buf->available -= nc;
    return nc;
}

int bufprintf(buffer_t *buf, const char *format, ...)
{
    int rc;
    va_list args;
    va_start(args, format);
    rc = __bufprintf(buf, format, args, vsnprintf);
    va_end(args);
    return rc;
}

int bufprintf_asyncsignalsafe(buffer_t *buf, const char *format, ...)
{
    int rc;
    va_list args;
    va_start(args, format);
    rc = __bufprintf(buf, format, args, vsnprintf_asyncsignalsafe);
    va_end(args);
    return rc;
}

#define PTR_TO_LONG(x)		((unsigned long)(x))

#define X_STORE_PRT(c) {\
        if (PTR_TO_LONG(bp) < PTR_TO_LONG(be))\
        {\
            *bp = (c);\
            bp++;\
        }\
        else if (bp == be)\
        {\
            *be=0;\
        }\
        n++;\
}

#define X_INF       0x7ffffff0

static void itoa(char *buf,         /* Large enough result buffer	*/
                 unsigned int num,  /* Number to convert		*/
                 int base,          /* Conversion base (2 to 16)	*/
                 int caps,          /* Capitalize letter digits	*/
                 int prec)          /* Precision (minimum digits)	*/
{
    char		tmp[36], *s, *digits;

    digits = (caps ? "0123456789ABCDEF" : "0123456789abcdef");

    s = &tmp[sizeof(tmp) - 1];

    for (*s = 0; num || s == &tmp[sizeof(tmp) - 1]; num /= base, prec--)
        * --s = digits[num % base];

    while (prec-- > 0)
        *--s = '0';

    do {
        *buf++ = *s;
    } while (*s++ != '\0');
}

static void ftoa(char *buf, double f, int decimals)
{
    int          exp = 0;
    unsigned int int_part;
    double       round;
    int          i;

    if (f < 0.0) {
        *buf++ = '-';
        f = -f;
    }

    for (round = 0.5, i = 0; i < decimals; i++)
        round /= 10.0;

    f += round;
    if (f >= 4294967296.0)
        while (f >= 10.0) {
            f /= 10.0;
            exp++;
        }

    int_part = (unsigned int) f;
    f -= int_part;

    itoa(buf, int_part, 10, 0, 0);
    while (*buf)
        buf++;

    *buf++ = '.';

    for (i = 0; i < decimals; i++) {
        f *= 10.0;
        int_part = (unsigned int) f;
        f -= int_part;
        *buf++ = '0' + int_part;
    }

    if (exp) {
        *buf++ = 'e';
        itoa(buf, exp, 10, 0, 0);
    } else
        *buf = 0;
}


int vsnprintf_asyncsignalsafe(char *buf, size_t size, const char *fmt, va_list ap)
{
    char		c, *bp, *be;
    int  n = 0;

    bp = buf;
    be = &buf[size - 1];

    while ((c = *fmt++) != 0) {
        int 	width = 0, ljust = 0, plus = 0, space = 0;
        int	altform = 0, prec = 0, half = 0, base = 0;
        int	tlong = 0, fillz = 0, plen, pad, prec_given = 0;
        long	num = 0;
        char	tmp[36] = {0}, *p = tmp;

        if (c != '%') {
            X_STORE_PRT(c);
            continue;
        }

        for (c = *fmt++; ; c = *fmt++)
            switch (c) {
            case 'h':
                half = 1;
                break;
            case 'l':
                if ('l' == *fmt) {
                    tlong = 2;
                    fmt++;
                } else {
                    tlong = 1;
                }
                break;
            case '-':
                ljust = 1;
                break;
            case '+':
                plus = 1;
                break;
            case ' ':
                space = 1;
                break;
            case '0':
                fillz = 1;
                break;
            case '#':
                altform = 1;
                break;
            case '*':
                width = -1;
                break;	/* Mark as need-to-fetch */
            case '.':
                if ((c = *fmt++) == '*')
                    prec = -1;			/* Mark as need-to-fetch */
                else {
                    for (prec = 0; c >= '0' && c <= '9'; c = *fmt++)
                        prec = prec * 10 + (c - '0');
                    fmt--;
                }
                prec_given = 1;
                break;
            default:
                if (c >= '1' && c <= '9') {
                    for (width = 0; c >= '0' && c <= '9'; c = *fmt++)
                        width = width * 10 + (c - '0');
                    fmt--;
                } else
                    goto break_for;
                break;
            }
break_for:

        if (width == -1)
            width = va_arg(ap, int);
        if (prec == -1)
            prec = va_arg(ap, int);

        if (c == 0)
            break;

        switch (c) {
        case 'd':
        case 'i':
            if (2 == tlong)
                num = va_arg(ap, long long);
            else if (1 == tlong)
                num = va_arg(ap, long);
            else
                num = va_arg(ap, int);

            if (half)
                num = (int)(short) num;
            /* For zero-fill, the sign must be to the left of the zeroes */
            if (fillz && (num < 0 || plus || space)) {
                X_STORE_PRT(num < 0 ? '-' : space ? ' ' : '+');
                if (width > 0)
                    width--;
                if (num < 0)
                    num = -num;
            }
            if (! fillz) {
                if (num < 0) {
                    *p++ = '-';
                    num = -num;
                } else if (plus)
                    *p++ = '+';
                else if (space)
                    *p++ = ' ';
            }
            base = 10;
            break;
        case 'u':
            if (2 == tlong)
                num = va_arg(ap, long long);
            else if (1 == tlong)
                num = va_arg(ap, long);
            else
                num = va_arg(ap, int);

            if (half)
                num = (int)(unsigned short) num;
            base = 10;
            break;
        case 'p':
            altform = 0;
        /* Fall through */
        case 'x':
        case 'X':
            if (2 == tlong)
                num = va_arg(ap, long long);
            else if (1 == tlong)
                num = va_arg(ap, long);
            else
                num = va_arg(ap, int);

            if (half)
                num = (int)(unsigned short) num;
            if (altform) {
                prec += 2;
                *p++ = '0';
                *p++ = c;
            }
            base = 16;
            break;
        case 'o':
        case 'O':
            if (2 == tlong)
                num = va_arg(ap, long long);
            else if (1 == tlong)
                num = va_arg(ap, long);
            else
                num = va_arg(ap, int);

            if (half)
                num = (int)(unsigned short) num;
            if (altform) {
                prec++;
                *p++ = '0';
            }
            base = 8;
            break;
        case 'f': {
            double		f;

            f = va_arg(ap, double);
            if (! prec_given)
                prec = 6;
            ftoa(p, f, prec);
            fillz = 0;
            p = tmp;
            prec = X_INF;
        }
        break;
        case 's':
            p = va_arg(ap, char *);
            if (prec == 0)
                prec = X_INF;
            break;
        case 'c':
            p[0] = va_arg(ap, int);
            p[1] = 0;
            prec = 1;
            break;
        case 'n':
            *va_arg(ap, int *) = bp - buf;
            p[0] = 0;
            break;
        case '%':
            p[0] = '%';
            p[1] = 0;
            prec = 1;
            break;
        default:
            X_STORE_PRT(c);
            continue;
        }

        if (base != 0) {
            itoa(p, (unsigned int) num, base, (c == 'X'), prec);
            if (prec)
                fillz = 0;
            p = tmp;
            prec = X_INF;
        }

        if ((plen = strlen(p)) > prec)
            plen = prec;

        if (width < plen)
            width = plen;

        pad = width - plen;

        while (! ljust && pad-- > 0)
            X_STORE_PRT(fillz ? '0' : ' ');
        for (; plen-- > 0 && width-- > 0; p++)
            X_STORE_PRT(*p);
        while (pad-- > 0)
            X_STORE_PRT(' ');
    }

    if (bp < be)
        *bp = 0;
    else
        /*    coverity[var_deref_op : FALSE]    */
        *be = 0;

    return n;
}

int snprintf_asyncsignalsafe(char *buf, size_t size, const char *format, ...)
{
    int rc;
    va_list args;
    va_start(args, format);
    rc = vsnprintf_asyncsignalsafe(buf, size, format, args);
    va_end(args);
    return rc;
}

/*!
 * Output sample:
 *
 * 00000000  61 20 63 68 61 72 20 73 74 72 69 6e 67 20 67 72  |a char string gr|
 * 00000010  65 61 74 65 72 20 74 68 61 6e 20 31 36 20 63 68  |eater than 16 ch|
 * 00000020  61 72 73                                         |ars             |
 */
int hexdump(void *addr, size_t len, char **buffer)
{
    size_t my_str_ite = 0;
    int i = 0;
    int j = 0;
    int line_ite = 0;
    int line_nbr = 0;
    char buf[17] = {'\0'};
    char *line = NULL;
    unsigned char *my_str = (unsigned char *)addr;

    if (addr == NULL || len == 0 || buffer == NULL) {
        return -1;
    }

    if (len % 16 == 0)
        line_nbr = len / 16;
    else
        line_nbr = (len / 16) + 1;


    // TODO allocate multiple of page size
    line = (char *)calloc(line_nbr, LINE_SIZE);
    if (line == NULL) {
        // TODO do ylog error record
        return -1;
    }

    for (j = 0; j < line_nbr; j++) {
        /* copy the line address at iterator position */
        line_ite += sprintf(&line[line_ite], "%08x  ", j);

        /* copy each caracter in hexadecimal at iterator position */
        for (i = 0; i < 16; i++) {
            if (i + my_str_ite < len) {
                line_ite += sprintf(&line[line_ite], "%x ", my_str[i + my_str_ite]);
                buf[i] = my_str[i + my_str_ite];
            } else if (j != 0) {
                line_ite += sprintf(&line[line_ite], "   ");
                buf[i] = ' ';
            }
        }

        /* copy the ascii caracters at iterator position */
        line_ite += sprintf(&line[line_ite], " |%s|\n", buf);
        my_str_ite += 16;
    }

    line[line_ite - 1] = '\0';
    *buffer = line;

    return line_nbr * LINE_SIZE;
}

