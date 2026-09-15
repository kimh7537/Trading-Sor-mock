#include "worker_pool.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"

/*
 * 워커가 연달아 실패하면 그만둔다.
 *
 * 리스닝 소켓이 닫히면 `accept()`가 곧바로 실패하고, 그대로 두면 워커가 CPU를
 * 태우며 도는 루프가 된다. 잘못된 전문도 음수를 돌려주지만 그건 정상 운영이므로
 * **연달아** 실패할 때만 그만둔다.
 *
 * ponytail: 16은 어림수다. 실패 원인을 구분할 수 있게 되면(T3-08의 epoll 루프가
 * 들어오면) 이 눈금은 없앤다.
 */
#define WORKER_FAIL_LIMIT 16

/* 감시 루프가 자식을 거두러 도는 간격. */
#define SUPERVISE_TICK_NS (20 * 1000 * 1000) /* 20ms */

typedef struct {
    pid_t   pid;
    int     rfd;     /* 부모가 읽는 끝. 워커는 반대쪽에 1바이트씩 쓴다 */
    int32_t handled; /* 지금까지 거둔 처리 건수 */
} worker_t;

struct worker_pool {
    pool_config_t cfg;
    worker_t      w[POOL_WORKERS_MAX];
    int32_t       count;
    int32_t       restarts;
    bool          stopped;
};

/* --- 워커 쪽 --- */

/*
 * 워커의 본체. **돌아오지 않는다.**
 *
 * fork한 자식에서 `return`하면 부모의 호출 스택을 그대로 이어받아 테스트 코드
 * 안으로 돌아간다 — 자식이 부모인 척하게 된다. `_exit`으로 끝낸다.
 *
 * `exit`이 아니라 `_exit`인 이유: `exit`은 atexit 핸들러를 부르고 stdio 버퍼를
 * 비운다. 자식은 부모의 버퍼 사본을 들고 있으므로 이미 부모가 찍은 내용을 한 번
 * 더 찍게 된다.
 */
static void worker_main(worker_pool_t *pool, int wfd)
{
    /* 부모가 남긴 멈춤 상태를 물려받지 않는다. */
    listener_reset_stop();
    (void)listener_install_signals();

    int fails = 0;

    while (!listener_stopping()) {
        bool accepted = false;
        int  rc =
            listener_serve_one(pool->cfg.ln, pool->cfg.fn, pool->cfg.ctx,
                               &accepted);

        if (rc == ERR_NULL_PTR) {
            break; /* 리스너가 없다. 더 할 일이 없다 */
        }

        if (!accepted) {
            break; /* 접속을 받지 못했다 — 멈추라는 신호였다 */
        }

        /*
         * 접속 하나를 다뤘다. 전문이 0건이어도(그냥 붙었다 끊은 접속) 접속은
         * 접속이므로 센다 — 분배가 일어났는지를 보는 값이다.
         *
         * **멈춤 확인보다 먼저 센다.** 접속을 끝낸 직후에 SIGTERM이 오면, 나중에
         * 세는 코드는 이미 끝낸 일을 집계에서 빠뜨린다. 실제로 40건 중 39건만
         * 잡히는 것으로 나타났다.
         *
         * 1바이트 쓰기는 커널이 원자성을 보장한다. 락이 필요 없다.
         */
        uint8_t one = 1;
        ssize_t w = write(wfd, &one, 1);
        (void)w;

        if (rc < 0) {
            if (++fails >= WORKER_FAIL_LIMIT) {
                break;
            }
        } else {
            fails = 0;
        }
    }

    close(wfd);
    _exit(0);
}

/* --- 부모 쪽 --- */

/* 파이프에 쌓인 것을 전부 읽어 센다. 읽기 끝은 논블로킹이라 막히지 않는다. */
static void drain(worker_t *w)
{
    if (w->rfd < 0) {
        return;
    }

    uint8_t buf[256];
    for (;;) {
        ssize_t r = read(w->rfd, buf, sizeof(buf));
        if (r > 0) {
            w->handled += (int32_t)r;
            continue;
        }
        if (r < 0 && errno == EINTR) {
            continue;
        }
        break; /* 0(EOF) 또는 EAGAIN */
    }
}

/*
 * 워커 하나를 띄운다. 성공하면 ERR_OK.
 *
 * 파이프를 만들고 fork한다. **부모는 쓰기 끝을 곧바로 닫는다** — 열어 두면
 * 워커가 다 죽어도 EOF가 오지 않고, 다음 워커를 fork할 때 그 fd가 상속되어
 * 문제가 번진다.
 */
static int spawn(worker_pool_t *pool, int32_t idx)
{
    int fds[2];

    if (pipe(fds) != 0) {
        return ERR_POOL_EXHAUSTED;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return ERR_POOL_EXHAUSTED;
    }

    if (pid == 0) {
        close(fds[0]); /* 자식은 읽지 않는다 */
        worker_main(pool, fds[1]);
        _exit(0); /* worker_main이 돌아오지 않지만 컴파일러는 모른다 */
    }

    close(fds[1]); /* 부모는 쓰지 않는다 */

    /* 읽기 끝을 논블로킹으로. drain이 막히면 감시 루프가 멈춘다. */
    int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }

    pool->w[idx].pid = pid;
    pool->w[idx].rfd = fds[0];

    return ERR_OK;
}

