import { useCallback, useEffect, useRef, useState } from "react";
import {
  ACCOUNT,
  SYMBOL,
  cancelOrder,
  fetchBalance,
  fetchBook,
  fetchOrder,
  fetchOrders,
  submitOrder,
  type OrderRequest,
  type OrderResponse,
} from "./api";
import { useStream, type ConnState, type StreamEvent } from "./useStream";
import type { Balance, Book, Fill, LocalReject, Market, OrderView } from "./types";
import { MARKET_KRX, MARKET_NXT, marketName, reasonText, type Side } from "./wire";
import { time } from "./format";

/** 방송이 끊겼을 때 원장 상태를 대신 끌어오는 간격 */
const FALLBACK_POLL_MS = 3000;
/** 방송이 살아 있어도 가끔 전체를 맞춘다 — 놓친 방송이 있어도 화면이 영영 틀리지 않게 */
const RESYNC_MS = 20000;

export type NewOrder = Pick<OrderRequest, "side" | "type" | "market" | "price" | "qty">;

export interface Trading {
  ws: { state: ConnState; attempt: number };
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
  const lastClOrdId = useRef(0);
  const fillSeq = useRef(0);

  const upsertOrder = useCallback((v: OrderView) => {
    setOrders((prev) => [v, ...prev.filter((o) => o.orderId !== v.orderId)].sort(byNewest));
  }, []);

  const refresh = useCallback(() => {
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

  const submit = useCallback(
    async (o: NewOrder) => {
      // 같은 밀리초에 두 번 눌러도 번호가 겹치지 않게
      const now = Date.now() % 1_000_000_000;
      lastClOrdId.current = Math.max(now, lastClOrdId.current + 1);
      const res = await submitOrder({
        ...o,
        account: ACCOUNT,
        symbol: SYMBOL,
        clOrdId: lastClOrdId.current,
      });
      if (res.outcome === "ACCEPTED") {
        const v = await fetchOrder(res.orderId).catch(() => null);
        if (v) upsertOrder(v);
        void fetchBalance().then(setBalance).catch(() => undefined);
      }
      return res;
    },
    [upsertOrder],
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
