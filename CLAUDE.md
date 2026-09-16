# mini-sor

복수시장(KRX / NXT) 주문 집행 시스템 시뮬레이터.

## 이 프로젝트가 무엇인가

2025년 3월 넥스트레이드(NXT) 출범으로 국내 주식은 KRX와 NXT 두 시장에서 거래된다.
증권사는 최선집행기준에 따라 주문을 유리한 시장으로 배분(SOR)해야 한다.

이 프로젝트는 그 환경을 축소 재현한다.

1. 규칙이 다른 두 개의 매칭 엔진(KRX형 / NXT형)
2. 그 위에서 주문을 배분하는 SOR 엔진
3. 증권사 원장·FEP 계층
4. 화면에서 양 시장 호가와 SOR 판단을 확인하는 프론트엔드

**최종 산출물은 동작하는 시스템이 아니라 측정 결과다.**
"집행 전략별로 평균 체결 단가가 얼마나 달랐는가"를 근거와 함께 제시하는 것이 목표다.

## 현재 상태 (2026-09-15)

**Phase 1·2·3 완료 (49/49), Phase 4 진행 중 (4/13).** Phase 2의 산출물인 전략 비교 표가 `bench/results/strategies-2026-09-16.md`에 있다. `core/`, `exchange/`(매칭 엔진), `sor/`(통합 호가창·최선집행 평가·집행 전략 4종), `bench/`, `ledger/`(리스너·워커 풀·공유메모리·계좌 원장·주문 검증), `fep/`(epoll 이벤트 루프·전문 조립·송신 큐·세션·시퀀스 복구·주문번호 매핑·미응답 판정·전 구간 통합), `channel/`(Spring Boot 골격·전문 매핑·원장 커넥션 풀·REST API). `web/`은 아직 계획이다.

- 다음 태스크: `docs/TASKS.md`의 T4-05 (WebSocket 실시간 배포)
- **어디서 빌드하는지가 언어마다 다르다.**

  | 대상 | 어디서 | 왜 |
  |---|---|---|
  | C (`core/` `exchange/` `sor/` `ledger/` `fep/` `bench/`) | **WSL Ubuntu** | Windows MSYS2 gcc에 libasan/libubsan이 없어 커밋 전 ASan 게이트를 못 지킨다 |
  | Java (`channel/`) | **Windows** | JDK 17이 여기 있고 ASan 제약이 없다. Maven은 `mvnw`가 받아 온다 |
  | React (`web/`) | **Windows** | node v22 / npm이 여기 있다 |

  C 빌드는 WSL 안에서 실행한다:
  `wsl -d Ubuntu` 후 `cd /mnt/c/Users/hyunwoo/OneDrive/*/Study/mock-sor`
  Java는 `cd channel && ./mvnw.cmd test` (Windows 셸).

## 아키텍처

```
프론트엔드 (React)
      │ REST / WebSocket
채널계 (Java / Spring Boot)
      │ 고정 길이 전문 (TCP)
원장 (C)  ── 계좌·주문 원장, 증거금·한도 검증
      │
SOR 엔진 (C)  ── 통합 호가창, 최선집행 평가, 라우팅
      ├──────────────┬
FEP-KRX (C)    FEP-NXT (C)
      │              │
KRX 시뮬 (C)   NXT 시뮬 (C)   ── 매칭 엔진 2종
```

각 계층은 별도 프로세스. 전문으로만 통신한다.

## 디렉터리 구조

`docs/` 아래만 실재한다. 나머지는 계획이다.

