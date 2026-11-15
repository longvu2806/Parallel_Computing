"use strict";

/* Giao diện thử nghiệm: đọc ui/programs.json qua server, dựng tab và thẻ chương trình,
   gửi yêu cầu chạy tới /api/run. Mọi văn bản từ chương trình được gắn bằng textContent. */

const DEFAULT_THREADS = [1, 2, 4, 6, 12];
const MAX_TEXT = 5 * 1024 * 1024;
const NUM_RE = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;
const RANDOM_LIMIT = { dot: 100000, matmul: 300 };
const EXAMPLES = {
  dot: "dot 4\n1 2 3 4\n5 6 7 8\n",
  matmul: "matmul 2\n1 2\n3 4\n5 6\n7 8\n",
};
const SOURCES = [
  ["seed", "Sinh theo seed"],
  ["sample", "File mẫu"],
  ["upload", "Tải file lên"],
  ["manual", "Nhập tay"],
];

const state = { manifest: null, samples: [], active: null, view: {}, busy: false, nextRow: 1 };
const $ = (id) => document.getElementById(id);

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") node.className = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return node;
}

async function api(path, options) {
  const res = await fetch(path, options);
  let body;
  try {
    body = await res.json();
  } catch {
    body = { error: `Phản hồi không hợp lệ (HTTP ${res.status}).` };
  }
  if (!res.ok) throw new Error(body.error || `HTTP ${res.status}`);
  return body;
}

function setStatus(text, kind = "") {
  const box = $("busy");
  box.textContent = text;
  box.className = kind ? `msg ${kind}` : "muted";
}

/* ------------------------------------------------------------ dữ liệu */

function parseCommon(text, problem) {
  const lines = text.trim().split(/\r?\n/);
  const m = /^\s*(dot|matmul)\s+(\d+)\s*$/.exec(lines[0] || "");
  if (!m) throw new Error("Dòng đầu phải là 'dot <n>' hoặc 'matmul <n>'.");
  if (m[1] !== problem) throw new Error(`Dữ liệu thuộc bài '${m[1]}', tab hiện tại là '${problem}'.`);
  const n = Number(m[2]);
  if (n < 1) throw new Error("n phải lớn hơn 0.");
  const tokens = lines.slice(1).join(" ").split(/\s+/).filter(Boolean);
  const need = problem === "dot" ? 2 * n : 2 * n * n;
  if (tokens.length !== need) throw new Error(`Cần đúng ${need} số sau dòng đầu, đang có ${tokens.length}.`);
  const bad = tokens.findIndex((t) => !NUM_RE.test(t));
  if (bad >= 0) throw new Error(`Giá trị thứ ${bad + 1} không phải số: "${tokens[bad]}".`);
  return n;
}

function parseCsv(text) {
  const rows = text.split(/\r?\n/)
    .filter((line) => line.trim())
    .map((line) => line.split(/[,;\t ]+/).map((c) => c.trim()).filter((c) => c !== ""));
  if (rows.length && rows[0].some((c) => !NUM_RE.test(c))) rows.shift(); // bỏ dòng tiêu đề
  rows.forEach((row, i) => {
    const bad = row.find((c) => !NUM_RE.test(c));
    if (bad !== undefined) throw new Error(`Dòng dữ liệu ${i + 1}: "${bad}" không phải số.`);
  });
  if (!rows.length) throw new Error("File CSV không có dữ liệu.");
  return rows;
}

function dotFromCsv(rows) {
  let x, y;
  if (rows.length === 2 && rows[0].length === rows[1].length) [x, y] = rows;
  else if (rows.every((r) => r.length === 2)) { x = rows.map((r) => r[0]); y = rows.map((r) => r[1]); }
  else throw new Error("CSV tích vô hướng cần 2 hàng cùng độ dài (x, y) hoặc đúng 2 cột.");
  return `dot ${x.length}\n${x.join(" ")}\n${y.join(" ")}\n`;
}

