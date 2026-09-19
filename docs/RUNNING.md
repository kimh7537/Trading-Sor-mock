# mini-sor 실행·테스트 안내서

> **문서 3.** 이 저장소를 **어떻게 빌드하고, 띄우고, 시험하는지**만 다룬다.
> 코드가 무엇을 하는지는 문서 1 `docs/GUIDE.md`, 왜 그렇게 만들었는지는 문서 2 `docs/ENGINEERING-NOTES.md`.
>
> 이 문서의 명령과 결과는 2026-09-17 최종 점검(T6-13)에서 실제로 돌린 것이다. 채널계 API·화면 부분(3.3, 4.4, 6.1)은 Phase 7(T7-03~05) 뒤에 다시 돌려 고쳤다.
> 단, **CLion·IntelliJ 화면 설정 절차(2장)는 각 IDE의 표준 기능을 적은 것이고 이 저장소에서 IDE 화면으로 따라 해
> 검증하지는 않았다.** 명령줄 절차(3장~6장)는 전부 검증했다.

---

## 목차

1. 준비물 — 무엇을 어디에 설치하나
2. IDE로 실행하기 — CLion(WSL 툴체인), IntelliJ IDEA, 화면(VS Code·WebStorm)
3. IDE 없이 명령어로 실행하기
4. 자동 테스트 — 무엇이 몇 개 있고, 어떻게 돌리고, 결과가 어떻게 나오나
5. 부하·성능 테스트(벤치마크) — 데이터, 개수, 방법, 결과 파일
6. 실제 스택 점검 — 브라우저·동시 요청·장애 시험
7. 변이 검사 — 테스트를 시험하는 방법
8. 문제가 생겼을 때

---

## 1. 준비물

이 프로젝트는 **언어마다 빌드하는 곳이 다르다.**

| 대상 | 폴더 | 어디서 | 필요한 것 (점검에 쓴 버전) | 왜 거기서 |
|---|---|---|---|---|
| C | `core/` `exchange/` `sor/` `ledger/` `fep/` `bench/` `sdk/` | **WSL Ubuntu** | Ubuntu 24.04, gcc 13.3, cmake 3.28, build-essential | Windows용 gcc에 ASan(메모리 검사) 라이브러리가 없다 |
| Java | `channel/` | **Windows** | JDK 17 (17.0.12). Maven은 `mvnw`가 알아서 받는다 | JDK가 Windows에 있다 |
| 화면 | `web/` | **Windows** | Node.js 22 (v22.14.0), npm 10 | node가 Windows에 있다 |

### 1.1 WSL 준비 (한 번만)

```powershell
# [Windows PowerShell, 관리자] WSL과 Ubuntu 설치
wsl --install -d Ubuntu
```

```bash
# [WSL Ubuntu] C 빌드 도구
sudo apt update
sudo apt install -y build-essential cmake gdb
gcc --version     # gcc 13 이상
cmake --version   # 3.20 이상이면 된다
```

WSL 안에서 Windows의 저장소는 `/mnt/c/...`로 보인다. 이 저장소는 OneDrive의 "바탕 화면" 아래에 있어
경로에 한글·공백이 있으므로 **따옴표로 감싸거나 `*`로 건너뛴다.**

```bash
cd /mnt/c/Users/<사용자>/OneDrive/*/Study/mock-sor
```

### 1.2 Windows 준비

```powershell
java -version    # 17.x 이어야 한다 (Spring Boot 4가 17 이상을 요구)
node --version   # v22.x
npm --version
```

### 1.3 포트

| 포트 | 누가 | 어디서 |
|---|---|---|
| 9100 | 원장 데몬 `ledgerd` | WSL |
| 8080 | 채널계 (Spring Boot) | Windows |
| 5173 | 화면 개발 서버 (Vite) | Windows |

WSL2는 WSL 안에서 연 포트를 Windows의 `localhost`로 넘겨준다. 그래서 Windows의 채널계가
`127.0.0.1:9100`으로 WSL의 원장에 붙는다(점검에서 확인).

---

## 2. IDE로 실행하기

### 2.1 CLion — C 코드 (Windows의 CLion + WSL 툴체인)

CLion을 Windows에 설치하고, **컴파일은 WSL의 gcc/cmake로** 하게 설정하는 방식을 권한다.
(CLion을 WSL 안에 설치해 WSLg로 띄우는 방법도 있지만, 아래 방식이 파일을 Windows 쪽에 둔 이 저장소와 잘 맞는다.)

**① 툴체인 추가** — `File > Settings > Build, Execution, Deployment > Toolchains`

1. `+` → **WSL** 선택
2. Environment: `Ubuntu`
3. CMake·Make·C Compiler·Debugger가 `/usr/bin/...`로 자동으로 잡히는지 확인 (안 잡히면 1.1절 설치를 먼저)
4. 이 툴체인을 목록 맨 위로 올려 기본값으로 둔다

**② 프로젝트 열기** — `File > Open` → 저장소 **루트의 `CMakeLists.txt`** → `Open as Project`

**③ CMake 프로필 세 개** — `Settings > Build, Execution, Deployment > CMake`

| 프로필 이름 | Build type | Toolchain | CMake options | Build directory |
|---|---|---|---|---|
| Debug | Debug | WSL | (비움) | `build-clion-debug` |
| Release | Release | WSL | (비움) | `build-clion-release` |
| ASan | Debug | WSL | `-DENABLE_ASAN=ON` | `build-clion-asan` |

> **빌드 폴더 이름을 `build`로 시작하게 바꾼다.** CLion 기본값 `cmake-build-debug`는 `.gitignore`(`build*/`)에
> 걸리지 않아 빌드 산출물이 git에 잡힌다.

**④ 빌드** — 오른쪽 위 프로필을 고르고 `Build > Build Project` (Ctrl+F9).
경고가 하나라도 있으면 실패한다(`-Werror`). 정상이면 경고 0으로 끝난다.

**⑤ 테스트 실행**

- 전체: `Run > Edit Configurations` → `+` → **CTest Application** → Target: All CTest → 실행.
  58개가 모두 초록이어야 한다
- 하나만: 실행 구성 목록에서 `test_msg` 같은 대상을 골라 실행한다
- 디버깅: 같은 대상을 Debug(벌레 아이콘)로 실행하면 `assert`가 실패한 줄에서 멈춘다

**⑥ 원장 데몬 실행** — 실행 구성 `ledgerd`

| 항목 | 값 |
|---|---|
| Target / Executable | `ledgerd` |
| Program arguments | (비우면 9100번 포트) — 다른 포트는 `9200` 처럼 숫자 |
| Working directory | 저장소 루트 |

실행 창에 `ledgerd 포트 9100 에서 대기`가 나오면 준비 끝이다. 정지 버튼으로 끈다.

**⑦ 벤치마크 실행** — **Release 프로필**로 빌드한 뒤, 실행 구성을 만들어 인자를 넣는다.
결과 파일 경로를 안 주면 기본 경로 `bench/results/...`(저장소 루트 기준)에 쓰는 프로그램이 있으므로
**Working directory를 저장소 루트로** 둔다(5장).

| 실행 구성 | Program arguments 예 |
|---|---|
| `compare_strategies` | `2026-09-17 /tmp/strategies.md` |
| `quality_report` | `2026-09-17 30 /tmp/quality.md` |
| `bench_match` | `/tmp/match.md` |
| `bench_pipeline` | `2026-09-17 /tmp/bench.jrn /tmp/pipeline.md "WSL2 ext4"` |

> Debug나 ASan 프로필로 벤치마크를 돌리면 숫자가 수 배 느리게 나온다. 성능 숫자는 Release로만 잰다.

