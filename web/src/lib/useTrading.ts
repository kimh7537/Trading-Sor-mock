import { useCallback, useEffect, useRef, useState } from "react";
import {
  ACCOUNT,
  SYMBOL,
  cancelOrder,
  fetchSymbol,
  switchSymbol,
  type CurrentSymbol,
  fetchBalance,
  fetchBook,
  fetchFeed,
  fetchOrder,
  fetchOrders,
  setFeedMode,
  submitOrder,
  type OrderRequest,
  type OrderResponse,
} from "./api";
import { useStream, type ConnState, type StreamEvent } from "./useStream";
import type { Balance, Book, FeedStatus, Fill, LocalReject, Market, OrderView, Tick } from "./types";
import { MARKET_KRX, MARKET_NXT, marketName, reasonText, type Side } from "./wire";
import { time } from "./format";

/** 방송이 끊겼을 때 원장 상태를 대신 끌어오는 간격 */
const FALLBACK_POLL_MS = 3000;
/** 방송이 살아 있어도 가끔 전체를 맞춘다 — 놓친 방송이 있어도 화면이 영영 틀리지 않게 */
const RESYNC_MS = 20000;

export type NewOrder = Pick<OrderRequest, "side" | "type" | "market" | "price" | "qty">;

/** 차트에 남기는 점의 수. 1초에 한 점꼴이므로 4분쯤 */
const TICK_MAX = 240;

export interface Trading {
  ws: { state: ConnState; attempt: number };
  feed: FeedStatus | null;
  /** 지금 다루는 종목. 바꾸면 원장이 그 종목으로 새로 열린다(T8-10) */
  symbol: CurrentSymbol;
  pickSymbol: (code: string) => Promise<{ ok: boolean; message: string }>;
  ticks: Tick[];
  setMode: (mode: "sim" | "live") => Promise<{ ok: boolean; message: string }>;
  ledgerDown: string | null;
  books: Partial<Record<Market, Book>>;
  bookError: string | null;
  balance: Balance | null;
  orders: OrderView[];
  rejects: LocalReject[];
  fills: Fill[];
  events: number;
  lastSync: string | null;
  submit: (o: NewOrder) => Promise<OrderResponse>;
  cancel: (orderId: number) => Promise<{ ok: boolean; message: string }>;
  refresh: () => void;
}

const byNewest = (a: OrderView, b: OrderView) => b.orderId - a.orderId;

/**
 * 화면이 쓰는 원장 상태 전부(T7-04).
 *
 * 처음에 한 번 전부 읽고(호가·잔고·주문 목록), 그다음은 **채널계가 밀어 보내는 사건**으로 고친다.
 * 방송이 끊기면 몇 초마다 직접 읽는다. 상태의 주인은 원장이다 — 화면은 받은 것을 보여 줄 뿐
 * 체결이나 잔고를 스스로 계산하지 않는다.
 */