function matmulFromCsv(rowsA, rowsB) {
  const n = rowsA.length;
  const square = (rows, name) => {
    if (rows.length !== n || rows.some((r) => r.length !== n)) {
      throw new Error(`${name} phải là ma trận vuông ${n}×${n}, cùng cỡ với A.`);
    }
  };
  square(rowsA, "A");
  square(rowsB, "B");
  const body = (rows) => rows.map((r) => r.join(" ")).join("\n");
  return `matmul ${n}\n${body(rowsA)}\n${body(rowsB)}\n`;
}

function randomSmall(problem, n) {
  const r = () => Math.floor(Math.random() * 19) - 9;
  const line = (k) => Array.from({ length: k }, r).join(" ");
  if (problem === "dot") return `dot ${n}\n${line(n)}\n${line(n)}\n`;
  const block = () => Array.from({ length: n }, () => line(n)).join("\n");
  return `matmul ${n}\n${block()}\n${block()}\n`;
}

function hash(text) {
  let h = 5381;
  for (let i = 0; i < text.length; i++) h = ((h << 5) + h + text.charCodeAt(i)) | 0;
  return (h >>> 0).toString(16);
}

function viewFor(problem) {
  if (!state.view[problem]) {
    const info = state.manifest.problems[problem] || {};
    state.view[problem] = {
      source: "seed", n: info.default_n || 256, seed: 42, sample: "",
      upload: null, uploadMsg: null, csvA: null, csvB: null,
      manual: EXAMPLES[problem] || "", manualMsg: null, randN: problem === "dot" ? 8 : 4,
      selected: new Set(), params: {}, results: [],
    };
  }
  return state.view[problem];
}

function dataSpec(problem) {
  const v = viewFor(problem);
  if (v.source === "seed") {
    const n = Number(v.n), seed = Number(v.seed);
    if (!Number.isInteger(n) || n < 1) throw new Error("n phải là số nguyên dương.");
    if (!Number.isInteger(seed) || seed < 0) throw new Error("seed phải là số nguyên không âm.");
    return { data: { source: "seed", n, seed }, key: `seed:${n}:${seed}`, label: `seed ${seed}, n = ${n}` };
  }
  if (v.source === "sample") {
    if (!v.sample) throw new Error("Chưa chọn file mẫu.");
    const s = state.samples.find((x) => x.name === v.sample);
    return { data: { source: "sample", sample: v.sample }, key: `sample:${v.sample}`,
             label: `mẫu ${v.sample}${s && s.n ? `, n = ${s.n}` : ""}` };
  }
  if (v.source === "upload") {
    if (!v.upload) throw new Error("Chưa tải lên file hợp lệ.");
    return { data: { source: "inline", text: v.upload.text }, key: `inline:${hash(v.upload.text)}`,
             label: `tải lên ${v.upload.label}, n = ${v.upload.n}` };
  }
  if (v.manual.length > MAX_TEXT) throw new Error("Dữ liệu quá 5 MB, hãy dùng nguồn sinh theo seed.");
  const n = parseCommon(v.manual, problem);
  return { data: { source: "inline", text: v.manual }, key: `inline:${hash(v.manual)}`, label: `nhập tay, n = ${n}` };
}

/* ------------------------------------------------------------ hiển thị: tab và dữ liệu */

function renderAll() {
  renderTabs();
  renderData();
  renderPrograms();
  renderResults();
}

function renderInfo() {
  const m = state.manifest;
  $("server-info").textContent = `Thư mục chương trình: ${m.build_dir}` +
    (m.msys2_bin ? ` · MSYS2: ${m.msys2_bin}` : " · Không thấy thư mục MSYS2, chương trình OpenMP có thể báo thiếu DLL");
}

function renderTabs() {
  $("tabs").replaceChildren(...Object.entries(state.manifest.problems).map(([key, info]) =>
    el("button", {
      type: "button", role: "tab", "aria-selected": String(key === state.active),
      onclick: () => { state.active = key; setStatus(""); renderAll(); },
    }, info.label || key)));
}

function msgLine(msg) {
  return msg ? el("p", {}, el("span", { class: `msg ${msg.ok ? "ok" : "bad"}` }, msg.text)) : null;
}