worker_pool_t *pool_start(const pool_config_t *cfg)
{
    if (cfg == NULL || cfg->ln == NULL) {
        return NULL;
    }
    if (cfg->worker_count <= 0 || cfg->worker_count > POOL_WORKERS_MAX) {
        return NULL;
    }

    worker_pool_t *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }
    pool->cfg = *cfg;
    pool->count = cfg->worker_count;

    for (int32_t i = 0; i < pool->count; i++) {
        pool->w[i].pid = -1;
        pool->w[i].rfd = -1;
    }

    /*
     * **요청이 오기 전에 전부 만든다.** 이것이 사전 fork다 — 접속마다 fork하면
     * 주소 공간 복사 비용이 응답 시간에 그대로 실린다.
     */
    for (int32_t i = 0; i < pool->count; i++) {
        if (spawn(pool, i) != ERR_OK) {
            /* 일부만 만들어진 풀을 돌려주지 않는다. 만든 것을 거두고 접는다. */
            pool_stop(pool);
            pool_destroy(pool);
            return NULL;
        }
    }

    return pool;
}

/* 그 pid를 가진 워커의 자리. 없으면 -1. */
static int32_t find_by_pid(const worker_pool_t *pool, pid_t pid)
{
    for (int32_t i = 0; i < pool->count; i++) {
        if (pool->w[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

int pool_supervise(worker_pool_t *pool)
{
    if (pool == NULL) {
        return ERR_NULL_PTR;
    }

    while (!listener_stopping()) {
        int   status = 0;
        pid_t dead = waitpid(-1, &status, WNOHANG);

        if (dead > 0) {
            int32_t idx = find_by_pid(pool, dead);
            if (idx >= 0) {
                /*
                 * 워커가 죽었다. **남긴 처리 건수를 먼저 거둔 뒤** 자리를 채운다.
                 * 순서를 바꾸면 새 파이프로 덮여 그만큼이 사라진다.
                 */
                drain(&pool->w[idx]);
                close(pool->w[idx].rfd);
                pool->w[idx].rfd = -1;
                pool->w[idx].pid = -1;

                if (spawn(pool, idx) == ERR_OK) {
                    pool->restarts++;
                }
            }
            continue; /* 한 번에 여럿이 죽었을 수 있다 */
        }

        if (dead < 0 && errno == ECHILD) {
            break; /* 자식이 하나도 없다 */
        }

        /*
         * 거둘 자식이 없다. 잠깐 쉰다.
         *
         * ponytail: 폴링이다. SIGCHLD + self-pipe로 바꾸면 깨어나는 횟수가 줄지만
         * 감시 루프는 전문 경로가 아니라 20ms가 비싸지 않다. T3-08에서 epoll이
         * 들어오면 그때 같이 옮긴다.
         */
        struct timespec ts = {0, SUPERVISE_TICK_NS};
        nanosleep(&ts, NULL);
    }

    return pool->restarts;
}

void pool_stop(worker_pool_t *pool)
{
    if (pool == NULL || pool->stopped) {
        return;
    }
    pool->stopped = true;

    /* 먼저 전부에게 멈추라고 한다. 하나씩 기다리며 죽이면 그만큼 느려진다. */
    for (int32_t i = 0; i < pool->count; i++) {
        if (pool->w[i].pid > 0) {
            kill(pool->w[i].pid, SIGTERM);
        }
    }

    /*
     * 전부 거둔다. **좀비를 남기지 않는다** — 부모가 오래 사는 프로세스라
     * 거두지 않은 자식이 쌓이면 프로세스 표가 찬다.
     */
    for (int32_t i = 0; i < pool->count; i++) {
        if (pool->w[i].pid > 0) {
            int status = 0;
            while (waitpid(pool->w[i].pid, &status, 0) < 0 && errno == EINTR) {
                /* 시그널에 깨어난 것뿐이다. 다시 기다린다 */
            }
            pool->w[i].pid = -1;
        }
        /* 워커가 죽은 뒤에 읽어야 파이프에 남은 것까지 다 센다. */
        drain(&pool->w[i]);
        if (pool->w[i].rfd >= 0) {
            close(pool->w[i].rfd);
            pool->w[i].rfd = -1;
        }
    }
}

void pool_destroy(worker_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }
    if (!pool->stopped) {
        pool_stop(pool);
    }
    free(pool);
}

int32_t pool_worker_count(const worker_pool_t *pool)
{
    return (pool != NULL) ? pool->count : 0;
}

int32_t pool_handled(const worker_pool_t *pool, int32_t index)
{
    if (pool == NULL || index < 0 || index >= pool->count) {
        return -1;
    }
    return pool->w[index].handled;
}

int32_t pool_handled_total(const worker_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }
    int32_t sum = 0;
    for (int32_t i = 0; i < pool->count; i++) {
        sum += pool->w[i].handled;
    }
    return sum;
}

int32_t pool_restarts(const worker_pool_t *pool)
{
    return (pool != NULL) ? pool->restarts : 0;
}

pid_t pool_worker_pid(const worker_pool_t *pool, int32_t index)
{
    if (pool == NULL || index < 0 || index >= pool->count) {
        return -1;
    }
    return pool->w[index].pid;
}