### 2.2 IntelliJ IDEA — 채널계 (Java)

**① 열기** — `File > Open` → `channel/pom.xml` → `Open as Project`
(저장소 루트가 아니라 **`channel/`의 Maven 프로젝트**를 연다. 저장소 루트에는 `pom.xml`이 없다.)

**② JDK** — `File > Project Structure > Project > SDK`: **17**

**③ Maven 래퍼 사용** — `Settings > Build, Execution, Deployment > Build Tools > Maven`
→ `Maven home path`: **Use Maven wrapper** (명령줄의 `mvnw`와 같은 Maven을 쓰게 한다)

**④ 실행**

- 방법 A: `src/main/java/com/minisor/channel/ChannelApplication.java`를 열고 `main` 옆 ▶ 실행
- 방법 B: 오른쪽 Maven 창 → `channel > Plugins > spring-boot > spring-boot:run`

콘솔에 `Tomcat started on port 8080`이 나오면 뜬 것이다.
원장 주소는 `src/main/resources/application.properties`의 `minisor.ledger.host`(127.0.0.1)·`minisor.ledger.port`(9100)다.
바꾸려면 실행 구성의 VM options에 `-Dminisor.ledger.port=9200`을 넣는다.

> 원장이 떠 있지 않아도 채널계는 뜬다. 대신 주문은 503, 화면에는 빨간 띠 "원장에 연결되지 않음"이 뜬다.

**⑤ 테스트** — `src/test/java`에서 오른쪽 클릭 → `Run 'All Tests'`. 88개가 모두 초록이어야 한다.
`WireLayoutTest`는 C 헤더(`core/include/msg.h`, `types.h`)를 **저장소에서 직접 찾아 읽으므로** 저장소 전체가 받아져 있어야 한다.
테스트는 원장을 띄울 필요가 없다 — `FakeLedger`(시험용 가짜 원장)가 대신 답한다.

### 2.3 화면 — VS Code / WebStorm / IntelliJ Ultimate

1. `web/` 폴더를 연다
2. 터미널에서 `npm install` (처음 한 번)
3. `npm run dev` — 또는 WebStorm/IntelliJ에서 `package.json`의 `dev` 스크립트 옆 ▶
4. 브라우저로 **`http://localhost:5173`** (`127.0.0.1`은 안 된다 — 8장)

`web/.env`는 **비워 두거나 `.env.example`을 그대로 복사**한다. 채널계 주소를 직접 적으면 CORS로 주문이 막힌다.

### 2.4 IDE로 전체를 띄우는 순서

1. CLion: `ledgerd` 실행
2. IntelliJ: `ChannelApplication` 실행
3. 터미널: `web/`에서 `npm run dev`
4. 브라우저: `http://localhost:5173`

---

## 3. IDE 없이 명령어로 실행하기

### 3.1 C — 빌드 (WSL)

```bash
cd /mnt/c/Users/<사용자>/OneDrive/*/Study/mock-sor

# Debug — 평소 개발·테스트용
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8

# Release — 벤치마크용 (최적화, assert 꺼짐. 단 테스트 대상은 assert를 다시 켠다)
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel -j8

# ASan — 메모리 오류·정의되지 않은 동작 검사 (AddressSanitizer + UBSan)
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j8
```

만들어지는 실행 파일

| 파일 | 무엇 |
|---|---|
| `build/ledger/ledgerd` | 원장 데몬 |
| `build/<모듈>/tests/test_*` | 테스트 58개 |
| `build-rel/bench/bench_match` | 매칭 엔진 성능 |
| `build-rel/bench/bench_pipeline` | 전 구간 7단계 성능 |
| `build-rel/bench/compare_strategies` | 집행 전략 비교 (시드 1개) |
| `build-rel/bench/quality_report` | 집행 품질 리포트 (시드 30개) |

### 3.2 전체 띄우기 — 터미널 셋

```bash
# [터미널 1 · WSL] 원장
./build/ledger/ledgerd            # 포트 9100
# ./build/ledger/ledgerd 0        # 0이면 운영체제가 빈 포트를 골라 찍어 준다
# ./build/ledger/ledgerd 9100 --live 40   # 가상 참가자가 시장마다 초당 40건을 더 낸다
```

출력:
```
ledgerd 포트 9100 에서 대기 (SIGTERM/SIGINT로 종료)
  계좌 123456789012, 예수금 100000000원, 종목 005930, 기준가 70000원
  실시세 모드: 시장마다 초당 40건 (25ms마다 1건)      <- --live를 줬을 때만
```

`--live`를 **주지 않으면 예전과 똑같다** — 시드 유동성을 넣고 호가창이 그대로 멈춰 있다.
테스트와 `bench`의 결정성을 깨지 않으려고 그렇게 뒀다(T8-01). 틈은 `poll()` 타임아웃으로
만든다: 접속을 기다리다 만료되면 한 틱을 친다.

```powershell
# [터미널 2 · Windows] 채널계
cd channel
./mvnw.cmd spring-boot:run
```

```powershell
# [터미널 3 · Windows] 화면
cd web
npm install      # 처음 한 번
npm run dev      # --port 같은 인자를 붙이지 않는다 (8장)
```

브라우저: `http://localhost:5173`. 끌 때는 각 터미널에서 Ctrl+C.

원장은 **시작할 때마다 같은 시드로 같은 호가창을 만든다.** 원장을 껐다 켜면 계좌와 호가창이 처음 상태로 돌아간다
(KRX 70,000원 매수 잔량 9,231주, NXT 70,000원 매도 잔량 2,257주).

### 3.2.1 실시세 모드로 띄우기 (Phase 8)

호가창을 **바깥 시세**가 움직이게 한다. 주문·체결·잔고는 그대로 내 원장 안에서만 일어난다 —
**바깥으로 주문이 나가지 않는다.** 화면 오른쪽 위에 지금 어느 모드인지 늘 보이고, "시세 모드와
가격" 칸에서 바꾼다.

**가) 토스증권 실시세**

```bash
cp .env.example .env     # 저장소 루트. .gitignore에 있다
# TOSS_CLIENT_ID / TOSS_CLIENT_SECRET 를 채운다
```

```powershell
cd channel
$env:TOSS_ENABLED="true"; ./mvnw.cmd spring-boot:run
```

- 채널계가 `spring.config.import`로 루트의 `.env`를 읽는다. 키가 비어 있으면 **붙지 않는다**
  (키 없이 뜬 채 403을 되풀이하는 것보다 낫다)
- 연결이 **유휴 상태로 2분 48초**를 넘기면 서버가 끊는다. 30초마다 표준 Ping 프레임을 보내
  유지하고, 끊기면 토큰을 새로 받아 다시 붙는다 — 끊긴 세션의 토큰으로는 재연결이 거부된다
- **기준가는 이제 스스로 맞춘다**(T8-10). 호가창은 기준가 ±30%만 펼치므로 그 밖의 실호가는
  버려지는데, 채널계가 그것을 알아채면 원장에 `SYMBOL_SET`을 보내 **그 가격대로 호가창을
  다시 연다.** `--ref-price`는 처음 가격대를 정하고 싶을 때만 준다
- **종목을 바꿀 수 있다**(T8-10). 화면 왼쪽 위의 종목 이름을 눌러 이름·코드로 찾는다.
  바꾸면 그 종목의 원장이 새로 열리므로 **미체결 주문과 잔고가 초기화된다** — 한 종목짜리
  원장이라 앞 종목의 주문을 다른 가격대의 호가창에 남겨 둘 자리가 없다.
  검색은 `GET /api/stocks?q=`, 전환은 `POST /api/symbol {"symbol":"000660"}`
