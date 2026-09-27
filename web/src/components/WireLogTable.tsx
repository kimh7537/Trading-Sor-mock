import { useMemo, useState } from "react";
import type { WireFieldView, WireFrame, WireHeaderView } from "../lib/types";

/**
 * 오간 전문 **전체 내역** (점검). 한 줄을 누르면 그 전문에 실린 **필드 값 전부**가 펴진다.
 *
 * <p>전문 이름과 길이만 보이면 "고정 길이로 주고받는다"는 사실까지다. 무슨 값이 실렸는지는
 * 필드를 봐야 안다 — 계좌·종목·가격·수량이 어느 자리에 몇 바이트로 놓였는지가 이 프로젝트의
 * 통신 규격 그 자체다.
 *
 * <p>필드 이름·타입·위치·길이는 <b>채널계가 전문 선언을 그대로 되짚어</b> 보낸 것이다.
 * 화면이 따로 적어 두지 않으므로 전문이 바뀌면 이 표도 같이 바뀐다.
 *
 * <p>종별로 거를 수 있다. 주기 조회(호가·상세)가 수를 채우므로 주문만 보고 싶을 때가 있다.
 */
export function WireLogTable({ frames }: { frames: WireFrame[] }) {
  const [open, setOpen] = useState<number | null>(null);
  const [only, setOnly] = useState<string>("");

  const kinds = useMemo(() => {
    const seen = new Map<string, number>();
    for (const f of frames) seen.set(f.sent, (seen.get(f.sent) ?? 0) + 1);
    return [...seen.entries()].sort((a, b) => b[1] - a[1]);
  }, [frames]);

  const shown = useMemo(
    () => (only ? frames.filter((f) => f.sent === only) : frames),
    [frames, only],
  );

  if (frames.length === 0) {
    return (
      <p className="note">
        아직 오간 전문이 없다. 채널계가 원장에 무엇이든 물으면(호가 조회만 해도) 여기 쌓인다.
      </p>
    );
  }

  return (
    <div className="wirelog">
      <div className="wirelog-filter" role="group" aria-label="전문 종별로 거르기">
        <button type="button" aria-pressed={only === ""} onClick={() => setOnly("")}>
          전체 <span className="num">{frames.length}</span>
        </button>
        {kinds.map(([k, c]) => (
          <button type="button" key={k} aria-pressed={only === k} onClick={() => setOnly(k)}>
            {k} <span className="num">{c}</span>
          </button>
        ))}
      </div>

      <div className="wirelog-scroll">
        <table>
          <thead>
            <tr>
              <th scope="col">시각</th>
              <th scope="col">보낸 전문</th>
              <th scope="col">받은 전문</th>
              <th scope="col" className="r">
                seq
              </th>
              <th scope="col" className="r">
                길이
              </th>
              <th scope="col" className="r">
                왕복
              </th>
            </tr>
          </thead>
          <tbody>
            {[...shown].reverse().map((f) => (
              <FrameRow
                key={f.id}
                f={f}
                open={open === f.id}
                onToggle={() => setOpen(open === f.id ? null : f.id)}
              />
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

function FrameRow({ f, open, onToggle }: { f: WireFrame; open: boolean; onToggle: () => void }) {
  return (
    <>
      <tr className={`wirelog-row${open ? " open" : ""}${f.ok ? "" : " bad"}`}>
        <td>
          <button
            type="button"
            className="wirelog-open"
            aria-expanded={open}
            aria-label={`${f.sent} ${f.at} 필드 ${open ? "접기" : "펼치기"}`}
            onClick={onToggle}
          >
            {open ? "▾" : "▸"} <span className="num">{f.at}</span>
          </button>
        </td>
        <td>
          <b>{f.sent}</b> <span className="muted num">#{f.sentType}</span>
        </td>
        <td>
          {f.ok ? (
            <>
              <b>{f.got}</b> <span className="muted num">#{f.gotType}</span>
            </>
          ) : (
            <span className="tag danger">답 못 받음</span>
          )}
        </td>
        <td className="num r">{f.seq}</td>
        <td className="num r">
          {f.sentBytes}
          {f.ok ? ` / ${f.gotBytes}` : ""}B
        </td>
        <td className="num r">
          {f.micros >= 1000 ? `${(f.micros / 1000).toFixed(2)}ms` : `${f.micros}µs`}
        </td>
      </tr>
      {open && (
        <tr className="wirelog-detail">
          <td colSpan={6}>
            <div className="wirelog-bodies">
              <FieldTable
                title={`보냄 — ${f.sent}`}
                bytes={f.sentBytes}
                fields={f.sentFields}
                hex={f.sentHex}
                header={f.sentHeader}
              />
              {f.ok ? (
                <FieldTable
                  title={`받음 — ${f.got}`}
                  bytes={f.gotBytes}
                  fields={f.gotFields}
                  hex={f.gotHex}
                  header={f.gotHeader}
                />
              ) : (
                <p className="note">
                  <b>답을 받지 못했다.</b> 보낸 것은 옆에 그대로 있고, 받은 것이 없어 적을 것이
                  없다. 이때 주문은 <b>닿았는지 모른다</b> — 다시 보내면 중복이 될 수 있어
                  채널계가 스스로 재시도하지 않는다.
                </p>
              )}
            </div>
          </td>
        </tr>
      )}
    </>
  );
}

/** 전문 하나의 바디를 필드 단위로. 위치와 길이를 같이 적어 고정 길이 배치가 보이게 한다. */
function FieldTable({
  title,
  bytes,
  fields,
  hex,
  header,
}: {
  title: string;
  bytes: number;
  fields: WireFieldView[];
  hex: string;
  header: WireHeaderView | null;
}) {
  const body = fields.reduce((s, f) => s + f.size, 0);
  const headLen = bytes - body;
  return (
    <div className="fieldtable">
      <h4>
        {title}{" "}
        <span className="muted num">
          헤더 {headLen}B + 바디 {body}B
        </span>
      </h4>
      {header && (
        <dl className="hdr">
          <div>
            <dt>version</dt>
            <dd className="num">{header.version}</dd>
          </div>
          <div>
            <dt>type</dt>
            <dd className="num">{header.type}</dd>
          </div>
          <div>
            <dt>bodyLen</dt>
            <dd className="num">{header.bodyLen}</dd>
          </div>
          <div>
            <dt>seq</dt>
            <dd className="num">{header.seq}</dd>
          </div>
          <div>
            <dt>ts</dt>
            <dd className="num">{header.ts}</dd>
          </div>
        </dl>
      )}
      <table>
        <thead>
          <tr>
            <th scope="col" className="r">
              위치
            </th>
            <th scope="col">필드</th>
            <th scope="col">타입</th>
            <th scope="col" className="r">
              길이
            </th>
            <th scope="col">값</th>
          </tr>
        </thead>
        <tbody>
          {fields.map((x) => (
            <tr key={x.name}>
              <td className="num r muted">{x.offset}</td>
              <td>{x.name}</td>
              <td className="muted">{x.type}</td>
              <td className="num r muted">{x.size}B</td>
              <td className="num val">{x.value === "" ? "—" : x.value}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {hex && <HexDump hex={hex} headLen={headLen} />}
    </div>
  );
}

/**
 * **선에 실제로 흐른 바이트.** 16바이트씩 끊어 왼쪽에 위치를 적는다.
 *
 * <p>필드 표가 "무슨 값이 실렸나"라면 이쪽은 "그래서 무엇이 나갔나"다. 헤더 몫은 흐리게
 * 두어 어디까지가 머리이고 어디부터가 바디인지 눈으로 갈린다. 값이 빅엔디언으로 놓인 것도
 * 여기서 보인다 — 이를테면 수량 120은 {@code 00 00 00 78}이다.
 */
function HexDump({ hex, headLen }: { hex: string; headLen: number }) {
  const bytes: string[] = [];
  for (let i = 0; i + 1 < hex.length; i += 2) bytes.push(hex.slice(i, i + 2));
  const lines: string[][] = [];
  for (let i = 0; i < bytes.length; i += 16) lines.push(bytes.slice(i, i + 16));

  return (
    <div className="hexdump">
      <h5>
        바이트 그대로 <span className="muted">— 앞 {headLen}B가 헤더</span>
      </h5>
      <pre aria-label="전문 바이트 16진수">
        {lines.map((row, li) => (
          <div key={li}>
            <span className="off">{(li * 16).toString(16).padStart(4, "0")}</span>
            {row.map((b, bi) => {
              const at = li * 16 + bi;
              return (
                <span key={bi} className={at < headLen ? "hb head" : "hb"}>
                  {b}
                </span>
              );
            })}
          </div>
        ))}
      </pre>
    </div>
  );
}
