# 작업 기록

세션마다 한 줄씩 추가한다. 최신 항목이 위로 온다.

**형식**

```
## YYYY-MM-DD

- [T1-05] 가격 레벨 FIFO 리스트 구현
  - 한 일: 이중 연결 리스트, total_qty 불변조건 유지
  - 막힌 점: (있으면)
  - 다음: T1-06 호가창
```

**기록 원칙**

- 무엇을 했는지보다 **왜 그렇게 했는지**를 남긴다. 코드는 git에 있다
- 막힌 점과 해결 방법을 반드시 남긴다. 면접에서 쓸 소재가 된다
- 중요한 설계 판단은 `decisions/`에 따로 남기고 여기서는 링크만

---

## (여기부터 기록)

## 2026-09-15

- [T1-02] 기본 타입과 에러 코드
  - 한 일: `core/include/types.h`, `core/include/errors.h`, `core/src/errors.c`.
    `test_smoke.c`는 지우고 `test_types.c`로 대체. `core` 정적 라이브러리 타깃 추가.
  - 판단 1 — 에러 코드를 X 매크로 목록(`ERROR_CODE_LIST`) 하나로 정의했다. 열거형과
    `err_str()`의 문자열 테이블을 따로 두면 코드를 추가할 때 한쪽만 고치고 끝나는 일이
    반드시 생긴다. 테스트도 같은 목록을 순회하므로 "모든 에러 코드"라는 완료 조건이
    코드를 추가해도 자동으로 유지된다.
  - 판단 2 — `err_str()`은 정의되지 않은 코드에도 NULL을 반환하지 않는다. 로그 경로에서
    널 검사를 강요하지 않기 위해서다. 대신 테스트가 "알 수 없는 에러"를 반환하면 실패하도록
    해서, 목록에 있는 코드가 default로 새는 경우를 잡는다.
  - 판단 3 — 열거형 이름에 접두사를 붙였다(`SIDE_BUY`, `ORDER_LIMIT`, `STATUS_NEW`).
    TASKS.md에는 `{ BUY, SELL }`로 적혀 있으나 `market_t`가 `MARKET_` 접두사를 쓰고
    CLAUDE.md도 모듈 접두사를 요구하므로 그쪽에 맞췄다.
  - 판단 4 — 타입 폭·부호와 경계값 정합성은 `_Static_assert`로 컴파일 타임에 본다.
    런타임 테스트로 확인할 성질이 아니다.
  - 확인: `err_str()`을 일부러 망가뜨린 버전으로 테스트를 돌려 실제로 실패하는지 봤다.
    통과만 확인하면 테스트가 아무것도 안 잡는 경우를 놓친다.
  - 결과: Debug ctest 1/1, ASan ctest 1/1 통과. 경고 0.
  - 다음: T1-03 호가 단위 테이블.

- [T1-01] 프로젝트 스캐폴딩
  - 한 일: 루트 CMakeLists(공통 컴파일 옵션 + `mini_sor_add_test` 함수), `core/tests/test_smoke.c`, `.gitignore`.
    테스트 등록을 함수로 뺀 이유는 모듈이 늘어날 때마다 `add_executable`/`add_test` 세 줄을
    반복하지 않기 위해서다. 테스트 CMakeLists는 한 줄씩만 늘어난다.
  - 막힌 점: 툴체인이 없었다. MSYS2 UCRT64에 gcc는 있지만 cmake/ninja/make가 없고,
    무엇보다 **mingw-w64 gcc에는 libasan/libubsan이 없다**. CLAUDE.md는 커밋 전 ASan 통과를
    요구하므로 MSYS2로는 그 규약 자체를 지킬 수 없다. WSL Ubuntu에 build-essential + cmake를
    설치해 해결. ASan이 실제로 링크되는지(`ldd`에 libasan.so.8) 그리고 실제로 걸리는지
    (의도적 배열 범위 밖 접근이 잡히는지)까지 확인했다. 옵션만 켜지고 무동작인 상태를 피하려고.
  - 결과: Debug 빌드 · ctest 1/1 · ASan 빌드 ctest 1/1 통과.
  - 다음: T1-02 기본 타입과 에러 코드. `test_types.c`가 들어오면 `test_smoke.c`는 지운다.
