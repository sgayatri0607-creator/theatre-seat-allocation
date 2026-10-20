const $ = (id) => document.getElementById(id);
const state = { data: null, selected: new Set(), viewRow: "A", fresh: new Set() };

async function api(path, method = "GET", body) {
  const res = await fetch(`/api${path}`, {
    method,
    headers: { "Content-Type": "application/json" },
    body: body ? JSON.stringify(body) : undefined,
  });
  const json = await res.json();
  if (!res.ok) throw new Error(json.error || "Something went wrong");
  return json;
}

function say(text, kind = "ok") {
  const el = $("message");
  el.textContent = text;
  el.className = `message ${kind}`;
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function renderSeatMap() {
  const map = $("seatmap");
  map.replaceChildren();
  for (const row of state.data.rows) {
    const line = el("div", "seat-row");
    line.appendChild(el("span", "row-label", row.label));
    row.seats.forEach((seat, i) => {
      if (i === row.seats.length / 2) line.appendChild(el("span", "aisle"));
      const booked = seat.bookingId !== null;
      const selected = state.selected.has(seat.label);
      const btn = el("button", `seat ${booked ? "booked" : selected ? "selected" : "free"}`, seat.seatNo);
      btn.type = "button";
      btn.disabled = booked;
      btn.setAttribute("aria-label", `Seat ${seat.label}, ${booked ? "booked" : selected ? "selected" : "free"}`);
      if (state.fresh.has(seat.label)) btn.classList.add("fresh");
      btn.onclick = () => toggleSeat(seat.label);
      line.appendChild(btn);
    });
    map.appendChild(line);
  }
  const s = state.data.stats;
  $("counts").textContent = `${s.free} of ${s.total} seats free`;
}

function renderSelection() {
  const labels = [...state.selected].sort();
  $("selection").textContent = labels.length ? `Selected: ${labels.join(", ")}` : "No seats selected. Click seats on the map.";
  $("btn-book").disabled = labels.length === 0;
}

function renderBookings() {
  const list = $("bookings");
  list.replaceChildren();
  if (!state.data.bookings.length) {
    list.appendChild(el("li", "empty", "No bookings yet."));
    return;
  }
  for (const b of state.data.bookings) {
    const li = el("li");
    const info = el("div");
    info.appendChild(el("div", "who", `${b.name} (#${b.id})`));
    info.appendChild(el("div", "where", b.seats.join(", ")));
    const cancel = el("button", "", "Cancel");
    cancel.onclick = () => run(() => api(`/booking/${b.id}`, "DELETE"), `Cancelled booking #${b.id}`);
    li.append(info, cancel);
    list.appendChild(li);
  }
}

function renderRowTabs() {
  const tabs = $("row-tabs");
  tabs.replaceChildren();
  for (const row of state.data.rows) {
    const b = el("button", "", `Row ${row.label}`);
    b.setAttribute("role", "tab");
    b.setAttribute("aria-selected", String(row.label === state.viewRow));
    b.onclick = () => { state.viewRow = row.label; renderRowTabs(); renderChain(); };
    tabs.appendChild(b);
  }
}

function renderChain() {
  const chain = $("chain");
  chain.replaceChildren();
  const row = state.data.rows.find((r) => r.label === state.viewRow);
  row.seats.forEach((seat, i) => {
    const node = el("div", `node ${seat.bookingId !== null ? "booked" : ""}`);
    if (i === 0) node.appendChild(el("span", "tag", "head"));
    if (i === row.seats.length - 1) node.appendChild(el("span", "tag", "tail"));
    const data = el("div", "data");
    data.appendChild(el("b", "", seat.label));
    data.appendChild(el("small", "", seat.bookingId !== null ? `booking #${seat.bookingId}` : "free"));
    node.append(data, el("div", "next", "next"));
    chain.appendChild(node);
    chain.appendChild(el("span", "arrow"));
  });
  chain.appendChild(el("span", "null", "null"));
}

function render() {
  renderSeatMap();
  renderSelection();
  renderBookings();
  renderRowTabs();
  renderChain();
}

function toggleSeat(label) {
  state.selected.has(label) ? state.selected.delete(label) : state.selected.add(label);
  renderSeatMap();
  renderSelection();
}

async function run(action, okText) {
  try {
    const result = await action();
    state.data = result;
    // drop selections that are no longer free
    for (const label of [...state.selected]) {
      const seat = state.data.rows.flatMap((r) => r.seats).find((s) => s.label === label);
      if (!seat || seat.bookingId !== null) state.selected.delete(label);
    }
    if (okText) say(okText, "ok");
    render();
    return result;
  } catch (err) {
    say(err.message, "error");
  }
}

$("btn-book").onclick = async () => {
  const seats = [...state.selected];
  const name = $("name").value;
  const result = await run(() => api("/book/seats", "POST", { name, seats }));
  if (result) {
    state.selected.clear();
    state.fresh = new Set(seats);
    say(`Booked ${seats.sort().join(", ")} for ${name.trim()}`, "ok");
    render();
  }
};

$("btn-auto").onclick = async () => {
  const name = $("name").value;
  const count = Number($("count").value);
  const result = await run(() => api("/book/auto", "POST", { name, count }));
  if (result) {
    const { last } = result;
    state.fresh = new Set(last.seats);
    say(
      last.split
        ? `No block of ${count} adjacent seats was free, so the group was split: ${last.seats.join(", ")}`
        : `Seated together: ${last.seats.join(", ")}`,
      "ok"
    );
    state.viewRow = last.seats[0][0];
    render();
  }
};

$("btn-reset").onclick = () => run(() => api("/reset", "POST"), "All bookings cleared");

api("/theatre").then((d) => { state.data = d; render(); }).catch((e) => say(e.message, "error"));
