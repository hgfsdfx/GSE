"use strict";

const $ = id => document.getElementById(id);
const jobs = ["사냥꾼", "약탈자", "해커", "상인", "탐구자", "경비"];
const actions = ["휴식", "사냥", "추격", "거래", "해킹", "탐구", "회복", "이사 중"];
const colors = {npc: "#53dcd0", monster: "#fb839f", player: "#f8ca7b", text: "#8ca1b8"};
let snapshot = null, selectedId = null, session = null, lastSequence = null;
let townOptionsSignature = "";
const history = [];
const number = value => Number.isFinite(Number(value)) ? Number(value) : 0;
const fmt = value => number(value).toLocaleString("ko-KR", {maximumFractionDigits: 1});
const healthRatio = npc => Math.max(0, Math.min(1, number(npc.health) / Math.max(1, number(npc.maxHealth))));
const pointText = point => point ? `${fmt(point.x)}, ${fmt(point.y)}` : "—";
const townName = id => snapshot?.towns.find(town => town.id === id)?.name ?? "미지정";

function node(tag, className = "", text = "") {
  const result = document.createElement(tag);
  result.className = className;
  result.textContent = String(text);
  return result;
}

function option(select, value, label) {
  const item = node("option", "", label);
  item.value = String(value);
  select.append(item);
}
jobs.forEach((job, index) => option($("job-filter"), index, job));

function metric(label, value, caption, alert = false) {
  const card = node("article", `metric${alert ? " alert" : ""}`);
  card.append(node("div", "label", label), node("div", "value", value), node("small", "", caption));
  return card;
}

function renderSummary() {
  const c = snapshot.counts;
  $("metrics").replaceChildren(
    metric("전체 NPC", `${fmt(c.npcs)} / ${fmt(c.expectedNpcs)}`, `생존 ${fmt(c.aliveNpcs)}명 · 기준 인원 대비`, c.npcs !== c.expectedNpcs),
    metric("전체 몬스터", `${fmt(c.monsters)} / ${fmt(c.monsterLimit)}`, `일반 ${fmt(c.regularMonsters)} · 보스 ${fmt(c.bosses)}`, c.monsters > c.monsterLimit),
    metric("몹 보충 간격", `${fmt(c.respawnIntervalSeconds)}초`, "마을 안에서는 보충 중지"),
    metric("거래 / 약탈", `${fmt(snapshot.society.trades)} / ${fmt(snapshot.society.raids)}`, `NPC 사냥 성공 ${fmt(snapshot.society.hunts)}회`),
    metric("플레이어", snapshot.player.dead ? "사망" : `LV ${fmt(snapshot.player.level)}`, `체력 ${fmt(snapshot.player.health)} / ${fmt(snapshot.player.maxHealth)}`)
  );
  $("towns").replaceChildren(...snapshot.towns.map(town => {
    const card = node("article", "town");
    card.append(node("h3", "", town.name));
    const grid = node("div", "grid");
    const percent = number(town.residentMaxHealth) > 0 ? 100 * number(town.residentHealth) / number(town.residentMaxHealth) : 0;
    for (const [label, value] of [
      ["소속 / 생존 주민", `${fmt(town.residents)} / ${fmt(town.aliveResidents)}명`],
      ["현재 마을 내 NPC", `${fmt(town.presentNpcs)}명`],
      ["마을 자금 / 재고", `${fmt(town.treasury)} / ${fmt(town.stock)}`],
      ["소속 주민 총자산", `${fmt(town.residentCredits)} 크레딧`],
      ["주민 부품 / 깨달음", `${fmt(town.residentCargo)} / ${fmt(town.residentInsight)}`],
      ["주민 체력 합계 비율", `${fmt(percent)}%`],
      ["빈집 / 전체 주택", `${fmt(town.vacantHouses)} / ${fmt(town.housingCapacity)}`],
      ["경제 / 안전 / 탐구", `${fmt(town.prosperity)} / ${fmt(town.safety)} / ${fmt(town.learning)}`]
    ]) {
      const cell = node("div");
      cell.append(node("span", "label", label), node("strong", "", value));
      grid.append(cell);
    }
    const jobText = jobs.map((job, i) => `${job} ${fmt(town.jobs?.[i])}`).join(" · ");
    const actionText = actions.map((action, i) => `${action} ${fmt(town.actions?.[i])}`).join(" · ");
    card.append(grid, node("div", "summary", `${jobText}\n${actionText}`));
    return card;
  }));
  $("activity").replaceChildren(
    node("span", "", `전리품 ${fmt(c.loot)}개`), node("span", "", `활성 청크 ${fmt(c.loadedChunks)}개`),
    node("span", "", `플레이어 자금 ${fmt(snapshot.player.credits)}`),
    node("span", "", `편지 도착 ${fmt(snapshot.society.lettersDelivered)} / 운송 중 ${fmt(snapshot.society.lettersInTransit)}`),
    node("span", "", `NPC 소문 전달 ${fmt(snapshot.society.rumorVisits)} / 이사 ${fmt(snapshot.society.migrations)}`));
  $("news").replaceChildren(...snapshot.news.map(text => node("li", "", text)));
  $("sequence").textContent = `수신 #${fmt(snapshot.sequence)} · ${snapshot.paused ? "게임 일시정지" : "게임 진행"}`;
  const signature = JSON.stringify(snapshot.towns.map(town => [town.id, town.name]));
  if (signature !== townOptionsSignature) {
    const currentTown = $("town-filter").value;
    $("town-filter").replaceChildren();
    option($("town-filter"), "", "전체 마을");
    snapshot.towns.forEach(town => option($("town-filter"), town.id, town.name));
    $("town-filter").value = currentTown;
    townOptionsSignature = signature;
  }
  $("download").disabled = false;
}

