package com.minisor.channel.auth;

import com.minisor.channel.store.Db;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.security.spec.InvalidKeySpecException;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Optional;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import tools.jackson.databind.ObjectMapper;

/**
 * 가입한 사람들 (T9-03, 저장소는 T11-02에서 SQLite로).
 *
 * <p>처음에는 JSON 파일 하나였다. 거래 기록을 남기면서 SQLite가 들어왔고, 가입자를
 * 따로 둘 이유가 없어져 같은 파일로 옮겼다 — <b>백업할 파일이 하나</b>가 된다.
 * 예전 {@code users.json}이 있으면 처음 뜰 때 한 번 옮겨 담는다.
 *
 * <p><b>비밀번호는 PBKDF2-HMAC-SHA256으로 늘려 적는다.</b> 새 의존성을 들이지 않으려고
 * JDK에 있는 것을 쓴다. 사람마다 다른 소금을 쓰므로 같은 비밀번호라도 해시가 다르다.
 * 대조는 {@link MessageDigest#isEqual}로 한다 — 바이트를 앞에서부터 비교하면
 * 맞는 자리 수가 시간으로 새어 나간다.
 *
 * <p><b>계좌번호는 여기서 발급한다.</b> 사람이 고르게 두면 남의 계좌번호를 적어
 * 낼 수 있다. {@code u} + 11자리 일련번호로 전문 규격의 12자를 채운다.
 */
@Component
public class UserStore {

    private static final Logger log = LoggerFactory.getLogger(UserStore.class);

    /** PBKDF2 반복 수. 늘릴수록 대입 공격이 느려지고 로그인도 느려진다. */
    private static final int ITERATIONS = 120_000;

    private static final int SALT_BYTES = 16;
    private static final int KEY_BITS = 256;

    /** 아이디에 허용하는 글자. 파일·JSON·화면 어디서도 말썽이 없는 범위로 좁힌다. */
    private static final String ID_PATTERN = "[a-zA-Z0-9_-]{3,20}";

    private static final int PASSWORD_MIN = 8;
    private static final int PASSWORD_MAX = 100;

    private final Db db;
    private final SecureRandom random = new SecureRandom();

    public UserStore(Db db, @Value("${minisor.auth.users-file:users.json}") String legacy) {
        this.db = db;
        importLegacy(legacy);
    }

    /** 가입이 거절되는 까닭. 화면이 그대로 보여 줄 수 있는 말을 담는다. */
    public static final class SignupException extends RuntimeException {
        public SignupException(String message) {
            super(message);
        }
    }

    /**
     * 옛 {@code users.json}을 한 번 옮겨 담는다.
     *
     * <p>이미 표에 사람이 있으면 건너뛴다 — 옮긴 뒤에 지워진 계정이 파일에 남아 있다가
     * 되살아나면 안 된다.
     */
    private void importLegacy(String path) {
        Path file = Path.of(path);
        if (!Files.exists(file) || size() > 0) {
            return;
        }
        try {
            User[] read = new ObjectMapper().readValue(Files.readString(file), User[].class);
            for (User u : read) {
                insert(u);
            }
            log.info("옛 사용자 파일에서 {}명을 옮겨 담았다: {}", read.length, file);
        } catch (IOException | RuntimeException e) {
            log.warn("옛 사용자 파일을 읽지 못했다 {}: {}", file, e.toString());
        }
    }

    /**
     * 가입. 아이디가 겹치거나 형식이 틀리면 {@link SignupException}.
     *
     * @return 새로 만든 사람(계좌번호 포함)
     */
    public synchronized User signup(String id, String password) {
        if (id == null || !id.matches(ID_PATTERN)) {
            throw new SignupException("아이디는 영문·숫자·_·- 로 3~20자여야 한다");
        }
        if (password == null || password.length() < PASSWORD_MIN
                || password.length() > PASSWORD_MAX) {
            throw new SignupException("비밀번호는 " + PASSWORD_MIN + "자 이상이어야 한다");
        }
        if (find(id).isPresent()) {
            throw new SignupException("이미 있는 아이디다");
        }

        byte[] salt = new byte[SALT_BYTES];
        random.nextBytes(salt);
        User u = new User(
                id,
                Base64.getEncoder().encodeToString(salt),
                Base64.getEncoder().encodeToString(derive(password, salt)),
                nextAccount(),
                Instant.now().toString());

        if (!insert(u)) {
            throw new SignupException("이미 있는 아이디다");
        }
        log.info("가입: {} -> 계좌 {}", id, u.account());
        return u;
    }