- 장중에만 움직인다. **허용 IP를 등록해야 한다** — 미등록이면 토큰 발급이
  `403 {"error":"access_denied","error_description":"IP address not allowed"}`이고,
  화면의 "시세 모드와 가격" 칸에 그 문구가 그대로 뜬다. 지금 나가는 주소는
  `curl https://api.ipify.org`로 확인한다
- **client 당 유효한 토큰이 하나다.** 같은 키로 프로세스를 둘 띄우면 서로의 토큰을 무효로 만든다
- 읽기 경로만 만들었다. 주문 API는 호출하지 않는다

**나) 녹화한 장 재생** — 장 마감·주말 데모용. 같은 파일은 늘 같은 호가창을 만든다.

```powershell
cd channel
$env:FEED_REPLAY_FILE="../docs/samples/feed-sample.jsonl"
$env:FEED_REPLAY_SPEED="3"      # 3배속. 1이면 실시간
./mvnw.cmd spring-boot:run
```

`docs/samples/feed-sample.jsonl`은 키 없이 재생 경로를 볼 수 있게 만든 **합성 샘플**이다.
실제 장은 `FEED_RECORD_FILE=../tape.jsonl`로 녹화해서 쓴다(한 줄에 스냅샷 하나, JSONL).
녹화는 뜰 때 자동으로 시작하고 `POST /api/feed/record?on=false`로 멈춘다 — **적을 자리는
설정이 정한다**(요청이 경로를 정하면 부를 수 있는 누구나 아무 자리에 파일을 만든다).

**둘 다 설정했을 때**: 토스를 먼저 고른다. 토스가 붙지 못하고 있으면(허용 IP 미등록 등)
실시세를 다시 요청할 때 **토스를 멈추고 재생으로 넘어간다** — 그러지 않으면 키는 있는데
IP를 등록 못 한 사람이 실시세를 영영 못 본다.

**확인**

```powershell
curl http://localhost:8080/api/feed
# {"mode":"sim","source":"sim","available":true,...,"error":null}
curl -X POST "http://localhost:8080/api/feed/mode?mode=live"
# 200 바뀌었다(재생) / 202 붙는 중(토스) / 409 실시세 설정이 없다
```

붙지 못하면 `error`에 이유가 담기고 화면에도 그대로 나온다. 조용히 시뮬로 남아 있으면
"켰는데 왜 안 바뀌지"를 알 길이 없다.

**캔들 차트**(화면의 1분·1일 봉)는 따로 조회한다.

```powershell
curl "http://localhost:8080/api/candles?interval=1d&count=5"
curl "http://localhost:8080/api/candles?interval=1m&count=120"
# 409 = 토스 설정이 없다(화면은 이때 내 호가창의 중간가 선으로 되돌아간다)
```

봉은 **바깥 시장의 체결**을 집계한 것이고 이 프로젝트 원장의 호가창과는 별개다 — 시뮬 모드의
가상 참가자가 만든 체결은 봉에 섞이지 않는다. 화면에도 그렇게 적혀 있다. 조회에는 별도
레이트리밋(`MARKET_DATA_CHART`)이 걸려 있어 채널계가 1분봉 20초·일봉 5분 동안 캐시한다.

바깥 시세를 받은 시장에는 `--live` 틱이 더 끼어들지 않는다 — 실호가 위에 가상 참가자의
주문을 계속 얹으면 그건 실시세도 시뮬도 아니다.

### 3.3 명령줄로 주문 넣어 보기 (화면 없이)

```powershell
# [Windows] 호가 조회 — market 0=KRX, 1=NXT
curl.exe http://localhost:8080/api/book?market=1

# SOR 자동(market 255) 매수 70,000원 100주
curl.exe -X POST http://localhost:8080/api/orders -H "Content-Type: application/json" `
  -d '{\"account\":\"123456789012\",\"symbol\":\"005930\",\"clOrdId\":1,\"side\":0,\"type\":0,\"market\":255,\"price\":70000,\"qty\":100}'
```

응답 예 (원장을 막 띄운 상태):
```json
{"outcome":"ACCEPTED","clOrdId":1,"orderId":200000000,"reason":0,"message":"접수","status":2,"filledQty":100,"avgPrice":70000}
```

| 필드 | 뜻 |
|---|---|
| `side` | 0 매수, 1 매도 |
| `type` | 0 지정가, 2 IOC(되는 만큼만), 3 FOK(전량 아니면 취소) |
| `market` | 0 KRX, 1 NXT, 255 SOR 자동 |
| `status` | 0 대기, 1 부분 체결, 2 전량 체결 |
| HTTP 상태 | 200 접수, 400 입력 오류(원장에 안 감), 422 원장이 거절(`reason` 참조), 503 원장에 못 붙음, 202 결과 모름 |

`reason`의 뜻은 `core/include/errors.h` (예: -5 호가 단위 위반, -9 없는 계좌, -14 증거금 부족).

주문 목록·상세·취소·잔고(T7-03):

```powershell
curl.exe http://localhost:8080/api/orders                  # 이 채널계가 낸 주문과 최신 상태, 최근 것부터
curl.exe http://localhost:8080/api/orders/200000003        # 하나. 모르는 번호면 404
curl.exe -X DELETE http://localhost:8080/api/orders/200000003
curl.exe http://localhost:8080/api/balance
```

응답 예 (69,000원 10주를 걸어 두고 4주가 나중에 체결된 뒤 취소):
```json
{"orderId":200000003,"reason":0,"status":1,"canceledQty":6,
 "order":{"orderId":200000003,"side":0,"type":0,"market":1,"price":69000,"qty":10,"filled":4,"canceled":6,"working":0,
          "notional":276000,"avgPrice":69000,"status":1,"done":true,
          "legs":[{"market":1,"sent":10,"filled":4,"canceled":6,"notional":276000,"avgPrice":69000}]}}
```
```json
{"account":"123456789012","cash":100000000,"reserved":0,"available":100000000}
```

| 취소의 HTTP 상태 | 뜻 |
|---|---|
| 200 | 취소됨. `canceledQty`만큼 풀렸다 |
| 409 | 이미 체결·취소로 끝난 주문 — 다시 누를 필요 없음 |
| 404 | 이 채널계가 모르는 주문번호 |
| 503 | 원장에 못 붙음. 취소는 다시 보내도 안전하다 |

`legs`는 논리 주문 하나가 시장별로 나간 몫(물리 주문)이다. `market` 255(SOR)로 낸 주문도 `legs`에는 실제 시장(0·1)이 적힌다.
원장 상태는 채널계가 1초마다 다시 읽어 **바뀐 것만** WebSocket으로 보낸다(`book`, `balance`, `order-update`, `fill`).
`application.properties`의 `minisor.poller.enabled=false`로 끌 수 있다.

### 3.4 Java·화면 — 테스트와 빌드

```powershell
cd channel
./mvnw.cmd test                         # 전체 88개
./mvnw.cmd clean test                   # 빌드 산출물을 지우고 처음부터
./mvnw.cmd test "-Dtest=OrderApiTest"   # 한 클래스만

