/*
 * T1-01 스캐폴딩 확인용 더미 테스트.
 * 빌드 · ctest · ASan 경로가 살아 있는지만 본다.
 * T1-02에서 test_types.c가 들어오면 지운다.
 */
#include <assert.h>
#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof(int));
    assert(p != NULL);
    *p = 1;
    assert(*p == 1);
    free(p);
    return 0;
}