function numInput(value, min, onChange, label) {
  return el("input", { type: "number", min, value, "aria-label": label, oninput: (e) => onChange(e.target.value) });
}

function renderData() {
  const p = state.active;
  const v = viewFor(p);
  const radios = el("div", { class: "sources", role: "radiogroup", "aria-label": "Nguồn dữ liệu" },
    SOURCES.map(([key, label]) => el("label", {},
      el("input", { type: "radio", name: "source", value: key, checked: v.source === key,
                    onchange: () => { v.source = key; renderData(); } }),
      label)));
  $("data-panel").replaceChildren(radios, sourceBody(p, v));
}

function sourceBody(p, v) {
  if (v.source === "seed") {
    return el("div", {},
      el("div", { class: "row" },
        el("label", {}, "n ", numInput(v.n, 1, (x) => { v.n = x; }, "n")),
        el("label", {}, "seed ", numInput(v.seed, 0, (x) => { v.seed = x; }, "seed"))),
      el("p", { class: "hint" }, "Chương trình tự sinh dữ liệu theo seed (tham số --n, --seed). Dùng cho kích thước lớn."));
  }

  if (v.source === "sample") {
    const list = state.samples.filter((s) => s.problem === p);
    if (!list.length) {
      return el("p", { class: "empty" }, "Chưa có file mẫu cho bài này trong data/sample/.");
    }
    if (!list.some((s) => s.name === v.sample)) v.sample = list[0].name;
    return el("div", { class: "row" },
      el("select", { "aria-label": "File mẫu", onchange: (e) => { v.sample = e.target.value; } },
        list.map((s) => el("option", { value: s.name, selected: s.name === v.sample }, `${s.name} (n = ${s.n})`))),
      el("button", { type: "button", onclick: () => openSample(v) }, "Mở trong ô nhập tay"));
  }

  if (v.source === "upload") {
    const txtInput = el("label", {}, "File .txt (định dạng chung) ",
      el("input", { type: "file", accept: ".txt", onchange: (e) => loadUpload(p, v, e.target.files[0], "txt") }));
    const csvInputs = p === "dot"
      ? el("label", {}, "hoặc file .csv (2 hàng x, y hoặc 2 cột) ",
          el("input", { type: "file", accept: ".csv", onchange: (e) => loadUpload(p, v, e.target.files[0], "dotcsv") }))
      : el("span", { class: "row" },
          el("label", {}, "hoặc A.csv ", el("input", { type: "file", accept: ".csv", onchange: (e) => loadUpload(p, v, e.target.files[0], "csvA") })),
          el("label", {}, "và B.csv ", el("input", { type: "file", accept: ".csv", onchange: (e) => loadUpload(p, v, e.target.files[0], "csvB") })));
    return el("div", {}, el("div", { class: "row" }, txtInput), el("div", { class: "row" }, csvInputs), msgLine(v.uploadMsg));
  }

  const area = el("textarea", { spellcheck: "false", "aria-label": "Dữ liệu nhập tay",
                                oninput: (e) => { v.manual = e.target.value; } }, v.manual);
  return el("div", {}, area,
    el("div", { class: "row" },
      el("label", {}, "n ", numInput(v.randN, 1, (x) => { v.randN = x; }, "n cho dữ liệu ngẫu nhiên")),
      el("button", { type: "button", onclick: () => {
        const n = Number(v.randN);
        if (!Number.isInteger(n) || n < 1 || n > RANDOM_LIMIT[p]) {
          v.manualMsg = { ok: false, text: `n phải trong [1, ${RANDOM_LIMIT[p]}]; lớn hơn thì dùng nguồn sinh theo seed.` };
        } else {
          v.manual = randomSmall(p, n);
          v.manualMsg = { ok: true, text: `Đã sinh số nguyên trong [-9, 9], n = ${n}. Kết quả tính được là chính xác tuyệt đối.` };
        }
        renderData();
      } }, "Sinh số nguyên nhỏ ngẫu nhiên"),
      el("button", { type: "button", onclick: () => {
        try { v.manualMsg = { ok: true, text: `Hợp lệ, n = ${parseCommon(v.manual, p)}.` }; }
        catch (e) { v.manualMsg = { ok: false, text: e.message }; }
        renderData();
      } }, "Kiểm tra định dạng"),
      el("button", { type: "button", onclick: () => { v.manual = EXAMPLES[p]; v.manualMsg = null; renderData(); } }, "Khôi phục ví dụ")),
    msgLine(v.manualMsg),
    el("p", { class: "hint" }, p === "dot"
      ? "Định dạng: dòng 'dot n', rồi một dòng n số của x, rồi một dòng n số của y."
      : "Định dạng: dòng 'matmul n', rồi n dòng của A, rồi n dòng của B."));
}