cd ../web
npm run build                # 타입 검사 + 번들
npm run lint                 # oxlint
npm run check                # 예상 체결·호가 단위 계산 자체 점검 (Node로 바로 실행)
```

---

## 4. 자동 테스트

### 4.1 한눈에

| 종류 | 개수 | 어디 | 실행 | 걸리는 시간(점검 PC) |
|---|---:|---|---|---|
| C 단위·통합 테스트 | **58** | 각 모듈 `tests/test_*.c` | `ctest` | 빌드 포함 수 분, ASan이 가장 느리다 |
| Java 테스트 | **88** (16개 클래스) | `channel/src/test/java` | `mvnw test` | 약 1분 (Maven 시작 포함) |
| 화면 | 자체 점검 스크립트 1개 (13경우) | `web/scripts/estimate.check.ts` | `npm run build`, `npm run lint`, `npm run check` | 수 초 |

**커밋 전 규칙**: C는 **Debug·Release·ASan 세 빌드에서 58개가 모두 통과**해야 한다. Release는 `assert`가 꺼지는
빌드라서 거기서만 나는 경고·버그가 있고, ASan은 배열 밖 쓰기 같은 메모리 오류를 잡는다.

### 4.2 C 테스트 실행 방법

```bash
# 전체
ctest --test-dir build --output-on-failure

# 여러 개를 동시에 (빠르다)
ctest --test-dir build -j4 --output-on-failure

# 이름으로 고르기 (정규식)
ctest --test-dir build -R test_match --output-on-failure     # test_match_* 넷
ctest --test-dir build -R "ledger_core|order_validate" -V    # -V: 테스트가 찍는 것까지 보기

# 목록만 보기
ctest --test-dir build -N

# 테스트 실행 파일을 직접 돌리기 (가장 자세하다)
./build/ledger/tests/test_ledger_core

# 세 빌드 모두
for d in build build-rel build-asan; do ctest --test-dir $d -j4 --output-on-failure | tail -3; done
```

**결과가 나오는 모양**

성공:
```
      Start 48: test_ledger_core
48/58 Test #48: test_ledger_core .................   Passed    0.01 sec
...
100% tests passed, 0 tests failed out of 58
```

실패 (`assert`가 깨진 경우) — 파일·줄·조건이 그대로 나온다:
```
test_ledger_core: .../ledger/tests/test_ledger_core.c:468: test_book_query_full_depth:
Assertion `book_snapshot(ob, SIDE_BUY, MSG_BOOK_DEPTH, view) == MSG_BOOK_DEPTH' failed.
...
The following tests FAILED:
	 48 - test_ledger_core (Subprocess aborted)
```
(개발 중에 실제로 났던 실패다. 테스트의 가정이 틀렸던 것이라 테스트를 고쳤다.)

ASan이 잡은 경우 — 일반 빌드에서는 통과하는 버그를 여기서 멈춘다:
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address ...
```

테스트 프레임워크는 쓰지 않는다. 각 파일은 `main()`이 있는 독립 프로그램이고, `assert`가 깨지면 비정상 종료(abort)한다.
테스트 파일 몇 개는 진행 중인 단계 이름을 `[test_...]`처럼 찍는다.

### 4.3 C 테스트 58개 목록

**core — 13개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_types` | 타입의 폭·부호·경계값(컴파일 시점 검사), 에러 코드 문구 |
| `test_tick_size` | 가격대별 호가 단위, 경계값 |
| `test_order_pool` | 주문 풀에서 꺼내고 돌려주기, 고갈 |
| `test_order_pool_double_free` | 같은 주문을 두 번 돌려주면 멈추는지 (일부러 abort를 일으켜 확인) |
| `test_order_index` | 주문번호 해시 테이블 넣기·찾기·지우기 |
| `test_wire` | 24바이트 헤더, 빅엔디언 바이트 위치 |
| `test_msg` | 전문 16종의 길이·바이트 배치·왕복, 틀린 길이 거절, 256개 종별 코드 전수 |
| `test_journal` | 저널 쓰기·읽기, CRC, 바이트 단위로 자른 모든 위치에서 복구 |
| `test_snapshot` | 스냅샷 쓰기(임시 파일 → rename)와 복구 |
| `test_recon` | 대사 — 불일치를 첫 번째에서 멈추지 않고 전부 찾는지 |
| `test_fault_inject` | **실제 `SIGKILL`로 쓰는 도중 프로세스를 죽이고** 복구 확인 |
| `test_feed` | 시세 피드 메시지 형식 |
| `test_kv_config` | `key = value` 설정 파서 |

**exchange — 15개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_price_level` | 한 가격의 주문 줄(먼저 온 순서) |
| `test_order_book` | 호가창 넣기·빼기, 최우선호가, N단 조회 |
| `test_match_limit` | 지정가 체결: 가격·시간 우선, 부분 체결, 체결가는 maker 가격 |
| `test_match_market` | 시장가 |
| `test_match_ioc_fok` | IOC(되는 만큼)·FOK(전부 아니면 취소) |
| `test_match_cancel_modify` | 취소·정정, 정정 시 우선순위 |
| `test_event` | 이벤트 순서(maker 먼저, taker 다음) |
| `test_market_rules` | 시장 규칙 추상화 |
| `test_krx_rules` | KRX 거래 시간·주문 유형 |
| `test_nxt_rules` | NXT 프리·메인·애프터 구간, KRX와의 시간 비대칭 |
| `test_midpoint` | 중간가 주문 |
| `test_synthetic` | 시드 난수 유동성 생성 |
| `test_divergent` | 시나리오 4종(BALANCED·KRX_THIN·NXT_THIN·CROSSED)이 의도한 호가창 모양을 만드는지 |
| `test_determinism` | **시장당 5만 건**을 두 번 돌려 이벤트가 바이트까지 같은지, 시드를 바꾸면 달라지는지 |
| `test_feed_source` | 호가창 → 시세 피드 변환 |

**sor — 14개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_consolidated`, `test_consolidated_update` | 통합 호가창과 갱신 |
| `test_best_execution`, `test_be_weights` | 최선집행 점수와 가중치 |
| `test_strategy_krx_only`, `test_strategy_best_price`, `test_strategy_split`, `test_strategy_sweep` | 전략 4종의 배분 결과(손으로 계산한 기대값) |
| `test_order_map` | 논리 ↔ 물리 주문 매핑 |
| `test_split_state` | 여러 시장으로 나눈 주문의 상태 |
| `test_split_cancel` | 나눈 주문 취소: 양쪽 성공 / 한쪽 이미 체결 / 한쪽 시장 마감 |
| `test_routing_log` | 배분 판단 기록과 그 기록만으로 재계산 |
| `test_execution_quality` | 슬리피지·체결률·체결 금액 기준 bp |
| `test_recon_live` | 실제 계획·체결·취소 흐름에서 뽑은 숫자로 대사, 일부러 넣은 불일치를 잡는지 |

**ledger — 6개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_listener` | 진짜 소켓: 포트 0, 전문 단위 수신, 잘린 전문, 모르는 종별·틀린 길이면 접속 끊기, SIGTERM으로 멈추기 |
| `test_worker_pool` | `fork()` 워커가 일을 실제로 나눠 받는지, 죽은 워커 다시 띄우기, 좀비를 남기지 않는 정지 |
| `test_shm` | 공유메모리 영역 |
| `test_account` | 예수금·묶인 금액 불변조건, 계좌 단위 락, 프로세스 사이 락, **락을 쥔 채 죽은 프로세스 회수** |
| `test_order_validate` | 계좌·호가 단위·증거금 검증 |
| `test_ledger_core` | 주문 전문 → 검증 → SOR → 체결 → **정산 금액**, 나중에 체결되는 maker, 가격 개선, 조회, 호가 조회, 결정성 |

**fep — 7개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_evloop` | epoll 루프, 반닫힘 |
| `test_framer` | 바이트를 1개씩 흘려 넣어도 전문이 정확히 잘리는지 |
| `test_sendq` | 송신 큐, 부분 쓰기, 배압 |
| `test_session` | 로그인·하트비트·재접속 |
| `test_seqtrack` | 시퀀스 갭·재전송·갭 채우기 |
| `test_ordmap` | 주문번호 매핑, 미응답 주문 판정 |
| `test_integration` | **두 프로세스 + 진짜 매칭 엔진**으로 주문 → 체결 → 끊김 → 정리 |