```
mini-sor/
├── CLAUDE.md              ← 이 파일
├── docs/
│   ├── PLAN.md            ← 기획서. 왜 만드는가
│   ├── TASKS.md           ← 작업 목록 (Phase 1~5). 기본 진행 경로
│   ├── TASKS-TRACK-B.md   ← 백테스트 연동 트랙
│   ├── TASKS-TRACK-C.md   ← 운영 신뢰성·이벤트 트랙
│   ├── SPEC.md            ← 시장 규칙 명세
│   ├── PROGRESS.md        ← 작업 기록 (누적)
│   ├── INTERVIEW.md       ← 설계 판단 정리 (직접 작성)
│   └── decisions/         ← 설계 결정 기록 (ADR)
├── core/                  ← C 공통 (타입, 유틸, 자료구조)
│   ├── include/
│   ├── src/
│   └── tests/
├── exchange/              ← 매칭 엔진 + 거래소 시뮬
│   ├── include/
│   ├── src/
│   │   ├── book/          ← 호가창
│   │   ├── match/         ← 매칭 로직
│   │   └── rules/         ← 시장별 규칙 (krx.c, nxt.c)
│   └── tests/
├── sor/                   ← SOR 엔진
├── ledger/                ← 원장
├── fep/                   ← FEP
├── channel/               ← Java 채널계
├── web/                   ← React 프론트엔드
└── bench/                 ← 벤치마크 하네스
```

## 빌드 / 테스트

```bash
# 전체 빌드
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 전체 테스트
ctest --test-dir build --output-on-failure

# 특정 모듈만
ctest --test-dir build -R exchange --output-on-failure

# 메모리 검사 (커밋 전 필수)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# 벤치마크 (Release)
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel
./build-rel/bench/bench_match
```

## 작업 프로토콜

**세션 시작 시**

1. `docs/TASKS.md`를 읽는다. 사용자가 트랙을 지정했다면 해당 트랙 파일을 읽는다
2. `[ ]` 상태인 가장 앞선 태스크를 선택한다 (의존성이 해결된 것 중에서)
3. 해당 태스크의 완료 조건을 확인한다
4. 관련 기존 코드를 읽는다. 추측하지 말고 실제 파일을 확인한다

**트랙 선택**

- 기본: `docs/TASKS.md` (Phase 1 → 2 → 3 → 4 → 5)
- Phase 2 완료 후 Track B / Track C 착수 가능
- 어느 트랙을 진행할지 불분명하면 **임의로 정하지 말고 사용자에게 묻는다**

**작업 중**

- 한 세션에 한 태스크만 한다. 여러 태스크를 묶지 않는다
- 테스트를 먼저 쓴다. 완료 조건이 곧 테스트다
- 완료 조건에 없는 기능을 추가하지 않는다
- 설계 판단이 필요하면 `docs/decisions/`에 짧게 기록한다

**세션 종료 시**

1. 전체 테스트를 돌린다. 실패하면 끝내지 않는다
2. ASan 빌드로 한 번 더 돌린다
3. `docs/TASKS.md`의 해당 항목을 `[x]`로 바꾼다
4. `docs/PROGRESS.md`에 한 줄 추가한다 (형식은 해당 파일 참조)
5. 커밋한다. 메시지는 `[T1-05] 가격 레벨 FIFO 리스트 구현` 형식

**막혔을 때**

- 완료 조건이 모호하면 임의로 해석하지 말고 질문한다
- 기존 설계와 충돌하면 진행하지 말고 질문한다
- 태스크가 한 세션에 끝나기 너무 크면 쪼개자고 제안한다

## C 코딩 규약

**컴파일 옵션**

```
-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion
```

경고를 남기지 않는다. `-Werror`이므로 경고는 곧 빌드 실패다.

**명명**

- 함수·변수: `snake_case`
- 타입: `snake_case_t`
- 상수·매크로: `UPPER_SNAKE`
- 모듈 접두사를 붙인다: `book_insert()`, `match_limit_order()`

**에러 처리**

```c
// 성공 0, 실패 음수 에러코드. 결과는 출력 파라미터로.
int book_insert(order_book_t *book, const order_t *order, exec_result_t *out);
```