async function openSample(v) {
  try {
    const res = await fetch(`/api/samples/${encodeURIComponent(v.sample)}`);
    if (!res.ok) throw new Error((await res.json()).error);
    v.manual = await res.text();
    v.manualMsg = { ok: true, text: `Đã mở ${v.sample}.` };
    v.source = "manual";
  } catch (e) {
    setStatus(`Không mở được file mẫu: ${e.message}`, "bad");
  }
  renderData();
}

async function loadUpload(p, v, file, kind) {
  if (!file) return;
  try {
    if (file.size > MAX_TEXT) throw new Error("File quá 5 MB, hãy dùng nguồn sinh theo seed.");
    const text = await file.text();
    let common = null;
    let label = file.name;
    if (kind === "txt") common = text;
    else if (kind === "dotcsv") common = dotFromCsv(parseCsv(text));
    else {
      v[kind] = { rows: parseCsv(text), name: file.name };
      if (v.csvA && v.csvB) {
        common = matmulFromCsv(v.csvA.rows, v.csvB.rows);
        label = `${v.csvA.name} + ${v.csvB.name}`;
      } else {
        v.upload = null;
        v.uploadMsg = { ok: true, text: `Đã đọc ${file.name}; cần thêm ${kind === "csvA" ? "B.csv" : "A.csv"}.` };
      }
    }
    if (common !== null) {
      const n = parseCommon(common, p);
      v.upload = { text: common, n, label };
      v.uploadMsg = { ok: true, text: `${label}: hợp lệ, n = ${n}.` };
    }
  } catch (e) {
    v.upload = null;
    v.uploadMsg = { ok: false, text: `${file.name}: ${e.message}` };
  }
  renderData();
}

/* ------------------------------------------------------------ hiển thị: chương trình */

function defaultParams(prog) {
  const all = {
    b: 64,
    threads: prog.threads ? (prog.threads.includes(4) ? 4 : prog.threads[0]) : 4,
    variant: prog.variants ? prog.variants[0] : undefined,
    schedule: "static",
    pin: false,
  };
  return Object.fromEntries(prog.params.map((k) => [k, all[k]]));
}

function paramControl(prog, key, params) {
  const select = (options, label) => el("label", {}, label,
    el("select", { onchange: (e) => { params[key] = key === "threads" ? Number(e.target.value) : e.target.value; } },
      options.map((o) => el("option", { value: o, selected: String(params[key]) === String(o) }, o))));
  switch (key) {
    case "b":
      return el("label", {}, "block b",
        el("input", { type: "number", min: 1, value: params.b, oninput: (e) => { params.b = e.target.value; } }));
    case "threads":
      if (prog.threads) return select(prog.threads, "threads");
      return el("label", {}, "threads",
        el("input", { type: "number", min: 1, max: 1024, list: "threads-list", value: params.threads,
                      oninput: (e) => { params.threads = e.target.value; } }));
    case "variant":
      return select(prog.variants, "variant");
    case "schedule":
      return select(["static", "dynamic"], "schedule");
    case "pin":
      return el("label", { class: "check" },
        el("input", { type: "checkbox", checked: params.pin, onchange: (e) => { params.pin = e.target.checked; } }),
        "ghim thread");
    default:
      return null;
  }
}