**sdk·bench — 3개**

| 테스트 | 확인하는 것 |
|---|---|
| `test_order_sdk` | 전략 엔진용 주문 상태 기계 |
| `test_compare` | 전략 비교 하네스(작은 설정으로 표 생성, bp 부호) |
| `test_quality` | 품질 리포트의 승/패/무 판정과 결론 문장 |

> 벤치마크 실행 파일 4개(`bench_match` 등)는 오래 걸려 ctest에 넣지 않았다. 5장에서 따로 돌린다.

### 4.4 Java 테스트 74개

| 클래스 | 개수 | 확인하는 것 |
|---|---:|---|
| `ChannelApplicationTests` | 1 | 스프링 컨텍스트가 뜨는지 |
| `ChannelStartupTests` | 4 | 헬스 체크 응답, health 말고는 열려 있지 않음, 원장 주소를 설정에서 읽음, **원장이 없으면 구독자에게 `ledger-down`** |
| `wire.WireCodecTest` | 12 | 빅엔디언, 고정 길이 문자열, 배열 필드(i32·i64), 헤더 검사, **호가 스냅샷 배치와 단수 자르기** |
| `wire.WireLayoutTest` | 4 | **C 헤더를 직접 읽어** 전문 길이·열거값을 Java와 대조 |
| `ledger.LedgerConnectionPoolTest` | 9 | 접속 재사용, 타임아웃 시 버리기, 원장 죽음, 풀 고갈, 동시 요청이 응답을 바꿔 받지 않음, **동시 빌리기 200판**, 대기자 깨우기 |
| `api.OrderApiTest` | 14 | 상태 코드 200/400/422/202, SOR 시장값 255, 호가 API, 주문·체결 방송, 원장 끊김·회복 방송, **주문 목록·상세, 취소 200/409/404, 잔고, SOR 체결이 실제 시장으로 방송, 조회 실패도 끊김 방송** |
| `api.LedgerPollerTest` | 1 | 원장을 다시 읽어 **바뀐 것만** 방송: 첫 바퀴 호가·잔고, 변화 없으면 없음, 나중 체결의 가격(금액 차이 / 수량 차이), 끝난 주문은 다시 묻지 않음 |
| `api.OrderRegistryTest` | 2 | 주문 목록 상한 500건, 최근 순서 |
| `stream.StreamTest` | 4 | 구독·방송, 원장 끊김 전파, 나간 구독자 정리, 많은 이벤트 |
| `feed.SnapshotTest` | 4 | 바깥 시세 읽기: **문자열 decimal → 정수**, 시각 `null` 대체, 못 쓰는 단 버리기, 10단 넘으면 자르기 |
| `feed.LiveFeedTest` | 4 | 스냅샷이 `MSG_BOOK_FEED`로 원장까지, 심은 호가창 즉시 방송, **설정 없으면 실시세로 못 바꿈(409)**, 모드 전환 |
| `feed.TossTokenSourceTest` | 6 | client credentials 폼, 토큰 캐시(재발급하면 이전 토큰이 죽으므로), **429의 `Retry-After`**, 403 본문 전달, **gzip 본문 해독**, 만료 전 재발급 |
| `feed.FeedReplayTest` | 4 | 녹화 파일 왕복, 반쪽 줄 건너뛰기, **같은 파일 같은 결과**, 배속은 간격만 바꿈 |
| `feed.TossCandlesTest` | 5 | 캔들 조회: **최신순 → 오래된 순으로 뒤집기**, 문자열 decimal → 정수, 못 읽는 봉 버리기, 캐시, 실패 본문 전달 |

결과는 콘솔과 `channel/target/surefire-reports/*.txt`에 남는다:
```
Tests run: 9, Failures: 0, Errors: 0, Skipped: 0, Time elapsed: 7.7 s -- in com.minisor.channel.api.OrderApiTest
```
콘솔에 `Validation failed ... rejected value [2]` 같은 **WARN 로그가 보이는 것은 정상**이다 — 잘못된 입력이 400으로
막히는지 일부러 시험하는 테스트가 남기는 로그다.

---

## 5. 부하·성능 테스트 (벤치마크)

### 5.1 데이터는 어디서 오나

**외부 시세 데이터 파일은 쓰지 않는다.** 저장소에 CSV 같은 데이터 파일이 없다.
모든 주문은 **시드(시작 숫자)를 받은 난수 생성기로 그 자리에서 만든다.**

| 무엇 | 코드 | 설명 |
|---|---|---|
| 가짜 참가자 주문 | `exchange/src/liquidity/synthetic.c` | 기준가 주변 가격·수량·방향을 시드 난수로 생성 |
| 두 시장 시나리오 | `exchange/src/liquidity/divergent.c` | 두 시장에 서로 다른 유동성을 넣는다 |
| 전략 비교 설정 | `bench/compare.c`의 `COMPARE_DEFAULT` | 시드·기준가·유동성 수·측정 주문 수 |
| 원장 데몬의 호가창 | `ledger/src/ledger_core.c`의 `LEDGER_CORE_DEFAULT` | 시장당 1,000건, 시드 20260917, 기준가 70,000원 |

시나리오 4종 (`exchange/include/divergent.h`)

| 이름 | 뜻 |
|---|---|
| `BALANCED` | 두 시장이 비슷하다 — 라우팅이 소용없어야 하는 대조군 |
| `KRX_THIN` | KRX 유동성이 부족하다 |
| `NXT_THIN` | NXT 유동성이 부족하다 |
| `CROSSED` | 한쪽 최우선호가가 다른 쪽보다 유리하다 |

**같은 시드 → 같은 주문 → 같은 결과.** 그래서 결과 파일은 "데이터"이면서 "재현 가능한 실험 기록"이다.
시간(지연·TPS)만은 기계 상태에 따라 달라진다.

### 5.2 결과 파일 — `bench/results/` 4개

| 파일 | 만든 프로그램 | 내용 | 크기 |
|---|---|---|---:|
| `2026-09-15.md` | `bench_match` | 매칭 엔진 단독 지연·TPS (시나리오 3종) | 1.8 KB |
| `strategies-2026-09-16.md` | `compare_strategies` | 전략 4종 × 시나리오 4종 = 16칸 비교 (시드 1개) | 3.5 KB |
| `quality-2026-09-16.md` | `quality_report` | 같은 실험을 시드 30개로, 승/패/무와 판정 | 4.8 KB |
| `pipeline-2026-09-16.md` | `bench_pipeline` | 전 구간 7단계 지연, 저널 끔/켬 비교 | 5.8 KB |

전부 사람이 읽는 **Markdown 표**다. 각 파일에 "측정 조건 → 결과 표 → 읽는 법 → 이 실험이 말하지 못하는 것" 순서로 들어 있다.

### 5.3 네 가지 벤치마크

모두 **Release 빌드**(`build-rel`)로, **저장소 루트에서** 돌린다.

#### ① `bench_match` — 매칭 엔진 단독