function renderNpcs() {
  if (!snapshot) return;
  const search = $("search").value.trim().toLocaleLowerCase();
  const town = $("town-filter").value, job = $("job-filter").value, status = $("health-filter").value;
  const rows = snapshot.npcs.filter(npc => {
    if (search && !`${npc.id} ${npc.name}`.toLocaleLowerCase().includes(search)) return false;
    if (town !== "" && String(npc.home) !== town) return false;
    if (job !== "" && String(npc.jobId) !== job) return false;
    if (status === "low" && healthRatio(npc) >= .3) return false;
    if (status === "combat" && npc.actionId !== 1 && npc.actionId !== 2) return false;
    if (status === "outside" && npc.inSafeZone) return false;
    if (status === "dead" && npc.alive) return false;
    return true;
  });
  const sort = $("sort").value;
  rows.sort((a, b) => (sort === "health" ? healthRatio(a) - healthRatio(b)
    : sort === "credits" ? number(b.credits) - number(a.credits)
    : sort === "insight" ? number(b.insight) - number(a.insight) : 0) || a.id - b.id);
  $("npc-rows").replaceChildren(...rows.map(npc => {
    const row = node("tr", npc.id === selectedId ? "selected" : "");
    const name = node("td"), button = node("button", "npc-link", `${npc.name} #${npc.id}`);
    button.addEventListener("click", () => { selectedId = npc.id; renderNpcs(); drawMap(); });
    name.append(button);
    const profession = node("td", "", townName(npc.home));
    profession.append(node("span", "sub", npc.job));
    const action = node("td", "", npc.action);
    action.append(node("span", "sub", npc.inSafeZone ? "마을 안" : "마을 밖"));
    const health = node("td", "", `${fmt(npc.health)} / ${fmt(npc.maxHealth)}`);
    const bar = node("div", `health${healthRatio(npc) < .3 ? " low" : ""}`);
    const fill = node("span"); fill.style.width = `${healthRatio(npc) * 100}%`;
    bar.append(fill); health.append(bar);
    row.append(name, profession, action, health, node("td", "", fmt(npc.credits)),
      node("td", "", fmt(npc.cargo)), node("td", "", fmt(npc.insight)), node("td", "", pointText(npc.position)));
    return row;
  }));
  $("visible-count").textContent = `${rows.length} / ${snapshot.npcs.length}명 표시`;
  $("empty").hidden = rows.length !== 0;
  $("empty").textContent = snapshot.npcs.length ? "조건에 맞는 NPC가 없습니다." : "수신된 NPC가 없습니다.";
  renderDetail();
}

