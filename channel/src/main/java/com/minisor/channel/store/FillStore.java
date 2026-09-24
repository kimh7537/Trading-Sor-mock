package com.minisor.channel.store;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

/**
 * 체결 기록 (T11-02).
 *
 * <p>이 표가 <b>유일한 기록</b>이다. 보유·평균 단가·현금·수익률은 전부 여기서 계산한다
 * ({@link Portfolio}) — 따로 적어 두면 두 값이 언젠가 어긋나고, 그때 어느 쪽이 맞는지
 * 알 방법이 없다.
 *
 * <p><b>같은 체결을 두 번 적지 않는다.</b> 주문 응답과 주기 작업이 같은 체결을 볼 수
 * 있어서(둘 다 원장 상세를 읽는다) 유일 인덱스로 막고, 부딪히면 조용히 넘어간다.
 */
@Component
public class FillStore {

    private static final Logger log = LoggerFactory.getLogger(FillStore.class);

    /**
     * 체결 하나.
     *
     * @param kind 0=국내(원), 1=미국(센트). 통화가 다르므로 섞어 더하면 안 된다
     * @param side 0=매수, 1=매도 (WireEnums)
     */
    public record Fill(
            long id,
            String account,
            String symbol,
            int kind,
            int side,
            int market,
            long price,
            long qty,
            long orderId,
            long clOrdId,
            String at) {

        /** 체결 금액. 가격 x 수량. */
        public long notional() {
            return price * qty;
        }
    }

    private final Db db;

    public FillStore(Db db) {
        this.db = db;
    }

    /**
     * 적는다. 이미 있는 체결이면 아무것도 하지 않는다.
     *
     * @return 새로 적었으면 true
     */
    public boolean record(String account, String symbol, int kind, int side, int market,
            long price, long qty, long orderId, long clOrdId) {
        if (account == null || qty <= 0) {
            return false;
        }
        String sql = """
                INSERT OR IGNORE INTO fills
                  (account, symbol, kind, side, market, price, qty, order_id, cl_ord_id, at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""";
        try (Connection c = db.connection(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, account);
            ps.setString(2, symbol == null ? "" : symbol);
            ps.setInt(3, kind);
            ps.setInt(4, side);
            ps.setInt(5, market);
            ps.setLong(6, price);
            ps.setLong(7, qty);
            ps.setLong(8, orderId);
            ps.setLong(9, clOrdId);
            ps.setString(10, Instant.now().toString());
            return ps.executeUpdate() > 0;
        } catch (SQLException e) {
            /*
             * 기록을 못 남겨도 거래는 이미 일어났다. 주문 경로를 막지 않는다 —
             * 막으면 원장과 화면은 체결됐는데 사용자에게는 실패로 보인다.
             */
            log.warn("체결 기록 실패 {} {}: {}", account, orderId, e.getMessage());
            return false;
        }
    }

    /** 그 계좌의 체결 전부, 오래된 것부터. 되짚기가 순서를 쓴다. */
    public List<Fill> all(String account) {
        return query("SELECT * FROM fills WHERE account = ? ORDER BY id", account, 0);
    }

    /** 그 계좌의 최근 체결. 화면의 거래 내역이 쓴다. */
    public List<Fill> recent(String account, int limit) {
        return query("SELECT * FROM fills WHERE account = ? ORDER BY id DESC LIMIT ?",
                account, Math.max(1, Math.min(limit, 500)));
    }

    private List<Fill> query(String sql, String account, int limit) {
        List<Fill> out = new ArrayList<>();
        try (Connection c = db.connection(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, account);
            if (limit > 0) {
                ps.setInt(2, limit);
            }
            try (ResultSet rs = ps.executeQuery()) {
                while (rs.next()) {
                    out.add(new Fill(
                            rs.getLong("id"),
                            rs.getString("account"),
                            rs.getString("symbol"),
                            rs.getInt("kind"),
                            rs.getInt("side"),
                            rs.getInt("market"),
                            rs.getLong("price"),
                            rs.getLong("qty"),
                            rs.getLong("order_id"),
                            rs.getLong("cl_ord_id"),
                            rs.getString("at")));
                }
            }
        } catch (SQLException e) {
            log.warn("체결 조회 실패 {}: {}", account, e.getMessage());
        }
        return out;
    }
}
