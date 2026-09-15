# 막힌 것

진행하다 사람의 손이 필요해 멈춘 지점을 적는다. 해결되면 지운다.

---

## [B-01] Phase 4 툴체인이 WSL에 없다 (2026-09-16)

**상태**: 열림. 사람이 한 줄 실행해 줘야 한다.

### 무엇이 없나

WSL Ubuntu에 Phase 4(채널계 Java/Spring Boot + 프론트엔드 React)에 필요한 것이
하나도 없다.

| 도구 | 상태 |
|---|---|
| java / javac | 없음 |
| maven / gradle | 없음 |
| node | 없음 |
| npm | 10.9.2가 응답하지만 **Windows 쪽 npm이 PATH로 새어 든 것**이다. node가 없으므로 리눅스 툴체인으로 쓸 수 없다 |

### 왜 내가 못 깔았나

`sudo`가 비밀번호를 요구한다(`sudo -n true` 실패). 자동 진행 중에는 답할 사람이
없어서 설치가 비밀번호 프롬프트에서 멈춘다.

### 해결

WSL 안에서 한 번 실행한다.

```bash
sudo apt update
sudo apt install -y openjdk-21-jdk maven nodejs npm
```

확인:

```bash
java -version && mvn -version && node --version && npm --version
```

`nodejs`가 너무 낮은 버전으로 깔리면(Ubuntu 기본 저장소는 뒤처진다) nvm을 쓴다.

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
source ~/.bashrc && nvm install --lts
```

### 영향 범위

- **Phase 2 잔여 (T2-09~T2-14)**: 영향 없음. 순수 C
- **Phase 3 (원장 + FEP)**: 영향 없음. 순수 C + TCP 소켓
- **Phase 4 (채널계 + 프론트엔드)**: **전면 차단**
- **Phase 5**: 측정·문서 부분은 진행 가능. Phase 4에 얹히는 부분은 차단

그래서 이 블로커가 풀릴 때까지 **Phase 2 잔여 → Phase 3 → Phase 5의 측정 부분**
순서로 진행한다. Phase 4는 건너뛰고 나중에 돌아온다.

### 덧붙임 — 프론트엔드는 툴체인이 생겨도 한계가 있다

React 화면은 빌드가 통과해도 **내가 눈으로 확인할 수 없다.** 브라우저를 띄워
보는 도구가 이 환경에 붙어 있지 않다. "빌드는 되는데 화면이 맞는지 모르는" 상태가
되므로, Phase 4의 프론트엔드는 사람이 한 번은 직접 봐야 한다.