- 에러코드는 `core/include/errors.h`에 모아 정의
- 포인터를 반환하는 함수는 실패 시 `NULL`
- 호출자가 반드시 반환값을 확인한다

**메모리**

- 핫 패스에서 `malloc`/`free`를 호출하지 않는다. 사전 할당 풀을 사용한다
- 할당하는 함수는 이름에 `create`/`alloc`, 해제는 `destroy`/`free`를 짝으로 맞춘다
- 소유권이 이동하면 주석으로 명시한다

**자료구조**

- 고정 크기를 우선한다. 동적 확장은 정말 필요할 때만
- 크기 상수는 헤더에 정의하고 매직 넘버를 쓰지 않는다

**검증**

- 내부 불변조건은 `assert`로 검사한다. 적극적으로 쓴다
- 외부 입력은 `assert`가 아니라 에러 반환으로 처리한다

**금지**

- `strcpy`, `sprintf`, `gets` — `strncpy`, `snprintf` 사용
- 전역 가변 상태 — 컨텍스트 구조체를 인자로 넘긴다
- 매칭 엔진 내부에서 `time()`, `clock()` 등 시스템 시각 호출 (아래 결정성 참조)

## 결정성 (중요)

매칭 엔진과 SOR 엔진은 **결정적**이어야 한다.
같은 입력 시퀀스는 항상 같은 출력을 만든다. 이것이 전략 비교의 전제다.

- 시스템 시각을 읽지 않는다. 모든 시각은 입력 이벤트가 들고 오는 논리 시각을 쓴다
- 난수는 시드를 명시적으로 주입받는다. 전역 `rand()` 금지
- 해시 순회 등 순서가 불정한 연산에 의존하지 않는다
- 단일 스레드로 동작한다. 병렬화는 이 제약을 깨지 않는 범위에서만

이 규칙을 어기면 재현 테스트가 깨진다. 의심스러우면 질문한다.

## 테스트 규약

- 파일 위치: 각 모듈의 `tests/` 하위
- 파일명: `test_<대상>.c`
- 프레임워크 없이 `assert` 기반. 외부 의존성을 만들지 않는다
- 각 테스트 파일은 독립 실행 가능한 `main()`을 가진다
- CMake에서 `add_test()`로 등록

테스트는 완료 조건을 그대로 옮긴 것이어야 한다.
"동작할 것 같다"가 아니라 "이 입력에 이 출력이 나온다"를 검증한다.

## 하지 말 것

- **업무상 접한 전문 규격, 소스, 내부 문서를 일절 반영하지 않는다.**
  공개 자료와 자체 설계만 사용한다. 이 프로젝트는 공개 저장소다
- 완료 조건에 없는 기능 추가
- 리팩터링을 태스크에 끼워 넣기 (필요하면 별도 태스크로 제안)
- 성능 최적화를 먼저 하기 (Phase 1은 정확성, 측정 후 개선)
- 여러 태스크를 한 커밋에 묶기

## 도메인 용어

| 용어 | 의미 |
|---|---|
| 호가 | 매수·매도 의사표시. 가격과 수량 |
| 호가창 (order book) | 가격별로 정리된 미체결 주문 목록 |
| 호가 단위 | 가격대별로 정해진 최소 가격 변동폭 |
| 최우선호가 | 매수 중 가장 높은 가격 / 매도 중 가장 낮은 가격 |
| 잔량 | 특정 가격에 남아 있는 미체결 수량 |
| 슬리피지 | 의도한 가격과 실제 체결 가격의 차이 |
| SOR | Smart Order Routing. 유리한 시장으로 주문을 배분 |
| 최선집행의무 | 자본시장법 제68조. 증권사가 최선의 조건으로 주문을 집행할 의무 |
| 논리 주문 | 사용자가 낸 하나의 주문 |
| 물리 주문 | 논리 주문이 시장별로 쪼개져 실제 전송된 주문 |

상세한 시장 규칙은 `docs/SPEC.md` 참조.