function renderDetail() {
  const npc = snapshot.npcs.find(item => item.id === selectedId);
  if (!npc) {
    $("detail").replaceChildren(node("h3", "", "NPC 상세"), node("p", "muted", "목록에서 주민을 선택하세요."));
    return;
  }
  const list = node("dl");
  const target = npc.againstPlayer ? "플레이어" : npc.targetNpc ? `NPC #${npc.targetNpc}` : "없음";
  for (const [label, value] of [
    ["소속 / 직업", `${townName(npc.home)} / ${npc.job}`], ["행동", npc.action],
    ["내 집", `${townName(npc.home)} ${number(npc.houseSlot) + 1}번 집`],
    ["집 입구", pointText(npc.housePosition)],
    ["알고 있는 소문", `${npc.reports?.length ?? 0}개 마을`],
    ["이사 재검토", `${fmt(npc.migrationCooldown)}초 후`],
    ["생존", npc.alive ? "생존" : "사망"], ["체력", `${fmt(npc.health)} / ${fmt(npc.maxHealth)}`],
    ["자산 / 부품", `${fmt(npc.credits)} / ${fmt(npc.cargo)}`], ["깨달음", fmt(npc.insight)],
    ["악명", fmt(npc.notoriety)], ["공격 대상", target], ["현재 위치", pointText(npc.position)],
    ["목표 위치", pointText(npc.destination)], ["목표 마을", townName(npc.destinationTown)],
    ["작업 / 쿨타임", `${fmt(npc.workSeconds)}초 / ${fmt(npc.attackCooldown)}초`],
    ["목표 결정 횟수", npc.decisions], ["부활 대기", `${fmt(npc.recoverySeconds)}초`]
  ]) list.append(node("dt", "", label), node("dd", "", value));
  const weights = node("div", "weights");
  for (const [label, key] of [["생존", "survival"], ["재산", "wealth"], ["탐구", "insight"], ["약탈", "plunder"]]) {
    const value = number(npc.weights?.[key]);
    const bar = node("div", "weight-bar"), fill = node("span");
    fill.style.width = `${Math.max(0, Math.min(1, value / (key === "plunder" ? 1 : 3))) * 100}%`;
    bar.append(fill);
    weights.append(node("span", "", `${label} ${fmt(value)}`), bar);
  }
  $("detail").replaceChildren(node("h3", "", `${npc.name} #${npc.id}`), list, node("h3", "", "행동 성향"), weights);
  if (npc.reports?.length) {
    const rumors = node("ul", "news");
    for (const report of npc.reports) {
      const age = Math.max(0, number(snapshot.society.simulationTime) - number(report.timestamp));
      rumors.append(node("li", "", `${townName(report.town)} · 경제 ${fmt(report.prosperity)} / 안전 ${fmt(report.safety)} / 탐구 ${fmt(report.learning)} · ${fmt(age)}초 전 정보`));
    }
    $("detail").append(node("h3", "", "전달받은 마을 소문"), rumors);
  }
}

function canvasContext(id) {
  const canvas = $(id), rect = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.round(rect.width * ratio));
  canvas.height = Math.max(1, Math.round(rect.height * ratio));
  const ctx = canvas.getContext("2d");
  ctx.scale(ratio, ratio);
  return {ctx, width: rect.width, height: rect.height};
}

function drawMap() {
  if (!snapshot) return;
  const {ctx, width, height} = canvasContext("map");
  const points = [...snapshot.towns, ...snapshot.npcs, ...snapshot.monsters, ...(snapshot.houses ?? []), snapshot.player].map(item => item.position);
  const xs = points.map(p => number(p.x)), ys = points.map(p => number(p.y));
  const margin = Math.max(250, ...snapshot.towns.map(town => number(town.safeRadius))) + 40;
  const minX = Math.min(0, ...xs) - margin, maxX = Math.max(1920, ...xs) + margin;
  const minY = Math.min(0, ...ys) - margin, maxY = Math.max(1920, ...ys) + margin;
  const scale = Math.min((width - 40) / (maxX - minX), (height - 40) / (maxY - minY));
  const originX = (width - (maxX - minX) * scale) / 2, originY = (height - (maxY - minY) * scale) / 2;
  const project = p => [originX + (number(p.x) - minX) * scale, originY + (number(p.y) - minY) * scale];
  ctx.strokeStyle = "#203149"; ctx.lineWidth = 1;
  for (let i = 0; i <= 6; i++) {
    const x = 20 + (width - 40) * i / 6, y = 20 + (height - 40) * i / 6;
    ctx.beginPath(); ctx.moveTo(x, 20); ctx.lineTo(x, height - 20); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(20, y); ctx.lineTo(width - 20, y); ctx.stroke();
  }
  for (const town of snapshot.towns) {
    const [x, y] = project(town.position);
    ctx.beginPath(); ctx.arc(x, y, Math.max(2, number(town.safeRadius) * scale), 0, Math.PI * 2);
    ctx.fillStyle = "#143a3c99"; ctx.fill(); ctx.strokeStyle = colors.npc; ctx.stroke();
    ctx.fillStyle = colors.text; ctx.font = "11px sans-serif"; ctx.fillText(town.name, x + 8, y - 10);
  }
  for (const house of snapshot.houses ?? []) {
    const [x, y] = project(house.position);
    ctx.fillStyle = house.owner === selectedId ? colors.player : house.owner ? "#657f9c" : "#344659";
    ctx.fillRect(x - 1.5, y - 1.5, 3, 3);
  }
  function dot(item, color, size) {
    const [x, y] = project(item.position);
    ctx.beginPath(); ctx.arc(x, y, size, 0, Math.PI * 2); ctx.fillStyle = color; ctx.fill();
  }
  snapshot.npcs.forEach(item => dot(item, colors.npc, 3));
  snapshot.monsters.forEach(item => dot(item, colors.monster, item.kind === 2 ? 6 : 4));
  dot(snapshot.player, colors.player, 5);
  const selected = snapshot.npcs.find(item => item.id === selectedId);
  if (selected) {
    const [x, y] = project(selected.position);
    ctx.strokeStyle = "#80baff"; ctx.lineWidth = 2; ctx.strokeRect(x - 7, y - 7, 14, 14);
  }
}