function programCard(prog) {
  const v = viewFor(state.active);
  if (!v.params[prog.id]) v.params[prog.id] = defaultParams(prog);
  const params = v.params[prog.id];
  const controls = prog.params.map((k) => paramControl(prog, k, params)).filter(Boolean);
  return el("div", { class: "card" },
    el("div", { class: "card-head" },
      el("label", { class: "check" },
        el("input", { type: "checkbox", checked: v.selected.has(prog.id), disabled: !prog.built,
                      "aria-label": `Chọn ${prog.label} để so sánh`,
                      onchange: (e) => { if (e.target.checked) v.selected.add(prog.id); else v.selected.delete(prog.id); } }),
        el("span", {}, el("span", { class: "card-title" }, prog.label), el("br"),
          el("span", { class: "card-meta" }, `${prog.id} · ${prog.owner || "chưa rõ"}`))),
      el("span", { class: `badge ${prog.built ? "built" : "missing"}`,
                   title: prog.built ? prog.exe : "Chạy make trong MSYS2 UCRT64 để build" },
        prog.built ? "đã build" : "chưa build")),
    controls.length ? el("div", { class: "params" }, controls) : el("p", { class: "hint" }, "Không có tham số riêng."),
    el("div", { class: "card-foot" },
      el("span", { class: "card-meta" }, prog.built ? "" : "Chạy make trong MSYS2 UCRT64"),
      el("button", { type: "button", class: "primary", disabled: !prog.built || state.busy,
                     onclick: () => runBatch([prog.id]) }, "Chạy")));
}

function renderPrograms() {
  const progs = state.manifest.programs.filter((x) => x.problem === state.active);
  $("programs-panel").replaceChildren(...[["seq", "Tuần tự"], ["par", "Song song"]].map(([kind, title]) => {
    const list = progs.filter((x) => x.kind === kind);
    return el("div", {}, el("h3", {}, title),
      list.length ? list.map(programCard) : el("p", { class: "empty" }, "Chưa khai báo chương trình nào."));
  }));
  $("run-selected").disabled = state.busy;
}

/* ------------------------------------------------------------ chạy */

function payloadParams(prog, params) {
  const out = {};
  for (const key of prog.params) {
    const value = params[key];
    if (value === undefined || value === "") continue;
    out[key] = key === "b" || key === "threads" ? Number(value) : value;
  }
  return out;
}

async function runBatch(ids) {
  if (state.busy) return;
  const p = state.active;
  const v = viewFor(p);
  const progs = ids.map((id) => state.manifest.programs.find((x) => x.id === id)).filter((x) => x && x.built);
  if (!progs.length) { setStatus("Chưa chọn bản nào đã build.", "bad"); return; }
  progs.sort((a, b) => (a.kind === b.kind ? 0 : a.kind === "seq" ? -1 : 1)); // chạy tuần tự trước để có mốc so sánh
  let spec;
  try { spec = dataSpec(p); } catch (e) { setStatus(e.message, "bad"); return; }

  state.busy = true;
  renderPrograms();
  try {
    for (const [i, prog] of progs.entries()) {
      setStatus(`Đang chạy ${prog.id} (${i + 1}/${progs.length})…`);
      const payload = { program: prog.id, params: payloadParams(prog, v.params[prog.id] || defaultParams(prog)),
                        data: spec.data, verify: $("verify").checked };
      const row = { no: state.nextRow++, prog, params: payload.params, dataKey: spec.key, dataLabel: spec.label,
                    verify: payload.verify, res: null, err: null, open: false };
      try {
        row.res = await api("/api/run", { method: "POST", headers: { "Content-Type": "application/json" },
                                          body: JSON.stringify(payload) });
      } catch (e) {
        row.err = e.message;
      }
      v.results.unshift(row);
      if (state.active === p) renderResults();
    }
    setStatus(`Xong ${progs.length} lần chạy.`, "ok");
  } finally {
    state.busy = false;
    renderPrograms();
  }
}

/* ------------------------------------------------------------ kết quả */

function num(x) {
  const v = Number.parseFloat(x);
  return Number.isFinite(v) ? v : null;
}

