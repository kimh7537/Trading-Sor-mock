package com.minisor.channel.store;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/**
 * 거래 기록 저장소 (T11-02).
 *
 * <p><b>왜 파일 하나짜리 SQLite인가.</b> 남는 것과 안 남는 것을 나눠 보면 답이 나온다 —
 * 원장(C)은 메모리에만 있어서 껐다 켜면 계좌가 사라진다. 그런데 사람은 컴퓨터를 끄고
 * 자고, 다음 날 <b>어제까지의 거래 내역과 수익률</b>을 보고 싶어 한다. 그 기록은
 * 프로세스보다 오래 살아야 한다.
 *
 * <p>클라우드 DB를 쓰지 않은 이유: 체결 한 건마다 네트워크 왕복이 생긴다. 이 프로젝트가
 * 재려는 것이 마이크로초 단위 집행 품질인데 그것과 어긋난다. 그리고 공개 저장소라
 * 키를 또 하나 관리해야 한다. 실제 증권사도 <b>매칭·원장은 메모리에서 돌리고 기록은
 * 따로 남긴다</b> — 저장소는 체결의 근거가 아니라 기록이다.
 *
 * <p><b>표는 둘뿐이다.</b> 가입자와 체결. 보유·현금·수익률은 저장하지 않고
 * {@link Portfolio}가 체결을 되짚어 계산한다 — 두 군데 적으면 언젠가 어긋난다.
 */
@Component
public class Db {

    private static final Logger log = LoggerFactory.getLogger(Db.class);

    private final String url;

    public Db(@Value("${minisor.db.file:minisor.db}") String file) {
        this.url = "jdbc:sqlite:" + file;
        init();
        log.info("거래 기록 저장소: {}", file);
    }

    /**
     * 새 접속. 쓰는 쪽이 닫는다.
     *
     * <p>접속을 재사용하지 않는다 — SQLite 접속은 여는 값이 싸고, 하나를 여럿이 나눠
     * 쓰면 스레드 경합을 이쪽에서 다시 다뤄야 한다.
     */
    public Connection connection() throws SQLException {
        return DriverManager.getConnection(url);
    }

    private void init() {
        try (Connection c = connection(); Statement st = c.createStatement()) {
            /*
             * WAL: 읽는 쪽이 쓰는 쪽을 막지 않는다. 주기 작업이 1초마다 읽는
             * 동안에도 체결이 들어오기 때문이다.
             */
            st.execute("PRAGMA journal_mode=WAL");
            st.execute("PRAGMA busy_timeout=3000");

            st.execute("""
                    CREATE TABLE IF NOT EXISTS users (
                      id         TEXT PRIMARY KEY,
                      salt       TEXT NOT NULL,
                      hash       TEXT NOT NULL,
                      account    TEXT NOT NULL UNIQUE,
                      created_at TEXT NOT NULL
                    )""");

            /*
             * 체결 하나. **이것만이 기록이고 나머지는 계산이다.**
             *
             * price는 정수다 - 국내는 원, 미국은 센트(kind로 구분). 통화를 섞어
             * 더하지 않도록 조회할 때 kind로 나눈다.
             */
            st.execute("""
                    CREATE TABLE IF NOT EXISTS fills (
                      id         INTEGER PRIMARY KEY AUTOINCREMENT,
                      account    TEXT    NOT NULL,
                      symbol     TEXT    NOT NULL,
                      kind       INTEGER NOT NULL,
                      side       INTEGER NOT NULL,
                      market     INTEGER NOT NULL,
                      price      INTEGER NOT NULL,
                      qty        INTEGER NOT NULL,
                      order_id   INTEGER NOT NULL,
                      cl_ord_id  INTEGER NOT NULL,
                      at         TEXT    NOT NULL,
                      feed       INTEGER NOT NULL DEFAULT 0
                    )""");
            /*
             * feed는 **어느 시세로 체결했나**다(0=시뮬, 1=실시세). 시뮬 가격은 가상
             * 참가자가 지어낸 값이고 실시세 가격은 바깥에서 받은 실호가다. 둘을 한
             * 수익률에 섞으면 그 숫자는 아무것도 뜻하지 않으므로 **장부를 따로 둔다**
             * — 통화를 섞지 않는 것과 같은 이유다.
             *
             * 칸이 생기기 전에 적힌 체결은 시뮬로 본다(기본값 0). 어느 시세로 냈는지
             * 알 길이 없고, 모르는 것을 실시세 성적에 넣는 것보다 낫다.
             */
            addColumnIfMissing(c, "fills", "feed", "INTEGER NOT NULL DEFAULT 0");

            st.execute("CREATE INDEX IF NOT EXISTS fills_by_account "
                    + "ON fills(account, id)");
            /*
             * 같은 체결이 두 번 적히지 않게 한다. 주문 응답과 주기 작업이 같은
             * 체결을 볼 수 있다 — 둘 다 원장 상세를 읽기 때문이다.
             */
            st.execute("CREATE UNIQUE INDEX IF NOT EXISTS fills_unique "
                    + "ON fills(account, order_id, market, price, qty, cl_ord_id)");
        } catch (SQLException e) {
            throw new IllegalStateException("거래 기록 저장소를 열지 못했다: " + url, e);
        }
    }

    /**
     * 없으면 칸을 더한다.
     *
     * <p>SQLite에 {@code ADD COLUMN IF NOT EXISTS}가 없어서 먼저 확인한다. 이미 쓰던
     * 파일에 칸이 생길 때 필요하다 — 지우고 다시 만들면 <b>거래 기록이 날아간다.</b>
     */
    private void addColumnIfMissing(Connection c, String table, String column,
            String definition) throws SQLException {
        try (var rs = c.getMetaData().getColumns(null, null, table, column)) {
            if (rs.next()) {
                return;
            }
        }
        try (Statement st = c.createStatement()) {
            st.execute("ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition);
            log.info("{} 표에 {} 칸을 더했다", table, column);
        }
    }
}
