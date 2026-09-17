# mini-sor 처음부터 끝까지 — 전체 안내서

> 이 문서 하나만 읽고 이 저장소의 **코드, 동작, 기술**을 전부 이해하는 것이 목표다.
> 프로그래밍·운영체제 지식이 많지 않은 사람을 기준으로 썼다. 모르는 말이 나오면
> 맨 끝의 **7장 용어집**을 먼저 본다.
>
> 작성 기준: 2026-09-17, 최종 점검(`[T6-13]`)을 마친 코드. 설명한 동작은 코드와 테스트를 읽고
> 확인했고, 화면 흐름(2장)은 실제 브라우저로 주문을 내어 확인했다.

---

## 0. 이 문서를 읽는 순서

한 번에 다 읽지 않아도 된다. 아래 순서를 권한다.

| 단계 | 읽을 곳 | 얻는 것 | 걸리는 시간(대략) |
|---|---|---|---|
| 1 | 1장 "무엇을 만들었나" | 주식 시장 기초와 이 프로젝트의 질문 | 15분 |
| 2 | 2장 "주문 하나의 여행" | 화면 클릭부터 체결까지 **어느 파일의 어느 함수**를 지나는지 | 30분 |
| 3 | 3장 "먼저 알아야 할 기술" | fork, 소켓, 공유메모리, epoll, fsync 같은 바탕 지식 | 1시간 |
| 4 | 4장 "모듈별 코드 읽기" | 폴더마다 파일을 여는 순서, 자료구조, 핵심 함수, 테스트 | 모듈당 30분~1시간 |
| 5 | 5장 "직접 띄워 보기" | 원장·채널계·화면을 실제로 띄워 주문 | 20분 |
| 6 | 6장 "측정 결과 읽는 법" | bp, p99, TPS, 승/패/무 | 20분 |

4장은 **아래층부터 위층으로** 읽는다: core → exchange → sor → bench/sdk → ledger/fep → channel/web.
위층 코드는 아래층의 타입과 함수를 그대로 쓰기 때문이다.
**실제로 파일을 하나씩 열어 가며 읽을 때는 바로 아래 0.1절 "코드 읽기 로드맵"을 따라간다.**

**코드를 읽을 때의 요령**

- 항상 **헤더(`.h`) → 소스(`.c`) → 테스트(`tests/test_*.c`)** 순서로 연다.
  헤더에는 "무엇을 하는가"와 "왜 그렇게 했는가"가 주석으로 길게 적혀 있다.
  소스는 "어떻게"다. 테스트는 "정말 그렇게 되는가"를 입력과 출력으로 보여 준다.
- 테스트 파일이 가장 좋은 예제다. 예를 들어 원장이 돈을 어떻게 계산하는지는
  `ledger/tests/test_ledger_core.c`의 숫자를 따라가면 가장 빨리 이해된다.
- 이 저장소의 주석은 거의 전부 "왜"를 설명한다. 주석을 건너뛰지 않는다.
- 작업 순서와 그때그때의 판단은 `docs/TASKS.md`(할 일과 완료 조건)와 `docs/PROGRESS.md`
  (한 일, 발견, 판단)에 태스크 번호(`T1-05` 같은)로 남아 있다. 코드 주석의 태스크 번호로 찾아간다.
- 오류를 어떻게 찾고 고쳤는지, 왜 그 방법을 골랐는지는 **문서 2 `docs/ENGINEERING-NOTES.md`** 에 모았다.

---

### 0.1 코드 읽기 로드맵 — 처음 보는 사람이 파일을 여는 순서

이 절은 저장소 **전체**를 한 줄로 이어 놓은 순서다. 4장의 모듈별 "읽는 순서"는 모듈 안의 순서이고,
여기서는 **모듈과 모듈 사이를 어떻게 건너가는지**까지 정한다.

**원칙 세 가지**

1. **작은 부품 → 부품을 조립한 것.** 타입 → 호가창 → 매칭 엔진 → SOR → 원장 → 채널계 → 화면.
   앞 단계에서 본 이름이 뒤 단계에 그대로 나온다.
2. **한 단계 = 헤더 → 소스 → 테스트.** 표의 "파일" 칸에 적힌 순서대로 열고, "확인" 칸의 테스트로 끝낸다.
   소스가 어렵게 느껴지면 헤더 주석과 테스트만 읽고 넘어가도 된다. 테스트의 숫자가 곧 동작 설명이다.
3. **단계 묶음 끝의 체크포인트 질문에 답할 수 있으면 다음으로 간다.** 답이 막히면 "안내서" 칸의 절을 읽는다.

각 단계의 "안내서" 칸은 이 문서의 어디에 자세한 설명이 있는지 가리킨다.
"(선택)"이 붙은 단계는 처음 읽을 때 건너뛰어도 전체 흐름을 이해하는 데 지장이 없다.

#### 가. 코드를 열기 전 (약 1시간)

| 단계 | 읽을 것 | 볼 것 | 안내서 |
|---|---|---|---|
| 0-1 | `README.md` | 무엇을 재려는 프로젝트인지, 결과 표 | 1장 |
| 0-2 | 이 문서 2장 "주문 하나의 여행" | 주문 하나가 지나가는 **파일·함수 이름**을 눈에 익힌다. 아래 단계에서 그 이름들을 하나씩 연다 | 2장 |
| 0-3 | `docs/SPEC.md` 1~4장 | 호가 단위, 가격 제한폭, 매칭 규칙(가격·시간 우선), KRX와 NXT의 거래 시간 차이 | 4.1절 "KRX와 NXT" |
| 0-4 | `CLAUDE.md` "C 코딩 규약", "결정성" | 에러 코드 규약, 금지 사항, 왜 시스템 시각을 안 읽는지 | 3.1절, 3.10절 |

#### 나. 공통 부품 — `core/` 기초 (약 1시간)

| 단계 | 파일 (이 순서로 연다) | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 1 | `core/include/types.h` | `price_t`·`qty_t`·`order_id_t`, `side_t`(매수 0/매도 1), `order_type_t`, `market_t`(KRX 0/NXT 1) | `core/tests/test_types.c` | 4.1절 1 |
| 2 | `core/include/errors.h` → `core/src/errors.c` | 음수 에러 코드 목록(X 매크로). 이후 모든 함수가 이 값을 돌려준다 | 같은 테스트 | 4.1절 1 |
| 3 | `core/include/tick_size.h` → `core/src/tick_size.c` | 가격대별 호가 단위 표. 70,000원대는 100원 | `test_tick_size.c` | 4.1절 2 |
| 4 | `core/include/order.h` → `core/src/order_pool.c` | 주문 구조체, 미리 만들어 둔 주문 풀에서 꺼내고 돌려주기 | `test_order_pool.c`, `test_order_pool_double_free.c` | 4.1절 3 |
| 5 | `core/include/order_index.h` → `core/src/order_index.c` | 주문번호로 주문을 찾는 해시 테이블 | `test_order_index.c` | 4.1절 4 |

> **체크포인트 나.** 함수가 실패하면 무엇을 돌려주고 결과는 어디에 쓰나? 주문마다 `malloc`하지 않는 이유는?

#### 다. 매칭 엔진 — `exchange/` (약 3시간)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 6 | `exchange/include/price_level.h` → `src/book/price_level.c` | 한 가격에 줄 선 주문들(먼저 온 순서) | `test_price_level.c` | 4.1절 "price_level" |
| 7 | `exchange/include/order_book.h` → `src/book/order_book.c` | 가격 칸 배열로 만든 호가창, 최우선호가, `book_snapshot()`(N단 조회) | `test_order_book.c` | 4.1절 "order_book" |
| 8 | `exchange/include/event.h` → `src/event.c` | 접수·체결·취소 이벤트와 싱크(콜백). **나중에 원장이 돈을 옮기는 곳이 이 콜백이다** | `test_event.c` | 4.1절 "event" |
| 9 | `exchange/include/match.h` → `src/match/match_internal.h` → `match_engine.c` → `match_limit.c` | 지정가 주문이 반대편 호가를 먹고 남으면 걸리는 과정. **이 저장소에서 가장 중요한 함수 `match_limit()`** | `test_match_limit.c` | 4.1절 "매칭 엔진" |
| 10 | `match_market.c` → `match_ioc_fok.c` → `match_cancel_modify.c` | 시장가, IOC/FOK, 취소·정정(정정 시 우선순위) | `test_match_market.c`, `test_match_ioc_fok.c`, `test_match_cancel_modify.c` | 같은 절 |
| 11 | `exchange/include/market_rules.h` → `src/rules/market_rules.c` → `krx.c` → `nxt.c` | 두 시장의 차이는 "언제 무엇을 받아 주나"뿐이다. 체결 알고리즘은 같다 | `test_market_rules.c`, `test_krx_rules.c`, `test_nxt_rules.c`, `test_midpoint.c` | 4.1절 "market_rules" |
| 12 | `exchange/include/synthetic.h` → `src/liquidity/synthetic.c` | 시드를 받는 난수로 가짜 참가자 주문 만들기 | `test_synthetic.c` | 4.1절 "synthetic" |
| 13 | (선택) `core/include/kv_config.h` → `core/src/kv_config.c` | `key = value` 설정 읽기. 다음 단계 시나리오 설정에 쓰인다 | `core/tests/test_kv_config.c` | 4.1절 "kv_config" |
| 14 | `exchange/include/divergent.h` → `src/liquidity/divergent.c` | 두 시장에 **다른** 유동성 넣기 — BALANCED / KRX_THIN / NXT_THIN / CROSSED 시나리오 | `test_divergent.c` | 4.1절 "divergent" |
| 15 | `exchange/tests/test_determinism.c` | 같은 시드로 두 번 돌려 이벤트가 바이트까지 같은지 | (이 파일 자체) | 4.1절, 3.10절 |

> **체크포인트 다.** 70,200원 매수 80주가 들어오면 어떤 순서로 누구와 얼마에 체결되나? 체결가는 누구의 가격인가?
> KRX와 NXT 엔진은 코드 어디가 다른가? 전략 비교에 결정성이 왜 꼭 필요한가?

#### 라. 주문 배분 — `sor/` (약 3시간)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 16 | `sor/include/consolidated.h` → `src/consolidated.c` | KRX·NXT 호가창을 가격순으로 합쳐 보는 통합 호가창 | `test_consolidated.c`, `test_consolidated_update.c` | 4.2절 sor/ 3·4 |
| 17 | `sor/include/best_execution.h` → `src/best_execution.c` | 가격·수량·수수료로 시장에 점수를 매기는 최선집행 평가와 가중치 | `test_best_execution.c`, `test_be_weights.c` | 같은 절 |
| 18 | `sor/include/strategy.h` → `src/strategy.c` | 집행 계획(`exec_plan_t`)과 다리(leg), `plan_add_leg()` | — | 같은 절 |
| 19 | `src/strategy_krx_only.c` → `strategy_best_price.c` → `strategy_split.c` → `strategy_sweep.c` | 같은 300주 매수를 네 전략이 어떻게 나누는지 (안내서의 손계산 예시를 옆에 두고 읽는다) | `test_strategy_*.c` 넷 | 같은 절 |
| 20 | `sor/include/routing_log.h` → `src/routing_log.c` | `routing_plan()` — 전략을 부르고 판단 근거를 기록 | `test_routing_log.c` | 같은 절 |
| 21 | `sor/include/order_map.h` → `src/order_map.c` | 논리 주문 ↔ 물리 주문 매핑, 체결 반영 | `test_order_map.c` | 같은 절 |
| 22 | `sor/include/executor.h` → `src/executor.c` | `exec_submit()` — 계획의 다리마다 매칭 엔진에 넣고 보고서를 만든다 | `test_split_state.c`, `test_split_cancel.c` | 같은 절 |
| 23 | `sor/include/execution_quality.h` → `src/execution_quality.c` | 슬리피지(bp), 체결률, 체결 금액에서 직접 계산하는 `eq_avg_diff_bp()` | `test_execution_quality.c`, (선택) `test_recon_live.c` | 같은 절, 3.11절 |

> **체크포인트 라.** 논리 주문과 물리 주문은 무엇이 다른가? BEST_PRICE가 더 싼 시장을 **안** 고를 수 있는 경우는?
> 평균 단가를 원 단위로 반올림한 뒤 빼면 왜 bp가 틀리나?

#### 마. 전문과 내구성 — `core/` 나머지 (약 2시간)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 24 | `core/include/wire.h` → `core/src/wire.c` | 24바이트 헤더, 빅엔디언으로 숫자 쓰고 읽기 | `test_wire.c` | 4.1절 "wire", 3.2절 |
| 25 | `core/include/msg.h` → `core/src/msg.c` | 전문 종별 16개(주문·취소·조회·호가 조회…)와 바디 배치. **채널계 Java가 이 파일과 똑같아야 한다** | `test_msg.c` | 4.1절 "msg" |
| 26 | (선택) `core/include/feed.h` → `src/feed.c` → `exchange/include/feed_source.h` → `exchange/src/feed_source.c` | 외부 전략 엔진용 시세 피드(스냅샷 방식) | `test_feed.c`, `test_feed_source.c` | 4.1절 "feed" |
| 27 | `core/include/journal.h` → `src/journal.c` | 덧붙이기 전용 저널, CRC, 쓰다 만 마지막 레코드 버리기, `fsync` | `test_journal.c` | 4.1절 "journal", 3.9절 |
| 28 | `core/include/snapshot.h` → `src/snapshot.c` | 임시 파일 → fsync → rename 스냅샷과 복구 | `test_snapshot.c` | 같은 절 |
| 29 | `core/include/recon.h` → `src/recon.c` | 독립 재계산으로 대사 | `test_recon.c` | 같은 절 |
| 30 | `core/tests/test_fault_inject.c` | 쓰는 도중 `SIGKILL`로 죽이고 복구 확인. SIGKILL이 전원 차단과 다른 이유 | (이 파일 자체) | 3.9절 |

> **체크포인트 마.** TCP로 63바이트를 보냈는데 20바이트만 읽히면 받는 쪽은 어떻게 하나?
> `fsync`를 빼도 장애 주입 테스트가 통과하는 이유는?

#### 바. 측정 — `bench/` (약 1시간 30분)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 31 | `bench/compare.c` → `bench/compare_strategies.c` | 전략마다 호가창을 새로 만들어 같은 시드로 채우고 비교 | `bench/tests/test_compare.c`, 결과 `bench/results/strategies-2026-09-16.md` | 4.2절 bench/, 6장 |
| 32 | `bench/quality.c` → `bench/quality_report.c` | 시드 30개로 넓혀 승/패/무와 판정 | `bench/tests/test_quality.c`, 결과 `quality-2026-09-16.md` | 같은 절 |
| 33 | `bench/bench_match.c` | 매칭 엔진만의 지연·TPS | 결과 `2026-09-15.md` | 같은 절 |
| 34 | `bench/bench_pipeline.c` | 전문 해석 → 검증 → 저널 → SOR → 매칭 7단계를 나눠 잰다. 저널을 켜면 왜 376 TPS가 되나 | 결과 `pipeline-2026-09-16.md` | 같은 절 |

> **체크포인트 바.** 이 프로젝트의 **최종 결론** 한 문장은? 단계별 p50을 더하면 전 구간 p50이 안 되는 이유는?

#### 사. 원장 — `ledger/` (약 3시간)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 35 | `ledger/include/listener.h` → `src/listener.c` | 소켓 열고 접속 받고 전문 조립해 처리 함수 부르기, 시그널로 멈추기 | `test_listener.c` | 4.3절 ledger/, 3.5·3.6절 |
| 36 | `ledger/include/worker_pool.h` → `src/worker_pool.c` | `fork()`로 워커 여럿, `waitpid`로 거두기. **실제 `ledgerd`는 이것을 쓰지 않는다 — 왜인지가 핵심** | `test_worker_pool.c` | 같은 절, 3.3절 |
| 37 | `ledger/include/shm_segment.h` → `src/shm_segment.c` | 여러 프로세스가 같이 보는 메모리 | `test_shm.c` | 같은 절, 3.4절 |
| 38 | `ledger/include/account.h` → `src/account.c` | 예수금·묶인 금액, `acct_reserve` / `acct_release` / `acct_settle` / `acct_deposit` | `test_account.c` | 같은 절 |
| 39 | `ledger/include/order_validate.h` → `src/order_validate.c` | 계좌·호가 단위·증거금 검증과 묶기 | `test_order_validate.c` | 같은 절 |
| 40 | `ledger/include/ledger_core.h` → `src/ledger_core.c` | **지금까지의 부품이 모이는 곳.** `process_order()` → 검증 → `routing_plan`/`plan_add_leg` → `exec_submit` → 콜백 `on_event()`에서 정산 → 남은 묶음 풀기 | `test_ledger_core.c` (특히 `test_resting_then_maker_fill`의 금액을 손으로 따라가기) | 같은 절, 2.3절 |
| 41 | `ledger/ledgerd.c` | 원장 코어를 만들고 리스너에 연결하는 70줄짜리 `main()` | 5장처럼 직접 띄워 본다 | 5장 |

> **체크포인트 사.** 20주 70,000원 매수가 걸린 뒤 12주 매도에 체결되면 예수금과 묶인 금액은 각각 얼마가 되나?
> 원장을 `fork()` 워커로 늘리면 무엇이 깨지나?

#### 아. 거래소 게이트웨이 — `fep/` (선택, 약 3시간)

화면에서 도는 구성에는 들어가지 않는다(원장이 매칭 엔진을 직접 품는다). **설계 그림의 다중 프로세스 구성과
네트워크 기법을 공부하려면** 읽는다. 처음에는 건너뛰고 자·차 단계를 먼저 읽어도 된다.

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 42 | `fep/include/evloop.h` → `src/evloop.c` | epoll 이벤트 루프, 논블로킹 소켓 | `test_evloop.c` | 4.3절 fep/, 3.7절 |
| 43 | `fep/include/framer.h` → `src/framer.c` | 바이트 흐름에서 전문 하나씩 잘라 내기 | `test_framer.c` | 같은 절 |
| 44 | `fep/include/sendq.h` → `src/sendq.c` | 다 못 보낸 바이트를 모아 두는 송신 큐, 배압 | `test_sendq.c` | 같은 절 |
| 45 | `fep/include/session.h` → `src/session.c` → `seqtrack.h` → `src/seqtrack.c` | 로그인·하트비트·재접속, 시퀀스 갭과 재전송 | `test_session.c`, `test_seqtrack.c` | 같은 절, 3.8절 |
| 46 | `fep/include/ordmap.h` → `src/ordmap.c` | 우리 주문번호 ↔ 거래소 주문번호, 미응답 주문 판정 | `test_ordmap.c` | 같은 절 |
| 47 | `fep/tests/test_integration.c` | 두 프로세스 + 진짜 매칭 엔진으로 주문 → 체결 → 끊김 → 정리 | (이 파일 자체) | 같은 절 |

> **체크포인트 아.** 답이 끊긴 주문을 자동으로 다시 보내면 안 되는 이유는?

#### 자. (선택) 전략 엔진 SDK — `sdk/` (약 30분)

| 단계 | 파일 | 볼 것 | 확인 | 안내서 |
|---|---|---|---|---|
| 48 | `sdk/order_sdk.h` → `sdk/order_sdk.c` | 외부 전략 엔진이 주문 상태를 추적하는 상태 기계(NONE → PENDING → LIVE → DONE) | `sdk/tests/test_order_sdk.c` | 4.2절 sdk/ |

#### 차. 채널계 — `channel/` (Java, 약 2시간)

Spring을 처음 보면 4.4절의 "Spring Boot를 처음 보는 사람을 위한 기초"를 먼저 읽는다.
경로는 모두 `channel/src/main/java/com/minisor/channel/` 아래다.

| 단계 | 파일 | 볼 것 | 확인 (`channel/src/test/java/...`) | 안내서 |
|---|---|---|---|---|
| 49 | `wire/WireEnums.java` → `WireType.java` → `WireField.java` → `WireMessage.java` → `WireHeader.java` → `WireCodec.java` | 어노테이션 선언만 보고 바이트를 만들고 읽는 코덱. 25단계 `msg.h`와 나란히 놓고 본다 | `WireCodecTest`, **`WireLayoutTest`**(C 헤더를 직접 읽어 대조) | 4.4절 4 |
| 50 | `wire/OrderReq.java` → `OrderAck.java` → `BookReq.java` → `BookAck.java` (나머지 전문 클래스는 같은 모양) | 전문 한 종별 = 클래스 하나 | 같은 테스트 | 같은 절 |
| 51 | `ledger/LedgerProperties.java` → `LedgerConnection.java` → `LedgerConnectionPool.java` | 요청 하나에 응답 하나, 깨진 접속은 버리기, 세마포어로 접속 1개 지키기 | `LedgerConnectionPoolTest`, `FakeLedger`(시험용 원장) | 4.4절 5 |
| 52 | `api/OrderRequestDto.java` → `OrderResponseDto.java` → `OrderService.java` → `OrderController.java` | 입력 검증, 상태 코드(200/400/422/503/202), 답을 못 받으면 "모른다"(IN_DOUBT) | `OrderApiTest` | 4.4절 7 |
| 53 | `api/BookController.java` | `GET /api/book` — 원장의 호가 10단 | `OrderApiTest.bookComesFromLedger` | 같은 절 |
| 54 | `stream/StreamEvent.java` → `StreamHub.java` → `StreamHandler.java` → `StreamConfig.java` | WebSocket 방송, 느린 구독자 끊기, 원장 끊김·회복 알림 | `StreamTest`, `ChannelStartupTests` | 4.4절 6 |

> **체크포인트 차.** 응답이 200이 아니라 202인 경우는 언제이고, 그때 다시 보내면 안 되는 이유는?
> `WireLayoutTest`가 잡는 것과 못 잡는 것은?

#### 카. 화면 — `web/` (TypeScript + React, 약 1시간 30분)

React를 처음 보면 4.4절의 "React를 처음 보는 사람을 위한 기초"를 먼저 읽는다.

| 단계 | 파일 | 볼 것 | 안내서 |
|---|---|---|---|
| 55 | `web/vite.config.ts` → `web/src/lib/wire.ts` → `types.ts` → `api.ts` → `format.ts` | 프록시(CORS를 피하는 이유), C와 같은 열거값, REST 호출 | 4.4절 13 |
| 56 | `web/src/lib/useStream.ts` | WebSocket 연결과 끊기면 간격을 늘리며 재접속 | 같은 절 |
| 57 | `web/src/main.tsx` → `web/src/App.tsx` | 탭, 1초마다 호가 읽기, `order`·`fill`·`ledger-down` 이벤트 처리 | 같은 절 |
| 58 | `components/OrderTicket.tsx` → `OrderBook.tsx` → `SorPanel.tsx` → `Working.tsx` → `Fills.tsx` → `Strategies.tsx` → `StatusBar.tsx` → `Ops.tsx` → `Panel.tsx` | 주문 칸, 호가창, SOR 판단, 주문·체결 내역, 전략 비교, 상태 표시 | 같은 절 |

화면에는 자동 테스트가 없다. 확인은 `npm run build`(타입 검사 포함)와 5장처럼 직접 띄워서 한다.

#### 타. 마무리 (약 1시간)

| 단계 | 할 일 | 얻는 것 |
|---|---|---|
| 59 | 5장대로 원장·채널계·화면을 띄우고 5.3절 "해 볼 것"을 따라 한다 | 읽은 코드가 실제로 움직이는 모습 |
| 60 | 2장 "주문 하나의 여행"을 **다시** 읽는다 | 처음에는 이름뿐이던 함수들이 이제 무엇을 하는지 연결된다 |
| 61 | `docs/ENGINEERING-NOTES.md` 3장 | 이 코드들이 왜 지금 모양이 되었는지 — 오류와 판단의 역사 |

#### 시간이 없을 때 — 최소 경로 (약 4시간)

전체 흐름만 잡으려면 아래만 읽는다. 나머지는 필요할 때 로드맵의 해당 단계로 돌아온다.

`README.md` → 이 문서 2장 → 1(`types.h`) → 7(`order_book.h`) → 9(`match_limit.c`와 `test_match_limit.c`)
→ 11(`krx.c`·`nxt.c`) → 19(전략 넷, 손계산 예시) → 22(`executor.c`) → 25(`msg.h`)
→ 40(`ledger_core.c`와 `test_ledger_core.c`) → 52(`OrderService.java`) → 57(`App.tsx`) → 59(직접 띄워 보기)

---

## 1. 무엇을 만들었나

### 1.1 주식 시장 5분 요약

- **호가**: "이 가격에 이만큼 사겠다/팔겠다"는 의사표시다. 예: "70,000원에 10주 매수".
- **호가창(order book)**: 아직 체결되지 않은 호가를 가격별로 모아 둔 표다.
  사겠다는 쪽(매수호가, bid)은 비싼 가격부터, 팔겠다는 쪽(매도호가, ask)은 싼 가격부터 줄을 선다.

  ```
        매도(ask)            ← 파는 사람들. 위로 갈수록 비싸다
        70,300원   9,810주
        70,200원  16,019주
        70,100원  35,679주   ← 최우선 매도호가 (지금 살 수 있는 가장 싼 가격)
   ────────────────────────
        70,000원   9,231주   ← 최우선 매수호가 (지금 팔 수 있는 가장 비싼 가격)
        69,900원  29,329주
        매수(bid)            ← 사는 사람들
  ```

  (이 숫자는 원장 데몬을 기본 설정으로 띄웠을 때 KRX 호가창의 실제 값이다.)

- **체결**: 매수 가격이 매도 가격 이상이 되면 거래가 성사된다. 70,100원 이상에 사겠다는
  주문이 들어오면 70,100원에 줄 선 매도와 체결된다. **체결 가격은 먼저 와서 기다리던 쪽(maker)의 가격**이다.
- **maker / taker**: 호가창에 먼저 걸려 기다리던 주문이 maker, 새로 들어와 그것을 먹는 주문이 taker다.
- **가격-시간 우선**: 더 좋은 가격이 먼저, 같은 가격이면 먼저 온 주문이 먼저 체결된다.
- **호가 단위**: 가격은 아무 숫자나 못 쓴다. 70,000원대는 100원 단위라 70,050원은 거절된다.
- **부분 체결과 잔량**: 100주를 사려는데 70,100원에 30주만 있으면 30주만 체결되고 70주는
  (주문 유형에 따라) 호가창에 걸리거나 취소된다.
- **주문 유형**: 지정가(가격 지정, 남으면 걸림), 시장가(가격 무관 즉시), IOC(즉시 되는 만큼만, 나머지 취소),
  FOK(전량 즉시 아니면 전부 취소), 중간가(두 최우선호가의 중간에서 체결). 4장 exchange 절에서 코드로 본다.

### 1.2 두 시장, 그리고 SOR

2025년 3월 넥스트레이드(NXT)가 열리면서 같은 삼성전자 주식이 **KRX와 NXT 두 곳에서** 거래된다.
두 호가창의 가격과 수량은 다르다. 예를 들어 KRX 최우선 매도가 70,100원이고 NXT가 70,000원이면
NXT에서 사는 편이 100원 싸다. 대신 NXT의 70,000원에는 2,257주밖에 없을 수 있다.

증권사는 법(자본시장법 제68조, **최선집행의무**)에 따라 고객 주문을 더 유리한 조건으로 집행해야 한다.
이 일을 하는 프로그램이 **SOR(Smart Order Routing)** 이다. 사용자가 낸 주문 하나(**논리 주문**)를
시장별로 나눈 실제 주문(**물리 주문**)으로 바꿔 보낸다.

### 1.3 이 프로젝트가 재려는 것

**최종 산출물은 "동작하는 시스템"이 아니라 "측정 결과"다.** 질문은 하나다.

> 집행 전략(KRX만 쓰기 / 가장 유리한 곳 / 나눠 보내기 / 쓸어 담기)에 따라
> 평균 체결 단가와 체결률이 얼마나 달라지는가?

만든 이유는 알고리즘 트레이딩 백테스트가 "신호 가격에 전량 즉시 체결"을 가정하기 때문이다.
실제로는 슬리피지·부분 체결·미체결이 있고, 그 차이를 재려면 현실적인 체결 환경이 필요했다.
답은 `bench/results/`에 있다(6장). 그 숫자를 믿을 수 있게 하려고 매칭 엔진을 **결정적**으로
만들었다 — 같은 입력은 언제나 같은 출력을 낸다(3.10절).

### 1.4 전체 구조

```
┌──────────────────────────────┐
│ 화면 (React, web/)            │  브라우저. 호가창·주문·체결 내역·전략 비교·관제
└──────────────┬───────────────┘
               │ HTTP(REST) + WebSocket   (개발 서버가 /api·/ws를 8080으로 넘긴다)
┌──────────────▼───────────────┐
│ 채널계 (Java Spring, channel/) │  JSON ↔ 고정 길이 전문 변환, 방송, 원장 상태 감시
└──────────────┬───────────────┘
               │ TCP, 고정 길이 이진 전문 (포트 9100, 접속 1개)
┌──────────────▼───────────────┐
│ 원장 데몬 ledgerd (C, ledger/) │  계좌·증거금 검증, 정산
│   ├─ SOR (sor/)               │  어느 시장으로 보낼지
│   └─ 매칭 엔진 ×2 (exchange/) │  KRX형, NXT형 호가창과 체결
└──────────────────────────────┘

그 밖에:  core/  공통 타입·전문 형식·저널·스냅샷·대사
          fep/   거래소와 붙는 게이트웨이 (라이브러리 + 테스트. 두 프로세스 통합 테스트로 검증)
          bench/ 측정 프로그램          sdk/ 외부 전략 엔진용 주문 상태 라이브러리
```

원래 설계는 원장·SOR·FEP·거래소를 **모두 별도 프로세스**로 두는 것이다(`CLAUDE.md`의 그림).
그 조각(FEP의 이벤트 루프, 세션, 재전송)은 만들고 테스트했지만, 화면에서 실제로 띄우는 구성은
위 그림처럼 **SOR과 매칭 엔진을 원장 프로세스 안에** 두었다(T6-03 "최소 연결"). 이유는 2.4절과 3.3절에서 설명한다.

### 1.5 폴더 지도

| 폴더 | 언어 | 한 줄 설명 | 의존하는 것 |
|---|---|---|---|
| `core/` | C | 가격·수량 타입, 에러 코드, 주문 풀, 전문 형식, 저널·스냅샷·대사 | 없음 (맨 아래) |
| `exchange/` | C | 호가창과 매칭 엔진, KRX/NXT 규칙, 가짜 유동성 생성 | core |
| `sor/` | C | 통합 호가창, 최선집행 평가, 전략 4종, 집행기, 주문 매핑 | core, exchange |
| `bench/` | C | 매칭·전 구간 성능 측정, 전략 비교, 품질 리포트 | core, exchange, sor |
| `sdk/` | C | 외부 전략 엔진이 쓰는 주문 상태 추적 라이브러리 | core |
| `ledger/` | C | TCP 리스너, 워커 풀, 공유메모리, 계좌, 주문 검증, 원장 코어, `ledgerd` | core, exchange, sor |
| `fep/` | C | epoll 루프, 전문 조립, 송신 큐, 세션·시퀀스·재전송, 주문번호 매핑·미응답 판정 | core |
| `channel/` | Java | 화면과 원장 사이의 서버 | (TCP로 ledger와 통신) |
| `web/` | TypeScript | 브라우저 화면 | (HTTP·WebSocket으로 channel과 통신) |
| `docs/` | 문서 | 기획(PLAN), 시장 규칙(SPEC), 작업 목록(TASKS), 작업 기록(PROGRESS), 설계 결정(decisions) | |

---

## 2. 주문 하나의 여행

화면에서 **"SOR 자동, 매수, 70,000원, 100주"** 를 누르면 무슨 일이 일어나는지 파일과 함수 이름으로 따라간다.
실제 브라우저로 확인했을 때 NXT 최우선 매도가 70,000원 2,257주였고, 주문 뒤 2,157주가 됐다.

### 2.1 화면 → 채널계

1. `web/src/components/OrderTicket.tsx`의 `send()`가 `submitOrder()`(`web/src/lib/api.ts`)를 부른다.
2. `submitOrder()`는 `POST /api/orders`로 JSON을 보낸다.

   ```json
   {"account":"123456789012","symbol":"005930","clOrdId":123456789,
    "side":0,"type":0,"market":255,"price":70000,"qty":100}
   ```

   숫자의 뜻은 `web/src/lib/wire.ts`에 이름으로 있다: `side 0 = 매수`, `type 0 = 지정가`,
   `market 255 = SOR 자동`. **이 숫자는 C의 `core/include/types.h`와 반드시 같아야 한다** —
   한때 화면은 매수를 1로 보냈고 C는 1을 매도로 읽었다(T6-01, 문서 2 참조).
3. 브라우저 주소는 `localhost:5173`(화면 개발 서버)인데, `web/vite.config.ts`의 프록시가
   `/api`로 시작하는 요청을 `localhost:8080`(채널계)으로 넘긴다.

### 2.2 채널계 안

4. `channel/src/main/java/com/minisor/channel/api/OrderController.java`의 `submit()`이 받는다.
   `@Valid`가 붙어 있어 `OrderRequestDto`의 규칙(계좌 12자리, 수량 1 이상, 시장은 0·1·255)을
   **먼저 검사**한다. 틀리면 원장에 가지 않고 **400**으로 끝난다.
5. `OrderService.submit()` → `send()`가 JSON 값을 전문 객체 `OrderReq`에 옮긴다.
6. `LedgerConnectionPool.borrow()`로 원장 접속을 하나 빌린다. 접속은 1개뿐이라 다른 요청이 쓰는 중이면 최대 2초 줄을 선다.
7. `LedgerConnection.call()`이 `WireCodec.encodeBody()`로 **39바이트 바디**를 만들고
   24바이트 헤더(`WireHeader`)를 앞에 붙여 TCP로 한 번에 쓴다. 그리고 응답 헤더와 바디를 읽는다.

### 2.3 원장(C) 안

8. `ledger/ledgerd.c`의 `main()`은 시작할 때 `ledger_core_create()`로 계좌(예수금 1억 원),
   KRX·NXT 매칭 엔진, 시장당 유동성 1,000건(시드 20260917로 생성)을 만들어 두었다. 그리고
   `listener_run()`(`ledger/src/listener.c`)에서 전문을 기다린다.
9. 전문이 오면 `ledger_core_handle()`(`ledger/src/ledger_core.c`)이 불린다. 종별이
   `MSG_ORDER_REQ`이므로 `msg_decode_order_req()`로 바이트를 구조체로 풀고 `process_order()`를 부른다.
10. `process_order()`의 순서:
    1. **자리 검사** — 받을 수 있는 주문 수(65,536)를 넘었는가.
    2. **검증** — `validate_order()`(`ledger/src/order_validate.c`)가 계좌가 있는지, 호가 단위가 맞는지,
       돈이 충분한지 보고, 매수면 `70,000 × 100 = 7,000,000원`을 **묶는다**(예수금은 그대로, 묶인 금액이 는다).
    3. **배분 계획** — 시장이 255이므로 `routing_plan(&STRATEGY_BEST_PRICE, ...)`(`sor/src/routing_log.c`)이
       통합 호가창을 보고 NXT로 100주를 보내는 계획을 세운다.
       시장을 직접 골랐다면 `plan_add_leg()`로 그 시장 하나짜리 계획을 만든다.
    4. **집행** — `exec_submit()`(`sor/src/executor.c`)이 계획의 다리마다 물리 주문을 만들어
       `match_limit()`(`exchange/src/match/match_limit.c`)에 넣는다. NXT 호가창의 70,000원 매도와 체결된다.
    5. **정산** — 매칭 엔진은 체결마다 **이벤트**를 낸다. 원장은 시작할 때 걸어 둔 콜백
       `on_event()`에서만 돈을 옮긴다. 매수 100주 @70,000이면 묶어 둔 돈 중 7,000,000원을
       예수금에서 빼며 푼다(`acct_settle`). 지정가보다 싸게 체결됐다면 그 차액은 그냥 푼다(`acct_release`).
       예전에 걸어 둔 주문이 나중에 체결되는 경우(maker)도 같은 콜백이 처리한다.
    6. **남은 묶음 정리** — `주문 수량 − 체결 수량 − 아직 호가창에 걸린 수량` 만큼의 증거금을 푼다.
       여기서는 100 − 100 − 0 = 0.
    7. **응답** — 주문번호, 상태(전량 체결 = 2), 체결 수량 100, 평균 체결가 70,000을
       `msg_encode_order_ack()`로 29바이트에 담는다.

### 2.4 돌아오는 길

11. 채널계 `LedgerConnection.call()`이 응답을 `OrderAck`로 풀고, `OrderService`가
    `OrderResponseDto`(`ACCEPTED`, 체결 100주, 평균 70,000원)를 만든다.
12. `OrderService`는 모든 WebSocket 구독자에게 **`order` 이벤트**와 **`fill` 이벤트**를 방송한다
    (`channel/.../stream/StreamHub.java`).
13. 화면은 두 경로로 결과를 받는다. HTTP 응답은 주문 칸 아래에 "100주 체결 · 평균 70,000원 · 전량"을
    띄우고, WebSocket 이벤트는 `web/src/App.tsx`가 받아 "주문·체결" 탭의 목록을 채운다.
    `order` 이벤트를 받으면 호가창을 곧바로 다시 읽어(`GET /api/book`) NXT 잔량이 줄어든 것을 보인다.
    호가창은 그 밖에도 1초마다 다시 읽는다.

**원장이 죽으면.** 1초마다 호가를 읽던 `BookController`가 실패를 알아채고 `StreamHub.ledgerReachable(false)`가
`ledger-down`을 방송한다. 화면 위쪽에 "원장 끊김"이 뜬다. 원장을 다시 띄우면 다음 호가 읽기가 성공하며
`ledger-up`이 가고 표시가 사라진다(T6-10, 실제로 원장을 죽였다 살려 확인했다).

**왜 SOR과 매칭 엔진이 원장 프로세스 안에 있나.** 원장을 여러 프로세스(`fork()`)로 늘리면
프로세스마다 호가창이 따로 생긴다. 같은 시장인데 호가창이 워커 수만큼 생기는 셈이다(3.3절).
그래서 한 프로세스가 전문을 하나씩 차례로 처리한다. 결정성 원칙과도 맞는다. 원장이 접속을 한 번에 하나씩
끝까지 처리하므로 채널계의 접속 풀도 1개다.

---

## 3. 먼저 알아야 할 기술

이 장은 4장의 코드를 읽기 위한 바탕이다. 각 절 끝에 **이 저장소의 어디에서 쓰는지** 적었다.
4장의 모듈별 절에도 같은 기법이 코드와 함께 더 자세히 나온다.

### 3.1 C 언어에서 꼭 필요한 것

- **구조체(`struct`)**: 여러 값을 한 덩어리로 묶는다. `order_t`는 주문번호·방향·가격·수량을 묶은 것이다.
- **포인터(`*`)**: "값"이 아니라 "값이 있는 메모리 주소"다. 큰 구조체를 복사하지 않고 넘기려고 쓴다.
  `const order_t *o`는 "주문을 읽기만 하겠다"는 뜻이다.
- **헤더와 소스**: `.h`에는 "이런 함수가 있다"(선언), `.c`에는 "이렇게 동작한다"(정의)가 있다.
- **에러 처리 규약**: 이 저장소의 함수는 **성공하면 0(`ERR_OK`), 실패하면 음수 에러 코드**를 돌려주고,
  결과는 포인터 인자(출력 파라미터)에 쓴다. 코드 목록은 `core/include/errors.h`(예: -5 호가 단위 위반, -14 증거금 부족).
- **`assert`**: "여기서는 절대 이럴 리 없다"는 **내부 약속**을 검사한다. 어기면 프로그램이 멈춘다.
  바깥에서 온 입력(전문, 사용자 주문)은 `assert`가 아니라 에러 코드로 거절한다. Release 빌드에서는 `assert`가 꺼진다.
- **`malloc`/`free`를 핫 패스에서 안 쓴다**: 주문마다 메모리를 할당하면 느리고 예측이 안 된다.
  그래서 시작할 때 주문 수만큼 미리 만들어 두고 꺼내 쓴다(**메모리 풀**, `core/src/order_pool.c`).
- **금지 함수**: `strcpy`, `sprintf`, `gets`는 버퍼 길이를 모르고 써서 넘칠 수 있다. 길이를 받는 `snprintf`를 쓴다.
- **전역 가변 상태 금지**: 상태는 컨텍스트 구조체(예: `ledger_core_t`)에 담아 인자로 넘긴다. 예외는 시그널 플래그처럼
  달리 방법이 없는 곳뿐이고, 그 자리에 이유가 주석으로 있다.

### 3.2 바이트와 전문 — 빅엔디언, 고정 길이

컴퓨터끼리 숫자를 주고받으려면 바이트 순서를 정해야 한다. 70000은 16진수로 `0x00011170`이다.

- **빅엔디언**: 큰 자리부터 `00 01 11 70` 순서로 보낸다. 사람이 읽는 순서와 같다.
- **리틀엔디언**: 작은 자리부터 `70 11 01 00`. 인텔·AMD CPU가 메모리에 저장하는 순서다.

이 프로젝트의 전문은 **모두 빅엔디언, 필드 길이 고정, 채움 바이트 없음**이다. C는 `wire_put_i32()`,
Java는 `ByteBuffer.order(BIG_ENDIAN)`으로 같은 규칙을 따른다. 전문 앞에는 24바이트 헤더가 붙고
그 안에 종별(type)과 바디 길이(`body_len`)가 있어, 받는 쪽은 "헤더 24바이트 → 그 안의 길이만큼 바디"를 읽는다.

**고정 길이의 함정**: 양쪽이 필드 순서나 값의 뜻을 다르게 알면, 바이트는 멀쩡한데 **조용히 틀린 값**으로
읽힌다. 그래서 채널계 테스트 `WireLayoutTest`가 C 헤더 파일을 직접 읽어 길이와 열거값을 대조한다.
(필드 순서까지는 못 잡는다는 한계도 그 테스트 주석에 적혀 있다.)

*쓰는 곳*: `core/include/wire.h`, `core/include/msg.h`, `channel/.../wire/`.

### 3.3 프로세스와 `fork()`

**프로세스**는 실행 중인 프로그램 하나다. 각자 **자기만의 메모리**를 가진다. 한 프로세스가 바꾼 변수를
다른 프로세스는 볼 수 없다.

`fork()`는 **지금 프로세스를 통째로 복사해 자식 프로세스를 하나 더 만드는** 시스템 호출이다.

```
          fork() 호출 전                    fork() 호출 후
   ┌────────────────────────┐       ┌────────────────────┐   ┌────────────────────┐
   │ 부모 프로세스            │       │ 부모                │   │ 자식 (복사본)        │
   │ 변수 x = 1              │  ──▶  │ x = 1               │   │ x = 1               │
   │ 호가창 A                │       │ 호가창 A            │   │ 호가창 A' (복사)     │
   └────────────────────────┘       │ fork()가 돌려준 값: │   │ fork()가 돌려준 값:  │
                                    │ 자식의 번호(PID)     │   │ 0                   │
                                    └────────────────────┘   └────────────────────┘
```

- `fork()`는 **한 번 불리고 두 번 반환한다.** 부모에게는 자식의 번호(PID, 양수)를, 자식에게는 0을 돌려준다.
  실패하면 부모에게 -1. 그래서 코드는 `if (pid == 0) { 자식이 할 일 } else { 부모가 할 일 }` 모양이 된다.
- 복사 직후 둘의 메모리 내용은 같지만 **그 뒤로는 따로 논다.** 자식이 호가창 A'에 주문을 넣어도
  부모의 호가창 A는 그대로다. (실제로는 한쪽이 값을 바꿀 때에야 그 부분을 복사하는 **copy-on-write**라 복사가 빠르다.)
- 자식이 끝나면 부모가 `waitpid()`로 거둬야 한다. 안 거두면 끝났는데도 목록에 남는 **좀비 프로세스**가 된다.
- 소켓 같은 열린 파일(fd)도 복사된다. 그래서 부모가 `listen` 중인 소켓을 여러 자식이 같이 `accept`할 수 있다 —
  워커 풀이 접속을 나눠 받는 방식이다.

**이 프로젝트에서의 의미.** `ledger/src/worker_pool.c`는 접속을 여러 자식 프로세스(워커)가 나눠
처리하게 만든 모듈이다(T3-04). 그런데 워커마다 매칭 엔진을 가지면 **호가창이 워커 수만큼** 생긴다.
어떤 주문은 워커 1의 호가창에서, 다음 주문은 워커 2의 호가창에서 체결되는 이상한 시장이 된다.
그래서 실제 원장 데몬은 워커 풀을 쓰지 않고 **한 프로세스, 한 스레드**로 전문을 차례로 처리한다.
워커 풀과 공유메모리 코드는 테스트를 통과한 부품으로 남아 있고, 계좌는 지금도 공유메모리 위에 있다(3.4절).

### 3.4 공유메모리

프로세스는 메모리를 따로 쓰지만, 운영체제에 부탁하면 **여러 프로세스가 같은 메모리 조각**을 볼 수 있다.
`mmap()`으로 만든 공유 영역을 `fork()` 전에 만들어 두면 부모와 자식이 같은 영역을 가리킨다.
한쪽이 쓴 값을 다른 쪽이 읽는다. 대신 **동시에 쓰면 깨지므로 잠금**이 필요하고, 잠금을 쥔 채 프로세스가 죽는 경우까지
생각해야 한다(4장 ledger 절).

*쓰는 곳*: `ledger/src/shm_segment.c`(영역 만들기), `ledger/src/account.c`(계좌를 그 안에 둔다).

### 3.5 TCP 소켓

프로세스(또는 컴퓨터)끼리 바이트를 주고받는 통로다. 서버와 클라이언트가 이런 순서로 호출한다.

```
서버(원장)                               클라이언트(채널계)
socket()   통로 만들기
bind(9100) 포트 번호 붙이기
listen()   손님 받을 준비
accept()   ◀────── 연결 ─────────────── socket() + connect(127.0.0.1:9100)
read()     ◀────── 전문 바이트 ───────── write()
write()    ─────── 응답 바이트 ────────▶ read()
close()                                  close()
```

**중요한 성질 — TCP는 "메시지"가 아니라 "바이트 흐름"이다.** 63바이트를 한 번에 써도 받는 쪽
`read()`는 20바이트, 43바이트로 나눠 받을 수 있다. 그래서 받는 쪽은 "헤더 24바이트가 다 모일 때까지 읽고,
헤더 속 길이만큼 바디가 모일 때까지 또 읽는다." 이것을 **전문 조립(framing)** 이라 한다.

*쓰는 곳*: `ledger/src/listener.c`, `fep/src/framer.c`, `channel/.../ledger/LedgerConnection.java`
(`DataInputStream.readFully`가 "다 모일 때까지 읽기"다).

### 3.6 시그널

운영체제가 프로세스에 보내는 짧은 알림이다. Ctrl+C는 `SIGINT`, `kill`은 기본 `SIGTERM`,
`kill -9`는 `SIGKILL`(막을 수 없는 강제 종료). 시그널을 받으면 **하던 일을 멈추고 핸들러 함수**가 불린다.
핸들러 안에서는 할 수 있는 일이 매우 적어서, 보통 **전역 플래그 하나에 1을 쓰고** 본 흐름이 그 플래그를 보고
정리한다. 그 변수의 타입이 `volatile sig_atomic_t`다(전역 가변 상태 금지 규약의 명시적 예외).

*쓰는 곳*: `ledger/src/listener.c`의 `g_stop`, `fep/src/evloop.c`. 프로세스를 `SIGKILL`로 죽여 복구를 시험하는
장애 주입 테스트는 `core/tests/test_fault_inject.c`.

### 3.7 epoll과 논블로킹 I/O

`read()`는 기본적으로 데이터가 올 때까지 **멈춰 기다린다(블로킹)**. 접속이 하나면 괜찮지만, FEP처럼
여러 접속을 한 스레드로 다루면 한 곳에서 멈추는 순간 나머지가 다 멈춘다.

- **논블로킹**: "읽을 게 없으면 기다리지 말고 바로 '없음'이라고 돌려줘"로 소켓을 설정한다.
- **epoll**: "이 소켓들 중 읽을 수 있거나 쓸 수 있게 된 것이 있으면 알려 줘"라고 운영체제에 등록하고
  `epoll_wait()`로 한꺼번에 기다린다. 알림이 온 소켓만 처리한다. 이 반복을 **이벤트 루프**라 한다.
- **송신 큐**: 상대가 느려 한 번에 다 못 보내면 남은 바이트를 큐에 두고, "쓸 수 있음" 알림이 오면 이어 보낸다.
  큐가 가득 차면 더 받지 않는 것이 **배압(backpressure)** 이다.

*쓰는 곳*: `fep/src/evloop.c`, `fep/src/sendq.c`.

### 3.8 세션, 시퀀스 번호, 재전송, 미응답 주문

오래 붙어 있는 접속에서는 "몇 번째 전문까지 받았나"를 세어야 빠진 것을 안다.

- 보낼 때마다 **시퀀스 번호**를 1씩 늘려 헤더에 싣는다.
- 받는 쪽이 14 다음에 21을 받으면 15~20이 빠졌다(**갭**). "15번부터 다시 보내 달라"(**재전송 요청**)를 보낸다.
- 보내는 쪽이 이미 버린 번호라면 "그 앞은 없다, 21번부터 이어라"(**갭 채우기**)로 답한다.
- 한동안 보낼 게 없어도 **하트비트**를 보내 살아 있음을 알린다. 끊기면 간격을 늘려 가며 다시 붙는다.

**미응답 주문(in-doubt)**: 주문을 보냈는데 답이 끊기면 "처리됐는지 모른다." 이때 **자동으로 다시 보내지 않는다.**
처리된 주문을 또 보내면 중복 주문이 되고, 그것이 가장 비싼 실수이기 때문이다. 조회로 확인한다.
채널계의 `202 IN_DOUBT` 응답도 같은 원칙이다.

*쓰는 곳*: `fep/src/session.c`, `fep/src/seqtrack.c`, `fep/src/ordmap.c`(미응답 판정), `channel/.../api/OrderService.java`.

### 3.9 디스크에 안전하게 쓰기 — 저널, fsync, 스냅샷, 대사

프로그램이 `write()`를 해도 데이터는 바로 디스크에 가지 않고 운영체제의 메모리(페이지 캐시)에 잠시 머문다.
이때 **전원이 나가면 사라진다.**

- **`fsync()`**: "지금 이 파일을 디스크에 확실히 써라"를 강제한다. 안전하지만 **느리다**
  (이 프로젝트 측정에서 한 프로세스 안 주문 처리량이 초당 1,534,801건에서 376건으로 떨어졌다).
- **저널(append-only journal)**: 들어온 입력을 **뒤에 덧붙이기만** 하는 파일이다. 레코드마다 CRC(체크섬)를 붙여,
  쓰다 만 마지막 레코드(**torn tail**)를 알아보고 버린다. 재시작하면 저널을 처음부터 다시 적용해 상태를 복구한다.
- **스냅샷**: 저널이 길어지면 재생이 느리므로 어느 시점의 상태를 통째로 저장한다. **임시 파일에 쓰고 → fsync →
  이름 바꾸기(rename)** 순서로 해서, 중간에 죽어도 "옛 스냅샷" 아니면 "새 스냅샷"만 남고 반쪽짜리는 없다.
- **대사(reconciliation)**: 결과를 독립적으로 다시 계산해 맞는지 대조한다. 첫 불일치에서 멈추지 않고 전부 센다.
- **장애 주입**: 쓰는 도중 프로세스를 `SIGKILL`로 죽이고 복구가 맞는지 본다. 단, `SIGKILL`은 **전원 차단이 아니다** —
  운영체제는 살아 있어 페이지 캐시의 데이터가 결국 디스크에 간다. 그래서 이 방법으로는 `fsync`의 효과를 시험할 수 없다.

*쓰는 곳*: `core/src/journal.c`, `core/src/snapshot.c`, `core/src/recon.c`, `core/tests/test_fault_inject.c`.

### 3.10 결정성과 시드

**같은 입력 → 언제나 같은 출력.** 전략 A와 B를 비교하려면 둘이 **똑같은 호가창**을 상대해야 한다. 그래서

- 매칭·SOR 코드에서 **시스템 시각(`time()`, `clock()`)을 읽지 않는다.** 시각은 입력 이벤트가 들고 오는 **논리 시각**이다.
- 난수는 **시드**(시작 숫자)를 명시적으로 받는 자체 생성기를 쓴다. 전역 `rand()`는 금지다 —
  다른 코드가 한 번 더 부르기만 해도 뒤의 숫자가 전부 바뀐다.
- 전략마다 호가창을 새로 만들고 같은 시드로 다시 채운다. 안 그러면 앞 전략이 먹은 호가를 뒤 전략이 못 먹어 불공평하다.

*쓰는 곳*: `exchange/src/liquidity/synthetic.c`(시드 난수), `exchange/src/liquidity/divergent.c`(두 시장 시나리오),
`exchange/tests/test_determinism.c`, `bench/compare.c`.

### 3.11 bp, 슬리피지, 분위수

- **bp(basis point)**: 1bp = 0.01%. 70,000원의 1bp는 7원이다.
- **슬리피지**: 주문을 낸 순간의 통합 최우선호가 대비 실제 평균 체결가가 얼마나 불리했는가(bp).
- **평균 체결 단가**: 체결 금액 합 ÷ 체결 수량. 원 단위로 버린 평균끼리 빼면 1bp 안팎의 차이가 사라지므로,
  이 프로젝트는 **체결 금액에서 직접** bp를 계산한다(T6-07).
- **체결률**: 낸 수량 중 체결된 비율. 단가가 좋아도 체결률이 낮으면 남은 수량을 나중에 더 비싸게 사게 된다.
- **p50/p95/p99**: 지연 시간을 줄 세웠을 때 50%, 95%, 99% 위치의 값. p99가 1,452ns면 100건 중 99건이 그보다 빠르다.
  **분위수는 더할 수 없다** — 단계별 p50의 합은 전 구간 p50이 아니다.
- **TPS**: 초당 처리 건수.

### 3.12 빌드와 검증 도구

- **CMake / ctest**: C 코드를 빌드하고 테스트를 등록·실행한다. `cmake -B build`, `cmake --build build`, `ctest --test-dir build`.
- **3종 빌드**: Debug(디버그 정보), Release(최적화, `assert` 꺼짐 — 여기서만 나는 경고가 있다), **ASan**(메모리 오류 검출).
  커밋 전에 셋 다 통과해야 한다. C는 ASan 라이브러리가 있는 **WSL(Ubuntu)** 에서 빌드한다.
- **ASan(AddressSanitizer)**: 배열 밖 쓰기, 해제한 메모리 쓰기, 누수를 실행 중에 잡는다. 일반 테스트가 통과해도
  배열 밖에 쓰는 버그가 있을 수 있는데, ASan 빌드는 그 순간 멈추고 위치를 알려 준다(T6-03의 변이 L13이 실제 예).
- **컴파일 옵션** `-std=c11 -Wall -Wextra -Werror -Wshadow -Wconversion`: 경고를 오류로 취급한다.
- **변이 검사(mutation testing)**: 코드를 일부러 망가뜨리고(예: `>`를 `>=`로) 테스트가 실패하는지 본다.
  테스트가 여전히 통과하면(**살아남음**) 그 줄은 사실상 검증되지 않은 것이다. 이 프로젝트는 태스크마다 수행했다.
  함정 세 가지: 망가뜨린 코드가 **컴파일 자체가 안 되면** "잡힘"이 아니다. 치환이 **실제로 적용됐는지** 파일을 비교해야 한다.
  그리고 **원본으로 먼저 돌려 통과하는지** 확인해야 한다 — 검사기가 고장 나면 전부 "잡힘"으로 보인다(T6-04에서 실제로 겪었다).
- **Maven(`mvnw`)**: Java 빌드·테스트. `./mvnw.cmd test`. Java와 화면은 Windows에서 빌드한다.
- **Vite / npm**: 화면 개발 서버와 빌드. `npm run dev`, `npm run build`, `npm run lint`.

### 3.13 채널계와 화면의 기초 (Java·React를 처음 보는 경우)

- **HTTP REST**: `POST /api/orders`처럼 "동사 + 주소"로 요청하고 JSON으로 주고받는다. 응답 상태 코드
  (200 접수, 400 입력 오류, 422 원장이 거절, 503 원장에 못 붙음, 202 결과 모름)가 뜻을 전한다.
- **WebSocket**: 한 번 연결해 두면 **서버가 먼저** 메시지를 밀어 보낼 수 있는 통로다. 체결·원장 상태 알림에 쓴다.
- **Spring Boot**: `@RestController`(HTTP 입구), `@Service`(업무 로직), `@Component`(그 밖의 부품)를 붙이면
  스프링이 객체를 하나씩 만들어 **생성자 인자로 서로 연결**해 준다(의존성 주입).
- **React**: 화면을 **컴포넌트**(함수)로 쪼갠다. `useState`는 값이 바뀌면 화면을 다시 그리는 변수,
  `useEffect`는 "화면이 뜬 뒤 할 일"(예: 1초마다 호가 읽기)과 "사라질 때 정리할 일"을 적는 곳이다.
- **CORS**: 브라우저는 `localhost:5173`에서 뜬 페이지가 `localhost:8080`에 JSON POST를 보내는 것을,
  8080이 허락하지 않으면 막는다. 그래서 개발 서버가 대신 전달하는 **프록시**를 쓴다(T6-05).

---

## 4. 모듈별 코드 읽기

이 장은 폴더마다 **읽는 순서 → 자료구조 → 함수 흐름 → 기법 → 테스트**로 설명한다.
위에서부터 차례로 읽는다.

### 4.1 core · exchange — 공통 기반과 매칭 엔진

#### 0. core와 exchange 한눈에 보기

`core/`는 모든 C 모듈이 함께 쓰는 **바닥 부품**이다. 가격·수량 같은 기본 타입, 에러 코드, 호가 단위 표, 주문 구조체와 주문 풀, 주문번호 해시 테이블, 전문(네트워크 메시지) 직렬화, 시세 피드 형식, 저널·스냅샷(재기동 복구), 대사(정합성 검사), 설정 파일 파서가 들어 있다.

`exchange/`는 그 부품 위에 세운 **가짜 거래소**다. 호가창, 매칭 엔진(지정가·시장가·IOC·FOK·취소·정정), 시장별 규칙(KRX/NXT 거래 시간과 허용 주문 유형, NXT 중간가), 체결 이벤트, 가상 참가자(난수로 주문을 만드는 생성기), 호가창에서 시세 피드를 뽑는 코드가 들어 있다.

의존 방향은 한쪽이다. `exchange`는 `core`를 링크하지만(`exchange/CMakeLists.txt`의 `target_link_libraries(exchange PUBLIC core m)`), `core`는 `exchange`를 모른다.

##### 전체 추천 읽기 순서

C를 잘 모르는 사람이라면 아래 순서가 가장 덜 막힌다. "작은 부품 → 그 부품을 조립한 것" 순서다.

| 단계 | 파일 | 한 줄 요약 |
|---|---|---|
| 1 | `core/include/types.h` | 가격·수량·주문번호·시각 타입, 매수/매도 등 열거형 |
| 2 | `core/include/errors.h` → `core/src/errors.c` | 에러 코드 목록(X 매크로) |
| 3 | `core/include/tick_size.h` → `core/src/tick_size.c` | 호가 단위 표 |
| 4 | `core/include/order.h` → `core/src/order_pool.c` | 주문 구조체와 주문 풀 |
| 5 | `core/include/order_index.h` → `core/src/order_index.c` | 주문번호 → 주문 해시 테이블 |
| 6 | `exchange/include/price_level.h` → `exchange/src/book/price_level.c` | 한 가격의 주문 대기열 |
| 7 | `exchange/include/order_book.h` → `exchange/src/book/order_book.c` | 호가창 |
| 8 | `exchange/include/event.h` → `exchange/src/event.c` | 체결·접수 이벤트 |
| 9 | `exchange/include/match.h` → `exchange/src/match/match_internal.h` → `match_engine.c` → `match_limit.c` → `match_market.c` → `match_ioc_fok.c` → `match_cancel_modify.c` | 매칭 엔진 |
| 10 | `docs/SPEC.md` 2·4장 → `exchange/include/market_rules.h` → `src/rules/market_rules.c` → `krx.c` → `nxt.c` | 시장 규칙 |
| 11 | `exchange/tests/test_midpoint.c` | 중간가 주문 동작 확인 |
| 12 | `exchange/include/synthetic.h` → `src/liquidity/synthetic.c` | 가상 참가자(시드 난수) |
| 13 | `core/include/kv_config.h` → `core/src/kv_config.c` | `key = value` 설정 파서 |
| 14 | `exchange/include/divergent.h` → `src/liquidity/divergent.c` | 두 시장에 다른 유동성 넣기 |
| 15 | `exchange/tests/test_determinism.c` | 전체가 결정적인지 확인 |
| 16 | `core/include/wire.h` → `core/src/wire.c` | 전문 헤더·빅엔디언 직렬화 |
| 17 | `core/include/msg.h` → `core/src/msg.c` | 전문 종별과 바디 |
| 18 | `core/include/feed.h` → `core/src/feed.c` → `exchange/include/feed_source.h` → `exchange/src/feed_source.c` | 시세 피드 |
| 19 | `core/include/journal.h` → `core/src/journal.c` | 덧붙이기 전용 저널 |
| 20 | `core/include/snapshot.h` → `core/src/snapshot.c` | 스냅샷과 복구 |
| 21 | `core/include/recon.h` → `core/src/recon.c` | 대사 |
| 22 | `core/tests/test_fault_inject.c` | 실제로 프로세스를 죽여 보는 장애 주입 |

규칙: **헤더(.h)를 먼저, 소스(.c)를 다음, 테스트(test_*.c)를 마지막에** 읽는다. 헤더에는 "무엇을 약속하는가"가 주석으로 길게 적혀 있고, 소스는 "어떻게 지키는가", 테스트는 "정말 지키는가"다.

---

#### 0-1. KRX와 NXT — 코드가 구현한 규칙 차이 (docs/SPEC.md 요약)

2025년 3월 넥스트레이드(NXT) 출범으로 같은 종목이 KRX와 NXT 두 곳에서 거래된다. 이 시뮬레이터의 두 매칭 엔진은 **체결 알고리즘은 똑같고**(가격 우선 → 시간 우선, 체결가는 먼저 있던 주문의 가격), **"언제 무엇을 받아 주는가"만 다르다.** 그 차이는 `exchange/src/rules/krx.c`와 `nxt.c` 두 파일에만 있다.

| 항목 | KRX (`krx.c`) | NXT (`nxt.c`) |
|---|---|---|
| 거래 시간 | 정규장 09:00:00 ~ 15:30:00 하나 | 프리 08:00~08:50, 오전 휴장 08:50~09:00:30, 메인 09:00:30~15:20, 오후 휴장 15:20~15:30, 애프터 15:30~20:00 |
| 경계 처리 | 시작 포함, 끝 제외 (15:30:00 정각은 마감) | 구간마다 `from 이상 to 미만` |
| 메인 시작 | 09:00:00 | **09:00:30** (30초 늦다) |
| 휴장 중 신규·정정 | 휴장 구간 없음 | 불가 (`ERR_MARKET_CLOSED`) |
| 휴장 중 취소 | 장 밖이면 취소도 불가 | **가능** |
| 지정가 | 정규장 | 프리·메인·애프터 |
| 시장가·IOC·FOK | 정규장 | 메인마켓만 (프리·애프터는 `ERR_NOT_SUPPORTED`) |
| 중간가(MIDPOINT) | 받지 않음 | 메인마켓만 |
| 중간가 가격 결정 | — | `(최우선매수 + 최우선매도) / 2`를 호가 단위로 **내림**, 접수 시점에 고정, 한쪽 호가가 비면 거부, 정정으로 가격 변경 불가 |
| 가격 제한폭 | 기준가 ±30% | 같음 |
| 호가 단위 | SPEC 3.2 표 | 같은 표 (코스피·코스닥 차이는 단순화로 무시) |

두 시장 공통 규칙으로 코드에 들어간 것:

- **정정 시 우선순위**(SPEC 4.4): 가격 변경·수량 증가는 시간 우선순위를 잃고, 수량 감소는 유지한다 (`match_cancel_modify.c`).
- **단순화 지점**: 단일가 매매(시가·종가) 없음, 연속 체결만. 정정한 가격이 반대편 최우선호가와 겹치면 체결하지 않고 거절한다(`ERR_INVALID_PRICE`). 청산·결제, 서킷브레이커 없음.
- 08:00~09:00, 15:30~20:00에는 NXT만 열려 있다. `test_nxt_rules.c`의 `test_asymmetry_with_krx`가 이 비대칭을 확인하고, SOR 테스트의 근거가 된다.

---

#### 1. types.h / errors.h — 기본 타입과 에러 코드

##### 1) 한 줄 역할과 필요성

프로젝트 전체가 쓰는 "숫자의 모양"과 "실패의 이름"을 정한다. 주식 시스템의 최종 산출물이 **평균 체결 단가 비교**이므로, 가격을 소수(`double`)로 두면 반올림 오차가 쌓여 비교 자체를 믿을 수 없다. 그래서 가격·수량을 전부 정수로 고정한다.

##### 2) 읽는 순서

1. `core/include/types.h`
2. `core/include/errors.h`
3. `core/src/errors.c`
4. `core/tests/test_types.c`

##### 3) 핵심 자료구조

| 타입 | 실제 타입 | 뜻 | 예 |
|---|---|---|---|
| `price_t` | `int32_t` | 원 단위 정수 가격 | 70,000원 → `70000` |
| `qty_t` | `int32_t` | 주식 수 | 10주 → `10` |
| `order_id_t` | `uint64_t` | 주문번호. **0은 "없음"** | `ORDER_ID_INVALID == 0` |
| `ts_t` | `int64_t` | 나노초 단위 **논리 시각**. 시스템 시계가 아니다 | 09:00:30 → `32430000000000` |

열거형(`enum`)은 "정해진 몇 가지 중 하나"를 표현한다.

| 열거형 | 값 |
|---|---|
| `side_t` | `SIDE_BUY=0`, `SIDE_SELL=1` |
| `order_type_t` | `ORDER_LIMIT`(지정가), `ORDER_MARKET`(시장가), `ORDER_IOC`, `ORDER_FOK`, `ORDER_MIDPOINT`(중간가) |
| `order_status_t` | `STATUS_NEW`, `STATUS_PARTIAL`, `STATUS_FILLED`, `STATUS_CANCELED`, `STATUS_REJECTED` |
| `market_t` | `MARKET_KRX=0`, `MARKET_NXT=1`, `MARKET_COUNT=2` |

경계 상수: `PRICE_MIN=1`, `PRICE_MAX=10,000,000`, `QTY_MIN=1`, `QTY_MAX=1,000,000`, `PRICE_LIMIT_PCT=30`, `TS_INVALID=-1`.

에러 코드(`error_code_t`)는 성공 0, 실패 음수다. `ERR_INVALID_PRICE(-3)`, `ERR_INVALID_TICK(-5)`, `ERR_PRICE_LIMIT(-6)`, `ERR_MARKET_CLOSED(-7)`, `ERR_NOT_SUPPORTED(-8)`, `ERR_NOT_FOUND(-9)`, `ERR_DUPLICATE(-10)`, `ERR_POOL_EXHAUSTED(-11)`, `ERR_NO_LIQUIDITY(-13)`, `ERR_IO(-16)` 등 18개가 있다.

##### 4) 핵심 함수 흐름

- `err_str(code)`: 코드를 사람이 읽을 설명으로 바꾼다. 모르는 코드여도 NULL 대신 `"알 수 없는 에러"`를 돌려준다 — 로그를 찍는 쪽이 NULL 검사를 안 해도 되게 한다.

##### 5) 이 코드가 쓰는 C 기법

- **typedef**: `typedef int32_t price_t;`는 "`price_t`라는 새 이름으로 `int32_t`를 부르겠다"는 뜻이다. 컴퓨터가 보기엔 같은 정수지만, 읽는 사람에게 "이건 가격"이라고 알려 준다.
- **고정 폭 정수**(`<stdint.h>`): `int`는 기계마다 크기가 다를 수 있지만 `int32_t`는 항상 32비트다. 전문(네트워크 메시지) 길이가 여기에 기대므로 필수다.
- **`_Static_assert`**: 컴파일할 때 조건을 검사해서 틀리면 **빌드 자체가 실패**한다. `types.h`는 `PRICE_MAX × 130 / 100`이 `int32` 범위를 넘지 않는지, `PRICE_MAX × QTY_MAX`(체결 금액 최대)가 `int64`를 넘지 않는지를 이렇게 못 박는다. `test_types.c`도 타입 폭·부호를 같은 방식으로 확인한다.
- **X 매크로**: 에러 코드를 한 목록에 한 번만 적고, 그 목록을 매크로로 두 번 "펼쳐" 열거형과 문자열 switch를 만든다.

```c
#define ERROR_CODE_LIST(X)                           \
    X(ERR_OK,             0,   "성공")               \
    X(ERR_INVALID_ARG,   -1,   "잘못된 인자")        \
    ...
#define ERROR_ENUM_ENTRY(name, value, text) name = (value),
typedef enum { ERROR_CODE_LIST(ERROR_ENUM_ENTRY) } error_code_t;
```

`errors.c`에서는 같은 목록을 `case (value): return (text);`로 펼친다. 코드를 하나 추가하면 설명 문자열이 자동으로 따라오므로 "한쪽만 고치는" 실수가 구조적으로 불가능하다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_types.c`)

- `price_t`/`qty_t`는 4바이트 부호 있는 정수, `order_id_t`는 8바이트 부호 없는 정수, `ts_t`는 8바이트 부호 있는 정수다(컴파일 타임 검사).
- 모든 에러 코드가 설명 문자열을 가진다.
- 성공은 0, 실패는 음수라는 규약이 지켜진다.

---

#### 2. tick_size — 호가 단위

##### 1) 한 줄 역할과 필요성

"70,050원에 사겠다"는 주문은 받을 수 없다. 50,000원 이상 200,000원 미만 구간의 최소 가격 변동폭(호가 단위)이 100원이기 때문이다. 이 모듈은 가격대별 호가 단위를 알려 주고, 가격이 단위에 맞는지 검사하고, 단위에 맞게 올림·내림한다.

##### 2) 읽는 순서

1. `core/include/tick_size.h`
2. `core/src/tick_size.c`
3. `core/tests/test_tick_size.c`

##### 3) 핵심 자료구조

`tick_size.c` 안의 정적 표 `TICK_TABLE[]`. 각 줄은 `{below, tick}` — "`below` 미만이면 단위는 `tick`"이다.

| below (배타적 상한) | tick |
|---|---|
| 2,000 | 1 |
| 5,000 | 5 |
| 20,000 | 10 |
| 50,000 | 50 |
| 200,000 | 100 |
| 500,000 | 500 |
| PRICE_MAX + 1 | 1,000 |

"2,000원은 5원 단위 구간"이라는 SPEC의 경계 규칙이 "미만(<)" 비교 하나로 표현된다.

##### 4) 핵심 함수 흐름

| 함수 | 동작 | 예 |
|---|---|---|
| `tick_size_of(price)` | 표를 위에서부터 훑어 `price < below`인 첫 줄의 `tick`. 범위 밖이면 0 | `tick_size_of(70000) == 100`, `tick_size_of(2000) == 5` |
| `is_valid_tick(price)` | `price % tick == 0`인가 | `is_valid_tick(70050) == false` |
| `round_to_tick(price, up)` | 나머지를 빼서 내림, 한 틱 더해 올림. 이미 맞으면 그대로 | `round_to_tick(70050, true) == 70100`, `round_to_tick(4999, true) == 5000` |
| `tick_segment_end(price)` | 그 가격이 속한 구간의 배타적 상한 | `tick_segment_end(3000) == 5000` |

올림이 다음 구간으로 넘어가도(4,999 → 5,000) 결과가 유효한 이유: 모든 구간의 시작 가격은 새 구간의 단위로도 나누어떨어진다. 코드가 `assert(is_valid_tick(result))`로 이를 확인한다.

##### 5) 이 코드가 쓰는 C 기법

- **외부 입력은 값으로 거른다**: 범위 밖 가격은 `assert`로 죽이지 않고 0/false를 돌려준다. 0은 어떤 구간에서도 정상 호가 단위가 아니므로 실패 표시로 쓸 수 있다.
- **`sizeof(표)/sizeof(표[0])`**: 배열 원소 개수를 구하는 C 관용구(`TICK_TABLE_LEN`).
- **`assert(0 && "메시지")`**: 도달하면 안 되는 자리에 메시지를 달아 둔다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_tick_size.c`)

- 모든 구간 경계(2,000 / 5,000 / 20,000 / 50,000 / 200,000 / 500,000)에서 (경계-1, 경계, 경계+1)의 호가 단위가 맞다.
- 2,000원이 1원이 아니라 5원 구간이라는 점을 명시적으로 확인한다.
- 하한(`PRICE_MIN`)과 범위 밖 입력의 처리.

---

#### 3. order.h / order_pool — 주문 구조체와 주문 풀

##### 1) 한 줄 역할과 필요성

주문 하나를 담는 상자(`order_t`)를 정의하고, 그 상자를 **미리 잔뜩 만들어 두었다가 빌려주고 돌려받는** 풀을 제공한다. 매칭 엔진이 주문마다 `malloc`(운영체제에 메모리 요청)을 하면 그 지연이 들쭉날쭉해져 측정을 흐린다.

##### 2) 읽는 순서

1. `core/include/order.h`
2. `core/src/order_pool.c`
3. `core/tests/test_order_pool.c`
4. `core/tests/test_order_pool_double_free.c`

##### 3) 핵심 자료구조

`order_t`

| 필드 | 뜻 |
|---|---|
| `id` | 거래소가 준 주문번호. 0이면 미배정 |
| `client_order_id` | 증권사(채널계)가 준 주문번호 |
| `ts` | 논리 시각. 시간 우선의 기준 |
| `price` | 가격 |
| `qty` | **원 주문 수량** |
| `filled_qty` | 체결된 수량. 잔량 = `qty - filled_qty` |
| `side`, `type`, `market` | 매수/매도, 주문 유형, 시장 |
| `prev`, `next` | 같은 가격 대기열 안의 앞·뒤 주문을 가리키는 포인터 |

예: "KRX에 70,000원에 10주 사는 지정가 주문, 주문번호 1"은 이렇게 들어간다.

```c
order_t req = {0};
req.id = 1;  req.side = SIDE_BUY;  req.type = ORDER_LIMIT;
req.price = 70000;  req.qty = 10;  req.market = MARKET_KRX;
req.ts = TOD_NS(10, 0, 0);   /* 10:00:00 */
```

3주가 체결되면 `filled_qty = 3`, 잔량은 `order_remaining_qty(&req) == 7`이다. `qty`는 10 그대로 둔다 — 평균 체결 단가 계산에 원 수량이 필요하기 때문이다.

풀 내부(`order_pool.c`의 `struct order_pool`)

| 필드 | 뜻 |
|---|---|
| `slots` | `order_t` 배열 (capacity개) |
| `next` | `int32_t` 배열. 빈 슬롯이면 "다음 빈 슬롯 번호", 사용 중이면 `SLOT_IN_USE(-2)` |
| `free_head` | 첫 빈 슬롯 번호. 없으면 `SLOT_NIL(-1)` |
| `capacity` | 슬롯 수 |

예: 용량 4로 만들면 `next = [1, 2, 3, -1]`, `free_head = 0`. 하나 빌리면 슬롯 0을 주고 `free_head = 1`, `next[0] = -2`. 슬롯 0을 돌려주면 `next[0] = 1`(원래 head), `free_head = 0`.

##### 4) 핵심 함수 흐름

- `order_pool_create(capacity)`: `calloc`으로 슬롯과 `next` 배열을 잡고, `next[i] = i+1`로 빈 슬롯 사슬을 만든다.
- `order_pool_acquire(pool)`:
  1. `free_head`가 `SLOT_NIL`이면 NULL (고갈 — 크래시하지 않는다)
  2. `free_head`를 다음 빈 칸으로 옮기고 그 칸을 `SLOT_IN_USE`로 표시
  3. `memset`으로 슬롯을 0으로 밀고 `id = ORDER_ID_INVALID`, `ts = TS_INVALID`로 초기화 (이전 주문 흔적 제거)
- `order_pool_release(pool, order)`:
  1. 포인터 뺄셈 `order - pool->slots`로 슬롯 번호를 구한다
  2. 이 풀의 슬롯이 아니거나, `SLOT_IN_USE`가 아니면(= 이중 해제) `assert`. `NDEBUG` 빌드에서 assert가 없어도 **무시하고 반환**해서 빈 칸 사슬이 꼬이지 않게 한다
  3. 빈 칸 사슬 맨 앞에 끼운다

##### 5) 이 코드가 쓰는 C 기법

- **구조체와 포인터**: `order_t *p`는 "주문이 있는 메모리 주소". `p->qty`는 "그 주소에 있는 주문의 qty". `&x`는 "x의 주소"다.
- **불투명 타입(opaque type)**: 헤더에는 `typedef struct order_pool order_pool_t;`만 있고 내용은 `.c`에만 있다. 쓰는 쪽은 포인터로만 다루므로 내부 구조를 건드릴 수 없다. `order_index_t`, `order_book_t`, `match_engine_t`, `journal_t`, `synth_gen_t`, `divergent_t`도 같은 방식이다.
- **고정 크기 메모리 풀 + 프리리스트**: 시작할 때 한 번만 할당하고, 이후 빌리기/돌려주기는 배열 인덱스 몇 개만 바꾸는 O(1) 연산이다. `next[]` 하나가 "빈 칸 사슬"과 "사용 중 표시"를 겸해서 이중 해제를 O(1)에 잡는다.
- **`static inline` 함수**: `order_remaining_qty()`처럼 짧은 함수를 헤더에 두어 호출 비용 없이 쓴다.
- **`create`/`destroy` 짝**: 할당 함수와 해제 함수 이름을 짝맞춘다(CLAUDE.md 규약). `destroy`는 NULL을 받아도 아무 일도 안 한다.
- **`calloc`의 오버플로 검사**: `calloc(개수, 크기)`는 곱셈이 넘치면 실패로 돌려준다(주석에 명시).
- **assert vs 에러 코드**: 이중 해제는 외부 입력이 아니라 **우리 코드의 버그**라서 `assert`로 잡는다. 반대로 풀 고갈은 정상적으로 일어날 수 있는 상황이라 NULL을 돌려준다.

##### 6) 테스트가 보장하는 것

- `test_order_pool.c`: 용량 0 이하는 NULL; 고갈까지 빌리고 → 전부 돌려주고 → 다시 빌려도 정확히 용량만큼 나온다(슬롯이 새지 않는다); 고갈 시 NULL; 재사용 슬롯에 이전 주문 값이 남지 않는다; NULL 인자 안전.
- `test_order_pool_double_free.c`: 이중 해제 시 `SIGABRT` 핸들러로 "assert가 터졌다"를 통과로 본다. 터지지 않았다면(NDEBUG 빌드) 이후 풀에서 용량만큼만, 서로 다른 슬롯이 나오는지 확인한다. 핸들러를 곧바로 원복하는 이유(다른 assert 실패까지 통과로 오인하지 않기 위해)가 주석에 있다.

---

#### 4. order_index — 주문번호 해시 테이블

##### 1) 한 줄 역할과 필요성

취소·정정 요청은 "주문번호 123을 취소하라"처럼 번호만 들고 온다. 호가창 전체를 뒤지지 않고 번호로 주문을 **O(1)**에 찾기 위한 해시 테이블이다.

##### 2) 읽는 순서

1. `core/include/order_index.h`
2. `core/src/order_index.c`
3. `docs/decisions/0001-주문-인덱스-해시-테이블.md` (설계 근거)
4. `core/tests/test_order_index.c`

##### 3) 핵심 자료구조

```c
typedef struct { order_id_t key; order_t *val; } index_slot_t;  /* key 0 = 빈 칸 */
struct order_index {
    index_slot_t *slots;
    int32_t mask;     /* 칸 수 - 1 (칸 수는 2의 거듭제곱) */
    int32_t capacity; /* 담을 수 있는 주문 수 상한 */
    int32_t count;
};
```

예: `index_create(100)`이면 부하율 0.5를 지키기 위해 200 이상인 2의 거듭제곱 256칸을 잡고 `mask = 255`다. 주문번호 7을 넣으면 `hash_id(7) & 255` 번 칸부터 빈 칸을 찾는다.

##### 4) 핵심 함수 흐름

- `hash_id(id)`: splitmix64의 마무리 함수로 비트를 섞는다. 주문번호가 1, 2, 3…처럼 연속이라 그대로 쓰면 이웃 칸에 몰리기 때문이다. 난수가 아니라 고정 계산이라 결정적이다.
- `index_put(idx, id, order)`:
  1. id가 0이면 `ERR_INVALID_ARG`, 이미 `capacity`만큼 찼으면 `ERR_POOL_EXHAUSTED`
  2. 제자리 칸부터 한 칸씩(`(i + 1) & mask`) 가며 빈 칸을 찾는다(**선형 탐사**). 가다가 같은 키를 만나면 `ERR_DUPLICATE`
  3. 빈 칸에 기록, `count++`
- `index_get(idx, id)`: 같은 경로로 찾다가 빈 칸을 만나면 "없다"(NULL).
- `index_remove(idx, id)` — **역방향 시프트 삭제**(Knuth 알고리즘 R):
  1. 지울 칸을 비운다(hole)
  2. 뒤쪽 칸들을 차례로 보면서, 그 항목의 "제자리(home)"가 (hole, probe] 범위 밖이면 — 즉 원래 hole 자리나 그 앞에 있어야 했는데 밀려난 항목이면 — hole로 당겨 온다
  3. 당겨 온 자리가 새 hole이 되어 반복, 빈 칸을 만나면 끝

그냥 칸을 비우기만 하면 그 뒤로 밀려 있던 항목까지 가는 탐사 사슬이 끊겨, 멀쩡한 주문을 "없다"고 답하게 된다.

##### 5) 이 코드가 쓰는 C 기법

- **오픈 어드레싱 + 선형 탐사**: 칸마다 연결 리스트를 다는 체이닝은 노드 할당이 필요한데, 배열 하나면 핫 패스에서 `malloc`이 없다.
- **2의 거듭제곱 크기와 `& mask`**: `% 256` 대신 `& 255`로 나머지를 구한다.
- **톰스톤 대신 역방향 시프트**: 삭제 표시(톰스톤)를 쌓으면 주문을 끊임없이 넣고 빼는 엔진에서 탐사 길이가 계속 는다.
- **순회 API를 일부러 두지 않는다**: 해시 순서는 사람이 예측할 수 없으므로, 거기에 기대는 코드가 생기면 결정성이 깨진다.
- `assert(idx->count <= idx->mask)`: "빈 칸이 반드시 남으므로 탐사가 무한히 돌지 않는다"는 불변조건.

##### 6) 테스트가 보장하는 것 (`core/tests/test_order_index.c`)

- 기본 등록·조회·삭제, 중복 번호 거절, 0번 거절.
- 용량을 넘으면 `ERR_POOL_EXHAUSTED`.
- 10만 건을 넣고 홀수 번호만 지운 뒤 짝수 번호가 전부 살아 있다(무리 한가운데 삭제에도 사슬이 끊기지 않는다).
- 충돌이 몰리는 경우에도 올바르게 찾는다.

---

#### 5. wire — 전문 공통 헤더와 빅엔디언 직렬화

##### 1) 한 줄 역할과 필요성

프로세스끼리 TCP로 주고받는 **전문**(고정 형식 메시지)의 24바이트 공통 헤더와, 정수·문자열을 바이트로 옮기는 도구다. 실제 증권사에서 채널계·원장·FEP 사이를 오가는 메시지에 해당한다. 형식은 이 프로젝트의 자체 설계다.

##### 2) 읽는 순서

1. `core/include/wire.h` (주석에 "왜 구조체를 그대로 보내지 않는가"가 길게 있다)
2. `core/src/wire.c`
3. `core/tests/test_wire.c`

##### 3) 핵심 자료구조

헤더 배치 (24바이트, 모두 빅엔디언)

| 오프셋 | 크기 | 필드 | 뜻 |
|---|---|---|---|
| 0 | 2 | magic | `0x4D53` ("MS"). 스트림 동기가 깨진 것을 빨리 잡는다 |
| 2 | 1 | version | 1. 다르면 해석하지 않는다 |
| 3 | 1 | type | 전문 종별(msg.h) |
| 4 | 4 | body_len | 뒤따르는 바디 바이트 수. 상한 `WIRE_BODY_MAX` = 65,536 |
| 8 | 8 | seq | 보낸 쪽 기준 시퀀스 번호 |
| 16 | 8 | ts | 논리 시각 |

`wire_header_t`는 이 값들을 담는 C 구조체이지만 **바이트 배치와는 무관**하다(magic은 구조체에 없다 — 인코딩 때 채운다).

예: 종별 1(주문 요청), 바디 39바이트, seq 7이면 앞 8바이트는 `4D 53 01 01 00 00 00 27`이다.

##### 4) 핵심 함수 흐름

- `wire_put_u32(p, v)`: `p[0] = v >> 24`, `p[1] = v >> 16`, `p[2] = v >> 8`, `p[3] = v` — 큰 자리부터 적는다.
- `wire_get_i32(p)`: 부호 없는 값으로 읽은 뒤, `INT32_MAX`를 넘으면 정의된 연산만으로 음수로 되돌린다. `(int32_t)u` 직접 캐스팅은 범위 밖일 때 "구현 정의" 동작이라 피한다.
- `wire_put_str(p, field_len, s)`: 필드를 0으로 채우고 앞에 문자열을 복사, 넘치면 자른다. **널 종료를 보장하지 않는다** — 길이가 규격이다.
- `wire_get_str(p, field_len, out)`: 0을 만나거나 필드 끝까지 복사하고 `out`에 널을 붙인다(`out`은 `field_len + 1` 이상).
- `wire_encode_header(h, buf, cap)`: 자리가 24바이트 미만이거나 body_len이 한도 초과면 `ERR_INVALID_ARG`. magic과 version은 여기서 직접 채운다.
- `wire_decode_header(buf, len, out)`: 길이 부족 → `ERR_INVALID_ARG`, magic 불일치 → `ERR_INVALID_ARG`, version 불일치 → `ERR_NOT_SUPPORTED`(해석 시도 없음), body_len 초과 → `ERR_INVALID_ARG`. 성공하면 24를 돌려준다.

##### 5) 이 코드가 쓰는 C 기법

- **구조체를 그대로 `write`하지 않는다**: 컴파일러가 필드 사이에 넣는 **패딩**과 CPU의 **바이트 순서**에 기대게 되기 때문이다. 필드를 하나씩 바이트로 옮긴다.
- **빅엔디언(네트워크 바이트 순서)**: 100은 `00 00 00 64`로 적힌다. 16진수 덤프를 눈으로 읽기 쉬워서 골랐다(x86은 리틀엔디언이라 메모리에는 `64 00 00 00`).
- **비트 시프트(`>>`, `<<`)와 캐스팅**: 정수를 바이트로 쪼개고 다시 합친다.
- **길이 선행(length-prefix) 프레이밍**: 구분자 대신 헤더에 바디 길이를 둔다. 받는 쪽은 헤더만 보면 얼마를 더 읽을지 안다.
- **magic과 version**: 체크섬이 아니다. TCP가 이미 체크섬을 하므로 여기서는 "엉뚱한 스트림"과 "다른 판"을 빨리 알아채는 표식만 둔다.
- **상한 검사**: body_len이 `0xFFFFFFFF`여도 4GB를 할당하지 않도록 64KB 상한을 둔다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_wire.c`)

- 정수가 빅엔디언으로 적힌다 — **기대 바이트열을 손으로 적어 대조**한다(왕복만 보면 인코딩·디코딩이 같이 틀려도 통과하므로).
- 경계값(최솟값·최댓값·음수) 왕복 보존, 고정 길이 문자열 자르기·채우기.
- 헤더 길이가 구조체 `sizeof`와 무관하게 24바이트다.
- 짧은 버퍼, 한도 초과 body_len, 다른 magic, 다른 version을 거절한다.

---

#### 6. msg — 전문 종별과 바디

##### 1) 한 줄 역할과 필요성

주문 요청·응답, 취소, 정정, 조회, 체결 통보, 로그인, 하트비트, 재전송, 호가 조회 등 **16가지 전문의 바디 배치와 인코딩/디코딩 함수**다. 채널계(Java) ↔ 원장(C) ↔ FEP 사이의 "말"을 정의한다.

##### 2) 읽는 순서

1. `core/include/msg.h` (맨 위 표가 바디 배치 요약이다)
2. `core/src/msg.c` — 종별 표 함수 → `enc_check`/`dec_check` → 주문 요청 인코딩/디코딩 한 쌍만 자세히 보면 나머지는 같은 패턴이다
3. `core/tests/test_msg.c`

##### 3) 핵심 자료구조

종별 목록(`MSG_TYPE_LIST`, X 매크로)

| 코드 | 이름 | 바디 길이 |
|---|---|---|
| 1 / 2 | `MSG_ORDER_REQ` / `MSG_ORDER_ACK` | 39 / 29 |
| 3 / 4 | `MSG_CANCEL_REQ` / `MSG_CANCEL_ACK` | 28 / 25 |
| 5 / 6 | `MSG_MODIFY_REQ` / `MSG_MODIFY_ACK` | 36 / 25 |
| 7 / 8 | `MSG_QUERY_REQ` / `MSG_QUERY_ACK` | 20 / 38 |
| 9 | `MSG_FILL_NOTI` (체결 통보, 응답 없음) | 46 |
| 10 / 11 | `MSG_LOGIN_REQ` / `MSG_LOGIN_ACK` | 16 / 4 |
| 12 | `MSG_HEARTBEAT` | 0 |
| 13 | `MSG_RESEND_REQ` | 8 |
| 14 | `MSG_GAP_FILL` | 8 |
| 15 / 16 | `MSG_BOOK_REQ` / `MSG_BOOK_ACK` | 9 / 169 |

코드 0은 쓰지 않는다 — 0으로 초기화된 버퍼가 유효한 전문으로 보이면 안 되기 때문이다.

`msg_order_req_t` (주문 요청, 바디 39바이트)

| 필드 | 바이트 | 뜻 |
|---|---|---|
| `account[13]` | 12 | 계좌번호 (C 구조체는 널 자리까지 +1) |
| `symbol[9]` | 8 | 종목코드 |
| `cl_ord_id` | 8 | 채널계 주문번호 |
| `side`, `type`, `market` | 1씩 | 매수/매도, 주문 유형, 시장. `market = MSG_MARKET_AUTO(255)`이면 원장이 SOR로 시장을 정한다 |
| `price`, `qty` | 4씩 | 가격, 수량 |

예: 계좌 "ACC001"이 삼성전자("005930")를 70,000원에 10주 매수 → `account="ACC001"`, `symbol="005930"`, `side=0`, `type=0`, `market=255`, `price=70000`, `qty=10`. 바이트로는 계좌 12바이트(뒤는 0), 종목 8바이트, …, 마지막 8바이트가 `00 01 11 70 00 00 00 0A`다.

`msg_query_ack_t`의 `last`는 "이것이 마지막 응답"이라는 표시다. 한 건도 없어도 `last=1`인 빈 응답이 와야 "거래소에 없다"를 결론 낼 수 있다. `msg_book_ack_t`는 10단 호가를 가격·수량 배열 넷(`bid_price`, `bid_qty`, `ask_price`, `ask_qty`)으로 나눠 담는다.

##### 4) 핵심 함수 흐름

- `msg_body_len(type)`: X 매크로로 만든 switch. 모르는 종별이면 -1. 받는 쪽은 **헤더의 body_len을 이 값과 대조**한다.
- `msg_is_known(type)`: `msg_body_len(type) >= 0`.
- `msg_reply_type(req)`: ORDER_REQ → ORDER_ACK 등. 체결 통보·응답 종별·모르는 값이면 `MSG_UNKNOWN`.
- `msg_encode_*(m, buf, cap)`:
  1. `enc_check`: `cap`이 규격 길이보다 작으면 **한 바이트도 쓰지 않고** `ERR_INVALID_ARG`
  2. 포인터 `p`를 앞으로 옮기며 `wire_put_*`로 필드를 차례로 적는다
  3. 쓴 바이트 수(`p - buf`)를 돌려준다
- `msg_decode_*(buf, len, out)`:
  1. `dec_check`: `len`이 규격 길이와 **정확히 같아야** 한다(짧아도, 길어도 거절)
  2. `memset(out, 0, ...)` 후 `wire_get_*`로 차례로 읽는다

헤더는 여기서 쓰지 않는다. 시퀀스 번호와 시각은 "세션의 상태"이지 전문 내용이 아니라서 호출부가 `wire_encode_header()`로 따로 쓴다.

##### 5) 이 코드가 쓰는 C 기법

- **X 매크로 확장판**: 이름·코드·길이·설명 네 가지를 한 목록에 두고 enum, 이름 switch, 길이 switch로 세 번 펼친다.
- **계산식으로 적은 길이 매크로**: `#define MSG_ORDER_REQ_LEN (MSG_ACCOUNT_LEN + MSG_SYMBOL_LEN + 8 + 1 + 1 + 1 + 4 + 4)`. 사람이 더한 숫자를 적지 않는다.
- **포인터 산술**: `p += 8`로 다음 필드 위치로 이동, `p - buf`로 쓴 길이를 구한다. `wire_put_u8(p++, ...)`는 "지금 자리에 쓰고 한 칸 전진".
- **채움 바이트 없음**: 바이트 단위로 직렬화하므로 정렬을 맞출 이유가 없다.
- **`void *` 인자**: `enc_check(const void *m, ...)`는 어떤 구조체 포인터든 받아 NULL 검사만 한다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_msg.c`)

- 종별 코드·이름·길이가 한 목록에서 나오고 헤더 표와 일치한다. 모르는 종별은 -1·`"알 수 없는 전문"`.
- 요청마다 응답 종별이 짝지어져 있고 체결 통보는 짝이 없다.
- ORDER_REQ, FILL_NOTI, BOOK_ACK의 **바이트 위치를 직접 대조**한다.
- 모든 종별 왕복이 필드를 보존한다; 자리 부족 인코딩과 길이가 다른 디코딩을 거절한다; 헤더 body_len과 바디 길이가 맞는다.

---

#### 7. feed — 전략 엔진용 시세 피드 형식

##### 1) 한 줄 역할과 필요성

호가(10단)와 체결을 **방송**하는 메시지 형식과, 받는 쪽이 "중간에 빠진 메시지가 있는가"를 판정하는 로직이다. 주문 전문은 1:1 대화라 빠진 걸 다시 달라고 할 수 있지만, 시세는 1:N 방송이라 다시 달라고 할 상대가 없다. 그래서 설계가 다르다.

##### 2) 읽는 순서

1. `core/include/feed.h` (증분 대신 스냅샷을 고른 이유가 주석에 있다)
2. `core/src/feed.c`
3. `core/tests/test_feed.c`
4. 이어서 11번 이후의 `exchange/feed_source` (만드는 쪽)

##### 3) 핵심 자료구조

머리 20바이트: `magic:u16("MF" 0x4D46) version:u8 type:u8 seq:u64 ts:i64`. 주문 전문의 "MS"와 magic을 다르게 둬서 채널을 섞으면 바로 드러난다.

| 구조체 | 필드 | 예 |
|---|---|---|
| `feed_level_t` | `price`, `qty` (없는 단은 둘 다 0) | `{70000, 150}` |
| `feed_book_t` | `symbol`, `market`, `depth`(1~10), `bid[10]`, `ask[10]` | 매수 3단만 있고 depth 5면 `bid[3]`, `bid[4]`는 `{0,0}` |
| `feed_trade_t` | `symbol`, `market`, `side`(**taker 방향**), `price`, `qty`, `exec_id` | 매수 주문이 들어와 70,100원에 30주 체결 → `side=SIDE_BUY` |
| `feed_hdr_t` | `version`, `type`(`FEED_BOOK=1`/`FEED_TRADE=2`), `seq`, `ts` | |
| `feed_sub_t` | `expected`(다음 기대 번호, 0이면 아직 없음), `stale`(믿지 말라), `gaps`(갭 누계) | |

BOOK 바디 = 12바이트 머리(`symbol[8] market depth rsv:u16`) + 매수 줄 depth개 + 매도 줄 depth개(줄당 8바이트). depth 10이면 172바이트. TRADE 바디는 26바이트.

##### 4) 핵심 함수 흐름

- `feed_encode_book`: depth·market 범위 검사 → 머리 → **매수를 다 쓰고 매도를 쓴다**.
- `feed_decode_book`: 가변 길이라 **depth를 먼저 읽고 범위(1~10)를 확인한 뒤** 기대 길이 `12 + depth × 16`과 `len`이 정확히 같은지 본다.
- `feed_sub_accept(s, h)` — 빠짐 판정:
  1. 첫 메시지(`expected == 0`): 번호가 무엇이든 OK, `expected = seq + 1` (장중에 붙을 수 있어야 한다)
  2. `seq < expected`: `FEED_SEQ_DUP` (버린다, expected는 되돌리지 않는다)
  3. `seq > expected`: `FEED_SEQ_GAP`, `gaps++`, `stale = true`, `expected = seq + 1`로 **앞으로 옮긴다**. 그 메시지가 BOOK이면 곧바로 `stale = false`
  4. 같으면 OK. BOOK이면 `stale = false`. TRADE로는 stale이 풀리지 않는다
- `feed_sub_usable(s)`: 받은 게 있고 stale이 아니면 true. false인 동안 전략은 새 주문을 내지 않는다(취소는 가능).

예: 100(BOOK) OK → 101(TRADE) OK → 105(TRADE) GAP, stale → 106(TRADE) OK지만 여전히 stale → 107(BOOK) OK, stale 해제 → 103이 늦게 오면 DUP.

##### 5) 이 코드가 쓰는 C 기법

- **증분(델타) 대신 매번 전체 스냅샷**: 하나 놓치면 뒤가 전부 틀리는 증분과 달리, 스냅샷은 다음 것 하나로 다시 맞는다. 크기(10단 약 200바이트)를 대가로 치른다. 헤더 주석에 `ponytail:` 표시로 "대역이 실제로 문제 되면 그때 증분 + 주기적 스냅샷"이라고 한계를 적어 두었다.
- **시퀀스 번호로 갭 감지**: 재전송을 요청할 수 없는 채널에서 "다음 스냅샷까지 믿지 않는다"로 회복한다.
- **`uint8_t` → enum 변환 전 범위 검사**: 전선에서 온 바이트를 `market_t`/`side_t`로 바꾸기 전에 값이 유효한지 먼저 본다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_feed.c`)

- 머리·BOOK·TRADE 왕복, magic "MF"가 빅엔디언으로 적힌다; 다른 magic/version 거절.
- depth 0, 11, 전선 위의 잘못된 depth, 길이가 depth와 안 맞는 BOOK을 거절한다.
- **같은 입력은 같은 바이트**를 만든다.
- 첫 메시지·순서대로·중복·갭 후 스냅샷 회복·갭에서 expected 전진·스냅샷 자체가 갭인 경우를 각각 확인한다.

---

#### 8. journal — 덧붙이기만 하는 입력 기록

##### 1) 한 줄 역할과 필요성

원장 프로세스가 죽었다 살아났을 때 상태를 되살리기 위해 **들어온 입력**을 파일에 순서대로 적는다. 엔진이 결정적이므로 입력만 다시 넣으면 결과가 똑같이 다시 나온다. 결과(체결)는 적지 않는다 — 양이 몇 배로 늘고, "적힌 결과"와 "다시 계산한 결과" 중 무엇이 진실인지 답할 수 없게 되기 때문이다.

##### 2) 읽는 순서

1. `core/include/journal.h`
2. `core/src/journal.c`
3. `core/tests/test_journal.c`

##### 3) 핵심 자료구조

레코드 하나의 파일 배치

| 오프셋 | 크기 | 필드 |
|---|---|---|
| 0 | 4 | magic `0x4D534A31` ("MSJ1") |
| 4 | 1 | type |
| 5 | 3 | 채움(0) |
| 8 | 4 | len (실을 것 길이, 상한 65,536) |
| 12 | 8 | seq |
| 20 | 8 | ts |
| 28 | len | payload |
| 28+len | 4 | CRC32 (머리 + payload 전체를 덮는다) |

예: 39바이트 주문 요청 전문을 적으면 레코드는 28 + 39 + 4 = 71바이트다.

- `journal_t`(불투명): `FILE *fp`, `writable`, `count`, 재생용 버퍼 `buf[65536]`.
- `jrec_t`: 재생 때 콜백에 넘기는 레코드 한 건(`type`, `seq`, `ts`, `payload`, `len`). `payload`는 **다음 콜백 호출까지만 유효**하다.
- `journal_fn`: `int (*)(const jrec_t *rec, void *ctx)` — 0이 아닌 값을 돌려주면 재생을 멈춘다.

##### 4) 핵심 함수 흐름

- `journal_create(path)`: `fopen(path, "ab")` — 덧붙이기 모드. 앞을 고칠 방법 자체를 두지 않는다.
- `journal_append(j, type, seq, ts, payload, len)`:
  1. 읽기 전용 핸들이면 `ERR_NOT_SUPPORTED`, len 초과면 `ERR_INVALID_ARG`
  2. 버퍼에 머리 → payload를 채우고 CRC32를 계산해 뒤에 붙인다
  3. **한 번의 `fwrite`**로 통째로 쓴다 (나눠 쓰면 찢어질 자리가 늘어난다)
  4. `journal_sync()` = `fflush`(프로세스 버퍼 → 커널) + `fsync`(커널 → 디스크). **매 레코드마다** 한다
- `journal_replay(j, fn, ctx, &truncated)`:
  1. 파일 처음으로 `fseek`
  2. 머리 28바이트를 읽는다. 0바이트면 깨끗한 끝, 28 미만이면 찢어진 꼬리
  3. magic이 틀리면 멈춘다(되맞추려 하지 않는다)
  4. **len이 버퍼 크기를 넘으면 믿지 않고 멈춘다**
  5. payload와 CRC를 읽고, 부족하거나 CRC가 틀리면 멈추고 `*truncated = true`
  6. 온전하면 콜백 호출, 개수를 센다
  7. 읽어 넘긴 레코드 수를 돌려준다

##### 5) 이 코드가 쓰는 C 기법

- **append-only 로그**: 앞쪽은 뒤에 의해 바뀌지 않으므로 "마지막 레코드가 반쪽"인 경우만 다루면 된다.
- **CRC32**: 데이터가 조금이라도 바뀌었는지 잡는 검사값. 여기서는 표 없이 비트 단위로 계산한다(`0xEDB88320` 다항식). 주석에 `ponytail:`로 "병목이 되면 표 방식으로"라고 적혀 있다. 전문에는 CRC가 없지만(TCP가 한다) 파일에는 쓰다 죽은 반쪽을 막아 줄 존재가 없어서 둔다.
- **찢어진 꼬리(torn tail) 복구**: 반쪽 레코드를 고치거나 지어내지 않고 **그 앞까지만** 인정한다. 그리고 그 사실을 `out_truncated`로 숨기지 않고 알린다.
- **`fflush` vs `fsync`**: `fflush`는 프로세스 안의 C 라이브러리 버퍼를 커널로 넘길 뿐이고, 전원이 끊기면 커널 버퍼는 사라진다. `fsync`까지 해야 디스크에 닿는다. 느리지만 "잃으면 안 되는 것"이라 기본값이다.
- **콜백(함수 포인터) + `void *ctx`**: 재생기는 "레코드를 어떻게 반영할지" 모른다. 부르는 쪽이 함수와 자기 상태를 넘긴다.
- **길이 필드 불신**: len을 그대로 믿고 `fread`하면 버퍼 뒤를 덮어쓴다. 주석에 따르면 이 검사는 지워도 테스트·ASan으로 안 보이는 자리라서(변이 검사 J3) "확인할 수 없지만 지우면 안 되는 검사"로 남겼다.
- **함수 안의 `static` 큰 버퍼**: `journal_append`의 `rec`, `journal_replay`의 `whole`은 65KB가 넘어 스택에 두기 크므로 `static`으로 둔다. 대신 같은 함수를 동시에 두 곳에서 부르면 같은 버퍼를 공유하게 된다(이 모듈은 단일 스레드 사용을 전제로 한다).
- **`_POSIX_C_SOURCE`**: `-std=c11` 엄격 모드에서는 `fsync`, `fileno`가 안 보이므로 `core/CMakeLists.txt`에서 `_POSIX_C_SOURCE=200809L`을 정의한다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_journal.c`)

- 적은 레코드가 같은 순서·같은 내용으로 돌아온다; 빈 payload와 최대 payload.
- 빈 파일(레코드 0, 꼬리 안 찢어짐)과 남의 파일(magic 불일치)을 구분한다.
- **바이트 단위 절단 전수**: 레코드 3개짜리 파일을 0바이트부터 끝까지 모든 길이로 잘라 보고, 어디서 잘려도 온전한 앞 레코드만 재생되는지 확인한다.
- 터무니없는 len에도 넘치지 않고, 비트 하나 뒤집힘을 CRC가 잡는다; 콜백이 멈출 수 있다; 읽기 전용 핸들에 append 거절; 알려진 CRC32 값과 일치.

---

#### 9. snapshot — 스냅샷과 재기동 복구

##### 1) 한 줄 역할과 필요성

저널을 처음부터 끝까지 재생하면 시간이 오래 걸린다. 중간 상태를 파일로 떠 두고(스냅샷), 재기동 때 **스냅샷 + 그 이후 저널만** 재생해 시간을 줄인다. 진실은 여전히 저널이다 — 스냅샷이 깨지면 버리고 저널만으로 복구한다(느릴 뿐 틀리지 않는다).

##### 2) 읽는 순서

1. `core/include/snapshot.h`
2. `core/src/snapshot.c`
3. `core/tests/test_snapshot.c`

##### 3) 핵심 자료구조

파일 배치: `magic(4, "MSS1") len(4) upto_seq(8)` + 본문(len, 상한 1MB) + CRC32(4).

`upto_seq`는 "저널의 몇 번까지 반영한 상태인가"다. 예: 저널에 seq 1~10이 있고 5까지 반영한 상태를 스냅샷으로 떴다면 `upto_seq = 5`. 복구 때 seq 6~10만 반영한다. 이 값을 모르면 이미 반영한 주문을 두 번 반영(체결 두 배)하거나, 아예 재생을 안 해 이후 주문을 잃는다.

| 구조체 | 필드 |
|---|---|
| `snap_apply_fn` | `load(data, len, ctx)`: 스냅샷 본문을 상태에 넣는다. `apply(rec, ctx)`: 저널 레코드 하나를 반영한다. `ctx` |
| `recover_result_t` | `used_snapshot`, `snapshot_bad`(있었지만 못 씀), `from_seq`, `replayed`(반영 건수), `journal_torn` |

##### 4) 핵심 함수 흐름

- `snapshot_write(path, upto_seq, data, len)`:
  1. 같은 디렉터리에 `path.tmp`를 연다 (다른 파일 시스템이면 rename이 원자적이지 않다)
  2. 머리 + 본문 + CRC를 쓰고 `fflush` + `fsync`
  3. 실패하면 `.tmp`를 지우고 `ERR_IO`
  4. `rename(tmp, path)` — **여기서 원자성이 생긴다.** 이 줄 전에 죽으면 옛 스냅샷, 후에 죽으면 새 스냅샷이 온전히 있다
- `snapshot_read(path, &seq, out, cap)`: 없으면 `ERR_NOT_FOUND`; 머리 없음·magic 불일치·len 초과·본문/CRC 부족·CRC 불일치는 모두 `ERR_IO`; `cap` 부족은 `ERR_INVALID_ARG`. 성공하면 본문 길이. 정리는 `goto done`으로 한 곳에서 `fclose`한다.
- `recover(snapshot_path, journal_path, fns, &res)`:
  1. 스냅샷을 읽는다. 성공 → `load` 호출, `from_seq = upto`. 실패하되 "없음"이 아니면 `snapshot_bad = true`로 표시하고 계속
  2. 저널을 연다. 없으면 스냅샷만 썼을 때 `ERR_OK`, 아니면 `ERR_NOT_FOUND`
  3. `journal_replay`에 내부 콜백 `on_journal_rec`를 건다. 이 콜백은 `rec->seq <= from_seq`이면 **건너뛰고**, 아니면 `apply`를 부른다
  4. 반영 건수와 찢어짐 여부를 `res`에 담는다

##### 5) 이 코드가 쓰는 C 기법

- **원자적 파일 교체(tmp + fsync + rename)**: POSIX에서 같은 파일 시스템 안 `rename`은 원자적이다. 저널처럼 "찢어진 꼬리 버리기"가 통하지 않는(통째로 하나의 상태인) 파일을 안전하게 바꾸는 표준 기법이다. `fsync`를 빼면 "이름은 바뀌었는데 내용은 디스크에 없는" 빈 파일이 새 스냅샷이 될 수 있다.
- **`snprintf`로 경로 조립**: `sprintf` 금지 규약에 따라 버퍼 크기를 넘지 않게 쓰고, 잘렸는지 반환값으로 확인한다.
- **`goto done` 정리 패턴**: 여러 실패 지점에서 파일 닫기를 한 곳으로 모은다.
- **구조체에 함수 포인터 묶기**(`snap_apply_fn`): 복구 로직은 무엇을 되살리는지 몰라도 된다.
- **대입 대신 assert로 약속 적기**: `assert(upto == 0)` — "`snapshot_read`는 실패 시 `out_seq`를 건드리지 않는다"는 약속이 깨지면 여기서 터진다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_snapshot.c`)

- 스냅샷 왕복(본문과 `upto_seq`), 쓰기 후 `.tmp`가 남지 않는다.
- **이어 재생**: 스냅샷 이후 저널만 반영하고, 스냅샷이 끝까지 담았으면 재생할 것이 없다.
- 스냅샷이 없거나 깨졌으면 저널만으로 전부 복구하고 `snapshot_bad`를 알린다.
- rename 전에 죽은 모양(`.tmp`만 있음)이면 옛 스냅샷이 그대로 쓰인다; 잘린 스냅샷, CRC까지 맞춘 남의 파일(magic만 틀림), 터무니없는 len을 거절한다.

---

#### 10. recon — 정합성 대사

##### 1) 한 줄 역할과 필요성

증권사는 장 마감 후 "원장 숫자가 체결 기록으로 설명되는가"를 맞춰 본다(대사). 이 모듈은 (a) 논리 주문과 시장별로 쪼개진 물리 주문(다리)의 수량이 맞는지, (b) 계좌 예수금이 입출금·체결로 설명되는지를 검사하고, **찾은 어긋남을 전부** 보고한다.

##### 2) 읽는 순서

1. `core/include/recon.h` ("대사는 대상의 코드를 쓰지 않는다"는 원칙이 핵심)
2. `core/src/recon.c`
3. `core/tests/test_recon.c`

##### 3) 핵심 자료구조

| 구조체 | 필드와 뜻 |
|---|---|
| `recon_leg_t` | 물리 다리 하나: `phys_id`, `sent_qty`(보낸 수량), `filled_qty`, `canceled_qty`, `notional`(체결 금액), `live`(아직 살아 있나) |
| `recon_order_t` | 논리 주문 하나: `logical_id`, `order_qty`, `legs[8]`, `leg_count` |
| `recon_account_t` | 계좌 하나: `cash_start`, `deposits`, `withdrawals`, `buy_notional`, `sell_notional`(체결·입출금이 남긴 값), `cash_now`, `reserved_now`(원장이 들고 있는 값), `open_reserved`(미체결 주문이 묶어야 할 금액) |
| `recon_finding_t` | 어긋남 한 건: `kind`, `logical_id`, `phys_id`, `account_no`, `expected`, `actual` |
| `recon_report_t` | 결과 담는 곳: `at`(호출자가 준 배열), `cap`, `count`, `dropped`(자리 부족으로 못 담은 건수) |

어긋남 종류(`recon_kind_t`): `RECON_LEG_SUM`, `RECON_LEG_OVERFILL`, `RECON_LIVE_NO_REMAIN`, `RECON_NOTIONAL`, `RECON_DUP_PHYS`, `RECON_CASH`, `RECON_RESERVED`, `RECON_RESERVED_OVER_CASH`.

예 (깨끗한 경우): 100주 매수 논리 주문을 KRX 60주(전량 70,000원 체결, 금액 4,200,000), NXT 40주(10주 70,000원 체결 → 금액 700,000, 30주 취소, `live=false`)로 나눴다면 다리 합 100 = 주문 100, 각 다리 체결+취소 ≤ 보낸 수량, 금액과 체결 여부가 일치 → 어긋남 0건. 만약 NXT 다리가 `live=true`로 남아 있으면 체결+취소(40)가 보낸 수량(40)에 이미 닿았으므로 `RECON_LIVE_NO_REMAIN`이다.

##### 4) 핵심 함수 흐름

- `recon_order(o, r)`: 다리마다
  1. `sent_sum`(64비트)에 보낸 수량을 더한다
  2. 체결+취소 > 보낸 수량 → `LEG_OVERFILL`
  3. `live`인데 체결+취소 ≥ 보낸 수량 → `LIVE_NO_REMAIN` (끝났다는 통보를 놓쳤다)
  4. (체결 0) ≠ (금액 0) → `NOTIONAL`
  5. 앞 다리들과 `phys_id`가 겹치면 → `DUP_PHYS` (다리 최대 8개라 이중 루프로 충분)
  6. 끝으로 `sent_sum != order_qty` → `LEG_SUM`
- `recon_account(a, r)`:
  1. `expect_cash = cash_start + deposits - withdrawals - buy_notional + sell_notional`가 `cash_now`와 다르면 `CASH`
  2. `open_reserved != reserved_now` → `RESERVED`
  3. `reserved_now > cash_now` → `RESERVED_OVER_CASH` (없는 돈으로 주문을 받았다)
- 내부 `add()`: 자리가 없으면 `dropped++`만 하고 **검사는 계속** 돈다.
- `recon_clean(r)`: `count == 0 && dropped == 0`일 때만 true.

어긋남을 찾는 것은 이 함수의 **정상 업무**라서 `ERR_OK`를 돌려준다. 에러 코드는 인자가 잘못됐을 때만이다.

##### 5) 이 코드가 쓰는 C 기법

- **독립 재계산**: 대사는 `sor`나 `ledger`의 조회 함수를 부르지 않는다. 대상 코드가 틀리면 대사도 같이 틀려 "일치"라고 답하기 때문이다. 호출자가 숫자를 이 모듈의 구조체로 옮겨 적어 넘긴다. 부수 효과로 `core`가 `sor`·`ledger`를 링크하지 않는다.
- **오버플로 방지 64비트 합**: `qty_t`(32비트) 여덟 개를 더하면 넘칠 수 있어 `int64_t`로 센다.
- **호출자 제공 버퍼**: 결과 배열을 모듈이 할당하지 않고 호출자가 준다(메모리 소유권이 명확하다).
- **`snprintf(dst, size, "%.*s", n, src)`**: 원본을 최대 n글자만 읽고 널 종료를 보장한다. `strncpy`는 꽉 찬 경우 널이 빠지고, `-O2`에서 `-Wstringop-truncation` 경고(=`-Werror`라 빌드 실패)를 낸다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_recon.c`)

- 맞는 주문·맞는 계좌는 깨끗하다.
- 여덟 가지 어긋남 종류를 각각 만들어 정확한 종류·expected·actual이 담기는지 확인한다.
- 여러 어긋남이 **한꺼번에** 보고되고, 자리가 모자라면 `dropped`가 실제 못 담은 건수와 같으며 `recon_clean`이 false다.
- 다리 0개, 큰 수량 합이 넘치지 않는 경우, NULL·범위 밖 `leg_count` 인자.
- 숫자는 100·200 같은 딱 떨어지는 값을 피해 더하기/빼기를 뒤바꾼 구현이 우연히 통과하지 못하게 골랐다.

---

#### 11. kv_config — `key = value` 설정 파서

##### 1) 한 줄 역할과 필요성

실험 시나리오(시드, 기준가 등)나 최선집행 가중치를 텍스트 파일에서 읽는다. 두 곳에 따로 있던 파서를 하나로 모은 것이다. **모르는 키는 거절한다** — 오타가 조용히 무시되면 그 설정으로 돌린 실험 결과를 나중에 해석할 수 없다.

##### 2) 읽는 순서

1. `core/include/kv_config.h`
2. `core/src/kv_config.c`
3. `core/tests/test_kv_config.c`
4. 실제 사용처: `exchange/src/liquidity/divergent.c`의 `divergent_load()`

##### 3) 핵심 자료구조

```c
typedef int (*kv_parse_fn)(const char *val, void *field);
typedef struct { const char *key; kv_parse_fn parse; void *field; } kv_entry_t;
```

예: 파일에 `seed = 2026  # 실험 1`이 있고 표에 `{"seed", kv_parse_u64, &cfg.seed}`가 있으면 `cfg.seed`에 2026이 들어간다.

##### 4) 핵심 함수 흐름

`kv_config_load(path, table, count)`:
1. 파일이 없으면 `ERR_NOT_FOUND`
2. 한 줄씩 `fgets` (줄 최대 512바이트)
3. `#` 뒤를 잘라 주석 제거, 앞뒤 공백·줄바꿈 제거(`kv_trim`), 빈 줄은 건너뛴다
4. `=`가 없거나 키·값이 비면 `ERR_INVALID_ARG`
5. 표에서 키를 선형 탐색, 없으면 `ERR_INVALID_ARG`
6. 그 항목의 `parse`를 부르고 실패하면 그 코드를 그대로 돌려준다
7. 첫 실패에서 멈추므로 필드 일부만 바뀌어 있을 수 있다 → 호출자는 **지역 사본에 읽고 성공했을 때만** 결과를 내보낸다(`divergent_load`가 그렇게 한다)

해석기: `kv_parse_i64`는 `strtoll` 후 "숫자가 하나도 없음(`end == val`)", "뒤에 글자가 남음(`"10000원"`)", `errno == ERANGE`를 거절한다. `kv_parse_i32`는 i64로 읽고 32비트 범위를 확인한다. `kv_parse_u64`는 `-`로 시작하면 거절한다(`strtoull`은 음수를 조용히 큰 양수로 뒤집는다).

##### 5) 이 코드가 쓰는 C 기법

- **표 주도(table-driven) 파싱**: 키마다 if를 쓰지 않고 "키 이름, 해석 함수, 써 넣을 주소" 표 하나로 처리한다.
- **`void *field`와 캐스팅**: 해석 함수가 `*(int64_t *)field = v;`처럼 자기 타입으로 바꿔 쓴다. 표가 서로 다른 타입의 필드를 한 배열에 담을 수 있게 된다.
- **`strtoll` + `end` 포인터 + `errno`**: C에서 문자열→정수를 안전하게 하는 표준 방법(`atoi`는 실패를 알릴 수 없다).
- **문자열을 제자리에서 자르기**: `*hash = '\0'`로 주석 시작점에 널을 박아 문자열을 끊는다.

##### 6) 테스트가 보장하는 것 (`core/tests/test_kv_config.c`)

- 표에 따라 필드가 채워지고, 파일에 없는 키의 필드는 기본값이 남는다.
- 해석기가 돌려준 실패 코드가 그대로 올라온다.
- 정수 범위를 넘으면 잘라 넣지 않고 거절한다.
- (주석·빈 줄·모르는 키·값 끝 글자 같은 형식 규칙은 `exchange/tests/test_divergent.c` 등 사용처 테스트가 확인한다.)

---

#### 12. 장애 주입 테스트 — test_fault_inject.c

##### 1) 한 줄 역할과 필요성

저널·스냅샷·복구가 "쓰다 죽어도 괜찮다"는 주장을 **실제로 프로세스를 죽여서** 확인한다. 손으로 자른 파일은 내가 상상한 모양만 시험하지만, 실제로 죽이면 상상하지 못한 시점도 만들어진다.

##### 2) 읽는 순서

8번(journal), 9번(snapshot), 10번(recon)을 읽은 뒤 `core/tests/test_fault_inject.c`. 맨 위 주석의 "이 테스트가 시험하지 **못하는** 것"을 꼭 읽는다.

##### 3) 핵심 자료구조

```c
typedef struct {
    volatile int32_t durable; /* fsync까지 끝난 레코드 수 */
    volatile int32_t rounds;  /* 스냅샷을 몇 번 다 썼는가 */
} shared_t;
```

`fork` 전에 `mmap(MAP_SHARED | MAP_ANONYMOUS)`로 잡아 부모와 자식이 같은 메모리를 본다. 자식이 죽어도 이 값은 남는다.

##### 4) 핵심 함수 흐름

| 테스트 | 흐름 |
|---|---|
| `test_fsynced_records_survive_sigkill` | 자식이 레코드를 적을 때마다 `durable++` → 부모가 몇 건 쌓이면 `SIGKILL` → 재생해 보면 최소 `durable`건은 반드시 읽힌다 |
| `test_torn_tail_from_real_kill` | 큰 레코드를 쉬지 않고 적게 해 `fwrite` 한가운데서 죽는 상황을 만든다 → 약속한 것은 하나도 잃지 않고, 약속하지 않은 것은 지어내지 않는다 |
| `test_snapshot_never_half_written` | 옛 스냅샷을 두고, 자식이 크기를 바꿔 가며 스냅샷을 계속 덮어쓰게 한 뒤 40번 서로 다른 시점에 죽인다 → 매번 원래 자리는 옛것이든 새것이든 온전해야 한다 |
| `test_recovery_after_sigkill` | 자식이 저널에 적고, 중간에 스냅샷을 남기고, 계속 적다 죽는다 → 부모가 `recover()`로 되살린 상태를 자식이 남긴 기록과 맞추고, 대사(`recon`)에도 건다 |

##### 5) 이 코드가 쓰는 C/시스템 기법

- **`fork` / `kill(pid, SIGKILL)` / `waitpid`**: 자식 프로세스를 만들고, 잡을 수도 막을 수도 없는 신호로 죽이고, 죽었음(`WIFSIGNALED`, `WTERMSIG == SIGKILL`)을 확인한다.
- **공유 메모리(`mmap MAP_SHARED`)** 와 **`volatile`**: 다른 프로세스가 바꾸는 값이라 컴파일러가 최적화로 캐시하지 않게 한다.
- **상한 있는 대기(`WAIT_UNTIL`)**: 1ms씩 최대 3,000번 기다린다. 매달린 테스트는 실패보다 나쁘다.
- **`do { ... } while (0)` 매크로**: 여러 문장짜리 매크로를 `if` 안에서도 한 문장처럼 쓰게 하는 C 관용구(`STEP`, `WAIT_UNTIL`).
- **변이 검사(mutation testing)로 확인한 한계**: `SIGKILL`은 커널 페이지 캐시를 살려 두므로 **`fsync`를 빼도 이 테스트는 통과한다**(전원 차단은 흉내 낼 수 없다). 반면 `fflush`를 빼면 잡힌다(프로세스 안 버퍼는 함께 사라지므로). 저널 CRC 검사를 빼도 통과한다 — 죽은 쓰기는 "짧은" 레코드를 남기지 "온전하되 틀린" 레코드를 남기지 않기 때문이다.

##### 6) 테스트가 보장하는 것

- `fsync`가 끝난 레코드는 프로세스가 죽어도 사라지지 않는다.
- 실제 강제 종료로 생긴 찢어진 꼬리에서도 앞 레코드는 온전하고 지어낸 레코드가 없다.
- 스냅샷 파일은 어느 순간에 죽어도 반쪽 상태로 남지 않는다.
- 스냅샷 + 저널 복구 결과가 죽기 직전 기록과 일치하고 대사가 깨끗하다.

---

#### 13. price_level — 한 가격의 시간 우선 대기열

##### 1) 한 줄 역할과 필요성

같은 가격(예: 70,000원 매도)에 걸린 주문들을 **먼저 온 순서대로** 줄 세운다. 거래소의 "같은 가격이면 먼저 접수된 주문이 우선"(시간 우선)을 표현하는 가장 작은 부품이다.

##### 2) 읽는 순서

1. `exchange/include/price_level.h`
2. `exchange/src/book/price_level.c`
3. `exchange/tests/test_price_level.c`

##### 3) 핵심 자료구조

```c
typedef struct price_level {
    order_t *head;        /* 가장 먼저 접수된 주문 */
    order_t *tail;        /* 가장 나중에 접수된 주문 */
    qty_t    total_qty;   /* 소속 주문 잔량의 합 (불변조건) */
    int32_t  order_count;
} price_level_t;
```

모든 필드가 0이면 빈 레벨이다. 호가창이 레벨 배열을 `calloc`(0으로 채워 할당)으로 잡으므로 초기화 함수가 없다.

예: 70,000원 매도에 A(30주) → B(20주) → C(50주)가 순서대로 들어오면

```
head → A(30) ⇄ B(20) ⇄ C(50) ← tail     total_qty = 100, order_count = 3
```

##### 4) 핵심 함수 흐름

| 함수 | 동작 |
|---|---|
| `level_push_back(level, order)` | 잔량 0 이하면 `ERR_INVALID_QTY`, 합계가 `INT32_MAX`를 넘을 상황이면 `ERR_BOOK_FULL`. tail 뒤에 붙이고 `total_qty += 잔량`, `order_count++` |
| `level_pop_front(level)` | head를 떼어 돌려준다(내부적으로 `level_remove`) |
| `level_remove(level, order)` | 앞 주문의 next와 뒤 주문의 prev를 서로 이어 붙여 O(1)에 뗀다. head/tail 갱신, 합계·개수 감소, 여러 불변조건 `assert` |
| `level_reduce_qty(level, order, qty)` | **부분 체결 전용**: `0 < qty < 잔량`만 받는다. `filled_qty += qty`, `total_qty -= qty`. 전량 체결은 호출부가 `remove`로 명시한다 |
| `level_amend_qty(level, order, new_qty)` | **수량 감소 정정 전용**: `filled_qty < new_qty < qty`만 받는다. `total_qty -= (qty - new_qty)`, `qty = new_qty`. 위치(우선순위)는 그대로 |

예: 위 레벨에서 B를 취소하면 A.next = C, C.prev = A가 되고 total_qty = 80. A가 10주 부분 체결되면 A.filled_qty = 10, total_qty = 70.

##### 5) 이 코드가 쓰는 C 기법

- **침투형(intrusive) 이중 연결 리스트**: 링크(`prev`, `next`)를 별도 노드가 아니라 `order_t` 안에 직접 둔다. 그래서 (a) 리스트 노드를 따로 할당할 필요가 없고, (b) 주문 포인터만 있으면 리스트 중간에서 O(1)로 뗄 수 있다. 대가는 "한 주문은 한 번에 한 리스트에만" 들어갈 수 있다는 것(`push_back`이 `prev == NULL && next == NULL`을 assert한다).
- **이중 연결인 이유**: 취소·정정이 줄 중간의 주문을 지목해 들어오기 때문이다. 단일 연결이면 앞 주문을 찾느라 O(n)이 된다.
- **O(1) 소속 검사 `assert_linked`**: 리스트를 훑지 않고 "내 앞 주문의 next가 나인가, 앞이 없으면 head가 나인가"만 본다. 싸서 핫 패스에 남겨도 된다.
- **캐시된 합계와 불변조건**: `total_qty`를 매번 다시 세지 않고 연산마다 갱신하며, "합계 == 소속 잔량의 합"을 유지한다.
- **assert vs 에러**: 이 레벨에 없는 주문을 떼려는 것은 내부 버그(assert). 부분 체결 수량이 잘못된 것은 상태를 안 바꾸고 거절할 수 있으므로 에러 코드.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_price_level.c`)

- 넣은 순서대로 나온다(FIFO).
- 중간·처음·끝 주문을 떼어도 링크·head/tail·합계·개수가 맞다 — 매 연산 뒤 리스트를 실제로 훑어 캐시 값과 대조하는 `check_invariant`로 확인한다.
- 부분 체결 반영 후 합계가 맞고 원 수량(`qty`)은 보존된다.
- 잔량 0 주문, 전량과 같은 부분 체결 수량 등을 거절한다.

---

#### 14. order_book — 호가창

##### 1) 한 줄 역할과 필요성

한 종목·한 시장의 매수 호가와 매도 호가를 가격별로 정리한 표다. "지금 가장 싸게 파는 가격(최우선매도호가)은?", "70,100원에 매도 잔량이 얼마인가?"에 빠르게 답한다. HTS 화면의 호가창과 같은 것이다.

##### 2) 읽는 순서

1. `exchange/include/order_book.h`
2. `exchange/src/book/order_book.c` — `compute_band` → `build_segments` → `price_to_index`/`index_to_price` → `book_insert`/`book_remove` → `recompute_best` → 조회 함수 순서
3. `exchange/tests/test_order_book.c`

##### 3) 핵심 자료구조

```c
typedef struct { price_t low; price_t tick; int32_t base_idx; int32_t count; } book_segment_t;
struct order_book {
    price_t base_price, low, high;
    book_segment_t seg[8]; int32_t seg_count; int32_t level_count;
    price_level_t *levels[2]; /* [SIDE_BUY], [SIDE_SELL] */
    price_t best[2];          /* 최우선호가 캐시, 없으면 BOOK_PRICE_NONE(0) */
};
```

핵심 아이디어: 기준가 ±30%가 받을 수 있는 가격의 전부이므로, 그 구간의 **유효 호가마다 배열 칸 하나**를 만들고 가격을 배열 인덱스로 바꾼다.

예: 기준가 70,000원 (`test_order_book.c`의 `test_create`와 같은 값)

1. 제한폭: 70,000 × 70/100 = 49,000, 70,000 × 130/100 = 91,000
2. 호가 단위에 맞춘다: 하한은 올림, 상한은 내림 → `low = 49,000`, `high = 91,000`
3. 구간으로 쪼갠다:

| 구간 | low | tick | base_idx | count |
|---|---|---|---|---|
| seg[0] | 49,000 | 50 | 0 | 20 (49,000 ~ 49,950) |
| seg[1] | 50,000 | 100 | 20 | 411 (50,000 ~ 91,000) |

4. `level_count = 431`. 매수·매도 각각 `price_level_t` 431칸을 `calloc`
5. 70,000원의 인덱스 = 20 + (70,000 − 50,000) / 100 = **220**

원 단위로 펼쳤다면 42,001칸이 필요했지만, 유효 호가만 칸을 차지하므로 431칸이다.

`level_view_t {price, total_qty, order_count}`는 N단 호가 조회 결과 한 줄이다.

##### 4) 핵심 함수 흐름

- `book_create(base)`: 기준가 범위 검사 → `compute_band`(제한폭을 호가 단위로 정렬, 7,778원처럼 경계가 어긋나면 하한 5,450 올림·상한 10,110 내림) → `build_segments` → 레벨 배열 두 개 할당. 하나라도 실패하면 `book_destroy` 후 NULL.
- `price_to_index(book, price)`: 범위 밖이면 -1; 구간을 훑어 해당 구간에서 `(price - low) % tick != 0`이면 -1(호가 단위 불일치), 아니면 `base_idx + (price - low) / tick`.
- `book_insert(book, order)`:
  1. 제한폭 밖 → `ERR_PRICE_LIMIT`; 인덱스 -1 → `ERR_INVALID_TICK`
  2. 그 레벨에 `level_push_back`
  3. 새 가격이 캐시된 최우선보다 좋으면(매수는 더 높으면, 매도는 더 낮으면) 캐시만 바꾼다 — 비교 한 번
- `book_remove(book, order)`: 레벨에서 떼고, **그 레벨이 비었고 그게 최우선 가격이었을 때만** `recompute_best`로 다음 최우선을 찾는다.
- `recompute_best(book, side)`: 매수는 배열 끝(높은 가격)부터, 매도는 처음(낮은 가격)부터 첫 비어 있지 않은 레벨을 찾는다. 선형 스캔이며 `ponytail:` 주석에 "병목이 되면 비트맵으로 64칸씩 건너뛴다"고 적혀 있다.
- `book_best_bid/ask`: 캐시 값을 돌려준다 O(1).
- `book_front(book, side, price)`: 그 레벨의 맨 앞(가장 오래된) 주문. 매칭 엔진이 상대 주문을 소진하는 통로.
- `book_reduce_qty` / `book_amend_qty`: 레벨 함수로 위임.
- `book_qty_at(book, side, price)`: 그 가격의 잔량 합계.
- `book_qty_up_to(book, side, limit, want)`: 최우선부터 `limit` 가격까지 잔량을 더하다가 `want`에 닿으면 즉시 `want`를 돌려준다. 호가창을 바꾸지 않는다 — FOK가 "전량 체결 가능한가"를 미리 묻는 데 쓴다.
- `book_snapshot(book, side, depth, out)`: 최우선부터 빈 레벨을 건너뛰며 `depth`단까지 채우고 채운 줄 수를 돌려준다.

##### 5) 이 코드가 쓰는 C 기법

- **가격 → 배열 인덱스 직접 매핑(구간별 폭이 다른 버킷)**: 트리나 해시 없이 배열 인덱스 계산으로 레벨을 찾는다. 가격 범위가 제한폭으로 유한하기 때문에 가능하다.
- **최우선호가 캐시**: 삽입 때는 비교 한 번, 삭제 때는 최우선 레벨이 비었을 때만 스캔한다.
- **`levels[2]`처럼 enum 값을 배열 인덱스로 쓰기**: `SIDE_BUY=0`, `SIDE_SELL=1`이라 `book->levels[order->side]`로 바로 고른다.
- **센티넬 값**: `BOOK_PRICE_NONE = 0`. 0원은 어떤 구간에서도 유효 호가가 아니라 "없음"으로 쓸 수 있다.
- **정수 곱셈 오버플로를 컴파일 타임에 배제**: `base * 130 / 100`이 넘지 않음을 `types.h`의 `_Static_assert`가 보장한다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_order_book.c`)

- 생성 거부(0, 음수, `PRICE_MAX` 초과)와 제한폭 계산(70,000 → 49,000~91,000; 7,778 → 5,450~10,110; `PRICE_MAX`·1원 경계).
- 가격 ↔ 인덱스가 일대일이다 — 1원·5원 구간을 함께 걸치는 기준가 2,500으로 전 가격을 훑는다.
- 빈 호가창 조회, 삽입 거부(제한폭 밖, 호가 단위 불일치).
- 매 삽입·제거 뒤 캐시된 최우선호가를 전수 조사로 구한 값과 대조한다; 10단 스냅샷이 올바른 순서와 값이다.

---

#### 15. event — 주문 생애주기 이벤트

##### 1) 한 줄 역할과 필요성

매칭 결과를 "주문을 낸 쪽"뿐 아니라 **호가창에 있던 상대 주문의 주인**에게도 알리기 위한 이벤트 스트림이다. 실제 거래소가 체결 통보를 양쪽에 보내는 것과 같다. 이벤트 순서가 결정적이어야 두 실행을 바이트 단위로 비교할 수 있다.

##### 2) 읽는 순서

1. `exchange/include/event.h` (한 호출 안의 이벤트 순서 규칙이 주석에 있다)
2. `exchange/src/event.c`
3. 매칭 엔진을 읽은 뒤 `exchange/tests/test_event.c`

##### 3) 핵심 자료구조

`event_type_t`: `EVENT_ACCEPTED`(잔량이 호가창에 등록), `EVENT_EXECUTED`(전량 체결), `EVENT_PARTIALLY_EXECUTED`, `EVENT_CANCELED`, `EVENT_REJECTED`, `EVENT_MODIFIED`.

`order_event_t`

| 필드 | 뜻 |
|---|---|
| `type`, `ts`, `order_id`, `market` | 종류, 논리 시각, 이벤트 주인, 시장 |
| `price`, `qty` | 체결이면 체결가·체결 수량, 접수·정정이면 주문 가격과 그 시점 수량 |
| `remaining_qty` | 이 이벤트 직후 잔량 |
| `counterparty_id` | 체결 상대. 체결이 아니면 0 |
| `reason` | REJECTED일 때 에러 코드 |

`event_sink_t {fn, ctx}`: 이벤트를 받을 함수와 그 함수가 쓸 상태.

한 호출 안의 순서 규칙:
1. 접수 거부: `REJECTED` 하나로 끝
2. 체결마다: **상대(maker) 이벤트 → 들어온 쪽(taker) 이벤트**
3. 잔량이 등록되면 `ACCEPTED`
4. 시장가·IOC 잔량이 취소되면 `CANCELED`

##### 4) 핵심 함수 흐름

- `event_emit(sink, ev)`: 싱크가 없거나 `fn == NULL`이면 아무것도 안 하고, 아니면 `sink->fn(ev, sink->ctx)`.
- `event_type_str(type)`: X 매크로 switch. 모르는 값은 `"알 수 없는 이벤트"`.

콜백 안에서 엔진을 다시 호출하면 안 된다(재진입은 이벤트 순서를 흐트러뜨린다).

##### 5) 이 코드가 쓰는 C 기법

- **콜백 + 컨텍스트 포인터**(`void (*fn)(const order_event_t *, void *ctx)`): C에서 "나중에 불러 줄 함수"를 넘기는 표준 방식. 엔진은 소비자가 무엇을 하는지 모른다.
- **X 매크로**(`EVENT_TYPE_LIST`): 종류와 이름을 한 목록에서.
- **switch에 default를 두지 않고 함수 끝에서 반환**: 열거형 값을 빠뜨리면 컴파일러 경고(`-Wall`의 `-Wswitch`)가 나게 하는 방식이다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_event.c`)

- 시나리오마다 기대 이벤트 시퀀스를 표로 적고 종류·주문번호·가격·수량·잔량을 한 건씩 대조한다: 접수, 전량 체결 쌍, taker 부분 체결, maker 부분 체결, 여러 레벨에 걸친 순서.
- 시장가·IOC 잔량이 `CANCELED`로 나온다; 거부는 `REJECTED` 한 건.
- 취소·정정 이벤트; 싱크가 없으면 아무 일도 없다; 같은 입력은 같은 시퀀스.

---

#### 16. match — 매칭 엔진

##### 1) 한 줄 역할과 필요성

들어온 주문을 호가창의 반대편 주문과 맞붙여 체결시키고, 남은 수량은 호가창에 올리거나 취소한다. **거래소의 심장**이다. 체결 규칙은 SPEC 4.1 그대로다: 가격 우선 → 시간 우선, 체결가는 **먼저 호가창에 있던 주문(maker)의 가격**.

##### 2) 읽는 순서

1. `exchange/include/match.h` — 공개 API와 각 함수의 약속
2. `exchange/src/match/match_internal.h` — 엔진 구조체와 내부 공용 함수
3. `exchange/src/match/match_engine.c` — 생성·검증·**`match_sweep`(핵심)**·`match_rest`·관문
4. `exchange/src/match/match_limit.c` — 지정가(가장 기본 흐름)
5. `exchange/src/match/match_market.c` — 시장가
6. `exchange/src/match/match_ioc_fok.c` — IOC/FOK
7. `exchange/src/match/match_cancel_modify.c` — 취소/정정
8. 테스트: `test_match_limit.c` → `test_match_market.c` → `test_match_ioc_fok.c` → `test_match_cancel_modify.c` → `test_event.c`

##### 3) 핵심 자료구조

```c
struct match_engine {
    order_book_t *book;  order_pool_t *pool;  order_index_t *index;
    int32_t capacity;
    event_sink_t sink;
    const market_rules_t *rules; /* NULL이면 세션 검사를 하지 않는다 */
};
```

엔진 하나 = 한 종목·한 시장. 엔진이 호가창·주문 풀·주문 인덱스를 **모두 소유**한다. 들어오는 주문(`const order_t *req`)은 읽기만 하고, 잔량이 호가창에 남을 때만 풀에서 슬롯을 빌려 복사한다. 그래서 "전량 체결된 상대 주문의 슬롯을 누가 돌려주나"가 명확하다(엔진이 돌려준다).

`fill_t`: 체결 한 건 — `price`(maker 호가), `qty`, `maker_id`, `taker_id`, `ts`(taker의 시각).

`exec_result_t`: 한 호출의 결과

| 필드 | 뜻 |
|---|---|
| `fills[64]`, `fill_count`, `truncated` | 체결 목록. 64건을 넘으면 목록만 잘리고 `truncated = true` |
| `filled_qty`, `notional` | 총 체결 수량, 총 체결 금액. **목록이 잘려도 정확**. 평균 단가 = `notional / filled_qty` |
| `remaining_qty` | 남은 수량 (취소면 취소된 잔량) |
| `resting` | 잔량이 호가창에 등록됐는가 |
| `status` | NEW / PARTIAL / FILLED / CANCELED / REJECTED |

##### 4) 핵심 함수 흐름

###### 공통 부품 (`match_engine.c`)

- `match_engine_create(base_price, capacity)`: 호가창·풀(capacity)·인덱스(capacity)를 만든다.
- `match_set_sink`, `match_set_rules`: 이벤트 소비자와 시장 규칙을 건다. 규칙이 NULL이면 시간·유형 검사 없이 순수 매칭만 한다(규칙 무관 테스트용).
- `match_validate(eng, req)`: 주문번호 0, 잘못된 side, 수량 범위 밖 또는 `filled_qty != 0`, **이미 살아 있는 같은 주문번호**(`ERR_DUPLICATE`)를 거른다.
- `match_gate_submit(eng, ts, type)`: 규칙이 있으면 `is_open` + `can_submit` 실패 → `ERR_MARKET_CLOSED`, `is_order_type_allowed` 실패 → `ERR_NOT_SUPPORTED`.
- `match_gate_cancel(eng, ts)`: `can_cancel`만 본다(휴장 중 취소 허용 여부).
- `match_resolve_price`, `match_allows_reprice`: 규칙 테이블에 위임(규칙 없으면 주문 가격 그대로, 정정 허용).
- `match_reject(...)`: `status = REJECTED`, `REJECTED` 이벤트 발행, 받은 에러 코드 반환. 각 파일의 `REJECT(code)` 매크로가 이것을 부른다.

###### `match_sweep(eng, taker, limit, out)` — 상대 호가 소진 (모든 신규 주문의 공통 핵심)

```
remaining = taker.qty
반복 (remaining > 0 동안):
  1. best = 반대편 최우선호가 (매수면 best_ask, 매도면 best_bid)
  2. best가 없거나, limit을 넘으면(매수: best > limit / 매도: best < limit) 멈춤
     (limit == BOOK_PRICE_NONE이면 가격 제한 없음 = 시장가)
  3. maker = book_front(반대편, best)     ← 시간 우선: 그 가격의 맨 앞
  4. fill_qty = min(remaining, maker 잔량)
  5. maker 잔량을 다 먹었으면: book_remove → index_remove → 풀에 반납
     아니면: book_reduce_qty(maker, fill_qty)
  6. record_fill: filled_qty += fill_qty, notional += best × fill_qty, 목록에 추가
  7. remaining -= fill_qty
  8. 이벤트: maker(EXECUTED 또는 PARTIALLY_EXECUTED) → taker(같은 판정)
남은 remaining을 돌려준다
```

maker 슬롯은 5단계에서 풀로 돌아갈 수 있으므로, 이벤트에 쓸 `maker_id`, `maker_market`을 **미리 복사**해 둔다.

###### `match_limit` — 지정가 (`match_limit.c`)

1. `match_result_init(out, qty)`
2. `match_gate_submit` 실패 → 거부
3. `eff = *req; eff.price = match_resolve_price(...)` — 지정가는 그대로, 중간가는 여기서 가격이 정해진다. `BOOK_PRICE_NONE`이면 `ERR_INVALID_PRICE`
4. 가격이 호가창 범위 밖 → `ERR_PRICE_LIMIT`, 호가 단위 불일치 → `ERR_INVALID_TICK` (**호가창을 건드리기 전에** 검사)
5. `match_validate`
6. `remaining = match_sweep(eng, req, req->price, out)` — 지정가가 곧 limit
7. 잔량이 있으면 `match_rest`로 등록 → `ACCEPTED` 이벤트, `status = 체결 있으면 PARTIAL, 없으면 NEW`. 등록 실패(풀 고갈 등)면 이미 일어난 체결은 되돌리지 않고 잔량만 `REJECTED`
8. 잔량이 없으면 `status = FILLED`

`match_rest`: 풀에서 슬롯을 빌려 요청 내용을 복사하고 `filled_qty = qty - remaining`(원 수량 보존), `book_insert` → `index_put`. 중간에 실패하면 한 일을 되돌리고 슬롯을 반납한다.

**구체 예**: KRX 호가창(기준가 70,000)에 매도 A 70,100원×30주, B 70,100원×20주(A보다 늦게), C 70,200원×50주가 있다. 매수 지정가 주문 T: 70,200원×80주가 들어온다.

| 단계 | best ask | maker | 체결 | T 잔량 | 이벤트 |
|---|---|---|---|---|---|
| 1 | 70,100 | A(30) | 30주 @70,100, A 제거 | 50 | A EXECUTED(잔 0) → T PARTIALLY(잔 50) |
| 2 | 70,100 | B(20) | 20주 @70,100, B 제거, 레벨 비어 최우선 재계산 | 30 | B EXECUTED → T PARTIALLY(잔 30) |
| 3 | 70,200 | C(50) | 30주 @70,200, C 부분 체결 | 0 | C PARTIALLY(잔 20) → T EXECUTED(잔 0) |

결과: `filled_qty = 80`, `notional = 30×70,100 + 20×70,100 + 30×70,200 = 5,611,000`, 평균 단가 70,137.5원, `status = FILLED`, `resting = false`. T의 지정가가 70,200원이어도 앞 50주는 **maker 가격인 70,100원**에 체결됐다.

같은 호가창에 T가 70,100원×80주였다면: 50주 체결 후 best ask 70,200 > 70,100이라 멈추고, 30주가 70,100원 매수 호가로 등록된다(`ACCEPTED`, `status = PARTIAL`, `resting = true`).

###### `match_market` — 시장가 (`match_market.c`)

1. 관문 → `match_validate` (**가격 검증 없음**, `req->price`를 보지 않는다)
2. 반대 호가가 전혀 없으면 `ERR_NO_LIQUIDITY`로 거부(아무것도 안 함)
3. `match_sweep(..., BOOK_PRICE_NONE, ...)` — 가격 제한 없이 소진
4. 잔량은 **등록하지 않는다**. 남으면 `CANCELED` 이벤트, `status = PARTIAL`; 다 채웠으면 `FILLED`

###### `match_ioc` / `match_fok` (`match_ioc_fok.c`)

- IOC: 관문 → 지정가와 같은 가격 검증(`check_price`) → validate → `match_sweep(limit = 지정가)` → 한 건도 체결 못 했으면 `ERR_NO_LIQUIDITY` 거부, 잔량 있으면 `CANCELED` 이벤트. 호가창에 남지 않는다.
- FOK: 관문 → 가격 검증 → validate → **먼저 센다**: `book_qty_up_to(반대편, 지정가, 주문 수량)`가 주문 수량보다 작으면 호가창을 전혀 건드리지 않고 `ERR_NO_LIQUIDITY`. 충분하면 sweep하고 `assert(remaining == 0)`.
  - "부분 체결 후 되돌리기"를 쓰지 않는 이유: 뗀 상대 주문을 다시 넣으면 그 주문들의 시간 우선순위가 복원되지 않는다.

###### `match_cancel(eng, id, ts, out)` (`match_cancel_modify.c`)

1. `match_gate_cancel` 실패 → `ERR_MARKET_CLOSED` (이벤트 없음)
2. `index_get`으로 찾고, 없으면 `ERR_NOT_FOUND` (이벤트 없음). **전량 체결된 주문은 이미 인덱스에서 빠졌으므로** 별도 상태 플래그 없이 자연히 NOT_FOUND다
3. 잔량·가격·시장을 미리 복사 → `book_remove` → `index_remove` → 풀 반납
4. `remaining_qty = 취소된 잔량`, `status = CANCELED`, `CANCELED` 이벤트

###### `match_modify(eng, id, new_price, new_qty, ts, out)`

`new_qty`는 **원 주문 수량**이다(잔량이 아니다). 예: 100주 주문이 30주 체결된 뒤 `new_qty = 80`이면 잔량은 50주가 된다.

1. `match_gate_submit(ts, ORDER_LIMIT)` — 정정은 신규와 같은 관문(휴장 중 불가)
2. 주문을 찾는다. 없으면 `ERR_NOT_FOUND`
3. 가격을 바꾸는데 규칙이 재가격을 금지하면(중간가) `ERR_NOT_SUPPORTED`
4. 새 가격의 제한폭·호가 단위 검사; `new_qty <= filled_qty` 또는 `> QTY_MAX`면 `ERR_INVALID_QTY` — **모든 검사를 호가창에서 떼기 전에** 한다
5. 분기:
   - 같은 가격 + 수량 감소 → `book_amend_qty`로 **제자리에서** 줄인다 (우선순위 유지)
   - 같은 가격 + 같은 수량 → 아무것도 안 바꾸고 `MODIFIED` 이벤트 (괜히 떼면 우선순위만 잃는다)
   - 가격 변경 또는 수량 증가 → 새 가격이 반대편 최우선과 **교차하면 `ERR_INVALID_PRICE`**(정정 시 매칭하지 않는 단순화). 아니면 `book_remove` → 가격·수량 변경 → `book_insert` (**줄 맨 뒤로** = 우선순위 상실)
6. `MODIFIED` 이벤트, `status = 체결 있으면 PARTIAL 아니면 NEW`

##### 5) 이 코드가 쓰는 C 기법

- **소유권 명시**: 엔진이 모든 슬롯을 소유하고, 요청은 `const order_t *`(읽기 전용 "틀")로 받는다. 호출부는 풀이나 호가창을 직접 건드리지 않는다.
- **값 복사로 가격 덮어쓰기**: `order_t eff = *req; eff.price = ...; req = &eff;` — 원본을 바꾸지 않고 지역 사본을 만들어 이후 코드가 같은 이름(`req`)으로 쓰게 한다.
- **`memset`으로 패딩까지 0**: `match_emit`의 `order_event_t`와 `match_sweep`의 `fill_t`를 채우기 전에 `memset(..., 0, ...)`한다. 구조체 필드 사이 패딩 바이트는 값이 정해져 있지 않아서, 이벤트를 바이트 단위로 비교하는 결정성 검증이 깨질 수 있기 때문이다.
- **파일별 로컬 매크로 `REJECT(code)`**: 반복되는 거부 코드를 줄인다. 매크로가 `req`, `out`, `eng`라는 이름에 기대므로 그 이름이 보이는 파일 안에서만 정의한다.
- **내부 헤더 분리**: `match_internal.h`는 `src/match/` 안에서만 쓴다. CMake에서 `PRIVATE src/match`로 include 경로를 걸어 외부 모듈은 볼 수 없다.
- **거부 시 무변경 원칙**: 검사를 전부 앞에 두어 "반쯤 체결되고 나서 실패"하는 상태를 만들지 않는다.
- **집계는 정확, 목록은 잘릴 수 있음**: `EXEC_FILLS_MAX = 64` 고정 배열. `ponytail:` 주석으로 한계를 적었다.
- **`int64_t` 체결 금액**: `(int64_t)price * (int64_t)qty` — 32비트끼리 곱하면 넘칠 수 있어 먼저 64비트로 바꾼다.
- **`(void)rc;`**: NDEBUG 빌드에서 `assert(rc == ERR_OK)`가 사라지면 "쓰지 않는 변수" 경고(`-Werror`)가 나므로 명시적으로 무시한다.
- **시스템 시각 금지**: 모든 `ts`는 주문이나 취소 요청이 들고 온 값이다.

##### 6) 테스트가 보장하는 것

- `test_match_limit.c`: 교차 없으면 등록만; 전량 체결; **체결가는 maker 가격**; 부분 체결 후 잔량 등록; maker 부분 체결; 여러 레벨 평균 단가를 손계산과 대조; 지정가에서 멈춤; 시간 우선; 매도 방향; 각종 거부; 64건 초과 시 목록만 잘리고 집계는 정확.
- `test_match_market.c`: 빈 호가창 전량 거부; 지정가라면 멈췄을 레벨까지 먹는다; 잔량은 남지 않고 취소; 다중 레벨 평균 단가; 매도 방향.
- `test_match_ioc_fok.c`: IOC 부분·전량·무체결; FOK 성공·정확히 딱 맞는 수량; **FOK 실패 전후로 각 레벨 잔량과 맨 앞 주문번호까지 같다**(호가창 무변경); 빈 호가창; `book_qty_up_to` 동작.
- `test_match_cancel_modify.c`: 취소·부분 체결 주문 취소·체결 완료 주문은 NOT_FOUND; 수량 감소는 우선순위 유지, 수량 증가·가격 변경은 상실(큐 순서를 읽고 **실제로 체결을 흘려 누가 먼저 체결되는지**까지 확인); 무변경 정정; 부분 체결 주문 정정; 교차 정정 거절; 취소 후 같은 슬롯 재사용.

---

#### 17. market_rules / krx / nxt — 시장별 세션 규칙

##### 1) 한 줄 역할과 필요성

KRX와 NXT의 **거래 시간표, 구간별 허용 주문 유형, 가격 결정 방식**을 함수 테이블로 표현한다. 매칭 엔진은 `if (market == KRX)` 같은 분기를 하나도 갖지 않고 이 테이블만 부른다. 시장이 셋이 되어도 테이블 하나만 추가하면 된다.

##### 2) 읽는 순서

1. `docs/SPEC.md` 2장(거래 시간), 4.2(주문 유형)
2. `docs/decisions/0002-시장-규칙-추상화.md`
3. `exchange/include/market_rules.h`
4. `exchange/src/rules/market_rules.c` (세션 이름표)
5. `exchange/src/rules/krx.c` → `exchange/src/rules/nxt.c`
6. `exchange/tests/test_market_rules.c` → `test_krx_rules.c` → `test_nxt_rules.c`

##### 3) 핵심 자료구조

`market_session_t`: `SESSION_CLOSED`, `SESSION_PRE`, `SESSION_PRE_BREAK`, `SESSION_REGULAR`, `SESSION_POST_BREAK`, `SESSION_AFTER`. KRX는 CLOSED/REGULAR만, NXT는 여섯 값을 모두 쓴다.

`market_rules_t` (함수 포인터 테이블)

| 멤버 | 뜻 |
|---|---|
| `name` | "KRX" / "NXT" |
| `is_open(ts, &session)` | 거래 가능한가. **닫혀 있어도 구간은 채운다** (휴장 = 닫혔지만 취소는 됨) |
| `is_order_type_allowed(session, type)` | 이 구간에서 이 유형을 받는가 |
| `can_submit(session)` | 신규 주문을 받는가 |
| `can_cancel(session)` | 취소를 받는가 |
| `resolve_price(book, req)` | 실제로 쓸 가격 (지정가는 그대로, 중간가는 계산) |
| `allows_reprice(type)` | 정정으로 가격을 바꿀 수 있는가 |

시각 해석: `ts`는 나노초이고, 세션 판정은 **그날 자정 기준 경과 시간**으로 한다.

```c
#define TOD_NS(h, m, s) ((((int64_t)(h) * 3600) + ((int64_t)(m) * 60) + (int64_t)(s)) * NS_PER_SEC)
static inline int64_t ts_time_of_day(ts_t ts)
{
    return (((int64_t)ts % NS_PER_DAY) + NS_PER_DAY) % NS_PER_DAY;
}
```

예: `TOD_NS(9, 0, 30)` = 32,430 × 10⁹. `ts`가 며칠치 나노초여도 하루로 접어서 본다. `% NS_PER_DAY`를 두 번 하는 것은 음수 `ts`에서도 결과가 0 이상이 되게 하려는 것이다.

NXT 구간 표 (`nxt.c`의 `NXT_SESSIONS[]`, from 이상 to 미만)

| from | to | session | open |
|---|---|---|---|
| 08:00:00 | 08:50:00 | PRE | true |
| 08:50:00 | 09:00:30 | PRE_BREAK | false |
| 09:00:30 | 15:20:00 | REGULAR | true |
| 15:20:00 | 15:30:00 | POST_BREAK | false |
| 15:30:00 | 20:00:00 | AFTER | true |

##### 4) 핵심 함수 흐름

KRX (`krx.c`)
- `krx_is_open`: `09:00:00 ≤ tod < 15:30:00`이면 REGULAR/true, 아니면 CLOSED/false.
- `krx_type_allowed`: REGULAR에서 중간가를 **제외한** 모든 유형.
- `krx_can_submit`, `krx_can_cancel`: REGULAR일 때만 (휴장이 없으니 장 밖이면 취소도 불가).
- `krx_resolve_price`: 주문 가격 그대로. `krx_allows_reprice`: 항상 true.

NXT (`nxt.c`)
- `nxt_is_open`: 표를 순서대로 훑어 구간과 open을 돌려준다. 표에 없으면 CLOSED.
- `nxt_type_allowed`: REGULAR는 전부(중간가 포함), PRE/AFTER는 LIMIT만, 나머지는 false.
- `nxt_can_submit`: PRE, REGULAR, AFTER.
- `nxt_can_cancel`: CLOSED가 아니면 true — **휴장 중에도 취소 가능**.
- `nxt_resolve_price`, `nxt_allows_reprice`: 18번(중간가)에서 설명한다.

엔진 쪽 사용 (`match_engine.c`)
- 신규·정정: `is_open` false 또는 `can_submit` false → `ERR_MARKET_CLOSED`; 유형 불가 → `ERR_NOT_SUPPORTED`.
- 취소: `is_open`의 반환값은 무시하고 채워진 session으로 `can_cancel`만 본다.

예:
- 08:55 NXT에 지정가 신규 → PRE_BREAK, open=false → `ERR_MARKET_CLOSED`. 같은 시각 취소 → 성공.
- 08:30 NXT에 시장가 → PRE, 지정가만 허용 → `ERR_NOT_SUPPORTED`.
- 09:00:10 → KRX는 열려 있고 NXT는 아직 PRE_BREAK.
- 15:30:00 정각 KRX → 이미 마감. NXT는 AFTER 시작.

가격 제한폭은 여기서 검사하지 않는다. 호가창이 만들어질 때 이미 받을 수 있는 가격 구간이 정해지므로, 같은 검사를 두 곳에 두지 않는다(`krx.c` 주석).

##### 5) 이 코드가 쓰는 C 기법

- **함수 포인터 테이블(C식 인터페이스/가상 함수 표)**: 구조체 멤버에 함수 주소를 담아 `eng->rules->is_open(ts, &session)`처럼 부른다. 객체지향 언어의 인터페이스와 같은 역할이다.
- **지정 초기화자(designated initializer)**: `const market_rules_t KRX_RULES = { .name = "KRX", .is_open = krx_is_open, ... };` — 멤버 이름을 적어 초기화하므로 순서 실수가 없다.
- **`extern const` 전역 상수**: 테이블은 상수라 "전역 가변 상태 금지" 규약에 걸리지 않는다. 규칙 함수는 상태가 없어 같은 인자에 항상 같은 답이다(결정성).
- **`static` 함수**: `krx_is_open` 등은 파일 밖에서 이름으로 부를 수 없고 테이블로만 접근한다.
- **데이터 표 + 순서 훑기**: NXT 경계를 if 사슬 대신 표로 적어, 경계 하나를 고칠 때 앞뒤 조건을 같이 볼 필요가 없다.
- **출력 파라미터**: `bool is_open(ts_t, market_session_t *out)` — 반환값 하나 외에 구간을 포인터로 함께 돌려준다.
- **`(void)book;`**: 쓰지 않는 인자 경고를 막는 관용구.

##### 6) 테스트가 보장하는 것

- `test_market_rules.c`: 규칙 내용이 아니라 **엔진이 규칙을 실제로 거쳐 가는가**를 본다. 호출 횟수를 세는 더미 규칙으로 규칙 없음(예전과 동일), 열림, 닫힘(유형 검사까지 가지 않음), 휴장(신규 불가·취소 가능), 유형 불가, 규칙이 정한 가격이 실제로 쓰임, 가격을 못 정하면 거부를 확인한다.
- `test_krx_rules.c`: 09:00·15:30 경계마다 (직전 1초, 직전 1나노초, 정각, 직후); 정규장 유형(중간가 불가); 신규·취소; 재가격 허용; 엔진을 통한 확인.
- `test_nxt_rules.c`: 다섯 구간 안쪽과 네 경계(특히 09:00:00과 09:00:29는 아직 휴장); 구간별 유형; 휴장 중 신규 불가·취소 가능; 중간가만 재가격 불가; **KRX와 NXT가 동시에 열려 있지 않은 구간이 실제로 있다**; 엔진을 통한 확인.

---

#### 18. 중간가 주문 (NXT 전용)

##### 1) 한 줄 역할과 필요성

수량만 정하고 가격은 "매수·매도 최우선호가의 한가운데"로 자동 결정되는 NXT 전용 주문이다. 스프레드 중간에서 체결되므로 양쪽 모두 호가를 건너는 비용을 줄인다. SPEC은 세 가지를 "직접 정하라"고 남겼고, 코드는 다음처럼 정했다(`docs/decisions/0003-중간가-주문.md`).

| SPEC의 열린 질문 | 코드의 결정 | 이유 |
|---|---|---|
| 호가 단위로 안 떨어질 때 | **항상 내림** | 매수는 내림·매도는 올림처럼 나누면 두 중간가 주문이 서로 다른 가격을 받아 영영 만나지 못한다 |
| 재계산 시점 | **접수 시점 고정** | 지속 갱신은 호가가 바뀔 때마다 주문을 다른 레벨로 옮겨야 하고, 그 이동이 또 호가를 바꾸는 연쇄가 생긴다 |
| 한쪽 호가가 비었을 때 | **거부** (`ERR_INVALID_PRICE`) | 중간값이 정의되지 않는다 |

##### 2) 읽는 순서

1. `exchange/src/rules/nxt.c`의 `nxt_resolve_price`, `nxt_allows_reprice`
2. `exchange/src/match/match_limit.c` (중간가도 `match_limit`으로 들어온다)
3. `exchange/src/match/match_cancel_modify.c`의 재가격 검사
4. `exchange/tests/test_midpoint.c`

##### 3) 핵심 자료구조

별도 구조체는 없다. `order_t.type = ORDER_MIDPOINT`로 표시하고 `price`는 무시된다(0을 넣어도 된다). 가격이 정해진 뒤에는 보통 지정가 주문처럼 호가창에 들어간다.

예: NXT(기준가 10,000원) 호가창에 매수 최우선 9,980원, 매도 최우선 10,020원이 있을 때 중간가 매수 50주를 내면 가격은 (9,980 + 10,020) / 2 = 10,000원이 되어 **10,000원 매수 호가로 등록**된다. 새 최우선매수호가가 10,000원이 된다.

##### 4) 핵심 함수 흐름

`nxt_resolve_price(book, req)`:
1. 중간가가 아니면 `req->price` 그대로
2. `bid = book_best_bid`, `ask = book_best_ask`. 하나라도 없으면 `BOOK_PRICE_NONE`
3. `mid = (bid + ask) / 2` — 둘 다 양수라 정수 나눗셈의 버림이 곧 내림
4. `round_to_tick(mid, false)` (호가 단위로 내림)

`match_limit`에서 이 가격이 `eff.price`가 되고, 이후 흐름은 지정가와 같다(제한폭·호가 단위 검사 → sweep → 잔량 등록).

사례별 결과 (`test_midpoint.c`)

| 호가 상태 | 계산 | 결과 |
|---|---|---|
| 9,980 / 10,020 | 10,000 | 10,000원에 등록 |
| 10,000 / 10,010 (한 틱 스프레드) | 10,005 → 10원 단위 내림 10,000 | 기존 매수 호가와 같은 10,000원에 뒤로 줄 선다 |
| 1,000 / 1,003 (1원 구간) | 2,003 / 2 = 1,001 | 1,001원 |
| 매수만 있음 | — | `ERR_INVALID_PRICE` |

두 중간가가 만나는 예: 10,000 / 10,010 상태에서 중간가 매수 40주 → 10,000원에 등록(잔량 합 140). 이어서 중간가 매도 40주 → 같은 10,000원을 받는다. 매도의 limit 10,000 이상인 매수 호가가 있으므로 sweep이 체결하는데, **시간 우선에 따라 먼저 있던 지정가 매수 100주에서 40주가 체결**된다.

정정: `match_modify`는 가격이 바뀌는 정정이면 `nxt_allows_reprice(ORDER_MIDPOINT) == false`라 `ERR_NOT_SUPPORTED`. 같은 가격으로 수량만 줄이는 정정은 허용된다. 세션: NXT 메인마켓 밖이면 프리·애프터는 `ERR_NOT_SUPPORTED`(지정가만 허용), 휴장·폐장은 `ERR_MARKET_CLOSED`다. KRX는 정규장 중에도 중간가를 `ERR_NOT_SUPPORTED`로 거부한다(장 밖이면 관문에서 먼저 `ERR_MARKET_CLOSED`).

##### 5) 이 코드가 쓰는 C 기법

- **전략을 데이터(함수 포인터)로 주입**: 엔진은 "중간가면 이렇게" 분기가 없다. `resolve_price`와 `allows_reprice` 두 훅이 차이를 흡수한다.
- **정수 나눗셈의 버림**: C에서 양수끼리 `/`는 소수점 아래를 버린다. 음수가 없다는 전제를 주석으로 명시한다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_midpoint.c`)

- 딱 떨어지는 중간값, 호가 단위로 안 떨어져 내림되는 중간값, 홀수 합의 절단.
- 한쪽이나 양쪽 호가가 비면 거부, 호가가 생기면 성공.
- 매수 중간가와 매도 중간가가 같은 가격을 받아 서로(시간 우선에 맞게) 체결된다.
- 등록 후 호가가 바뀌어도 가격이 움직이지 않는다(접수 시점 고정); 메인마켓 밖에서는 거부; 정정으로 가격 변경 불가·수량 정정 가능; 규칙 함수를 직접 불러도 같은 계산.

---

#### 19. synthetic — 가상 참가자 (시드 난수 주문 생성기)

##### 1) 한 줄 역할과 필요성

SOR 전략을 비교하려면 양 시장 호가창에 주문이 차 있어야 한다. 손으로 넣은 호가는 다양하지 않으므로, **확률 분포에서 주문을 뽑아** 호가창을 채운다. 같은 시드는 항상 같은 주문 시퀀스를 만들어서, 전략 A와 B가 **똑같은 시장**에서 비교되게 한다.

##### 2) 읽는 순서

1. `exchange/include/synthetic.h`
2. `exchange/src/liquidity/synthetic.c`
3. `exchange/tests/test_synthetic.c`

##### 3) 핵심 자료구조

`synth_config_t`

| 필드 | 뜻 | 예 |
|---|---|---|
| `seed` | 난수 시드. 0이면 생성 실패 | 2026 |
| `arrival_per_sec` | 초당 평균 주문 수(포아송 도착) | 100.0 |
| `price_decay` | 기준가에서 떨어지는 틱 수의 지수분포 비율. 클수록 기준가 근처에 몰린다 | 0.5 |
| `qty_min`, `qty_max` | 수량 균등분포 범위 | 10, 500 |
| `ref_price` | 기준가(이격의 중심) | 10,000 |
| `price_low`, `price_high` | 생성 가격 범위 | 7,000, 13,000 |
| `market` | 시장 | `MARKET_KRX` |
| `start_ts` | 첫 시각 기준점 | `TOD_NS(9, 1, 0)` |
| `first_id` | 주문번호 시작값(0이면 실패) | 1 |

내부 `synth_gen_t`: `cfg`(설정 사본), `state`(난수 상태 uint64 하나), `next_id`, `now`(현재 논리 시각), `count`.

##### 4) 핵심 함수 흐름

`synth_next(gen, out)` — **뽑는 순서 자체가 재현성 계약**이다.

1. 도착 간격: `gap_sec = -log(u) / arrival_per_sec` (지수분포). 나노초로 바꾸고 최소 1ns를 보장해 `now += gap_ns` (같은 시각에 두 주문이 겹치지 않게)
2. 매수/매도: 난수의 최하위 비트
3. 가격 이격: `offset = -log(u) / price_decay` 틱. `price_at_offset`에서 정수 틱 수로 자르고, 매수는 기준가 **아래**, 매도는 **위**에 놓는다 → 스프레드가 생긴다. 범위로 자르고, 다른 호가 단위 구간으로 넘어갔으면 매수는 내림·매도는 올림으로 맞춘다
4. 수량: `qty_min + (난수 % 범위폭)`
5. `order_t`를 채운다: `id = next_id++`, `ts = now`, `type = ORDER_LIMIT`, `market = cfg.market`

예: 기준가 10,000원(10원 단위), `offset = 3.7`이면 3틱 → 매수 9,970원 / 매도 10,030원.

##### 5) 이 코드가 쓰는 C 기법

- **xorshift64\* 난수 생성기**: 상태 `uint64_t` 하나를 시프트·XOR로 섞고 상수를 곱한다. 주기 2⁶⁴−1.

```c
static uint64_t next_u64(synth_gen_t *gen)
{
    uint64_t x = gen->state;
    x ^= x >> 12;  x ^= x << 25;  x ^= x >> 27;
    gen->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}
```

- **왜 `rand()`/`time()`을 쓰지 않는가**: `rand()`는 프로세스 **전역 상태** 하나를 공유해서, KRX 생성기와 NXT 생성기가 서로의 수열을 건드리고 호출 순서가 결과를 바꾼다. `time()`은 실행할 때마다 값이 달라 같은 입력이 같은 출력을 만들지 않는다. 생성기마다 자기 상태를 들고, 시각도 스스로 전진시키면 이 문제가 사라진다(CLAUDE.md 결정성 규칙).
- **균등 난수 → (0,1) 실수**: 상위 53비트(`double` 가수부 폭)만 써서 `(bits + 0.5) / 2⁵³`로 0과 1이 절대 나오지 않게 한다(`log(0)` 방지).
- **역변환 샘플링**: `-log(u) / rate`는 균등 난수를 지수분포로 바꾸는 공식이다. 지수분포 간격으로 도착하면 포아송 과정이 된다.
- **`!(x > 0.0)`로 NaN까지 거르기**: NaN은 모든 비교가 거짓이라 `x <= 0.0`으로는 안 걸리지만 `!(x > 0.0)`으로는 걸린다.
- **복합 리터럴** `*out = (order_t){0};`: 구조체 전체를 0으로 한 번에 초기화한다.
- `libm` 링크: `log()` 때문에 `exchange/CMakeLists.txt`에서 `m`을 링크한다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_synthetic.c`)

- 잘못된 설정(시드 0, 도착률 0 이하, 수량·가격 범위 역전 등)은 생성 실패.
- **같은 시드 → 바이트 단위로 같은 수열**, 다른 시드 → 다른 수열.
- 생성된 주문이 유효하다(범위 안, 호가 단위 일치, 번호 증가, 시각 단조 증가); 4,990원처럼 호가 단위 구간 경계 근처 기준가에서도 정렬이 맞다.
- 극단적으로 높은 도착률에서도 시각이 엄격히 증가; decay가 크면 기준가에 더 몰린다; 도착률이 높으면 같은 건수를 더 짧은 시간에 만든다; 만든 주문을 실제 엔진에 넣어도 동작한다.

---

#### 20. divergent — 두 시장에 의도적으로 다른 유동성 넣기

##### 1) 한 줄 역할과 필요성

두 시장에 같은 분포로 호가를 채우면 어떤 SOR 전략을 써도 결과가 같아 측정할 것이 없다. 그래서 "KRX가 얇다", "NXT가 얇다", "한쪽이 가격상 유리하다" 같은 **시나리오 프리셋**으로 시장별 생성기 설정 한 쌍을 만든다.

##### 2) 읽는 순서

1. `exchange/include/divergent.h`
2. `exchange/src/liquidity/divergent.c` (11번 kv_config를 먼저 읽어 두면 `divergent_load`가 쉽다)
3. `exchange/tests/test_divergent.c`

##### 3) 핵심 자료구조

`scenario_t`: `SCENARIO_BALANCED`(대조군), `SCENARIO_KRX_THIN`, `SCENARIO_NXT_THIN`, `SCENARIO_CROSSED`.

프리셋 표(`PRESETS[]`) — 시장 하나를 네 숫자로 요약한다.

| 시나리오 | KRX (arrival, decay, qty, shift) | NXT (arrival, decay, qty, shift) |
|---|---|---|
| BALANCED | 100, 0.5, 10~500, 0 | 100, 0.5, 10~500, 0 |
| KRX_THIN | **20, 0.12, 10~60**, 0 | 100, 0.5, 10~500, 0 |
| NXT_THIN | 100, 0.5, 10~500, 0 | **20, 0.12, 10~60**, 0 |
| CROSSED | 100, 1.5, 10~500, 0 | 100, 1.5, 10~500, **+20틱** |

"얇다"를 도착률 하나로만 표현하지 않는다. 주문 수가 적어도 각 주문이 크면 체결 단가에 불리하지 않으므로, 도착률·가격 퍼짐·주문 크기 세 축을 함께 낮춘다.

`divergent_config_t`: `scenario`, `seed`, `ref_price`, `price_low`, `price_high`, `start_ts`, `orders_per_market`.

내부 `struct divergent`: `cfg`, `gen[2]`(시장별 생성기), `ref[2]`(시장별 기준가).

##### 4) 핵심 함수 흐름

`divergent_create(cfg)`: 시장마다
1. `derive_seed(base, market)`: 기본 시드에 `0x9E3779B97F4A7C15 × (market + 1)`을 더하고 splitmix64로 섞는다. 결과가 0이면 1 (같은 시드를 그대로 쓰면 두 시장이 똑같은 주문을 낸다)
2. `shifted_ref`: 프리셋의 `ref_shift_ticks`만큼 기준가를 틱 단위로 옮긴다. CROSSED에서 기준가 10,000원이면 NXT 기준가는 10,000 + 20×10 = 10,200원
3. 주문번호 공간을 나눈다: KRX는 1부터, NXT는 1,000,000,001부터 (통합 로그에서 구분)
4. `synth_create`. 하나라도 실패하면 전부 정리하고 NULL

- `divergent_gen(div, market)`: 시장별 생성기(소유권은 `divergent_t`).
- `divergent_ref_price(div, market)`: 시장별 기준가. 호출자는 이 값으로 `match_engine_create`를 한다.
- `divergent_load(path, &cfg)`: `kv_config_load`에 키 7개짜리 표를 넘긴다. `scenario` 키는 `parse_scenario`가 이름("CROSSED" 등)을 열거형으로 바꾼다. **지역 변수 `cfg`에 읽고 성공했을 때만 `*out = cfg`**.

설정 파일 예:

```
# 실험 3
scenario = KRX_THIN
seed = 2026
ref_price = 10000
price_low = 7000
price_high = 13000
start_ts = 32460000000000
orders_per_market = 50000
```

##### 5) 이 코드가 쓰는 C 기법

- **인덱스 지정 배열 초기화**: `[SCENARIO_KRX_THIN] = {...}`처럼 열거형 값을 배열 첨자로 써서 표를 만든다. 열거형 순서를 바꿔도 표가 어긋나지 않는다.
- **시드 파생(splitmix64)**: 인접한 기본 시드(1, 2, 3…)도 서로 멀리 떨어진 시장별 시드가 된다.
- **부분 실패 정리**: 두 번째 생성기에서 실패해도 `divergent_destroy`가 NULL 생성기를 안전하게 건너뛰며 정리한다(`synth_destroy(NULL)`은 `free(NULL)`이라 무해).
- **소유권 주석**: "divergent_t가 소유하므로 따로 파괴하지 않는다"를 헤더에 적는다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_divergent.c`)

각 시나리오로 호가창을 채운 뒤 **깊이**(최우선 근처 10단 잔량 합)와 **스프레드**를 잰다.

- BALANCED: 두 시장의 깊이·쓸어 담는 비용이 같은 자릿수이고, 두 시장 가격 중심의 어긋남이 CROSSED의 절반보다 작다.
- KRX_THIN / NXT_THIN: 얇은 쪽이 깊이가 얕고 스프레드가 넓으며 쓸어 담으면 더 비싸다.
- CROSSED: NXT 매수호가가 KRX 매도호가보다 높다(KRX에서 사서 NXT에 팔면 이득 — SOR이 잡아야 할 장면).
- 같은 시드는 재현되고 다른 시드는 달라진다; 두 시장이 같은 주문을 내지 않는다; 생성 인자 검증; 시나리오 이름 왕복; 설정 파일 읽기.

---

#### 21. feed_source — 호가창에서 시세 피드 만들기

##### 1) 한 줄 역할과 필요성

7번에서 본 피드 **형식**(`core/feed.h`)에 실제 호가창 값을 채운다. 형식은 `core`에, 생성기는 `exchange`에 둔다 — 피드를 **읽는** 전략 엔진은 `core`만 링크하면 되고 호가창 내부 구조를 몰라도 된다. **만드는** 쪽만 호가창(`book_snapshot`)을 알아야 한다.

##### 2) 읽는 순서

1. `exchange/include/feed_source.h`
2. `exchange/src/feed_source.c`
3. `exchange/tests/test_feed_source.c`

##### 3) 핵심 자료구조

새 구조체는 없다. 입력은 `match_engine_t`와 `fill_t`, 출력은 `feed_book_t`와 `feed_trade_t`다.

예: 매수 70,000×100, 69,900×50, 69,800×30 / 매도 70,100×40, 70,200×60인 호가창에서 depth 5로 만들면 `bid[0..2]`에 세 단, `bid[3..4]`는 0; `ask[0..1]`에 두 단, `ask[2..4]`는 0.

##### 4) 핵심 함수 흐름

- `feed_book_from_engine(eng, symbol, market, depth, out)`:
  1. 인자·depth(1~10)·market 검사
  2. **`memset(out, 0, ...)`을 먼저** — 없는 단이 자동으로 0이 된다
  3. `snprintf(out->symbol, ..., "%.*s", 8, symbol)`
  4. `book_snapshot(SIDE_BUY, depth)` → `bid[]`에 가격·잔량 복사, 매도도 같게
- `feed_trade_from_fill(fill, symbol, market, taker_side, out)`: 체결가·수량을 옮기고 `side = taker_side`, `exec_id = fill->taker_id`. 한 주문이 여러 레벨을 가로지르면 같은 `exec_id`가 여러 번 나가는데, 주석은 "같은 주문이 만든 체결"이라는 정보이며 구분은 머리의 seq로 한다고 설명한다.

시각과 시퀀스 번호는 받지 않는다. 그것은 보내는 쪽의 상태라 머리(`feed_hdr_t`)에 들어간다.

##### 5) 이 코드가 쓰는 C 기법

- **모듈 경계로 의존성 끊기**: "형식은 core, 생성은 exchange"로 나눠 읽는 쪽이 매칭 엔진 라이브러리에 매이지 않게 한다.
- **먼저 0으로 밀고 채우기**: 빠뜨릴 자리를 구조적으로 없앤다.
- **스택 위 고정 배열** `level_view_t view[FEED_DEPTH_MAX]`: 최대 10줄이라 할당 없이 지역 배열로 충분하다.

##### 6) 테스트가 보장하는 것 (`exchange/tests/test_feed_source.c`)

- 실제 호가창의 값(가격·잔량·순서)이 그대로 실린다; 빈 호가창이면 전부 0.
- 만든 BOOK이 곧바로 `feed_encode_book`으로 인코딩된다.
- **같은 호가창은 같은 바이트**를 만든다.
- 체결 한 건이 taker 방향으로 실린다; 잘못된 인자 거절.

---

#### 22. 결정성 검증 — test_determinism.c

##### 1) 한 줄 역할과 필요성

이 프로젝트의 결론은 "전략 A와 B의 평균 체결 단가가 이만큼 달랐다"이다. 그 문장이 성립하려면 **같은 입력에서 항상 같은 출력**이 나와야 한다. 이 테스트는 유동성 생성 → 두 시장 엔진 → 이벤트 스트림 전체를 두 번 돌려 **바이트 단위로** 비교한다.

##### 2) 읽는 순서

15~20번을 모두 읽은 뒤 `exchange/tests/test_determinism.c`.

##### 3) 핵심 자료구조

- `evlog_t`: 첫 실행은 이벤트를 `realloc`으로 늘려 가며 저장하고, 두 번째 실행(`replay`)은 저장하지 않고 들어오는 즉시 첫 실행의 같은 번째 이벤트와 `memcmp`한다. 첫 불일치 위치를 `mismatch_at`에 남긴다.
- `run_stat_t`: 두 시장 합산 `notional`, `filled`, `rejected`.

##### 4) 핵심 함수 흐름

`run_once(seed, scenario, log)`:
1. `divergent_create`(기준가 10,000원, 가격 7,000~13,000, 시장당 5만 건)
2. 시장마다 `match_engine_create(divergent_ref_price(...))`, `KRX_RULES`/`NXT_RULES`, 같은 이벤트 싱크를 건다. 시작 시각은 09:01:00(`TOD_NS(9, 1, 0)`)으로, 두 시장이 모두 열린 구간이다(NXT 메인이 09:00:30부터). 엔진 용량은 65,536
3. 5만 번 반복하며 KRX, NXT 순서로 한 건씩 `synth_next` → `match_limit`
4. 64건마다 최근 등록 주문을 정정(수량 5로)하거나, 128건마다 취소한다
5. 합산 통계를 돌려준다

테스트:
- `test_identical_runs`: 같은 시드로 두 번 → 이벤트 10만 건 이상, 첫 불일치 없음, 건수·금액·수량·거부 수 동일
- `test_different_seed_differs`: 시드가 다르면 달라야 한다 (아니면 위 비교가 "아무것도 안 하는 코드"도 통과시킨다)
- `test_scenario_changes_outcome`: 시나리오가 다르면 체결 수량이 달라진다
- `main`: 전체가 25초 안에 끝나야 한다(CI에 넣을 수 있어야 한다는 완료 조건)

##### 5) 이 코드가 쓰는 C 기법

- **`memcmp`로 구조체 비교**: 16번에서 본 `memset` 패딩 정리가 이 비교를 가능하게 한다.
- **`realloc`으로 두 배씩 늘리는 동적 배열**: 테스트 코드라 핫 패스 규약과 무관하다.
- **테스트에서만 `clock()`**: 실행 시간 상한을 재는 데만 쓴다. 엔진 안에서는 시각을 읽지 않는다.

##### 6) 테스트가 보장하는 것

- 10만 건 규모에서 같은 시드·같은 시나리오 → 이벤트 스트림이 바이트 단위로 같다.
- 시드나 시나리오를 바꾸면 결과가 실제로 달라진다.
- 전체 검증이 25초 안에 끝난다.

---

#### 부록 A. C/시스템 기법 색인

| 기법 | 쉽게 말하면 | 어디에 |
|---|---|---|
| typedef, 고정 폭 정수 | 타입에 뜻있는 이름을 붙이고 크기를 고정 | `core/include/types.h` |
| 구조체·포인터·`->` | 여러 값을 묶고, 그 묶음의 주소로 다룬다 | 전부. 특히 `order.h` |
| 불투명 타입 | 헤더에는 이름만, 내용은 .c에만 | `order_pool_t`, `order_index_t`, `order_book_t`, `match_engine_t`, `journal_t`, `synth_gen_t`, `divergent_t` |
| `_Static_assert` | 컴파일 때 조건 검사, 틀리면 빌드 실패 | `types.h`, `test_types.c` |
| X 매크로 | 목록 하나를 여러 모양으로 펼친다 | `errors.h`/`errors.c`, `msg.h`/`msg.c`, `event.c`, `market_rules.c` |
| 사전 할당 메모리 풀 + 프리리스트 | 미리 만들어 두고 빌려 쓰기 | `core/src/order_pool.c` |
| 침투형 이중 연결 리스트 | 링크를 데이터 안에 넣어 O(1) 제거 | `order.h`의 `prev/next`, `exchange/src/book/price_level.c` |
| 오픈 어드레싱 해시 + 역방향 시프트 삭제 | 배열 하나짜리 해시 테이블 | `core/src/order_index.c` |
| splitmix64 섞기 | 연속 번호·시드를 고르게 흩뜨림 | `order_index.c`의 `hash_id`, `divergent.c`의 `derive_seed` |
| 가격 → 배열 인덱스 | 유효 호가마다 칸 하나 | `exchange/src/book/order_book.c` |
| 최우선호가 캐시 | 매번 찾지 않고 기억 | `order_book.c`의 `best[]` |
| 함수 포인터 테이블 | C식 인터페이스 | `market_rules_t` (`krx.c`, `nxt.c`) |
| 콜백 + `void *ctx` | 나중에 불러 줄 함수와 그 상태 | `event_sink_t`, `journal_fn`, `snap_apply_fn`, `kv_parse_fn` |
| 지정 초기화자 | 멤버·첨자 이름으로 초기화 | `KRX_RULES`, `NXT_RULES`, `divergent.c`의 `PRESETS[]` |
| `memset`으로 패딩 0 | 바이트 비교가 가능하게 | `order_pool_acquire`, `match_emit`, `match_sweep`, `wire_decode_header`, 모든 `msg_decode_*` |
| 빅엔디언 직렬화 | 큰 자리부터 바이트로 | `core/src/wire.c`, `msg.c`, `feed.c`, `journal.c`, `snapshot.c` |
| 길이 선행 + magic + version | 메시지 경계와 판 구분 | `wire.h`, `feed.h`, 저널·스냅샷 머리 |
| 정확한 길이 대조 디코딩 | 짧아도 길어도 거절 | `msg.c`의 `dec_check`, `feed_decode_*` |
| 시퀀스 갭 감지 | 빠진 메시지 알아채기 | `core/src/feed.c`의 `feed_sub_accept` |
| CRC32 | 데이터 변조·반쪽 쓰기 검출 | `journal.c`의 `journal_crc32`, `snapshot.c` |
| append-only 저널 + fsync | 덧붙이기만, 매번 디스크까지 | `core/src/journal.c` |
| 찢어진 꼬리 복구 | 반쪽 레코드는 버리고 앞까지만 | `journal_replay` |
| tmp + fsync + rename | 파일을 원자적으로 교체 | `core/src/snapshot.c`의 `snapshot_write` |
| `goto` 정리 | 실패 경로를 한 곳에서 닫기 | `snapshot_read` |
| 독립 재계산 대사 | 대상 코드를 쓰지 않고 다시 셈 | `core/src/recon.c` |
| `snprintf("%.*s")` | 읽는 쪽·쓰는 쪽 모두 길이 제한 | `recon.c`, `feed_source.c`, `snapshot.c` |
| `strtoll` + `errno` + end 포인터 | 안전한 문자열→정수 | `core/src/kv_config.c` |
| xorshift64\* 시드 난수 | 전역 상태 없는 재현 가능한 난수 | `exchange/src/liquidity/synthetic.c` |
| 역변환 샘플링(지수분포) | 균등 난수 → 도착 간격·가격 이격 | `synthetic.c`의 `next_exponential` |
| 논리 시각 | 시계 대신 입력이 들고 온 시각 | `ts_t`, `market_rules.h`의 `ts_time_of_day` |
| assert vs 에러 코드 | 내부 버그는 assert, 외부 입력은 음수 코드 | 전부. 예: `order_pool_release`(assert) vs `book_insert`(에러) |
| 테스트만 `-UNDEBUG` | 릴리스 빌드여도 테스트의 assert는 산다 | 루트 `CMakeLists.txt`의 `mini_sor_add_test` |
| fork / SIGKILL / mmap 공유 | 실제로 죽여 보는 장애 주입 | `core/tests/test_fault_inject.c` |
| SIGABRT 핸들러 | "assert가 터져야 통과" 테스트 | `core/tests/test_order_pool_double_free.c` |
| `ponytail:` 주석 | 일부러 단순하게 둔 곳과 그 한계 | `order_book.c`(선형 스캔), `journal.c`(CRC 표 없음), `feed.h`(증분 없음), `match.h`(체결 목록 64건) |

#### 부록 B. 결정성을 지키는 장치 모음

결정성(같은 입력 → 같은 출력)은 이 프로젝트의 전제이고, core·exchange 곳곳에 흩어진 장치가 함께 지킨다.

| 위협 | 막는 장치 | 위치 |
|---|---|---|
| 시스템 시각 | 모든 시각은 `ts_t` 논리 시각. 세션 판정도 `ts`로 | `types.h`, `market_rules.h`, `synthetic.c`의 `now` |
| 전역 난수 | 생성기마다 xorshift 상태, 시드 명시 주입, 시장별 시드 파생 | `synthetic.c`, `divergent.c` |
| 해시 순회 순서 | 인덱스에 순회 API가 없음 | `order_index.h` |
| 부동소수 오차 | 가격·수량·금액이 정수 | `types.h`, `exec_result_t.notional` |
| 구조체 패딩 쓰레기 | `memset` 후 채우기 | `order_pool_acquire`, `match_emit`, `match_sweep` |
| 이벤트 순서 흔들림 | maker → taker 고정, 콜백 재진입 금지 | `event.h`, `match_sweep` |
| 난수 소비 순서 | 도착 간격 → 방향 → 이격 → 수량 순서 고정 | `synth_next` |
| 검증 | 10만 건 이벤트 바이트 비교 | `exchange/tests/test_determinism.c` |

---

### 4.2 sor · bench · sdk — 주문 배분, 측정, 전략 엔진 라이브러리

#### 들어가기 전에 — 이 부분이 다루는 것

이 장은 `sor/`, `bench/`, `sdk/` 세 모듈을 다룬다. 앞 장의 `core/`(타입·에러 코드)와 `exchange/`(호가창·매칭 엔진)를 이미 읽었다고 가정하지만, 여기서 쓰는 바깥 함수는 처음 나올 때 한 줄씩 설명한다.

세 모듈의 관계는 이렇다.

```
sdk/        바깥 전략 엔진이 "주문 전문"을 만들고 응답을 추적하는 도구 (core만 의존)
            ─ 시뮬레이터 내부와 독립이다

sor/        논리 주문 하나를 "KRX에 몇 주, NXT에 몇 주"로 나누고(계획),
            실제로 보내고(집행), 결과를 모으고(매핑), 근거를 남기고(로그),
            얼마나 잘했는지 잰다(품질)

bench/      sor/와 exchange/를 불러 "얼마나 빠른가"와 "어느 전략이 싸게 샀는가"를
            재서 bench/results/*.md 로 남긴다 ─ 이 프로젝트의 최종 산출물이 여기서 나온다
```

**C를 잘 모르는 독자를 위한 최소 지식.** 이 장의 코드를 읽는 데 필요한 것은 이 정도다.

| 보이는 것 | 뜻 |
|---|---|
| `typedef struct { ... } plan_leg_t;` | 여러 값을 한 덩어리로 묶은 "자료 상자"에 이름을 붙인다. `_t`로 끝나면 타입 이름이다 |
| `const cons_book_t *cons` | `cons`는 상자의 **주소**(포인터)다. `const`는 "읽기만 하고 고치지 않는다"는 약속이다 |
| `cons->book[m]` | 포인터가 가리키는 상자의 `book` 칸의 `m`번째 값 |
| `int f(..., exec_plan_t *out)` | 결과는 `out`이 가리키는 곳에 쓰고, 반환값은 성공(0 = `ERR_OK`)/실패(음수 에러 코드)만 알린다. 이 프로젝트의 공통 규약이다 |
| `static` 함수 | 그 파일 안에서만 쓰는 도우미 함수 |
| `assert(조건)` | 조건이 거짓이면 프로그램을 즉시 멈춘다. 테스트와 "절대 일어나면 안 되는 일" 검사에 쓴다 |
| `int (*plan)(...)` | **함수 포인터.** 칸에 함수를 넣어 두고 나중에 부른다. 전략 4종을 같은 모양으로 다루는 데 쓴다 |
| `memset(&x, 0, sizeof(x))` | 상자 `x`를 바이트 단위로 전부 0으로 민다 |

가격(`price_t`)과 수량(`qty_t`)은 정수(원, 주)다. 시각(`ts_t`)은 나노초 단위 정수이고, `TOD_NS(12, 0, 0)`은 "12시 0분 0초"를 나노초로 바꾼 값(43,200,000,000,000)이다. 시장은 `MARKET_KRX = 0`, `MARKET_NXT = 1`, `MARKET_COUNT = 2`로 정의돼 있다.

C 코드는 WSL Ubuntu에서 빌드한다. 테스트는 `ctest --test-dir build -R sor --output-on-failure`처럼 모듈 이름으로 골라 돌린다. 벤치 실행 파일은 ctest에 들어 있지 않고 Release 빌드(`build-rel/`)에서 따로 실행한다.

---

#### sor/ — Smart Order Routing 엔진

##### 1. 한 줄 역할과 왜 필요한가

**한 줄 역할: 사용자가 낸 주문 하나를 KRX와 NXT 중 어디에 얼마씩 보낼지 정하고, 보내고, 결과를 모으고, 그 판단의 근거와 품질을 기록한다.**

2025년 3월 넥스트레이드(NXT)가 문을 열면서 같은 종목이 두 시장에서 거래된다. 두 시장의 호가는 서로 다르다. 어떤 순간에는 NXT에서 10,000원에 살 수 있는데 KRX에서는 10,010원을 내야 한다. 증권사는 자본시장법 제68조의 **최선집행의무** 때문에 고객 주문을 "최선의 조건"으로 집행해야 하고, 그 판단의 **근거를 남겨야** 한다.

SOR(Smart Order Routing)은 그 일을 하는 계층이다. 이 프로젝트의 `sor/`는 다음 여섯 가지 일을 한다.

| 일 | 파일 | 태스크 |
|---|---|---|
| 두 시장 호가를 한 줄로 겹쳐 보기 | `consolidated.*` | T2-01, T2-02 |
| 시장별 점수 매기기(최선집행 평가) | `best_execution.*` | T2-03, T2-04 |
| 어디에 몇 주 보낼지 계획 세우기(전략 4종) | `strategy*.c`, `strategy.h` | T2-05 ~ T2-08 |
| 논리 주문과 물리 주문 번호 잇기 | `order_map.*` | T2-09 |
| 계획대로 보내고 결과를 합산하기, 취소하기 | `executor.*` | T2-09 ~ T2-11 |
| 판단 근거 기록 | `routing_log.*` | T2-12 |
| 집행 품질(슬리피지·체결률) 측정 | `execution_quality.*` | T2-13 |

`sor`는 정적 라이브러리 하나(`sor/CMakeLists.txt`)이고 `exchange`에 의존한다. 실제 운영 경로에서는 원장 코어(`ledger/src/ledger_core.c`)가 `routing_plan(&STRATEGY_BEST_PRICE, ...)`과 `exec_submit(...)`을 부른다. 즉 **화면에서 낸 주문은 BEST_PRICE 전략으로 라우팅된다.** 나머지 세 전략은 주로 벤치 비교 실험에서 쓰인다.

##### 2. 읽는 순서

태스크 번호 순서가 곧 의존 순서다. 헤더(무엇을 하는가, 왜 그렇게 정했는가) → 소스(어떻게) → 테스트(정말 그렇게 되는가) 순으로 읽는다. 헤더 주석에 설계 이유가 대부분 적혀 있으므로 헤더를 건너뛰지 않는다.

| 순서 | 파일 | 먼저 봐야 할 것 |
|---|---|---|
| 0 | (바깥) `exchange/include/order_book.h`, `match.h` | `book_best_ask`, `book_qty_at`, `book_qty_up_to`, `book_snapshot`, `match_limit`, `exec_result_t` |
| 1 | `sor/include/consolidated.h` → `sor/src/consolidated.c` | `better()` 동률 규칙 |
| 2 | `sor/tests/test_consolidated.c`, `test_consolidated_update.c` | 캐시가 없다는 것을 검사하는 방식 |
| 3 | `sor/include/best_execution.h` → `sor/src/best_execution.c` | 네 항목 점수 공식과 가중치 |
| 4 | `sor/tests/test_best_execution.c`, `test_be_weights.c` | "한 항목만 다르게" 만드는 테스트 설계 |
| 5 | `sor/include/strategy.h` → `sor/src/strategy.c` | `exec_plan_t`, `plan_validate`, `plan_resting_market` |
| 6 | `strategy_krx_only.c` → `strategy_best_price.c` → `strategy_split.c` → `strategy_sweep.c` | 쉬운 것부터 |
| 7 | `test_strategy_krx_only.c` → `_best_price` → `_split` → `_sweep` | 손계산 값 |
| 8 | `sor/include/order_map.h` → `sor/src/order_map.c` → `test_order_map.c` | 물리 번호 공식 |
| 9 | `sor/include/executor.h` → `sor/src/executor.c` | "되돌리지 않는다" 원칙 |
| 10 | `test_split_state.c`, `test_split_cancel.c` | 한쪽 거부/한쪽 마감 장면 |
| 11 | `sor/include/routing_log.h` → `sor/src/routing_log.c` → `test_routing_log.c` | 로그만으로 결정을 재현하는 검산 |
| 12 | `sor/include/execution_quality.h` → `sor/src/execution_quality.c` → `test_execution_quality.c` | 기준가 선택 이유, bp 반올림 |
| 13 | `sor/tests/test_recon_live.c` | 실제 흐름을 대사(core의 recon)에 넘기는 통합 검사 |

##### 3. 핵심 자료구조 (숫자 예시와 함께)

아래 모든 예시는 **같은 장면 하나**를 쓴다. 이 장면을 "예시 장면"이라고 부른다.

```
예시 장면 (12:00, 두 시장 모두 열림)

          KRX                         NXT
 매도  10,020원 × 200주          10,030원 × 300주
 매도  10,010원 × 100주          10,000원 × 150주
 ──────────────────────        ──────────────────────
 매수   9,990원 × 500주           9,980원 × 500주

주문: 매수 300주, 지정가 10,020원 (논리 주문번호 7)
```

###### 3-1. `cons_book_t` — 통합 호가창

```c
typedef struct {
    const order_book_t   *book[MARKET_COUNT];   // 시장별 호가창 주소
    const market_rules_t *rules[MARKET_COUNT];  // 시장별 개장 규칙 (NULL이면 항상 열림)
} cons_book_t;
```

통합 호가창은 **자기 데이터를 갖지 않는다.** 두 시장의 호가창 주소만 들고 있다가, 물어볼 때마다 각 시장에 다시 묻는다. 헤더 주석은 그 이유를 이렇게 적는다 — 캐시를 두면 "체결·취소·정정 때마다 캐시를 갱신하라고 알리는 경로"가 생기고, 알리는 것을 빠뜨린 경로가 곧 조용한 오답이 된다. 시장 호가창의 최우선호가 조회는 이미 O(1)이라 매번 묻는 비용이 작다(ADR `docs/decisions/0004-통합-호가창-캐시-없음.md`).

한 줄짜리 결과는 `cons_level_view_t`로 돌려준다.

```c
typedef struct {
    price_t  price;
    qty_t    total_qty;
    int32_t  order_count;
    market_t market;   // 이 호가가 어느 시장 것인지
} cons_level_view_t;
```

예시 장면에서 `cons_snapshot(cons, SIDE_SELL, 5, ts, out)`(매도 쪽 5단)을 부르면 이렇게 나온다.

| 순위 | price | total_qty | market |
|---|---:|---:|---|
| 1 | 10,000 | 150 | NXT |
| 2 | 10,010 | 100 | KRX |
| 3 | 10,020 | 200 | KRX |
| 4 | 10,030 | 300 | NXT |

반환값은 4(채운 줄 수)다. 매도는 싼 가격부터, 매수는 비싼 가격부터 놓인다. **같은 가격이라도 시장이 다르면 줄이 따로 나온다** — 합쳐 버리면 "그 물량이 어느 시장에 있는가"라는 라우팅 재료가 사라지기 때문이다.

###### 3-2. `venue_score_t` — 시장 하나에 대한 평가

```c
typedef struct {
    bool eligible;     // 라우팅 후보인가
    int  reason;       // 후보가 아니면 이유 (ERR_MARKET_CLOSED, ERR_NO_LIQUIDITY)
    price_t quote;     // 상대 최우선호가
    qty_t   fillable;  // 지정가 안에서 지금 채울 수 있는 수량
    price_t spread;    // 매도 최우선 - 매수 최우선
    int32_t price_score, fill_score, cost_score, state_score;  // 항목별 점수 0~10000
    int32_t total;     // 가중평균
} venue_score_t;
```

합계만이 아니라 **항목별 점수와 그 재료(quote, fillable, spread)를 함께 보관한다.** 최선집행의무는 근거를 남기는 의무이기도 해서, "가격 때문에 골랐나, 수수료 때문에 골랐나"를 나중에 말할 수 있어야 한다. 예시 장면의 실제 값은 4-2절에서 계산한다.

###### 3-3. `exec_plan_t` — 집행 계획

```c
typedef struct {
    market_t     market;
    qty_t        qty;
    price_t      limit_price;
    order_type_t type;
} plan_leg_t;                    // "다리(leg)" = 한 시장에 보낼 몫

typedef struct {
    plan_leg_t legs[PLAN_LEGS_MAX];  // PLAN_LEGS_MAX = 8
    int32_t    leg_count;
    qty_t      planned_qty;          // 다리 수량의 합
    int        reason;               // 못 세웠으면 그 이유
} exec_plan_t;
```

예시 장면에서 SPLIT 전략이 세우는 계획은 다음과 같다.

| legs[i] | market | qty | limit_price | type |
|---|---|---:|---:|---|
| 0 | KRX | 200 | 10,020 | LIMIT |
| 1 | NXT | 100 | 10,020 | LIMIT |

`leg_count = 2`, `planned_qty = 300`, `reason = ERR_OK`.

계획의 불변조건은 두 개이고 `plan_validate()`가 검사한다.

1. 비어 있지 않은 계획의 다리 수량 합 == 원 주문 수량. **한 주라도 새면** 논리 주문의 잔량 계산이 영원히 어긋난다(`ERR_INVALID_QTY`).
2. 한 시장에 다리는 하나뿐. 두 번 나오면 `ERR_DUPLICATE`. 이 규칙 덕분에 아래의 물리 주문번호 공식이 성립한다.

그리고 `plan_add_leg()`는 **0주짜리 다리를 만들지 않는다**(아무것도 하지 않고 `ERR_OK`). 0주 다리를 남기면 뒤에서 빈 물리 주문을 만들게 되기 때문이다.

**전략은 계획만 만들고 주문을 보내지 않는다.** `strategy.h` 머리 주석의 이유 — 네 전략을 **같은 유동성**에서 비교하려면 계획 단계에서는 호가창을 건드리지 않아야 한다. 전략이 직접 집행하면 첫 전략이 호가창을 바꿔 놓는다.

###### 3-4. 논리 주문과 물리 주문, 그리고 번호 공식

| 용어 | 뜻 | 예시 |
|---|---|---|
| 논리 주문 | 사용자가 낸 주문 하나 | "매수 300주" (번호 7) |
| 물리 주문 | 논리 주문이 시장별로 쪼개져 실제로 거래소에 간 주문 | "KRX에 200주"(번호 113), "NXT에 100주"(번호 114) |

체결 통보는 물리 주문번호로 오고, 사용자에게 보여 줄 것은 논리 주문의 상태다. 둘을 잇는 방법이 **해시 테이블이 아니라 산수**다(`order_map.h`).

```
phys = logical × PHYS_ID_SLOTS + market + 1        (PHYS_ID_SLOTS = 16)
```

| 논리 번호 | 시장 | 물리 번호 계산 | 물리 번호 |
|---:|---|---|---:|
| 7 | KRX(0) | 7 × 16 + 0 + 1 | 113 |
| 7 | NXT(1) | 7 × 16 + 1 + 1 | 114 |
| 200,000,000 | KRX | 200,000,000 × 16 + 1 | 3,200,000,001 |

거꾸로 갈 때는 나눗셈과 나머지를 쓴다.

```
114 % 16 = 2  → 자리값 2 → 시장 = 2 - 1 = 1 (NXT)
114 / 16 = 7  → 논리 번호 7
```

- 자리값이 0이면 "시장 없음", 시장 수(2)보다 크면 없는 시장이라 거절한다(`phys_id_market`).
- `+1`을 하는 이유 — 자리값 0을 "비어 있음"으로 남겨 두면 잘못된 번호를 산수만으로 걸러 낼 수 있다.
- 16은 시장 수(2)보다 넉넉한 2의 거듭제곱이다. 시장이 늘어도 번호 체계를 안 바꾸려는 여유다. 대가로 논리 번호 상한이 `UINT64_MAX / 16`(약 10^18)으로 줄지만 실질적 제약은 아니다.
- 이 공식 때문에 **작은 논리 번호의 물리 번호가 유동성 주문번호와 겹칠 수 있다.** 그래서 `bench/compare.h`는 측정 주문 논리 번호를 2억부터 시작하고(물리 번호 32억 이상), 테스트들은 유동성 주문번호를 100만 번대에서 뽑는다.

###### 3-5. `order_map_t` — 매핑 표

```c
typedef struct {
    order_id_t phys_id;
    market_t   market;
    qty_t      sent_qty;      // 보낸 수량
    qty_t      filled_qty;    // 체결된 수량
    int64_t    notional;      // 체결 금액(원) = Σ 가격×수량
    qty_t      canceled_qty;
    bool       live;          // 아직 호가창에 남아 있는가
    bool       accepted;      // 거래소가 한 번이라도 받아 주었는가
} phys_leg_t;

typedef struct {
    order_id_t logical_id;
    side_t     side;
    price_t    limit_price;
    qty_t      order_qty;
    phys_leg_t legs[PLAN_LEGS_MAX];
    int32_t    leg_count;
} logical_order_t;
```

`order_map` 내부(`order_map.c`의 `struct order_map`)는 두 배열이다.

- `orders[]` — 논리 주문을 **등록 순서대로** 채운다. 삭제가 없다(체결이 끝나도 평균 단가를 내려면 이력이 남아야 한다).
- `slot[]` — 논리 번호 → `orders[]` 칸 번호를 찾는 해시 표. 크기는 용량×2 이상인 2의 거듭제곱이고(부하율 0.5), 빈 칸은 -1이다. 충돌 시 옆 칸으로 가는 **오픈 어드레싱(선형 탐사)**을 쓴다. 해시 함수는 splitmix 계열 비트 섞기다.

물리 번호로 찾을 때는 먼저 산수로 논리 번호를 구하고(`phys_id_logical`), 해시 표로 논리 주문을 찾은 뒤, 그 주문의 다리 최대 8개를 훑는다.

예시 장면을 SPLIT으로 집행한 직후 논리 주문 7의 모습이다(4-7절에서 과정을 따라간다).

| 다리 | phys_id | sent | filled | notional | canceled | live | accepted |
|---|---:|---:|---:|---:|---:|---|---|
| KRX | 113 | 200 | 200 | 2,003,000 | 0 | false | true |
| NXT | 114 | 100 | 100 | 1,000,000 | 0 | false | true |

**불변조건: 논리 잔량 = 원 수량 − 모든 다리의 체결 합 − 모든 다리의 취소 합.** `omap_remaining()`이 정확히 이 식으로 계산한다.

###### 3-6. `exec_report_t` — 집행 보고서

`exec_submit()`이 돌려주는 요약이다. 다리별 결과(`leg_result_t`: 물리 번호, 보낸 수량, 거래소 반환 코드 `rc`, 체결 수량·금액, 남은 수량, 호가창에 등록됐는지 `resting`)와 합계를 담는다.

| 필드 | 뜻 | 불변조건 |
|---|---|---|
| `filled_qty` | 모든 다리 체결 합 | 다리별 `filled_qty`의 합 |
| `unfilled_qty` | 체결 안 된 수량 | `order_qty − filled_qty` |
| `working_qty` | 아직 시장에 살아 있는 수량 | `≤ unfilled_qty` (거부·IOC 잔량은 살아 있지 않다) |
| `rejected_count` | 거부된 다리 수 | |
| `status` | 논리 주문 상태 | `exec_status()`와 같은 값 |

`unfilled_qty`와 `working_qty`가 다를 수 있다는 점이 핵심이다. 40주 다리가 거부되면 그 40주는 "체결 안 됨"이지만 "살아 있음"은 아니다.

###### 3-7. `routing_decision_t` — 판단 근거 한 건

주문 번호·시각·방향·지정가·수량, 전략 이름, 두 시장의 `venue_score_t` 전부, **실제로 쓴** 가중치와 수수료 설정, 시장별 배분 수량 `alloc[]`, 다리 수, 이유 코드를 담는다. 4-8절에서 예시를 본다.

###### 3-8. `eq_metrics_t` — 집행 품질

주문 수량, 체결 수량, 체결 금액, 기준가(benchmark), 표시용 평균 단가(원, 버림), 표시용 슬리피지(원), **측정용 슬리피지(bp, 체결 금액에서 직접)**, 체결률(bp)을 담는다. 4-9절에서 계산한다.

##### 4. 핵심 함수 흐름

###### 4-1. 통합 호가창 — `cons_best_ask` / `cons_snapshot`

**열려 있는가 — `cons_is_open(cons, market, ts)`**

1. 붙지 않은 시장(`book[m] == NULL`)이면 false.
2. 규칙이 NULL이면 true(세션과 무관하게 라우팅만 시험하려는 테스트·벤치용).
3. 아니면 그 시장 규칙 테이블의 `is_open(ts, &session)`에 묻는다.

닫힌 시장은 통합 뷰에서 빠진다. 헤더 주석대로 NXT만 열린 구간(08:00~09:00, 15:30~20:00)에서는 통합 최우선호가가 곧 NXT의 호가다. 테스트는 18:00(NXT만), 15:25(NXT 오후 휴장, KRX만), 22:00(둘 다 닫힘)을 쓴다.

**동률 규칙 — `better()` 한 곳에 모은다**

a가 b보다 앞서는가를 이 순서로 따진다.

1. 가격이 다르면: 매수 쪽은 비싼 쪽이, 매도 쪽은 싼 쪽이 앞선다.
2. 가격이 같으면: **잔량이 많은 시장**이 앞선다(체결 가능성이 높다).
3. 잔량도 같으면: **시장 열거 순서**(KRX가 앞).

| 장면(매도 최우선) | 결과 |
|---|---|
| KRX 10,010×100, NXT 10,000×150 | NXT (가격) |
| KRX 10,000×100, NXT 10,000×300 | NXT (잔량) |
| KRX 10,000×200, NXT 10,000×200 | KRX (열거 순서) |

최우선호가(`best_of`)와 스냅샷(`cons_snapshot`)이 **같은 `better()`**를 쓴다. 둘이 다른 규칙을 쓰면 "1단 호가"와 "최우선호가"가 다른 시장을 가리키는 일이 생긴다. 마지막 동률까지 정해져 있으니 같은 입력에는 항상 같은 시장이 나온다(결정성).

**`best_of()` 흐름**

1. 시장 0, 1을 돌면서 닫힌 시장은 건너뛴다.
2. 매수 쪽이면 `book_best_bid`, 매도 쪽이면 `book_best_ask`를 묻는다. 없으면(`BOOK_PRICE_NONE`) 건너뛴다.
3. 그 가격의 잔량 `book_qty_at`을 구해 지금까지의 최선과 `better()`로 비교한다.
4. 찾았으면 가격을 돌려주고 `*out_market`에 시장을 쓴다. 못 찾으면 `BOOK_PRICE_NONE`을 돌려주고 `out_market`은 건드리지 않는다.

예시 장면: `cons_best_ask()` = 10,000, 시장 = NXT. `cons_best_bid()` = 9,990, 시장 = KRX.

**`cons_snapshot()` 흐름 — 두 줄 병합**

1. `depth`가 64(`PER_MARKET_MAX`)를 넘으면 64로 자른다.
2. 열린 시장마다 `book_snapshot`으로 `depth`단씩 떠 둔다. 각 시장에서 depth단이면 통합 depth단을 채우기에 충분하다(최악은 한 시장이 상위를 전부 차지하는 경우다).
3. 두 목록의 맨 앞끼리 `better()`로 비교해 이긴 줄을 결과에 넣고 그 목록의 커서를 한 칸 옮긴다. 정렬된 두 줄을 합치는 "병합(merge)"이다.
4. depth를 채우거나 양쪽이 바닥나면 멈추고 채운 줄 수를 돌려준다.

**`cons_qty_at(side, price)`** — 열린 시장의 그 가격 잔량을 더한다. 예시 장면에서 매도 10,000원은 150(NXT만).

###### 4-2. 최선집행 평가 — `be_evaluate` / `be_pick`

**무엇을 재는가.** 네 항목을 각각 0~10,000점(`BE_SCORE_MAX`)으로 매기고 가중평균한다.

| 항목 | 공식 | 기본 가중치 |
|---|---|---:|
| 가격 `price_score` | 10000 − (최선 호가 대비 불리함 bp) × 100 | 40 |
| 체결 가능성 `fill_score` | fillable × 10000 / 주문수량 | 30 |
| 비용 `cost_score` | 10000 − 수수료 bp × 100 | 20 |
| 시장 상태 `state_score` | 10000 − (스프레드 bp) × 20 | 10 |

- 모든 점수는 0 미만이면 0, 10,000 초과면 10,000으로 자른다(`clamp_score`).
- `BE_BP_PENALTY = 100` — 1bp 불리할 때마다 100점. 100bp(1%) 불리하면 0점이다. **가격과 수수료가 같은 계수로 깎인다.** 그래서 "수수료가 1bp 싼 대신 가격이 1bp 비싼 시장"은 정확히 같은 점수가 된다(ADR 0005).
- `BE_SPREAD_PENALTY = 20` — 스프레드는 가격보다 완만하게 본다.
- 기본 수수료(`BE_CONFIG_DEFAULT`)는 KRX 3bp, NXT 2bp다. 코드 주석이 밝히듯 **실제 수수료율이 아니라 차이를 만들기 위한 값**이다.
- 가중치 이유(주석): 가격이 최선집행의 1차 기준이라 가장 무겁고, 못 채운 주문은 결국 다음 호가에서 더 비싸게 체결되므로 체결 가능성은 "지연된 가격"에 가깝다. 비용·상태는 보조다.
- 불리함 bp는 정수 나눗셈이라 1bp 미만 차이는 0이 된다(호가 단위가 있으니 그보다 세밀한 구분은 의미가 없다는 판단).

**흐름 (`be_evaluate`)**

0. 인자 검사: NULL, 방향, 수량 하한, 가중치 음수, 가중치 합 0, 수수료 음수를 거절한다. `w`/`cfg`가 NULL이면 기본값을 쓴다. `out[]`을 0으로 민다.
1. **1단계 — 재료 모으기.** 매수 주문이면 상대(maker)는 매도 쪽이다. 시장마다:
   - 닫혔으면 `reason = ERR_MARKET_CLOSED`, 다음 시장.
   - 상대 최우선호가가 없으면 `reason = ERR_NO_LIQUIDITY`, 다음 시장.
   - `fillable = book_qty_up_to(book, 매도쪽, 지정가, 주문수량)` — 최우선호가부터 지정가까지 잔량을 더하되 **주문 수량에 닿으면 멈춘다.** 0이면 `ERR_NO_LIQUIDITY`(호가는 있으나 전부 지정가 밖).
   - 스프레드 = 매도 최우선 − 매수 최우선(한쪽이 비면 0).
   - `eligible = true`. 매수면 가장 싼 quote를 `best_quote`로 기억한다.
2. **2단계 — 점수.** 가격 점수는 두 시장을 비교해야 하므로 1단계가 끝난 뒤에 매긴다. 후보 시장마다 네 점수와 총점(`Σ 점수×가중치 / 가중치 합`, 정수 나눗셈)을 낸다.

**제외된 시장도 지우지 않는다.** `eligible = false`와 `reason`이 남는다. "왜 그 시장을 안 썼는가"도 근거다.

**예시 장면 계산 (매수 300주, 지정가 10,020, 기본 설정)**

| 재료/점수 | KRX | NXT |
|---|---:|---:|
| quote(매도 최우선) | 10,010 | 10,000 |
| fillable(지정가 10,020까지, 300에서 멈춤) | 100+200 = 300 | 150 (10,030은 지정가 밖) |
| spread | 10,010 − 9,990 = 20 | 10,000 − 9,980 = 20 |
| 불리함 bp | (10,010−10,000)×10000/10,000 = 10 | 0 |
| **price_score** | 10000 − 10×100 = **9,000** | **10,000** |
| **fill_score** | 300×10000/300 = **10,000** | 150×10000/300 = **5,000** |
| **cost_score** | 10000 − 3×100 = **9,700** | 10000 − 2×100 = **9,800** |
| 스프레드 bp | 20×10000/10,010 = 19 (버림) | 20×10000/10,000 = 20 |
| **state_score** | 10000 − 19×20 = **9,620** | 10000 − 20×20 = **9,600** |
| 가중합 | 9000×40 + 10000×30 + 9700×20 + 9620×10 = 950,200 | 10000×40 + 5000×30 + 9800×20 + 9600×10 = 842,000 |
| **total** (÷100) | **9,502** | **8,420** |

NXT가 10원 더 싸지만 **300주 중 150주밖에 못 채워서** KRX가 이긴다. 만약 NXT 10,000원에 300주가 있었다면 NXT의 fill_score가 10,000이 되어 total = (400,000 + 300,000 + 196,000 + 96,000)/100 = 9,920으로 NXT가 이긴다.

**`be_pick()` — 하나 고르기.** 후보 중에서 이 순서로 비교한다.

1. 총점이 높은 시장.
2. 총점이 같으면 `fillable`이 많은 시장(통합 호가창과 같은 방향의 규칙 — "물량이 많은 쪽").
3. 그것도 같으면 먼저 본 시장(KRX).

후보가 없으면 `ERR_NO_LIQUIDITY`. 예시 장면에서는 KRX를 돌려준다.

**`be_load_config(path, w, cfg)`** — `key = value` 설정 파일(`#` 뒤는 주석)에서 `weight_price`, `weight_fill`, `weight_cost`, `weight_state`, `fee_krx_bp`, `fee_nxt_bp`를 읽는다. 적지 않은 키는 기본값, **모르는 키는 거절**한다(오타 난 설정으로 돌린 실험은 나중에 해석할 수 없다). 음수·가중치 합 0은 `ERR_INVALID_ARG`, 파일이 없으면 `ERR_NOT_FOUND`. 파싱은 `core`의 `kv_config_load`가 한다.

###### 4-3. 전략의 공통 뼈대

네 전략 모두 `exec_strategy_t` 테이블 하나다.

```c
typedef struct exec_strategy {
    const char *name;
    int (*plan)(const struct exec_strategy *self, const exec_context_t *ctx,
                const order_t *req, exec_plan_t *out);
} exec_strategy_t;

extern const exec_strategy_t STRATEGY_KRX_ONLY, STRATEGY_BEST_PRICE,
                             STRATEGY_SPLIT, STRATEGY_SWEEP;
```

부르는 쪽은 `strategy->plan(strategy, &ctx, &req, &plan)`으로 어느 전략이든 같은 모양으로 부른다. 전략을 늘리는 일이 "집행 경로에 if를 더하는 일"이 아니라 "테이블 하나를 새로 쓰는 일"이 되게 하려는 것이다(시장 규칙 `market_rules_t`와 같은 방식).

`exec_context_t`는 전략이 판단에 쓰는 재료 묶음이다: 통합 호가창 주소, 가중치(NULL이면 기본), 수수료 설정(NULL이면 기본), 논리 시각.

모든 `plan` 함수의 첫 단계는 같다.

1. NULL 검사.
2. `plan_init(out)` — 빈 계획으로 민다. **실패해도 `out`은 항상 유효한 빈 계획이다.**
3. 수량이 `QTY_MIN`~`QTY_MAX` 밖이면 `ERR_INVALID_QTY`, 방향이 이상하면 `ERR_INVALID_ARG`. 실패 코드는 반환값과 `out->reason` 양쪽에 남긴다.

**`plan_resting_market()` — 지금 체결이 안 될 때 어디에 등록할까.** 지정가 주문은 상대 호가가 없어도 호가창에 등록되는 것이 정상이다. 여기서 주문을 버리면, 조용한 장에서 BEST_PRICE만 주문을 버리고 KRX_ONLY는 등록하게 되어 **라우팅 품질과 무관한 이유로 체결률 차이가 생긴다.** 그래서 규칙을 따로 둔다.

1. 열린 시장만 본다.
2. 호가가 있는 시장이 없는 시장을 이긴다.
3. 둘 다 호가가 있으면 유리한 쪽(매수면 싼 매도호가).
4. 같으면 먼저 본 시장(KRX).
5. 열린 시장이 없으면 `ERR_MARKET_CLOSED`.

###### 4-4. KRX_ONLY — 기준선

```
1. KRX가 닫혔으면 ERR_MARKET_CLOSED (NXT가 열려 있어도 거부)
2. KRX에 전량 한 다리
```

호가가 있는지는 보지 않는다("보낼 수 있는가"와 "지금 체결되는가"는 다른 질문이다). NXT는 아예 보지 않는다. NXT 출범 이전의 집행을 흉내 낸 것이고, **다른 세 전략의 개선폭은 전부 이 전략 대비로 말한다.** 기준선이 KRX인 이유는 NXT가 KRX 상장 종목 중 일부만 거래하기 때문이다(모든 종목에서 항상 가능한 선택지는 KRX뿐).

예시 장면: 계획 = KRX 300주 @10,020.

실제로 보내면 KRX 매도호가를 10,010×100 → 10,020×200 순으로 먹는다.

```
체결 금액 = 10,010×100 + 10,020×200 = 1,001,000 + 2,004,000 = 3,005,000원
평균 단가 = 3,005,000 / 300 = 10,016.67 → 10,016원(버림)
```

###### 4-5. BEST_PRICE — 이긴 시장 하나에 전량

```
1. be_evaluate()로 두 시장 점수
2. be_pick()으로 하나 고르기
3. 후보가 없으면(ERR_NO_LIQUIDITY) plan_resting_market()으로 등록 시장을 고른다
4. 고른 시장에 전량 한 다리
```

**쪼개지 않는 것이 이 전략의 정의다.** 이긴 시장의 잔량이 모자라도 전량을 보내고, 못 채운 잔량은 그 시장 호가창에 등록된다. 여기서 쪼개면 SPLIT·SWEEP과 같은 일을 하게 되어 "쪼개는 것이 이득인가"를 잴 수 없다.

예시 장면: 4-2절 계산대로 KRX(9,502) > NXT(8,420)이므로 **계획 = KRX 300주**. 이 장면에서는 결과가 KRX_ONLY와 같다(체결 금액 3,005,000원). NXT가 더 싸지만 물량이 절반뿐이라 체결 가능성 점수에서 졌기 때문이다.

###### 4-6. SPLIT — 즉시 체결 가능 잔량에 비례해서 나누기

**비례의 기준**은 "호가창 전체 물량"이 아니라 **"이 주문의 지정가 안에서 지금 채울 수 있는 수량"**이다. 지정가 밖 물량은 이 주문에게 없는 것과 같다.

그리고 여기서는 `book_qty_up_to(..., want = QTY_MAX)`로 **끝까지 센다.** 주문 수량에서 멈추게 하면 양쪽 다 물량이 많을 때 둘 다 주문 수량으로 잘려 비율이 1:1로 뭉개진다. 코드 주석과 회귀 테스트(`test_ratio_survives_large_liquidity`)가 이 실수를 기록한다 — KRX 10,000주 : NXT 20,000주에서 100주를 내면 50:50이 아니라 33:67이어야 한다.

**흐름**

1. 시장마다 열렸는지 보고, 열렸으면 `fillable`(끝까지)과 합계 `total`을 구한다.
2. 열린 시장이 0개 → `ERR_MARKET_CLOSED`.
3. 열린 시장이 1개 → 그 시장에 전량(잔량이 0이어도 등록은 되어야 한다).
4. 둘 다 열렸는데 `total == 0` → `plan_resting_market()`으로 고른 시장에 전량.
5. 시장마다 `base = 주문수량 × fillable / total`(내림), `remainder = 나머지`.
6. 내림 때문에 모자란 주수(`leftover`)를 **나머지가 큰 시장부터 한 주씩** 준다(최대 나머지 방식, Hare quota). 나머지가 같으면 fillable이 많은 시장, 그것도 같으면 KRX. 한 시장은 한 번만 받는다(모자란 수는 "열린 시장 수 − 1" 이하이므로).
7. 시장 순서대로 `plan_add_leg`(0주 몫은 다리가 생기지 않는다).
8. `plan_validate()`로 합계를 한 번 더 확인한다. 어긋나면 계산 버그이므로 빈 계획과 에러를 돌려준다.

**예시 장면 계산**

```
fillable(끝까지, 지정가 10,020):  KRX = 100 + 200 = 300,   NXT = 150,   total = 450

KRX: 300 × 300 / 450 = 90,000 / 450 = 200  나머지 0
NXT: 300 × 150 / 450 = 45,000 / 450 = 100  나머지 0
내림 합 = 300 → leftover 0

계획: KRX 200주, NXT 100주
```

실제로 보내면(각 시장은 서로 독립된 엔진이다):

```
KRX 200주: 10,010×100 + 10,020×100 = 1,001,000 + 1,002,000 = 2,003,000원
NXT 100주: 10,000×100                                     = 1,000,000원
합계 3,003,000원, 평균 10,010원
```

**단수 처리 예시(테스트에서 그대로 가져온 값)**

| 잔량 KRX:NXT | 주문 | 내림/나머지 | 결과 | 이유 |
|---|---:|---|---|---|
| 1 : 2 | 10주 | KRX 3 r1, NXT 6 r2 | 3 : 7 | 나머지 큰 NXT가 1주 |
| 100 : 100 | 7주 | KRX 3 r100, NXT 3 r100 | 4 : 3 | 나머지·잔량 동률 → KRX |
| 1 : 3 | 3주 | KRX 0 r3, NXT 2 r1 | 1 : 2 | **잔량은 NXT가 많지만 나머지가 큰 KRX** |
| 1 : 1000 | 10주 | KRX 0, NXT 9 → 10 | 0 : 10 | 0주 몫은 다리 없음 |

셋째 줄이 중요하다. 앞의 경우들은 "나머지 큰 쪽"과 "잔량 많은 쪽"이 우연히 일치해서, 규칙을 잘못 짜도 통과한다. 둘이 갈리는 장면을 따로 만들어야 최대 나머지 방식이 실제로 검증된다.

왜 최대 나머지인가 — 주석은 "비례 관계를 가장 적게 왜곡한다"고 적는다. 3주를 47:53으로 나누는 소액 주문에서는 단수 1주가 배분을 33%씩 움직인다.

###### 4-7. SWEEP — 통합 호가창을 가격 순으로 쓸어 담기

**흐름**

1. `cons_snapshot(매도 쪽, 64단)`으로 통합 호가를 가격 순으로 받는다(`SWEEP_DEPTH = 64`).
2. 위에서부터 한 줄씩: 지정가를 넘는 줄에서 멈춘다(뒤는 더 불리하다). 넘지 않으면 `min(남은 수량, 그 줄 잔량)`을 그 줄의 시장 몫 `alloc[market]`에 더한다.
3. 다 훑고도 남은 수량이 있으면 `plan_resting_market()`이 고른 시장 몫에 더한다(등록될 잔량).
4. 시장별로 **합산한 몫**을 다리로 만든다. 한 시장의 여러 단을 가져가도 다리는 하나다(한 시장 한 다리 규칙).
5. `plan_validate()`.

**예시 장면 계산**

```
통합 매도호가: NXT 10,000×150 → KRX 10,010×100 → KRX 10,020×200 → NXT 10,030×300
남은 수량 300

1줄: NXT 10,000  150주 가져감  → alloc NXT 150, 남음 150
2줄: KRX 10,010  100주 가져감  → alloc KRX 100, 남음 50
3줄: KRX 10,020   50주 가져감  → alloc KRX 150, 남음 0  (멈춤)

계획: KRX 150주, NXT 150주
```

실제로 보내면:

```
KRX 150주: 10,010×100 + 10,020×50 = 1,001,000 + 501,000 = 1,502,000원
NXT 150주: 10,000×150                                  = 1,500,000원
합계 3,002,000원, 평균 10,006.67 → 10,006원(버림)
```

**왜 최선집행 점수가 아니라 가격으로 순서를 정하는가.** 주석의 설명 — 평가는 "주문 전체를 어느 시장에"라는 **시장 단위** 판단이고, SWEEP은 "다음 한 호가를 어디서"라는 **호가 단위** 판단이라 알갱이가 다르다. 점수에는 수수료·스프레드가 섞여 가격 순서를 뒤집을 수 있고, 체결 가능성 항목은 단을 소진할 때마다 값이 흔들린다. 수수료로 시장을 고르는 일은 BEST_PRICE 몫으로 남긴다.

`ponytail:` 주석이 한계를 밝힌다 — 64단보다 깊이 파야 하는 주문은 남는 잔량이 등록 시장으로 몰린다. 측정에서 의미가 생기면 스냅샷 시작 위치를 받는 형태를 더한다.

###### 4-8. 네 전략 한눈에 비교 (예시 장면, 매수 300주 @10,020)

| 전략 | 계획 (KRX / NXT) | 체결 금액 | 평균 단가(버림) | 기준가 10,000 대비 슬리피지 |
|---|---|---:|---:|---:|
| KRX_ONLY | 300 / 0 | 3,005,000 | 10,016 | 5,000원 → **17bp** |
| BEST_PRICE | 300 / 0 | 3,005,000 | 10,016 | **17bp** |
| SPLIT | 200 / 100 | 3,003,000 | 10,010 | 3,000원 → **10bp** |
| SWEEP | 150 / 150 | 3,002,000 | 10,006 | 2,000원 → **7bp** |

슬리피지 bp 계산(4-9절의 공식): 기준 금액 = 10,000 × 300 = 3,000,000원.

- KRX_ONLY: 5,000 × 10,000 / 3,000,000 = 16.67 → 0에서 먼 쪽으로 반올림 → 17
- SPLIT: 3,000 × 10,000 / 3,000,000 = 10
- SWEEP: 2,000 × 10,000 / 3,000,000 = 6.67 → 7

이 장면은 SWEEP이 가장 유리하게 끝나는 장면이다. 테스트 `test_sweep_is_not_worse_than_split`도 "KRX는 싸지만 얇고 NXT는 비싸지만 두꺼운" 장면에서 SWEEP이 SPLIT보다 더 채우거나, 같이 채우면 더 싸다는 것을 확인한다. 다만 이것은 한 주문 한 장면의 이야기다. 수백 주문을 연속으로 낸 벤치 결과는 다르게 나온다(bench 절 참조).

###### 4-9. 집행기 — `exec_submit()`과 체결이 매핑으로 흘러가는 길

```c
int exec_submit(order_map_t *map, venues_t *venues, const order_t *req,
                const exec_plan_t *plan, exec_report_t *out);
```

`venues_t`는 시장별 매칭 엔진 주소 두 개다(집행기는 소유하지 않고 빌린다).

**흐름**

1. 보고서를 0으로 민다.
2. `omap_register(map, req, plan, phys)` — 계획을 검사(`plan_validate`, 빈 계획 거절, 중복 번호 `ERR_DUPLICATE`, 자리 없음 `ERR_POOL_EXHAUSTED`)하고, 다리마다 물리 번호를 발급해 `live = true`로 등록한다. **실패하면 어느 시장에도 보내지 않고** `STATUS_REJECTED`로 끝낸다.
3. 다리마다:
   1. 그 시장 엔진이 없으면 `rc = ERR_NULL_PTR`. 있으면 `send_leg()` — 물리 주문 구조체를 `memset`으로 민 뒤 채우고(`id = 물리 번호`, `client_order_id = 논리 번호`), 주문 유형에 따라 `match_market` / `match_ioc` / `match_fok` / `match_limit`(지정가·중간가·기타)을 부른다.
   2. **거부되면**: `rejected_count++`, 첫 거부 이유를 기억, `omap_on_cancel(phys, 보낸 수량)`으로 **그 수량을 취소로 기록**하고 다음 다리로. 다른 다리는 건드리지 않는다.
   3. **접수되면**: `omap_on_accept(phys)`로 `accepted = true`. 결과(체결 수량·금액·남은 수량·등록 여부)를 보고서에 옮긴다.
   4. 체결이 있으면 `record_fills()` — **체결 건마다** `omap_on_fill(phys, 수량, 가격)`을 부른다. 평균 단가 하나로 뭉뚱그리면 나눗셈 나머지만큼 금액이 새기 때문이다. 체결 목록이 잘린 경우(엔진의 `EXEC_FILLS_MAX` 초과)에만 남은 몫을 평균 가격으로 한 번 넣는다. 그래도 합계 수량·금액은 정확하다.
   5. 잔량이 호가창에 **등록되지 않았다면**(IOC·시장가) 남은 수량을 `omap_on_cancel`로 취소 기록한다. 지정가처럼 등록된 잔량은 살아 있으니 건드리지 않는다.
   6. 보고서 합계에 더한다.
4. `unfilled_qty = order_qty − filled_qty`, `working_qty = omap_remaining()`, `status = exec_status()`.
5. **모든 다리가 거부됐을 때만** 첫 거부 코드를 돌려준다. 한 다리라도 접수되면 `ERR_OK`.

**왜 한쪽이 거부돼도 되돌리지 않는가.** 두 시장 주문은 서로 다른 거래소의 서로 다른 거래다. KRX에서 40주가 체결된 뒤 NXT가 거부했다고 그 40주를 없던 일로 만들 방법이 없다. 되돌리려면 반대매매를 내야 하는데, 그것은 취소가 아니라 **새로운 손실 있는 거래**다. 그래서 다리마다 독립적으로 성공/실패하고 논리 주문 상태는 그 합이다. 한 거래소 안의 FOK(전량 아니면 전무)는 호가창을 건드리기 전에 미리 세어 볼 수 있지만, 두 거래소 사이에는 그런 지점이 없다. 쪼갠 FOK는 다리 하나 안에서만 "전량 아니면 전무"다(주석에 한계로 적혀 있다).

**예시 — SPLIT 계획(KRX 200 / NXT 100)이 매핑으로 흘러가는 과정**

| 단계 | 호출 | 다리 113(KRX) 상태 | 다리 114(NXT) 상태 |
|---|---|---|---|
| 등록 | `omap_register` | sent 200, filled 0, live, accepted=false | sent 100, filled 0, live |
| KRX 접수 | `omap_on_accept(113)` | accepted=true | |
| KRX 체결 1 | `omap_on_fill(113, 100, 10010)` | filled 100, notional 1,001,000 | |
| KRX 체결 2 | `omap_on_fill(113, 100, 10020)` | filled 200, notional 2,003,000, **live=false**(체결+취소 = 보낸 수량) | |
| NXT 접수 | `omap_on_accept(114)` | | accepted=true |
| NXT 체결 | `omap_on_fill(114, 100, 10000)` | | filled 100, notional 1,000,000, live=false |
| 마무리 | `omap_remaining(7)` | | 300 − 300 − 0 = 0 |

보고서: `filled_qty 300`, `notional 3,003,000`, `unfilled 0`, `working 0`, `status STATUS_FILLED`.

**`omap_on_fill`의 안전장치** — `filled + canceled + 이번 수량 > sent`면 `ERR_INVALID_QTY`로 거절하고 **아무것도 바꾸지 않는다.** 절반만 반영하면 합계가 조용히 틀어져 원인을 못 찾는다. `omap_on_cancel`도 같은 검사를 한다.

**`exec_status()` — 매핑만 보고 상태 다시 계산하기**

```
체결 합 ≥ 주문 수량          → FILLED   (전량체결)
체결 합 > 0                  → PARTIAL  (부분체결) — 한쪽이 거부돼도, 나머지를 취소해도
살아 있는 수량 > 0           → NEW      (접수)
어느 다리든 accepted == true → CANCELED (취소)
그 밖                        → REJECTED (거부)
```

체결이 한 주라도 있으면 그 사실이 상태를 지배한다. 거부나 취소로 부르면 체결된 수량이 상태에서 사라지기 때문이다. 마지막 두 줄에서 `accepted`가 필요한 이유 — "거부된 다리"와 "접수된 뒤 전부 취소된 다리"는 수량만 보면 둘 다 "체결 0, 취소 = 보낸 수량"이라 구별되지 않는다.

**테스트의 실제 장면 — 한쪽 거부(`test_one_leg_rejected_other_stands`)**

18:00(NXT만 열림)에 NXT 10,000원 매도 100주를 깔고, 매수 100주를 KRX 40 / NXT 60으로 보낸다.

| 항목 | 값 |
|---|---|
| 반환값 | `ERR_OK` (한 다리는 접수됐다) |
| `rejected_count` | 1 (KRX `rc = ERR_MARKET_CLOSED`) |
| NXT 체결 | 60주 — 되돌리지 않는다 |
| `filled_qty` / `unfilled_qty` / `working_qty` | 60 / 40 / **0** |
| 취소 기록 | 40 (거부된 KRX 몫) |
| 상태 | PARTIAL |

**`exec_cancel()` — 논리 주문 취소**

1. 논리 주문이 없으면 `ERR_NOT_FOUND`.
2. 다리마다: 이미 끝난 다리(`live == false`)는 건너뛴다(`was_live = false`, 실패가 아니다). 살아 있으면 `match_cancel(엔진, 물리 번호, ts)`. 성공하면 사라진 수량만큼 `omap_on_cancel`. 실패하면 `failed++`, 첫 실패 코드 기억, **앞서 성공한 취소는 되돌리지 않는다.**
3. 시도한 다리가 0개면 `ERR_NOT_FOUND`, 아니면 첫 실패 코드(전부 성공이면 `ERR_OK`).

되돌리지 않는 이유 셋(헤더 주석): 취소를 되돌리는 것은 주문을 다시 내는 것이라 큐의 원래 자리(시간 우선순위)를 복원할 수 없다. 되돌리는 사이 체결되면 "취소된 줄 알았는데 체결"이라는 최악의 결과가 난다. 실패한 취소는 **재시도**할 수 있다 — 재시도는 안전하다.

테스트 `test_one_market_closed`의 숫자: 12:00에 KRX 40 / NXT 60을 등록(호가 없음, 전량 대기). 18:00에 취소하면 KRX는 닫혀 실패(`ERR_MARKET_CLOSED`), NXT 60주만 취소 → `working_qty 40`, 상태 NEW. 다음 날 13:00에 재시도하면 KRX 40주만 취소되고(이미 취소된 NXT를 두 번 세지 않는다) 상태 CANCELED.

###### 4-10. 라우팅 로그 — `routing_plan()`과 `routing_format()`

```c
int routing_plan(const exec_strategy_t *strategy, const exec_context_t *ctx,
                 const order_t *req, const routing_sink_t *sink, exec_plan_t *out);
```

**흐름**

1. `strategy->plan(...)`을 부른다. 반환값은 전략의 반환값 그대로다.
2. 싱크(`sink`)가 없으면 여기서 끝(근거를 만들 필요가 없다). 벤치와 원장은 `NULL`을 넘긴다.
3. `routing_decision_t d`를 `memset`으로 **패딩 바이트까지** 0으로 민다(결정 기록을 바이트 단위로 비교하는 것이 결정성 확인 방법이라서).
4. 주문 정보와 전략 이름을 채운다.
5. **실제로 쓴** 가중치·설정을 적는다(ctx가 NULL이면 기본값).
6. 전략이 무엇이든 `be_evaluate()`를 한 번 더 돌려 두 시장 점수를 남긴다. 평가가 실패해도 기록은 남긴다 — 점수 0인 기록 자체가 "평가할 재료가 없었다"는 근거다.
7. 계획의 다리로 `alloc[market]`을 채우고, `reason`(전략이 실패했으면 그 코드)을 적는다.
8. `routing_emit(sink, &d)` — 싱크 함수 `fn(d, ctx)`를 부른다.

**이벤트 싱크 방식**인 이유: 로깅이 파일을 열거나 시각을 읽으면 SOR 엔진이 결정적이지 않게 된다. 싱크는 값을 넘겨받기만 하고, 메모리에 쌓을지(벤치) 파일에 쓸지(운영) 바이트 비교할지(테스트)는 소비자가 정한다. 콜백 안에서 전략을 다시 돌리면 결정 순서가 흐트러지므로 금지한다.

**`routing_format()` — 한 줄 텍스트.** 예시 장면을 BEST_PRICE로 돌렸을 때 기록을 포맷하면 이런 모양이 된다(4-2절 계산값, 12:00 = 43,200,000,000,000ns).

```
id=7 ts=43200000000000 side=BUY limit=10020 qty=300 strategy=BEST_PRICE w=40/30/20/10 fee=3/2
KRX[elig=1 quote=10010 fill=300 spread=20 s=9000/10000/9700/9620 total=9502 alloc=300]
NXT[elig=1 quote=10000 fill=150 spread=20 s=10000/5000/9800/9600 total=8420 alloc=0]
legs=1 reason=0
```

(실제 출력은 한 줄이다. 여기서는 읽기 좋게 끊었다. `fill=`은 `fillable`, `s=`는 가격/체결/비용/상태 점수 순서다.)

이 한 줄만 있으면 누구든 `(9000×40 + 10000×30 + 9700×20 + 9620×10) / 100 = 9502`를 다시 계산해 KRX가 이긴 이유를 짚을 수 있다. 버퍼가 모자라면 `ERR_INVALID_ARG`를 돌려주고 받은 크기 밖으로는 한 바이트도 쓰지 않는다(`snprintf`만 쓴다).

###### 4-11. 집행 품질 — 슬리피지, 체결률, `eq_avg_diff_bp`

**기준가(benchmark)를 무엇으로 할까 — `eq_benchmark()`**

**접수 시점의 통합 최우선 상대호가**(arrival price)를 쓴다. 매수면 `cons_best_ask`, 매도면 `cons_best_bid`. 없으면 `ERR_NO_LIQUIDITY`. 헤더가 적은 후보 비교:

| 후보 | 탈락/채택 이유 |
|---|---|
| 주문의 지정가 | 지정가를 보수적으로 적기만 해도 슬리피지가 좋아 보인다. 잣대가 측정 대상에 조종당한다 |
| 중간가(매수·매도 가운데) | 한쪽 호가가 비면 정의되지 않는다. 이 시뮬레이터에는 그런 장면이 흔하다 |
| **통합 최우선 상대호가(접수 시점)** | **채택.** 전략이 무엇이든 같은 값이라 공통 잣대가 된다. 시장별 기준가를 쓰면 KRX_ONLY는 KRX 호가로, BEST_PRICE는 NXT 호가로 재게 되어 비교가 안 된다 |

"접수 시점"인 이유 — 집행 뒤의 호가는 그 집행이 밀어 놓은 결과라서, 자기가 만든 변화를 자기 기준으로 삼는 순환이 된다. **그래서 반드시 집행 전에 부른다.**

**bp 환산 — `eq_to_bp(value, base)`**

1bp = 0.01% = 1/10,000. `value × 10000 / base`를 정수로 계산하고 **0에서 먼 쪽으로 반올림**한다(나머지의 두 배가 base 이상이면 절댓값을 1 올린다). 부호는 절댓값에 적용하므로 매수·매도가 대칭이다. `base ≤ 0`이면 0.

| 입력 | 실제 비율 | 결과 |
|---|---:|---:|
| `eq_to_bp(1, 30000)` | 0.333bp | 0 |
| `eq_to_bp(2, 30000)` | 0.667bp | 1 |
| `eq_to_bp(1, 20000)` | 정확히 0.5bp | 1 |
| `eq_to_bp(-2, 30000)` | −0.667bp | −1 |

버림(0 방향)을 쓰지 않는 이유 — 0.6bp 슬리피지가 0bp로 보고된다. **손해를 낙관적으로 표시하는 규칙은 측정 도구로 쓸 수 없다.**

**`eq_measure()` — 흐름**

1. 인자 검사: 기준가 ≤ 0이면 `ERR_INVALID_PRICE`, 수량이 맞지 않으면 `ERR_INVALID_QTY`, 체결 0인데 금액이 있거나 체결이 있는데 금액 ≤ 0이면 `ERR_INVALID_ARG`.
2. `fill_rate_bp = eq_to_bp(체결수량, 주문수량)`.
3. 체결이 0이면 여기서 끝. 평균 단가·슬리피지는 0으로 남는다 — 이 0은 "좋음"이 아니라 "잴 것이 없음"이다.
4. `avg_price = 체결금액 / 체결수량`(버림, **표시용**).
5. `slippage`(원, 표시용) = 매수면 평균 − 기준가, 매도면 기준가 − 평균. **불리할수록 양수.**
6. `slippage_bp`(**측정용**) = 평균 단가를 거치지 않고 금액으로 직접: `base = 기준가 × 체결수량`, `diff = 매수면 체결금액 − base, 매도면 base − 체결금액`, `eq_to_bp(diff, base)`.

**왜 bp를 체결 금액에서 직접 내는가 — 테스트의 손계산**

```
매수 3주, 기준가 10,000원. 1주 @10,000 + 2주 @10,010 = 30,020원

  평균 단가 = 30,020 / 3 = 10,006.67 → 버림 10,006원
  평균 단가로 bp를 내면: (10,006 − 10,000) / 10,000 = 6bp       ← 틀림
  금액으로 bp를 내면  : 20 × 10,000 / 30,000 = 6.67 → 7bp       ← 정답
```

10,000원짜리 주식에서 1원은 정확히 1bp다. **재려는 크기와 같은 크기의 오차**를 중간에 끼워 넣을 수 없어서, 표시용 평균(원)과 측정용 bp를 따로 계산한다.

**부분 체결 예시(테스트 값)**: 주문 100주, 35주 체결(10주 @10,000 + 25주 @10,010 = 350,250원). 평균 10,007(버림), bp = 250×10,000/350,000 = 7.14 → 7bp, 체결률 3,500bp(35%). **슬리피지는 체결된 수량 기준이다.** 못 채운 65주는 가격이 없으므로 평균에 섞을 수 없고, 그 사실은 체결률이 따로 말한다.

**`eq_avg_diff_bp(notional_a, filled_a, notional_b, filled_b)` — 두 집행의 평균 단가 차이**

`(a의 평균 − b의 평균) / a의 평균`을 bp로 낸다. 매수에서 a를 기준선으로 두면 b가 더 싸게 샀을 때 양수다. 어느 쪽이든 체결이 없으면 0.

핵심은 평균을 **1/`EQ_AVG_SCALE`원(= 1/10,000원) 정밀도의 정수**로 들고 가는 것이다.

```c
int64_t avg_a = notional_a * EQ_AVG_SCALE / filled_a;   // EQ_AVG_SCALE = 10000
int64_t avg_b = notional_b * EQ_AVG_SCALE / filled_b;
return eq_to_bp(avg_a - avg_b, avg_a);
```

**예시 장면의 KRX_ONLY(3,005,000원/300주) 대 SPLIT(3,003,000원/300주)**

```
원 단위 버린 평균끼리:  10,016 − 10,010 = 6원 → 6 × 10,000 / 10,016 = 5.99 → 6bp

고정소수점 평균:
  avg_a = 3,005,000 × 10,000 / 300 = 100,166,666   (= 10,016.6666원)
  avg_b = 3,003,000 × 10,000 / 300 = 100,100,000   (= 10,010.0000원)
  차이  = 66,666
  66,666 × 10,000 / 100,166,666 = 6.655... → 7bp
```

실제 평균 차이는 6.667원 / 10,016.667원 = 6.655bp이므로 정답은 7bp다. 원 단위로 버린 평균을 쓰면 1bp가 사라진다.

이 문제는 실제로 일어났다(T6-07). 처음에 전략 비교가 `avg_price`끼리 뺐더니 **세 전략 사이의 1bp 안팎 차이가 통째로 사라져** 30개 시드 내내 셋이 똑같이 나왔다. 테스트에 남은 손계산:

| 기준선 | 비교 대상 | 버린 평균으로 | `eq_avg_diff_bp` |
|---|---|---:|---:|
| 1,001,990원/100주 (10,019.90) | 1,001,400원/100주 (10,014.00) | (10,019−10,014)/10,019 → 5bp | **6bp** |
| 1,001,499원/100주 (10,014.99) | 1,001,400원/100주 (10,014.00) | 둘 다 10,014 → **0bp** | **1bp** |

남는 버림 오차는 1/10,000원이라 10,000원 주식에서 0.0001bp다. 부동소수를 쓰지 않고, 체결 금액끼리 곱하는 방식보다 넘칠 여지가 적다(주석: 체결 금액 × 10,000은 체결 금액이 9.2×10^14원까지 넘치지 않는다).

##### 5. 테스트가 보장하는 것 — sor/

**통합 호가창 (`test_consolidated.c`, `test_consolidated_update.c`)**

- 가격이 같으면 잔량 많은 시장, 잔량도 같으면 KRX — 최우선호가와 스냅샷이 같은 순서를 쓴다.
- 닫힌 시장은 최우선호가·합산 잔량·스냅샷에서 모두 빠진다(18:00/15:25/22:00 장면).
- **캐시가 없다는 결정이 지켜진다.** 체결·부분 체결·취소·정정 직후 통합 뷰가 즉시 새 값을 준다. 캐시를 몰래 들이면 이 테스트가 깨진다.

**최선집행 평가 (`test_best_execution.c`, `test_be_weights.c`)**

- 네 항목 각각이 **혼자서** 승부를 가르는 장면이 따로 있다(나머지 셋을 같게 두고 한 항목만 흔든다). 예: 가격·물량이 같고 수수료만 다르면 NXT, 수수료를 뒤집으면 KRX.
- 완전 동점은 KRX, 총점 동점이면 fillable 많은 쪽.
- 제외된 시장이 이유 코드와 함께 남는다.
- 설정 파일의 가중치·수수료를 바꾸면 **같은 호가창에서 선택한 시장이 실제로 뒤집힌다**(파일이 장식이 아니다). 모르는 키·음수·합 0을 거절한다.

**전략 (`test_strategy_*.c`)**

- KRX_ONLY는 NXT가 아무리 유리해도 보지 않고, KRX가 닫히면 거부한다. 계획 불변조건(합계·한 시장 한 다리)을 여기서 굳힌다.
- BEST_PRICE는 이긴 시장으로 가고, 잔량이 모자라도 쪼개지 않으며, 체결 가능한 시장이 없어도 주문을 버리지 않는다.
- SPLIT은 손계산과 정확히 맞고, 단수가 최대 나머지 규칙으로 가며, **잔량 비율 12가지 × 수량 1~300주 전부**에서 다리 합 = 주문 수량이다.
- SWEEP은 테스트 안에서 통합 호가창을 직접 훑어 만든 "손 스윕"과 결과가 같다. 예: NXT 10,000×100, KRX 10,010×100, NXT 10,020×100에 250주 → NXT 150 / KRX 100, 금액 2,502,000원, 평균 10,008원.
- 네 전략 모두 매도 방향과 결정성(같은 입력 → 같은 계획)을 따로 확인한다.

**매핑·집행·취소 (`test_order_map.c`, `test_split_state.c`, `test_split_cancel.c`)**

- 물리 번호가 **논리 번호 1~2,000 × 전 시장**에서 한 번도 겹치지 않는다(손으로 고른 몇 개가 아니라 전수 검사).
- 초과 체결은 아무것도 바꾸지 않고 거절된다. 논리 잔량 = 원 수량 − 체결 합 − 취소 합이 많은 주문에서 성립한다.
- 한쪽 거부/양쪽 부분 체결/한쪽만 체결/전부 거부/IOC 잔량 취소 각각에서 보고서와 매핑이 같은 숫자를 말한다(`check_invariant`).
- 취소는 살아 있는 다리만 시도하고, 한쪽 실패 시 성공한 취소를 되돌리지 않으며, 재시도가 남은 다리만 취소한다.

**로그·품질·대사 (`test_routing_log.c`, `test_execution_quality.c`, `test_recon_live.c`)**

- **로그에 담긴 값만으로 총점을 다시 계산하고 시장을 다시 골라** 실제 배분과 맞는지 본다(호가 배치 4×4×수량 3 = 48장면).
- 계획을 못 세운 주문도 기록이 남고, 기본값이 아닌 기준으로 돌리면 그 기준이 남는다. 버퍼 경계 밖으로 쓰지 않는다.
- 모든 품질 기대값은 주석에 계산 과정을 적은 손계산이다(코드 출력을 베끼지 않는다). bp 반올림 대칭성을 −50~50 전 구간에서 확인한다.
- 실제 계획·체결·취소로 만든 논리 주문을 대사(core의 recon)에 넘기면 깨끗하고, 숫자 한 칸을 망가뜨리면 `RECON_NOTIONAL`/`RECON_LEG_SUM`/`RECON_DUP_PHYS`로 잡힌다.

---

#### bench/ — 속도와 전략 품질을 재는 하네스

##### 1. 한 줄 역할과 왜 필요한가

**한 줄 역할: 매칭 엔진과 주문 전 구간이 얼마나 빠른지, 그리고 네 전략 중 무엇이 더 싸게 샀는지를 재서 `bench/results/*.md`로 남긴다.**

`CLAUDE.md`는 이 프로젝트의 최종 산출물이 "동작하는 시스템이 아니라 **측정 결과**"라고 못 박는다. "집행 전략별로 평균 체결 단가가 얼마나 달랐는가"를 근거와 함께 제시하는 코드가 여기 있다.

| 실행 파일 | 소스 | 재는 것 | 결과 파일 | ctest? |
|---|---|---|---|---|
| `bench_match` | `bench_match.c` | 매칭 엔진만의 TPS·지연(T1-20) | `results/2026-09-15.md` | 아니오 |
| `bench_pipeline` | `bench_pipeline.c` | 주문 한 건의 7단계 전 구간 지연, 저널 끔/켬(T5-05) | `results/pipeline-2026-09-16.md` | 아니오 |
| `compare_strategies` | `compare_strategies.c` + `compare.c` | 시드 하나로 시나리오 4 × 전략 4 비교(T2-14) | `results/strategies-2026-09-16.md` | 아니오 |
| `quality_report` | `quality_report.c` + `quality.c` | 시드 30개로 위 실험을 반복해 분포와 승패(T5-08) | `results/quality-2026-09-16.md` | 아니오 |
| `test_compare`, `test_quality` | `tests/` | 하네스 자체의 재현성과 집계 규칙 | — | **예** |

실험 본체(`compare.c`, `quality.c`)는 `compare`라는 정적 라이브러리로 묶여 있다. 실행 파일은 얇은 `main`이고, 테스트가 같은 라이브러리를 직접 부른다.

벤치 쪽만 시스템 시각(`clock_gettime(CLOCK_MONOTONIC)`)을 읽는다. 엔진은 읽지 않는다 — "벤치는 엔진 밖이다". 그래서 CMake에서 벤치 두 개에만 `_POSIX_C_SOURCE=200809L`을 연다. 비교·품질 실험(`compare`, `quality`)은 시각을 전혀 읽지 않고 **날짜를 명령줄 인자로 받는다.** 시각을 읽으면 같은 시드로 돌린 두 실행의 산출물 파일이 달라지기 때문이다.

실행 방법(각 `main`의 사용법):

```
./build-rel/bench/bench_match [출력경로]
./build-rel/bench/bench_pipeline <날짜> [저널경로=/tmp/bench_pipeline.jrn] [출력경로] [저장매체 설명]
./build-rel/bench/compare_strategies <날짜> [출력경로] [시드]
./build-rel/bench/quality_report <날짜> [시드 개수=30] [출력경로] [첫 시드]
```

##### 2. 읽는 순서

| 순서 | 파일 | 초점 |
|---|---|---|
| 1 | `bench/CMakeLists.txt` | 무엇이 실행 파일이고 무엇이 라이브러리인가, 왜 ctest에 안 넣는가 |
| 2 | `bench/results/2026-09-15.md` → `bench/bench_match.c` | 결과를 먼저 보고 코드를 본다 |
| 3 | `bench/results/pipeline-2026-09-16.md` → `bench/bench_pipeline.c` | 7단계와 "0건 보고서" 방어 |
| 4 | `bench/compare.h` → `bench/compare.c` → `compare_strategies.c` | 전략마다 새 호가창 |
| 5 | `bench/results/strategies-2026-09-16.md` | 한 시드의 표 읽기 |
| 6 | `bench/tests/test_compare.c` | 재현성·유동성 동일성 검사 |
| 7 | `bench/quality.h` → `bench/quality.c` → `quality_report.c` | 분포·판정 규칙 |
| 8 | `bench/results/quality-2026-09-16.md` → `bench/tests/test_quality.c` | 결론 |

바깥에서 쓰는 것: `exchange/include/divergent.h`(시나리오별로 두 시장에 일부러 다른 유동성을 넣는 생성기). 시나리오는 넷이다.

| 시나리오 | 뜻(`divergent.h` 주석) |
|---|---|
| `BALANCED` | 양 시장 유사 — 라우팅이 무의미한 대조군 |
| `KRX_THIN` | KRX 유동성 부족 |
| `NXT_THIN` | NXT 유동성 부족 |
| `CROSSED` | 한쪽 최우선호가가 다른 쪽보다 유리(한쪽 기준가를 밀어낸다) |

##### 3. 핵심 자료구조

| 구조체 | 파일 | 담는 것 |
|---|---|---|
| `bench_result_t` | `bench_match.c` | TPS, p50/p95/p99/최대 지연, 총 소요, 체결 수량·금액, 거부 수, 표본 수 |
| `pass_t` | `bench_pipeline.c` | 주문 한 건의 7단계별 ns, 완주 여부, 멈춘 단계와 이유, 체결 수량, 다리 수 |
| `round_t` | `bench_pipeline.c` | 한 판(저널 끔 또는 켬)의 단계별 분위수, 전 구간 합 p50/p99, TPS, 단계별 거부 수, 다리 수 합계 |
| `compare_config_t` | `compare.h` | 시드, 기준가 10,000, 가격 범위 7,000~13,000, 시작 10:00, 시장당 유동성 400건, 측정 주문 200건, 수량 50~500주, 공격도 5틱 |
| `compare_row_t` | `compare.h` | 한 칸(시나리오×전략): 낸 수량, 체결 수량, 체결 금액, **기준가 금액**, 평균 단가, 슬리피지 bp, 체결률 bp, KRX_ONLY 대비 bp, 거부 다리 수 |
| `compare_result_t` | `compare.h` | 설정 + 4×4 칸 |
| `quality_dist_t` | `quality.h` | p50(짝수면 가운데 둘 중 낮은 쪽), min, max |
| `quality_cell_t` | `quality.h` | 세 지표의 분포, 승/패/무, 판정 |
| `quality_report_t` | `quality.h` | 기본 설정, 시드 수, 4×4 칸 |

`compare_row_t.bench_notional`은 "기준가로 다 체결됐다면 들었을 금액"의 합(주문마다 `기준가 × 그 주문의 체결 수량`)이다. 칸의 슬리피지는 `eq_to_bp(notional − bench_notional, bench_notional)`로 낸다 — sor의 "bp는 금액에서 직접" 규칙을 칸 단위로 그대로 쓴 것이다.

##### 4. 각 실행 파일이 하는 일

###### 4-1. `bench_match` — 매칭 엔진만

1. 시나리오 BALANCED, KRX_THIN, CROSSED 셋(NXT_THIN은 없다)에 대해, 시드 20260915로 `divergent` 생성기를 만든다. 가격 범위 7,000~13,000.
2. 시장마다 매칭 엔진을 만들고 KRX/NXT 규칙을 건다. 이벤트 싱크는 걸지 않는다(소비자 비용까지 재면 엔진 비용이 가려진다).
3. **예열** 시장당 20,000건 — 빈 호가창에 넣는 주문은 체결이 없어 유난히 싸서, 측정에 넣으면 TPS가 부풀고 분위수가 왜곡된다.
4. **측정** 시장당 200,000건(합 400,000건). 건마다 `match_limit()` 호출 직전·직후 시각 차이를 배열에 담는다.
5. 정렬해서 분위수를 뽑는다. `percentile(p) = sorted[floor(p × n)]` — 보간하지 않는다.
6. 마크다운으로 쓴다. CPU 이름은 `/proc/cpuinfo`에서 읽는다(손으로 적으면 기계를 바꾼 뒤에도 옛 이름이 남는다).

###### 4-2. `bench_pipeline` — 주문 한 건의 전 구간 7단계

`bench_match`의 3.8M TPS는 "그 조각"의 숫자다. 실제 주문은 원장 검증·저널·라우팅도 지나므로, 단계를 나눠 재고 **가장 비싼 단계를 이름으로 지목한다.**

| 단계 | 호출 | 하는 일 |
|---|---|---|
| 1. 전문 해석 | `msg_decode_order_req` | 바이트 → 구조체 |
| 2. 원장 검증 | `validate_order` | 계좌 잠금 + 한도 + 증거금 묶기 |
| 3. 저널 기록 | `journal_append` | 입력을 파일에 덧붙이고 `fsync`(켠 판에서만) |
| 4. SOR 계획 | `routing_plan(BEST_PRICE, ..., sink=NULL)` | 시장별 몫 결정 |
| 5. 물리 등록 | `omap_register` | 논리 → 물리 다리 |
| 6. 매칭 | 다리마다 `match_limit` | 체결 |
| 7. 체결 반영 | `omap_on_accept`, `omap_on_fill` | 물리 체결 → 논리 합산 |

설정: 예열 2,000건, 측정 20,000건, 시장당 유동성 40,000건(BALANCED, 가격 9,000~11,000), 시드 20260916, 주문은 매수 지정가 **10,000 + 20원**, 10~50주(splitmix64 난수). 계좌 하나에 1조 원을 넣고 증거금률 40%, 한도는 보지 않는다.

같은 과정을 **저널 끔 → 저널 켬** 두 판 돈다. 판마다 호가창·계좌를 새로 만든다.

코드에 남은 교훈 두 가지:

- **호가 단위.** 처음에 +15원으로 뒀더니 10원 단위에 안 맞아 2만 건이 전부 원장에서 거부됐는데, 프로그램은 성공으로 끝나며 "TPS 2300만"이라는 표를 냈다. 그것은 거부 루프의 속도였다. 그래서 공격도를 상수 `AGGRESSION 20`으로 두고(코드와 보고서가 같은 값을 쓰게), **측정 건수가 0이면 어느 단계에서 몇 건이 왜 떨어졌는지 찍고 실패로 끝낸다.**
- **디코드 반환값.** `msg_decode_order_req`는 성공하면 0이 아니라 **읽은 바이트 수**를 돌려준다. `!= ERR_OK`로 검사하면 성공한 건이 전부 거부로 잡힌다(실제로 그랬다). 그래서 `rc < 0`으로 본다.

거부된 건은 분위수에서 뺀다(짧은 경로가 p50을 끌어내리기 때문). 단계별 거부 수와 첫 이유는 보고서에 숨기지 않는다.

표본 버퍼 `g_samp`, `g_total`은 **전역 변수**다. 주석이 "전역 가변 상태 금지의 예외 — 측정 도구의 표본 버퍼, 스택에 두기엔 크고 엔진이 아니라 결정성과 상관없다"고 이유를 적어 둔다.

###### 4-3. `compare_strategies` — 네 전략 비교(시드 하나)

`compare_run()` 흐름:

1. 설정 검사(측정 주문 > 0, 유동성 > 0, 시드 ≠ 0, 수량 범위).
2. 시나리오 4개 × 전략 4개, **칸마다** `run_cell()`:
   1. `vs_build()` — **매칭 엔진을 새로 만들고 같은 시드로 유동성을 다시 채운다.** 시장 규칙은 걸지 않는다(세션 규칙을 걸면 "어느 시장이 열려 있었나"가 단가 차이에 섞인다). 매핑도 새로 만든다.
   2. 측정 주문 생성기(`taker_gen_t`)를 같은 시드로 초기화한다 → 네 전략이 **같은 주문 열**을 받는다. 주문은 매수, 지정가 = 기준가 + 호가 단위 × 5(틱에 맞춤), 수량 50~500, 번호는 2억부터, 시각은 1ms씩 증가.
   3. 주문마다: `eq_benchmark()`로 **집행 전에** 기준가를 잡는다(상대 호가가 없으면 그 주문은 건너뛴다) → `routing_plan()` → `exec_submit()` → 칸에 수량·금액·기준가 금액·거부 다리 수를 더한다.
   4. 칸 요약: 평균 단가, 체결률, 슬리피지 bp.
3. 시나리오마다 `vs_krx_only_bp = eq_avg_diff_bp(기준선 금액·수량, 이 전략 금액·수량)`. **원 단위 평균끼리 빼지 않는다**(T6-07).

측정 주문 생성기는 유동성 생성기를 쓰지 않는다. 유동성 생성기는 기준가 아래에 매수, 위에 매도를 놓아 교차하지 않는 호가를 만드는데, 여기서 필요한 것은 반대로 호가를 **파고드는** 주문이다. 5틱 위를 지정가로 잡는 이유도 여러 단을 파고들어야 전략 차이가 드러나기 때문이다.

###### 4-4. `quality_report` — 시드 30개

T2-14의 표는 "한 시드의 한 장면"이다. "+5bp"가 시드를 바꿔도 +5인지 알 수 없다. 그래서:

1. `base.seed`부터 **1씩 늘린** 시드로 `compare_run()`을 30번 돈다(이웃 시드가 닮은 수열을 만들지 않는 것은 생성기들이 시드를 섞어 쓰기 때문이다).
2. 칸마다 세 지표(KRX_ONLY 대비 bp, 슬리피지 bp, 체결률 bp)를 시드별로 모으고, `KRX_ONLY 대비 > 0`이면 승, `< 0`이면 패, `== 0`이면 무를 센다.
3. `quality_dist()` — 정렬해서 `min = [0]`, `max = [n−1]`, `p50 = [(n−1)/2]`. 30개면 인덱스 14, 즉 **작은 쪽에서 15번째 값**이다.
4. `quality_verdict_of(승, 패, 무)`로 판정한다.
5. 마크다운의 "결론" 문장을 **표에서 자동으로 뽑아** 쓴다(손으로 쓴 결론이 아니다).

**평균을 쓰지 않는 이유**(`quality.h`) — 평균 +2bp는 "30번 모두 +2"일 수도 "29번 +5에 한 번 −85"일 수도 있다. 둘은 전혀 다른 전략이다.

**짝수 개 중앙값을 낮은 쪽으로 고르는 이유** — 가운데 둘의 평균은 정수가 아닐 수 있어 반올림 규칙이 하나 더 필요하고, **어느 시드에서도 실제로 나오지 않은 숫자**가 된다. 한쪽을 고르면 표의 중앙값은 언제나 실제 관측값이다. KRX_ONLY 대비와 체결률은 클수록 좋은 값이라 낮은 쪽을 고르면 개선폭을 부풀리지 않는다.

##### 5. 결과 파일 읽는 법

###### 5-1. 먼저 알아 둘 숫자 용어

| 용어 | 뜻 | 예 |
|---|---|---|
| **TPS** | Transactions Per Second. 초당 처리 건수 = 처리 건수 / 걸린 초 | 400,000건 / 0.106초 ≈ 3,789,251 |
| **ns** | 나노초 = 10억분의 1초 | 202ns = 0.000000202초 |
| **p50** | 50번째 백분위수(중앙값). 전체의 절반은 이보다 빨랐다 | "보통 주문은 이만큼 걸린다" |
| **p95 / p99** | 95%, 99%가 이보다 빨랐다. 나머지 5%, 1%가 **꼬리**다 | "운영에서 아픈 것은 평균이 아니라 p99다"(주석) |
| **최대** | 가장 느렸던 한 건. OS 스케줄링 같은 우연이 섞여 들쭉날쭉하다 | |
| **bp** | basis point = 0.01% = 1/10,000 | 10,000원의 1bp = 1원 |

**왜 평균이 아니라 분위수인가.** 매칭 비용이 균일하지 않다. 대부분의 주문은 호가창에 등록만 되고 끝나지만, 여러 가격 단을 가로지르며 체결되는 주문은 수십 배 걸린다. 평균은 그 꼬리를 감춘다.

**왜 분위수는 더해지지 않는가.** 단계별 p50은 **서로 다른 주문의** 중앙값이다. 1단계에서 중간인 주문과 4단계에서 중간인 주문은 다른 주문일 수 있다. "전 구간 합"은 주문마다 일곱 단계를 먼저 더한 뒤 그 분포에서 뽑은 값이라 뜻이 다르다. 파이프라인 결과에서 확인하면:

```
저널 끔: 단계별 p50 합 = 18 + 37 + 178 + 30 + 50 + 21 = 334ns   ≠  전 구간 합 p50 351ns
저널 켬: 단계별 p50 합 = 205 + 893 + 2,651,599 + 2,006 + 656 + 1,043 + 149 = 2,656,551ns
                                                                  ≠  전 구간 합 p50 2,657,903ns
```

p99는 차이가 더 크다. 한 주문이 모든 단계에서 동시에 꼬리에 있을 가능성은 낮기 때문에, 단계별 p99를 더하면 실제 전 구간 p99보다 훨씬 커진다.

###### 5-2. `2026-09-15.md` — 매칭 엔진만 (`bench_match`)

조건: Intel Core Ultra 9 285H, Release(`-O3`), 단일 스레드, 예열 시장당 20,000건, 측정 시장당 200,000건(합 400,000건), 지정가, 싱크 없음, 시드 20260915. 지연에는 측정 자체의 비용(`clock_gettime` 호출)이 포함된다.

| 시나리오 | 소요 | TPS | p50 | p95 | p99 | 최대 | 체결 수량 | 평균 단가 | 거부 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| BALANCED | 0.106초 | 3,789,251 | 202ns | 305ns | 423ns | 896,551ns | 20,074,677주 | 10,000원 | 0 |
| KRX_THIN | 0.100초 | 4,004,004 | 186ns | 314ns | 501ns | 637,162ns | 10,429,858주 | 10,000원 | 0 |
| CROSSED | 0.105초 | 3,822,613 | 216ns | 308ns | 449ns | 230,745ns | 39,606,071주 | 10,099원 | 0 |

읽는 법(결과 파일에 적힌 그대로):

- p50과 p99의 차이가 곧 "체결이 붙은 주문"의 비용이다. 대부분의 주문은 등록만 되고 끝난다.
- KRX_THIN이 가장 빠른 것은 호가가 성기어 체결이 덜 일어났기 때문이다. **빠른 것이 아니라 일을 덜 한 것이다.** 체결 수량이 BALANCED의 약 절반(10,429,858주)이라는 것이 그 증거다.
- 평균 체결 단가는 시나리오 간 비교용이며 절대값에는 의미가 없다.

###### 5-3. `pipeline-2026-09-16.md` — 전 구간 7단계 (`bench_pipeline`)

조건: 같은 CPU, Release, 단일 스레드·단일 프로세스, 예열 2,000건, 측정 20,000건, 유동성 시장당 40,000건(BALANCED, 시드 20260916), BEST_PRICE, 매수 지정가 10,000원 +20원, 10~50주, 저널 `/tmp/bench_pipeline.jrn`(WSL2 ext4, VHDX on NVMe). **계층을 별도 프로세스로 띄우지 않았으므로 전문 송수신과 프로세스 경계 비용은 빠져 있다.**

**저널 끔** (단위 ns)

| 단계 | p50 | p95 | p99 | 최대 |
|---|---:|---:|---:|---:|
| 1. 전문 해석 | 18 | 20 | 30 | 244 |
| 2. 원장 검증 | 37 | 40 | 44 | 15,139 |
| 3. 저널 기록 | — | — | — | — |
| 4. SOR 계획 | 178 | 189 | 202 | 10,785 |
| 5. 물리 등록 | 30 | 275 | 1,010 | 637,592 |
| 6. 매칭 | 50 | 296 | 345 | 12,679 |
| 7. 체결 반영 | 21 | 26 | 41 | 12,707 |
| **전 구간 합** | **351** | | **1,452** | |

측정 20,000건, 거부 0건, 0.013초, **TPS 1,534,801**, 체결 599,847주, 다리 평균 1.00개, **가장 비싼 단계: 4. SOR 계획(p50 178ns, 전 구간의 50.7%)**.

**저널 켬** (단위 ns)

| 단계 | p50 | p95 | p99 | 최대 |
|---|---:|---:|---:|---:|
| 1. 전문 해석 | 205 | 432 | 555 | 29,877 |
| 2. 원장 검증 | 893 | 2,289 | 3,345 | 92,077 |
| 3. 저널 기록 | 2,651,599 | 3,635,695 | 4,452,108 | 17,028,951 |
| 4. SOR 계획 | 2,006 | 4,510 | 6,592 | 102,971 |
| 5. 물리 등록 | 656 | 1,957 | 37,446 | 1,371,329 |
| 6. 매칭 | 1,043 | 2,093 | 3,031 | 1,418,539 |
| 7. 체결 반영 | 149 | 387 | 507 | 102,506 |
| **전 구간 합** | **2,657,903** | | **4,459,914** | |

측정 20,000건, 거부 0건, 53.133초, **TPS 376**, 체결 599,847주, 다리 평균 1.00개, **가장 비싼 단계: 3. 저널 기록(p50 2,651,599ns, 전 구간의 99.8%)**.

| | 저널 끔 | 저널 켬 | 배수 |
|---|---:|---:|---:|
| 전 구간 p50 | 351ns | 2,657,903ns | 7,572.4배 |
| TPS | 1,534,801 | 376 | 0.000배 |

**왜 fsync 하나로 TPS가 약 150만에서 376으로 떨어지는가**

- `fsync`는 "방금 쓴 내용이 디스크에 실제로 내려갈 때까지 기다려라"는 운영체제 호출이다. 전원이 나가도 기록이 남게 하는 대신, 저장 장치가 확인해 줄 때까지 프로그램이 멈춰 선다.
- 저널은 **매 레코드마다** `fsync`한다(T5-01의 선택. "잃으면 안 되는 기록"이라서).
- 저널 기록 p50이 2,651,599ns ≈ **2.65밀리초**다. 나머지 여섯 단계를 다 합쳐도 저널 끈 판에서 351ns, 즉 0.00035밀리초다. 한 건이 약 2.66밀리초 걸리면 1초에 처리할 수 있는 건수는 1,000,000,000 / 2,657,903 ≈ **376건**이다. 표의 TPS와 정확히 맞는다.
- 반대로 저널을 끄면 한 건에 약 351ns가 드니 이론상 1초에 약 285만 건인데, 실측 TPS 1,534,801은 그보다 낮다. TPS는 벽시계 시간(루프 전체, 시각 측정 호출 포함)으로 재고 분위수는 단계 호출만 재기 때문에 둘이 정확히 같지는 않다.
- 결과 파일의 해석 3번: **fsync의 비용은 자기 단계에만 머물지 않는다.** 저널을 켜자 매칭도 50ns → 1,043ns로 느려졌다. 저널을 쓰는 동안 프로세스가 내려갔다 올라오면서 캐시가 식기 때문이다. "저널만 빼면 나머지는 그대로"라고 읽으면 안 된다.
- 해석 4번: 이 숫자는 그 저장 매체의 것이다. 다른 장비에서는 배수가 바뀌지만, 동기 `fsync`가 있는 한 이 계층이 전 구간을 지배한다는 결론은 같다.

**다리 평균 1.00개의 의미.** BEST_PRICE는 한 시장만 고르므로 쪼개지 않는다. 그래서 매칭·체결 반영 숫자는 **한 시장에 보낸 주문의 비용**이다. SPLIT/SWEEP이면 이 두 단계가 다리 수만큼 늘어난다. 코드 주석은 이 사실이 **변이 검사(P6)**에서 드러났다고 적는다 — "다리마다 반복하는 경로가 이 워크로드에서 한 번도 돌지 않는다"(7장 참조).

**결론("그래서 무엇을 할 것인가")**: 지금은 아무것도 바꾸지 않는다. 376 TPS는 이 프로젝트의 목표(집행 전략 비교)에 모자라지 않는다. 바꿔야 할 때의 선택지는 셋이고 모두 내구성을 깎지 않는다 — 그룹 커밋(여러 주문을 모아 `fsync` 한 번), 저널 전용 스레드, 배터리 백업 캐시가 있는 더 빠른 장치. **재기 전에 고르지 않는다.**

###### 5-4. `strategies-2026-09-16.md` — 한 시드의 전략 비교

설정: 시드 20260916, 기준가 10,000, 시장당 유동성 400건, 측정 주문 200건(매수만), 수량 50~500주, 지정가 = 기준가 + 5틱. 기준가(benchmark)는 접수 시점의 통합 최우선 매도호가.

| 시나리오 | 전략 | 평균 체결 단가 | 슬리피지(bp) | 체결률 | KRX_ONLY 대비(bp) |
|---|---|---:|---:|---:|---:|
| BALANCED | KRX_ONLY | 10019 | +11 | 53.46% | +0 |
| BALANCED | BEST_PRICE | 10014 | +0 | 100.00% | +5 |
| BALANCED | SPLIT | 10014 | +1 | 100.00% | +5 |
| BALANCED | SWEEP | 10014 | +0 | 100.00% | +5 |
| KRX_THIN | KRX_ONLY | 10023 | +15 | 5.36% | +0 |
| KRX_THIN | BEST_PRICE | 10020 | +0 | 72.83% | +3 |
| KRX_THIN | SPLIT | 10020 | +2 | 72.83% | +3 |
| KRX_THIN | SWEEP | 10020 | +0 | 72.83% | +3 |
| NXT_THIN | KRX_ONLY | 10019 | +11 | 53.46% | +0 |
| NXT_THIN | BEST_PRICE | 10020 | +0 | 58.71% | -1 |
| NXT_THIN | SPLIT | 10020 | +1 | 58.71% | -1 |
| NXT_THIN | SWEEP | 10020 | +0 | 58.71% | -1 |
| CROSSED | KRX_ONLY | 10013 | +0 | 21.28% | +0 |
| CROSSED | BEST_PRICE | 10013 | +0 | 21.28% | +0 |
| CROSSED | SPLIT | 10013 | +0 | 21.28% | +0 |
| CROSSED | SWEEP | 10013 | +0 | 21.28% | +0 |

**세 숫자의 부호**

| 열 | 양수의 뜻 | 기준 |
|---|---|---|
| 슬리피지(bp) | 그만큼 **불리하게** 샀다 | 주문마다 접수 시점 통합 최우선호가 |
| KRX_ONLY 대비(bp) | 그만큼 **싸게** 샀다(이득) | 기준선의 평균 체결 단가 |
| 체결률 | 낸 수량 중 체결 비율 | |

**체결률을 같이 봐야 하는 이유.** 단가가 좋아도 체결률이 낮으면 남은 수량을 나중에 더 비싸게 사게 된다. KRX_THIN의 KRX_ONLY는 슬리피지 +15bp에 체결률이 **5.36%**뿐이다 — 거의 못 샀다.

**표에서 읽을 수 있는 것**

- **BALANCED**: 세 SOR 전략이 KRX_ONLY보다 평균 5bp 싸게 샀고(10,014 대 10,019), 체결률은 53.46% → 100.00%.
- **KRX_THIN**: 3bp 싸게, 체결률 5.36% → 72.83%. KRX가 얇을 때 NXT로 가는 것의 이득이 가장 크다.
- **NXT_THIN**: 평균 단가는 1bp 불리하지만(10,020 대 10,019) 체결률은 53.46% → 58.71%로 높다. 단가 한 숫자만 보면 "졌다"지만 더 많이 샀다는 점을 함께 읽어야 한다.
- **CROSSED**: 네 전략이 전부 같다.
- KRX_ONLY의 체결률이 BALANCED와 NXT_THIN에서 똑같이 53.46%다. KRX_ONLY는 NXT를 보지 않으니 NXT 쪽 유동성이 바뀌어도 체결 결과가 달라지지 않는 것과 일치한다.

**"이 실험이 말하지 못하는 것"(파일에 적힌 한계)**

- **매수만 낸다.** 매수와 매도의 평균 단가를 한 숫자로 합치면 서로 상쇄한다. 그래서 CROSSED처럼 한쪽 기준가가 밀린 시나리오에서 밀린 시장은 **불리한 쪽으로만** 나타나고, 그 시장이 유리해지는 국면은 표에 없다.
- **세 SOR 전략의 체결률·KRX_ONLY 대비가 같은 것은 우연도 반올림도 아니다.** 셋 다 양 시장에 접근하므로 지정가 안에서 가져갈 수 있는 물량이 같고, 200건을 연달아 내는 동안 결국 **같은 호가를 다 먹는다.** 체결 금액을 원 단위로 보면 KRX_THIN·NXT_THIN·CROSSED는 셋이 완전히 같고, BALANCED는 5억 원 중 수십~수천 원 차이(0.01bp 미만)다. 셋의 차이는 **어떤 순서로 채웠는가**이고, 그것은 평균 단가가 아니라 **주문마다 잰 슬리피지**(0~2bp)에서 드러난다. SPLIT만 +1~+2bp인 것은, 가격이 아니라 잔량 비율로 나누기 때문에 한 시장의 비싼 단을 다른 시장의 싼 단보다 먼저 먹는 주문이 생긴다는 SPLIT의 정의(4-6절, `test_sweep_is_not_worse_than_split`)와 맞는다.
- **세션 규칙을 걸지 않았다.** 개장 시간 영향은 T1-13/T1-14가 따로 검증한다.
- **한 시드의 한 장면이다.** 일반화하려면 시드를 바꿔 여러 번 돌려야 한다 → 그래서 `quality_report`가 있다.

###### 5-5. `quality-2026-09-16.md` — 시드 30개

설정: 시드 20260916 ~ 20260945(30개), 나머지는 위와 같다.

| 시나리오 | 전략 | KRX_ONLY 대비 p50 (min ~ max) | 승/패/무 | 판정 | 슬리피지 p50 (min ~ max) | 체결률 p50 (min ~ max) |
|---|---|---:|---:|---|---:|---:|
| BALANCED | KRX_ONLY | +0 (+0 ~ +0) | 0/0/30 | 기준선 | +12 (+6 ~ +23) | 52.31% (40.43% ~ 70.91%) |
| BALANCED | BEST_PRICE | +2 (-4 ~ +6) | 22/5/3 | 엇갈림 | +0 (+0 ~ +0) | 100.00% (89.04% ~ 100.00%) |
| BALANCED | SPLIT | +2 (-4 ~ +6) | 22/5/3 | 엇갈림 | +1 (+0 ~ +4) | 100.00% (89.04% ~ 100.00%) |
| BALANCED | SWEEP | +2 (-4 ~ +6) | 22/5/3 | 엇갈림 | +0 (+0 ~ +0) | 100.00% (89.04% ~ 100.00%) |
| KRX_THIN | KRX_ONLY | +0 (+0 ~ +0) | 0/0/30 | 기준선 | +18 (+12 ~ +31) | 5.27% (4.01% ~ 7.66%) |
| KRX_THIN | BEST_PRICE | +6 (+1 ~ +12) | 30/0/0 | 진 적 없음 | +0 (+0 ~ +1) | 59.46% (44.61% ~ 72.83%) |
| KRX_THIN | SPLIT | +6 (+1 ~ +12) | 30/0/0 | 진 적 없음 | +1 (+0 ~ +3) | 59.46% (44.61% ~ 72.83%) |
| KRX_THIN | SWEEP | +6 (+1 ~ +12) | 30/0/0 | 진 적 없음 | +0 (+0 ~ +0) | 59.46% (44.61% ~ 72.83%) |
| NXT_THIN | KRX_ONLY | +0 (+0 ~ +0) | 0/0/30 | 기준선 | +12 (+8 ~ +24) | 52.31% (40.43% ~ 70.91%) |
| NXT_THIN | BEST_PRICE | -1 (-1 ~ +0) | 0/25/5 | 이긴 적 없음 | +0 (+0 ~ +1) | 57.63% (45.14% ~ 76.07%) |
| NXT_THIN | SPLIT | -1 (-1 ~ +0) | 0/25/5 | 이긴 적 없음 | +1 (+1 ~ +2) | 57.63% (45.14% ~ 76.07%) |
| NXT_THIN | SWEEP | -1 (-1 ~ +0) | 0/25/5 | 이긴 적 없음 | +0 (+0 ~ +1) | 57.63% (45.14% ~ 76.07%) |
| CROSSED | KRX_ONLY | +0 (+0 ~ +0) | 0/0/30 | 기준선 | +0 (+0 ~ +1) | 21.67% (12.77% ~ 44.81%) |
| CROSSED | BEST_PRICE | +0 (+0 ~ +0) | 0/0/30 | 차이 없음 | +0 (+0 ~ +1) | 21.67% (12.77% ~ 44.81%) |
| CROSSED | SPLIT | +0 (+0 ~ +0) | 0/0/30 | 차이 없음 | +0 (+0 ~ +1) | 21.67% (12.77% ~ 44.81%) |
| CROSSED | SWEEP | +0 (+0 ~ +0) | 0/0/30 | 차이 없음 | +0 (+0 ~ +1) | 21.67% (12.77% ~ 44.81%) |

**승/패/무와 판정 라벨**

"승"은 그 시드에서 KRX_ONLY 대비가 양수(더 싸게 샀다), "패"는 음수, "무"는 0이다. 판정은 `quality_verdict_of()`가 이 규칙으로 낸다.

| 판정 | 조건 | 예 |
|---|---|---|
| **진 적 없음** | 이긴 적 있음, 진 적 없음(무는 섞여도 된다) | 30/0/0, 29/0/1 |
| **이긴 적 없음** | 진 적 있음, 이긴 적 없음 | 0/25/5, 0/20/10 |
| **엇갈림** | 이긴 적도 진 적도 있음 | 22/5/3, 29/1/0 |
| **차이 없음** | 둘 다 없음(전부 무). 기준선 자신이 여기 온다 | 0/0/30 |

**비김은 판정을 뒤집지 않는다**(T6-02). 처음에는 "항상 우위"를 비김이 하나도 없을 때만 주고 나머지를 전부 "엇갈림"에 넣었다. 그러자 29승 0패 1무와 0승 20패 10무가 둘 다 "엇갈림"이 되어, 한 번도 안 진 전략이 들쭉날쭉한 것처럼 읽혔다. 과장을 피하려다 반대 방향으로 틀린 것이다. 그래서 "항상"이라는 말을 버리고 실제로 일어난 일만 이름으로 쓴다. 판정만 보지 말고 승/패/무를 함께 본다.

**결론(파일의 자동 생성 문장)**

- **BALANCED** — 엇갈림: 세 전략 모두 22승 5패 3무, 중앙값 +2bp.
- **KRX_THIN** — 진 적 없음: 세 전략 모두 30승 0패 0무, 중앙값 +6bp.
- **NXT_THIN** — 이긴 적 없음: 세 전략 모두 0승 25패 5무, 중앙값 −1bp.
- **CROSSED** — 차이 없음.
- 기준선을 뺀 12칸 중 진 적 없음 3, 엇갈림 3, 이긴 적 없음 3, 차이 없음 3.

**쉬운 말로 풀면**

- KRX가 얇을 때(KRX_THIN) SOR은 30번 중 30번 KRX_ONLY보다 싸게 샀고(중앙값 6bp), 체결률도 5.27%에서 59.46%로 크게 올랐다. 이 프로젝트에서 SOR의 이득이 가장 분명한 장면이다.
- 두 시장이 비슷할 때(BALANCED) 대체로 이기지만(22번) 지는 시드도 있다(5번, 최저 −4bp). 대신 체결률은 중앙값 기준 52.31%에서 100.00%로 오른다.
- NXT가 얇을 때(NXT_THIN) 평균 단가로는 한 번도 못 이겼다(최대 0, 중앙값 −1bp). 그래도 체결률은 52.31% → 57.63%로 높다. 단가만 보고 "SOR이 손해"라고 결론 내리면 안 되는 칸이다.
- CROSSED는 30개 시드 모두 네 전략이 같다. 매수만 내는 실험의 한계 때문에 이 시나리오는 한쪽 국면만 보인다(5-4절).
- 세 SOR 전략이 칸마다 같은 승패를 보이는 것은 bp 해상도 때문이 아니라, 셋이 실제로 같은 호가를 다 먹어 체결 금액이 같거나 0.01bp 미만으로만 다르기 때문이다(파일의 설명). 셋의 차이는 슬리피지 열(SPLIT만 +1)에서 보인다.

**리포트가 말하지 못하는 것** — T2-14의 한계(매수만, 세션 규칙 없음)를 그대로 물려받는다. 0.5bp 미만 차이는 0(무)이 된다. 시드만 바꿨고 기준가·주문 크기·공격도는 고정이다.

##### 6. 테스트가 보장하는 것 — bench/

**`test_compare.c` (하네스의 재현성)**

- **같은 시드 → 바이트까지 같은 표**(`memcmp`로 칸 전체 비교).
- 다른 시드 → 다른 표. 그리고 시드가 **두 생성기 모두**에 쓰이는지 따로 떼어 본다: 낸 수량 합은 측정 주문 난수만으로 정해지므로 시드를 바꾸면 달라져야 하고, 수량 범위를 100~100으로 고정해 측정 주문을 완전히 같게 만든 뒤에도 표가 달라지면 그 차이는 유동성 생성기에서 온 것이다.
- **유동성이 전략마다 같다** — 네 전략의 낸 수량이 칸마다 같다(호가창을 공유했다면 어긋난다). 기준선 열의 KRX_ONLY 대비는 0이다.
- `vs_krx_only_bp`가 `eq_avg_diff_bp`(금액에서 직접)와 같고, 시드 30개 중 **옛 식(버린 평균끼리)과 값이 갈리는 칸을 적어도 한 번 봤다**는 것까지 확인한다. 갈리는 칸이 없으면 이 대조가 옛 버그를 잡을 수 없기 때문이다.

**`test_quality.c` (집계 규칙)**

- 분포가 손으로 센 값과 같다: `{5, −1, 3}` → p50 3, `{4, 1, 3, 2}` → p50 2(2와 3 중 낮은 쪽).
- 판정 규칙: 29/0/1은 진 적 없음, 0/20/10은 이긴 적 없음, 29/1/0·1/1/28은 엇갈림.
- 시드 3개 집계가 `compare_run()`을 3번 직접 돌린 값과 칸마다 같다(중앙값을 `합 − 최소 − 최대`로 따로 계산해 대조). 기준선은 언제나 차이 없음.
- 같은 인자 → 바이트까지 같은 파일. 시드 개수 범위와 시드 넘침을 거절한다.

---

#### sdk/ — 바깥 전략 엔진용 주문 SDK

##### 1. 한 줄 역할과 왜 필요한가

**한 줄 역할: 시뮬레이터 바깥에서 붙는 전략 엔진이 주문·취소·정정 전문(바이트)을 만들고, 돌아오는 응답·체결을 "내가 낸 그 주문"에 이어 붙여 상태를 추적하게 해 준다.**

T5-06이 시세를 **읽게** 했다면 이것은 주문을 **내게** 한다(T5-07). 파일은 `sdk/order_sdk.h`, `sdk/order_sdk.c` 둘뿐이다.

헤더의 네 가지 설계 원칙:

| 원칙 | 이유 |
|---|---|
| **`core`만 의존한다** | `exchange`·`sor`·`ledger`를 링크하면 전략 엔진이 시뮬레이터 내부를 통째로 끌고 가야 한다. 붙이는 쪽이 알아야 할 것은 전문 형식뿐이다. 그래서 `core/` 안이 아니라 최상위 `sdk/`에 둔다 |
| **주문번호는 SDK가 발급한다** | 전략이 직접 매기면 겹치거나 빠지고, 그 사실을 응답이 오고 나서야 안다. 겹친 번호로 온 체결은 어느 주문 것인지 정할 수 없다 |
| **보낸 것을 기억한다** | 응답은 번호로 오지만 전략이 알고 싶은 것은 "내 주문"의 종목·방향·수량·체결·평균 단가다. **떠 있는 주문 수를 셀 수 있어야** 전략이 자기 위험을 계산한다 |
| **모르는 번호를 조용히 버리지 않는다** | 조용히 버리면 체결이 사라진 것을 아무도 모른다. 자리가 차도 **덮어쓰지 않고 거절한다** |

그리고 SDK는 **보내지 않는다.** 전문 바이트를 버퍼에 써 줄 뿐이고 소켓은 부르는 쪽이 쥔다. SDK가 소켓을 쥐면 전략이 자기 이벤트 루프를 못 쓴다.

##### 2. 읽는 순서

1. `sdk/CMakeLists.txt` — `order_sdk`가 `core`에만 링크된다는 것.
2. `sdk/order_sdk.h` — 머리 주석(원칙), `sdk_ord_state_t`, `sdk_order_t`, 함수 목록.
3. `core/include/msg.h`(바깥) — `msg_order_req_t`, `msg_order_ack_t`, `msg_cancel_ack_t`, `msg_modify_ack_t`, `msg_fill_noti_t`와 `msg_encode_*`.
4. `sdk/order_sdk.c` — 찾기 → 내보내기 → 받아들이기 → 보기 순서로 나뉘어 있다.
5. `sdk/tests/test_order_sdk.c`.

##### 3. 핵심 자료구조

```c
#define SDK_ORDERS_MAX 1024   // 동시에 담을 주문 수
#define SDK_BODY_MAX   64     // 전문 바디 버퍼 크기 (헤더는 부르는 쪽이 쓴다)

typedef enum {
    SDK_ORD_NONE = 0,  // 빈 자리
    SDK_ORD_PENDING,   // 보냈고 응답을 기다린다
    SDK_ORD_LIVE,      // 받아들여졌고 잔량이 있다
    SDK_ORD_DONE       // 끝났다 — 전량 체결, 취소, 또는 거부
} sdk_ord_state_t;

typedef struct {
    uint64_t        cl_ord_id;   // SDK가 발급한 번호 (내 번호)
    order_id_t      order_id;    // 상대가 붙인 번호. 응답 전에는 0
    sdk_ord_state_t state;
    char    symbol[...];
    side_t  side;  price_t price;  qty_t qty;
    qty_t   filled_qty;
    int64_t notional;            // 평균 단가 = notional / filled_qty
    qty_t   canceled_qty;
    int     reject_reason;
    ts_t    sent_ts;             // 호출부가 준 논리 시각
} sdk_order_t;
```

`sdk_t` 내부(`order_sdk.c`의 `struct sdk`)는 계좌번호, **다음에 발급할 번호**, 고정 크기 배열 `ord[1024]`, 쓰는 자리 수 `count`, 모르는 번호 응답 수 `orphans`다. 번호 찾기는 1024칸을 훑는 **선형 탐색**이다. `ponytail:` 주석이 한계를 밝힌다 — 전략 하나가 동시에 수천 건을 띄우면 문제가 되겠지만, 재 본 적이 없으므로 지금은 자료구조를 두지 않는다.

**첫 번호를 인자로 받는 이유**(`sdk_create(account, first_cl_ord_id)`) — 재기동해도 앞서 쓴 번호를 다시 쓰면 안 된다. 어디서부터 이어야 하는지는 저널이나 원장을 가진 부르는 쪽이 안다. SDK가 1부터 시작하면 재기동이 곧 번호 충돌이다. 0은 "없음"이라 첫 번호로 받지 않는다.

##### 4. 상태 기계와 함수 흐름

```
                sdk_new_order()
   NONE ───────────────────────────▶ PENDING
    ▲                                  │  │
    │                  order_ack(성공)  │  │ order_ack(거부)
    │                  또는 fill 도착  │  │
    │                                  ▼  ▼
    │                                LIVE  DONE ◀─┐
    │                                  │          │
    │            체결+취소 ≥ 낸 수량   │          │
    │            (settle)              └──────────┘
    │
    └──────────── sdk_reap_done()  (DONE인 자리만 비운다)
```

| 사건 | 함수 | 상태 변화와 기록 |
|---|---|---|
| 주문 만들기 | `sdk_new_order` | 빈 자리를 찾는다(없으면 `ERR_POOL_EXHAUSTED`, 덮어쓰지 않음). 전문을 인코딩하고, **성공한 뒤에만** 자리를 PENDING으로 채우고 번호를 1 올린다. 인코딩이 실패하면 자리는 NONE 그대로 — 보내지도 않은 주문이 자리를 먹지 않는다. 반환값은 쓴 바이트 수 |
| 취소 만들기 | `sdk_cancel` | 모르는 번호 `ERR_NOT_FOUND`, **DONE이면 `ERR_NOT_SUPPORTED`**(끝난 주문을 건드리려는 것은 전략이 상태를 잘못 안다는 뜻). PENDING이라 상대 번호를 모르면 `order_id = 0`을 보낸다 — "우리 번호로 찾으라"는 약속 |
| 정정 만들기 | `sdk_modify` | 규칙은 취소와 같다. **여기서 가격·수량을 바꾸지 않는다**(상대가 받아 줬는지 모른다) |
| 주문 응답 | `sdk_on_order_ack` | 내 번호로 찾는다(없으면 `orphans++`, `ERR_NOT_FOUND`). 상대 번호를 기록한다. 거부면 `reject_reason` 기록 후 DONE. 성공이면 PENDING → LIVE. 응답의 체결 수량은 **누적치**라 **더하지 않고 큰 쪽을 취한다**. 그 뒤 `settle` |
| 취소 응답 | `sdk_on_cancel_ack` | 내 번호 → 없으면 상대 번호로 찾는다. 취소가 거부됐으면 **주문은 그대로 살아 있다**(취소 실패를 주문 실패로 읽으면 있지도 않은 잔량을 잃는다). 성공이면 취소 수량을 큰 쪽으로 반영 후 `settle` |
| 정정 응답 | `sdk_on_modify_ack` | 거부면 옛 값 그대로. **받아들여졌을 때에만** 반영한다(코드는 응답의 `price`를 반영한다) |
| 체결 통보 | `sdk_on_fill` | 내 번호 → 상대 번호로 찾는다(없으면 `orphans++`). 수량 ≤ 0 거절. 체결 수량과 금액(가격×수량)을 **더한다.** 응답보다 체결이 먼저 오면 PENDING → LIVE. 그 뒤 `settle` |
| 끝났는지 판정 | `settle`(내부) | `filled + canceled ≥ qty`면 DONE. **한 군데서만 정해야 상태가 갈라지지 않는다** |
| 비우기 | `sdk_reap_done` | DONE 자리를 0으로 밀고 `count`를 줄인다. 자동으로 하지 않는다 — 끝난 주문을 언제까지 볼지는 전략이 정한다 |

보기 함수: `sdk_get`(내 번호), `sdk_get_by_order_id`(상대 번호), `sdk_pending_count`(**응답을 기다리는 수** — 전략이 위험을 세는 값), `sdk_live_count`, `sdk_count`(끝난 것 포함), `sdk_remaining`(음수면 0으로 막는다), `sdk_avg_price`(`notional / filled_qty`, 체결 없으면 0), `sdk_orphans`.

**숫자 예시 — 부분 체결 누적(테스트 값)**

```
주문: 매수 500주 @68,400 (첫 번호 700,000,001)          → PENDING, pending_count 1
응답: 상대 번호 5,551,212, 체결 0                        → LIVE
체결 1: 137주 @68,300                                    → filled 137
체결 2: 211주 @68,400                                    → filled 348, LIVE
  notional = 68,300×137 + 68,400×211 = 9,357,100 + 14,432,400 = 23,789,500
  평균 단가 = 23,789,500 / 348 = 68,360.63 → 68,360원
  잔량 = 500 − 348 = 152
체결 3: 152주 @68,400                                    → filled 500, DONE, 잔량 0
```

**숫자 예시 — 응답과 체결이 겹칠 때**

```
주문 400주. 응답보다 체결 통보(90주, 상대 번호 0 → 내 번호로 찾음)가 먼저 온다 → filled 90, LIVE
응답이 누적치 90을 들고 온다 → 더하면 180이지만 큰 쪽을 취해 90 그대로
응답이 누적치 150을 들고 온다 → 150
```

**넘친 체결.** 100주 주문에 70주 + 55주 체결이 오면 `filled_qty`는 125로 **숨기지 않고 센다**. 상태는 DONE, `sdk_remaining`은 음수가 아니라 0이다. 넘쳤다는 **사실 자체를 잡는 것은 대사(T5-03의 `RECON_LEG_OVERFILL`)의 일**이고, SDK는 전략이 이상한 값으로 산술을 하지 않게만 막는다.

코드를 그대로 읽으면 보이는 경계 두 가지도 적어 둔다. `sdk_on_order_ack`는 응답의 누적 체결 수량으로 `filled_qty`만 올리고 `notional`은 건드리지 않는다(금액은 `sdk_on_fill`에서만 쌓인다). `sdk_on_modify_ack`는 응답의 가격만 반영하고 `qty`는 바꾸지 않는다.

##### 5. 어떻게 테스트하는가 — `sdk/tests/test_order_sdk.c`

파일 머리 주석: "숫자는 **맞아떨어지지 않는 값**으로 고른다(T3-11에서 한 번 당했다)." 68,300원 × 731주, 137주 + 211주처럼 어중간한 값을 쓴다. 100주 @10,000원처럼 둥근 값을 쓰면 한 건만 세거나 덮어쓰는 잘못된 구현도 우연히 맞는 답을 낼 수 있다.

테스트는 서로 독립적인 함수들이고 `STEP(fn)` 매크로가 이름을 찍으며 차례로 부른다.

- **왕복**: 주문 → 응답 → 전량 체결이 한 주문으로 이어지고, 상대 번호로도 찾힌다. 첫 번호는 받은 값 그대로(700,000,001)이고 50건을 내면 번호가 겹치지 않고 오른다.
- **누적**: 여러 번 나눈 체결의 금액·평균 단가가 손계산과 같고, 응답의 누적치를 두 번 세지 않으며, 넘친 체결에서도 잔량이 음수가 되지 않는다.
- **취소·정정·거부**: 정정은 응답 전까지 옛 가격(68,300)이고, 거부 응답이면 그대로, 수락이면 68,500. 거부된 주문(`ERR_NO_MARGIN`)은 DONE이 되고 취소·정정이 `ERR_NOT_SUPPORTED`다. 취소 거부 뒤에도 주문이 살아 있다.
- **모르는 번호**: 주문 응답·체결·취소 응답·정정 응답 네 종류 모두 `ERR_NOT_FOUND`를 돌려주고 `orphans`가 1 → 4로 오른다. 멀쩡한 주문은 영향이 없다.
- **자리 부족**: 1,024건을 채운 뒤 1,025번째는 `ERR_POOL_EXHAUSTED`이고 출력 번호도 건드리지 않는다. 첫 주문이 살아 있다(덮어쓰지 않음). 하나를 끝내고 `sdk_reap_done`하면 다시 받는다.
- **미결 건수**: 세 주문의 상태가 옮겨 갈 때마다 pending/live/count가 정확하다.
- **결정성**: 서로 다른 쓰레기 값(0xEE, 0x11)으로 채운 두 버퍼에 같은 입력으로 전문을 만들면 **바이트가 같다.** SDK가 시각이나 전역 난수를 읽으면 여기서 깨진다.
- **인자 검사**와 상태 이름 문자열.

---

#### 초보자용 기법 설명 — 이 세 모듈에 실제로 나오는 것

##### 결정성(determinism)과 시드(seed)

**결정적**이라는 것은 같은 입력을 넣으면 언제, 몇 번, 어느 컴퓨터에서 돌려도 같은 출력이 나온다는 뜻이다. 이 프로젝트에서 이것은 선택이 아니라 **전략 비교의 전제**다. KRX_ONLY와 SWEEP의 차이가 "전략 때문인지, 돌릴 때마다 달라지는 무언가 때문인지"를 가를 수 없으면 비교 표는 의미가 없다.

이 세 모듈이 결정성을 지키는 방법:

| 방법 | 어디서 |
|---|---|
| 시스템 시각을 읽지 않고, 시각은 주문이 들고 오는 논리 시각을 쓴다 | sor 전체, compare/quality, sdk(`sent_ts`는 호출부가 준다) |
| 결과 파일의 날짜를 **명령줄 인자**로 받는다 | `compare_strategies`, `quality_report`, `bench_pipeline` |
| 난수는 **시드에서만** 나온다. 전역 `rand()`를 쓰지 않는다 | `compare.c`의 xorshift64*, `bench_pipeline.c`의 splitmix64 |
| 동률을 마지막까지 규칙으로 끊는다(가격 → 잔량 → 시장 순서) | `better()`, `be_pick()`, SPLIT의 `prefers()`, `plan_resting_market()` |
| 점수를 부동소수가 아니라 **정수**로 계산한다 | `best_execution.c` (ADR 0005: 부동소수는 최적화 수준·FMA·x87 80비트에 따라 마지막 비트가 달라져 근소한 총점 차이의 순위를 뒤집을 수 있다) |
| 구조체를 `memset`으로 **패딩 바이트까지** 0으로 민다 | `routing_plan`, `send_leg`, sdk 전문 — 바이트 단위 비교로 결정성을 확인하기 때문 |

**시드**는 난수 생성기의 출발값이다. 같은 시드는 항상 같은 수열을 만든다. `compare.c`의 측정 주문 생성기는 `시드 ^ 0x9e3779b97f4a7c15`로 섞어서 시작한다 — 유동성 생성기와 같은 시드를 그대로 쓰면 두 수열이 같이 움직이기 때문이다. 섞은 결과가 0이면 1로 바꾼다(xorshift는 0 상태를 벗어나지 못한다). `compare_run()`은 시드 0을 거절한다.

`test_compare.c`는 이 성질을 "같은 시드 → 바이트까지 같은 표", "다른 시드 → 다른 표"로 직접 검사한다.

##### 전략마다 새 호가창을 만드는 이유

전략은 주문을 내서 **호가창의 물량을 먹는다.** 같은 호가창에 KRX_ONLY를 먼저 돌리고 이어서 SWEEP을 돌리면 SWEEP은 KRX_ONLY가 먹고 남은 호가로 재게 된다. 그것은 전략 비교가 아니라 **순서 측정**이다.

그래서 `compare.c`의 `run_cell()`은 **칸(시나리오×전략)마다** `vs_build()`로 매칭 엔진을 새로 만들고 같은 시드로 유동성을 다시 채운다. 측정 주문도 같은 시드로 다시 만든다. 결정성이 있기 때문에 "다시 채운 호가창 = 처음 호가창"이 보장된다. 테스트는 네 전략의 낸 수량이 칸마다 같다는 것으로 이를 확인한다.

같은 이유로 전략은 **계획만 세우고 보내지 않는다**(`strategy.h`). 계획은 호가창을 건드리지 않는 값이라 집행 없이도 "같은 입력에 같은 계획"을 검증할 수 있다.

##### basis point(bp)

| | |
|---|---|
| 1bp | 0.01% = 1/10,000 |
| 100bp | 1% |
| 10,000bp | 100% (`BP_SCALE`) |
| 10,000원의 1bp | 1원 |
| 10,016.67원 대 10,010원 | 6.67원 차이 = 약 6.66bp |

bp를 쓰는 이유는 가격 수준이 다른 주식을 같은 잣대로 비교하기 위해서다. 10,000원짜리의 1원과 100,000원짜리의 10원은 둘 다 1bp다. 이 프로젝트에서는 체결률(10,000bp = 100%)과 최선집행 점수(0~10,000)도 같은 자릿수를 써서 읽기 쉽게 맞췄다.

반올림은 **0에서 먼 쪽**이다. 손해를 낙관적으로 표시하지 않기 위해서다(`eq_to_bp`).

##### 정수 고정소수점 평균 — `EQ_AVG_SCALE`

C에서 정수끼리 나누면 소수점 아래를 버린다. `3,005,000 / 300 = 10,016`(실제는 10,016.67). 10,000원 주식에서 이 버림 오차 최대 1원은 정확히 1bp이고, 전략 간 차이가 1~6bp라 이 오차가 결론을 뒤집는다.

부동소수(`double`)를 쓰면 결정성이 흔들릴 수 있으니, **정수에 10,000을 곱해 놓고 나누는** 방법을 쓴다.

```
평균(원)       = 3,005,000 / 300          = 10,016          (소수 버림)
평균(1/10000원) = 3,005,000 × 10,000 / 300 = 100,166,666      (= 10,016.6666원)
```

"단위를 1/10,000원으로 바꿔 정수로 들고 다닌다"고 생각하면 된다. 이것이 **고정소수점(fixed-point)**이다. 소수점 위치가 항상 뒤에서 네 자리로 정해져 있다. 남는 오차는 1/10,000원이다. 넘침은 `int64_t`(약 9.2×10^18)가 받는다 — 체결 금액 × 10,000이 넘치려면 체결 금액이 9.2×10^14원이어야 한다.

같은 원리가 `eq_measure`에도 있다. 슬리피지 bp를 평균 단가가 아니라 `체결금액 − 기준가×체결수량`에서 직접 내는 것도 "나눗셈(버림)을 가능한 한 마지막에 한 번만 한다"는 같은 생각이다.

##### 최대 나머지 방식(Hare quota)

300주를 잔량 비율로 나누면 대개 정수로 떨어지지 않는다. 각자의 몫을 내림하고, 모자란 주수를 **나눗셈 나머지가 큰 순서대로** 한 주씩 나눠 준다. 선거에서 의석을 비례 배분할 때 쓰는 방식과 같다. 결과의 합이 항상 원래 수량과 같고, 비율을 가장 적게 왜곡한다(4-6절 표).

##### 산술 인코딩으로 해시 테이블 없애기

"(논리 번호, 시장) 쌍 → 물리 번호"를 표에 적어 두는 대신 `logical × 16 + market + 1`로 **계산**한다. 한 논리 주문이 한 시장에 다리 하나만 갖는다는 규칙(`plan_validate`)이 있어서 쌍이 유일하고, 그래서 자리값 인코딩(십진수의 "십의 자리/일의 자리"와 같은 원리를 16진 자리로)으로 만들 수 있다. 양방향 조회가 나눗셈·나머지 한 번이고, 물리 번호만 보고 시장을 안다.

##### 분위수(p50/p95/p99)와 예열

4-1, 5-1절 참조. 핵심만 다시: 평균은 꼬리를 감춘다. 분위수는 더해지지 않는다. 빈 호가창은 체결이 없어 비정상적으로 빠르니 예열 구간을 측정에서 뺀다. 거부된 건은 짧은 경로라 p50을 끌어내리니 빼고, 대신 몇 건이 왜 빠졌는지 보고서에 적는다.

##### 변이 검사(mutation testing)

**변이 검사**는 "테스트를 검사하는" 기법이다. 코드에 일부러 작은 버그(변이, mutant)를 심는다 — 예를 들어 `>`를 `>=`로 바꾸거나, 분모를 `avg_a`에서 `avg_b`로 바꾸거나, 검사 한 줄을 지운다. 그리고 테스트를 돌린다.

- 테스트가 실패하면 → 변이가 "죽었다". 테스트가 그 버그를 잡을 수 있다는 뜻이다.
- 테스트가 여전히 통과하면 → 변이가 "살아남았다". 테스트에 구멍이 있거나, 그 경로가 실행되지 않는다는 뜻이다.

이 세 모듈의 주석에 실제 기록이 있다.

| 기록 | 위치 | 무엇을 배웠나 |
|---|---|---|
| "변이 E4가 살아남았다" | `sor/tests/test_execution_quality.c` | `eq_avg_diff_bp`의 분모를 a가 아닌 b의 평균으로 바꿔도, 차이가 몇 bp일 때는 반올림 결과가 같아 테스트가 통과했다. 그래서 평균이 크게 다른 경우(20,000 대 10,000 → 5,000bp, 반대 방향 → −10,000bp)를 추가해 분모를 못 박았다 |
| "변이 검사 8종 중 셋이 살아남았고, 셋 다 '이 워크로드가 그 경로를 안 밟는다'가 이유였다" | `bench/bench_pipeline.c` 머리 주석 | 거부되는 주문이 없어 거부 집계 경로가 안 돌고, 다리가 하나씩이라 "다리마다 반복" 경로가 안 돈다. **테스트 구멍이 아니라 측정의 경계**로 보고 보고서에 다리 수 평균을 함께 적게 했다(변이 P6) |

변이 검사라는 이름은 없지만 같은 생각으로 짠 테스트도 있다.

- `test_strategy_split.c`의 `test_remainder_beats_fillable` — "나머지 큰 쪽"과 "잔량 많은 쪽"이 갈리는 장면을 따로 만든다. 없으면 단수 규칙을 잘못 바꿔도 통과한다.
- `test_routing_log.c`의 `test_format` — 버퍼 부족을 머리글·시장 블록·꼬리 세 지점 모두에서 만든다. 머리글만 보면 나머지 넘침 검사를 지워도 통과한다.
- `test_compare.c`의 `test_vs_krx_only_uses_notional` — 옛 식과 새 식의 값이 **실제로 갈리는 칸을 한 번은 봤다**는 것까지 확인한다. 갈리는 칸이 없으면 대조 테스트가 옛 버그를 잡을 수 없다.
- sdk 테스트의 "맞아떨어지지 않는 숫자" — 우연히 맞는 잘못된 구현을 죽인다.

##### 손계산 기대값과 전수 검사

sor 테스트는 기대값을 코드 출력에서 베끼지 않고 **주석에 계산 과정을 적은 손계산**으로 쓴다. 코드가 낸 값을 그대로 적으면 테스트가 아니라 "현상 기록"이 되어 버그도 정답으로 굳는다.

성질(불변조건)은 몇 개를 골라 보지 않고 **전부 훑는다**: SPLIT 합계(비율 12가지 × 1~300주), 물리 번호 충돌(논리 번호 1~2,000 × 전 시장), bp 반올림 대칭(−50~50), 로그 재현(호가 배치 48장면).

---

#### 한눈에 정리

| 모듈 | 핵심 한 문장 | 꼭 기억할 숫자/규칙 |
|---|---|---|
| sor/consolidated | 두 시장 호가를 캐시 없이 겹쳐 본다 | 동률: 가격 → 잔량 → KRX |
| sor/best_execution | 가격 40 / 체결 30 / 비용 20 / 상태 10, 정수 0~10,000점 | 1bp = 100점, 수수료 KRX 3bp·NXT 2bp(차이를 만들기 위한 값) |
| sor/strategy | 계획만 세운다. 다리 합 = 주문 수량, 한 시장 한 다리 | KRX_ONLY 기준선 / BEST_PRICE 한 시장 / SPLIT 비례·최대 나머지 / SWEEP 가격 순 |
| sor/order_map | 물리 번호 = 논리 × 16 + 시장 + 1 | 잔량 = 수량 − 체결 − 취소 |
| sor/executor | 한쪽이 거부돼도, 한쪽 취소가 실패해도 되돌리지 않는다 | 체결이 한 주라도 있으면 PARTIAL |
| sor/routing_log | 로그만 보고 결정을 재현할 수 있어야 한다 | 싱크 방식, 실제로 쓴 기준을 남긴다 |
| sor/execution_quality | 기준가 = 접수 시점 통합 최우선 상대호가, bp는 금액에서 직접 | 0에서 먼 쪽 반올림, `EQ_AVG_SCALE` 10,000 |
| bench | 전략마다 새 호가창, 날짜는 인자, 평균 대신 분위수·승패 | 매칭만 ~3.8M TPS, 전 구간 1,534,801 TPS → fsync로 376 TPS, KRX_THIN 30승 0패 중앙값 +6bp |
| sdk | core만 의존, 번호는 SDK가 발급, 모르는 번호는 세어서 알린다 | NONE → PENDING → LIVE → DONE, 누적치는 큰 쪽을 취한다 |

---

### 4.3 ledger · fep — 원장과 거래소 게이트웨이

#### 들어가기 전에 — 원장과 FEP는 어디에 있는가

이 장은 `ledger/`(원장)와 `fep/`(FEP) 두 모듈을 다룬다. 둘 다 C로 쓰였고 리눅스(WSL)
시스템 호출을 직접 부른다. 앞 장의 매칭 엔진·SOR은 "계산만 하는 코드"였지만, 이 두
모듈은 **소켓·프로세스·시그널·공유 메모리** 같은 운영체제 기능을 쓴다. 그래서 코드보다
개념이 먼저 걸리는 곳이 많고, 이 장은 그 개념을 0부터 설명한다.

설계도(`CLAUDE.md`)의 그림과 **지금 실제로 도는 모습**이 조금 다르므로 먼저 둘을 나란히
놓는다.

```
[설계도]                                   [지금 실제로 도는 것 (T6-03 "최소 연결")]

 화면(React)                                화면(React)
    │                                          │
 채널계(Java)                               채널계(Java)
    │ 고정 길이 전문(TCP)                      │ 고정 길이 전문(TCP, 127.0.0.1:9100)
 원장(C) ─ 계좌·증거금                      ┌──────────── ledgerd 프로세스 하나 ────────────┐
    │                                       │ listener  (접속 받기·전문 읽기)               │
 SOR(C)                                     │ ledger_core (검증→증거금→SOR→매칭→정산)      │
    ├────────────┐                          │   ├ 계좌(account, 공유 메모리 위)            │
 FEP-KRX     FEP-NXT                        │   ├ SOR (BEST_PRICE 전략)                    │
    │            │                          │   └ 매칭 엔진 KRX / NXT                      │
 KRX 시뮬    NXT 시뮬                        └───────────────────────────────────────────────┘

                                            fep/ 는 라이브러리 + 테스트로만 존재한다.
                                            "FEP ↔ 거래소" 두 프로세스 연결은
                                            fep/tests/test_integration.c(T3-15)가 검증했다.
```

정리하면 이렇다.

- **`ledgerd`는 한 프로세스, 한 스레드다.** SOR과 매칭 엔진 2개를 자기 안에 들고 있다
  (`ledger/include/ledger_core.h`의 "매칭을 원장 프로세스 안에 둔다").
- `ledger/`에는 **워커 풀(fork)과 공유 메모리** 코드도 있다. 각각 테스트를 통과하지만
  `ledgerd`는 워커 풀을 쓰지 않는다. 왜 쓰지 않는지가 이 장의 중요한 이야기 중 하나다.
- `fep/`에는 실행 파일이 없다. `fep` 정적 라이브러리와 테스트 7개가 전부다.

용어 몇 개를 먼저 정해 둔다.

| 용어 | 뜻 |
|---|---|
| 커널 | 운영체제의 핵심. 메모리·프로세스·네트워크를 관리한다. 프로그램은 커널에 "시스템 호출"로 일을 부탁한다 |
| 시스템 호출 | `read`, `write`, `fork`처럼 커널에 부탁하는 함수 |
| 프로세스 | 실행 중인 프로그램 하나. 자기만의 메모리 공간과 번호(pid)를 가진다 |
| fd (파일 디스크립터) | 커널이 열어 준 "무언가"(파일·소켓·파이프)를 가리키는 작은 정수. `3`, `4` 같은 번호다 |
| 블로킹 | 호출이 일을 끝낼 때까지 돌아오지 않는 것. 예: 데이터가 없으면 `read`가 기다린다 |
| 전문 | 시스템끼리 주고받는 고정 형식의 메시지 하나. 이 프로젝트에서는 24바이트 헤더 + 바디 |

---

#### ledger/ — 증권사 원장

##### 1. 한 줄 역할과 왜 필요한가

**원장은 "이 계좌에 돈이 얼마 있고, 그중 얼마가 주문에 묶여 있는가"를 책임지는 계층이다.**

증권사는 고객 주문을 거래소로 보내기 전에 막아야 할 것을 막는다. 없는 계좌, 거래정지
종목, 호가 단위가 틀린 가격, 한도를 넘는 금액, 그리고 **돈이 모자란 매수**다. 매수 주문이
접수되는 순간 그 주문 금액만큼을 "묶어" 두지 않으면, 같은 돈으로 주문을 여러 번 낼 수
있다. 이 묶어 둔 돈이 증거금이다.

이 프로젝트의 원장은 여기에 더해 **SOR 배분·매칭·체결 정산까지 한 번에 한다**(T6-03).
그래서 `ledgerd`에 주문 전문 하나를 보내면 검증부터 체결 대금 정산까지 끝난 응답이
돌아온다.

##### 2. 읽는 순서

위에서 아래로 읽는다. 각 줄은 "헤더 → 소스 → 테스트" 순서다.

| 순서 | 헤더 | 소스 | 테스트 | 한 줄 요약 |
|---|---|---|---|---|
| 0 | `core/include/wire.h`, `core/include/msg.h` | (core) | (core) | 전문 헤더 24바이트와 종별별 바디 배치 |
| 1 | `ledger/include/listener.h` | `ledger/src/listener.c` | `ledger/tests/test_listener.c` | TCP로 접속을 받아 전문을 읽는다 |
| 2 | `ledger/include/worker_pool.h` | `ledger/src/worker_pool.c` | `ledger/tests/test_worker_pool.c` | 미리 fork한 워커들이 접속을 나눠 받는다 |
| 3 | `ledger/include/shm_segment.h` | `ledger/src/shm_segment.c` | `ledger/tests/test_shm.c` | 프로세스끼리 같이 보는 메모리 |
| 4 | `ledger/include/account.h` | `ledger/src/account.c` | `ledger/tests/test_account.c` | 계좌 잔고와 계좌 단위 락 |
| 5 | `ledger/include/order_validate.h` | `ledger/src/order_validate.c` | `ledger/tests/test_order_validate.c` | 주문 검증 7단계와 증거금 묶기 |
| 6 | `ledger/include/ledger_core.h` | `ledger/src/ledger_core.c` | `ledger/tests/test_ledger_core.c` | 전부를 잇는 이음새 |
| 7 | — | `ledger/ledgerd.c` | — | 실행 파일. 소켓과 시그널만 맡는다 |

빌드 구성(`ledger/CMakeLists.txt`)도 이 구분을 따른다. 1~5는 `ledger` 라이브러리,
6은 SOR까지 끌어오는 `ledger_core` 라이브러리, 7은 `ledgerd` 실행 파일이다. 계좌·검증
테스트가 SOR에 매이지 않게 일부러 나눴다.

**처음 읽는다면** 0 → 1 → 5 → 6 → 7만 먼저 읽고, 2·3·4는 5절의 fork·공유 메모리 설명을
읽은 뒤에 돌아와도 된다. `ledgerd`가 실제로 거치는 길은 0·1·4·5·6·7이다(4의 계좌는
3의 공유 메모리 위에 놓인다).

##### 3. 핵심 자료구조

###### 3.1 전문 헤더 — 24바이트, 빅엔디언 (`core/include/wire.h`)

원장이 받는 모든 전문은 24바이트 헤더로 시작한다.

```
오프셋  크기  필드       뜻
------  ----  ---------  ------------------------------------------------
     0     2  magic      0x4D53 ("MS"). 스트림이 어긋났는지 빨리 알아챈다
     2     1  version    형식 판 번호(지금 1). 다르면 해석하지 않는다
     3     1  type       전문 종별 (1=주문 요청, 2=주문 응답, ...)
     4     4  body_len   헤더 뒤에 오는 바디의 바이트 수
     8     8  seq        보내는 쪽이 매긴 일련번호
    16     8  ts         논리 시각(나노초). 시스템 시각이 아니다
```

주문 요청(`MSG_ORDER_REQ`, 종별 1)의 바디는 39바이트다(`msg.h`).

```
ORDER_REQ (39)   account[12] symbol[8] cl_ord_id:u64 side:u8 type:u8
                 market:u8 price:i32 qty:i32
```

구체적인 바이트로 보자. 계좌 `123456789012`가 `005930`을 70,000원에 20주 매수하는 주문이고,
seq=1, ts=7이라고 하자.

```
헤더 (24바이트)
4D 53 | 01 | 01 | 00 00 00 27 | 00 00 00 00 00 00 00 01 | 00 00 00 00 00 00 00 07
magic  ver  type  body_len=39   seq=1                     ts=7

바디 (39바이트) 중 뒤쪽 일부
... side=00(매수) type=00(지정가) market=00(KRX) | 00 01 11 70 | 00 00 00 14
                                                   price=70000   qty=20
```

70,000은 16진수로 `0x00011170`이다. **빅엔디언**은 큰 자리부터 적는 방식이라 `00 01 11 70`
순서로 나간다. 사람이 16진수 덤프를 읽는 순서와 같다. 반대인 리틀엔디언(x86 CPU 메모리
안의 순서)이면 `70 11 01 00`이 된다. `wire.h`는 빅엔디언을 고른 이유를 "덤프를 눈으로
읽을 수 있다"고 적었다.

또 하나 중요한 규칙이 있다. **C 구조체를 그대로 `write`하지 않는다.** 컴파일러가 필드
사이에 끼워 넣는 채움 바이트(패딩)와 CPU의 바이트 순서에 기대게 되기 때문이다. 그래서
`wire_put_u32()` 같은 함수로 필드를 한 바이트씩 옮긴다.

종별 코드는 `msg.h`의 X 매크로 목록 한 곳에서 정해진다.

| 코드 | 종별 | 바디 길이 | 코드 | 종별 | 바디 길이 |
|---|---|---|---|---|---|
| 1 | ORDER_REQ | 39 | 9 | FILL_NOTI (체결 통보) | 46 |
| 2 | ORDER_ACK | 29 | 10 | LOGIN_REQ | 16 |
| 3 | CANCEL_REQ | 28 | 11 | LOGIN_ACK | 4 |
| 4 | CANCEL_ACK | 25 | 12 | HEARTBEAT | 0 |
| 5 | MODIFY_REQ | 36 | 13 | RESEND_REQ | 8 |
| 6 | MODIFY_ACK | 25 | 14 | GAP_FILL | 8 |
| 7 | QUERY_REQ | 20 | 15 | BOOK_REQ | 9 |
| 8 | QUERY_ACK | 38 | 16 | BOOK_ACK | 169 |

주문의 `market` 필드는 0=KRX, 1=NXT이고, **255(`MSG_MARKET_AUTO`)면 원장이 SOR로 시장을
고른다.** 255를 `market_t` 열거형에 넣지 않은 이유는 `MARKET_COUNT`가 3이 되어 시장마다
도는 반복문이 존재하지 않는 "SOR 시장"까지 돌게 되기 때문이다(`msg.h` 주석).

###### 3.2 계좌 레코드 — 예수금과 묶인 금액 (`account.h`)

```c
typedef struct {
    pthread_mutex_t lock;          /* 계좌 하나당 락 하나 */
    char     account_no[13];
    uint32_t in_use;               /* 0이면 빈 자리 */
    int64_t  cash;                 /* 예수금(원) */
    int64_t  reserved;             /* 미체결 주문에 묶인 금액(원) */
    uint64_t version;              /* 바뀔 때마다 +1 */
    uint32_t mutating;             /* 쓰는 중 표시 */
    int64_t  pre_cash;             /* 쓰기 직전 값 */
    int64_t  pre_reserved;
} account_t;
```

두 숫자의 관계가 전부다.

```
   cash (예수금) = 계좌에 들어 있는 돈 전체
 ┌──────────────────────────────────────────────┐
 │ reserved (묶인 금액) │   cash - reserved      │
 │ 미체결 매수 주문용   │   = 새 주문에 쓸 수 있는 돈 │
 └──────────────────────────────────────────────┘

 불변조건: 0 <= reserved <= cash
```

예를 들어 예수금 1억 원인 계좌가 70,000원에 20주 매수 주문을 걸어 두면
`cash = 100,000,000`, `reserved = 1,400,000`이고, 새 주문에 쓸 수 있는 돈은
98,600,000원이다. **묶였다고 예수금이 줄지는 않는다.** 실제로 체결돼야 줄어든다.

잔고를 바꾸는 연산은 다섯 개이고, 전부 같은 내부 함수 `apply()`를 지난다.

| 함수 | cash 변화 | reserved 변화 | 언제 쓰나 |
|---|---|---|---|
| `acct_deposit(a)` | +a | 0 | 입금, 매도 체결 대금 |
| `acct_withdraw(a)` | −a | 0 | 출금 (묶인 돈은 못 뺀다) |
| `acct_reserve(a)` | 0 | +a | 매수 주문 접수 |
| `acct_release(a)` | 0 | −a | 체결 안 될 수량, 가격 개선분 |
| `acct_settle(a)` | −a | −a | 매수 체결 — 묶인 돈을 실제로 쓴다 |

새 값이 불변조건을 어기면 **아무것도 바꾸지 않고** `ERR_INVALID_QTY`를 돌려준다.

`mutating`·`pre_cash`·`pre_reserved`는 "쓰다가 프로세스가 죽었을 때 되돌리기" 용도다.
5.7절에서 설명한다.

###### 3.3 공유 메모리 세그먼트 배치 (`shm_segment.h`)

계좌 레코드는 일반 `malloc` 메모리가 아니라 **공유 메모리 세그먼트** 안에 놓인다.

```
오프셋 0                    shm_header_t (magic "MSSM", version, total_size,
                             rec_count[2], rec_size[2], rec_off[2], initialized)
rec_off[0] (64의 배수)      영역 0: 계좌 레코드 배열 [0][1][2][3]...
rec_off[1] (64의 배수)      영역 1: 주문 레코드 배열 [0][1]...
```

- 세그먼트 안에는 **포인터를 저장하지 않고 오프셋만 둔다.** 포인터(주소)는 그 주소를
  가진 프로세스에서만 뜻이 있기 때문이다. `shm_record(seg, region, index)`가 그때그때
  `base + rec_off + rec_size × index`로 주소를 만든다.
- 레코드 크기와 영역 시작점을 **64바이트(캐시라인) 배수로 올린다.** 서로 다른 워커가 이웃
  레코드를 만질 때 같은 CPU 캐시 줄을 공유해 서로를 느리게 만드는 "거짓 공유"를 막는다.
- `ledger_core_create()`는 영역 0에 계좌 4자리, 영역 1에 64바이트짜리 4자리를 잡는다
  (`rec_count = {4, 4}`). 실제로 여는 계좌는 기본 설정의 `123456789012` 하나다.

###### 3.4 검증 설정과 결과 (`order_validate.h`)

```c
typedef struct { char symbol[9]; int32_t margin_bp; bool tradable; } symbol_rule_t;
typedef struct { symbol_rule_t symbols[64]; int32_t symbol_count;
                 int64_t max_order_notional; } validate_config_t;
typedef struct { int reason; int32_t account_index;
                 int64_t notional; int64_t margin; } validate_result_t;
```

- `margin_bp`는 증거금률을 1만분율로 적는다. 10000이 100%다. `ledger_core`는 100%로 등록한다
  (`vcfg_add_symbol(..., MARGIN_BP_FULL, true)`). 100% 미만이면 체결 대금과 묶은 돈이 달라
  미수 처리라는 별도 주제가 생기기 때문이다(`ledger_core.h`).
- 결과 구조체는 **거부돼도 왜 거부됐는지**를 남긴다.

###### 3.5 원장 코어 (`ledger_core.c`)

```c
struct ledger_core {
    ledger_core_config_t cfg;        /* 계좌·예수금·종목·기준가·유동성 설정 */
    shm_segment_t   *seg;            /* 계좌가 놓인 공유 메모리 */
    account_store_t  store;          /* 계좌 저장소 */
    validate_config_t vcfg;          /* 종목 규칙 */
    match_engine_t  *eng[2];         /* KRX, NXT 매칭 엔진 */
    cons_book_t      cons;           /* 두 호가창을 합쳐 보는 통합 호가창 */
    venues_t         venues;         /* 집행기가 주문을 넣을 엔진 목록 */
    order_map_t     *map;            /* 논리 주문 ↔ 물리 주문 (sor/order_map, T2-09) */
    int32_t         *acct_of;        /* 논리 주문번호 → 계좌 자리 */
    uint64_t        *cl_of;          /* 논리 주문번호 → 요청자가 붙인 번호 */
    order_id_t       next_logical;   /* 다음 논리 주문번호. 200,000,000부터 */
    ts_t             clock;          /* 논리 시각. 전문마다 +1 */
    order_id_t       submitting;     /* 지금 집행 중인 논리 주문번호 */
};
```

기본 설정 `LEDGER_CORE_DEFAULT`는 화면과 맞춘 값이다. 계좌 `123456789012`, 예수금 1억 원,
종목 `005930`, 기준가 70,000원, 시장당 유동성 주문 1,000건, 시드 20260917, 사용자 주문
용량 65,536건.

- **논리 주문번호가 2억에서 시작하는 이유**: 물리 주문번호는 `논리 × 16 + 시장 + 1`로
  만들어지는데(T2-09), 미리 깔아 두는 유동성 주문번호(KRX 1~, NXT 10억~)와 겹치지 않게
  하려는 것이다.
- `submitting`은 체결 이벤트가 "지금 들어온 주문(taker)"의 것인지, "예전에 걸어 둔
  주문(maker)"의 것인지 가르는 데 쓴다.

##### 4. 핵심 함수 흐름

###### 4.1 리스너 — 접속 하나를 끝까지 (`listener.c`)

`ledgerd`의 `main`은 `listener_run(ln, ledger_core_handle, core)`를 부른다. 그 안의 흐름이다.

```
listener_run
 └ 멈춤 요청이 없는 동안 반복
     listener_serve_one
      ├ accept()  ← 접속이 올 때까지 기다린다 (시그널이 오면 EINTR로 깨어나 멈춤 여부 확인)
      ├ serve_conn(cfd)
      │   반복:
      │   ├ read_exact(헤더 24바이트)        0이면 상대가 깨끗이 끊음 → 끝
      │   ├ wire_decode_header               magic·version·길이 한도 검사
      │   ├ msg_is_known(type)?              모르는 종별 → 접속 끊기
      │   ├ body_len == msg_body_len(type)?  규격과 다른 길이 → 접속 끊기
      │   ├ read_exact(바디 body_len바이트)
      │   ├ n = fn(&hdr, body, out, ...)     ← ledger_core_handle
      │   │     n < 0 → 접속 끊기, n == 0 → 응답 없음
      │   └ write_exact(out, n)              응답 전송
      └ close(cfd)
```

눈여겨볼 점.

- **헤더의 `body_len`을 믿지 않는다.** 종별 표의 길이와 다르면 끊는다. 헤더만 믿으면 필드가
  밀린 값을 그럴듯하게 읽어 버린다.
- **접속 하나가 잘못된 전문을 보내도 리스너는 살아 있다.** 그 접속만 닫고 다음 접속을 받는다.
- **한 번에 한 접속만 다룬다.** 채널계는 원장 접속 풀을 1로 두고 쓴다(`ledgerd.c` 주석).

###### 4.2 원장 코어 — 전문 하나 처리 (`ledger_core_handle`)

```
ledger_core_handle(hdr, body, out, cap, core)
 ├ reply = msg_reply_type(hdr->type)     응답 종별이 없으면 return 0
 ├ 응답 헤더 준비: type=reply, body_len=규격 길이,
 │               seq=요청의 seq, ts=요청의 ts   ← 원장이 자기 시각을 만들지 않는다
 └ switch (hdr->type)
     ORDER_REQ  → 디코드 → process_order → 주문 응답 인코드
     CANCEL_REQ → 디코드 → "REJECTED + ERR_NOT_SUPPORTED" 응답
     MODIFY_REQ → 디코드 → "REJECTED + ERR_NOT_SUPPORTED" 응답
     QUERY_REQ  → 디코드 → query_order → 조회 응답
     BOOK_REQ   → 디코드 → query_book  → 호가 응답
     그 밖      → return 0 (응답 없음)
   디코드 실패(바디가 규격과 다름) → return -1 → 리스너가 접속을 끊는다
```

**주문이 거부돼도 음수를 돌려주지 않는다.** 거부는 정상적인 대답이므로 `status=REJECTED`와
사유가 담긴 응답 전문을 돌려준다. 음수는 "전문 자체가 깨졌다"일 때만이다.

###### 4.3 주문 처리 — `process_order` 단계별

```
process_order(req)
 ① ack 기본값: status=REJECTED, cl_ord_id=요청 번호, price=요청 가격
 ② automatic = (req.market == 255)
 ③ 자리 검사: next_logical - 2억 >= order_capacity 이면 → ERR_POOL_EXHAUSTED
      (이 검사가 없으면 acct_of[] 배열 밖에 쓴다. ASan이 잡는다)
 ④ validate_order(store, vcfg, req, &v)       ← 7단계 검증 + 매수면 증거금 묶기
      실패 → reason=v.reason, 끝 (아무것도 안 묶였다)
      ── 여기부터 매수라면 "지정가 × 수량"이 묶여 있다 ──
 ⑤ order_t 만들기: id=next_logical++, ts=++clock,
                  market = automatic ? KRX(임시) : req.market
 ⑥ 배분 계획(plan)
      automatic → routing_plan(&STRATEGY_BEST_PRICE, {cons 통합 호가창, ts}, ...)
      아니면   → plan_add_leg(plan, 지정 시장, 수량, 가격, 유형)   ← 없는 시장이면 여기서 거절
      실패 → release_unused(전체 수량) → reason=rc, 끝
 ⑦ acct_of[id-2억] = 계좌 자리, cl_of[id-2억] = 요청 번호
 ⑧ submitting = id
    exec_submit(map, venues, order, plan, &rep)   ← 매칭 엔진에 넣는다
        매칭 중 체결마다 on_event 콜백이 불려 돈을 옮긴다 (아래 4.4)
    submitting = 없음
      실패(한 다리도 접수 안 됨) → release_unused(전체 수량), 끝
 ⑨ never = rep.order_qty - rep.filled_qty - rep.working_qty
    release_unused(never)         ← 체결되지도, 호가창에 남지도 않을 수량의 증거금을 푼다
 ⑩ ack: order_id=id, status=rep.status, reason=OK, filled_qty=rep.filled_qty,
        price = 체결이 있으면 평균 체결가(notional / filled_qty), 없으면 지정가
```

`release_unused(req, acct, q)`는 **매수일 때만** `acct_release(지정가 × q)`를 한다. 매도는
묶은 것이 없으므로 아무것도 하지 않는다.

⑨의 식이 이 모듈의 핵심 아이디어다. 매수 주문을 받을 때 `지정가 × 주문수량`을 묶었다.
그 뒤 수량은 세 갈래로 나뉜다.

```
주문수량 = 체결됨(filled) + 호가창에 살아 있음(working) + 앞으로도 체결 안 됨(never)
            │                 │                          │
            │                 │                          └ 여기서 바로 푼다 (⑨)
            │                 └ 계속 묶어 둔다. 나중에 체결되면 on_event가 정산한다
            └ on_event가 이미 정산했다
```

거부된 다리(leg)든 IOC 주문의 남은 수량이든 경우를 따로 세지 않고 이 식 하나로 끝낸다.

###### 4.4 체결 정산 — `on_event`와 `settle_fill`

매칭 엔진은 체결 한 건마다 이벤트를 **두 번** 낸다. 먼저 maker(호가창에 있던 주문), 그다음
taker(방금 들어온 주문)다(`exchange/src/match/match_engine.c`의 "순서 고정" 주석). 원장은
유동성을 다 깔아 둔 뒤 두 엔진에 `on_event`를 콜백으로 건다.

```
on_event(ev)
 ├ 체결 이벤트(EXECUTED / PARTIALLY_EXECUTED)가 아니면 무시
 ├ omap_leg(map, ev->order_id) == NULL 이면 무시     ← 미리 깔아 둔 유동성 주문이다
 ├ lo = 이 물리 주문의 논리 주문
 ├ lo->logical_id != submitting 이면                 ← 예전에 걸어 둔 주문(maker)이다
 │     omap_on_fill(map, ...)                        ← 매핑에도 체결 수량을 적는다
 │     (taker 쪽 매핑 반영은 exec_submit이 한다)
 └ settle_fill(lo, 체결가 p, 수량 q)
       매수: acct_settle(p × q)                      ← 묶인 돈에서 실제로 쓴다
             improvement = (지정가 - p) × q
             improvement > 0 이면 acct_release(improvement)  ← 더 싸게 산 만큼 푼다
       매도: acct_deposit(p × q)                     ← 대금 입금
```

**돈을 옮기는 곳이 이 콜백 하나뿐이다.** taker로 체결되든 maker로 나중에 체결되든 같은 코드가
정산한다. 두 곳에 두면 한쪽만 고치는 날이 온다는 것이 주석의 설명이다.

maker는 늘 자기 지정가에 체결되므로 maker 매수의 가격 개선분은 항상 0이다.

###### 4.5 숫자로 따라가기 — 매수 20주가 걸려 있다가 매도 12주에 맞는다

`test_ledger_core.c`의 `test_resting_then_maker_fill`을 그대로 따라간다. 유동성이 없는 빈
호가창(`liquidity_per_market = 0`)이고 계좌는 하나다.

**시작**: 예수금 100,000,000원, 묶인 금액 0원.

**요청 1 — KRX에 70,000원 × 20주 지정가 매수 (cl=10)**

| 단계 | 일어나는 일 | cash | reserved | 쓸 수 있는 돈 |
|---|---|---:|---:|---:|
| 시작 | | 100,000,000 | 0 | 100,000,000 |
| ④ 검증 | 매수 → 증거금 = 70,000×20×100% = 1,400,000 → `acct_reserve` | 100,000,000 | 1,400,000 | 98,600,000 |
| ⑥ 계획 | 시장 지정(KRX) → `plan_add_leg` 다리 하나 | 〃 | 〃 | 〃 |
| ⑧ 집행 | 호가창이 비어 체결 0. 20주가 매수호가로 걸린다. 이벤트 없음 | 〃 | 〃 | 〃 |
| ⑨ 해제 | never = 20 − 0 − 20 = **0** → 풀 것 없음 | 〃 | 〃 | 〃 |
| ⑩ 응답 | status=NEW, filled=0, price=70,000 | 100,000,000 | 1,400,000 | 98,600,000 |

KRX 호가창의 70,000원 매수 잔량이 20주가 된다.

**요청 2 — KRX에 69,900원 × 12주 지정가 매도 (cl=11)**

69,900원에 팔겠다는 주문은 70,000원에 사겠다는 걸린 주문과 가격이 맞는다. 체결가는 걸려
있던 쪽(maker)의 가격 70,000원이다.

| 단계 | 일어나는 일 | cash | reserved | 쓸 수 있는 돈 |
|---|---|---:|---:|---:|
| ④ 검증 | 매도 → 증거금을 묶지 않는다 | 100,000,000 | 1,400,000 | 98,600,000 |
| ⑧ 이벤트 1 (maker) | 걸린 매수 12주 @70,000. 논리번호 ≠ submitting → 매핑에 체결 반영. 매수 정산 `acct_settle(840,000)` | 99,160,000 | 560,000 | 98,600,000 |
| | 가격 개선 (70,000−70,000)×12 = 0 → 해제 없음 | 〃 | 〃 | 〃 |
| ⑧ 이벤트 2 (taker) | 매도 12주 @70,000 → `acct_deposit(840,000)` | 100,000,000 | 560,000 | 99,440,000 |
| ⑨ 해제 | 매도라서 `release_unused`가 아무것도 안 한다 | 〃 | 〃 | 〃 |
| ⑩ 응답 | filled=12, price = 840,000 / 12 = 70,000 | 100,000,000 | 560,000 | 99,440,000 |

테스트가 확인하는 핵심은 **묶인 금액이 8주분(560,000원)으로 줄었는가**이다. 예수금이 제자리인
것은 같은 계좌가 사고팔아 −840,000과 +840,000이 상쇄됐기 때문이다. 테스트 머리 주석은 이
상쇄 때문에 "합계만 보면 버그가 통과한다"고 경고하고, 그래서 유동성과 체결하는 경우
(`test_buy_takes_liquidity`: 예수금이 정확히 `매도호가 × 수량`만큼 준다)를 따로 본다.

**요청 3 — 70,000원 × 8주 매도 (cl=12)**

같은 과정으로 maker 정산 `acct_settle(560,000)` → cash 99,440,000 / reserved 0, taker 입금
560,000 → cash 100,000,000 / reserved 0. 호가창의 70,000원 매수 잔량도 0이 된다.

**다른 두 예도 같은 규칙으로 맞아떨어진다.**

- 가격 개선(`test_price_improvement_released`): NXT에 69,000원 매도 10주를 걸어 두고
  70,000원 매수 10주를 낸다. 묶음 700,000 → 체결가 69,000이므로 `acct_settle(690,000)`
  → 남은 묶음 10,000 → 개선분 (70,000−69,000)×10 = 10,000을 `acct_release` → 묶음 0.
- IOC 잔량(`test_ioc_remainder_released`): 5주만 걸린 곳에 IOC 매수 12주 @70,000. 묶음
  840,000 → 5주 체결 정산 350,000 → IOC라 남은 7주는 호가창에 남지 않는다 →
  never = 12 − 5 − 0 = 7 → `acct_release(490,000)` → 묶음 0.

###### 4.6 거부 경로에서 돈이 새지 않는 이유

| 거부 지점 | 묶였나 | 누가 푸나 |
|---|---|---|
| ③ 자리 없음 | 안 묶임 (검증 전) | — |
| ④ 검증 1~6단계 실패 | 안 묶임 | — |
| ④ 7단계 증거금 부족 | 묶기 시도 자체가 실패 | — |
| ⑥ 없는 시장(예: market=7) | **묶임** | `release_unused(전체)` |
| ⑧ 모든 다리 거부 | **묶임** | `release_unused(전체)` |

없는 시장 경로에는 사연이 있다. 처음에는 ② 근처에서 시장 값을 따로 검사했는데, 그러면
⑥의 해제 경로가 한 번도 실행되지 않았다. 변이 검사로 그 사실을 알고 앞의 검사를 지웠다
(`process_order` 안 주석).

###### 4.7 주문 검증 7단계 (`validate_order`)

```
1. 계좌가 있는가              acct_find            없으면 ERR_NOT_FOUND
2. 종목이 거래 가능한가        vcfg_find, tradable  아니면 ERR_NOT_SUPPORTED
3. 주문 유형을 받을 수 있나    지정가·IOC·FOK만     시장가·중간가 → ERR_NOT_SUPPORTED
   방향이 매수(0)/매도(1)인가                       아니면 ERR_INVALID_ARG
4. 수량이 범위 안인가          QTY_MIN~QTY_MAX      ERR_INVALID_QTY
5. 가격이 범위 안인가          PRICE_MIN~PRICE_MAX  ERR_INVALID_PRICE
   호가 단위에 맞나            is_valid_tick        ERR_INVALID_TICK
   notional = 가격 × 수량
6. 주문 한도                   max_order_notional   ERR_LIMIT_EXCEEDED
7. 증거금  ── 매도면 묶지 않고 통과
           ── 매수면 margin = margin_for(notional, margin_bp)
              acct_reserve(margin) 실패 → ERR_NO_MARGIN     ← 여기서만 계좌 락을 잡는다
```

- **싼 검사를 앞에 둔다.** 락을 잡는 7단계를 마지막에 두어, 형식이 틀린 주문이 락을 잡고
  있지 않게 한다. 그리고 거부 사유를 정확히 말하게 된다. 증거금부터 보면 "호가 단위가
  틀린 주문"이 "증거금 부족"으로 거부되어 주문한 쪽이 엉뚱한 곳을 고친다.
- **확인하고 묶지 않는다. 묶어 보고, 실패하면 그것이 곧 증거금 부족이다.** "잔고가
  충분한가"를 본 뒤 따로 묶으면 그 사이에 다른 워커가 끼어들어 둘 다 통과할 수 있다.
  `acct_reserve`는 계좌 락 안에서 불변조건을 검사하므로 확인과 묶기가 한 번에 일어난다.
- **시장가를 받지 않는 이유**: 증거금은 가격을 알아야 계산되는데 시장가는 체결돼 봐야
  가격을 안다. 실제 증권사처럼 상한가로 어림잡으려면 종목별 참조 가격이 필요하고, 그건
  범위 밖이라고 적어 두었다.
- **매도의 보유 수량 확인은 하지 않는다.** 주식 잔고 원장의 일이고 이 프로젝트의 범위
  밖이라고 코드에 적혀 있다.
- **증거금은 올림한다**: `(notional × bp + 9999) / 10000`. 증거금률 40%인 1,000,001원
  주문이면 400,000.4원 → 400,001원이다. 내림하면 1원이 덜 묶인다.
- 호가 단위의 예: 70,000원대는 100원 단위라 70,050원은 `ERR_INVALID_TICK`이다
  (`test_rejects_leave_money_alone`).

###### 4.8 조회와 호가 조회

**주문 조회 `query_order`** — 주문번호 하나의 지금 상태를 돌려준다.

```
ack 기본값: order_id=요청 번호, status=REJECTED, last=1, symbol=설정 종목, qty=0
번호가 [2억, 2억+용량) 밖이면     → 기본값 그대로 ("없음")
매핑에 논리 주문이 없으면         → 기본값 그대로
exec_status(map, id)             → status
cl_ord_id = cl_of[id-2억], price = 지정가, qty = 주문수량,
filled_qty = omap_filled_qty(map, id)
```

- 조회 응답에는 사유 필드가 없어서 "없음"을 `REJECTED` + 수량 0으로 표시한다.
- **주문번호 0(당일 전체 조회)은 아직 받지 않는다.** 응답을 여러 전문으로 나눠 보내야 하기
  때문이다. 0도 "없음"으로 답한다.
- 4.4에서 maker 체결을 `omap_on_fill`로 매핑에 적는 이유가 여기서 드러난다. 적지 않으면
  돈은 맞게 정산돼도 조회가 영영 "0주 체결"로 남는다. 테스트
  `test_query_reflects_maker_fill`은 20주 걸어 둔 주문이 12주 뒤 `PARTIAL`/12주, 8주 더 뒤
  `FILLED`/20주로 보이는지 확인한다.

**호가 조회 `query_book`** — 한 시장의 10단 호가.

```
ack: symbol, market = 요청 그대로, 나머지 전부 0
market >= 2 이거나 종목이 설정 종목과 다르면 → 빈 호가창(전부 0)으로 답한다
아니면 book_snapshot(매수, 10단) → bid_price[i], bid_qty[i]
      book_snapshot(매도, 10단) → ask_price[i], ask_qty[i]
      없는 단은 0
```

매수 쪽은 높은 가격부터, 매도 쪽은 낮은 가격부터 담긴다. 화면이 보는 호가가 원장 안의
호가창 그대로다(T6-04).

###### 4.9 취소·정정이 NOT_SUPPORTED로 답하는 이유

`CANCEL_REQ`, `MODIFY_REQ`는 바디를 디코드한 뒤 **`status = REJECTED`, `reason = ERR_NOT_SUPPORTED`**
로 답한다. 원장 코어에 아직 연결하지 않았기 때문이다.

이렇게 명시적으로 "안 된다"고 답하는 데는 이유가 있다. T3-03 때의 껍데기 `ledgerd`는
취소 요청에 **무조건 성공**을 돌려줬다. 그러면 호가창에 그대로 남아 체결될 수 있는 주문을
사용자는 취소됐다고 믿는다. 감사에서 이것을 찾았고, T6-03에서 "모르는 것은 성공이라 하지
않고 지원 안 함이라 한다"로 바꿨다. `test_cancel_is_not_faked`가 이것을 고정한다.

###### 4.10 `ledgerd` 실행 흐름

```
main(argc, argv)
 ├ 포트 = argv[1] (없으면 9100, 0~65535 아니면 종료 코드 2)
 ├ listener_install_signals()         SIGTERM·SIGINT 핸들러, SIGPIPE 무시
 ├ core = ledger_core_create(NULL)    기본 설정: 계좌 개설·1억 입금·유동성 1000건씩 깔기
 ├ ln = listener_open(포트, 64)       127.0.0.1에만 묶는다
 ├ "ledgerd 포트 9100 에서 대기" 출력
 ├ listener_run(ln, ledger_core_handle, core)    ← 여기서 SIGTERM까지 머문다
 └ 정리: listener_close, ledger_core_destroy
```

`listener_open`은 `INADDR_LOOPBACK`(127.0.0.1)에 묶으므로 **같은 기계에서만** 붙을 수 있다.

`ledger_core_create`의 유동성 깔기(`seed_liquidity`)는 시드로 만든 합성 주문을 두 엔진에
넣고, **다 넣은 뒤에** 체결 콜백을 건다. 준비 단계의 유동성끼리 체결은 정산 대상이 아니기
때문이다. 세션(장 시간) 규칙은 걸지 않는다. 걸면 화면을 켠 실제 시각과 논리 시각이 어긋나
이유 없이 "장이 닫혀서 거절"이 보이기 때문이다.

###### 4.11 워커 풀 흐름 (`worker_pool.c`) — `ledgerd`는 쓰지 않는다

`ledgerd`가 쓰지는 않지만 fork를 이해하는 가장 좋은 예제이므로 흐름을 적는다. 5.5절의
fork 설명을 먼저 읽는 것이 좋다.

```
pool_start(cfg)                       cfg.ln = 이미 열린 리스닝 소켓
 └ i = 0..N-1: spawn(pool, i)
      pipe(fds)                        fds[0]=읽기 끝, fds[1]=쓰기 끝
      pid = fork()
      자식(pid==0): close(fds[0]); worker_main(pool, fds[1])  ← 돌아오지 않는다
      부모(pid>0) : close(fds[1]); fds[0]을 논블로킹으로; w[i] = {pid, fds[0]}

worker_main (자식)
 ├ listener_reset_stop(); listener_install_signals()
 └ 멈춤 전까지: listener_serve_one(공유 리스닝 소켓)   ← 워커마다 각자 accept
        접속 하나 끝낼 때마다 파이프에 1바이트 write   ← 처리 건수 보고
        연달아 16번 실패하면 그만둔다
   close(wfd); _exit(0)

pool_supervise (부모)
 └ 멈춤 전까지:
      dead = waitpid(-1, WNOHANG)        죽은 자식이 있나?
      있으면: drain(파이프 남은 바이트 세기) → 파이프 닫기 → spawn으로 자리 채우기, restarts++
      없으면: 20ms 쉰다 (ponytail: 폴링)

pool_stop (부모)
 ├ 모든 워커에 SIGTERM
 └ 워커마다 reap_worker(pid):
       최대 200번: waitpid(pid, WNOHANG) — 죽었으면 끝
                   아직이면 SIGTERM 다시 보내고 1ms 쉰다
       그래도 안 죽으면 SIGKILL 후 waitpid로 거둔다 (forced_kills++)
   거둔 뒤 drain으로 파이프에 남은 건수까지 센다
```

SIGTERM을 여러 번 보내는 이유는 "시그널 유실 경쟁" 때문이다. 워커가 `listener_stopping()`을
확인한 직후, `accept()`에 들어가기 직전에 SIGTERM이 오면 핸들러가 플래그를 세우지만 워커는
그 뒤 `accept()`에 잠들어 깨어나지 못한다. 플래그는 이미 서 있으므로 한 번 더 보내면
`accept()`가 EINTR로 깨어 나간다. 실제로 `test_no_zombies`가 6번 중 2번 멈춰서 찾은 문제다
(`reap_worker` 위 주석).

##### 5. 시스템 기법을 0부터

###### 5.1 TCP 소켓 — 전화 교환대에 비유하면

두 프로그램이 네트워크로 이야기하려면 **소켓**이 필요하다. 소켓도 fd(작은 정수)다.
서버 쪽은 다섯 단계를 거친다. `listener_open`과 `listener_serve_one`이 정확히 이 순서다.

```
서버(ledgerd)                                        클라이언트(채널계)
fd = socket(AF_INET, SOCK_STREAM)  전화기를 산다     fd = socket(...)
setsockopt(SO_REUSEADDR)           (아래 설명)
bind(fd, 127.0.0.1:9100)           번호를 단다
listen(fd, 64)                     벨이 울리게 한다. 대기열 64
                                                      connect(fd, 127.0.0.1:9100)
cfd = accept(fd)                   수화기를 든다  ◄────  연결
      ↑ 새 fd가 생긴다. fd는 계속 "벨" 용, cfd가 이 통화 용
read(cfd, ...) / write(cfd, ...)   말하고 듣기  ◄───►  write / read
close(cfd)                         끊기
```

- `accept`는 **새 fd를 돌려준다.** 리스닝 소켓은 계속 다음 접속을 기다리는 데 쓰고, 통화는
  새 fd로 한다.
- **포트 0**을 주면 커널이 빈 포트를 고른다. `getsockname()`으로 실제 번호를 되묻는다.
  테스트가 고정 포트로 서로 부딪히지 않게 하려는 것이다.
- **`SO_REUSEADDR`**: TCP 접속을 닫은 뒤 한동안 그 주소가 `TIME_WAIT` 상태로 남는데, 그 사이
  재시작하면 `bind`가 실패한다. 이 옵션이 그것을 막는다.
- `SOCK_STREAM`(TCP)은 **바이트가 순서대로, 빠짐없이** 도착함을 보장한다. 대신 **메시지
  경계는 보장하지 않는다.** 이것이 다음 절의 주제다.

###### 5.2 부분 읽기·부분 쓰기, 그리고 길이 선행 방식

TCP는 "바이트의 강물"이다. 상대가 63바이트(헤더 24 + 주문 바디 39) 전문 하나를 한 번에
`write`해도, 받는 쪽 `read`는 이렇게 나눠 받을 수 있다.

```
보낸 쪽:  [ 63바이트 전문 ]
받는 쪽:  read → 10바이트   read → 30바이트   read → 23바이트
          [헤더 일부]       [헤더 나머지+바디 일부] [바디 나머지]
```

반대로 전문 세 개가 한 번의 `read`에 붙어 올 수도 있다. `read(fd, buf, 24)`는 "최대
24바이트"라는 뜻이지 "정확히 24바이트"가 아니다.

리스너는 `read_exact()`로 이 문제를 푼다.

```c
while (got < n) {
    r = read(fd, buf + got, n - got);   /* 남은 만큼 달라고 한다 */
    if (r == 0) → 상대가 끊었다 (전문 도중이면 오류)
    if (r < 0 && errno == EINTR) → 시그널에 끊겼다. 멈춤 요청이면 나가고 아니면 다시
    got += r;
}
```

`write_exact()`도 같은 모양이다. `write`도 일부만 쓸 수 있기 때문이다.

그러면 "몇 바이트를 읽어야 전문 하나인가"는 어떻게 아는가. **헤더에 길이를 적어 둔다**
(길이 선행, length-prefixed). 먼저 고정 24바이트 헤더를 읽고, 그 안의 `body_len`만큼
더 읽으면 된다. `wire.h`는 구분자(예: 줄바꿈) 방식을 쓰지 않은 이유를 적었다. 바디 안에
구분자 바이트가 나오면 이스케이프해야 하고, 이스케이프는 길이를 바꿔 버퍼 계산을
어렵게 만든다.

###### 5.3 시그널과 `volatile sig_atomic_t`

**시그널**은 커널이 프로세스에 보내는 "짧은 알림"이다. 터미널에서 Ctrl+C를 누르면
`SIGINT`, `kill 123`이면 `SIGTERM`, 끊긴 소켓에 쓰면 `SIGPIPE`가 간다. 기본 동작은 대개
"프로세스 종료"인데, **핸들러 함수**를 걸면 대신 그 함수가 불린다.

문제는 핸들러가 **아무 때나** 끼어든다는 것이다. `printf` 한가운데일 수도, `malloc`
한가운데일 수도 있다. 그래서 핸들러 안에서 부를 수 있는 함수는 극히 적다. 이 코드의
핸들러는 딱 한 줄이다.

```c
static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }
```

- `sig_atomic_t`: 핸들러와 본 흐름 사이에서 **쪼개지지 않고** 읽고 쓰인다고 C 표준이 보장하는
  정수 타입이다.
- `volatile`: 컴파일러에게 "이 값은 네가 모르는 사이에 바뀔 수 있으니 캐시하지 말고 매번
  메모리에서 읽어라"라고 알린다. 없으면 최적화로 루프가 옛 값만 보고 영원히 돈다.
- **전역 변수는 규약상 금지지만 여기는 예외다.** 핸들러는 인자를 받지 않으므로 컨텍스트를
  넘길 방법이 없다(`listener.c` 주석).

그다음이 **`SA_RESTART`를 켜지 않는 이유**다. `accept()`에서 기다리는 중 시그널이 오면,
`SA_RESTART`가 켜져 있을 때 커널은 핸들러를 부른 뒤 `accept()`를 **자동으로 다시 시작**한다.
그러면 루프가 플래그를 볼 기회가 없어 영원히 기다린다. 꺼 두면 `accept()`가 `-1`과
`errno == EINTR`로 돌아오고, 코드는 플래그를 보고 빠져나온다.

```
accept() 대기 중 ──SIGTERM──► 핸들러: g_stop = 1 ──► accept()가 EINTR로 돌아옴
                                                    └► listener_stopping()? 예 → return 0
```

`SIGPIPE`는 **무시**로 건다. 상대가 먼저 끊은 소켓에 쓰면 기본 동작이 프로세스 종료인데,
접속이 끊기는 것은 서버에게 일상이다. 무시해 두면 `write`가 `EPIPE` 오류로 돌아온다.

###### 5.4 fd와 파이프

**파이프**는 한쪽에 쓰면 다른 쪽에서 읽히는 커널 속 관이다. `pipe(fds)`가 fd 두 개를 준다.

```
fds[1] (쓰기 끝)  ──► [ 커널 버퍼 ] ──► fds[0] (읽기 끝)
```

워커 풀은 워커마다 파이프를 하나 두고, 워커(자식)가 접속을 하나 끝낼 때마다 1바이트를 쓴다.
부모는 읽은 바이트 수로 처리 건수를 센다. 1바이트 쓰기는 커널이 원자적으로 처리하므로 락이
필요 없다. 부모가 쓰기 끝을 곧바로 닫는 이유는, 열어 두면 워커가 다 죽어도 읽기 끝에서
"끝(EOF)"이 오지 않고 다음 fork 때 그 fd가 또 상속되기 때문이다.

###### 5.5 `fork()` — 프로세스 복사를 0부터

`fork()`는 **지금 실행 중인 프로세스를 통째로 복사해 하나 더 만든다.** 이 한 문장이 전부인데,
결과가 낯설어서 헷갈린다. 차근차근 본다.

**(1) 복사된다는 것**

```
fork() 호출 직전                     fork() 호출 직후
┌─ 프로세스 pid=100 ─┐              ┌─ 부모 pid=100 ─────┐   ┌─ 자식 pid=101 ─────┐
│ 코드: spawn() 안    │              │ 코드: spawn() 안    │   │ 코드: spawn() 안    │
│ 변수: idx = 2       │     ──►      │ 변수: idx = 2       │   │ 변수: idx = 2 (사본)│
│ 열린 fd: 3(리스닝)  │              │ 열린 fd: 3, 4, 5    │   │ 열린 fd: 3, 4, 5    │
│         4,5(파이프) │              │ fork() 반환값: 101  │   │ fork() 반환값: 0    │
└─────────────────────┘              └─────────────────────┘   └─────────────────────┘
```

- 자식은 **처음부터 시작하지 않는다.** 부모와 똑같이 `fork()`가 돌아오는 바로 그 줄에서
  이어서 실행한다. 한 번 불렀는데 **두 번 돌아오는** 셈이다. 부모에서 한 번, 자식에서 한 번.
- 둘을 구분하는 유일한 방법이 **반환값**이다.

| 반환값 | 뜻 | 어디서 보이나 |
|---|---|---|
| 음수 (`-1`) | 실패. 자식이 안 생겼다 | 부모 |
| `0` | "나는 자식이다" | 자식 |
| 양수 | "나는 부모이고, 이것이 자식의 pid다" | 부모 |

`worker_pool.c`의 `spawn()`이 교과서 모양이다.

```c
pid_t pid = fork();
if (pid < 0) { /* 실패 */ }
if (pid == 0) {                /* ← 이 블록은 자식만 실행한다 */
    close(fds[0]);
    worker_main(pool, fds[1]); /* 돌아오지 않는다 */
    _exit(0);
}
close(fds[1]);                 /* ← 여기부터는 부모만 온다 */
pool->w[idx].pid = pid;
```

**(2) 메모리는 사본이다 — copy-on-write**

fork 직후 부모와 자식의 변수 값은 같지만 **서로 다른 메모리**다. 자식이 `idx = 7`로 바꿔도
부모의 `idx`는 2 그대로다.

전부 복사하면 느리므로 커널은 꾀를 쓴다. 처음에는 같은 물리 메모리 페이지를 둘이 **읽기
전용으로 함께** 쓰게 해 두고, 어느 한쪽이 **쓰려는 순간** 그 페이지만 복사해 준다. 이것을
**copy-on-write(쓸 때 복사)**라고 한다. 결과적으로 프로그램 입장에서는 "각자 자기 사본을
가진다"와 똑같이 동작한다.

**(3) 물려받는 것**

- **열린 fd**: 부모가 연 리스닝 소켓, 파이프를 자식도 쓸 수 있다. 워커 풀은 fork **전에**
  리스닝 소켓을 열어 두고, 워커 N개가 **같은 소켓**에서 각자 `accept()`한다. 리눅스는
  `accept()`로 기다리는 여러 프로세스 중 **하나만** 깨운다(`worker_pool.h` 주석).
- **`MAP_SHARED`로 만든 공유 메모리**: 이것만은 사본이 아니라 **진짜로 같은 메모리**다(5.6절).

**(4) 자식은 `return`하지 말고 `_exit`로 끝낸다**

자식이 `worker_main`에서 그냥 `return`하면 부모의 호출 스택을 그대로 이어받아 호출한 곳
(테스트 코드 등)으로 돌아간다. **자식이 부모인 척 계속 달리게 된다.** 그래서 `_exit(0)`으로
끝낸다. `exit`이 아니라 `_exit`인 이유는, `exit`은 stdio 버퍼를 비우는데 자식은 부모가 아직
출력하지 않은 버퍼의 사본을 들고 있어서 같은 내용이 두 번 찍히기 때문이다
(`worker_main` 위 주석).

**(5) 사전 fork(pre-fork)**

`pool_start`는 **요청이 오기 전에** 워커를 전부 만든다. 접속마다 fork하면 복사 비용이 응답
시간에 그대로 얹히기 때문이다.

**(6) 그런데 왜 `ledgerd`는 fork하지 않는가**

(2)의 "메모리는 사본"이 답이다. 매칭 엔진의 호가창은 보통 메모리(`malloc`)에 있다. 워커
3개를 fork하면 이렇게 된다.

```
                  ┌─ 워커 A ────────────────┐
 채널계 주문 1 ──►│ 호가창 A: 매수 20@70,000 │
                  └─────────────────────────┘
                  ┌─ 워커 B ────────────────┐
 채널계 주문 2 ──►│ 호가창 B: (비어 있음)     │   ← 매도 12주가 여기 오면 체결 상대가 없다
                  └─────────────────────────┘
                  ┌─ 워커 C ────────────────┐
                  │ 호가창 C: ...            │
                  └─────────────────────────┘
                  공유 메모리: 계좌(예수금·묶인 금액)만 셋이 함께 본다
```

계좌는 공유 메모리에 있어 셋이 같은 잔고를 보지만, 호가창은 **워커마다 따로** 생긴다.
같은 매도 주문이 어느 워커에 닿느냐에 따라 체결되기도 하고 안 되기도 한다. `ledger_core.h`의
표현대로 "시장이 둘이 아니라 워커 수만큼 생기는 셈"이다. 그래서 `ledgerd`는 **한 프로세스가
전문을 하나씩 차례로** 처리한다. 이것은 "매칭 엔진은 단일 스레드로 결정적이어야 한다"는
프로젝트 규칙과도 맞는다. 동시 접속자가 여럿 필요해지면 fork가 아니라 FEP의 epoll 루프
방식으로 한 스레드에서 여러 접속을 받으면 된다고 주석이 적어 두었다.

###### 5.6 `waitpid`와 좀비

자식 프로세스가 끝나면 커널은 그 흔적을 바로 지우지 않는다. **종료 코드**를 부모가 받아 갈
수 있게 작은 기록을 남겨 둔다. 부모가 `wait`/`waitpid`로 그 기록을 가져가야 비로소 사라진다.
가져가지 않은 채 남은 죽은 자식을 **좀비**라고 한다.

```
자식 _exit(0) ──► [좀비: pid 101, 종료코드 0] ──waitpid(101)──► 완전히 사라짐
```

좀비는 CPU를 먹지 않지만 **프로세스 표의 자리**를 차지한다. 오래 사는 서버가 거두지 않으면
자리가 찬다. 그래서 `pool_stop`은 모든 워커를 거둔다.

- `waitpid(pid, &status, 0)`: 그 자식이 끝날 때까지 기다린다.
- `waitpid(-1, &status, WNOHANG)`: **아무 자식이든** 끝난 것이 있으면 거두고, 없으면
  **기다리지 않고** 0을 돌려준다. 자식이 하나도 없으면 `-1`과 `errno == ECHILD`.

`pool_supervise`는 `WNOHANG`으로 죽은 워커를 찾아 자리를 채운다. `test_no_zombies`는 마지막에
`waitpid(-1, WNOHANG)`이 `ECHILD`를 돌려주는지, 즉 거둘 자식이 하나도 안 남았는지 확인한다.

###### 5.7 공유 메모리 (`mmap`)와 프로세스 공유 락

**공유 메모리**는 여러 프로세스가 **같은 물리 메모리**를 각자의 주소 공간에 붙여 쓰는 방법이다.

```c
void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
```

- `MAP_ANONYMOUS`: 파일과 연결되지 않은 메모리. 이름이 없다.
- **`MAP_SHARED`가 핵심이다.** fork한 자식이 이 매핑을 물려받으면 copy-on-write가 적용되지
  않고 **진짜 같은 바이트**를 본다. `MAP_PRIVATE`로 바꾸면 `mmap`은 여전히 성공하고 한
  프로세스 안에서는 멀쩡하지만, fork 너머에서만 틀어진다. 그래서 `test_shm`은 진짜 fork로
  "부모가 fork 뒤에 쓴 값이 자식에게 보이는지, 자식이 쓴 값이 부모에게 보이는지"를 확인한다.

`shm_segment.h`는 후보 셋을 비교해 익명 `mmap`을 골랐다.

| 방식 | 장점 | 이 프로젝트에서의 문제 |
|---|---|---|
| System V `shmget` | 오래된 표준 | 재부팅까지 남아 손으로 지워야 한다. 키 충돌 관리 |
| POSIX `shm_open` + `mmap` | 혈연 없는 프로세스도 붙는다 | 이름 충돌, `/dev/shm` 찌꺼기 청소 |
| **익명 `mmap`** (채택) | 이름·찌꺼기가 없다. 프로세스가 다 죽으면 커널이 회수 | fork한 자손끼리만 공유 가능 — 이 구조에는 그것으로 충분 |

**초기화는 fork 전에 끝낸다.** 부모가 머리말을 채우고 `initialized = 1`을 마지막에 세운 뒤
fork하므로 "누가 먼저 초기화하나"라는 경쟁이 아예 없다.

**계좌가 공유 메모리에 있는 이유**는 워커 풀 구성에서 모든 워커가 같은 잔고를 봐야 하기
때문이다(T3-05·T3-06). 그러면 두 워커가 같은 계좌를 동시에 고치는 일을 막아야 하고, 그래서
계좌마다 **프로세스 사이에서 동작하는 뮤텍스**를 둔다.

```c
pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);  /* 프로세스끼리 쓰겠다 */
pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);     /* 쥔 채 죽어도 회수 */
```

- **계좌 단위 락**: 원장 전체에 락 하나면 서로 다른 계좌 주문끼리도 줄을 선다. 계좌마다 두면
  다른 계좌를 만지는 워커는 서로 기다리지 않는다.
- **robust 락과 되돌리기**: 락을 쥔 프로세스가 죽으면 락이 영영 안 풀린다. robust 속성이면
  다음에 잠그는 쪽이 `EOWNERDEAD`를 받는다. 이때 데이터가 반쯤 고쳐졌을 수 있으므로
  `acct_lock`은 이렇게 한다.

```
쓰기 순서 (apply):  pre_cash=cash, pre_reserved=reserved, mutating=1
                    cash, reserved를 새 값으로
                    mutating=0, version++

회수 (acct_lock이 EOWNERDEAD를 받았을 때):
    mutating == 1 이면 cash=pre_cash, reserved=pre_reserved, mutating=0  ← 되돌린다
    pthread_mutex_consistent()   ← 안 부르면 그 락은 영구히 ENOTRECOVERABLE
```

`test_recovers_from_dead_owner`가 진짜로 죽여 본다. 예수금 1,000,000원, 묶음 200,000원인
계좌를 자식이 잠그고 `mutating=1`로 표시한 뒤 값을 999원·888원으로 망쳐 놓고 락을 쥔 채
`_exit`한다. 부모가 잠그면 `recovered == true`이고 값이 1,000,000 / 200,000으로 돌아와 있다.

되돌리는 이유: 죽은 워커는 응답을 못 보냈으므로 요청한 쪽은 실패로 본다. 원장도 그 일이
없던 것으로 두어야 앞뒤가 맞는다. 절반을 굳히면 "증거금은 묶였는데 주문은 없는 계좌"가
생긴다(`account.h`).

`ledgerd`는 fork하지 않지만 `ledger_core`는 같은 계좌 코드를 그대로 쓰므로 계좌는 여전히 이
공유 메모리 세그먼트 위에 놓인다.

###### 5.8 결정성의 경계

매칭·SOR 엔진은 시스템 시각을 읽지 않는다. 네트워크 계층은 "언제 몇 바이트가 오는지"를
커널이 정하므로 결정적일 수 없다. 그래서 경계를 긋는다(`listener.h`).

- 리스너는 자기 시각을 만들지 않는다. 처리 결과는 전문 내용만으로 정해진다.
- 원장 코어의 논리 시각 `clock`은 주문마다 1씩 오른다. 응답 헤더의 `seq`·`ts`는 요청 것을
  그대로 쓴다.
- `test_deterministic`은 같은 설정의 원장 코어 두 개에 같은 주문 5건을 넣고 **응답 바이트가
  완전히 같은지** `memcmp`로 확인한다.

##### 6. 테스트가 보장하는 것

**`test_listener.c`** (진짜 소켓, 클라이언트는 fork한 자식)
- 포트 0으로 열면 커널이 준 번호를 되물을 수 있다. 서버가 먼저 끊어 **진짜 TIME_WAIT을 만든 뒤에도** 같은 포트를 다시 열 수 있다(`SO_REUSEADDR`이 실제로 일한다).
- 전문을 **한 바이트씩 나눠 보내도** 프레임 단위로 조립해 훅에 넘긴다(부분 수신 경로).
- 모르는 종별, 규격과 다른 길이는 접속을 끊는다. 헤더나 바디 도중에 끊긴 접속은 "깨끗한 종료"가 아니라 **오류**로 보고되고 훅이 불리지 않는다. 아무것도 안 보내고 끊은 접속은 오류가 아니다.
- `accept()`에서 기다리던 중 SIGTERM이 오면 멈춘다. `SA_RESTART`가 켜지면 이 테스트가 멈춤으로 드러난다.

**`test_worker_pool.c`**
- 워커를 요청 전에 미리 만든다. 40번 접속하면 합계 40건이 집계되고 **2개 이상의 워커가 실제로 일한다.**
- SIGKILL로 죽인 워커를 감시 루프가 다시 띄우고, 죽기 전 처리 건수도 잃지 않는다.
- 멈춘 뒤 워커 pid가 모두 사라지고 `waitpid`가 `ECHILD` — 좀비가 없다.
- 60번 되풀이해도 SIGKILL까지 가지 않고 유예 종료로 끝난다(`forced_kills == 0`).

**`test_shm.c`**
- fork 뒤 부모가 쓴 값이 자식에게, 자식이 쓴 값이 부모에게 보인다(진짜 공유).
- 배치가 문서와 맞고 영역이 64바이트 경계에 있으며, 크기 넘침 요청을 거절한다.
- magic·version·크기·겹침이 망가진 머리말을 `shm_validate`가 거절한다.

**`test_account.c`**
- 불변조건 `0 <= reserved <= cash`를 어기는 연산은 거절되고 아무것도 바뀌지 않는다.
- 락이 프로세스 사이에서 줄을 세우고, 계좌 단위라 다른 계좌는 막히지 않는다.
- 락을 쥔 채 죽은 프로세스의 반쯤 쓴 값을 되돌린다. 쓰기 중이 아니었으면 건드리지 않는다.

**`test_order_validate.c`**
- 거부 사유(계좌·종목·유형·수량·가격·호가 단위·한도·증거금)가 서로 구분되고, 거부되면 증거금을 묶지 않는다.
- 증거금은 올림 계산된다.
- 자식 4개가 각각 10번씩 주문을 넣어도 **정확히 허용된 10건만** 통과한다(초과 배분 없음).

**`test_ledger_core.c`** (소켓 없이 전문 바이트를 처리 훅에 직접 넣는다)
- 매수는 매도호가를 먹고 예수금이 정확히 체결 대금만큼 줄며, 지정한 시장만 건드린다(방향·시장 값 해석 고정).
- 걸어 둔 매수가 나중에 체결되면 묶음이 줄고, 가격 개선분과 IOC 잔량의 증거금이 풀린다.
- `market=255`면 더 싼 시장으로 가고, 조회·호가 조회가 원장 안의 실제 상태를 돌려준다.
- 거부는 사유를 말하고 돈을 건드리지 않으며, 취소는 성공을 지어내지 않는다.
- 같은 전문 순서는 같은 응답 바이트를 만든다.

---

#### fep/ — 거래소와 붙는 전문 게이트웨이

##### 1. 한 줄 역할과 왜 필요한가

**FEP(Front-End Processor)는 증권사 시스템과 거래소 사이에서 접속을 유지하고 전문을 주고받는
관문이다.**

거래소와의 연결은 "소켓 하나 열고 쓰면 끝"이 아니다. 오래 살아 있어야 하고, 끊기면 다시
붙어야 하며, 붙은 뒤에는 로그인해야 한다. 조용한 동안에도 살아 있음을 알려야 하고(하트비트),
끊겨 있던 사이 놓친 체결 통보를 되찾아야 한다(시퀀스·재전송). 거래소가 붙여 준 주문번호를
우리 번호와 이어야 하고, **응답을 못 받은 채 끊긴 주문**을 어떻게 볼지도 정해야 한다.
`fep/`는 이 일들을 조각으로 나눠 구현했다.

이 모듈에는 **실행 파일이 없다.** `fep` 라이브러리와 테스트가 전부이고, 여러 조각이 실제로
맞물리는지는 `test_integration.c`(T3-15)가 두 프로세스로 확인한다. 지금 `ledgerd`는 FEP를
거치지 않고 매칭 엔진을 직접 부른다.

##### 2. 읽는 순서

아래로 갈수록 위의 것을 쓴다.

| 순서 | 헤더 | 소스 | 테스트 | 한 줄 요약 |
|---|---|---|---|---|
| 1 | `fep/include/evloop.h` | `fep/src/evloop.c` | `fep/tests/test_evloop.c` | epoll로 여러 fd를 한 스레드에서 감시 |
| 2 | `fep/include/framer.h` | `fep/src/framer.c` | `fep/tests/test_framer.c` | 쪼개져 오는 바이트를 전문으로 조립 |
| 3 | `fep/include/sendq.h` | `fep/src/sendq.c` | `fep/tests/test_sendq.c` | 다 못 보낸 바이트를 들고 있는 송신 큐 |
| 4 | `fep/include/seqtrack.h` | `fep/src/seqtrack.c` | `fep/tests/test_seqtrack.c` | 시퀀스 갭 감지와 재전송 보관 |
| 5 | `fep/include/session.h` | `fep/src/session.c` | `fep/tests/test_session.c` | 로그인·하트비트·재접속 상태 기계 (2·3·4를 묶는다) |
| 6 | `fep/include/ordmap.h` | `fep/src/ordmap.c` | `fep/tests/test_ordmap.c` | 우리 번호 ↔ 거래소 번호, 미응답 주문 판정 |
| 7 | — | — | `fep/tests/test_integration.c` | FEP(부모) ↔ 진짜 매칭 엔진(자식) 두 프로세스 |

주의할 이름 두 개가 있다. `fep/`의 **`ordmap`**은 "우리 번호 ↔ 거래소 번호"이고, 원장 코어가
쓰는 `sor/`의 **`order_map`**(`omap_*`)은 "논리 주문 ↔ 물리 주문"이다. 한 줄로 이으면 이렇다
(`ordmap.h`).

```
사용자 주문 ──(sor/order_map, T2-09)── 물리 주문 ──(fep/ordmap, T3-13)── 거래소 주문번호
```

**처음 읽는다면** `session.h`의 머리 주석부터 읽고 5절의 epoll·논블로킹 설명을 본 뒤 1번부터
내려오는 것을 권한다.

##### 3. 핵심 자료구조

###### 3.1 이벤트 루프 등록 표 (`evloop.c`)

```c
typedef struct { ev_fn fn; void *ctx; uint32_t events; bool registered; } slot_t;
struct evloop { int epfd; slot_t slot[4096]; int32_t count; };
```

fd 번호를 **그대로 배열 칸 번호**로 쓴다. fd 7에 대한 콜백은 `slot[7]`에 있다. 관심 사건은
`EV_READ`(읽을 것 있음), `EV_WRITE`(쓸 자리 있음)이고, `EV_ERROR`·`EV_HANGUP`은 커널이 알아서
얹어 준다.

###### 3.2 조립기 `framer_t` (`framer.h`)

```c
typedef struct {
    uint8_t buf[WIRE_FRAME_MAX];   /* 24 + 65536 바이트 — 가장 큰 전문 하나가 통째로 들어간다 */
    size_t  off;                   /* 아직 안 꺼낸 첫 바이트 */
    size_t  len;                   /* 쌓인 바이트의 끝 */
    int     err;                   /* 한 번 어긋나면 이 접속은 끝. 그 에러 코드 */
} framer_t;
```

```
buf: [ 이미 꺼낸 것 | 아직 안 꺼낸 것 ............ | 빈 자리 ]
     0             off                             len        끝
```

접속마다 하나씩 둔다. 리스너의 `read_exact`처럼 기다릴 수 없으므로 "어디까지 받았나"를
접속별로 들고 있어야 하기 때문이다.

###### 3.3 송신 큐 `sendq_t` (`sendq.h`)

```c
typedef struct { uint8_t buf[262144]; size_t off; size_t len; } sendq_t;
```

모양은 조립기와 같다. `off`~`len`이 아직 못 보낸 바이트다. 256KB면 가장 큰 전문 네 개,
보통 크기(70바이트 안팎)로는 수천 개가 들어간다.

###### 3.4 세션 `session_t`와 상태 (`session.h`)

```c
typedef enum { SESSION_DOWN, SESSION_LOGGING_IN, SESSION_READY } session_state_t;

typedef struct {
    session_state_t state;
    int       fd;             /* -1이면 없음. 세션은 이 fd를 닫지 않는다 */
    framer_t  rx;             /* 받기 조립기 */
    sendq_t   tx;             /* 보내기 큐 */
    char      session_id[17]; /* 예: "FEP-KRX-01" */
    uint64_t  out_seq;        /* 다음에 보낼 번호. 1부터. 재접속해도 이어진다 */
    seqstore_t store;         /* 보낸 전문 보관 (재전송용) */
    seqtrack_t track;         /* 받은 번호 대조 */
    int64_t   last_tx_ms;     /* 마지막으로 무언가 "보낸" 시각 */
    int64_t   last_rx_ms;     /* 마지막으로 무언가 "받은" 시각 */
    int64_t   login_at_ms;
    int64_t   retry_at_ms;    /* DOWN일 때 다시 붙을 시각 */
    int32_t   backoff_ms;
    int32_t   attempts;       /* 연달아 끊긴 횟수 */
    session_config_t cfg;     /* 하트비트 5초, 무응답 15초, 로그인 5초, 백오프 200ms~30초 */
} session_t;
```

상태 전이는 세 칸이다.

```
            session_init
                 │
                 ▼
   ┌────────► DOWN ───────────── now >= retry_at 이고 호출부가 소켓을 붙여
   │           ▲    session_on_connected(fd) ──► LOGIN_REQ를 큐에 넣는다
   │           │                                   │
   │           │                                   ▼
   │           │                             LOGGING_IN
   │  session_drop                               │  LOGIN_ACK(result=OK) 수신
   │  (백오프 잡기)                               ▼
   │           │                               READY  ◄── 업무 전문은 여기서만 오간다
   │           │                                   │
   └───────────┴──── 끊김 / 무응답 15초 / 로그인 5초 초과 / 스트림 어긋남 / LOGIN_ACK 거절
```

`retry_at_ms`, `last_tx_ms` 같은 시각은 전부 **인자로 받은 `now_ms`**다. 세션 코드 어디에도
`time()`·`clock()`이 없다(5.6절).

###### 3.5 시퀀스 보관과 추적 (`seqtrack.h`)

**보내는 쪽 — `seqstore_t`**: 최근에 보낸 전문 256개를 고리 버퍼에 보관한다.

```c
typedef struct { uint64_t seq; uint16_t len; uint8_t frame[128]; } seqslot_t;
typedef struct { seqslot_t slot[256]; uint64_t oldest, newest; int32_t count; } seqstore_t;
```

번호 `seq`는 `seq % 256`번 칸에 들어간다. 257번은 1번 칸을 덮는다. 들고 있는 구간은 언제나
`[newest − count + 1, newest]`다. 꺼낼 때는 **칸에 적힌 번호가 요청한 번호와 같은지**만
보면 된다. 밀려난 번호의 칸에는 한 바퀴 뒤의 다른 번호가 앉아 있기 때문이다.

**받는 쪽 — `seqtrack_t`**:

```c
typedef struct {
    uint64_t expected;     /* 다음에 받을 번호 */
    bool     recovering;   /* 재전송을 기다리는 중인가 */
    uint64_t gap_from;     /* 재전송을 요청한 시작 번호 */
    int64_t  gaps, dups;   /* 운영 지표 */
} seqtrack_t;
```

판정 결과는 넷이다.

| 판정 | 조건 | 처리 |
|---|---|---|
| `SEQ_OK` | `seq == expected` | 위로 올린다. `expected++`, `recovering = false` |
| `SEQ_DUP` | `seq < expected` | 버린다. 이미 본 번호다 |
| `SEQ_GAP` | `seq > expected`, 메우는 중 아님 | 재전송 요청. `recovering = true` |
| `SEQ_WAIT` | `seq > expected`, 이미 메우는 중 | 버린다. 요청을 또 보내지 않는다 |

###### 3.6 주문번호 매핑 `ordmap_t` (`ordmap.h`)

```c
typedef enum { ORD_NONE, ORD_PENDING, ORD_LIVE, ORD_DONE, ORD_INDOUBT } ord_state_t;
typedef struct {
    ord_state_t state;
    uint64_t    cl_ord_id;   /* 우리 번호 (T2-09의 물리 주문번호) */
    order_id_t  exch_id;     /* 거래소가 매긴 번호. PENDING이면 0 */
    uint64_t    added_seq;   /* 들어온 차례. 밀어낼 것을 고를 때 쓴다 */
} ordent_t;
typedef struct { ordent_t ent[4096]; uint64_t next_seq; int32_t count; } ordmap_t;
```

```
ordmap_add ──► PENDING ──ordmap_on_ack(접수)──► LIVE ──ordmap_close──► DONE
                  │    └ordmap_on_ack(거부)──────────────────────────► DONE (exch_id=0)
                  │
      ordmap_on_disconnect (응답 못 받고 끊김)
                  ▼
               INDOUBT ──ordmap_on_query_result(live)──► LIVE
                  │    └ordmap_on_query_result(not live)► DONE
                  └──ordmap_finish_query (조회 끝, 응답에 없었음)──► DONE (exch_id=0)
```

`DONE`을 곧바로 지우지 않는다. **취소했다고 생각한 주문에 체결이 하나 더 늦게 오는** 일이
실제로 있어서, 그 체결이 제 주문을 찾을 수 있게 남겨 둔다. 표가 차면 `DONE` 중 가장 오래된
것부터 밀어내고, `DONE`조차 없으면 `ERR_POOL_EXHAUSTED`로 거절한다. 살아 있는 주문은 절대
밀어내지 않는다.

##### 4. 핵심 함수 흐름

###### 4.1 이벤트 루프 한 바퀴 — `evloop_once`

```
evloop_once(lp, timeout_ms)
 ├ n = epoll_wait(epfd, evs, 256, timeout_ms)   준비된 fd들을 한 묶음으로 받는다
 │    EINTR(시그널)이면 0을 돌려준다
 └ i = 0..n-1:
      fd = evs[i].data.fd
      slot[fd].registered 가 아니면 건너뛴다    ← 앞선 콜백이 이 fd를 닫았을 수 있다
      slot[fd].fn(fd, 사건 비트, ctx)
```

**"다시 확인"이 중요한 이유**: `epoll_wait`은 이벤트를 묶음으로 준다. 묶음의 첫 이벤트를
처리하다 콜백이 fd 7을 닫았는데 묶음 뒤쪽에 fd 7의 이벤트가 또 있을 수 있다. 게다가 fd
번호는 곧바로 재사용되므로, 그 사이 새로 연 접속이 fd 7을 받았다면 **엉뚱한 접속**의 콜백이
불린다. 그래서 꺼낼 때마다 아직 등록돼 있는지 본다.

`evloop_add`는 **논블로킹이 아닌 fd를 거절한다.** 블로킹 fd가 섞이면 루프가 어디선가 멈추고
그 원인을 찾기가 가장 어렵기 때문이다. `evloop_del`은 `epoll_ctl`이 실패해도(콜백이 fd를
먼저 닫아 EBADF) 표에서는 지운다.

`evloop_run(lp, tick_ms)`는 멈춤 플래그가 설 때까지 `evloop_once`를 되풀이한다. 무한 대기
(`tick_ms < 0`)는 멈춤 신호를 못 보므로 거절한다.

###### 4.2 조립 — `framer_push` / `framer_next`

```
framer_push(data, n)
 ├ 이미 어긋난 상태면 ERR_NOT_SUPPORTED
 ├ 뒤 공간이 모자라고 앞에 꺼낸 자리가 있으면 memmove로 당긴다
 ├ 그래도 모자라면 ERR_POOL_EXHAUSTED (꺼내지 않고 넣기만 했다는 뜻)
 └ buf[len..]에 복사, len += n

framer_next(&hdr, &body)
 ├ 어긋난 상태면 그 에러를 계속 돌려준다
 ├ 쌓인 것 < 24 → 0 (더 받아야 한다)
 ├ wire_decode_header → 실패(magic·version·길이 한도) → err 기록, 음수 → 접속을 끊어라
 ├ 쌓인 것 < 24 + body_len → 0 (바디가 아직)
 └ hdr, body 포인터(buf 안) 채우고 off += 24+body_len → 1
```

예: 63바이트 전문이 10·30·23바이트로 온다.

```
push(10)  → next: 쌓인 10 < 24 → 0
push(30)  → next: 쌓인 40 ≥ 24, 헤더 해석 body_len=39, 40 < 63 → 0
push(23)  → next: 쌓인 63 ≥ 63 → 1 (전문 하나), 다시 next → 0
```

- **헤더를 먼저 본다.** `body_len`이 한도를 넘으면 바디를 기다리기 전에 끊는다.
- **어긋나면 재동기하지 않는다.** TCP는 바이트를 빠뜨리지 않으므로 magic이 틀렸다는 것은
  "우리 조립 상태가 이미 틀렸다"는 뜻이다. 앞으로 훑으며 다음 "MS"(0x4D53)를 찾으면 가격이나
  계좌번호 안에서도 그 바이트가 나와 **그럴듯한 가짜 전문**이 생긴다. 끊고 재접속해 처음부터
  맞추는 편이 싸다(`framer.h`).
- `body` 포인터는 조립기 내부 버퍼를 가리키므로 **다음 `framer_next`/`framer_push`까지만** 유효하다.

###### 4.3 송신 — `sendq_push` / `sendq_flush`

```
sendq_push(frame, n)
 ├ n == 0 또는 n > 262144 → ERR_INVALID_ARG
 ├ 뒤가 모자라면 앞으로 당긴다
 ├ 그래도 모자라면 ERR_POOL_EXHAUSTED, 큐는 그대로   ← 전부 아니면 전무
 └ 복사, len += n

sendq_flush(fd)
 ├ 보낼 게 없으면 0 (write를 부르지 않는다)
 ├ w = write(fd, buf+off, 남은 전부)
 │    w < 0 이고 EAGAIN/EWOULDBLOCK/EINTR → 0   ← 에러 아님. 자리가 없을 뿐
 │    w < 0 그 밖(EPIPE, ECONNRESET...)   → ERR_IO  ← 접속이 끝났다
 └ off += w, return w                       ← 일부만 나가도 정상
```

`sendq_want_write()`는 남은 바이트가 있을 때만 참이다. 호출부는 이것이 참일 때만 `EV_WRITE`를
켠다(5.3절).

###### 4.4 세션 — 일생 한 바퀴

**보내기 공통 `enqueue(type, body)`**

```
헤더 채우기: version=1, type, body_len, seq = out_seq, ts = now_ms × 1,000,000 (나노초)
sendq_push(헤더+바디 한 덩어리)          실패하면 그대로 돌려준다(아무것도 안 넣었다)
seqstore_put(out_seq, 프레임)            ← 큐에 넣는 데 성공한 뒤에 보관
out_seq++, last_tx_ms = now_ms
```

보관을 큐 넣기 **뒤에** 하는 이유: 순서가 반대면 큐가 차서 거절된 전문까지 보관되고, 나중에
"보낸 적 없는 번호"를 재전송해 주게 된다.

**업무 전문 보내기 `session_send`**: READY가 아니면 `ERR_NOT_LOGGED_IN`, 세션 전문(로그인·
하트비트)을 흉내 내면 `ERR_INVALID_ARG`, 모르는 종별·틀린 길이도 거절한다. 거절이면 **아무것도
나가지 않은 상태**다. 로그인 전에 보낸 주문은 상대가 조용히 버릴 수 있고, 그러면 "보냈는데
응답 없는 주문"이 생기기 때문에 아예 보내지 않는다.

**받기 `session_on_readable`**

```
① 논블로킹 read(8192바이트)를 EAGAIN까지 되풀이
     받은 게 있으면 last_rx_ms = now_ms   ← 무엇을 받았든 살아 있다는 신호
     framer_push 실패 → drop
     read == 0(상대가 끊음) 또는 진짜 오류 → drop, ERR_IO
② framer_next를 0이 나올 때까지
     음수(어긋남) → drop
     handle_frame(전문) 실패 → drop
```

**전문 하나 처리 `handle_frame`**

```
verdict = seqtrack_on(track, hdr.seq)        ← 모든 전문의 번호를 센다
verdict == SEQ_GAP → RESEND_REQ(from_seq = expected)를 enqueue

switch (type)
  HEARTBEAT   → 끝. 되받아치지 않는다
  LOGIN_ACK   → LOGGING_IN이 아니면 무시(늦게 온 응답)
                result != OK → ERR_NOT_LOGGED_IN (끊긴다)
                seqtrack_restart_at(hdr.seq + 1)   ← 상대가 재기동했으면 기대값을 낮춘다
                state = READY, attempts = 0, backoff = 최소값
  LOGIN_REQ   → ERR_NOT_SUPPORTED (이 세션은 거는 쪽이다)
  RESEND_REQ  → resend_from(from_seq)
  GAP_FILL    → seqtrack_skip_to(next_seq)  (되돌리는 값이면 무시, 끊지 않음)
  업무 전문   → READY 아님 → ERR_NOT_LOGGED_IN
                모르는 종별 / 규격과 다른 길이 → 오류
                verdict != SEQ_OK → 조용히 버린다 (중복·순서 어긋남)
                verdict == SEQ_OK → fn(hdr, body, ctx) 로 위에 올린다
```

**번호는 모두 세지만 막는 것은 업무 전문뿐이다.** 세션 전문까지 갭에 막으면 빠져나올 수 없다.
재접속한 뒤 상대의 `LOGIN_ACK`은 이미 갭 너머의 번호를 달고 오는데, 그것을 막으면 로그인이
안 끝나고 로그인이 안 끝나면 재전송도 못 받는다. "갭에서 빠져나오는 열쇠를 갭 안에 가두는
셈"이라고 주석은 적었다.

**재전송 해 주기 `resend_from(from_seq)`**

```
oldest = seqstore_oldest(store)
보관이 비었거나 from_seq < oldest 이면:
    next = (비었으면 out_seq, 아니면 oldest)
    GAP_FILL(next_seq = next)를 enqueue      ← "그 앞은 없다. next부터 이어라"
    from_seq = next
seq = from_seq .. out_seq-1:
    requeue(seq)   ← 보관한 프레임을 "원래 번호 그대로" 다시 큐에 넣는다
                     (enqueue를 쓰면 새 번호가 붙어 상대가 갭을 영영 못 메운다)
    큐가 차면 오류 → 끊는다 (반쯤 채운 채 기다리게 하지 않는다)
```

**시간이 하는 일 `session_tick(now_ms)`**

```
DOWN        → 0 (다시 붙는 것은 호출부가 한다)
now - last_rx_ms >= 15000 → drop, ERR_IO          ← "받은 지" 기준
LOGGING_IN  : now - login_at_ms >= 5000 → drop, ERR_NOT_LOGGED_IN
              아니면 0 (로그인 중엔 하트비트 안 보냄)
READY       : now - last_tx_ms >= 5000 → HEARTBEAT enqueue, 1  ← "보낸 지" 기준
              하트비트조차 큐에 못 넣으면 → drop
```

**끊기 `session_drop(now_ms)`**

```
state = DOWN, fd = -1, 조립기·송신 큐 비우기
track.recovering = false         ← "메우는 중"은 접속에 딸린 상태라 푼다
(expected, store는 지우지 않는다) ← 접속 사이에 놓친 것을 알아내려면 남겨야 한다
retry_at = now + backoff, attempts++
backoff = min(backoff × 2, 30000)
```

**숫자로 따라가기** (기본 설정, `now_ms`는 호출부가 넣는 값)

| now_ms | 호출 | 결과 |
|---:|---|---|
| 0 | `session_init` | DOWN, retry_at=0 → `should_connect` 참 (첫 접속은 안 기다린다) |
| 0 | `session_on_connected(fd)` | LOGGING_IN, LOGIN_REQ(seq=1) 큐에, last_tx=0 |
| 0 | `session_on_writable` | 큐 flush |
| 100 | `session_on_readable` ← LOGIN_ACK | READY, last_rx=100, backoff=200 |
| 5,000 | `session_tick` | 받은 지 4,900 < 15,000. 보낸 지 5,000 ≥ 5,000 → HEARTBEAT(seq=2) |
| 10,000 | `session_tick` | HEARTBEAT(seq=3) |
| 15,100 | `session_tick` | 받은 지 15,000 ≥ 15,000 → drop. retry_at=15,300, 다음 backoff=400 |

계속 실패하면 대기 시간은 200 → 400 → 800 → 1,600 → 3,200 → 6,400 → 12,800 → 25,600 →
30,000 → 30,000 …ms로 자라다 상한에서 멈춘다. **TCP가 붙은 것이 아니라 로그인이 된 것이 성공**
이므로 백오프는 `LOGIN_ACK` 성공 때만 처음으로 돌아간다.

###### 4.5 시퀀스 갭 — 숫자로 따라가기

`seqtrack.h`의 예 "14번 다음이 21번"을 따라간다. 상대가 보낸 14번까지 받았고
(`expected = 15`), 끊겨 있던 동안 상대가 15~20번 체결 통보를 보냈다고 하자.

| 도착 | 판정 | expected | recovering | 처리 |
|---|---|---:|---|---|
| 21 | GAP | 15 | true | RESEND_REQ(from_seq=15) 보냄. 21은 버림 |
| 22 | WAIT | 15 | true | 버림. 요청을 또 보내지 않음 |
| 15 (재전송) | OK | 16 | false | 위로 올림 |
| 16~20 | OK | 21 | false | 위로 올림 |
| 21 (재전송) | OK | 22 | false | 위로 올림 |
| 22 (재전송) | OK | 23 | false | 위로 올림 |
| 21 (한 번 더 겹쳐 옴) | DUP | 23 | false | 버림, dups++ |

`seqtrack.h`는 두 방식을 비교해 이 "버리고 다시 받기"를 골랐다. 갭 뒤에 온 것을 쌓아 두고
재정렬하는 방식은 "얼마나 쌓나", "안 채워지면 어쩌나"를 또 정해야 하고, 틀리면 **순서가 조용히
뒤바뀐 채** 올라간다. 체결 통보 순서가 뒤바뀌면 잔량 계산이 어긋난다. 이미 본 전문을 한 번
더 받는 것이 더 싼 값이다.

보관이 한 바퀴 돌아 15번이 이미 밀려났다면 보내는 쪽은 **`GAP_FILL`로 "그 앞은 없다"고
말한다.** 받는 쪽은 `seqtrack_skip_to`로 기대값을 옮겨 기다리기를 그만둔다. 말해 주지 않으면
양쪽 다 살아 있는데 아무것도 흐르지 않는 접속이 된다.

상대가 **재기동**해 번호를 1부터 다시 매기면, `LOGIN_ACK`의 번호가 기대값보다 작게 온다.
`seqtrack_restart_at`이 기대값을 그 번호+1로 낮춘다. 처리하지 않으면 재기동한 상대의 모든 전문이
중복으로 버려진다. 이 구멍은 T3-15 통합 테스트가 찾았다.

###### 4.6 주문번호 매핑과 미응답 주문 판정 — `ordmap`

**평소 흐름**

```
주문 보내기 전      ordmap_add(cl=5001)                  → PENDING, exch_id=0
ORDER_ACK 수신      ordmap_on_ack(5001, 90003, accepted) → LIVE, exch_id=90003
FILL_NOTI 수신      ordmap_find_by_exch(90003)           → 5001번 주문을 되찾는다
전량 체결·취소      ordmap_close(5001)                   → DONE (exch_id는 남긴다)
```

- 거부 응답이면 `DONE`이고 거래소 번호를 기록하지 않는다.
- 이미 다른 번호로 접수된 주문에 또 접수 응답이 오면 덮지 않고 `ERR_DUPLICATE`. 같은 번호로
  다시 오는 것(재전송)은 받아 준다.
- 같은 `cl_ord_id`를 두 번 `add`하면 `ERR_DUPLICATE`. 덮으면 앞 주문의 체결이 갈 곳을 잃는다.
- 거래소 번호는 거래소가 정하므로 산술로 찾을 수 없어 **선형 탐색**한다(ponytail 표시: 병목으로
  재이면 색인을 얹는다).

**응답 전에 취소하고 싶을 때 `ordmap_cancel_key`**: PENDING이면 거래소 번호 자리에 **0**을 준다.
`CANCEL_REQ`가 거래소 번호와 우리 번호를 둘 다 실으므로, 0이면 "우리 번호로 찾아라"는 뜻이
된다. 거래소가 주문을 받았다면 우리 번호로 찾아 취소하고, 못 받았다면 "없는 주문"으로 거부한다.
어느 쪽이든 모호함이 남지 않는다. `DONE`인 주문을 취소하려 하면 `ERR_NOT_SUPPORTED`다.

**끊겼을 때 — 판정 보류(in-doubt)**

응답을 못 받은 채 끊긴 주문은 세 갈래 중 어느 것인지 우리 쪽 정보만으로는 알 수 없다.

```
          주문 전문을 보냈다 ── 응답 오기 전에 접속이 끊겼다
                         │
     ┌───────────────────┼────────────────────┐
     ▼                   ▼                    ▼
 1. 거래소가 못 받았다  2. 받고 접수했다      3. 받고 거부했다
    → 주문 없음           → 살아 있는 주문 있음  → 주문 없음
```

흔한 처리 두 가지가 모두 위험하다.

- "응답이 없었으니 거부로 치자" → 실제로는 2였다면 **우리가 모르는 포지션**이 생긴다. 체결이
  와도 붙일 주문이 없고, 장 끝나고 잔고가 안 맞는다.
- "접수됐겠지" → 1이었다면 없는 주문을 취소하려 들고, 없는 포지션을 근거로 다음 주문을 낸다.
- **자동으로 다시 보내기** → 2였다면 **같은 주문이 둘**이 된다. `ordmap.h`는 이것을 "이 계층이
  저지를 수 있는 가장 비싼 실수"라고 적었다. 그래서 이 모듈은 **아무것도 다시 보내지 않고
  판정만 한다.**

판정 절차는 이렇다.

```
① ordmap_on_disconnect()          PENDING → INDOUBT (LIVE·DONE은 건드리지 않는다)
② 다시 붙고 로그인
③ QUERY_REQ(order_id = 0)         당일 전체 주문 조회
④ QUERY_ACK 한 건마다             ordmap_on_query_result(cl, exch, live)
                                    live → LIVE(번호 기록), 아니면 → DONE
⑤ last = 1 인 QUERY_ACK를 받으면   ordmap_finish_query()
                                    아직 INDOUBT인 것 = 거래소가 모르는 주문 → DONE
```

⑤가 성립하려면 **"응답이 끝났다"를 알아야 한다.** 응답에 없던 주문이 "아직 안 온 것"인지
"정말 없는 것"인지 구분해야 하기 때문이다. 그래서 `QUERY_ACK` 끝에 `last` 1바이트를 더했다
(T3-14). 조회가 도중에 또 끊겼다면 `finish_query`를 부르면 안 된다 — 아직 안 온 주문을 없다고
판정하게 된다. 또 조회는 **당일 전체**여야 한다. 미체결만 돌려주면 이미 체결된 주문이 "없는
주문"으로 판정된다.

`ordmap_indoubt_count()`가 0이 아니면 **사람이 알아야 한다**고 헤더는 적었다.

##### 5. 시스템 기법을 0부터

###### 5.1 왜 원장 방식으로는 안 되는가 — 이벤트 루프의 필요

원장 리스너는 **한 번에 한 접속**을 블로킹으로 다룬다. `read`에서 기다리는 동안 다른 일을 못
한다. FEP는 거래소 접속을 유지하면서 동시에 하트비트 시각도 봐야 하고, 접속이 여러 개일 수도
있다. 블로킹 `read` 하나에 멈춰 있으면 나머지가 전부 선다.

```
[블로킹, 한 번에 하나]                  [이벤트 루프]
read(A) ... 기다림 ... (B는 방치)       epoll_wait: "A는 읽을 것 있음, B는 쓸 자리 있음"
                                        A 읽기 → B 쓰기 → 다시 epoll_wait
```

###### 5.2 `epoll`과 논블로킹 I/O

**epoll**은 리눅스에 "이 fd들을 지켜보다가 준비된 것이 생기면 알려 줘"라고 부탁하는 기능이다.

```c
epfd = epoll_create1(EPOLL_CLOEXEC);          /* 감시자 하나 만들기 */
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);      /* fd 등록 (EPOLLIN=읽기, EPOLLOUT=쓰기) */
n = epoll_wait(epfd, evs, 256, timeout_ms);   /* 준비된 것이 생길 때까지(또는 시간 초과) 대기 */
```

`EPOLL_CLOEXEC`는 나중에 이 프로세스가 다른 프로그램을 `exec`할 때 이 fd를 넘겨주지 않게 한다.
`evloop_add`는 `EPOLLRDHUP`도 늘 얹어 상대가 쓰기 쪽을 닫은 것을 알아챈다.

**레벨 트리거 vs 에지 트리거** — `evloop.h`가 고른 것은 레벨 트리거다.

```
받은 버퍼에 10바이트가 있고, 콜백이 3바이트만 읽었다

레벨 트리거(LT): 남은 7바이트가 있는 한 다음 epoll_wait에서 또 알려 준다   ← 채택
에지 트리거(ET): "없음 → 있음"으로 바뀔 때만 알려 준다.
                 새 데이터가 안 오면 남은 7바이트에 대해 다시는 안 알려 준다 → 접속이 조용히 멈춘다
```

ET는 fd가 수만 개일 때 깨어나는 횟수를 줄이지만, FEP는 거래소 접속 몇 개를 다룬다. 얻는 것은
작고 잃는 것은 "덜 읽으면 조용히 멈추는" 재현 어려운 버그다. `test_level_triggered`는 10바이트를
3바이트씩 읽게 해 콜백이 4번 이상 불리는지 확인한다.

**논블로킹**: LT는 "읽을 것이 있다"까지만 말해 주고 **얼마나** 있는지는 말하지 않는다. 블로킹
소켓에 원하는 만큼 읽으려 하면 거기서 멈춘다. 그래서 fd를 논블로킹으로 바꾼다.

```c
flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

논블로킹 fd는 **당장 줄 것이 없으면 기다리지 않고** `-1`과 `errno = EAGAIN`(또는
`EWOULDBLOCK`)으로 돌아온다. 이것은 오류가 아니라 "지금은 없다"는 뜻이다. `session_on_readable`
은 `EAGAIN`이 나올 때까지 읽고 멈춘다.

###### 5.3 송신 큐와 배압(backpressure)

논블로킹 소켓의 `write`는 커널 송신 버퍼에 **자리가 있는 만큼만** 쓰고 그 바이트 수를 돌려준다.
자리가 아예 없으면 `EAGAIN`이다. 남은 바이트는 누군가 들고 있다가 자리가 나면 이어 보내야 한다.
그 자리가 `sendq_t`다.

```
우리 프로그램                     커널                      상대
session_send ─► [sendq 256KB] ─flush─► [송신 버퍼 수 KB] ─네트워크─► 받는 쪽 버퍼
                   ▲                        ▲
                   │ 차면 ERR_POOL_EXHAUSTED │ 차면 write가 EAGAIN
                   │ 을 호출부에 돌려준다    │
```

**배압**은 "받는 쪽이 느리면 그 느림이 보내는 쪽으로 거슬러 올라오는 것"이다. 상대가 느리면
커널 버퍼가 차고(`EAGAIN`), 그러면 우리 큐가 차고, 큐가 차면 `session_send`가 실패를 돌려준다.
`sendq.h`는 큐가 찼을 때의 선택지 셋 중 **호출부에 알리기**를 골랐다.

- 오래된 것을 버리기, 새 것을 버리기는 안 된다. **조용히 버린 주문은 "보냈는데 응답이 없는
  주문"과 구분되지 않는다.** 4.6절의 판정 보류 문제를 이 계층이 스스로 만드는 셈이다.
- 실패를 돌려주면 호출부가 정한다. 거부로 응답하든, 끊고 다시 붙든.

두 가지 세부 규칙이 더 있다.

- **전문을 반만 넣지 않는다(전부 아니면 전무).** 70바이트 자리에 100바이트 전문의 70바이트만
  넣으면 그 반쪽이 나가 버릴 수 있고, 받는 쪽은 길이가 틀린 전문을 보고 접속을 끊는다.
- **`EV_WRITE`는 보낼 것이 있을 때만 켠다.** 레벨 트리거에서 소켓은 버퍼에 자리가 있는 한
  "쓸 수 있음"이 계속 참이고 보통은 늘 자리가 있다. 켜 둔 채면 보낼 게 없는데도 루프가 쉬지
  않고 깨어난다.
- **`EAGAIN`과 `EPIPE`를 반드시 구분한다.** 하나는 기다리면 되고, 하나는 접속이 끝났다.
  구분하지 않으면 끊긴 접속에 영원히 재시도한다.

`test_partial_write`는 200,000바이트를 한 번에 밀어 넣어 **첫 flush가 반드시 일부만** 나가게
만들고, 받는 쪽을 비워 가며 이어 보낸 결과가 순서·내용 그대로인지, 실제로 여러 번에 걸쳐
나갔는지(`rounds > 1`) 확인한다. 전문이 70바이트인 동안에는 `write`가 늘 한 번에 끝나 부분
쓰기 경로가 한 번도 실행되지 않았기 때문이다.

###### 5.4 하트비트와 무응답 판정

TCP 접속은 상대 기계가 전원이 나가도 **한동안 붙어 있는 것처럼 보인다.** 아무도 보내지 않으면
끊긴 것을 알 방법이 없다. 그래서 두 가지를 따로 센다(`session.h`).

| | 기준 시각 | 질문 | 기본값 |
|---|---|---|---|
| 하트비트 보내기 | `last_tx_ms` "내가 보낸 지" | 상대에게 내가 살아 있음을 알려야 하나 | 5초 |
| 무응답 끊기 | `last_rx_ms` "내가 받은 지" | 상대가 살아 있나 | 15초 |
| 로그인 시간 초과 | `login_at_ms` | 로그인 응답이 오나 | 5초 |

- 주문을 활발히 보내는 중이면 그 전문이 곧 살아 있다는 신호이므로 하트비트를 덧붙이지 않는다
  (`test_no_heartbeat_when_sending`).
- 내가 아무리 보내도 상대가 죽었으면 소용없으므로, 보내는 것은 접속을 살려 두지 못한다
  (`test_sending_does_not_keep_alive`).
- 무응답 한계를 하트비트의 **3배**로 둔다. 2배면 한 번만 늦어도 끊긴다.
- 받은 하트비트에 하트비트로 **되받아치지 않는다.** 양쪽이 서로 답하면 조용한 접속이란 것이
  없어진다. 각자 자기 시계로 보낸다.
- HEARTBEAT 전문은 바디가 0바이트다. 헤더의 `seq`와 `ts`가 전부다.

###### 5.5 재접속과 지수 백오프

끊기면 바로 다시 붙지 않고 기다린다. 기다림을 실패할 때마다 두 배로 늘리는 것을 **지수 백오프**
라고 한다. 상대가 쓰러져 있을 때 계속 두드려 더 힘들게 하지 않기 위해서다. 상한(30초)을 두는
이유는, 없으면 오래 끊긴 세션이 상대가 돌아온 뒤에도 몇 시간 뒤에나 다시 붙기 때문이다.

**지터(무작위 흔들기)를 넣지 않는다.** 지터는 많은 클라이언트가 동시에 재접속해 상대를 다시
쓰러뜨리는 것을 막는 장치인데, 이 FEP는 거래소마다 접속이 하나다. 넣으려면 난수가 필요하고,
그러면 시드를 주입받아야 하며(프로젝트 규칙), 재접속 시각이 시험하기 쉬운 값이 아니게 된다.

세션은 **소켓을 직접 만들지 않는다.** `session_should_connect()`가 참이면 호출부가 소켓을 붙여
`session_on_connected()`로 넘긴다. 주소 해석·`connect`는 환경마다 다르므로, 상태 기계를 환경 없이
시험할 수 있게 떼어 놓은 것이다. 그래서 백오프 테스트에는 소켓이 하나도 나오지 않는다.

###### 5.6 시각을 읽지 않고 받는다

하트비트·타임아웃은 "몇 초 동안"이라서 시계가 필요해 보인다. 그러나 세션의 모든 진입점은
`now_ms`를 **인자로 받는다.** 시계를 읽는 것은 프로세스의 주 루프 몫이다("경계가 읽고 논리는
받는다").

- 테스트에 `sleep`이 없다. 15초 뒤를 보려면 `now`에 15000을 더해 넣는다. `test_session.c`
  전체가 한순간에 끝난다.
- 같은 입력에 같은 상태 변화가 나온다. 시계가 끼면 부하가 있는 기계에서 가끔 실패하는 테스트가
  되고, 가끔 실패하는 테스트는 결국 아무도 믿지 않는다.

###### 5.7 시퀀스 번호·갭·재전송·갭 건너뛰기

**TCP를 쓰는데 왜 갭이 생기나?** 한 접속 **안에서는** 생기지 않는다. TCP가 순서와 완전성을
보장한다. 갭은 **접속과 접속 사이**에 생긴다. 우리가 끊겨 있는 동안 상대가 보낸 체결 통보는
그 끊긴 소켓과 함께 사라진다. 다시 붙었을 때 "14번 다음이 21번"을 보게 되고, 그 사이 6건이
우리가 못 본 체결이다.

그래서 필요한 장치가 넷이다.

| 장치 | 누가 | 하는 일 |
|---|---|---|
| 헤더의 `seq` | 보내는 쪽 | 보내는 전문마다 1씩 오르는 번호. 재접속해도 이어진다 |
| `seqstore` (보관) | 보내는 쪽 | 최근 256개를 고리 버퍼에 들고 있다 |
| `RESEND_REQ(from_seq)` | 받는 쪽 → 보내는 쪽 | "from_seq부터 지금까지 전부 다시" |
| `GAP_FILL(next_seq)` | 보내는 쪽 → 받는 쪽 | "그 앞은 더 없다. next_seq부터 이어라" |

재접속을 넘어 살아남아야 하는 것과 버려야 하는 것을 가르는 것이 핵심이다.

| 살아남는다 (drop에서 안 지움) | 버린다 (drop에서 지움) |
|---|---|
| `out_seq` — 보낼 번호 | 조립기에 반쯤 쌓인 바이트 |
| `store` — 보낸 전문 보관 | 송신 큐에 못 보낸 바이트 |
| `track.expected` — 받을 번호 | `track.recovering` — 앞 접속에서 보낸 재전송 요청은 그 접속과 함께 사라졌다 |

`recovering`을 풀지 않으면 새 접속에서 갭을 봐도 `SEQ_WAIT`으로 삼켜 **다시는 요청하지 않으면서
영원히 기다린다**(`test_recovering_cleared_on_reconnect`).

###### 5.8 미응답(in-doubt) 주문과 자동 재전송 금지

4.6절에 흐름을 적었다. 시스템 기법으로서의 요점만 다시 모은다.

- 네트워크에서 "보냈다"와 "닿았다"는 다르다. 응답 없이 끊기면 **알 수 없음**이 정답이다.
- 알 수 없는 것을 추측으로 채우지 않고 `ORD_INDOUBT`라는 상태로 드러낸다.
- 시퀀스 재전송(5.7)과 주문 재전송은 다르다. 시퀀스 재전송은 **같은 번호 그대로** 보내므로
  받는 쪽이 중복을 걸러 낸다. 주문을 새로 다시 내면 거래소는 **새 주문**으로 받는다. 그래서
  주문은 자동으로 다시 보내지 않고, 조회로 판정한다.
- 판정은 "조회 응답이 끝났다(`last`)"를 확인한 뒤에만 한다.

###### 5.9 `socketpair`와 두 프로세스 테스트

`socketpair(AF_UNIX, SOCK_STREAM, 0, sv)`는 **이미 서로 연결된 소켓 한 쌍**을 만든다. `sv[0]`에
쓰면 `sv[1]`에서 읽힌다. 네트워크 주소가 필요 없는 같은 기계 안 스트림 소켓이다. fork 전에 만들면
부모와 자식이 한 끝씩 나눠 가질 수 있다.

```
socketpair ─► sv[0], sv[1]
fork()
 부모(FEP)       : close(sv[1]); sv[0]을 논블로킹으로 → session_on_connected(sv[0])
 자식(거래소 역할): close(sv[0]); peer_serve(sv[1])  → 블로킹 read/write로 단순하게
```

`test_evloop`, `test_sendq`, `test_session`도 `socketpair`로 "한쪽에 쓰고 다른 쪽에서 읽는" 상황을
프로세스 하나 안에서 만든다. 스트림 소켓이므로 부분 읽기·쓰기, `EAGAIN`, 상대 끊김이 TCP와 같은
모양으로 일어난다.

###### 5.10 SIGPIPE와 멈춤 플래그

`evloop_install_signals()`는 원장 리스너와 똑같이 SIGTERM·SIGINT에 `volatile sig_atomic_t`
플래그만 세우는 핸들러를 걸고(`SA_RESTART` 없이), SIGPIPE는 무시로 건다. 그래야 끊긴 소켓에
`write`해도 프로세스가 죽지 않고 `sendq_flush`가 `EPIPE`를 `ERR_IO`로 돌려줄 수 있다.
`test_integration.c`의 `main`도 맨 앞에서 SIGPIPE를 무시로 건다.

##### 6. 테스트가 보장하는 것

**`test_evloop.c`**
- 여러 fd를 동시에 감시하고 fd마다 제 콜백이 불린다.
- 레벨 트리거다 — 덜 읽어도 다음에 또 알려 주고, 다 읽으면 더는 알려 주지 않는다.
- 블로킹 fd는 등록이 거절되고, 해제한 fd는 콜백을 받지 않는다.
- 콜백 안에서 fd를 닫아도(연쇄로 닫아도) 같은 묶음의 남은 이벤트가 엉뚱하게 불리지 않는다.
- 반쯤 닫힘·끊김(hangup)을 알리고, 멈춤 신호에 빠져나온다.

**`test_framer.c`**
- 헤더가 여러 조각으로, 바디가 나뉘어, 여러 전문이 한 번에 와도 각각 조립한다.
- **1바이트씩 흘려 넣어도 결과가 같다**(모든 쪼개짐 경계를 훑는다).
- `body_len` 한도 초과를 바디를 기다리기 전에 거절하고, 틀린 magic·version은 재동기 없이 버린다.
- 버퍼 앞당기기(compaction)와 오래 돌리기에서도 어긋나지 않는다.

**`test_sendq.c`**
- 실제로 부분 쓰기를 만들고, 이어 보낸 결과가 순서·내용 그대로 도착한다.
- 자리가 모자라면 전문을 반만 넣지 않고 통째로 거절한다.
- `want_write`는 보낼 것이 있을 때만 참이고 부분 쓰기 뒤에도 켜져 있다.
- 상대가 끊긴 것(`ERR_IO`)과 자리가 없는 것(0)을 구분한다.

**`test_seqtrack.c`**
- 정상 순서는 통과하고, 이미 본 번호는 버린다.
- 갭은 한 번만 알리고, 메우는 동안 오는 것은 버리며, 채워지면 다시 흐른다.
- `GAP_FILL`로 건너뛰되 되돌리는 요청은 거절한다.
- 보관 고리가 한 바퀴 돈 뒤 밀려난 번호는 "없다"고 답한다(한 바퀴 전 전문을 내주지 않는다).

**`test_session.c`** (`sleep` 없이 `now_ms`를 넣어 시험)
- 로그인 전에는 업무 전문을 보내지 않고, 로그인 왕복·거절·시간 초과가 맞게 동작한다.
- 하트비트는 "보낸 지", 무응답 끊기는 "받은 지" 기준이다.
- 백오프가 두 배씩 커지다 상한에서 멈추고, 곱셈 넘침에도 안전하며, 로그인 성공 때 처음으로 돌아간다.
- 재접속 뒤 다시 주고받고, 반쯤 받은 조립 상태는 버려진다. 틀린 길이·너무 긴 바디·쓰레기 바이트면 끊는다.
- 갭이면 재전송을 요청하고, 중복은 버리며, 보관 밖 요청에는 `GAP_FILL`로 답한다. 재접속을 넘는
  갭도 잡고(`LOGIN_ACK`이 갭 너머 번호여도 로그인은 된다), 재접속 때 "메우는 중"이 풀린다.

**`test_ordmap.c`**
- 우리 번호·거래소 번호 양방향으로 찾고, PENDING은 거래소 번호로 찾히지 않는다.
- 응답 전 취소는 거래소 번호 자리에 0을 쓰고, 끝난 주문 취소는 거절한다. 거부된 주문에는 거래소 번호가 없다.
- 같은 번호 재등록은 거절하고, 표가 차면 끝난 것 중 **가장 오래 전에 들어온 것**부터 밀어내며, 살아 있는 것만 가득하면 거절한다.
- 끝난 주문에 늦게 온 체결도 제 주문을 찾는다.
- 끊기면 PENDING만 보류가 되고, 조회 결과가 세 갈래(살아 있음·끝남·없음)로 판정하며, 조회 도중 또 끊기면 보류가 유지된다.

**`test_integration.c`** — 두 프로세스 통합 (T3-15)

부모는 FEP(`session_t` + `ordmap_t`), 자식은 **진짜 매칭 엔진**(`exchange/`, KRX 규칙)을 돌리는
거래소 역할이다. 자식은 시작할 때 70,000원에 매도 30주, 70,100원에 매도 50주를 깔고, 거래소
주문번호를 90001부터 스스로 매긴다. 응답을 지어내지 않고 매칭 엔진이 낸 값을 전문에 싣는다.
모든 대기는 `poll`과 횟수 상한(400번)으로 하며, 상한에 닿으면 **매달리지 않고 실패**한다.

- **주문 한 건이 끝까지 간다** (`test_order_to_fill_end_to_end`)
  1. 부모가 로그인해 READY가 된다.
  2. `ordmap_add(5001)` 후 70,100원 × 60주 매수를 보낸다.
  3. 자식의 매칭 엔진이 두 가격대를 소진한다: 30주 @70,000 + 30주 @70,100.
  4. 부모는 ORDER_ACK 1건과 FILL_NOTI 2건을 받는다. 체결 합계 60주, 대금
     70,000×30 + 70,100×30 = 2,100,000 + 2,103,000 = **4,203,000원**이 매칭 엔진이 낸 값과 같다.
  5. 5001번에 거래소 번호가 붙었고, 체결 통보가 그 거래소 번호로 5001번을 되찾으며 양방향 조회가 같은 항목을 가리킨다.

- **끊기고, 다시 붙고, 판정한다** (`test_disconnect_then_resolve`)
  1. 자식은 주문 하나를 처리하고 끊도록 띄운다.
  2. 6001번(70,000원 × 10주)을 보내 ORDER_ACK를 받는다 → LIVE.
  3. 6002번을 보내지만 거래소는 이미 끊었다 → 세션이 DOWN.
  4. `ordmap_on_disconnect()`가 **1건**만 옮긴다: 6002 = INDOUBT, 6001 = LIVE 그대로.
  5. `session_retry_in`만큼 시각을 넘기고 **우리 주문을 하나도 모르는 새 거래소**에 다시 붙어 로그인한다.
  6. `QUERY_REQ(order_id = 0)`을 보내면 새 거래소는 주문이 없으니 빈 응답 하나에 `last = 1`을 달아 보낸다.
  7. 끝 표시를 받고 `ordmap_finish_query()` → 6002는 **거래소에 닿지 않은 주문**으로 판정되어 DONE, 거래소 번호 0. 보류 건수 0.

이 테스트가 조각 테스트로는 못 찾던 구멍 하나(재기동한 상대의 `LOGIN_ACK` 번호가 기대값보다
작아 모든 전문이 중복으로 버려지던 문제)를 찾아 `seqtrack_restart_at`이 생겼다.

---

### 4.4 channel · web — 채널계와 화면

#### 채널계와 화면 읽기 안내 (channel/ · web/)

이 장은 저장소의 두 "바깥 계층"을 다룬다. `channel/`은 Java 17과 Spring Boot 4로 만든 **채널계**이고, `web/`은 React와 TypeScript, Vite로 만든 **화면**이다. C로 짠 원장·매칭 엔진·SOR은 이 둘 뒤에 숨어 있다. 이 장만 읽어도 "화면에서 매수 버튼을 누르면 무엇이 어디로 가는가"를 끝까지 따라갈 수 있게 쓴다.

설명은 모두 지금 디스크에 있는 파일을 기준으로 한다. 원장 끊김 방송(T6-10)은 커밋 `e14d692`에 들어갔고, 작업 트리에는 아직 커밋하지 않은 변경 셋이 더 있다. `LedgerConnectionPool.java`(세마포어로 상한 지키기, T6-11), `LedgerConnectionPoolTest.java`(그 테스트 둘), `web/src/lib/api.ts`(예상 못 한 응답 본문 막기)다. 이 글은 그 변경이 들어간 상태를 설명한다.

---

#### 1. 큰 그림: 요청 하나가 지나가는 길

```
브라우저 (web/, http://localhost:5173)
   │  fetch("/api/orders")  ·  new WebSocket("/ws/stream")
   ▼
Vite 개발 서버의 프록시  ── /api, /ws 를 8080으로 넘긴다
   │
   ▼
채널계 (channel/, Spring Boot, 8080)
   │  JSON을 받아 고정 길이 이진 전문으로 바꾼다
   ▼  TCP 127.0.0.1:9100
원장 데몬 ledgerd (C)  ── 그 안에서 SOR과 매칭 엔진이 돈다
```

- 화면은 JSON으로 말하고, 원장은 "24바이트 헤더 + 정해진 길이의 바디"라는 이진 전문으로 말한다. 둘은 서로의 말을 모른다. **채널계는 그 사이에서 통역한다.** 실제 증권사에서 고객 화면(HTS/MTS)과 계좌 원장 사이에 있는 계층을 "채널계"라고 부르는 것과 같은 자리다.
- 채널계는 통역만 하지 않는다. 형식이 틀린 주문은 원장에 보내기 전에 막고(400), 원장의 답을 HTTP 상태 코드로 분명히 나누고(200/422/503/202), 결과를 WebSocket으로 모든 화면에 방송한다.

실행 순서는 저장소 `README.md`의 "실행"에 있다. 원장(WSL, `./build/ledger/ledgerd`, 9100번) → 채널계(Windows, `channel/`에서 `./mvnw.cmd spring-boot:run`, 8080번) → 화면(Windows, `web/`에서 `npm install` 후 `npm run dev`, 5173번) 순서로 띄운다.

---

#### 2. 채널계(channel/) 읽는 순서

처음 읽는 사람에게 권하는 순서다. 아래로 갈수록 앞의 것에 기대므로 이 순서를 지키면 "이건 어디서 왔지?"가 줄어든다.

1. `channel/pom.xml` — 무엇에 의존하는가
2. `channel/src/main/resources/application.properties` — 설정값
3. `channel/src/main/java/com/minisor/channel/ChannelApplication.java` — 시작점
4. `wire/` — 전문 규격. `WireType` → `WireField` → `WireMessage` → `WireHeader` → `WireEnums` → `WireCodec` → 메시지 클래스들(`OrderReq`, `OrderAck`, `BookReq`, `BookAck`, 그리고 아직 쓰이지 않는 `CancelReq`, `CancelAck`, `QueryAck`, `FillNoti`)
5. `ledger/` — 원장과의 TCP. `LedgerProperties` → `LedgerException` → `LedgerConnection` → `LedgerConnectionPool`
6. `stream/` — 화면으로 밀어 보내기. `StreamEvent` → `StreamHub` → `StreamHandler` → `StreamConfig`
7. `api/` — REST 입구. `OrderRequestDto` → `OrderResponseDto` → `OrderService` → `OrderController` → `BookController`
8. `src/test/...` — 각 약속을 확인하는 테스트. `wire/WireCodecTest` → `wire/WireLayoutTest` → `ledger/FakeLedger` → `ledger/LedgerConnectionPoolTest` → `stream/StreamTest` → `api/OrderApiTest` → `ChannelStartupTests` → `ChannelApplicationTests`

`stream/`을 `api/`보다 먼저 읽는 이유는 `OrderService`와 `BookController`가 `StreamHub`를 부르기 때문이다.

---

#### 3. Spring Boot를 처음 보는 사람을 위한 기초

채널계 코드는 짧지만 Spring의 약속에 기대는 곳이 많다. 코드에 실제로 나오는 것만 짚는다.

##### 3.1 Spring Boot가 해 주는 일

보통 Java 프로그램은 `main`에서 필요한 객체를 전부 `new`로 만들고 서로 이어 붙인다. Spring은 그 일을 대신한다. 시작할 때 패키지를 훑어서 특정 어노테이션(`@Component`, `@Service`, `@RestController`, `@Configuration` 등)이 붙은 클래스를 찾고, 그 객체를 **하나씩만** 만들어 보관한다. 이렇게 Spring이 만들어 관리하는 객체를 **빈(bean)**이라고 부른다.

`ChannelApplication.java`의 `@SpringBootApplication`이 "이 패키지(`com.minisor.channel`)와 그 아래를 훑어라"는 표시이고, `SpringApplication.run(...)`이 실제로 훑고, 빈을 만들고, 내장 웹 서버(기본 8080)를 띄운다. `@ConfigurationPropertiesScan`은 `@ConfigurationProperties`가 붙은 설정 클래스(여기서는 `LedgerProperties`)도 찾아서 빈으로 만들라는 뜻이다.

##### 3.2 어노테이션별 의미

| 어노테이션 | 붙은 곳 | 뜻 |
|---|---|---|
| `@Component` | `LedgerConnectionPool`, `StreamHub` | "이 클래스로 빈을 하나 만들어라"의 가장 일반적인 표시 |
| `@Service` | `OrderService` | `@Component`와 동작은 같고, "업무 로직"이라는 역할 이름표다 |
| `@RestController` | `OrderController`, `BookController` | HTTP 요청을 받는 빈. 메서드가 돌려준 객체를 JSON으로 바꿔 응답 본문에 쓴다 |
| `@Configuration` | `StreamConfig` | 설정을 담는 빈. 여기서는 WebSocket 경로를 등록한다 |
| `@RequestMapping("/api/orders")` | `OrderController` 클래스 | 이 클래스의 경로 앞부분 |
| `@PostMapping` / `@GetMapping("/api/book")` | 메서드 | 어떤 HTTP 메서드·경로가 이 메서드로 오는가 |
| `@RequestBody` | 파라미터 | 요청 본문의 JSON을 이 타입으로 읽어라 |
| `@RequestParam` | 파라미터 | `?market=0` 같은 쿼리 문자열 값을 읽어라 |
| `@Valid` | 파라미터 | 읽은 객체의 검증 어노테이션을 확인하라. 어기면 400 |

##### 3.3 생성자 주입

`OrderController`를 보면 `OrderService`를 `new`하지 않는다.

```java
private final OrderService service;

public OrderController(OrderService service) {
    this.service = service;
}
```

Spring은 `OrderController` 빈을 만들 때 생성자의 파라미터 타입을 보고, 이미 만들어 둔 `OrderService` 빈을 넣어 준다. 이것을 **생성자 주입**이라고 부른다. `OrderService`는 다시 `LedgerConnectionPool`과 `StreamHub`를 생성자로 받고, `LedgerConnectionPool`은 `LedgerProperties`를 받는다. 결국 Spring이 이 사슬을 거꾸로 따라가며 필요한 것부터 만든다.

생성자가 하나면 Spring이 알아서 그것을 쓴다. **생성자가 둘 이상이면 어느 것을 쓸지 모른다.** `LedgerConnectionPool`에 공개 생성자와 패키지 전용 생성자가 둘 있고, 공개 생성자에 `@Autowired`를 붙여 "이것을 써라"고 알려 준다. 코드 주석에 따르면 처음에 둘 다 공개였을 때 컨텍스트가 뜨지 않았다.

##### 3.4 application.properties와 설정 바인딩

`application.properties`는 "이름=값" 줄의 모음이다. 채널계가 읽는 값은 다음과 같다.

```properties
minisor.ledger.host=127.0.0.1
minisor.ledger.port=9100
minisor.ledger.connect-timeout-ms=3000
minisor.ledger.read-timeout-ms=5000
management.endpoints.web.exposure.include=health
management.endpoint.health.probes.enabled=true
```

`LedgerProperties`는 `@ConfigurationProperties(prefix = "minisor.ledger")`가 붙은 `record`다. Spring은 `minisor.ledger.`로 시작하는 값을 읽어 record의 칸에 채운다. 파일에는 `connect-timeout-ms`처럼 하이픈으로 적고 코드에서는 `connectTimeoutMs`로 받는데, Spring이 두 표기를 같은 이름으로 맞춰 준다.

- `record`는 Java 16부터 있는 "값만 담는 불변 클래스"다. 칸 이름과 같은 조회 메서드(`host()`, `port()`)가 자동으로 생긴다. 주석은 "뜬 뒤에 바뀌지 않는 값"이라서 record로 두었다고 설명한다.
- 기본 포트가 9100인 이유도 주석에 있다. 처음엔 0이었는데, `isConfigured()`가 `port > 0`일 때만 참이라 그대로 띄우면 모든 주문이 503이었다.
- `management.*` 두 줄은 Spring Boot Actuator의 운영용 주소 중 `/actuator/health`만 연다. 전부 열면 환경 변수나 빈 목록이 밖으로 나간다.

##### 3.5 빈 검증(Bean Validation)

`@NotBlank`, `@Size`, `@Min`, `@Max`, `@AssertTrue`는 "이 값은 이래야 한다"는 규칙을 어노테이션으로 적는 표준(Jakarta Validation)이다. `pom.xml`의 `spring-boot-starter-validation`이 이것을 켠다.

- `@NotBlank` — 비어 있거나 공백뿐이면 안 된다
- `@Size(min=12, max=12)` — 문자열 길이
- `@Min(1)` / `@Max(65535)` — 숫자 범위
- `@AssertTrue` — 이 메서드가 `true`를 돌려줘야 한다. 연속 범위로 적을 수 없는 규칙을 코드로 적을 때 쓴다

검증이 도는 곳은 두 군데다.

1. **요청 본문** — `OrderController.submit`의 파라미터에 `@Valid`가 붙어 있다. 규칙을 어기면 Spring이 메서드를 부르지도 않고 400을 돌려준다. 그래서 형식이 틀린 주문은 원장에 닿지 않는다.
2. **설정** — `LedgerProperties`에 `@Validated`가 붙어 있다. 설정값이 규칙을 어기면(예: 포트 70000) 애플리케이션이 **뜨다가 죽는다.** 주석의 표현대로 "첫 주문에서야 실패"하는 것보다 낫다.

##### 3.6 Jackson: 객체와 JSON 사이

Spring Boot는 Jackson이라는 라이브러리로 Java 객체를 JSON으로 바꾸고(직렬화), JSON을 객체로 읽는다(역직렬화). Spring Boot 4는 Jackson 3을 쓰므로 패키지 이름이 `tools.jackson.databind`다(`StreamHub`의 import에서 보인다). 규칙은 단순하다.

- record의 칸 이름이 JSON 키가 된다. `OrderResponseDto(Outcome outcome, long clOrdId, ...)` → `{"outcome":..., "clOrdId":...}`
- enum은 이름 문자열이 된다. `Outcome.IN_DOUBT` → `"IN_DOUBT"`
- `Map`은 키-값 객체가 된다
- **`isXxx()`/`getXxx()` 모양의 공개 메서드도 속성으로 본다.** 그래서 `OrderRequestDto`를 JSON으로 쓰면 칸에 없는 `"marketKnown":true`가 붙는다(`isMarketKnown()` 때문이다). 컴파일된 클래스로 직접 직렬화해서 확인한 결과다. 7장의 방송 JSON에서 다시 나온다.

`@RestController`가 돌려준 객체는 Spring이 이 규칙으로 JSON 응답을 만든다. `StreamHub`는 Spring의 것을 받아 쓰지 않고 `new ObjectMapper()`로 자기 것을 만들어 쓴다.

##### 3.7 테스트용 Spring: @SpringBootTest와 무작위 포트

`@SpringBootTest(webEnvironment = RANDOM_PORT)`는 테스트 안에서 **진짜 애플리케이션을 통째로 띄우고**, 웹 서버를 비어 있는 아무 포트에 연다. `@LocalServerPort`가 붙은 필드에 그 포트 번호가 들어온다. 테스트는 JDK에 내장된 `HttpClient`로 `http://127.0.0.1:<port>/...`를 실제로 부른다. 포트를 무작위로 받는 이유는 여러 테스트가 동시에 돌거나 8080이 이미 쓰이고 있어도 부딪히지 않게 하려는 것이다.

설정을 테스트마다 바꾸는 방법이 둘 나온다.

- `@TestPropertySource(properties = "minisor.ledger.port=17001")` — 고정 값으로 덮어쓴다
- `@DynamicPropertySource` — 실행 중에 정해지는 값(가짜 원장이 받은 포트)으로 덮어쓴다

---

#### 4. wire/ — 전문 규격을 Java로 옮긴 곳

##### 4.1 전문이란

원장은 JSON을 모른다. 바이트를 정해진 자리에 정해진 크기로 늘어놓은 **고정 길이 전문**을 주고받는다. 모든 전문은 24바이트 헤더 뒤에 종별마다 길이가 정해진 바디가 붙는다. 숫자는 모두 **빅엔디언**(큰 자리 바이트가 먼저)이고, 필드 사이에 채움 바이트가 없다. C 쪽 규격은 `core/include/wire.h`(헤더)와 `core/include/msg.h`(바디)에 있다.

##### 4.2 WireType, WireField, WireMessage — 선언을 위한 어노테이션

`wire/`의 핵심 아이디어는 "**종별마다 직렬화 코드를 손으로 쓰지 않고, 필드에 규격을 적어 두면 코덱이 읽어서 처리한다**"이다.

- `WireType` — 필드 타입 다섯 개. `U8`(1바이트), `U64`(8), `I32`(4), `I64`(8), `STR`(길이는 따로 적음)
- `@WireField(order, type, length, count)` — 필드 하나의 규격
  - `order` — 바디 안의 차례. 1부터 빠짐없이. **Java 리플렉션이 돌려주는 필드 순서는 보장되지 않기 때문에** 순서를 선언 위치가 아니라 이 숫자로 정한다
  - `length` — `STR`일 때 바이트 길이
  - `count` — 같은 타입이 몇 개 이어지는가. 1보다 크면 필드는 `int[]`이고 타입은 `I32`만 된다. 호가 10단을 필드 40개로 풀어 쓰지 않으려고 더한 기능이다
- `@WireMessage(type, name)` — 클래스에 붙이는 종별 코드와 이름. 코드는 C `msg.h`의 `MSG_TYPE_LIST`와 같아야 한다

`@Retention(RUNTIME)`은 "실행 중에도 이 어노테이션을 읽을 수 있게 남겨라"는 뜻이다. 코덱이 실행 중에 읽어야 하므로 필요하다.

##### 4.3 메시지 클래스들

선언은 이렇게 생겼다(`OrderReq.java` 일부).

```java
@WireMessage(type = 1, name = "ORDERREQ")
public final class OrderReq {
    @WireField(order = 1, type = STR, length = 12)
    public String account;
    @WireField(order = 2, type = STR, length = 8)
    public String symbol;
    @WireField(order = 3, type = U64)
    public long clOrdId;
    @WireField(order = 4, type = U8)
    public int side;
```

| 클래스 | 종별 | 바디 길이 | 필드(차례대로) | 지금 쓰는 곳 |
|---|---|---|---|---|
| `OrderReq` | 1 | 39 | account(12) symbol(8) clOrdId(u64) side(u8) type(u8) market(u8) price(i32) qty(i32) | `OrderService` |
| `OrderAck` | 2 | 29 | clOrdId(u64) orderId(u64) status(u8) reason(i32) filledQty(i32) price(i32) | `OrderService` |
| `CancelReq` | 3 | 28 | account(12) orderId(u64) clOrdId(u64) | 없음 |
| `CancelAck` | 4 | 25 | orderId clOrdId status reason canceledQty | 없음 |
| `QueryAck` | 8 | 38 | orderId clOrdId symbol(8) status price qty filledQty last(u8) | 없음 |
| `FillNoti` | 9 | 46 | orderId clOrdId symbol(8) market side price qty remainingQty execId(u64) | 없음 |
| `BookReq` | 15 | 9 | symbol(8) market(u8) | `BookController` |
| `BookAck` | 16 | 169 | symbol(8) market(u8) bidPrice[10] bidQty[10] askPrice[10] askQty[10] | `BookController` |

- `BookAck`의 169는 8 + 1 + 10×4×4다. C 쪽도 "가격·수량 구조체 10개"가 아니라 "배열 넷"으로 나눠 이 모양에 맞췄다. 매수는 높은 가격부터, 매도는 낮은 가격부터 채우고, 없는 단은 가격·수량 모두 0이다.
- `CancelReq`의 주석: `orderId`가 0이면 `clOrdId`로 찾으라는 뜻이다.
- 취소·조회·체결 통보 클래스는 규격 대조(`WireLayoutTest`)에는 들어가지만, 지금 채널계에서 이것을 보내거나 받는 코드는 없다.

`clOrdId`는 "주문을 낸 쪽이 매긴 번호"(client order id), `orderId`는 원장이 매긴 번호다.

##### 4.4 WireHeader — 24바이트 공통 헤더

```
 0  2  magic    0x4D53
 2  1  version  (1)
 3  1  type     종별 코드
 4  4  bodyLen
 8  8  seq      이 접속에서 보낸 차례
16  8  ts       논리 시각
```

`WireHeader`는 record이고 `encode()`로 24바이트를 만들고 `decode(buf, off)`로 읽는다. 읽을 때 세 가지를 거절한다.

1. magic이 `0x4D53`이 아니면 — 전문이 아니거나 스트림 위치가 어긋났다
2. version이 1이 아니면 — 필드 배치가 다를 수 있다. 그대로 읽으면 "엉뚱한 값을 그럴듯하게" 돌려준다
3. bodyLen이 음수이거나 65536(`BODY_MAX`)을 넘으면 — 받는 쪽이 거대한 버퍼를 잡지 않게 한다

거절은 `WireException`(실행 시간 예외)을 던지는 것으로 한다.

##### 4.5 WireEnums — 숫자의 뜻을 C와 맞춘다

```java
public static final int SIDE_BUY = 0;
public static final int SIDE_SELL = 1;
public static final int ORDER_LIMIT = 0;   // MARKET 1, IOC 2, FOK 3, MIDPOINT 4
public static final int STATUS_NEW = 0;    // PARTIAL 1, FILLED 2, CANCELED 3, REJECTED 4
public static final int MARKET_KRX = 0;
public static final int MARKET_NXT = 1;
public static final int MSG_MARKET_AUTO = 255;
```

이 값들은 C `core/include/types.h`의 `side_t`, `order_type_t`, `order_status_t`, `market_t` 열거형과 **같아야 한다.** `MSG_MARKET_AUTO`만은 열거형이 아니라 `msg.h`의 `#define`이다(열거형에 넣으면 "시장마다 도는 반복문"이 없는 시장까지 돈다는 이유가 주석에 있다). 255는 "시장을 원장이 SOR로 정하라"는 뜻이다.

왜 이 파일이 따로 있는가가 이 프로젝트에서 중요한 교훈이다. 파일 주석에 적힌 사연은 이렇다.

- 처음에는 채널계와 화면이 **1부터 센 숫자**(매수=1, 지정가=1, KRX=1)를 전문에 그대로 실었다. C는 0부터 센다.
- 그래서 화면의 "KRX 지정가 매수"가 C에서 **"NXT 시장가 매도"**로 읽혔다.
- 어떤 테스트도 잡지 못했다. 당시 `WireLayoutTest`는 길이만 대조했고, 원장 데몬은 무조건 성공을 돌려줬기 때문이다.

바이트 배치가 맞는 것과 값의 뜻이 맞는 것은 다른 문제다. 그래서 숫자를 쓰는 곳은 전부 이 상수를 거치게 했고(`OrderRequestDto`의 `@Min(SIDE_BUY) @Max(SIDE_SELL)`처럼), `WireLayoutTest`가 C 헤더를 읽어 값까지 대조한다. 화면 쪽 짝은 `web/src/lib/wire.ts`다.

##### 4.6 WireCodec — 선언을 읽어 바이트를 만들고 읽는다

`WireCodec`은 모든 메서드가 `static`인 도구 클래스다. 흐름을 순서대로 보면 다음과 같다.

**(1) 배치 만들기 — `layoutOf(Class)`**

클래스의 필드를 훑어 `@WireField`가 붙은 것만 모으고, 각 필드의 바이트 크기를 계산하고(`STR`은 `length`, 배열은 크기×`count`), `order`로 정렬한다. 이 과정에서 선언이 잘못됐으면 바로 `WireException`을 던진다.

- `@WireMessage`가 없다
- `@WireField`가 하나도 없다
- 배열인데 `I32`가 아니거나 필드 타입이 `int[]`가 아니다
- 크기가 0 이하다
- `order`가 1, 2, 3, …으로 빠짐없이 이어지지 않는다(빠지거나 겹침)

결과는 `ConcurrentHashMap`에 클래스별로 한 번만 저장한다. 전문마다 리플렉션을 다시 돌 이유가 없기 때문이다. `ConcurrentHashMap.computeIfAbsent`는 여러 스레드가 동시에 불러도 안전하다.

**(2) `bodyLength(Class)`** — 배치의 크기를 모두 더한다.

**(3) `typeCode(Class)`** — `@WireMessage.type()`을 돌려준다.

**(4) `encodeBody(Object)` — 객체 → 바이트**

바디 길이만큼 빅엔디언 `ByteBuffer`를 잡고, 배치 순서대로 필드 값을 꺼내 넣는다. `U8`은 1바이트, `I32`는 `putInt`, `U64`/`I64`는 `putLong`, `STR`은 `putStr`, 배열은 `putInts`다.

- `putStr` — US-ASCII 바이트로 바꿔 넣고, 짧으면 남는 자리를 0으로 채우고, 길면 자른다. C의 `wire_put_str`과 같은 규칙이다. "전문은 길이가 규격이다."
- `putInts` — 배열이 `null`이면 전부 0으로 채운다. 길이가 선언과 다르면 예외다.

헤더는 만들지 않는다. `seq`와 `ts`는 "접속의 상태"라서 부르는 쪽(`LedgerConnection`)이 붙인다.

**(5) `decodeBody(Class, byte[], off, len)` — 바이트 → 객체**

먼저 **길이가 규격과 정확히 같은지** 본다. 짧으면 필드가 모자라고, 길면 규격이 다른 상대다. 같으면 기본 생성자로 객체를 만들고 배치 순서대로 값을 읽어 필드에 넣는다. `U8`은 `Byte.toUnsignedInt`로 부호 없이 읽는다(255가 -1이 되지 않게). `getStr`은 0바이트가 나오는 곳에서 끊는다.

코덱 주석은 한계도 적어 둔다. 규격이 C `msg.h`와 Java 선언 **두 곳에** 적혀 있으므로, 어긋나면 컴파일도 테스트도 통과하는데 주고받을 때만 조용히 틀린다. `WireLayoutTest`가 C 헤더를 읽어 대조하지만, 그 대조가 잡는 것은 **길이**이지 **필드 순서**가 아니다(10.2절).

---

#### 5. ledger/ — 원장과 TCP로 말하기

##### 5.1 LedgerProperties, LedgerException

`LedgerProperties`는 3.4절에서 본 설정 record다. `isConfigured()`는 `port > 0`이다. `LedgerException`은 "원장과의 통신이 실패했다"를 뜻하는 실행 시간 예외이고, 원인 예외를 함께 담을 수 있다.

##### 5.2 LedgerConnection — 접속 하나, 요청 하나에 응답 하나

생성자에서 소켓을 연다.

- `socket.connect(주소, connectTimeoutMs)` — 붙는 데 3초 이상 걸리면 포기한다
- `socket.setSoTimeout(readTimeoutMs)` — 읽을 때 5초 이상 아무것도 안 오면 `SocketTimeoutException`이 난다
- `socket.setTcpNoDelay(true)` — 네이글 알고리즘을 끈다. 네이글은 작은 패킷을 모아 보내려고 잠깐 기다리는데, 작은 전문을 주고받는 요청-응답에서는 그 기다림이 그대로 주문 지연이 된다

생성자가 패키지 전용(접근 제한자 없음)이라 `ledger` 패키지 밖에서는 `new`할 수 없다. 접속은 풀을 통해서만 얻는다.

핵심 메서드는 `call(request, responseType, ts)`다. 하는 일을 순서대로 적는다.

1. 이미 깨진 접속이면 바로 `LedgerException`
2. `WireCodec.encodeBody(request)`로 바디를 만든다
3. `WireHeader(VERSION, 종별, 바디길이, outSeq++, ts)`로 헤더를 만든다. `outSeq`는 **접속마다 따로** 1부터 센다
4. 헤더와 바디를 **한 배열로 합쳐 한 번에** `write`한다. 나눠 쓰다 중간에 실패하면 반쪽 전문이 상대에게 남기 때문이다
5. `readFully`로 응답 헤더 24바이트를 끝까지 읽고 해석한다
6. 헤더의 `bodyLen`만큼 바디를 끝까지 읽는다
7. 응답 종별이 기다리던 종별(`responseType`의 코드)과 다르면 접속을 깨진 것으로 표시하고 예외
8. 같으면 `decodeBody`로 객체를 만들어 돌려준다

`IOException`(시간 초과·끊김 포함)이나 `WireException`(해석 실패)이 나면 `broken = true`로 표시하고 `LedgerException`으로 바꿔 던진다.

**왜 한 번 어긋난 접속을 버리는가.** 요청을 보냈는데 5초 안에 답이 안 왔다고 하자. 원장은 그 요청을 처리하는 중일 수도 있고, 답을 조금 늦게 보낼 수도 있다. 그 접속을 다음 요청에 다시 쓰면, 다음 요청이 읽는 첫 응답은 **앞 요청의 늦게 온 답**이다. 응답이 한 칸씩 밀리고, 값이 그럴듯해서 한참 뒤에야 드러난다. 스트림이 한 번 어긋나면 고칠 방법이 없으므로 버리고 새로 붙는다.

`isBroken()`은 `broken` 표시뿐 아니라 소켓이 닫혔거나 연결되지 않은 경우도 참으로 본다. `close()`는 소켓을 닫고 그때 나는 오류는 무시한다.

이 객체는 스레드 안전하지 않다. 주석대로 "한 번에 한 스레드만 쓴다"는 약속을 풀이 지킨다.

##### 5.3 LedgerConnectionPool — 접속을 빌려 주고 돌려받는다

`@Component`이므로 애플리케이션에 하나만 있다. 상태는 넷이다.

- `idle` — 쉬고 있는 접속을 담는 `ArrayBlockingQueue`(크기 `maxSize`)
- `slots` — `Semaphore(maxSize)`. **빌려 갈 수 있는 자리의 수**이고, 상한을 실제로 지키는 것은 이것이다
- `alive` — 살아 있는 접속 수(빌려 간 것 + 쉬는 것). `AtomicInteger`라 여러 스레드가 동시에 더하고 빼도 안전하다. 지금은 테스트가 확인하는 "보고용" 값이다
- `closed` — 풀이 닫혔는가

세마포어는 "허가증 N장"이라고 생각하면 된다. `tryAcquire(시간)`은 한 장을 가져가되, 남은 것이 없으면 정해진 시간까지 누가 돌려주기를 기다린다. `release()`는 한 장을 돌려놓고 기다리던 스레드 하나를 깨운다.

**`borrow()` — 빌리기**

1. 닫혔으면 예외
2. 설정이 없으면(`port`가 0) "원장 접속 설정이 없다(minisor.ledger.port)" 예외. 엉뚱한 곳에 붙거나 한참 기다리지 않고 먼저 말한다
3. **자리부터 잡는다.** `slots.tryAcquire(borrowTimeoutMs)` — 자리가 없으면 그 시간만큼 기다리고, 끝내 못 잡으면 "원장 접속이 모자라다(N개가 모두 쓰이는 중)" 예외
4. 자리를 잡았으면 쉬는 접속을 꺼낸다. 깨졌으면(쉬는 동안 상대가 끊었으면) 버리고 다음 것을 본다. 멀쩡한 것이 있으면 그것을 준다
5. 쉬는 것이 없으면 새로 만든다(`create()`)
6. 4~5에서 예외가 나면(예: 원장에 못 붙음) 잡았던 자리를 돌려놓고 예외를 다시 던진다

`create()`는 `alive`를 먼저 올리고 접속을 시도하며, 실패하면 다시 내리고 "원장에 붙을 수 없다" 예외를 던진다. 실패한 접속을 수에 넣지 않기 위해서다.

**왜 자리를 먼저 잡는가(T6-11).** 주석에 따르면 예전 코드는 "살아 있는 수 < 상한"을 확인한 **뒤에** 수를 올렸다. 여러 스레드가 동시에 확인하면 모두 통과해 상한을 넘겨 접속을 만들 수 있었다. 원장은 접속을 하나씩만 받으므로, 넘친 접속으로 보낸 주문은 원장의 접속 대기열에 갇혀 있다가 한참 뒤 옛 가격으로 체결될 수 있었다. 화면이 KRX·NXT 호가를 **동시에** 읽으므로 첫 화면을 열 때마다 이 경합이 날 수 있었다. 허가증을 먼저 가져가면 동시에 몇 명이 오든 허가증 수보다 많이 통과할 수 없다.

**`release(c)` — 돌려주기**

풀이 닫혔거나, 접속이 깨졌거나, 큐에 자리가 없으면 **돌려받지 않고 닫아 버린다**(`discard`). 깨진 것을 큐에 넣으면 다음 요청이 그것을 집기 때문이다. 그리고 **어느 경우든 마지막에 `slots.release()`로 자리를 푼다.** 그래서 주석은 "빌린 접속마다 정확히 한 번 부른다"고 약속을 적는다. 두 번 부르면 허가증이 늘어나고, 안 부르면 자리가 영영 사라진다.

자리가 버려질 때도 풀리므로, 기다리던 요청은 **접속이 버려지는 순간에도 깨어나** 새 접속을 만든다. 예전에는 버린 접속이 대기열로 돌아오지 않아, 새로 만들 수 있는데도 대기 시간을 다 채운 뒤 "모자라다"로 실패했다.

**왜 풀인가, 왜 늘리지 않는가.** 요청마다 새로 붙으면 TCP 연결 비용이 주문 지연에 얹힌다. 그렇다고 무한정 늘리면 원장이 접속마다 자원을 잡으므로 모두가 함께 느려진다. 그래서 상한을 두고, 비면 정해진 시간만 기다렸다 실패한다. 무엇을 할지는 부르는 쪽이 정한다.

**왜 크기가 1인가.** Spring이 쓰는 공개 생성자는 `this(cfg, 1, 2000)` — 접속 1개, 대기 2초다. 주석이 이유를 적는다. 원장(`ledger_core.h`)은 호가창을 하나로 지키려고 **접속을 한 번에 하나씩 끝까지 처리한다.** 처음에 8개로 두었더니, 원장이 첫 접속을 붙들고 있는 동안 두 번째 접속은 받아지지도 않아서 동시 주문이 읽기 제한 시간(5초)만큼 멈췄다가 "확인 필요"로 떨어졌다. 접속이 하나면 요청이 풀 앞에서 줄을 서는데, 매칭은 마이크로초 단위라 줄이 짧다. 동시 처리가 필요해지면 원장을 여러 접속을 받는 구조로 바꾼 뒤에 이 값을 올려야 한다. 풀만 키우면 같은 멈춤이 돌아온다.

크기와 대기 시간을 받는 두 번째 생성자는 패키지 전용이고, 테스트가 크기 2나 4로 바꿔 보는 데 쓴다.

**밀려오는 전문은 다루지 않는다.** 체결 통보처럼 원장이 요청 없이 보내는 전문이 요청-응답 접속에 섞이면, 주문 응답을 기다리는 자리에 남의 체결이 온다. 그래서 이 풀은 요청-응답만 다룬다고 주석에 적혀 있다. 지금은 그런 구독 접속 자체가 채널계에 없다(8.3절의 ponytail 주석).

---

#### 6. stream/ — 화면으로 밀어 보내기

##### 6.1 WebSocket이란

HTTP는 "화면이 묻고 서버가 답한다"가 기본이다. 서버가 먼저 말을 걸 수 없다. **WebSocket**은 한 번 연결해 두면 양쪽이 아무 때나 메시지를 보낼 수 있는 통로다. 채널계는 주문 결과, 체결, 원장 상태를 모든 화면에 동시에 알리려고 이것을 쓴다. 화면이 채널계에 보내는 메시지는 받지 않는다(일방 방송).

##### 6.2 StreamEvent — 보내는 사건의 모양

```java
public record StreamEvent(String kind, Object payload) {
    public static StreamEvent fill(Object p)   { return new StreamEvent("fill", p); }
    public static StreamEvent order(Object p)  { return new StreamEvent("order", p); }
    public static StreamEvent book(Object p)   { return new StreamEvent("book", p); }
    public static StreamEvent ledgerDown(String why) { return new StreamEvent("ledger-down", why); }
    public static StreamEvent ledgerUp()       { return new StreamEvent("ledger-up", "연결됨"); }
}
```

(실제 파일은 메서드마다 여러 줄로 적혀 있다. 위는 줄여 옮긴 것이다.) JSON으로는 `{"kind":"...","payload":...}`가 된다. `book`을 만드는 메서드는 있지만 **부르는 곳이 없다.** 호가는 화면이 1초마다 직접 읽는다(12.3절).

##### 6.3 StreamHub — 구독자 목록과 방송

`@Component`라 하나뿐이다. 상태는 넷이다.

- `sessions` — 연결된 화면들. 세션 ID → 세션. `ConcurrentHashMap`
- `json` — 직렬화용 `ObjectMapper`
- `ledgerUp` — 마지막으로 본 원장 상태. 처음엔 `true`(붙어 있다고 본다)
- `ledgerDownWhy` — 마지막 끊김 사유

메서드별로 본다.

- `add(s)` — 목록에 넣는다. **원장이 이미 끊긴 상태면 이 새 화면에만 `ledger-down`을 따로 보낸다.** 끊긴 뒤에 들어온 화면도 사실을 알아야 하기 때문이다.
- `remove(s)`, `subscriberCount()` — 빼기, 세기
- `broadcast(event)` — 사건을 JSON 문자열로 한 번 바꾸고 모든 세션에 `send`한다
- `send(id, s, text)` — 한 구독자에게 보낸다
  - 세션이 이미 닫혔으면 목록에서 빼고 끝
  - `synchronized (s)` 안에서 보낸다. Spring의 WebSocket 세션은 여러 스레드가 동시에 `sendMessage`를 부르면 안 되므로 세션 단위로 잠근다
  - 보내다 `IOException`이나 `IllegalStateException`이 나면 **그 구독자만 목록에서 빼고 닫는다.** 나머지는 계속 받는다. 클래스 주석의 "느린 구독자 하나가 전체를 막지 않게"가 이것이다. 즉 이 코드의 느린/고장 난 구독자 처리는 "보내기가 실패하면 그 하나를 끊는다"이다. 따로 버퍼를 두거나 비동기로 보내지는 않는다
- `ledgerReachable(up, why)` — 원장과 주고받은 결과를 알린다. `BookController`와 `OrderService`가 원장 호출 성공·실패 때마다 부른다
  - `up`이 참: `compareAndSet(false, true)` — **끊김 → 연결로 바뀌는 순간에만** `ledger-up`을 방송한다
  - `up`이 거짓: 사유를 저장하고, `compareAndSet(true, false)` — **연결 → 끊김으로 바뀌는 순간에만** `ledger-down`을 방송한다

```java
public void ledgerReachable(boolean up, String why) {
    if (up) {
        if (ledgerUp.compareAndSet(false, true)) {
            broadcast(StreamEvent.ledgerUp());
        }
    } else {
        ledgerDownWhy = why == null ? "원인 미상" : why;
        if (ledgerUp.compareAndSet(true, false)) {
            broadcast(StreamEvent.ledgerDown(ledgerDownWhy));
        }
    }
}
```

`compareAndSet(기대값, 새값)`은 "지금 값이 기대값과 같으면 새값으로 바꾸고 참을 돌려준다"를 한 번에(다른 스레드가 끼어들 틈 없이) 한다. 그래서 두 스레드가 동시에 실패를 알려도 방송은 한 번만 나간다. **상태가 바뀔 때만 방송하는 이유**는 화면이 1초마다 호가를 읽기 때문이다. 원장이 죽어 있는 동안 실패할 때마다 보내면 같은 알림이 초마다 쌓인다. 사유 문구는 실패할 때마다 최신 것으로 바뀌므로, 끊긴 뒤에 새로 들어온 화면은 가장 최근 사유를 받는다.

클래스 주석에는 이 메서드가 생긴 이유도 있다. "원장이 끊기면 화면에 보인다"는 완료 조건은 예전부터 있었지만 `ledger-down`을 보내는 곳이 없어 화면의 "원장 끊김" 표시는 한 번도 켜진 적이 없었다. 테스트가 `broadcast`를 직접 불러서 통과했기 때문이다.

##### 6.4 StreamHandler, StreamConfig

- `StreamHandler`는 Spring의 `TextWebSocketHandler`를 상속하고, 연결되면 `hub.add`, 끊기면 `hub.remove`만 한다. 화면이 보내는 메시지를 처리하는 메서드는 재정의하지 않았다.
- `StreamConfig`는 `@Configuration` + `@EnableWebSocket`이고, `registerWebSocketHandlers`에서 `/ws/stream` 경로에 `StreamHandler`를 등록한다. `setAllowedOrigins("*")`로 어느 출처의 화면이든 WebSocket을 열 수 있게 했다. **이것은 WebSocket에만 해당하고, REST(`/api/...`)에는 CORS 설정이 없다.** 이 차이가 13.6절의 Vite 프록시가 필요한 이유다.

`StreamHandler`는 `@Component`가 아니라 `StreamConfig`가 `new`로 만들고 `StreamHub` 빈을 넘겨준다.

---

#### 7. api/ — REST 입구

##### 7.1 OrderRequestDto — 주문 요청 JSON의 모양과 검증

```java
public record OrderRequestDto(
        @NotBlank @Size(min = 12, max = 12) String account,
        @NotBlank @Size(min = 1, max = 8) String symbol,
        @Min(1) long clOrdId,
        @Min(SIDE_BUY) @Max(SIDE_SELL) int side,
        @Min(ORDER_LIMIT) @Max(ORDER_MIDPOINT) int type,
        int market,
        @Min(1) int price,
        @Min(1) int qty) {
```

- 계좌는 정확히 12자, 종목은 1~8자(전문 필드 길이와 같다)
- `clOrdId`, `price`, `qty`는 1 이상
- `side`는 0~1, `type`은 0~4. 범위를 숫자가 아니라 `WireEnums` 상수로 적는다. 처음에 `@Min(1) @Max(2)`로 적었다가 C와 한 칸씩 어긋났기 때문이다
- `market`은 0, 1, 255만 된다. 연속 구간이 아니라 `@Min/@Max`로 못 적으므로 `@AssertTrue isMarketKnown()`으로 적었다

이 검증은 "형식만 봐도 아는 잘못"만 거른다. 증거금·한도처럼 계좌 상태를 봐야 아는 것은 원장이 판단한다.

##### 7.2 OrderResponseDto — 응답의 모양과 Outcome

```java
public record OrderResponseDto(
        Outcome outcome, long clOrdId, long orderId, int reason,
        String message, int status, int filledQty, int avgPrice) {
    public enum Outcome { ACCEPTED, REJECTED, IN_DOUBT }
```

핵심은 `outcome`이다. 접수/거절 둘로는 부족하다. **원장에 보냈는데 답을 못 받은 경우**가 있고, 그것을 성공이나 실패로 단정하면 안 되기 때문에 세 번째 값 `IN_DOUBT`(모른다)가 있다.

만드는 정적 메서드가 셋이다.

- `accepted(clOrdId, orderId, status, filledQty, avgPrice)` — `reason` 0, `message` "접수"
- `rejected(clOrdId, reason, message)` — `orderId`, `status`, `filledQty`, `avgPrice`는 0
- `inDoubt(clOrdId, message)` — `reason`을 포함해 숫자는 모두 0

`status`·`filledQty`·`avgPrice`는 원장이 돌려준 값 그대로다. `avgPrice`에는 `OrderAck.price`가 들어간다.

##### 7.3 OrderService — 보내고, 판정하고, 방송한다

`@Service`. 생성자로 `LedgerConnectionPool`과 `StreamHub`를 받는다. `logicalClock`(`AtomicLong`, 1부터)은 전문 헤더의 `ts`에 실을 논리 시각이다. 프로젝트 규약대로 **시스템 시각을 읽지 않고** 호출마다 1씩 늘린다.

**`send(req)` — 원장과 한 번 왕복**

1. `OrderRequestDto`의 값을 `OrderReq` 전문 객체에 옮긴다
2. `pool.borrow()`
   - 실패하면: `hub.ledgerReachable(false, 사유)`를 부르고 예외를 **그대로 다시 던진다.** 아직 아무것도 보내지 않았으므로 "주문이 나가지 않았다"가 확실하다
3. `c.call(m, OrderAck.class, logicalClock.getAndIncrement())`
   - 성공하면: 접속을 돌려주고 `hub.ledgerReachable(true, null)`
     - `ack.reason != 0`이면 `rejected(ack.clOrdId, ack.reason, "원장이 거절했다")`
     - 아니면 `accepted(ack.clOrdId, ack.orderId, ack.status, ack.filledQty, ack.price)`
   - `LedgerException`이 나면: **보낸 뒤에 실패했다.** 원장에 닿았는지 모른다. `pool.release(c)`를 부르는데, `call`이 이미 `broken`으로 표시했으므로 풀은 이 접속을 버린다. `hub.ledgerReachable(false, 사유)`를 부르고 `inDoubt(req.clOrdId(), "원장 응답을 받지 못했다. 조회로 확인해야 한다")`를 돌려준다

**왜 자동으로 다시 보내지 않는가.** 답을 못 받은 주문을 다시 보내면, 원장이 사실은 첫 주문을 처리했을 경우 **같은 주문이 두 번** 들어간다. 중복 주문은 이 계층이 저지를 수 있는 가장 비싼 실수다. 그래서 "모른다"고 정직하게 답하고 판단을 사람에게 넘긴다.

다만 클래스 주석(ponytail 표시)이 스스로 밝히듯 "조회로 확인해야 한다"는 지금 **원칙**일 뿐 화면에서 할 수 있는 일이 아니다. 원장은 주문번호 조회에 답하지만 채널계에는 그것을 여는 API가 없고, 답을 못 받은 주문은 원장 주문번호도 모르므로 `clOrdId`로 찾는 조회가 전문에 먼저 생겨야 한다.

**`submit(req)` — 바깥에서 부르는 메서드**

```java
OrderResponseDto res = send(req);
hub.broadcast(StreamEvent.order(Map.of("request", req, "result", res)));
if (res.filledQty() > 0) {
    hub.broadcast(StreamEvent.fill(Map.of(
            "clOrdId", res.clOrdId(), "orderId", res.orderId(),
            "side", req.side(), "market", req.market(),
            "price", res.avgPrice(), "qty", res.filledQty())));
}
return res;
```

(실제 파일은 줄바꿈이 더 많다.) `send`가 정상적으로 돌아오면(접수·거절·모름 모두) `order` 사건을 방송하고, 체결 수량이 있으면 `fill` 사건도 방송한다. 주문을 낸 화면뿐 아니라 **다른 화면도 같은 것을 보게** 하려는 것이다.

`send`가 예외를 던지면(풀에서 못 빌림) 방송 없이 예외가 컨트롤러로 올라간다. 즉 **503인 주문은 방송되지 않는다.**

한계도 주석에 있다. 원장은 요청-응답만 하므로 **예전에 걸어 둔 주문이 나중에 체결된 것**은 채널계가 알 수 없고 방송하지 못한다. 방송되는 `fill`은 "이 주문을 넣는 순간 바로 체결된 몫"뿐이다. `fill`의 `market`도 원장이 실제로 체결한 시장이 아니라 요청의 시장 값이라, SOR 자동(255)으로 낸 주문이면 255가 실린다.

##### 7.4 OrderController — 상태 코드로 말한다

```java
@PostMapping
public ResponseEntity<OrderResponseDto> submit(@Valid @RequestBody OrderRequestDto req) {
    OrderResponseDto res;
    try {
        res = service.submit(req);
    } catch (LedgerException e) {
        return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE)
                .body(OrderResponseDto.rejected(req.clOrdId(), -16, e.getMessage()));
    }
```

그 뒤에 `outcome`에 따라 `ACCEPTED → ok(200)`, `REJECTED → unprocessableEntity(422)`, `IN_DOUBT → accepted(202)`로 나눈다. `ResponseEntity`는 "상태 코드 + 본문"을 함께 돌려주는 Spring의 포장 타입이다.

상태 코드를 뭉개지 않는 이유는 부르는 쪽이 **"고쳐서 다시"인지 "기다렸다 다시"인지 "다시 보내면 안 되는지"**를 알아야 하기 때문이다.

| 코드 | 언제 | 본문 | 부르는 쪽이 할 일 |
|---|---|---|---|
| **200 OK** | 원장이 접수했다(`reason` 0) | `outcome: "ACCEPTED"`, 원장 주문번호, 상태, 체결 수량·가격 | 끝. 체결 여부는 `status`·`filledQty`로 본다 |
| **400 Bad Request** | `@Valid` 검증 실패, JSON 형식 오류 | Spring 기본 오류 본문(이 DTO 모양이 아니다) | 입력을 고쳐서 다시. **원장까지 가지 않았다** |
| **422 Unprocessable Entity** | 원장이 거절했다(`reason` ≠ 0, 예: 증거금 부족) | `outcome: "REJECTED"`, `reason`에 C 오류 코드, `message` "원장이 거절했다" | 그대로 다시 보내도 또 거절된다. 조건을 바꿔야 한다 |
| **503 Service Unavailable** | 원장 접속을 빌리지 못했다(원장이 꺼짐, 설정 없음, 풀 대기 2초 초과) | `outcome: "REJECTED"`, `reason: -16`, `message`에 예외 문구 | 주문은 **확실히 나가지 않았다.** 원장을 살린 뒤 다시 보내도 된다 |
| **202 Accepted** | 보냈지만 답을 못 받았다(읽기 시간 초과, 스트림 어긋남) | `outcome: "IN_DOUBT"`, `message` "원장 응답을 받지 못했다. 조회로 확인해야 한다" | **모른다.** 다시 보내면 중복 주문이 될 수 있다. 확인이 먼저다 |

- 503 본문의 `outcome`이 `"REJECTED"`인 점에 주의한다. 상태 코드가 503이고, 본문 모양은 `rejected()`를 재사용했다. `-16`은 C `errors.h`에서 "원장에 연결하지 못함"에 해당하는 코드로 화면이 옮겨 적은 값이다(`web/src/lib/wire.ts`).
- 풀 크기가 1이라, 앞 요청이 2초 넘게 접속을 붙들고 있으면 다음 요청은 원장이 살아 있어도 "접속이 모자라다"로 503이 되고, 그때도 `ledgerReachable(false, ...)`가 불린다. 코드가 두 원인을 구분하지 않는다.
- 202는 보통 "받아서 나중에 처리하겠다"는 뜻으로 쓰는 코드인데, 여기서는 "처리 결과를 모른다"를 표현하는 데 썼다.

##### 7.5 BookController — GET /api/book

```java
@GetMapping("/api/book")
public ResponseEntity<BookDto> book(
        @RequestParam int market, @RequestParam(defaultValue = "005930") String symbol) {
```

- 요청: `GET /api/book?market=0` (KRX) 또는 `?market=1` (NXT). `symbol`은 생략하면 `005930`
- 검증: `market`이 0·1이 아니거나(**255 자동은 여기서 안 된다**), 종목이 비었거나 8자를 넘으면 400. `market`을 아예 안 주면 Spring이 400을 낸다
- 처리: 풀에서 접속을 빌려 `BookReq`를 보내고 `BookAck`를 받는다. 성공하면 `ledgerReachable(true)`, `LedgerException`이면 `ledgerReachable(false, 사유)` 후 본문 없는 **503**. 접속은 `finally`에서 항상 돌려준다(깨졌으면 풀이 버린다)
- 응답: 원장의 배열 넷을 `Level(price, qty)` 목록 둘로 바꾼다. `levels()`는 **가격이 0인 첫 단에서 멈춘다.** 원장이 앞에서부터 채우므로 그 뒤는 모두 빈 단이다

응답 JSON 모양(컴파일된 record로 확인했다):

```json
{"symbol":"005930","market":0,
 "bids":[{"price":70000,"qty":10},{"price":69900,"qty":5}],
 "asks":[{"price":70100,"qty":7}]}
```

주문과 달리 조회는 **아무것도 바꾸지 않으므로** 답을 못 받아도 모호하지 않다. 그래서 202가 아니라 503이고, 다시 불러도 된다. 주석대로 화면이 1초마다 이것을 부르므로, 원장이 죽으면 채널계는 대개 여기서 가장 먼저 알게 된다.

`BookController`는 `OrderService`와 별도로 자기 `logicalClock`을 갖는다.

---

#### 8. 한 주문의 JSON 모양 (화면 ↔ 채널계)

모두 코드에서 끌어낸 모양이다. 7.3~7.5절의 record와 `Map`, `web/src/components/OrderTicket.tsx`의 호출에서 나온다. 값은 예시다.

##### 8.1 화면이 보내는 요청 — POST /api/orders

`OrderTicket`이 `submitOrder`에 넘기는 객체를 `JSON.stringify`한 것이 그대로 본문이 된다. 헤더는 `Content-Type: application/json`.

```json
{
  "account": "123456789012",
  "symbol": "005930",
  "clOrdId": 417283915,
  "side": 0,
  "type": 0,
  "market": 255,
  "price": 70000,
  "qty": 10
}
```

- `account`, `symbol`은 화면에 고정돼 있다
- `clOrdId`는 `Date.now() % 1_000_000_000` — 지금 시각(밀리초)을 10억으로 나눈 나머지
- `side`는 매수 0 / 매도 1, `type`은 항상 지정가 0
- `market`은 SOR 자동 255(기본) / KRX 0 / NXT 1

##### 8.2 채널계의 응답

200 접수(체결 4주, 나머지 대기 — `status` 1은 PARTIAL):

```json
{"outcome":"ACCEPTED","clOrdId":417283915,"orderId":1,"reason":0,
 "message":"접수","status":1,"filledQty":4,"avgPrice":70000}
```

422 원장 거절(예: 증거금 부족 -14):

```json
{"outcome":"REJECTED","clOrdId":417283915,"orderId":0,"reason":-14,
 "message":"원장이 거절했다","status":0,"filledQty":0,"avgPrice":0}
```

503 원장에 못 붙음(`message`는 예외 문구라 상황마다 다르다):

```json
{"outcome":"REJECTED","clOrdId":417283915,"orderId":0,"reason":-16,
 "message":"원장에 붙을 수 없다: java.net.ConnectException: ...","status":0,"filledQty":0,"avgPrice":0}
```

202 응답 없음:

```json
{"outcome":"IN_DOUBT","clOrdId":417283915,"orderId":0,"reason":0,
 "message":"원장 응답을 받지 못했다. 조회로 확인해야 한다","status":0,"filledQty":0,"avgPrice":0}
```

400은 Spring의 기본 오류 본문이라 위 모양이 아니다. 그래서 화면(`api.ts`)은 400이면 본문을 읽지 않고 스스로 `REJECTED` 응답을 만든다.

##### 8.3 WebSocket 사건 — ws://.../ws/stream

모두 `{"kind": ..., "payload": ...}` 모양이다.

**order** — 원장과 왕복이 끝난 주문마다(200/422/202일 때. 503은 없음):

```json
{"kind":"order",
 "payload":{
   "request":{"account":"123456789012","symbol":"005930","clOrdId":417283915,
              "side":0,"type":0,"market":255,"price":70000,"qty":10,"marketKnown":true},
   "result":{"outcome":"ACCEPTED","clOrdId":417283915,"orderId":1,"reason":0,
             "message":"접수","status":1,"filledQty":4,"avgPrice":70000}}}
```

- `request` 안의 `"marketKnown":true`는 3.6절에서 말한 Jackson의 동작 때문에 붙는다. 화면은 이 값을 쓰지 않는다
- `payload`는 `Map.of(...)`로 만들었으므로 `request`와 `result` 중 **어느 키가 먼저 나올지는 정해져 있지 않다.** 직접 직렬화해 보았을 때는 `result`가 먼저 나왔다. JSON을 객체로 읽는 쪽에는 상관없다

**fill** — 위 주문의 `filledQty`가 0보다 클 때만, `order` 바로 뒤에:

```json
{"kind":"fill",
 "payload":{"clOrdId":417283915,"orderId":1,"side":0,"market":255,"price":70000,"qty":4}}
```

`price`는 `avgPrice`, `qty`는 `filledQty`, `market`은 요청의 시장 값이다. 키 순서는 역시 정해져 있지 않다.

**ledger-down** — 원장 호출이 연결 상태에서 처음 실패한 순간 한 번, 그리고 끊긴 상태에서 새 화면이 붙을 때 그 화면에만:

```json
{"kind":"ledger-down","payload":"원장에 붙을 수 없다: java.net.ConnectException: ..."}
```

**ledger-up** — 끊김 상태에서 원장 호출이 처음 성공한 순간 한 번:

```json
{"kind":"ledger-up","payload":"연결됨"}
```

---

#### 9. 한 주문의 전체 여정 (시간 순서)

1. 사용자가 거래 탭의 주문 칸에서 "매수 주문"을 누른다 → `OrderTicket.send()`
2. `submitOrder()`가 `fetch("/api/orders", POST, JSON)`
3. Vite 개발 서버가 `/api`로 시작하는 요청을 `http://localhost:8080`으로 넘긴다
4. Spring이 JSON을 `OrderRequestDto`로 읽고 `@Valid` 검증 → 틀리면 여기서 400
5. `OrderController.submit` → `OrderService.submit` → `send`
6. `LedgerConnectionPool.borrow()` → 못 빌리면 503
7. `LedgerConnection.call()` — `OrderReq`를 39바이트 바디 + 24바이트 헤더로 만들어 원장에 쓰고, `OrderAck` 29바이트를 기다린다 → 5초 안에 안 오면 202
8. 원장이 SOR·매칭을 돌려 `OrderAck`로 답한다 → `reason`에 따라 200 또는 422
9. `StreamHub.broadcast`가 `order`(그리고 체결이 있으면 `fill`)를 모든 화면에 보낸다
10. HTTP 응답이 화면에 돌아와 `OrderTicket`이 결과 상자를 그린다
11. 모든 화면의 `useStream`이 `order`/`fill`을 받아 `App`의 주문·체결 목록을 갱신하고, 호가를 곧바로 다시 읽는다

10과 11은 **서로 다른 길**이다. 결과 상자는 HTTP 응답으로, 주문·체결 내역은 WebSocket으로 채워진다. WebSocket이 끊겨 있으면 결과 상자는 뜨는데 주문·체결 탭에는 아무것도 추가되지 않는다.

---

#### 10. 채널계 테스트 — 무엇을 약속하는가

테스트는 `channel/`에서 `./mvnw.cmd test`로 돈다(Windows). JUnit 5(`@Test`)와 AssertJ(`assertThat(...)`)를 쓴다. 각 테스트 클래스 주석에 어느 태스크의 완료 조건을 옮긴 것인지 적혀 있다.

##### 10.1 WireCodecTest — 코덱이 C와 같은 바이트를 만든다 (T4-02)

Spring 없이 도는 순수 단위 테스트다.

- `orderReqRoundTrip` — `OrderReq`를 인코딩하면 39바이트이고, 다시 읽으면 같은 값이다(`clOrdId`에 `Long.MAX_VALUE`까지)
- `extremesSurvive` — 음수·최솟값·최댓값이 그대로 돌아온다. u8 255가 -1로 읽히지 않는다. u64 최댓값은 Java에서 -1L로 보인다
- `bigEndianOnTheWire` — `0x0102030405060708`을 넣으면 첫 바이트가 `01`, 여덟째가 `08`이다. 리틀엔디언이면 C가 뒤집힌 수를 읽는다
- `fixedWidthStrings` — 짧은 문자열은 0으로 채우고, 긴 문자열(`ABCDEFGHIJK`)은 8자로 잘린다
- `intArraysInOrder` — `BookAck`가 169바이트이고, 배열 넷이 선언 순서대로 놓인다(바이트 9~12가 첫 매수 가격, 165~168이 마지막 매도 수량). 비운 배열은 0이 된다. 선언과 길이가 다른 배열은 예외다
- `rejectsWrongLength` — 38바이트나 40바이트로 39바이트짜리를 읽으려 하면 거절한다
- `headerRoundTrip`, `headerRejectsBadMagicAndVersion`, `headerRejectsHugeBody` — 헤더 왕복, magic·version 불일치 거절, 거대한 bodyLen 거절

##### 10.2 WireLayoutTest — C 헤더를 직접 읽어 대조한다

이 테스트가 특이한 점은 **Java 테스트가 C 소스 파일을 연다**는 것이다. 규격이 C와 Java 두 곳에 적혀 있으니, 한쪽만 고치면 여기서 깨지게 하려는 장치다.

**헤더 파일 찾기 — `findRepoFile(rel)`**

현재 작업 디렉터리에서 시작해 부모 폴더로 최대 5단계 올라가며 `core/include/msg.h`(또는 `types.h`)를 찾는다. `channel/`에서 테스트를 돌리면 한 단계 위인 저장소 루트에서 찾는다.

**`javaLayoutMatchesCHeader` — 바디 길이**

- `readCLengths()`가 `msg.h`를 읽는다. 줄 끝 역슬래시로 이어진 `#define`을 한 줄로 합치고, `#define MSG_이름 식` 줄마다 식을 계산한다
- `eval()`은 괄호를 지우고 `+`로 나누고, 각 항을 `*`로 나눠 곱한다. 숫자이거나 **이미 계산한 상수**면 값으로 쓰고, 모르는 것이 섞이면 포기한다. 예를 들어 `MSG_ORDER_REQ_LEN (MSG_ACCOUNT_LEN + MSG_SYMBOL_LEN + 8 + 1 + 1 + 1 + 4 + 4)`는 12+8+8+1+1+1+4+4 = 39, `MSG_BOOK_ACK_LEN (MSG_SYMBOL_LEN + 1 + MSG_BOOK_DEPTH * 4 * 4)`는 8+1+160 = 169다
- 8개 메시지 클래스 각각에 대해 `WireCodec.bodyLength(cls)`가 C 상수와 같은지 본다
- **헤더를 못 찾으면 실패한다.** "파일이 없어서 건너뜀"이 통과로 보이면 대조를 안 한 것보다 나쁘기 때문이다

**`headerLengthMatches`** — `WireHeader.LENGTH`가 24, `MAGIC`이 `0x4D53`. 이 둘은 C 파일을 읽지 않고 숫자로 확인한다.

**`javaEnumValuesMatchCHeader` — 열거값(4.5절의 사고를 막는 테스트)**

- `readCEnums()`가 `types.h`에서 주석을 지우고 `typedef enum { ... }` 블록을 모두 찾아 `이름 = 값`을 모은다. **값을 안 적은 항목은 앞 값 + 1**이라는 C 규칙을 따른다(`ORDER_LIMIT = 0, ORDER_MARKET, ...` → 0, 1, 2, …). 이것을 빼먹으면 둘째부터 전부 0으로 읽혀 틀린 Java 상수가 오히려 통과한다고 주석이 경고한다
- `MSG_MARKET_AUTO`는 `msg.h`의 `#define`이므로 `readCLengths()`에서 가져와 더한다
- `WireEnums`의 `public static int` 필드를 리플렉션으로 모두 모은다
- **양쪽 방향으로** 대조한다. Java에 있는 이름은 C에도 있고 값이 같아야 하고, C에 있는 이름은 Java에도 있어야 한다. C에 새 열거값이 생겼는데 Java가 모르면 여기서 깨진다

**`typeCodesAreDistinct`** — 8개 클래스의 종별 코드가 서로 겹치지 않는다.

**이 테스트가 잡지 못하는 것 — 필드 순서.** 길이의 **합**만 비교하므로, 합이 같으면서 순서만 다른 배치는 통과한다. 예를 들어 `OrderReq`에서 `price`(i32)와 `qty`(i32)의 `order`를 서로 바꿔도 길이는 39 그대로라 통과하지만, 원장은 가격 자리에서 수량을 읽는다. 같은 이유로 `side`·`type`·`market`(모두 u8) 사이의 순서 바뀜도 못 잡는다. 순서까지 잡으려면 C 쪽이 필드 배치를 기계가 읽을 수 있는 형태로 내보내야 하는데, 아직 하지 않았다. 코드 주석은 이것을 "한계를 알고 쓰는 것과 모르고 쓰는 것은 다르다"로 적어 둔다. 또 `-16` 같은 오류 코드(`errors.h`)는 대조 대상이 아니다.

##### 10.3 FakeLedger — 시험용 가짜 원장

`src/test/.../ledger/FakeLedger.java`는 테스트에서만 쓰는 **진짜 TCP 서버**다. 채널계 입장에서는 원장과 구분되지 않는다. "진짜 전문 규격으로 답한다 — 지어내면 시험이 아니다"가 주석의 원칙이다.

- 생성자에서 `ServerSocket(0)`으로 빈 포트를 받고, 접속을 받는 스레드를 띄운다. `port()`로 그 포트를 알려 준다
- 접속마다 스레드 하나로 `serve()`가 돈다: 헤더 24바이트 → 바디를 읽고, 요청 수를 세고, 종별이 `BookReq`면 호가를, 아니면 `OrderReq`로 보고 주문 응답을 만든다. 응답 헤더의 `seq`·`ts`는 요청 것을 그대로 돌려준다
- 주문 응답: `orderId = clOrdId + 100000`(어느 요청의 답인지 알아볼 수 있게), `filledQty = fillQty`, `status`는 체결이 있으면 1, 없으면 0, `price`는 요청 가격, `reason`은 항상 0
- 호가 응답: 매수 3단(70000, 69900, 69800 / 수량 `10+i+100×market`), 매도 2단(70100, 70200 / 수량 20, 21), 나머지 0
- 조절 손잡이: `setDelayMs`(답하기 전에 쉼), `setSilent(true)`(받고 답하지 않음 — 시간 초과를 만든다), `setFillQty`(체결 수량)
- 관찰: `connections()`(받은 접속 수), `requests()`(받은 요청 수)

##### 10.4 LedgerConnectionPoolTest — 풀의 약속 (T4-03)

Spring 없이 `FakeLedger`와 풀을 직접 만든다.

- `roundTrip` — 한 번 왕복하면 `orderId`가 100007이다
- `reusesConnections` — 20번 요청해도 접속은 1개. 풀을 두는 이유 그 자체
- `discardsConnectionOnTimeout` — 가짜 원장이 침묵하면(읽기 제한 200ms) `call`이 예외를 내고 접속이 깨진 것으로 표시되며, 돌려줘도 풀에 들어가지 않는다(쉬는 수 0, 살아 있는 수 0)
- `reportsWhenLedgerIsDown` — 원장을 닫은 포트로 빌리면 예외, 살아 있는 수는 0
- `refusesWhenUnconfigured` — 포트 0이면 "설정" 문구가 든 예외
- `failsWhenPoolExhausted` — 크기 2, 대기 150ms로 두 개를 빌린 뒤 셋째를 빌리면 **100ms 이상 기다렸다가** "모자라" 예외. 상한을 넘겨 만들지 않았다. 돌려주면 다시 빌릴 수 있다
- `concurrentBorrowNeverExceedsLimit`(작업 트리, T6-11) — 크기 1인 **빈** 풀에 스레드 16개를 `CyclicBarrier`로 한꺼번에 출발시켜 빌리고 돌려주게 한다. 이것을 200판 반복하며, 판마다 건네진 접속 객체가 **정확히 1개**인지 본다. 경합이 나노초 단위 틈에서만 나므로 한 판으로는 잘 안 걸려서 여러 판을 돌린다
- `waiterWakesWhenConnectionIsDiscarded`(작업 트리, T6-11) — 크기 1, 대기 3초. 첫 접속을 빌린 상태에서 다른 스레드가 빌리려고 기다리게 하고, 첫 접속을 시간 초과로 깨뜨려 돌려준다(버려진다). 기다리던 쪽이 **2초 안에** 새 접속을 받아야 한다. 대기 3초를 다 채우지 않았다는 뜻이다
- `concurrentCallsDoNotCrossAnswers` — 크기 4 풀에 스레드 8개로 64개 요청을 동시에 보내고(가짜 원장 지연 5ms로 겹칠 틈을 넓힘), **모든 요청이 자기 번호의 답을 받는지** 본다. 접속은 4개를 넘지 않는다. 주석은 이것을 "풀에서 가장 조용한 위험"이라 부른다

##### 10.5 StreamTest — 방송 (T4-05)

`@SpringBootTest(RANDOM_PORT)`로 애플리케이션을 띄우고 JDK `HttpClient`의 WebSocket으로 `/ws/stream`에 붙는다. `Sink`는 받은 문자열을 모으는 구독자다.

- `broadcastsToAllSubscribers` — 구독자 둘 모두 `fill` 사건을 받는다
- `ledgerDownIsVisible` — `hub.broadcast(ledgerDown(...))`를 직접 부르면 구독자가 받는다
- `goneSubscriberIsDropped` — 하나가 나가면 구독자 수가 1이 되고, 남은 쪽은 계속 받는다
- `manyEventsGetThrough` — 50개를 연달아 보내도 모두 도착한다

이 테스트들은 `broadcast`를 **직접** 부른다. 그래서 "실제 원장 실패가 방송으로 이어지는가"는 확인하지 못한다. 6.3절에서 말한 "화면의 원장 끊김 표시가 한 번도 켜지지 않았던" 사고가 이 틈에서 났고, 그 부분은 아래 두 클래스가 맡는다.

##### 10.6 OrderApiTest — REST 상태 코드와 방송 (T4-04, T6-04, T6-10)

`@SpringBootTest(RANDOM_PORT)` + `@DynamicPropertySource`로 `FakeLedger`의 포트와 읽기 제한 500ms를 설정에 넣는다. 가짜 원장은 클래스 전체가 공유하고(`static`), `@AfterEach`에서 손잡이를 원래대로 돌린다.

- `acceptedOrderReturns200` — 200, 본문에 `ACCEPTED`와 `100011`
- `malformedOrderReturns400` — 계좌를 `"12"`로 바꾸면 400이고 **가짜 원장의 요청 수가 늘지 않는다**(원장에 가지 않았다)
- `outOfRangeEnumReturns400` — `side` 2, `market` 2, `type` 5는 400이고 원장에 가지 않는다. 옛 1부터 세기 값이 경계에서 막힌다
- `autoMarketIsAccepted` — `market` 255는 200
- `noAnswerReturns202InDoubt` — 가짜 원장이 침묵하면 202, 본문에 `IN_DOUBT`와 "조회"
- `recoversAfterInDoubt` — 202 뒤에 다음 주문은 200이고 본문이 `100015`(자기 답)이다. 14번의 늦은 답을 받지 않았다는 것, 즉 깨진 접속을 버렸다는 증거다
- `bookComesFromLedger` — `/api/book?market=1`이 200이고 `{"price":70000,"qty":110}` 등이 들어 있으며 `"price":0`은 없다. `market` 2와 255는 400이고 원장에 가지 않는다
- `orderAndFillAreBroadcast` — 체결 4주로 설정하고 주문하면 구독자가 정확히 2개(`order`에 `ACCEPTED`, `fill`에 `"qty":4`·`"price":70000`)를 받고, 체결 0이면 `order`만 하나 더 받는다
- `ledgerDownAndUpAreBroadcast` — 침묵 상태에서 주문(202) 후 정상 주문(200)을 하면, `ledger-` 사건이 `ledger-down`, `ledger-up` 순서로 2개다

`422`(원장 거절)를 확인하는 테스트는 없다. `FakeLedger`가 `reason`을 항상 0으로 답하기 때문이다.

##### 10.7 ChannelStartupTests — 뜨는가, 설정을 읽는가 (T4-01, T6-10)

`@TestPropertySource`로 원장 포트를 **아무도 없는** 17001로 덮어쓴다.

- `healthResponds` — `/actuator/health`가 200이고 `"status":"UP"`
- `onlyHealthIsExposed` — `/actuator/env`, `/actuator/beans`는 200이 아니다
- `ledgerEndpointComesFromConfiguration` — 포트가 17001(덮어쓴 값)이고, 호스트·시간 제한은 파일 값이다. 기본값 9100을 그대로 읽고 통과하면 "코드에 박힌 값"과 구분되지 않으므로 **일부러 바꿔서** 확인한다
- `ledgerDownReachesSubscribers` — 구독한 뒤 호가를 두 번 조회하면(둘 다 503) 구독자가 `ledger-down`을 **정확히 1개** 받는다(바뀔 때만 방송한다). 그 뒤 새로 붙은 구독자도 1개 받는다(끊긴 뒤에 들어온 화면에 따로 보낸다)

##### 10.8 ChannelApplicationTests

`@SpringBootTest`로 컨텍스트가 뜨기만 하면 통과한다. 풀은 첫 `borrow()` 때 접속하므로 원장이 없어도 뜬다.

---

#### 11. 화면(web/) 읽는 순서

1. `web/package.json` — 쓰는 도구(`react`, `react-dom`, `vite`, `typescript`, `oxlint`)와 명령(`dev`, `build`, `lint`, `preview`)
2. `web/vite.config.ts` — 개발 서버 프록시
3. `web/.env.example` — 주소를 바꾸는 환경 변수(기본은 비워 둔다)
4. `web/index.html` → `web/src/main.tsx` — 시작점
5. `web/src/lib/wire.ts` → `types.ts` → `format.ts` → `api.ts` → `useStream.ts` — 화면이 기대는 도구들
6. `web/src/App.tsx` — 상태를 모두 쥐고 있는 최상위 컴포넌트
7. `web/src/components/Panel.tsx`, `StatusBar.tsx` — 틀
8. 거래 탭: `OrderBook.tsx` → `SorPanel.tsx` → `OrderTicket.tsx`
9. 주문·체결 탭: `Working.tsx` → `Fills.tsx`
10. 전략 비교·관제 탭: `Strategies.tsx`, `Ops.tsx`
11. `web/src/index.css` — 색·간격 변수

`web/README.md`는 Vite 템플릿이 만든 기본 문서 그대로라 이 프로젝트 설명은 없다.

---

#### 12. React를 처음 보는 사람을 위한 기초

##### 12.1 컴포넌트와 JSX

React 화면은 **컴포넌트**를 쌓아 만든다. 컴포넌트는 "화면 조각을 돌려주는 함수"다.

```tsx
export function Panel({ title, right, children, pad = true }: { ... }) {
  return (
    <section style={{ ... }}>
      {title && <header>...</header>}
      <div>{children}</div>
    </section>
  );
}
```

함수 안의 `<section>...</section>`처럼 HTML 비슷한 문법이 **JSX**다(TypeScript와 함께 쓰면 파일 확장자가 `.tsx`). `{...}` 안에는 JavaScript 식을 쓴다. `{title && <header/>}`는 "title이 있으면 header를 그린다", `{list.map((x) => <Row key=... />)}`는 "목록의 각 항목마다 Row를 그린다"는 뜻이다. 목록으로 그릴 때 `key`는 React가 항목을 구분하는 이름표다.

이 프로젝트는 CSS 파일 대신 `style={{ ... }}` 객체로 스타일을 직접 적고, 색·간격은 `index.css`의 CSS 변수(`var(--buy)` 등)를 쓴다. 매수는 빨강(`--buy`), 매도는 파랑(`--sell`)으로 국내 관행을 따른다.

##### 12.2 props — 부모가 넘겨주는 값

컴포넌트 함수의 인자가 **props**다. `<OrderTicket price={price} onPriceChange={setPrice} />`라고 쓰면 `OrderTicket` 함수가 `{ price, onPriceChange }`를 받는다. 값뿐 아니라 함수도 넘길 수 있다. 자식은 props를 바꾸지 않고, 바꾸고 싶으면 부모가 넘겨준 함수를 부른다. `children`은 여는 태그와 닫는 태그 사이에 넣은 내용이다(`<Panel>여기</Panel>`).

##### 12.3 useState — 기억하는 값

```tsx
const [tab, setTab] = useState<Tab>("trade");
```

컴포넌트 함수는 화면을 다시 그릴 때마다 처음부터 다시 실행된다. 그래서 보통 변수는 매번 초기화된다. `useState`는 다시 실행돼도 **값을 기억하는 칸**을 만든다. `tab`은 지금 값, `setTab`은 값을 바꾸는 함수다. `setTab("orders")`를 부르면 React가 값을 바꾸고 **화면을 다시 그린다.**

이전 값을 바탕으로 바꿀 때는 `setEvents((n) => n + 1)`처럼 함수를 넘긴다. 짧은 시간에 여러 번 바뀌어도 가장 최신 값에서 계산되게 하려는 것이다.

##### 12.4 useEffect — 그리기 바깥의 일과 정리

타이머를 걸거나, 서버에 묻거나, WebSocket을 여는 일은 "화면을 그리는 계산"이 아니다. 이런 일은 `useEffect` 안에서 한다.

```tsx
useEffect(() => {
  // 1) 할 일
  const t = window.setInterval(load, 1000);
  // 2) 정리 함수
  return () => window.clearInterval(t);
}, [bookTick]);
```

- 첫 번째 인자 함수는 화면이 그려진 **뒤에** 실행된다
- 두 번째 인자 `[bookTick]`은 **의존 배열**이다. 그 안의 값이 바뀔 때만 다시 실행한다. 빈 배열 `[]`이면 처음 한 번만 실행한다
- 함수가 돌려주는 함수가 **정리(cleanup)**다. 다시 실행되기 직전, 그리고 컴포넌트가 화면에서 사라질 때 불린다. 타이머를 끄거나 연결을 닫지 않으면 옛 타이머가 계속 돌아 같은 일을 여러 번 하게 된다

`main.tsx`의 `<StrictMode>`는 개발 모드에서 일부러 효과를 "실행 → 정리 → 다시 실행"해 정리를 빼먹은 코드를 드러낸다. 그래서 정리 함수가 제대로 있어야 한다.

##### 12.5 useCallback, useMemo, useRef

- `useCallback(fn, [의존])` — 함수를 **같은 객체로 유지**한다. 보통은 다시 그릴 때마다 함수가 새로 만들어진다. `App`의 `onEvent`는 의존이 `[]`라 처음 만든 함수가 계속 쓰인다
- `useMemo(() => 계산, [의존])` — 의존이 바뀔 때만 다시 계산하고 결과를 기억한다. `App`의 `bestOverall`이 `books`가 바뀔 때만 다시 계산된다
- `useRef(초기값)` — `.current`에 값을 담는 상자. 바꿔도 **화면을 다시 그리지 않는다.** 다시 그려도 같은 상자가 유지된다. `useStream`이 "가장 최신 콜백"을 담아 두는 데 쓴다

##### 12.6 Vite와 개발 서버

브라우저는 `.tsx`나 TypeScript를 바로 실행하지 못한다. **Vite**는 개발 중에 파일을 브라우저가 읽을 수 있게 그때그때 바꿔 주는 개발 서버다(`npm run dev`, 기본 `http://localhost:5173`). 파일을 저장하면 화면이 곧바로 갱신된다. `npm run build`는 `tsc -b`로 타입 검사 후 배포용 파일을 만든다.

`import.meta.env.VITE_...`는 Vite가 `.env` 파일의 `VITE_`로 시작하는 값을 코드에 넣어 주는 방법이다.

---

#### 13. web/ 파일별 설명

##### 13.1 index.html, main.tsx — 시작점

`index.html`에는 `<div id="root"></div>` 하나와 `/src/main.tsx`를 불러오는 스크립트뿐이다. `main.tsx`가 그 `div`를 찾아 `App`을 그린다.

```tsx
createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
```

`!`는 TypeScript에게 "이 값은 null이 아니다"라고 알려 주는 표시다. `index.css`도 여기서 불러온다.

##### 13.2 lib/wire.ts — 숫자의 뜻

`WireEnums.java`의 화면 쪽 짝이다. `SIDE_BUY 0`, `SIDE_SELL 1`, `ORDER_LIMIT 0`, `MARKET_KRX 0`, `MARKET_NXT 1`, `MARKET_AUTO 255`, `STATUS_NEW 0`, `STATUS_PARTIAL 1`, `STATUS_FILLED 2`. 주석이 4.5절의 사고를 다시 적고, "날숫자를 없애고 이 이름만 쓴다"고 정한다. **이 파일은 C 헤더와 자동 대조되지 않는다.** Java 쪽만 `WireLayoutTest`로 대조된다.

- `type Side = typeof SIDE_BUY | typeof SIDE_SELL` — 0 또는 1만 되는 타입
- `marketName(m)` — 0 → `"KRX"`, 1 → `"NXT"`, 그 밖(255 포함) → `"SOR"`
- `reasonText(code)` — C `errors.h`의 문구를 옮긴 표(-1 잘못된 인자, -14 증거금 부족, -16 원장에 연결하지 못함 등). 모르는 코드는 `사유 코드 N`. 주석대로 화면 글자일 뿐이라 틀려도 주문이 잘못 나가지는 않는다

##### 13.3 lib/types.ts — 화면 안의 데이터 모양

- `Market = "KRX" | "NXT"`
- `Level { price, qty }`
- `Book { market, bids, asks }` — 채널계 응답과 달리 `market`이 **문자열**이다(`api.ts`가 바꾼다). `symbol`은 타입에 없다
- `Fill { at, market, side, price, qty, clOrdId }` — `market`이 `MarketName`이라 `"SOR"`도 된다. 주석: SOR이면 어느 시장에서 체결됐는지는 응답에 없다

##### 13.4 lib/format.ts — 표시용 글자

- `won(n)`, `qty(n)` — `toLocaleString("ko-KR")`로 세 자리마다 쉼표
- `bp(n)`, `pct(n)` — 부호 붙은 bp, 백분율. 지금 컴포넌트에서 쓰는 곳은 없다
- `time(d)` — 24시간제 시각 문자열. 체결 목록의 시각에 쓴다

##### 13.5 lib/api.ts — REST 호출

```ts
const BASE = import.meta.env.VITE_API_BASE ?? "";
```

`VITE_API_BASE`가 없으면 빈 문자열이다. 그러면 요청 주소가 `/api/orders`처럼 **같은 출처**가 되고 Vite 프록시가 채널계로 넘긴다. `.env`와 `.env.example`은 둘 다 이 줄을 주석으로만 담고 있어 기본은 비어 있다.

- `OrderRequest`, `OrderResponse` 인터페이스 — 8장의 JSON과 같은 모양. `Outcome`은 `"ACCEPTED" | "REJECTED" | "IN_DOUBT"`
- `submitOrder(req)`
  - `fetch`로 POST
  - **400이면** 본문을 읽지 않고 `outcome "REJECTED"`, `reason -1`, `message "입력이 올바르지 않습니다"`인 응답을 직접 만든다. 400 본문은 Spring 기본 오류라 모양이 다르기 때문이다
  - 그 밖에는 본문을 JSON으로 읽되(읽기 실패면 `null`), **`outcome`이 세 값 중 하나일 때만** 그대로 `OrderResponse`로 돌려준다. 200/422/503/202는 모두 `OrderResponseDto` 모양이라 여기를 통과한다
  - `outcome`이 없거나 모르는 값이면(예: 예상 못 한 500 오류 본문) `outcome "REJECTED"`, `reason 0`, `message "채널계 오류 (HTTP 500)"`을 직접 만든다. 주석에 따르면 그대로 넘기면 `OrderTicket`이 `OUTCOME_STYLE[undefined]`를 읽다 화면이 통째로 멈춘다. 이 부분은 작업 트리의 커밋되지 않은 변경이다
  - `fetch` 자체가 실패하면(채널계가 꺼짐) 예외가 호출자로 올라간다
- `fetchBook(market)` — `GET /api/book?market=N`. `res.ok`가 아니면(503 등) 예외. 받은 `market` 숫자를 `marketName`으로 문자열로 바꿔 `Book`을 만든다

##### 13.6 vite.config.ts — 프록시와 CORS

```ts
const CHANNEL = 'http://localhost:8080'
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': CHANNEL,
      '/ws': { target: CHANNEL, ws: true },
    },
  },
})
```

**출처(origin)와 CORS.** 브라우저는 "주소의 프로토콜 + 호스트 + 포트"를 출처라고 부른다. 화면은 `http://localhost:5173`, 채널계는 `http://localhost:8080`이라 **출처가 다르다.** 브라우저는 보안상 다른 출처로 보내는 요청을 제한하고, 서버가 "이 출처는 허락한다"는 응답 헤더를 줄 때만 풀어 준다. 이 규칙이 CORS(Cross-Origin Resource Sharing)다. 특히 `Content-Type: application/json`인 POST는 본 요청 전에 `OPTIONS` **사전 요청(preflight)**을 먼저 보내 허락을 묻는다.

주석에 따르면 처음엔 화면이 `http://localhost:8080`을 직접 불렀고, 채널계에 CORS 설정이 없어서 브라우저가 사전 요청 단계에서 주문 POST를 막았다(T6-05에서 발견).

**프록시로 푼 방법.** 화면은 자기 출처(`5173`)의 `/api/...`와 `/ws/...`를 부른다. 브라우저 입장에서는 같은 출처라 CORS가 끼어들 일이 없다. Vite 개발 서버가 그 요청을 받아 **서버 쪽에서** `8080`으로 넘기고 답을 돌려준다. 서버끼리의 전달에는 브라우저의 CORS 규칙이 없다. `ws: true`는 WebSocket 업그레이드 요청도 넘기라는 뜻이다. 주석은 `vite preview`도 이 설정을 쓴다고 적는다.

그래서 채널계에 CORS 설정을 더하지 않았다. `.env.example`은 채널계를 다른 호스트에 둘 때만 `VITE_API_BASE`/`VITE_WS_URL`을 채우라고 안내하면서, 그러면 CORS 때문에 주문 POST가 막힌다고 경고한다.

##### 13.7 lib/useStream.ts — WebSocket 구독과 재연결

직접 만든 훅(hook)이다. 훅은 `use`로 시작하고 안에서 `useState`/`useEffect` 등을 쓰는 함수이며, 컴포넌트가 불러 쓴다.

```ts
const WS_URL =
  import.meta.env.VITE_WS_URL ??
  `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws/stream`;
```

기본 주소는 지금 페이지와 같은 호스트의 `/ws/stream`이다(프록시를 탄다). 돌려주는 값은 `{ state, attempt }` — 연결 상태(`"connecting" | "open" | "closed"`)와 끊긴 횟수다.

**최신 콜백 담기**

```ts
const cb = useRef(onEvent);
useEffect(() => { cb.current = onEvent; }, [onEvent]);
```

연결을 여는 효과는 `[]`로 한 번만 돈다. 그 안에서 `onEvent`를 직접 쓰면 처음 받은 함수에 묶인다. 그래서 `useRef` 상자에 최신 함수를 넣어 두고 메시지가 올 때 `cb.current(...)`를 부른다. 콜백이 바뀌어도 연결을 다시 열지 않는다.

**연결과 물러서며 다시 붙기(backoff)**

효과 안의 지역 변수: `ws`(지금 소켓), `timer`(재연결 예약), `closed`(정리됐는가), `backoff`(다음 대기, 500ms에서 시작).

- `connect()` — 상태를 `"connecting"`으로 두고 `new WebSocket(WS_URL)`
- `onopen` — `backoff`를 500으로 되돌리고 상태 `"open"`
- `onmessage` — `JSON.parse` 후 `cb.current(사건)`. 해석에 실패하면 버린다
- `onclose` — 정리된 뒤면 아무것도 안 한다. 아니면 상태 `"closed"`, `attempt` +1, `backoff`만큼 뒤에 `connect`를 예약하고, `backoff`를 두 배로(상한 10초) 늘린다. 대기는 500ms → 1초 → 2초 → 4초 → 8초 → 10초 → 10초…
- `onerror` — 소켓을 닫는다. 그러면 `onclose`가 불려 위의 재연결로 이어진다

물러서며 다시 붙는 이유는 채널계가 꺼져 있을 때 0.5초마다 끝없이 두드리지 않기 위해서다. 연결에 성공하면 대기는 처음으로 돌아가지만, `attempt`는 되돌리지 않고 계속 센다.

**정리** — `closed = true`로 두어 이후 `onclose`가 재연결하지 않게 하고, 예약된 타이머를 취소하고, 소켓을 닫는다. `StrictMode`의 "실행 → 정리 → 실행"에서도 연결이 둘로 늘지 않는다.

주석의 한 줄이 이 훅의 목적이다. "화면이 조용히 멈추면 사용자는 '시장이 조용한 것'과 구분하지 못한다."

##### 13.8 App.tsx — 상태를 모두 쥔 최상위

**상태(useState)**

| 이름 | 초기값 | 뜻 |
|---|---|---|
| `tab` | `"trade"` | 지금 탭 |
| `events` | 0 | 받은 WebSocket 사건 수 |
| `ledgerDown` | `null` | 원장 끊김 사유. `null`이면 정상 |
| `price` | 70000 | 주문 칸의 가격. 호가 클릭과 주문 칸이 함께 쓴다 |
| `fills` | `[]` | 체결 목록 |
| `orders` | `[]` | 주문 목록 |
| `books` | `[]` | KRX·NXT 호가창 |
| `bookError` | `null` | 호가 조회 실패 문구 |
| `bookTick` | 0 | 호가를 "지금 당장" 다시 읽게 하는 신호 |

**탭** — `TABS` 배열(거래 / 주문·체결 / 전략 비교 / 관제)을 버튼으로 그리고, 누르면 `setTab`. 아래 `main`에서 `tab === "trade" && (...)`처럼 지금 탭의 내용만 그린다. 다른 탭의 컴포넌트는 화면에서 빠진다(그래서 `Working`의 펼침 상태 같은 것은 탭을 오가면 초기화된다).

**호가를 1초마다 읽기**

```tsx
useEffect(() => {
  let alive = true;
  const load = () =>
    Promise.all([fetchBook(MARKET_KRX), fetchBook(MARKET_NXT)])
      .then((b) => { if (!alive) return; setBooks(b); setBookError(null); })
      .catch((e: unknown) => alive && setBookError(String(e)));
  load();
  const t = window.setInterval(load, BOOK_POLL_MS);
  return () => { alive = false; window.clearInterval(t); };
}, [bookTick]);
```

- 효과가 돌면 곧바로 한 번 읽고, 1초(`BOOK_POLL_MS`)마다 다시 읽는다. 원장은 호가 변화를 밀어 보내지 않으므로 화면이 직접 묻는다
- `Promise.all`로 KRX와 NXT를 동시에 묻고, 둘 다 성공해야 `books`를 바꾼다. 하나라도 실패하면 `bookError`에 문구를 넣고 **`books`는 마지막으로 성공한 값을 그대로 둔다**
- `alive` 표시는 정리된 뒤에 늦게 도착한 응답이 상태를 건드리지 않게 막는다
- 의존 배열이 `[bookTick]`이라, `bookTick`이 바뀌면 옛 타이머를 정리하고 곧바로 다시 읽은 뒤 새 타이머를 건다. 주문 결과가 오면 `bookTick`을 올려 1초를 기다리지 않고 호가를 갱신한다

**WebSocket 사건 처리 — `onEvent`**

`useCallback(..., [])`로 만든 함수를 `useStream(onEvent)`에 넘긴다. 사건마다 먼저 `events`를 1 올리고 `kind`에 따라 나눈다.

- `ledger-down` → `setLedgerDown(payload 문자열 또는 "원인 미상")`. 상단 표시줄과 관제 탭에 끊김이 뜬다
- `ledger-up` → `setLedgerDown(null)`
- `order` → `payload`를 `{ request, result }`로 읽어 `LogicalOrder` 하나를 만든다
  - `clOrdId`, `symbol`, `side`, `price`, `qty`는 요청에서
  - `legs`에는 `legOf(req, res)`로 만든 항목 **하나**
  - 목록 맨 앞에 넣고, 같은 `clOrdId`의 옛 항목은 빼고, 200개까지만 남긴다
  - `setBookTick(n => n + 1)`로 호가를 즉시 다시 읽게 한다
- `fill` → `{ market, side, price, qty, clOrdId }`를 읽어 `Fill`을 만들고(`at`은 지금 시각, `market`은 `marketName`으로 문자열) 맨 앞에 넣고 200개까지 남긴다

**`legOf(req, res)`** — 원장 응답을 주문 내역 한 줄로 바꾼다.

- 상태: `IN_DOUBT`면 `"IN_DOUBT"`, `REJECTED`면 `"REJECTED"`, 접수이면서 `status`가 `STATUS_FILLED`(2)면 `"DONE"`, 그 밖이면 `"LIVE"`
- `market`은 요청 시장의 이름(자동이면 `"SOR"`), `exchOrderId`는 원장 주문번호
- `price`는 체결이 있으면 평균 체결가, 없으면 주문 가격
- 거절이면 `note`에 `reasonText(reason)`

**`bestOverall`** — `useMemo`로 두 호가창의 최우선 매도 중 가장 낮은 값과 최우선 매수 중 가장 높은 값을 구한다. 호가창이 비어 있으면 `Math.min()`은 `Infinity`, `Math.max()`는 `-Infinity`가 되는데, 어떤 가격과도 같지 않으므로 강조가 켜지지 않을 뿐이다.

**배치** — 위에 `StatusBar`, 그 아래 탭, 그 아래 내용.

- 거래 탭: 세 칸 격자. `호가창 · 005930 삼성전자` 패널 안에 두 시장의 `OrderBook`을 나란히(오류가 있으면 위에 빨간 안내), `SOR 판단` 패널에 `SorPanel`(항상 매수 기준), `주문` 패널에 `OrderTicket`
- 주문·체결 탭: `Working`과 `Fills`를 나란히
- 전략 비교 탭: `Strategies`
- 관제 탭: `Ops`에 `state`, `ledgerDown`, `events`

##### 13.9 components/Panel.tsx — 제목 달린 상자

`title`(선택), `right`(제목 줄 오른쪽에 넣을 것, 선택), `children`, `pad`(기본 `true`, 안쪽 여백)를 받아 테두리 상자를 그린다. 내용이 넘치면 상자 안에서 스크롤된다. 모든 탭의 패널이 이것을 쓴다.

##### 13.10 components/StatusBar.tsx — 상단 표시줄

props: `state`, `attempt`, `ledgerDown`.

- 왼쪽: `mock-sor` · "복수시장 주문 집행"
- `ledgerDown`이 있으면 빨간 알약 모양으로 "원장 끊김 · 사유"
- 오른쪽: 연결 상태 점과 글자. `connecting` → 노랑 "연결 중", `open` → 초록 "실시간", `closed` → 빨강 "끊김". 끊긴 상태에서 `attempt`가 1 이상이면 "· 재시도 N"

이 줄 하나로 "채널계와의 연결"과 "채널계와 원장 사이의 연결"을 따로 보여 준다. 앞의 것은 브라우저가 스스로 알고, 뒤의 것은 채널계가 방송해 줘야 안다.

##### 13.11 components/OrderBook.tsx — 한 시장의 호가창

props: `book`, `onPick`(가격을 누르면 부를 함수), `bestOverall`.

- 머리: 시장 이름(KRX 주황, NXT 초록)과 스프레드(최우선 매도 − 최우선 매수. 한쪽이 비면 0으로 계산한다)
- 매도 단은 `flexDirection: "column-reverse"`로 **거꾸로** 쌓는다. 데이터는 낮은 가격부터 오지만, 화면에서는 가격이 위로 갈수록 높게 보이도록 가장 낮은 매도가가 구분선 바로 위에 온다
- 구분선 아래에 매수 단(높은 가격부터)
- 각 단은 `Row` 버튼이다. 잔량을 두 시장 전체 최대 잔량 대비 비율 막대로 깔고, 가격(매수 빨강/매도 파랑)과 수량을 쓴다. 누르면 `onPick(가격)` → `App`의 `setPrice` → 주문 칸 가격이 바뀐다
- **강조**: 그 시장의 첫 단이면서 두 시장 통틀어 최우선(`bestOverall`)과 가격이 같으면 굵게 쓴다

##### 13.12 components/SorPanel.tsx — "SOR 판단" 패널

props: `books`, `side`(App은 항상 `SIDE_BUY`를 넘긴다).

- 시장마다 최우선 호가를 뽑는다. 매수면 매도 1단(싸게 살 곳), 매도면 매수 1단
- 가장 유리한 가격(매수면 최솟값)을 `best`로 잡고, 같은 가격인 시장에 "유리" 표시와 초록 테두리
- 다른 시장에는 `best`와의 차이를 `+100`처럼 표시
- 막대는 "그 시장 최우선 잔량 ÷ 두 시장 최우선 잔량 합" 비중이고, 아래에 "체결 가능 N주 · M%"
- 맨 아래 고정 문구: Phase 2 측정 요약(BALANCED에서 KRX 단독 53% → 라우팅 100%)

**주의할 점**: 이 패널은 원장 안의 SOR 엔진이 실제로 내린 판단을 받아 오는 것이 아니다. **브라우저가 1초마다 읽은 호가 최우선 1단만으로 계산해 보여 주는 참고 화면**이다. 실제 배분은 주문을 `market: 255`로 보냈을 때 원장이 정하고, 그 내역(시장별로 나뉜 물리 주문)은 지금 응답에 실려 오지 않는다(13.14절).

##### 13.13 components/OrderTicket.tsx — 주문 칸

props: `price`, `onPriceChange`. 가격은 `App`이 쥐고(호가 클릭과 공유하려고), 나머지는 이 컴포넌트가 쥔다.

| 상태 | 초기값 | 뜻 |
|---|---|---|
| `side` | `SIDE_BUY` | 매수/매도 |
| `qty` | 10 | 수량 |
| `market` | `MARKET_AUTO`(255) | SOR 자동/KRX/NXT |
| `busy` | `false` | 보내는 중인가 |
| `result` | `null` | 마지막 응답 |

화면 구성: 매수/매도 버튼 둘(선택된 쪽이 색으로 채워짐) → 시장 버튼 셋("SOR 자동", "KRX", "NXT") → 가격 입력(`step` 100, 호가를 클릭해도 담긴다) → 수량 입력(1 미만으로 못 내려감)과 10/50/100/500 빠른 버튼 → 주문 금액(가격 × 수량) → 주문 버튼 → 결과 상자.

**`send()`**

1. `busy`를 켜고 이전 결과를 지운다
2. `submitOrder`에 8.1절의 객체를 넘긴다(계좌·종목 고정, `clOrdId`는 시각 기반, 유형은 지정가)
3. 응답을 `result`에 담는다
4. `fetch`가 예외를 내면(채널계에 못 붙음) `REJECTED`, `reason -16`, `"채널계에 붙지 못했다"`를 스스로 만든다
5. 끝나면 `busy`를 끈다

주문 버튼은 `busy`이거나 가격·수량이 0 이하면 눌리지 않는다. 한 번 누른 뒤 응답이 올 때까지 다시 누를 수 없는 것도 이 `busy` 덕분이다.

**결과 상자** — `OUTCOME_STYLE`로 색과 제목을 정한다.

- `ACCEPTED` 초록 "접수됨": 체결이 있으면 "N주 체결 · 평균 P원" + (전량이면 " · 전량", 아니면 " · 나머지는 호가창에 대기"), 체결이 없으면 "체결 없음 · 호가창에 대기"
- `REJECTED` 빨강 "거절됨": `reasonText(reason)`. 400이면 -1 "잘못된 인자", 503이면 -16 "원장에 연결하지 못함". `api.ts`가 모르는 본문을 대신 만든 경우는 `reason`이 0이라 "사유 코드 0"으로 보인다(`message`의 "채널계 오류 (HTTP N)"은 거절 때 표시되지 않는다)
- `IN_DOUBT` 노랑 "확인 필요": 채널계의 `message`와 함께 안내문 "원장 응답을 못 받았다. 다시 보내면 중복 주문이 될 수 있다 — 호가창에 걸렸는지 먼저 확인한다." 채널계에 조회 API가 없으므로, 화면이 사용자에게 제안할 수 있는 확인 수단이 호가창을 눈으로 보는 것이다

##### 13.14 components/Working.tsx — 주문 내역

`Leg`(시장, 원장 주문번호, 가격, 수량, 체결, 상태, 메모)와 `LogicalOrder`(주문번호, 종목, 방향, 가격, 수량, `legs`) 타입을 이 파일이 정의하고 `App`이 가져다 쓴다. 상태 이름은 `PENDING`(응답 대기), `LIVE`(접수), `DONE`(전량 체결), `REJECTED`(거절), `IN_DOUBT`(확인 필요)인데, `App`의 `legOf`는 `PENDING`을 만들지 않는다.

- 주문이 없으면 "주문이 없다"
- 주문마다 한 줄 버튼: 매수/매도, 종목, "가격 × 수량", 확인 필요 표시, 거절이면 "거절 · 사유" 표시(T6-13 — 접힌 줄에서도 대기 중인 주문과 구분되게), "체결/수량", 펼침 화살표, 체결 비율 막대
- 누르면 펼치고 다시 누르면 접는다. 처음 그려질 때(탭에 들어올 때) 맨 위 주문이 펼쳐져 있다
- 펼치면 `legs`마다 시장 이름, `#원장주문번호`(0이면 "—"), 가격 × 수량, 체결 수량, 상태(거절이면 사유 덧붙임)

주석의 ponytail 메모대로, 원장 응답에는 시장별로 나뉜 물리 주문이 없어서 **항상 한 줄**만 보인다. SOR이 여러 시장으로 나눈 내역을 보이려면 응답 전문에 그 목록을 실어야 한다.

##### 13.15 components/Fills.tsx — 체결 내역

- 없으면 "체결 내역이 없다"
- 맨 위에 목록 전체의 **평균 체결 단가**(Σ가격×수량 ÷ Σ수량, 반올림)와 총 수량. 매수·매도를 구분하지 않고 합산한다
- 그 아래 체결마다 시각, 시장(KRX/NXT/SOR), 매수/매도, 가격, 수량

##### 13.16 components/Strategies.tsx — 전략 비교(고정 데이터)

서버를 부르지 않는다. 파일 안의 `DATA` 배열에 Phase 2 측정 결과를 **직접 적어 둔** 화면이다(주석: `bench/results/strategies-2026-09-16.md`, 시드 20260916 한 장면).

| 시나리오 | KRX 단독 슬리피지 | KRX 단독 체결률 | SPLIT 슬리피지 | SPLIT 체결률 |
|---|---|---|---|---|
| BALANCED | 11bp | 53.46% | 1bp | 100.0% |
| KRX_THIN | 15bp | 5.36% | 2bp | 72.83% |
| NXT_THIN | 11bp | 53.46% | 1bp | 58.71% |
| CROSSED | 0bp | 21.28% | 0bp | 21.28% |

시나리오마다 "체결률 A% → B%"(나아졌으면 초록)와 KRX 단독·SPLIT 막대(체결률)와 bp(슬리피지)를 그린다. bp는 "접수 시점 통합 최우선호가 대비 슬리피지"다. 결론 문구: 복수시장 라우팅의 이득은 단가보다 체결률에서 먼저 온다. NXT_THIN에서 라우팅이 1bp 비싼 것은 더 많이 채우느라 비싼 호가까지 갔기 때문이다. 이 값은 측정 결과 파일이 바뀌어도 자동으로 따라가지 않는다.

##### 13.17 components/Ops.tsx — 관제

props: `wsState`, `ledgerDown`, `events`. 세 줄을 초록/빨강 점과 함께 보여 준다.

- 채널계 구독: `open`이면 "실시간 수신 중", 아니면 "끊김 — 자동 재시도"
- 원장: `ledgerDown`이 없으면 "정상", 있으면 그 사유
- 받은 사건: `N건`(항상 초록)

맺음 문구: "시장이 조용한 것"과 "우리가 못 받는 것"은 다르다.

---

#### 14. 알아 두면 좋은 한계와 어긋남 (코드에서 확인한 것)

읽다가 "이건 왜 안 되지?" 하고 멈추기 쉬운 지점을 모았다. 모두 현재 코드에서 확인한 사실이다.

1. **나중에 체결된 주문은 화면에 오지 않는다.** 채널계는 주문을 넣는 순간 원장이 돌려준 체결만 방송한다. 호가창에 걸려 있다가 다른 주문과 맞아 체결돼도 주문·체결 탭은 바뀌지 않는다(호가창 숫자는 1초 폴링으로 바뀐다).
2. **"조회로 확인"할 API가 없다.** `IN_DOUBT` 뒤에 사용자가 할 수 있는 것은 호가창을 보는 것뿐이다. `QueryAck`, `CancelReq` 등의 전문 클래스는 있지만 쓰는 API가 없다.
3. **SOR 판단 패널은 브라우저 계산이다.** 원장의 실제 배분이 아니다. 주문 내역도 시장별 물리 주문을 보여 주지 못한다.
4. **SOR 자동 주문의 체결 시장은 알 수 없다.** `fill` 사건의 `market`은 요청 값(255)이라 화면에 "SOR"로 뜬다.
5. **풀 크기 1.** 원장이 접속을 하나씩 처리하기 때문이다. 앞 요청이 2초를 넘기면 뒤 요청은 503이 되고, 그때도 원장이 끊긴 것으로 기록돼 `ledger-down`이 방송될 수 있다. 코드는 "원장이 죽음"과 "자리 대기 초과"를 구분하지 않는다.
6. **작업 트리가 움직이고 있다.** 이 글을 쓰는 동안 T6-10이 커밋됐고, 풀의 세마포어(T6-11)와 `api.ts`의 응답 검사는 아직 커밋 전이다. 다시 읽을 때 `git status`로 먼저 확인한다.
7. **`WireLayoutTest`는 필드 순서를 못 잡는다.** 길이와 열거값만 대조한다. 화면 쪽 `wire.ts`와 오류 문구 표는 자동 대조 대상이 아니다.
8. **`order` 방송의 `request`에 `marketKnown`이 붙고, `payload` 키 순서는 정해져 있지 않다.** 화면 동작에는 영향이 없다.
9. **CORS는 프록시로 피했다.** Vite 개발 서버(또는 `vite preview`) 없이 화면 파일을 다른 곳에서 열고 채널계를 직접 부르면 주문 POST가 막힌다. WebSocket은 `setAllowedOrigins("*")`라 열린다.
10. **전략 비교 탭은 고정 숫자다.** 벤치마크를 다시 돌려도 화면 숫자는 바뀌지 않는다.

---

## 5. 직접 띄워 보기

C는 WSL(Ubuntu), Java와 화면은 Windows에서 돌린다. 터미널 셋을 연다.

### 5.1 빌드와 테스트

```bash
# [WSL] 저장소로 이동
cd /mnt/c/Users/<사용자>/OneDrive/*/Study/mock-sor

# C 빌드와 테스트 — 58개 테스트가 모두 통과해야 한다
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

# 메모리 검사 빌드
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan && ctest --test-dir build-asan
```

```powershell
# [Windows] 채널계 테스트 — 40개
cd channel
./mvnw.cmd test

# [Windows] 화면 빌드 확인
cd ../web
npm install
npm run build
```

### 5.2 세 프로그램 띄우기

| 순서 | 어디서 | 명령 | 뜨면 보이는 것 |
|---|---|---|---|
| 1 | WSL | `./build/ledger/ledgerd` | `ledgerd 포트 9100 에서 대기` + 계좌·종목·기준가 |
| 2 | Windows `channel/` | `./mvnw.cmd spring-boot:run` | `Tomcat started on port 8080` |
| 3 | Windows `web/` | `npm run dev` | `Local: http://localhost:5173/` |

브라우저로 `http://localhost:5173`을 연다.

**주의할 점 (실제로 겪은 것)**

- PowerShell에서 `npm run dev -- --port 5173`처럼 쓰면 `--`가 사라져 Vite가 `5173`을 **폴더 이름**으로 알아듣는다.
  모든 주소가 404가 된다. 그냥 `npm run dev`를 쓴다(기본 포트가 5173이다).
- Vite는 `localhost`(IPv6 `::1`)에만 붙는다. `http://127.0.0.1:5173`은 연결되지 않는다.
- `web/.env`에 `VITE_API_BASE=http://localhost:8080`을 적으면 화면이 채널계를 직접 불러 **CORS에 막힌다.**
  비워 두면 개발 서버 프록시를 쓴다(`web/.env.example` 참조).

### 5.3 해 볼 것

1. **SOR 자동 매수**: 시장 "SOR 자동", 가격 70000, 수량 100 → "100주 체결 · 평균 70,000원 · 전량".
   NXT 70,000원 잔량이 100 줄어든다.
2. **걸어 두기**: 시장 "KRX", 수량 10 → "체결 없음 · 호가창에 대기". KRX 70,000원 매수 잔량이 10 는다.
3. **거절**: 가격 70050 → "거절됨 · 호가 단위에 맞지 않는 가격". 수량 999999 → "증거금이 모자람".
4. **주문·체결 탭**: 위 주문들이 원장 응답(체결 수량, 상태, 거절 사유)으로 채워진다.
5. **원장 장애**: WSL에서 `ledgerd`를 Ctrl+C로 끈다 → 화면 위에 "원장 끊김". 다시 띄우면 표시가 사라지고
   호가창이 처음 상태로 돌아간다(원장은 시작할 때마다 같은 시드로 유동성을 만든다).

**이 데모가 하지 않는 것**: 계좌는 하나(`123456789012`), 종목은 하나(`005930`)다. 취소·정정은 원장이
"미지원"으로 답한다. 예전에 걸어 둔 주문이 **나중에** 체결되면 원장은 계산은 하지만 화면으로 밀어 보내지는 않는다.
SOR 판단 패널은 화면이 호가창 맨 위만 보고 그린 것이지 원장의 실제 배분 계산이 아니다.

---

## 6. 측정 결과 읽는 법

결과 파일은 `bench/results/`에 있고, 읽는 법은 4장 bench 절에 자세히 있다. 여기서는 결론만 짚는다.

### 6.1 집행 전략 — `quality-2026-09-16.md` (시드 30개)

| 시나리오 | 뜻 | 복수시장 전략 vs KRX_ONLY | 체결률 중앙값 |
|---|---|---|---|
| BALANCED | 두 시장 유동성이 비슷 | 22승 5패 3무, 중앙값 +2bp | 52.31% → 100.00% |
| KRX_THIN | KRX가 얇음 | 30승 0패, 중앙값 +6bp | 5.27% → 59.46% |
| NXT_THIN | NXT가 얇음 | 0승 25패 5무, 중앙값 -1bp | 52.31% → 57.63% |
| CROSSED | 한쪽 기준가가 밀림 | 차이 없음 | 21.67% → 21.67% |

읽는 법:
- **+bp는 KRX_ONLY보다 그만큼 싸게 샀다는 뜻**이다(매수만 냈으므로 낮은 단가가 이득).
- **승/패/무**는 시드 30개 각각에서 KRX_ONLY보다 나았는지를 센 것이다. 평균 대신 이것을 보는 이유는
  평균이 "몇 번 이겼는지"를 감추기 때문이다.
- **이득은 단가보다 체결률에서 먼저 온다.** NXT_THIN에서 단가가 1bp 나쁜 것은 체결률이 올라간 만큼
  더 비싼 호가까지 샀기 때문이다.
- BEST_PRICE·SPLIT·SWEEP의 평균 단가가 같은 것은 반올림이 아니라 **같은 호가를 다 먹기 때문**이다.
  차이는 채우는 순서에서 나고, 그것은 주문마다 잰 슬리피지(0~2bp)에 드러난다.

### 6.2 성능 — `pipeline-2026-09-16.md`

| 항목 | 저널 끔 | 저널 켬(레코드마다 fsync) |
|---|---:|---:|
| 전 구간 p50 | 351ns | 2,657,903ns (약 2.66ms) |
| TPS | 1,534,801 | 376 |
| 가장 비싼 단계 | SOR 계획 (50.7%) | 저널 기록 (99.8%) |

- 한 프로세스 안에서 7단계(전문 해석 → 원장 검증 → 저널 → SOR 계획 → 물리 등록 → 매칭 → 체결 반영)를 잰 값이라
  **프로세스 사이 통신 비용은 빠져 있다.**
- fsync를 켜면 처리량이 4천 분의 1로 떨어진다. 그래도 끄지 않은 이유는 "잃으면 안 되는 기록"이기 때문이다(문서 2).

---

## 7. 용어집

| 용어 | 뜻 |
|---|---|
| 호가 / 호가창 | 매수·매도 의사표시(가격과 수량) / 가격별로 정리한 미체결 호가 목록 |
| 최우선호가 | 매수 중 가장 높은 가격, 매도 중 가장 낮은 가격 |
| 잔량 | 특정 가격에 남아 있는 미체결 수량 |
| 호가 단위 | 가격대별 최소 가격 변동폭 (70,000원대는 100원) |
| maker / taker | 먼저 걸려 기다리던 주문 / 들어와서 그것을 먹는 주문. 체결가는 maker 가격 |
| 지정가·시장가·IOC·FOK·중간가 | 주문 유형 (1.1절) |
| 슬리피지 | 주문 순간의 최우선호가 대비 실제 체결가가 불리한 정도 |
| bp | basis point, 0.01% |
| 체결률 | 낸 수량 중 체결된 비율 |
| SOR | Smart Order Routing. 유리한 시장으로 주문을 배분 |
| 최선집행의무 | 자본시장법 제68조. 최선의 조건으로 집행할 의무 |
| 논리 주문 / 물리 주문 | 사용자가 낸 주문 하나 / 시장별로 쪼개져 실제로 나간 주문 |
| 다리(leg) | 논리 주문의 시장별 한 조각 |
| 통합 호가창 | KRX와 NXT 호가창을 가격순으로 합쳐 본 것 |
| 원장 | 증권사의 계좌·주문 장부. 예수금과 묶인 금액을 관리 |
| 예수금 / 묶인 금액(증거금) | 계좌의 현금 / 주문을 위해 쓰지 못하게 잡아 둔 금액 |
| 정산 | 체결에 따라 돈을 실제로 옮기는 것 |
| FEP | Front End Processor. 거래소와 전문을 주고받는 게이트웨이 |
| 채널계 | 화면(고객 접점)과 원장 사이의 서버 |
| 전문 | 시스템끼리 주고받는 고정 형식 메시지. 이 프로젝트는 24바이트 헤더 + 바디 |
| 종별 | 전문의 종류(주문 요청=1, 주문 응답=2, …, 호가 조회 응답=16) |
| 빅엔디언 | 큰 자리 바이트부터 보내는 순서 |
| 시퀀스 번호 / 갭 / 재전송 | 전문 순번 / 빠진 번호 구간 / 빠진 것을 다시 받기 |
| 하트비트 | 보낼 게 없어도 살아 있음을 알리는 전문 |
| 미응답(in-doubt) | 보냈는데 결과를 모르는 주문. 자동 재전송 금지 |
| 프로세스 / PID | 실행 중인 프로그램 / 그 번호 |
| fork | 프로세스를 복사해 자식을 만드는 시스템 호출 |
| 좀비 프로세스 | 끝났지만 부모가 `waitpid`로 거두지 않은 자식 |
| 공유메모리 | 여러 프로세스가 함께 보는 메모리 영역 |
| 소켓 / 포트 | 네트워크 통로 / 통로에 붙이는 번호 |
| fd | 파일 디스크립터. 열린 파일·소켓을 가리키는 작은 정수 |
| 블로킹 / 논블로킹 | 일이 끝날 때까지 기다림 / 기다리지 않고 바로 돌아옴 |
| epoll / 이벤트 루프 | 여러 fd의 준비 상태를 한꺼번에 기다리는 리눅스 기능 / 그것을 도는 반복 |
| 배압 | 받는 쪽이 느릴 때 보내는 쪽이 더 받지 않는 것 |
| 시그널 | 운영체제가 프로세스에 보내는 짧은 알림 (SIGINT, SIGTERM, SIGKILL) |
| fsync | 파일을 디스크에 확실히 쓰게 강제하는 호출 |
| 저널 / torn tail | 입력을 덧붙이기만 하는 기록 / 쓰다 만 마지막 레코드 |
| 스냅샷 | 어느 시점의 상태 전체를 저장한 파일 |
| 대사 | 결과를 독립적으로 다시 계산해 대조하는 것 |
| CRC | 데이터가 깨졌는지 확인하는 체크섬 |
| 결정성 / 시드 | 같은 입력이면 같은 출력 / 난수 생성기의 시작 값 |
| 논리 시각 | 시스템 시계가 아니라 이벤트가 들고 오는 순서용 시각 |
| 메모리 풀 | 미리 만들어 두고 꺼내 쓰는 객체 묶음 |
| X 매크로 | 목록 하나에서 열거형·이름표·길이표를 함께 만드는 C 전처리기 기법 |
| assert | 내부 약속 검사. 어기면 프로그램이 멈춘다 |
| ASan | 메모리 오류를 실행 중에 잡는 검사 도구 |
| 변이 검사 | 코드를 일부러 망가뜨려 테스트가 잡는지 보는 방법 |
| p50/p95/p99 | 줄 세웠을 때 50/95/99% 위치의 값 |
| TPS | 초당 처리 건수 |
| REST / JSON | HTTP 주소와 동사로 요청하는 방식 / 텍스트 데이터 형식 |
| WebSocket | 서버가 먼저 밀어 보낼 수 있는 연결 |
| CORS / 프록시 | 브라우저의 다른 출처 요청 제한 / 요청을 대신 전달하는 서버 |
| 의존성 주입 | 스프링이 객체를 만들어 생성자로 연결해 주는 것 |
| 세마포어 | "허가증 N장". 동시에 쓸 수 있는 자리 수를 지키는 도구 |
| 컴포넌트 / state / effect | React의 화면 조각 / 바뀌면 다시 그리는 값 / 그린 뒤 할 일 |
