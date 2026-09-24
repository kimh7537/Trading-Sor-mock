package com.minisor.channel.auth;

import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.security.spec.InvalidKeySpecException;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import tools.jackson.databind.ObjectMapper;

/**
 * 가입한 사람들. JSON 파일 하나에 적는다(T9-03).
 *
 * <p><b>왜 파일인가.</b> 이 저장소에 데이터베이스 계층이 없다. 사람 몇 명이 쓰는
 * 시뮬레이터에 DB를 들이면 설정·스키마·마이그레이션이 따라오는데, 여기서 지켜야 할
 * 불변조건은 "아이디가 겹치지 않는다" 하나뿐이다. 파일 하나와 메서드 단위 잠금으로 충분하다.
 *
 * <p><b>비밀번호는 PBKDF2-HMAC-SHA256으로 늘려 적는다.</b> 새 의존성을 들이지 않으려고
 * JDK에 있는 것을 쓴다. 사람마다 다른 소금을 쓰므로 같은 비밀번호라도 해시가 다르다.
 * 대조는 {@link MessageDigest#isEqual}로 한다 — 바이트를 앞에서부터 비교하면
 * 맞는 자리 수가 시간으로 새어 나간다.
 *
 * <p><b>계좌번호는 여기서 발급한다.</b> 사람이 고르게 두면 남의 계좌번호를 적어
 * 낼 수 있다. {@code u} + 11자리 일련번호로 전문 규격의 12자를 채운다.
 *
 * <p>ponytail: 파일을 통째로 읽어 메모리에 들고 있다가 통째로 쓴다. 가입자가 수만 명이
 * 되면 다시 볼 일이지만, 그때는 파일이 아니라 DB를 봐야 하는 때다.
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

    private final Path file;
    private final ObjectMapper json = new ObjectMapper();
    private final SecureRandom random = new SecureRandom();

    /** 아이디 -> 사람. 순서를 지켜 계좌번호 일련이 파일에서도 읽힌다. */
    private final Map<String, User> users = new LinkedHashMap<>();

    public UserStore(@Value("${minisor.auth.users-file:users.json}") String path) {
        this.file = Path.of(path);
        load();
    }

    /** 가입이 거절되는 까닭. 화면이 그대로 보여 줄 수 있는 말을 담는다. */
    public static final class SignupException extends RuntimeException {
        public SignupException(String message) {
            super(message);
        }
    }

    private synchronized void load() {
        if (!Files.exists(file)) {
            log.info("사용자 파일이 없다. 첫 가입 때 만든다: {}", file.toAbsolutePath());
            return;
        }
        try {
            User[] read = json.readValue(Files.readString(file), User[].class);
            for (User u : read) {
                users.put(u.id(), u);
            }
            log.info("사용자 {}명을 읽었다: {}", users.size(), file.toAbsolutePath());
        } catch (IOException | RuntimeException e) {
            /*
             * 읽지 못한 파일을 빈 것으로 치고 덮어쓰면 가입 기록이 통째로 날아간다.
             * 뜨지 않는 편이 낫다 — 사람이 파일을 보고 고쳐야 한다.
             */
            throw new IllegalStateException(
                    "사용자 파일을 읽지 못했다: " + file.toAbsolutePath(), e);
        }
    }

    /** 통째로 쓰고 제자리로 옮긴다. 쓰다 죽어도 반쪽짜리 파일이 남지 않는다. */
    private void save() {
        try {
            Path dir = file.toAbsolutePath().getParent();
            if (dir != null) {
                Files.createDirectories(dir);
            }
            Path tmp = Path.of(file.toAbsolutePath() + ".tmp");
            Files.writeString(tmp, json.writeValueAsString(new ArrayList<>(users.values())));
            try {
                Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING);
            }
        } catch (IOException e) {
            throw new IllegalStateException("사용자 파일을 쓰지 못했다: " + file, e);
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
        if (users.containsKey(id)) {
            throw new SignupException("이미 있는 아이디다");
        }

        byte[] salt = new byte[SALT_BYTES];
        random.nextBytes(salt);
        String saltB64 = Base64.getEncoder().encodeToString(salt);
        String hash = Base64.getEncoder().encodeToString(derive(password, salt));

        User u = new User(id, saltB64, hash, nextAccount(), Instant.now().toString());
        users.put(id, u);
        save();
        log.info("가입: {} -> 계좌 {}", id, u.account());
        return u;
    }

    /**
     * 로그인. 아이디가 없거나 비밀번호가 다르면 빈 값.
     *
     * <p><b>둘을 구분해 알리지 않는다</b> — "없는 아이디"와 "비밀번호 틀림"을 나눠 주면
     * 어떤 아이디가 있는지 찾아낼 수 있다.
     */
    public synchronized Optional<User> login(String id, String password) {
        User u = (id == null) ? null : users.get(id);
        if (u == null || password == null) {
            return Optional.empty();
        }
        byte[] want = Base64.getDecoder().decode(u.hash());
        byte[] got = derive(password, Base64.getDecoder().decode(u.salt()));
        return MessageDigest.isEqual(want, got) ? Optional.of(u) : Optional.empty();
    }

    public synchronized Optional<User> find(String id) {
        return Optional.ofNullable(users.get(id));
    }

    /** 가입한 모든 사람의 계좌번호. 주기 작업이 누구의 잔고를 읽을지 정하는 데 쓴다. */
    public synchronized List<String> accounts() {
        return users.values().stream().map(User::account).toList();
    }

    public synchronized int size() {
        return users.size();
    }

    /**
     * 다음 계좌번호. {@code u} + 11자리로 12자를 채운다.
     *
     * <p>사람 수로 세지 않고 <b>지금 있는 번호 중 가장 큰 것 다음</b>으로 간다 —
     * 사람 수로 세면 탈퇴가 생겼을 때 번호가 겹친다.
     */
    private String nextAccount() {
        long max = 0;
        for (User u : users.values()) {
            try {
                max = Math.max(max, Long.parseLong(u.account().substring(1)));
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
