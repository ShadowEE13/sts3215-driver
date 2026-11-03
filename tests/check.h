/*
 * check.h — 極簡測試框架
 *
 * 用法：
 *     #include "check.h"
 *     int main(void) {
 *         CHECK(1 + 1 == 2);
 *         CHECK_EQ(sum, 0xFB);
 *         CHECK_BYTES(out, expected, len);
 *         return CHECK_SUMMARY();
 *     }
 *
 * 設計：失敗不中止，一次跑完所有檢查再回報；main 回傳值給 make test 判斷成敗。
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>
#include <string.h>

static int check_total_;
static int check_failed_;

/* 條件成立就通過 */
#define CHECK(cond)                                                          \
    do {                                                                     \
        check_total_++;                                                      \
        if (!(cond)) {                                                       \
            check_failed_++;                                                 \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

/* 兩個整數相等；失敗時印出實際值（十進位與十六進位） */
#define CHECK_EQ(actual, expected)                                           \
    do {                                                                     \
        long long a_ = (long long)(actual), e_ = (long long)(expected);      \
        check_total_++;                                                      \
        if (a_ != e_) {                                                      \
            check_failed_++;                                                 \
            printf("  FAIL %s:%d  %s == %s\n       got %lld (0x%llX), "     \
                   "expected %lld (0x%llX)\n",                               \
                   __FILE__, __LINE__, #actual, #expected,                   \
                   a_, (unsigned long long)a_, e_, (unsigned long long)e_);  \
        }                                                                    \
    } while (0)

/* 兩段 byte 陣列逐 byte 相等；失敗時兩行對齊印出，差異處標 ^^ */
static inline int check_bytes_(const unsigned char *a, const unsigned char *e,
                               size_t n, const char *file, int line,
                               const char *expr)
{
    check_total_++;
    if (memcmp(a, e, n) == 0) return 1;
    check_failed_++;
    printf("  FAIL %s:%d  %s\n       got     ", file, line, expr);
    for (size_t i = 0; i < n; i++) printf(" %02X", a[i]);
    printf("\n       expected");
    for (size_t i = 0; i < n; i++) printf(" %02X", e[i]);
    printf("\n               ");
    for (size_t i = 0; i < n; i++) printf(a[i] != e[i] ? " ^^" : "   ");
    printf("\n");
    return 0;
}
#define CHECK_BYTES(actual, expected, n) \
    check_bytes_((const unsigned char *)(actual), \
                 (const unsigned char *)(expected), (n), \
                 __FILE__, __LINE__, #actual " vs " #expected)

/* 放在 main 最後：印總結並回傳 0（全過）或 1 */
#define CHECK_SUMMARY()                                                      \
    (printf("  %d/%d passed\n", check_total_ - check_failed_, check_total_), \
     check_failed_ ? 1 : 0)

#endif /* CHECK_H */
