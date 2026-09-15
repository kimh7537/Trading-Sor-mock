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
