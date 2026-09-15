#include "errors.h"

#define ERROR_CASE_ENTRY(name, value, text) case (value): return (text);

const char *err_str(int code)
{
    switch (code) {
        ERROR_CODE_LIST(ERROR_CASE_ENTRY)
    default:
        return "알 수 없는 에러";
    }
}
