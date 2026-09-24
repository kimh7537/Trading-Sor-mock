package com.minisor.channel.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** T9-03 — 가입·로그인, 비밀번호 보관, 계좌 발급. */
class UserStoreTest {

    @TempDir Path dir;

    private UserStore store() {
        return new UserStore(dir.resolve("users.json").toString());
    }

    @Test
    void signupIssuesSequentialAccounts() {
        UserStore s = store();
        assertThat(s.signup("alice", "password1").account()).isEqualTo("u00000000001");
        assertThat(s.signup("bob", "password2").account()).isEqualTo("u00000000002");
        assertThat(s.accounts()).containsExactly("u00000000001", "u00000000002");
    }

    /** <b>비밀번호 원문이 파일에 남으면 안 된다.</b> 파일이 새면 그대로 남의 계정이 된다. */
    @Test
    void passwordIsNeverStoredInClear() throws Exception {
        UserStore s = store();
        User u = s.signup("alice", "hunter2secret");

        String raw = Files.readString(dir.resolve("users.json"));
        assertThat(raw).doesNotContain("hunter2secret");
        assertThat(u.hash()).isNotEqualTo("hunter2secret");
        /* 소금이 사람마다 다르므로 같은 비밀번호라도 해시가 다르다 */
        assertThat(s.signup("bob", "hunter2secret").hash()).isNotEqualTo(u.hash());
    }

    @Test
    void loginChecksPassword() {
        UserStore s = store();
        s.signup("alice", "password1");

        assertThat(s.login("alice", "password1")).isPresent();
        assertThat(s.login("alice", "password2")).isEmpty();
        assertThat(s.login("nobody", "password1")).isEmpty();
        assertThat(s.login(null, null)).isEmpty();
    }

    @Test
    void duplicateIdIsRejected() {
        UserStore s = store();
        s.signup("alice", "password1");
        assertThatThrownBy(() -> s.signup("alice", "password9"))
                .isInstanceOf(UserStore.SignupException.class);
        assertThat(s.size()).isEqualTo(1);
    }

    @Test
    void badIdOrShortPasswordIsRejected() {
        UserStore s = store();
        assertThatThrownBy(() -> s.signup("a", "password1"))
                .isInstanceOf(UserStore.SignupException.class);
        assertThatThrownBy(() -> s.signup("한글이름", "password1"))
                .isInstanceOf(UserStore.SignupException.class);
        assertThatThrownBy(() -> s.signup("alice", "short"))
                .isInstanceOf(UserStore.SignupException.class);
        assertThat(s.size()).isZero();
    }

    /** 채널계를 다시 띄워도 가입이 남는다 — 그것이 파일에 적는 이유다. */
    @Test
    void survivesRestart() {
        store().signup("alice", "password1");

        UserStore again = store();
        assertThat(again.login("alice", "password1")).isPresent();
        /* 계좌번호는 사람 수가 아니라 가장 큰 번호 다음으로 간다 */
        assertThat(again.signup("bob", "password2").account()).isEqualTo("u00000000002");
    }
}