    /**
     * 로그인. 아이디가 없거나 비밀번호가 다르면 빈 값.
     *
     * <p><b>둘을 구분해 알리지 않는다</b> — "없는 아이디"와 "비밀번호 틀림"을 나눠 주면
     * 어떤 아이디가 있는지 찾아낼 수 있다.
     */
    public Optional<User> login(String id, String password) {
        Optional<User> found = find(id);
        if (found.isEmpty() || password == null) {
            return Optional.empty();
        }
        User u = found.get();
        byte[] want = Base64.getDecoder().decode(u.hash());
        byte[] got = derive(password, Base64.getDecoder().decode(u.salt()));
        return MessageDigest.isEqual(want, got) ? found : Optional.empty();
    }

    public Optional<User> find(String id) {
        if (id == null) {
            return Optional.empty();
        }
        try (Connection c = db.connection();
                PreparedStatement ps = c.prepareStatement("SELECT * FROM users WHERE id = ?")) {
            ps.setString(1, id);
            try (ResultSet rs = ps.executeQuery()) {
                return rs.next() ? Optional.of(read(rs)) : Optional.empty();
            }
        } catch (SQLException e) {
            throw new IllegalStateException("사용자를 읽지 못했다: " + id, e);
        }
    }

    /** 계좌번호로 찾는다. 주기 작업이 세션의 계좌만 들고 있을 때 쓴다. */
    public Optional<User> byAccount(String account) {
        if (account == null) {
            return Optional.empty();
        }
        try (Connection c = db.connection();
                PreparedStatement ps =
                        c.prepareStatement("SELECT * FROM users WHERE account = ?")) {
            ps.setString(1, account);
            try (ResultSet rs = ps.executeQuery()) {
                return rs.next() ? Optional.of(read(rs)) : Optional.empty();
            }
        } catch (SQLException e) {
            throw new IllegalStateException("계좌로 사용자를 읽지 못했다: " + account, e);
        }
    }

    /** 가입한 모든 사람의 계좌번호. 주기 작업이 누구의 잔고를 읽을지 정하는 데 쓴다. */
    public List<String> accounts() {
        List<String> out = new ArrayList<>();
        try (Connection c = db.connection();
                PreparedStatement ps =
                        c.prepareStatement("SELECT account FROM users ORDER BY account")) {
            try (ResultSet rs = ps.executeQuery()) {
                while (rs.next()) {
                    out.add(rs.getString(1));
                }
            }
        } catch (SQLException e) {
            log.warn("계좌 목록을 읽지 못했다: {}", e.getMessage());
        }
        return out;
    }

    public int size() {
        try (Connection c = db.connection();
                PreparedStatement ps = c.prepareStatement("SELECT COUNT(*) FROM users");
                ResultSet rs = ps.executeQuery()) {
            return rs.next() ? rs.getInt(1) : 0;
        } catch (SQLException e) {
            return 0;
        }
    }

    // ------------------------------------------------------------------

    private static User read(ResultSet rs) throws SQLException {
        return new User(rs.getString("id"), rs.getString("salt"), rs.getString("hash"),
                rs.getString("account"), rs.getString("created_at"));
    }

    /** 넣는다. 아이디나 계좌가 겹치면 false. */
    private boolean insert(User u) {
        String sql = "INSERT OR IGNORE INTO users (id, salt, hash, account, created_at) "
                + "VALUES (?, ?, ?, ?, ?)";
        try (Connection c = db.connection(); PreparedStatement ps = c.prepareStatement(sql)) {
            ps.setString(1, u.id());
            ps.setString(2, u.salt());
            ps.setString(3, u.hash());
            ps.setString(4, u.account());
            ps.setString(5, u.createdAt());
            return ps.executeUpdate() > 0;
        } catch (SQLException e) {
            throw new IllegalStateException("사용자를 적지 못했다: " + u.id(), e);
        }
    }

    /**
     * 다음 계좌번호. {@code u} + 11자리로 12자를 채운다.
     *
     * <p>사람 수로 세지 않고 <b>지금 있는 번호 중 가장 큰 것 다음</b>으로 간다 —
     * 사람 수로 세면 탈퇴가 생겼을 때 번호가 겹친다.
     */
    private String nextAccount() {
        long max = 0;
        for (String a : accounts()) {
            try {
                max = Math.max(max, Long.parseLong(a.substring(1)));
            } catch (NumberFormatException | IndexOutOfBoundsException ignored) {
                // 손으로 적어 넣은 번호. 일련번호 계산에서만 빼고 그대로 둔다
            }
        }
        return String.format("u%011d", max + 1);
    }

    private byte[] derive(String password, byte[] salt) {
        try {
            PBEKeySpec spec =
                    new PBEKeySpec(password.toCharArray(), salt, ITERATIONS, KEY_BITS);
            return SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
                    .generateSecret(spec)
                    .getEncoded();
        } catch (NoSuchAlgorithmException | InvalidKeySpecException e) {
            throw new IllegalStateException("PBKDF2를 쓸 수 없다", e);
        }
    }
}