| 항목 | 값 |
|---|---|
| 무엇을 재나 | `match_limit()` 한 번의 지연과 초당 처리량 |
| 데이터 | 시나리오 3종(BALANCED, KRX_THIN, CROSSED), 시드 20260915 |
| 건수 | 시나리오마다 예열 시장당 20,000건(측정 제외) + **측정 시장당 200,000건 = 두 시장 400,000건** |
| 명령 | `./build-rel/bench/bench_match` (화면에 출력) / `./build-rel/bench/bench_match <결과파일>` (파일로) |
| 걸리는 시간 | 수 초 |

결과 (커밋된 `2026-09-15.md`):

| 시나리오 | 처리 건수 | TPS | p50 | p95 | p99 | 체결 수량 |
|---|---:|---:|---:|---:|---:|---:|
| BALANCED | 400,000 | 3,789,251 | 202ns | 305ns | 423ns | 20,074,677주 |
| KRX_THIN | 400,000 | 4,004,004 | 186ns | 314ns | 501ns | 10,429,858주 |
| CROSSED | 400,000 | 3,822,613 | 216ns | 308ns | 449ns | 39,606,071주 |

읽는 법: p50과 p99의 차이가 "체결이 붙은 주문"의 비용이다. 얇은 시장(KRX_THIN)이 빠르게 보이는 것은 일을 덜 했기 때문이다.

#### ② `bench_pipeline` — 주문 한 건이 지나는 7단계 전 구간

| 항목 | 값 |
|---|---|
| 무엇을 재나 | 전문 해석 → 원장 검증 → 저널 기록 → SOR 계획 → 물리 등록 → 매칭 → 체결 반영, **단계별** 지연 |
| 데이터 | 유동성 시장당 40,000건, BALANCED, 시드 20260916. 주문은 매수 지정가 기준가 10,000원 +20원, 10~50주, 전략 BEST_PRICE |
| 건수 | 예열 2,000 + **측정 20,000건**을 **두 판**(저널 끔 / 레코드마다 fsync하는 저널 켬) |
| 명령 | `./build-rel/bench/bench_pipeline <날짜> <저널경로> <결과파일> "<저장 매체 설명>"` |
| 예 | `./build-rel/bench/bench_pipeline 2026-09-17 /tmp/bench.jrn /tmp/pipeline.md "WSL2 ext4"` |
| 걸리는 시간 | **약 1분** (저널 켠 판이 fsync를 2만 번 한다) |

> 결과 파일을 안 주면 화면에 출력한다. **저널 경로는 WSL 안의 경로(`/tmp/...`)로 준다.**
> `/mnt/c/...`(Windows 디스크)에 두면 fsync 비용이 전혀 다른 값이 된다.

결과 (커밋된 `pipeline-2026-09-16.md`):

| 단계 | 저널 끔 p50 | 저널 켬 p50 |
|---|---:|---:|
| 1. 전문 해석 | 18ns | 205ns |
| 2. 원장 검증 | 37ns | 893ns |
| 3. 저널 기록 | — | 2,651,599ns |
| 4. SOR 계획 | 178ns | 2,006ns |
| 5. 물리 등록 | 30ns | 656ns |
| 6. 매칭 | 50ns | 1,043ns |
| **전 구간 p50** | **351ns** | **2,657,903ns** |
| **TPS** | **1,534,801** | **376** |

- 한 프로세스 안에서 차례로 부른 값이라 **프로세스 사이 통신 비용은 빠져 있다**
- 단계별 p50을 더해도 전 구간 p50이 안 된다 — 분위수는 더해지지 않는다
- 체결 수량 599,847주는 저널을 켜도 끄도 같다(같은 주문이므로)
- 파일에는 단계별 p95·p99·최대와 "그래서 무엇을 할 것인가"(fsync를 유지하는 이유)도 있다

#### ③ `compare_strategies` — 집행 전략 비교 (이 프로젝트의 핵심 결과)

| 항목 | 값 |
|---|---|
| 무엇을 재나 | 전략 4종(KRX_ONLY·BEST_PRICE·SPLIT·SWEEP)의 평균 체결 단가, 슬리피지(bp), 체결률, KRX_ONLY 대비 단가 차이(bp) |
| 데이터 | 시나리오 4종 × 전략 4종 = 16회. **전략마다 호가창을 새로 만들어 같은 시드로 다시 채운다** |
| 건수 | 한 회에 유동성 시장당 400건 + **측정 주문 200건**(매수, 50~500주, 기준가 10,000원 + 5틱) → 16회 합계 측정 주문 3,200건 |
| 시드 | 20260916 (세 번째 인자로 바꿀 수 있다) |
| 명령 | `./build-rel/bench/compare_strategies <날짜> [결과파일] [시드]` |
| 기본 결과 파일 | 결과파일을 안 주면 **`bench/results/strategies-<날짜>.md`에 쓴다** (같은 날짜면 커밋된 파일을 덮어쓴다) |
| 걸리는 시간 | 수 초 |

결과 (커밋된 파일 중 BALANCED):
```
| 시나리오 | 전략 | 평균 체결 단가 | 슬리피지(bp) | 체결률 | KRX_ONLY 대비(bp) |
| BALANCED | KRX_ONLY   | 10019 | +11 |  53.46% | +0 |
| BALANCED | BEST_PRICE | 10014 | +0  | 100.00% | +5 |
| BALANCED | SPLIT      | 10014 | +1  | 100.00% | +5 |
| BALANCED | SWEEP      | 10014 | +0  | 100.00% | +5 |
```
읽는 법: "KRX_ONLY 대비" **양수면 그만큼 싸게 샀다**(매수). 체결률과 함께 봐야 한다 — 단가가 좋아도 체결률이 낮으면
남은 수량을 나중에 더 비싸게 사게 된다.

#### ④ `quality_report` — 시드 30개 집행 품질 리포트

| 항목 | 값 |
|---|---|
| 무엇을 재나 | ③을 시드마다 돌려 칸마다 분포(최소·중앙·최대)와 **KRX_ONLY를 이긴 시드 수(승/패/무)** |
| 데이터 | 시드 20260916 ~ 20260945 (30개) |
| 건수 | 시드 30 × 16회 × 측정 주문 200건 = **측정 주문 96,000건** |
| 명령 | `./build-rel/bench/quality_report <날짜> [시드 개수=30] [결과파일] [시작 시드]` |
| 기본 결과 파일 | **`bench/results/quality-<날짜>.md`** |
| 걸리는 시간 | 수 초 ~ 수십 초 |

결과 (커밋된 `quality-2026-09-16.md`의 결론 — 이 문장도 프로그램이 표에서 자동으로 뽑는다):
```
- BALANCED — 엇갈림: BEST_PRICE(22승 5패 3무, 중앙값 +2bp), SPLIT(...), SWEEP(...)
- KRX_THIN — 진 적 없음: BEST_PRICE(30승 0패 0무, 중앙값 +6bp), ...
- NXT_THIN — 이긴 적 없음: BEST_PRICE(0승 25패 5무, 중앙값 -1bp), ...
- CROSSED — 차이 없음: BEST_PRICE, SPLIT, SWEEP
```

| 판정 | 뜻 |
|---|---|
| 진 적 없음 | 30개 시드 중 KRX_ONLY보다 나쁜 적이 없다 |
| 이긴 적 없음 | 나은 적이 없다 |
| 엇갈림 | 이기기도 지기도 했다 |
| 차이 없음 | 전부 비겼다 |
| 기준선 | KRX_ONLY 자신 |

### 5.4 결과가 재현되는지 확인하는 방법

전략 비교와 품질 리포트는 **시간 값이 없으므로 바이트까지 같아야 한다.**