function fmtSec(x) {
  const v = num(x);
  if (v === null) return "—";
  if (v < 1e-3) return `${(v * 1e6).toFixed(1)} µs`;
  if (v < 1) return `${(v * 1e3).toFixed(2)} ms`;
  return `${v.toFixed(3)} s`;
}

function metric(row, key) {
  return row.res && row.res.metrics ? row.res.metrics[key] : undefined;
}

// Mốc T1 = bản tuần tự nhanh nhất đã chạy trên cùng dữ liệu (đúng định nghĩa speedup trong báo cáo)
function baselineFor(row, rows) {
  let best = null;
  for (const r of rows) {
    if (r.dataKey !== row.dataKey || r.prog.kind !== "seq" || !r.res || !r.res.ok) continue;
    const t = num(metric(r, "t_compute"));
    if (t > 0 && (best === null || t < num(metric(best, "t_compute")))) best = r;
  }
  return best;
}

function sameCheck(a, b) {
  const x = num(a), y = num(b);
  if (x === null || y === null) return true;
  return Math.abs(x - y) <= 1e-9 * Math.max(1, Math.abs(x), Math.abs(y));
}

function statusCell(row) {
  if (row.err) return el("span", { class: "status-bad" }, `Lỗi: ${row.err}`);
  if (!row.res.ok) return el("span", { class: "status-bad" }, row.res.error || `Mã thoát ${row.res.returncode}`);
  if (row.res.warnings && row.res.warnings.length) return el("span", { class: "status-warn" }, "Xong, có cảnh báo");
  return el("span", { class: "status-ok" }, row.verify ? "OK, đã kiểm tra" : "OK");
}

function previewBlock(res) {
  const p = res.result_preview;
  if (!p) return null;
  if (p.type === "scalar") return el("div", {}, el("strong", {}, "Kết quả: "), p.value ?? "—");
  if (p.type === "matrix" && p.rows) {
    return el("div", {}, el("strong", {}, `Ma trận kết quả ${p.n}×${p.n}`),
      el("table", { class: "matrix" }, el("tbody", {}, p.rows.map((r) => el("tr", {}, r.map((c) => el("td", {}, c)))))));
  }
  if (p.type === "matrix") return el("div", {}, `Ma trận kết quả ${p.n}×${p.n}, quá lớn để xem trước; tải output.txt để xem.`);
  return el("div", {}, el("strong", {}, "Dòng đầu file kết quả: "), el("code", {}, p.head));
}

function copyText(text) {
  if (navigator.clipboard) {
    navigator.clipboard.writeText(text).then(() => setStatus("Đã copy lệnh.", "ok"),
                                             () => setStatus("Không copy được, hãy chọn và copy tay.", "bad"));
  } else {
    setStatus("Trình duyệt không cho copy tự động, hãy chọn và copy tay.", "bad");
  }
}

function detailRow(row, span) {
  const res = row.res;
  const content = row.err ? [el("div", { class: "msg bad" }, row.err)] : [
    el("div", {}, el("strong", {}, "Lệnh tương đương "),
      el("button", { type: "button", class: "small", onclick: () => copyText(res.cmd) }, "Copy"),
      el("pre", {}, res.cmd)),
    res.error ? el("div", { class: "msg bad" }, res.error) : null,
    res.warnings && res.warnings.length ? el("ul", {}, res.warnings.map((w) => el("li", {}, w))) : null,
    previewBlock(res),
    res.output_available ? el("a", { href: `/api/output/${res.run_id}`, download: "" }, "Tải output.txt") : null,
    el("div", {}, el("strong", {}, "Thời gian đo ở server (gồm khởi động chương trình): "), fmtSec(res.wall)),
    res.metrics ? el("details", {}, el("summary", {}, "Dòng CSV"), el("pre", {}, JSON.stringify(res.metrics, null, 2))) : null,
    el("details", { open: Boolean(res.stderr) }, el("summary", {}, "stdout / stderr"),
      el("pre", {}, `--- stdout ---\n${res.stdout || "(trống)"}\n--- stderr ---\n${res.stderr || "(trống)"}`)),
  ];
  return el("tr", { class: "detail" }, el("td", { colspan: span }, el("div", { class: "detail-box" }, content)));
}