function drawHistory() {
  const {ctx, width, height} = canvasContext("history");
  if (!history.length) return;
  const top = Math.max(10, ...history.flatMap(item => [item.npcs, item.monsters]));
  ctx.font = "11px sans-serif"; ctx.fillStyle = colors.text; ctx.strokeStyle = "#203149";
  for (let i = 0; i <= 4; i++) {
    const y = 22 + (height - 48) * i / 4;
    ctx.fillText(fmt(top * (1 - i / 4)), 5, y + 3);
    ctx.beginPath(); ctx.moveTo(36, y); ctx.lineTo(width - 12, y); ctx.stroke();
  }
  for (const [key, color] of [["npcs", colors.npc], ["monsters", colors.monster]]) {
    ctx.beginPath(); ctx.lineWidth = 2; ctx.strokeStyle = color;
    history.forEach((item, i) => {
      const x = 36 + (width - 48) * i / Math.max(1, history.length - 1);
      const y = 22 + (height - 48) * (1 - item[key] / top);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
  }
}

function updateFreshness() {
  if (!snapshot) return;
  const age = Math.max(0, (Date.now() - number(snapshot.timestampMs)) / 1000);
  const stale = age > 3;
  $("status").className = `badge${stale || snapshot.paused ? " stale" : ""}`;
  $("status").textContent = stale ? "데이터 갱신 중단" : snapshot.paused ? "게임 일시정지" : "실시간 수신 중";
  $("updated").textContent = `${new Date(snapshot.timestampMs).toLocaleTimeString("ko-KR")} · ${fmt(age)}초 전`;
  $("message").textContent = stale ? "마지막 수신 상태입니다. 게임 실행 또는 JSON 전달 상태를 확인하세요."
    : snapshot.paused ? "게임이 일시정지되어 있습니다. 상태 수신은 계속됩니다." : snapshot.society.objective;
}

async function poll() {
  const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), 2000);
  try {
    const response = await fetch("/api/state", {cache: "no-store", signal: controller.signal});
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || "JSON 수신 실패");
    if (data.schemaVersion !== 1 || !Array.isArray(data.npcs) || !Array.isArray(data.towns)
        || !Array.isArray(data.monsters) || !data.counts || !data.player || !data.society) throw new Error("지원하지 않는 JSON 상태 형식");
    snapshot = data;
    if (session !== data.session) { history.length = 0; lastSequence = null; session = data.session; }
    if (lastSequence !== data.sequence) {
      history.push({npcs: number(data.counts.npcs), monsters: number(data.counts.monsters)});
      if (history.length > 120) history.shift();
      lastSequence = data.sequence;
    }
    renderSummary(); renderNpcs(); drawMap(); drawHistory(); updateFreshness();
  } catch (error) {
    $("status").className = "badge error";
    $("status").textContent = snapshot ? "수신 중단" : "연결 대기";
    $("message").textContent = `마지막 상태를 유지합니다. ${error.message}`;
    if (snapshot) $("updated").textContent = `마지막 수신 ${new Date(snapshot.timestampMs).toLocaleTimeString("ko-KR")}`;
  } finally {
    clearTimeout(timeout);
    setTimeout(poll, 500);
  }
}

for (const id of ["search", "town-filter", "job-filter", "health-filter", "sort"]) {
  $(id).addEventListener(id === "search" ? "input" : "change", renderNpcs);
}
$("download").addEventListener("click", () => {
  if (!snapshot) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(snapshot, null, 2)], {type: "application/json"}));
  const link = node("a"); link.href = url; link.download = `nox-state-${snapshot.timestampMs}.json`;
  link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
});
window.addEventListener("resize", () => { drawMap(); drawHistory(); });
poll();
