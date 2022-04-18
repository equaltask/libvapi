
#include <ctype.h> // isdigit

#include "vlog_format.h"
#include "YlogTagFormatter.h"
#include "YlogFormat.h"
#include "vlog_vapi.h"


struct vlog_format {
    YlogFormat::UnitList units;
};

vlog_format_t *vlog_format_compile(const char *fmt, int set_prefix)
{
    const char *c, *s, *p, *e;
    char flag_dash_is_set, flag_plus_is_set, flag_space_is_set, flag_zero_is_set;
    YlogFormat::UnitList *units;
    YlogTags::TagFormatter *f;

    if (strlen(fmt) == 0) {
        vapi_warning("Failed to compile log format: empty string");
        return NULL;
    }

    units = new YlogFormat::UnitList(fmt);

    if (units == NULL) {
        vapi_error("Failed to allocate memory for vlog format");
        return NULL;
    }

    for (c = fmt, s = c ; *c != '\0' ; c++) {
        if (!isprint(*c)) {
            vapi_warning("Non-printable char 0x%hhx in log format", *c);
            delete units;
            return NULL;
        }

        if (*c == '%') {
            p = c;
            c++;

            if (*c == '%') {
                units->addConstUnit(s, c);
                s = c+1;
                continue;
            }

            if (strncmp(c, "TIME", 4) == 0) {
                units->addConstUnit(s, p);
                units->addTimeUnit();
                c += 3;
                s = c+1;
                continue;
            }

            if (strncmp(c, "MSG", 3) == 0) {
                units->addConstUnit(s, p);
                units->addMessageUnit();
                c += 2;
                s = c+1;
                continue;
            }

            flag_dash_is_set = 0;
            flag_plus_is_set = 0;
            flag_space_is_set = 0;
            flag_zero_is_set = 0;

            while (1) {
                switch (*c) {
                    case '-':
                        if (flag_dash_is_set == 1) break;
                        flag_dash_is_set = 1;
                        c++;
                        continue;
                    case '+':
                        if (flag_plus_is_set == 1) break;
                        flag_plus_is_set = 1;
                        c++;
                        continue;
                    case ' ':
                        if (flag_space_is_set == 1) break;
                        flag_space_is_set = 1;
                        c++;
                        continue;
                    case '0':
                        if (flag_zero_is_set == 1) break;
                        flag_zero_is_set = 1;
                        c++;
                        continue;
                    default:
                        break;
                }
                break;
            }

            while (isdigit(*c)) {c++;}

            if (*c == '.') {
                c++;
                while (isdigit(*c)) {c++;}
            }

            f = YlogTags::string2TagFormatter(p+1, c, &e);

            if (f != NULL) {
                units->addConstUnit(s, p);
                if (set_prefix)
                    units->addPrefixUnit(f);

                units->addTagUnit(f);
                c = e-1;
                s = c+1;
            }
        }
    }

    units->addConstUnit(s, c);

    return reinterpret_cast<vlog_format_t*>(units);
}

void vlog_format_free(vlog_format_t *fmt)
{
    YlogFormat::UnitList *units = reinterpret_cast<YlogFormat::UnitList*>(fmt);

    delete units;
}

int vlog_format(buffer_t *buf, vlog_format_t *fmt, const char *msg, vlog_tags_t *tags)
{
    YlogFormat::UnitList *units = reinterpret_cast<YlogFormat::UnitList*>(fmt);

    // allow formatting traces without formatter for early traces
    if (units == NULL)
        return bufprintf(buf, "%s", msg);

    return units->print(buf, msg, tags);
}

const char *vlog_format_get_string(vlog_format_t *fmt)
{
    YlogFormat::UnitList *units = reinterpret_cast<YlogFormat::UnitList*>(fmt);

    if (units == NULL)
        return "%MSG";
    else
        return units->getString().c_str();
}

// allow to retrieve the complete size of expanded tags
int vlog_format_get_len(vlog_format_t *fmt, const char *msg, vlog_tags_t *tags)
{
    YlogFormat::UnitList *units = reinterpret_cast<YlogFormat::UnitList*>(fmt);

    // allow formatting traces without formatter for early traces
    if (units == NULL)
        return msg ? strlen(msg) : 0;
    else
        return units->count(msg, tags);
}