```bash
./build-rel/bench/compare_strategies 2026-09-16 /tmp/strat.md
cmp /tmp/strat.md bench/results/strategies-2026-09-16.md && echo IDENTICAL

./build-rel/bench/quality_report 2026-09-16 30 /tmp/qual.md
cmp /tmp/qual.md bench/results/quality-2026-09-16.md && echo IDENTICAL
```

2026-09-17 최종 점검에서 둘 다 **IDENTICAL**이었다.

`bench_match`·`bench_pipeline`은 지연·TPS가 기계 부하에 따라 달라진다. 점검 때는 다른 작업이 함께 돌고 있어
`bench_match` BALANCED TPS가 약 272만, `bench_pipeline`이 1,114,814 / 329 TPS로 커밋 값보다 낮게 나왔다.
**체결 수량(599,847주)처럼 입력으로 정해지는 값은 같았다.** 성능 숫자를 비교하려면 같은 기계에서 다른 프로그램을 끄고 여러 번 잰다.

---

## 6. 실제 스택 점검 — 브라우저·동시 요청·장애

자동 테스트가 못 보는 것(브라우저 CORS, 프로세스 사이 연결, 화면 표시)을 사람이 확인하는 절차다. 3.2절로 셋을 띄운 뒤 진행한다.
아래 기대값은 **원장과 채널계를 막 띄운 상태**에서 순서대로 했을 때의 값이다(T7-05 개편 뒤 2026-09-17에 실제로 확인).

### 6.1 화면 시나리오

거래 화면 하나에 왼쪽 호가, 가운데 시장 비교와 미체결·주문 내역·체결 탭, 오른쪽 주문창이 있다. 알림은 왼쪽 아래에 뜬다.

| # | 화면에서 할 일 | 기대 결과 |
|---|---|---|
| 1 | 처음 열기 | 오른쪽 위 "실시간". 예수금·주문 가능 100,000,000원, 묶인 금액 0원. KRX 70,000원 매수 9,231주·NXT 70,000원 매도 2,257주에 "최우선" |
| 2 | NXT 70,000원 매도호가를 누르고 수량 "100" | 주문창이 매수·70,000원·100주. 주문 전 확인 "예상 체결 100주 · 평균 70,000원 · KRX 0 · NXT 100", 필요 7,000,000원, "✓ 가능". 시장 비교는 NXT 단독·두 시장이 "유리" |
| 3 | Ctrl+Enter | 알림 "체결 · NXT 매수 100주 · 70,000원". NXT 70,000원 잔량 2,257 → **2,157**(▼ 깜빡임). 예수금 **93,000,000원** |
| 4 | 매수 · KRX · 지정가 · 70000 · 10 → 주문 | 확인란 "나머지 10주 호가창 대기". 알림 "주문 접수 … 호가창에 대기". KRX 70,000원 매수 잔량 **9,241**에 "내 10", 묶인 금액 **700,000원**, 미체결 탭 1 |
| 5 | 매도(또는 S 키) · SOR 자동 · 69900 · 50 → 주문 | 확인란 "예상 체결 50주 · 평균 70,000원 · KRX 50", 예상 수령 3,500,000원. 알림 "체결 · KRX 매도 50주 · 70,000원". KRX 70,000원 매수 잔량 **9,191**(4번 주문은 뒤에 서 있어 그대로 "내 10"). 예수금 **96,500,000원** |
| 6 | 매수 · KRX · 70050 | 주문 버튼이 꺼지고 "호가 단위(100원)에 맞지 않는 가격 — ↑↓로 맞추세요". 가격칸에서 ↑를 누르면 70,100 |
| 7 | 매수 · NXT · 70000 · 99999 | 주문 버튼이 꺼지고 "주문 가능 금액이 6,904,130,000원 모자람", 확인란 "✕ … 부족" |
| 8 | 미체결 탭에서 4번 주문의 ▸ 펼치기 → 취소 | 펼치면 "KRX 보냄 10 · 체결 0 · 취소 0 · 대기 10". 취소 뒤 알림 "취소 완료 … 10주 취소", 묶인 금액 **0원**, KRX 70,000원 매수 잔량 **9,181**, 미체결 0 |
| 9 | 매수 · NXT · 69000 · 10 → 주문 | NXT 69,000원에 새 줄 10주와 "내 10", 묶인 금액 690,000원 |
| 10 | 다른 터미널에서 NXT 69,000원 매도 72,280주를 보낸다(아래 명령) | 1~2초 안에 알림 "체결 · NXT 매도 72,280주 · 69,728원"과 "체결 · NXT 매수 4주 · 69,000원". 미체결 줄이 "부분 체결 · 체결 4 · 대기 6"으로 깜빡이고, 호가 "내 6". **나중 체결이 밀려온 것**(채널계 주기 작업) |
| 11 | 체결 탭 / 주문 내역 탭 | 체결마다 한 줄(시각·시장·구분·가격·수량), 매수·매도 평균을 따로. 주문 내역에 모든 주문 |
| 12 | 창을 1,000px 안팎으로 좁히기 / 500px 안팎으로 좁히기 | 2열(호가·주문창 / 시장 비교 / 주문 내역) → 1열, 500px대에서는 두 시장이 나란히 5단만. 가로 스크롤 없음 |
| 13 | "관제" 탭 | 채널계 구독 "실시간 수신 중", 원장 "정상", 받은 사건 수가 늘어남 |

10번의 명령 — NXT 매수호가 69,100원까지 72,276주를 모두 채운 뒤 남은 4주가 9번 주문에 붙는다:
```powershell
curl.exe -X POST http://localhost:5173/api/orders -H "Content-Type: application/json" `
  -d '{\"account\":\"123456789012\",\"symbol\":\"005930\",\"clOrdId\":920001,\"side\":1,\"type\":0,\"market\":1,\"price\":69000,\"qty\":72280}'
```

키보드: 입력칸 밖에서 **B** 매수·**S** 매도(한글 입력 상태에서도 된다), 가격칸 **↑↓** 한 호가, **Ctrl+Enter** 주문.
입력칸에서 Enter만 치면 주문이 나가지 않는다.

### 6.2 입력 검증 (명령줄)

```powershell
# 계좌번호가 12자리가 아님 → 400 (원장에 가지 않는다)
curl.exe -i -X POST http://localhost:5173/api/orders -H "Content-Type: application/json" `
  -d '{\"account\":\"12\",\"symbol\":\"005930\",\"clOrdId\":900001,\"side\":0,\"type\":0,\"market\":0,\"price\":70000,\"qty\":1}'