export function useTrading(): Trading {
  const [books, setBooks] = useState<Partial<Record<Market, Book>>>({});
  const [bookError, setBookError] = useState<string | null>(null);
  const [balance, setBalance] = useState<Balance | null>(null);
  const [orders, setOrders] = useState<OrderView[]>([]);
  const [rejects, setRejects] = useState<LocalReject[]>([]);
  const [fills, setFills] = useState<Fill[]>([]);
  const [ledgerDown, setLedgerDown] = useState<string | null>(null);
  const [events, setEvents] = useState(0);
  const [lastSync, setLastSync] = useState<string | null>(null);
  const [feed, setFeed] = useState<FeedStatus | null>(null);
  const [ticks, setTicks] = useState<Tick[]>([]);
  const lastClOrdId = useRef(0);
  const fillSeq = useRef(0);
  /* 다음 차트 점에 실을 체결 수량. 점을 찍을 때 0으로 되돌린다 */
  const pendingVol = useRef(0);
  /* 마지막으로 점을 찍을 때의 시세 모드. 바뀌면 그림을 새로 시작한다 */
  const lastFeedKey = useRef("");

  const [symbol, setSymbol] = useState<CurrentSymbol>({
    code: SYMBOL,
    name: "삼성전자",
    refPrice: 0,
  });

  const upsertOrder = useCallback((v: OrderView) => {
    setOrders((prev) => [v, ...prev.filter((o) => o.orderId !== v.orderId)].sort(byNewest));
  }, []);

  const refresh = useCallback(() => {
    void fetchFeed().then(setFeed).catch(() => undefined);
    /* 새로고침해도 지금 종목을 그대로 보여 준다 — 설정값이 아니라 원장이 든 것을 읽는다 */
    void fetchSymbol().then(setSymbol).catch(() => undefined);
    void Promise.allSettled([
      fetchBook(MARKET_KRX),
      fetchBook(MARKET_NXT),
      fetchBalance(),
      fetchOrders(),
    ]).then(([krx, nxt, bal, ords]) => {
      if (krx.status === "fulfilled" && nxt.status === "fulfilled") {
        setBooks({ KRX: krx.value, NXT: nxt.value });
        setBookError(null);
        setLastSync(time());
      } else {
        const r = krx.status === "rejected" ? krx : (nxt as PromiseRejectedResult);
        setBookError(String(r.reason));
      }
      if (bal.status === "fulfilled") setBalance(bal.value);
      if (ords.status === "fulfilled") setOrders([...ords.value].sort(byNewest));
    });
  }, []);

  /*
   * 종목을 바꾼다 — **원장이 새로 열린다.** 주문·잔고가 초기화되므로 바뀐 뒤에 전체를
   * 다시 읽는다. 이쪽 상태는 채널계가 성공을 돌려준 뒤에만 바꾼다.
   */
  const pickSymbol = useCallback(
    async (code: string) => {
      const res = await switchSymbol(code);
      if (!res.ok || !res.symbol) return { ok: false, message: res.message };
      setSymbol(res.symbol);
      setTicks([]);
      setOrders([]);
      refresh();
      return { ok: true, message: `${res.symbol.name}(${res.symbol.code})으로 바꿨다` };
    },
    [refresh],
  );

  const onEvent = useCallback(
    (e: StreamEvent) => {
      setEvents((n) => n + 1);
      switch (e.kind) {
        case "ledger-down":
          setLedgerDown(String(e.payload ?? "원인 미상"));
          break;
        case "ledger-up":
          setLedgerDown(null);
          refresh();
          break;
        case "book": {
          const b = e.payload as Omit<Book, "market"> & { market: number };
          const m = marketName(b.market) as Market;
          setBooks((prev) => ({ ...prev, [m]: { ...b, market: m } }));
          setBookError(null);
          setLastSync(time());
          break;
        }
        case "balance":
          setBalance(e.payload as Balance);
          break;
        case "feed-mode":
          setFeed(e.payload as FeedStatus);
          break;
        /* 다른 화면에서 종목을 바꿨다 — 이 화면도 따라간다 */
        case "symbol":
          setSymbol(e.payload as CurrentSymbol);
          setTicks([]);
          setOrders([]);
          refresh();
          break;
        case "order-update":
          upsertOrder(e.payload as OrderView);
          break;
        case "order": {
          // 어느 화면에서 낸 주문이든 목록에 들어온다. 접수된 것은 원장에서 상세를 읽는다.
          const { request: req, result: res } = e.payload as {
            request: OrderRequest;
            result: OrderResponse;
          };
          if (res.outcome === "ACCEPTED") {
            void fetchOrder(res.orderId)
              .then((v) => v && upsertOrder(v))
              .catch(() => undefined);
          } else {
            setRejects((prev) =>
              prev.some((r) => r.clOrdId === req.clOrdId)
                ? prev
                : [
                    {
                      clOrdId: req.clOrdId,
                      at: time(),
                      side: req.side as Side,
                      type: req.type,
                      market: req.market,
                      price: req.price,
                      qty: req.qty,
                      outcome: res.outcome as LocalReject["outcome"],
                      reason:
                        res.outcome === "IN_DOUBT" ? res.message : reasonText(res.reason),
                    },
                    ...prev,
                  ].slice(0, 100),
            );
          }
          void fetchBalance().then(setBalance).catch(() => undefined);
          break;
        }
        case "fill": {
          const p = e.payload as {
            market: number;
            side: Side;
            price: number;
            qty: number;
            clOrdId: number;
            orderId: number;
          };
          fillSeq.current += 1;
          pendingVol.current += p.qty;
          const id = `${p.orderId}-${fillSeq.current}`;
          setFills((prev) =>
            [
              {
                id,
                at: time(),
                market: marketName(p.market),
                side: p.side,
                price: p.price,
                qty: p.qty,
                clOrdId: p.clOrdId,
                orderId: p.orderId,
              },
              ...prev,
            ].slice(0, 300),
          );
          break;
        }
      }
    },
    [refresh, upsertOrder],
  );

  const { state, attempt } = useStream(onEvent);

  useEffect(() => {
    refresh();
  }, [refresh]);

  useEffect(() => {
    const t = window.setInterval(refresh, state === "open" ? RESYNC_MS : FALLBACK_POLL_MS);
    return () => window.clearInterval(t);
  }, [refresh, state]);

  const feedKey = `${feed?.mode ?? ""}:${feed?.source ?? ""}`;

  /*
   * 호가가 바뀔 때마다 차트에 점 하나. **화면이 값을 지어내지 않는다** — 가격은 그 시장
   * 최우선호가의 중간값이고, 막대는 방송으로 받은 내 체결 수량이다.
   *
   * 두 시장을 합쳐서 재지 않는다. 합치면 한쪽의 최우선 매도와 다른 쪽의 최우선 매수가
   * 기준가에서 맞물려 중간가가 기준가에 붙박이가 된다 — 선이 평평해서 아무것도 못 읽는다.
   */
  useEffect(() => {
    /* 실시세 모드에서는 시장이 하나다 — 통합 시세를 심는 그 시장만 센다 */
    const only = feed?.mode === "live" ? (marketName(feed.market) as Market) : null;
    const mid = (m: Market) => {
      if (only && only !== m) return 0;
      const ask = books[m]?.asks[0]?.price ?? 0;
      const bid = books[m]?.bids[0]?.price ?? 0;
      if (!ask && !bid) return 0;
      return ask && bid ? Math.round((ask + bid) / 2) : ask || bid;
    };
    const krx = mid("KRX");
    const nxt = mid("NXT");
    if (!krx && !nxt) return;
    const vol = pendingVol.current;
    pendingVol.current = 0;

    /*
     * **모드가 바뀌면 앞의 점을 버린다.** 시뮬의 중간가와 실시세의 중간가는 호가를 만드는
     * 주체가 다르다. 한 그림에 이어 붙이면 모드를 바꾼 자리가 가격이 뛴 것처럼 보인다.
     */
    const fresh = lastFeedKey.current !== feedKey;
    lastFeedKey.current = feedKey;
    setTicks((prev) =>
      [...(fresh ? [] : prev), { t: Date.now(), krx, nxt, vol }].slice(-TICK_MAX),
    );
  }, [books, feed, feedKey]);

  const setMode = useCallback(async (mode: "sim" | "live") => {
    const { status, feed: got } = await setFeedMode(mode);
    if (got) setFeed(got);
    if (status === 200) {
      return { ok: true, message: mode === "live" ? "실시세 모드" : "시뮬 모드" };
    }
    if (status === 202) {
      // 붙어 봐야 안다. 붙으면 방송이 화면을 바꾼다
      return { ok: true, message: "실시세에 붙는 중 — 붙으면 호가창이 바뀐다" };
    }
    if (status === 409) {
      return { ok: false, message: got?.note ?? "실시세 설정이 없다" };
    }
    return { ok: false, message: got?.error ?? "채널계가 모드를 바꾸지 못했다" };
  }, []);

  const submit = useCallback(
    async (o: NewOrder) => {
      // 같은 밀리초에 두 번 눌러도 번호가 겹치지 않게
      const now = Date.now() % 1_000_000_000;
      lastClOrdId.current = Math.max(now, lastClOrdId.current + 1);
      const res = await submitOrder({
        ...o,
        account: ACCOUNT,
        symbol: symbol.code,
        clOrdId: lastClOrdId.current,
      });
      if (res.outcome === "ACCEPTED") {
        const v = await fetchOrder(res.orderId).catch(() => null);
        if (v) upsertOrder(v);
        void fetchBalance().then(setBalance).catch(() => undefined);
      }
      return res;
    },
    [symbol.code, upsertOrder],
  );

  const cancel = useCallback(
    async (orderId: number) => {
      const { status, result } = await cancelOrder(orderId);
      if (result?.order) upsertOrder(result.order);
      void fetchBalance().then(setBalance).catch(() => undefined);
      if (status === 200) return { ok: true, message: `${result?.canceledQty ?? 0}주 취소` };
      if (status === 409) return { ok: false, message: "이미 체결·취소로 끝난 주문" };
      if (status === 404) return { ok: false, message: "모르는 주문" };
      if (status === 503 || status === 0) {
        return { ok: false, message: "원장에 연결하지 못함 — 다시 시도해도 안전" };
      }
      return { ok: false, message: result ? reasonText(result.reason) : `HTTP ${status}` };
    },
    [upsertOrder],
  );

  return {
    ws: { state, attempt },
    feed,
    symbol,
    pickSymbol,
    ticks,
    setMode,
    ledgerDown,
    books,
    bookError,
    balance,
    orders,
    rejects,
    fills,
    events,
    lastSync,
    submit,
    cancel,
    refresh,
  };
}