function renderResults() {
  const v = viewFor(state.active);
  const box = $("results-panel");
  if (!v.results.length) {
    box.replaceChildren(el("p", { class: "empty" }, "Chưa có lần chạy nào."));
    return;
  }
  const head = ["#", "Chương trình", "Tham số", "Dữ liệu", "t_init", "t_compute", "t_io", "t_total", "check", "Tăng tốc*", "Trạng thái", ""];
  const tbody = el("tbody");
  for (const row of v.results) {
    const base = baselineFor(row, v.results);
    const tc = num(metric(row, "t_compute"));
    const ratio = base && tc > 0 ? `${(num(metric(base, "t_compute")) / tc).toFixed(2)}×` : "—";
    const check = metric(row, "check");
    const mismatch = base && base !== row && !sameCheck(check, metric(base, "check"));
    const paramText = Object.entries(row.params)
      .map(([k, val]) => (k === "pin" ? (val ? "ghim" : "không ghim") : `${k}=${val}`)).join(", ") || "—";
    tbody.append(el("tr", {},
      el("td", { class: "num" }, row.no),
      el("td", {}, row.prog.label, el("br"), el("span", { class: "card-meta" }, row.prog.id)),
      el("td", {}, paramText),
      el("td", {}, row.dataLabel),
      ...["t_init", "t_compute", "t_io", "t_total"].map((k) => el("td", { class: "num" }, fmtSec(metric(row, k)))),
      el("td", { class: "num" }, check === undefined ? "—" : mismatch
        ? el("span", { class: "mismatch", title: "Lệch so với bản tuần tự mốc trên cùng dữ liệu" }, `${check} ≠`) : check),
      el("td", { class: "num" }, ratio),
      el("td", {}, statusCell(row)),
      el("td", {}, el("button", { type: "button", class: "small", "aria-expanded": String(row.open),
                                  onclick: () => { row.open = !row.open; renderResults(); } }, row.open ? "Ẩn" : "Chi tiết"))));
    if (row.open) tbody.append(detailRow(row, head.length));
  }
  box.replaceChildren(
    el("div", { class: "table-wrap" },
      el("table", {}, el("thead", {}, el("tr", {}, head.map((h) => el("th", { scope: "col" }, h)))), tbody)),
    el("p", { class: "hint" },
      "* Tăng tốc = t_compute của bản tuần tự nhanh nhất đã chạy trên cùng dữ liệu chia cho t_compute của hàng này. " +
      "Chỉ để tham khảo, không dùng cho báo cáo."));
}

/* ------------------------------------------------------------ khởi động */

async function refresh() {
  if (state.busy || !state.manifest) return;
  try {
    const [programs, samples] = await Promise.all([api("/api/programs"), api("/api/samples")]);
    state.manifest = programs;
    state.samples = samples.samples;
    renderPrograms();
    if (viewFor(state.active).source === "sample") renderData();
  } catch {
    /* server tạm thời không phản hồi: giữ nguyên giao diện */
  }
}

async function init() {
  try {
    const [programs, samples] = await Promise.all([api("/api/programs"), api("/api/samples")]);
    state.manifest = programs;
    state.samples = samples.samples;
  } catch (e) {
    $("main").replaceChildren(el("p", { class: "msg bad" }, `Không tải được cấu hình từ server: ${e.message}`));
    return;
  }
  $("threads-list").append(...DEFAULT_THREADS.map((t) => el("option", { value: t })));
  state.active = Object.keys(state.manifest.problems)[0];
  $("run-selected").addEventListener("click", () => runBatch([...viewFor(state.active).selected]));
  $("clear-results").addEventListener("click", () => { viewFor(state.active).results = []; renderResults(); });
  window.addEventListener("focus", refresh); // build xong quay lại trình duyệt là thấy nhãn "đã build"
  renderInfo();
  renderAll();
}

init();
