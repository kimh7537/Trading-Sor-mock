package com.minisor.channel.auth;

/**
 * 가입한 사람 하나. {@code users.json}에 이 모양 그대로 적힌다.
 *
 * <p><b>비밀번호 원문은 어디에도 남기지 않는다.</b> 소금(salt)과 PBKDF2 해시만 적는다 —
 * 파일이 통째로 새어도 비밀번호를 되돌릴 수 없어야 한다.
 *
 * <p>{@code account}는 원장 계좌번호다. 전문 규격이 12자로 못 박아 뒀으므로
 * ({@code MSG_ACCOUNT_LEN}) 여기서도 12자다. 사람이 고르게 두지 않고 가입할 때
 * 발급한다 — 고르게 두면 남의 계좌번호를 적어 낼 수 있다.
 */
public record User(String id, String salt, String hash, String account, String createdAt) {}