```

| 입력 | 기대 (T6-13 실제 결과) |
|---|---|
| 계좌 `"12"` | 400 |
| `side: 2` | 400 |
| `market: 2` | 400 (0·1·255만 허용) |
| `qty: 0` | 400 |
| 계좌 `"999999999999"` (없는 계좌) | 422, `reason: -9` |
| `GET /api/book?market=2` | 400 |

### 6.3 동시 요청 — 접속 1개 풀 확인 (PowerShell)

원장은 접속을 하나씩 처리하고 채널계 풀은 접속 1개다. **주문 30건과 호가 조회 30건을 동시에** 보내
모두 성공하는지 본다. 다음 스크립트를 PowerShell에 그대로 붙여 넣는다.

```powershell
Add-Type -AssemblyName System.Net.Http
$c = New-Object System.Net.Http.HttpClient
$c.Timeout = [TimeSpan]::FromSeconds(20)
$tasks = New-Object System.Collections.Generic.List[System.Threading.Tasks.Task[System.Net.Http.HttpResponseMessage]]
for ($i = 0; $i -lt 30; $i++) {
  $side = $i % 2; $price = if ($side -eq 0) { 69500 } else { 70500 }
  $json = '{"account":"123456789012","symbol":"005930","clOrdId":' + (910000 + $i) + ',"side":' + $side +
          ',"type":0,"market":' + @(0,1,255)[$i % 3] + ',"price":' + $price + ',"qty":1}'
  $body = New-Object System.Net.Http.StringContent($json, [Text.Encoding]::UTF8, "application/json")
  $tasks.Add($c.PostAsync("http://localhost:5173/api/orders", $body))
  $tasks.Add($c.GetAsync("http://localhost:5173/api/book?market=$($i % 2)"))
}
[System.Threading.Tasks.Task]::WaitAll($tasks.ToArray()) | Out-Null
$tasks | ForEach-Object { [int]$_.Result.StatusCode } | Group-Object | ForEach-Object { "status $($_.Name): $($_.Count)" }
```

기대 결과 (T6-13 실제 결과, 주문 30건 모두 `ACCEPTED`):
```
status 200: 60
```
202(결과 모름)나 503(접속 모자람)이 섞이면 접속 풀 또는 원장 연결에 문제가 있는 것이다.

### 6.4 원장 장애와 회복

| # | 할 일 | 기대 결과 |
|---|---|---|
| 1 | WSL 원장 터미널에서 Ctrl+C (또는 `pkill -x ledgerd`) | 1~2초 안에 머리글 아래 빨간 띠 "원장에 연결되지 않음 — …", 호가 패널 위에 "원장 호가를 읽지 못했다" |
| 2 | 그 상태로 매수 주문 | 알림 "주문 거절" (HTTP 503) — 주문이 나가지 않은 것이 분명한 경우 |
| 3 | `./build/ledger/ledgerd`로 다시 띄움 | 몇 초 안에 두 표시가 사라지고 호가창이 처음 상태(9,231 / 2,257)로 |
| 4 | 매수 · SOR 자동 · 70000 · 100 | 곧바로 알림 "체결 · NXT 매수 100주 · 70,000원" |

---

## 7. 변이 검사 — 테스트를 시험하는 방법

"테스트가 통과한다"가 "테스트가 버그를 잡는다"를 보장하지 않는다. 그래서 **코드를 일부러 망가뜨려 테스트가 실패하는지** 본다.
태스크마다 한 이 작업의 결과(몇 개 중 몇 개를 잡았는지, 살아남은 변이를 어떻게 처리했는지)는 `docs/PROGRESS.md`에
태스크별로 적혀 있다. **변이를 거는 스크립트는 저장소에 넣지 않았다**(작업할 때마다 임시로 만들었다). 방법은 이렇다.

```bash
# [WSL] 예: 원장 코어에서 "매도 대금 입금"을 지워 본다
SRC=ledger/src/ledger_core.c
cp $SRC /tmp/orig.c

# 1) 원본이 먼저 통과하는지 확인 — 이걸 안 하면 고장 난 검사기가 전부 "잡힘"으로 보인다
cmake --build build --target test_ledger_core && (cd build/ledger/tests && ./test_ledger_core) && echo "원본 통과"

# 2) 한 곳만 바꾼다
perl -0pi -e 's/rc = acct_deposit\(&c->store, acct, amount\);/rc = ERR_OK;/' $SRC

# 3) 정말 바뀌었는지 확인 — 치환이 안 먹었는데 "살아남음"으로 읽으면 안 된다
cmp -s /tmp/orig.c $SRC && echo "변이가 적용되지 않았다"

# 4) 빌드·실행. 빌드 실패는 "잡힘"이 아니다
if cmake --build build --target test_ledger_core; then
  (cd build/ledger/tests && timeout -s KILL 60 ./test_ledger_core) && echo "살아남음 — 테스트 구멍" || echo "잡힘"
else
  echo "빌드 실패 — 값만 바꾸는 변이로 다시"
fi

# 5) 반드시 되돌린다
cp /tmp/orig.c $SRC && cmake --build build --target test_ledger_core
```

Java는 같은 방법을 `channel/src/main/java`의 파일에 쓰고 `./mvnw.cmd test "-Dtest=대상테스트"`로 돌린다.
배열 밖 쓰기처럼 **일반 테스트로는 안 보이는 변이는 ASan 빌드로** 돌린다.

---

## 8. 문제가 생겼을 때

| 증상 | 원인 | 해결 |
|---|---|---|
| 화면 모든 주소가 404 | PowerShell에서 `npm run dev -- --port 5173`을 쓰면 `--`가 사라져 Vite가 `5173`을 폴더로 안다 | 인자 없이 `npm run dev` |
| `http://127.0.0.1:5173`이 안 열림 | Vite가 IPv6 `localhost`(`::1`)에만 붙는다 | `http://localhost:5173` |
| 주문을 누르면 콘솔에 "blocked by CORS policy" | `web/.env`에 `VITE_API_BASE=http://localhost:8080` | `.env`를 비우거나 `.env.example` 복사 후 개발 서버 재시작 |
| 화면 위 빨간 띠 "원장에 연결되지 않음", 주문 503 | 원장이 안 떠 있거나 포트가 다름 | WSL에서 `ledgerd` 실행, `application.properties`의 포트 확인 |
| 원장은 떠 있는데 채널계가 못 붙음 | WSL 네트워크 설정에서 localhost 전달이 꺼짐 | WSL에서 `hostname -I`로 주소를 보고 `-Dminisor.ledger.host=<그 주소>`로 채널계 실행 |
| `ledgerd`: "포트 9100 를 열 수 없다" | 이미 떠 있는 `ledgerd`가 있다 | `pkill -x ledgerd` 후 다시 |
| 채널계: 8080 포트 사용 중 | 전에 띄운 채널계가 남음 | PowerShell: `Get-NetTCPConnection -LocalPort 8080` → 해당 프로세스 종료 |
| C 빌드가 경고로 실패 | `-Werror` — 경고는 곧 실패 | 경고 내용을 고친다. Release에서만 나는 경고가 있으니 세 빌드 모두 확인 |
| 소스를 고쳤는데 다시 빌드가 안 됨 | `/mnt/c`의 파일 시각이 WSL 시계와 어긋남 | 해당 목적 파일을 지우거나 빌드 폴더를 새로 만든다 |
| 빌드 로그에 `gmake: warning: Clock skew detected` / `modification time … in the future` | **컴파일러 경고가 아니다.** WSL2 가상 머신 시계가 Windows와 몇 초 어긋나면(점검 때 약 5초) make가 내는 알림이다. 컴파일러 경고였다면 `-Werror`로 빌드가 실패했을 것이다 | 빌드가 성공하고 테스트가 통과하면 결과는 믿어도 된다. 소스를 막 고친 직후라면 덜 빌드됐을 수 있으니 빌드 폴더를 새로 만든다. 시계를 맞추려면 PowerShell에서 `wsl --shutdown` 후 다시 연다 |
| 테스트가 영원히 안 끝남 | 테스트가 SIGTERM을 스스로 처리한다 | `timeout -s KILL 60 ./test_...` |
| 스크립트가 `\r` 오류로 실패 | Windows 줄바꿈(CRLF) | 저장소의 `.gitattributes`가 LF로 맞춘다. 직접 만든 파일은 LF로 저장 |
| `WireLayoutTest` 실패 "C 헤더에서 … 읽지 못했다" | `channel/`만 따로 받았다 | 저장소 전체를 받는다 (테스트가 `core/include`를 읽는다) |
| 원장을 다시 띄웠더니 주문 내역이 사라짐 | 원장 데몬은 상태를 메모리에만 둔다(데몬은 저널을 쓰지 않는다) | 정상 동작. 호가창·계좌가 시드 상태로 돌아간다 |
