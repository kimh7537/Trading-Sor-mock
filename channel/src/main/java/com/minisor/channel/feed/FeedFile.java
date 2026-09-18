package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ArrayNode;
import tools.jackson.databind.node.ObjectNode;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.List;

/**
 * 스냅샷을 파일로 남기고 다시 읽는다 (T8-06).
 *
 * <p><b>한 줄에 스냅샷 하나(JSONL).</b> 받는 도중에 프로세스가 죽어도 그때까지의 줄은
 * 온전하다 — 배열 하나로 감싸면 닫는 괄호가 없어 파일 전체를 못 읽는다.
 *
 * <pre>{@code
 * {"ts":32400000000000,"symbol":"005930","bids":[[69900,12]],"asks":[[70000,7]]}
 * }</pre>
 *
 * <p>가격·잔량을 정수 쌍으로 적는다. 바깥 시세의 문자열 소수는 받는 자리
 * ({@link Snapshot#fromToss})에서 이미 정수가 됐다 — 녹화 파일은 <b>우리 모양</b>이다.
 * 그래야 재생이 바깥 스키마 변화에 흔들리지 않는다.
 */
public final class FeedFile implements AutoCloseable {

    private static final JsonMapper JSON = JsonMapper.builder().build();

    private final BufferedWriter out;

    /** 이어 적는다 — 하루에 여러 번 켜도 앞의 녹화를 지우지 않는다. */
    public FeedFile(Path path) throws IOException {
        Path parent = path.getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }
        out =
                Files.newBufferedWriter(
                        path,
                        StandardCharsets.UTF_8,
                        StandardOpenOption.CREATE,
                        StandardOpenOption.APPEND);
    }

    public synchronized void write(Snapshot s) {
        try {
            out.write(toJson(s));
            out.newLine();
            /*
             * 줄마다 흘려보낸다. 녹화는 초당 몇 건이라 비용이 문제되지 않고, 버퍼에 남은
             * 채로 죽으면 마지막 몇 초가 통째로 사라진다.
             */
            out.flush();
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    @Override
    public void close() throws IOException {
        out.close();
    }

    static String toJson(Snapshot s) {
        ObjectNode n = JSON.createObjectNode();
        n.put("ts", s.tsNanos());
        n.put("symbol", s.symbol());
        n.set("bids", levels(s.bids()));
        n.set("asks", levels(s.asks()));
        return n.toString();
    }

    private static ArrayNode levels(List<int[]> src) {
        ArrayNode a = JSON.createArrayNode();
        for (int[] l : src) {
            ArrayNode pair = JSON.createArrayNode();
            pair.add(l[0]);
            pair.add(l[1]);
            a.add(pair);
        }
        return a;
    }

    /**
     * 파일 하나를 통째로 읽는다.
     *
     * <p><b>망가진 줄은 건너뛴다.</b> 녹화가 중간에 끊겼을 때 마지막 줄이 반쪽일 수 있는데,
     * 그 한 줄 때문에 앞의 멀쩡한 장 전체를 못 쓰는 것이 더 나쁘다.
     */
    public static List<Snapshot> read(Path path) throws IOException {
        List<Snapshot> out = new ArrayList<>();
        for (String line : Files.readAllLines(path, StandardCharsets.UTF_8)) {
            if (line.isBlank()) {
                continue;
            }
            try {
                out.add(fromJson(JSON.readTree(line)));
            } catch (RuntimeException e) {
                // 반쪽 줄이다. 건너뛴다
            }
        }
        return out;
    }

    static Snapshot fromJson(JsonNode n) {
        return new Snapshot(
                n.path("symbol").asText(""),
                n.path("ts").asLong(),
                levels(n.path("bids")),
                levels(n.path("asks")));
    }

    private static List<int[]> levels(JsonNode arr) {
        List<int[]> out = new ArrayList<>();
        if (arr != null && arr.isArray()) {
            for (JsonNode p : arr) {
                if (p.isArray() && p.size() == 2) {
                    out.add(new int[] {p.get(0).asInt(), p.get(1).asInt()});
                }
            }
        }
        return out;
    }
}
