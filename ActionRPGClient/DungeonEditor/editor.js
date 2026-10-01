"use strict";
(() => {
  const M = window.DungeonModel, $ = id => document.getElementById(id);
  const canvas = $("canvas"), ctx = canvas.getContext("2d"), mini = $("minimap");
  const kinds = { images: "이미지", walkablePolygons: "이동 가능", blockedPolygons: "진입 불가",
    playerSpawns: "입장 위치", entryPoints: "워프 도착점", warpZones: "워프존" };
  const modeKinds = { walkable: "walkablePolygons", blocked: "blockedPolygons", spawn: "playerSpawns", entry: "entryPoints", warp: "warpZones" };
  let project = M.createDocument(), roomIndex = -1, selection = null, view = "overview", mode = "select";
  let pending = [], connectionSource = null, projectHandle = null, filename = "새 던전", dirty = false, busy = false;
  let camera = { x: 0, y: 0, scale: 1 }, graphCamera = null, roomCamera = null;
  let drag = null, space = false, refreshSelects = [], miniTransform = null, imageCache = new Map();
  const room = () => project.rooms[roomIndex] || null;
  const element = (tag, text, className) => {
    const item = document.createElement(tag);
    if (text !== undefined) item.textContent = text;
    if (className) item.className = className;
    return item;
  };
  function notify(message, severity = "success") {
    const item = element("div", message, "issue " + severity);
    $("results").replaceChildren(item);
  }
  function updateStatus() {
    $("file-status").textContent = filename + (dirty ? " · 저장되지 않은 변경" : "");
    document.title = (dirty ? "* " : "") + project.name + " — Dungeon Map Editor";
  }
  function touch() {
    dirty = true;
    $("validation-summary").textContent = "변경됨 · 출력 전에 다시 검사합니다.";
    $("results").replaceChildren();
    updateStatus(); renderLists(); updateHeading();
    for (const refresh of refreshSelects) refresh();
    draw();
  }
  function setBusy(value) {
    busy = value; $("workspace").inert = value; document.body.classList.toggle("busy", value);
    for (const id of ["new", "open", "save", "validate", "export"]) $(id).disabled = value;
    if (value) { drag = null; space = false; }
  }
  async function operation(action) {
    if (busy) return;
    setBusy(true);
    try { await action(); }
    catch (error) { if (error.name !== "AbortError") notify(error.message || String(error), "error"); }
    finally { setBusy(false); }
  }
  function unfinished() {
    if (!pending.length) return false;
    notify("작성 중인 영역을 완성하거나 취소한 뒤 진행하세요.", "warning");
    return true;
  }
  function discardPending() {
    if (pending.length && !confirm("작성 중인 영역을 취소할까요?")) return false;
    pending = []; return true;
  }
  function roomLabel(value) { return value.name + " [" + value.id + "]"; }
  function selectRoom(index, inside = false) {
    if (!discardPending()) return;
    roomIndex = index; selection = null; connectionSource = null; imageCache.clear();
    roomCamera = null;
    if (inside) { const wasInside = view === "room"; switchView("room"); if (wasInside) fit(); }
    else if (view === "room") fit();
    renderAll();
  }
  function updateHeading() {
    const current = room();
    $("title").textContent = view === "room" && current ? roomLabel(current) : project.name;
    $("summary").textContent = project.rooms.length + "개 방 · " + project.connections.length + "개 연결";
    $("overview-tools").hidden = view !== "overview"; $("room-tools").hidden = view !== "room";
    $("overview-tab").classList.toggle("active", view === "overview");
    $("room-tab").classList.toggle("active", view === "room"); $("room-tab").disabled = !current;
    $("duplicate-room").disabled = !current; $("delete-room").disabled = !current;
    $("connect").disabled = !current || project.rooms.length < 2;
    $("connect").classList.toggle("active", connectionSource !== null);
    $("finish-polygon").disabled = pending.length < 3; $("cancel-polygon").disabled = !pending.length;
    $("add-image").disabled = !current;
    for (const button of document.querySelectorAll("[data-mode]")) {
      button.classList.toggle("active", mode === button.dataset.mode); button.disabled = !current;
    }
    $("hint").textContent = !project.rooms.length ? "왼쪽 ＋ 버튼으로 방을 추가하세요." :
      connectionSource ? "연결할 목적지 방을 클릭하세요. Esc: 취소" :
      view === "overview" ? "방 드래그: 미니맵 배치 · 방 사이 선 클릭: 연결 속성 · 가운데 버튼 / Space+드래그: 화면 이동" :
      pending.length ? "좌클릭: 꼭짓점 추가 · Enter: 완성 · Backspace: 마지막 점 취소 · Esc: 취소" :
      mode === "select" ? "요소나 꼭짓점을 드래그하세요. 겹친 요소는 왼쪽 목록에서 선택하세요. 휠: 확대·축소" :
      mode === "spawn" ? "클릭한 발 위치에 파티원 입장 슬롯을 추가합니다. 최대 인원만큼 배치하세요." :
      mode === "entry" ? "워프로 이동했을 때 도착할 발 위치를 클릭하세요." : "좌클릭으로 영역을 그리고 Enter로 완성하세요.";
  }
  function renderLists() {
    $("rooms").replaceChildren();
    project.rooms.forEach((value, index) => {
      const button = element("button", (value.id === project.entryRoomId ? "↳ " : "") + roomLabel(value) + (value.kind === "boss" ? " · Boss" : ""));
      button.classList.toggle("active", index === roomIndex);
      button.onclick = () => selectRoom(index);
      $("rooms").append(button);
    });
    if (!project.rooms.length) $("rooms").append(element("p", "추가된 방이 없습니다.", "muted"));
    $("layers").replaceChildren();
    const current = room();
    if (!current) return;
    for (const [kind, label] of Object.entries(kinds)) current[kind].forEach((value, index) => {
      const name = kind === "images" ? (project.assets.find(asset => asset.asset === value.asset)?.name || value.asset) :
        Array.isArray(value) ? value.length + "개 점" : value.id;
      const button = element("button", label + " " + (index + 1) + " · " + name);
      button.classList.toggle("active", selection?.kind === kind && selection.index === index);
      button.onclick = () => {
        if (!discardPending()) return;
        if (view !== "room") switchView("room");
        mode = "select"; selection = { kind, index }; renderAll();
      };
      $("layers").append(button);
    });
  }
  function section(title) { $("properties").append(element("h3", title)); }
  function textInfo(text) { $("properties").append(element("p", text, "help")); }
  function button(label, action, danger = false) {
    const item = element("button", label, danger ? "danger" : "");
    item.onclick = action; $("properties").append(item); return item;
  }
  function field(label, value, apply, numeric = false, integer = false) {
    const wrapper = element("label", undefined, "field"), input = element("input");
    wrapper.append(element("span", label), input); input.type = numeric ? "number" : "text";
    input.value = value; input.maxLength = 256; if (numeric) { input.step = integer ? "1" : "any"; input.min = "-1000000"; input.max = "1000000"; }
    input.onchange = () => {
      const next = numeric ? Number(input.value) : input.value;
      if (numeric && (input.value === "" || !Number.isFinite(next) || Math.abs(next) > 1000000 || (integer && !Number.isInteger(next)))) {
        input.value = value; notify("유효한 숫자를 입력하세요.", "error"); return;
      }
      if (apply(next) === false) { input.value = value; return; }
      value = next; touch();
    };
    $("properties").append(wrapper); return input;
  }
  function dropdown(label, getValue, options, apply) {
    const wrapper = element("label", undefined, "field"), select = element("select");
    wrapper.append(element("span", label), select); $("properties").append(wrapper);
    const refresh = () => {
      const current = getValue(), values = options();
      select.replaceChildren();
      select.append(new Option("선택하세요", ""));
      for (const option of values) select.append(new Option(option.label, option.value));
      if (current && !values.some(option => option.value === current)) select.append(new Option("미지정 / " + current, current));
      select.value = current;
    };
    refreshSelects.push(refresh); refresh();
    select.onchange = () => { apply(select.value); touch(); };
    return select;
  }
  function checked(label, getValue, apply) {
    const wrapper = element("label", undefined, "field"), input = element("input");
    input.type = "checkbox"; input.checked = getValue(); wrapper.append(element("span", label), input);
    input.onchange = () => { apply(input.checked); touch(); }; $("properties").append(wrapper);
  }
  function validId(id, values, previous) {
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(id) || values.some(value => value.id.toLowerCase() === id.toLowerCase() && value.id !== previous)) {
      notify("고유한 ID를 입력하세요. 영문자로 시작하는 영문·숫자·_-를 64자까지 사용할 수 있습니다.", "error"); return false;
    }
    return true;
  }
  function renderInspector() {
    const current = room(); $("properties").replaceChildren(); refreshSelects = [];
    $("inspector-title").textContent = selection?.kind === "connections" ? "연결 속성" : current ? "방 / 요소 속성" : "던전 속성";
    section("던전");
    field("던전 ID", project.dungeonId, value => { project.dungeonId = value; });
    field("던전 이름", project.name, value => { project.name = value; });
    field("최대 입장 인원", project.maxPlayers, value => { project.maxPlayers = value; }, true, true);
    dropdown("최초 입장 방", () => project.entryRoomId, () => project.rooms.map(value => ({ value: value.id, label: roomLabel(value) })), value => { project.entryRoomId = value; });
    if (selection?.kind === "connections") {
      const connection = project.connections[selection.index];
      if (!connection) return;
      section("방 연결");
      field("연결 ID", connection.id, value => {
        if (!validId(value, project.connections, connection.id)) return false;
        for (const valueRoom of project.rooms) for (const zone of valueRoom.warpZones)
          if (zone.connectionId === connection.id) zone.connectionId = value;
        connection.id = value;
      });
      textInfo(connection.fromRoomId + " → " + connection.toRoomId);
      checked("양방향 이동", () => connection.bidirectional, value => { connection.bidirectional = value; });
      textInfo("각 방향의 출발 방에 워프존을 그리고 연결 ID와 목적지 도착점을 지정하세요.");
      button("연결 삭제", deleteSelection, true); return;
    }
    if (!current) { textInfo("방을 추가해 던전을 구성하세요. 작업 저장은 미완성 상태에서도 가능합니다."); return; }
    section("방");
    field("방 ID", current.id, value => {
      if (!validId(value, project.rooms, current.id)) return false;
      const old = current.id;
      if (project.entryRoomId === old) project.entryRoomId = value;
      if (connectionSource === old) connectionSource = value;
      for (const connection of project.connections) {
        if (connection.fromRoomId === old) connection.fromRoomId = value;
        if (connection.toRoomId === old) connection.toRoomId = value;
      }
      for (const target of project.rooms) for (const zone of target.warpZones) if (zone.targetRoomId === old) zone.targetRoomId = value;
      current.id = value;
    });
    field("방 이름", current.name, value => { current.name = value; });
    dropdown("방 종류", () => current.kind, () => [{ value: "normal", label: "일반 방" }, { value: "boss", label: "보스 방" }], value => { if (value) current.kind = value; });
    field("미니맵 열 (X)", current.layout.x, value => { current.layout.x = value; }, true, true);
    field("미니맵 행 (Y)", current.layout.y, value => { current.layout.y = value; }, true, true);
    section("월드 표시 영역");
    for (const [key, label] of [["left", "왼쪽"], ["top", "위쪽"], ["right", "오른쪽"], ["bottom", "아래쪽"]])
      field(label, current.world[key], value => { current.world[key] = value; }, true);
    button("이미지에 월드 영역 맞춤", () => {
      if (!current.images.length) return notify("이미지를 먼저 추가하세요.", "warning");
      current.world = { left: Math.min(...current.images.map(i => i.x)), top: Math.min(...current.images.map(i => i.y)),
        right: Math.max(...current.images.map(i => i.x + i.width)), bottom: Math.max(...current.images.map(i => i.y + i.height)) };
      touch(); renderInspector(); fit();
    });
    for (const [key, label] of [["walkSpeed", "걷기 속도"], ["runSpeed", "달리기 속도"], ["sectorWidth", "섹터 너비"], ["sectorHeight", "섹터 높이"]])
      field(label, current[key], value => { current[key] = value; }, true);
    dropdown("기본 워프 도착점", () => current.defaultEntryPointId,
      () => current.entryPoints.map(point => ({ value: point.id, label: point.id })), value => { current.defaultEntryPointId = value; });
    if (!selection) return;
    const value = current[selection.kind]?.[selection.index];
    if (!value) return;
    section(kinds[selection.kind] + " " + (selection.index + 1));
    if (selection.kind === "images") {
      textInfo(value.asset);
      for (const key of ["x", "y", "width", "height"]) field(key, value[key], next => { value[key] = next; }, true);
      button("이 이미지 복제", () => {
        if (current.images.length >= 64) return notify("방당 이미지는 64개까지 가능합니다.", "error");
        current.images.push(M.clone(value)); selection.index = current.images.length - 1; touch(); renderInspector();
      });
    } else if (selection.kind === "playerSpawns" || selection.kind === "entryPoints") {
      field("ID", value.id, next => {
        if (!validId(next, current[selection.kind], value.id)) return false;
        if (selection.kind === "entryPoints") {
          for (const target of project.rooms) for (const zone of target.warpZones)
            if (zone.targetRoomId === current.id && zone.targetEntryPointId === value.id) zone.targetEntryPointId = next;
          if (current.defaultEntryPointId === value.id) current.defaultEntryPointId = next;
        }
        value.id = next;
      });
      field("발 위치 X", value.position.x, next => { value.position.x = next; }, true);
      field("발 위치 Y", value.position.y, next => { value.position.y = next; }, true);
    } else {
      const polygon = Array.isArray(value) ? value : value.polygon;
      if (selection.kind === "warpZones") {
        field("워프 ID", value.id, next => { if (!validId(next, current.warpZones, value.id)) return false; value.id = next; });
        const outgoing = () => project.connections.filter(c => c.fromRoomId === current.id || (c.bidirectional && c.toRoomId === current.id));
        dropdown("사용할 방 연결", () => value.connectionId,
          () => outgoing().map(c => ({ value: c.id, label: c.id + " → " + (c.fromRoomId === current.id ? c.toRoomId : c.fromRoomId) })), next => {
            value.connectionId = next;
            const connection = outgoing().find(c => c.id === next);
            value.targetRoomId = connection ? connection.fromRoomId === current.id ? connection.toRoomId : connection.fromRoomId : "";
            value.targetEntryPointId = "";
          });
        textInfo("목적지 방은 선택한 연결의 반대편 방으로 설정됩니다.");
        dropdown("목적지 도착점", () => value.targetEntryPointId,
          () => (project.rooms.find(target => target.id === value.targetRoomId)?.entryPoints || []).map(point => ({ value: point.id, label: point.id })),
          next => { value.targetEntryPointId = next; });
      }
      textInfo("꼭짓점을 드래그하거나 아래 좌표로 수정하세요.");
      polygon.forEach((point, index) => {
        field("점 " + (index + 1) + " X", point.x, next => { point.x = next; }, true);
        field("점 " + (index + 1) + " Y", point.y, next => { point.y = next; }, true);
      });
    }
    button("선택 요소 삭제", deleteSelection, true);
  }
  function renderAll() { renderLists(); updateHeading(); renderInspector(); draw(); updateStatus(); }
  function switchView(next) {
    if (next === "room" && !room()) return;
    if (next !== view && !discardPending()) return;
    if (next !== view) {
      if (view === "overview") graphCamera = { ...camera }; else roomCamera = { ...camera };
      view = next; camera = (view === "overview" ? graphCamera : roomCamera) || { x: 0, y: 0, scale: 1 };
      if (!(view === "overview" ? graphCamera : roomCamera)) fit();
    }
    selection = null; connectionSource = null; renderAll();
  }
  function setMode(next) {
    if (!room() || !discardPending()) return;
    mode = next; selection = null; renderAll(); canvas.focus();
  }
  function screenPoint(event) {
    const rect = canvas.getBoundingClientRect(); return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }
  const worldPoint = p => ({ x: (p.x - camera.x) / camera.scale, y: (p.y - camera.y) / camera.scale });
  const round = number => Math.round(number * 100) / 100;
  function resize() {
    const rect = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    canvas.width = Math.max(1, Math.round(rect.width * ratio)); canvas.height = Math.max(1, Math.round(rect.height * ratio));
    draw();
  }
  function fit() {
    const rect = canvas.getBoundingClientRect();
    let bounds;
    if (view === "overview" && project.rooms.length) {
      bounds = { left: Math.min(...project.rooms.map(r => r.layout.x * 140)), top: Math.min(...project.rooms.map(r => r.layout.y * 105)),
        right: Math.max(...project.rooms.map(r => r.layout.x * 140)) + 100, bottom: Math.max(...project.rooms.map(r => r.layout.y * 105)) + 65 };
    } else if (view === "room" && room()) bounds = room().world;
    else bounds = { left: 0, top: 0, right: 500, bottom: 300 };
    const width = Math.max(1, bounds.right - bounds.left), height = Math.max(1, bounds.bottom - bounds.top);
    const scale = Math.max(0.002, Math.min(4, (rect.width - 80) / width, (rect.height - 80) / height));
    camera = { scale, x: rect.width / 2 - (bounds.left + width / 2) * scale, y: rect.height / 2 - (bounds.top + height / 2) * scale };
    draw();
  }
  function arrow(context, a, b, scale) {
    const dx = b.x - a.x, dy = b.y - a.y, length = Math.hypot(dx, dy);
    if (!length) return;
    const x = (a.x + b.x) / 2, y = (a.y + b.y) / 2, ux = dx / length * 8 * scale, uy = dy / length * 8 * scale;
    context.beginPath(); context.moveTo(x - ux - uy / 2, y - uy + ux / 2); context.lineTo(x + ux, y + uy);
    context.lineTo(x - ux + uy / 2, y - uy - ux / 2); context.stroke();
  }
  function drawMap(context, map, highlight = "", selectedConnection = null) {
    const byId = new Map(map.rooms.map(r => [r.id, r])), s = map.scale;
    for (const [index, connection] of map.connections.entries()) {
      const a = byId.get(connection.fromRoomId), b = byId.get(connection.toRoomId);
      if (!a || !b) continue;
      context.strokeStyle = selectedConnection === index ? "#ffcd6c" : "#8ba4b8"; context.lineWidth = 3 * s;
      context.beginPath(); context.moveTo(a.center.x, a.center.y); context.lineTo(b.center.x, b.center.y); context.stroke();
      if (!connection.bidirectional) { context.strokeStyle = "#d8e5ee"; context.lineWidth = 2 * s; arrow(context, a.center, b.center, s); }
    }
    for (const value of map.rooms) {
      context.fillStyle = M.color(value); context.fillRect(value.x, value.y, value.width, value.height);
      context.lineWidth = (value.id === highlight ? 3 : 2) * s;
      context.strokeStyle = value.id === highlight ? "#fff" : "#adc6d9";
      context.strokeRect(value.x, value.y, value.width, value.height);
      context.fillStyle = "#fff"; context.font = 13 * s + "px sans-serif"; context.textAlign = "center"; context.textBaseline = "middle";
      context.fillText(value.id.slice(0, 12), value.center.x, value.center.y, value.width - 8 * s);
    }
  }
  function graphMap() {
    return { scale: 1, connections: project.connections, rooms: project.rooms.map(r => ({
      id: r.id, name: r.name, kind: r.kind, isEntry: r.id === project.entryRoomId,
      x: r.layout.x * 140, y: r.layout.y * 105, width: 100, height: 65,
      center: { x: r.layout.x * 140 + 50, y: r.layout.y * 105 + 32.5 } })) };
  }
  function drawMini() {
    const context = mini.getContext("2d"), map = M.minimap(project);
    context.clearRect(0, 0, mini.width, mini.height);
    const scale = Math.min((mini.width - 12) / map.width, (mini.height - 12) / map.height);
    const x = (mini.width - map.width * scale) / 2, y = (mini.height - map.height * scale) / 2;
    miniTransform = { x, y, scale, map };
    context.save(); context.translate(x, y); context.scale(scale, scale); drawMap(context, map, room()?.id); context.restore();
    if (!map.rooms.length) { context.fillStyle = "#8194a5"; context.font = "14px sans-serif"; context.fillText("방을 추가하세요", 75, 100); }
  }
  function polygonPath(context, polygon) {
    context.beginPath();
    polygon.forEach((p, i) => i ? context.lineTo(p.x, p.y) : context.moveTo(p.x, p.y)); context.closePath();
  }
  function drawPolygon(polygon, color, active) {
    if (!polygon.length) return;
    polygonPath(ctx, polygon); ctx.fillStyle = color; ctx.fill();
    ctx.strokeStyle = active ? "#fff" : color.replace(/0\.\d+\)/, "1)");
    ctx.lineWidth = (active ? 2 : 1) / camera.scale; ctx.stroke();
    if (active) for (const p of polygon) { ctx.fillStyle = "#fff"; ctx.fillRect(p.x - 4 / camera.scale, p.y - 4 / camera.scale, 8 / camera.scale, 8 / camera.scale); }
  }
  function imageFor(assetPath) {
    if (imageCache.has(assetPath)) return imageCache.get(assetPath);
    const asset = project.assets.find(value => value.asset === assetPath);
    if (!asset) return null;
    const image = new Image(), cache = { image, ready: false, failed: false };
    imageCache.set(assetPath, cache);
    image.onload = () => { cache.ready = true; if (imageCache.get(assetPath) === cache) draw(); };
    image.onerror = () => { cache.failed = true; if (imageCache.get(assetPath) === cache) draw(); };
    image.src = asset.dataUrl; return cache;
  }
  function drawRoom() {
    const current = room(); if (!current) return;
    const w = current.world;
    ctx.save(); ctx.beginPath(); ctx.rect(w.left, w.top, w.right - w.left, w.bottom - w.top); ctx.clip();
    ctx.fillStyle = "#19242e"; ctx.fillRect(w.left, w.top, w.right - w.left, w.bottom - w.top);
    ctx.imageSmoothingEnabled = false;
    current.images.forEach((value, index) => {
      const cache = imageFor(value.asset);
      if (cache?.ready) ctx.drawImage(cache.image, value.x, value.y, Math.max(1, value.width), Math.max(1, value.height));
      else {
        ctx.fillStyle = cache?.failed ? "#832b32" : "#273442"; ctx.fillRect(value.x, value.y, Math.max(1, value.width), Math.max(1, value.height));
        ctx.fillStyle = "#fff"; ctx.font = 14 / camera.scale + "px sans-serif"; ctx.fillText(cache?.failed ? "이미지 읽기 실패" : "이미지 읽는 중", value.x + 10, value.y + 20);
      }
      if (selection?.kind === "images" && selection.index === index) {
        ctx.strokeStyle = "#fff"; ctx.lineWidth = 2 / camera.scale; ctx.strokeRect(value.x, value.y, value.width, value.height);
      }
    });
    for (const [kind, color] of [["walkablePolygons", "rgba(44,192,123,0.22)"], ["blockedPolygons", "rgba(238,83,91,0.3)"], ["warpZones", "rgba(138,104,246,0.35)"]])
      current[kind].forEach((value, index) => drawPolygon(Array.isArray(value) ? value : value.polygon, color, selection?.kind === kind && selection.index === index));
    for (const [kind, color, prefix] of [["playerSpawns", "#8dffc5", "P"], ["entryPoints", "#ffd789", "E"]]) current[kind].forEach((value, index) => {
      const p = value.position; ctx.fillStyle = color; ctx.strokeStyle = color; ctx.lineWidth = 1 / camera.scale;
      ctx.beginPath(); ctx.ellipse(p.x, p.y, 32, 18, 0, 0, Math.PI * 2); ctx.stroke();
      ctx.beginPath(); ctx.arc(p.x, p.y, 4 / camera.scale, 0, Math.PI * 2); ctx.fill();
      if (selection?.kind === kind && selection.index === index) { ctx.strokeStyle = "#fff"; ctx.lineWidth = 3 / camera.scale; ctx.stroke(); }
      ctx.font = 12 / camera.scale + "px sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "bottom";
      ctx.fillText(prefix + (index + 1) + " " + value.id, p.x, p.y - 20);
    });
    ctx.restore();
    ctx.strokeStyle = "#adc6d9"; ctx.lineWidth = 1 / camera.scale; ctx.strokeRect(w.left, w.top, w.right - w.left, w.bottom - w.top);
    if (pending.length) {
      ctx.beginPath(); pending.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y));
      ctx.strokeStyle = "#ffdb87"; ctx.lineWidth = 2 / camera.scale; ctx.stroke();
      for (const p of pending) { ctx.fillStyle = "#ffdb87"; ctx.fillRect(p.x - 3 / camera.scale, p.y - 3 / camera.scale, 6 / camera.scale, 6 / camera.scale); }
    }
  }
  function draw() {
    const ratio = window.devicePixelRatio || 1;
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0); ctx.clearRect(0, 0, canvas.width / ratio, canvas.height / ratio);
    ctx.save(); ctx.translate(camera.x, camera.y); ctx.scale(camera.scale, camera.scale);
    if (view === "overview") drawMap(ctx, graphMap(), room()?.id, selection?.kind === "connections" ? selection.index : null);
    else drawRoom();
    ctx.restore(); $("zoom").textContent = Math.round(camera.scale * 100) + "%"; drawMini();
  }
  function distanceToSegment(p, a, b) {
    const dx = b.x - a.x, dy = b.y - a.y, length = dx * dx + dy * dy;
    const t = length ? Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / length)) : 0;
    return Math.hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
  }
  function graphHit(p) {
    for (let i = project.rooms.length - 1; i >= 0; --i) {
      const r = project.rooms[i], x = r.layout.x * 140, y = r.layout.y * 105;
      if (p.x >= x && p.x <= x + 100 && p.y >= y && p.y <= y + 65) return { kind: "room", index: i };
    }
    for (let i = project.connections.length - 1; i >= 0; --i) {
      const c = project.connections[i], a = project.rooms.find(r => r.id === c.fromRoomId), b = project.rooms.find(r => r.id === c.toRoomId);
      if (a && b && distanceToSegment(p, { x: a.layout.x * 140 + 50, y: a.layout.y * 105 + 32.5 }, { x: b.layout.x * 140 + 50, y: b.layout.y * 105 + 32.5 }) < 8 / camera.scale) return { kind: "connections", index: i };
    }
    return null;
  }
  function selectedValue() { return selection && room()?.[selection.kind]?.[selection.index]; }
  function roomHit(p) {
    const current = room(); if (!current) return null;
    const active = selectedValue();
    const polygon = active && (Array.isArray(active) ? active : active.polygon);
    if (polygon) for (let i = 0; i < polygon.length; ++i)
      if (Math.hypot(p.x - polygon[i].x, p.y - polygon[i].y) < 9 / camera.scale) return { ...selection, vertex: i };
    for (const kind of ["entryPoints", "playerSpawns", "warpZones", "blockedPolygons", "walkablePolygons", "images"])
      for (let i = current[kind].length - 1; i >= 0; --i) {
        const value = current[kind][i];
        const hit = value.position ? Math.hypot((p.x - value.position.x) / 32, (p.y - value.position.y) / 18) <= 1 :
          kind === "images" ? p.x >= value.x && p.x <= value.x + value.width && p.y >= value.y && p.y <= value.y + value.height :
          M.contains(p, Array.isArray(value) ? value : value.polygon);
        if (hit) return { kind, index: i };
      }
    return null;
  }
  function linkRoom(target) {
    if (target.id === connectionSource) return;
    if (project.connections.some(c => [c.fromRoomId, c.toRoomId].includes(connectionSource) && [c.fromRoomId, c.toRoomId].includes(target.id))) {
      notify("두 방 사이에는 이미 연결이 있습니다.", "warning"); return;
    }
    if (project.connections.length >= 1024) return notify("연결은 1024개까지 가능합니다.", "error");
    project.connections.push({ id: M.unique("Link", project.connections), fromRoomId: connectionSource, toRoomId: target.id, bidirectional: true });
    connectionSource = null; selection = { kind: "connections", index: project.connections.length - 1 }; touch(); renderInspector(); updateHeading();
  }
  function addPoint(p) {
    const current = room(); if (!current) return;
    const kind = modeKinds[mode];
    if (mode === "spawn" || mode === "entry") {
      if (current[kind].length >= (mode === "spawn" ? 32 : 256)) return notify("위치 개수 제한을 초과했습니다.", "error");
      const point = { id: M.unique(mode === "spawn" ? "Player" : "Entry", current[kind]), position: { x: round(p.x), y: round(p.y) } };
      current[kind].push(point);
      if (mode === "entry" && !current.defaultEntryPointId) current.defaultEntryPointId = point.id;
      selection = { kind, index: current[kind].length - 1 }; touch(); renderInspector();
    } else {
      if (pending.length >= 64) return notify("꼭짓점은 64개까지 가능합니다.", "error");
      pending.push({ x: round(p.x), y: round(p.y) }); updateHeading(); draw();
    }
  }
  function finishPolygon() {
    const current = room(), kind = modeKinds[mode];
    if (!current || !kind || ["spawn", "entry"].includes(mode)) return;
    const problem = M.polygonProblem(pending);
    if (problem) return notify(problem, "error");
    if (current[kind].length >= 256) return notify("영역은 종류별로 256개까지 가능합니다.", "error");
    current[kind].push(mode === "warp" ? { id: M.unique("Warp", current.warpZones), polygon: pending,
      connectionId: "", targetRoomId: "", targetEntryPointId: "" } : pending);
    selection = { kind, index: current[kind].length - 1 }; pending = []; mode = "select"; touch(); renderInspector();
  }
  function deleteSelection() {
    if (!selection || !confirm(selection.kind === "connections" ? "연결과 이 연결을 사용하는 워프존을 삭제할까요?" : "선택 요소를 삭제할까요? 목적지 참조가 있다면 다시 지정해야 합니다.")) return;
    if (selection.kind === "connections") {
      const connection = project.connections[selection.index]; project.connections.splice(selection.index, 1);
      for (const target of project.rooms) target.warpZones = target.warpZones.filter(zone => zone.connectionId !== connection.id);
    } else if (room()) {
      room()[selection.kind].splice(selection.index, 1);
      // Unreferenced images are removed from portable projects to keep them bounded.
      if (selection.kind === "images") pruneAssets();
    }
    selection = null; touch(); renderInspector();
  }
  function pruneAssets() {
    const used = new Set(project.rooms.flatMap(r => r.images.map(image => image.asset)));
    project.assets = project.assets.filter(asset => used.has(asset.asset)); imageCache.clear();
  }
  canvas.addEventListener("pointerdown", event => {
    if (busy || ![0, 1].includes(event.button)) return;
    canvas.focus(); const screen = screenPoint(event), p = worldPoint(screen);
    if (event.button === 1 || space) drag = { type: "pan", screen, start: { ...camera } };
    else if (view === "overview") {
      const hit = graphHit(p);
      if (connectionSource) { if (hit?.kind === "room") linkRoom(project.rooms[hit.index]); return; }
      if (hit?.kind === "room") {
        roomIndex = hit.index; selection = null; imageCache.clear(); roomCamera = null;
        drag = { type: "room", p, original: { ...room().layout }, moved: false }; renderAll();
      } else if (hit?.kind === "connections") { selection = hit; renderAll(); }
      else { selection = null; renderInspector(); drag = { type: "pan", screen, start: { ...camera } }; }
    } else if (mode !== "select") addPoint(p);
    else {
      const hit = roomHit(p); selection = hit ? { kind: hit.kind, index: hit.index } : null;
      if (hit) drag = { type: "element", p, hit, original: M.clone(selectedValue()), moved: false };
      renderAll();
    }
    if (drag) canvas.setPointerCapture(event.pointerId); event.preventDefault();
  });
  canvas.addEventListener("pointermove", event => {
    if (busy) return;
    const screen = screenPoint(event), p = worldPoint(screen);
    $("cursor").textContent = view === "room" ? "X " + round(p.x) + " · Y " + round(p.y) : "미니맵 열 " + Math.round(p.x / 140) + " · 행 " + Math.round(p.y / 105);
    if (!drag) return;
    if (drag.type === "pan") { camera.x = drag.start.x + screen.x - drag.screen.x; camera.y = drag.start.y + screen.y - drag.screen.y; draw(); return; }
    if (drag.type === "room") {
      const x = Math.max(-1000, Math.min(1000, Math.round(drag.original.x + (p.x - drag.p.x) / 140)));
      const y = Math.max(-1000, Math.min(1000, Math.round(drag.original.y + (p.y - drag.p.y) / 105)));
      if (room().layout.x !== x || room().layout.y !== y) { room().layout = { x, y }; drag.moved = true; touch(); }
    } else {
      const value = selectedValue(), original = drag.original, dx = round(p.x - drag.p.x), dy = round(p.y - drag.p.y);
      if (!value || (dx === 0 && dy === 0 && !drag.moved)) return;
      const translate = point => ({ x: Math.max(-1000000, Math.min(1000000, round(point.x + dx))), y: Math.max(-1000000, Math.min(1000000, round(point.y + dy))) });
      if (value.position) value.position = translate(original.position);
      else if (selection.kind === "images") { const next = translate(original); value.x = next.x; value.y = next.y; }
      else {
        const polygon = Array.isArray(value) ? value : value.polygon, old = Array.isArray(original) ? original : original.polygon;
        if (drag.hit.vertex !== undefined) polygon[drag.hit.vertex] = translate(old[drag.hit.vertex]);
        else for (let i = 0; i < polygon.length; ++i) polygon[i] = translate(old[i]);
      }
      drag.moved = true; touch();
    }
  });
  function endDrag(event) {
    const moved = drag?.moved; drag = null;
    if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    if (moved) renderInspector();
  }
  canvas.addEventListener("pointerup", endDrag); canvas.addEventListener("pointercancel", endDrag);
  canvas.addEventListener("dblclick", event => { if (!busy && view === "overview") { const hit = graphHit(worldPoint(screenPoint(event))); if (hit?.kind === "room") selectRoom(hit.index, true); } });
  canvas.addEventListener("contextmenu", event => event.preventDefault());
  canvas.addEventListener("wheel", event => {
    event.preventDefault(); if (busy) return;
    const p = screenPoint(event), w = worldPoint(p), scale = Math.max(0.002, Math.min(8, camera.scale * Math.exp(-event.deltaY * 0.001)));
    camera = { x: p.x - w.x * scale, y: p.y - w.y * scale, scale }; draw();
  }, { passive: false });
  mini.onclick = event => {
    if (busy || !miniTransform) return;
    const rect = mini.getBoundingClientRect(), t = miniTransform;
    const x = ((event.clientX - rect.left) * mini.width / rect.width - t.x) / t.scale;
    const y = ((event.clientY - rect.top) * mini.height / rect.height - t.y) / t.scale;
    const value = t.map.rooms.find(r => x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height);
    if (value) selectRoom(project.rooms.findIndex(r => r.id === value.id), true);
  };
  document.addEventListener("keydown", event => {
    if (busy || /INPUT|TEXTAREA|SELECT/.test(event.target.tagName)) return;
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") { event.preventDefault(); save(); return; }
    if (event.code === "Space") { space = true; event.preventDefault(); return; }
    if (event.key === "Escape") { pending = []; connectionSource = null; drag = null; renderInspector(); updateHeading(); draw(); return; }
    if (event.key === "Enter" && pending.length) { event.preventDefault(); finishPolygon(); }
    if (event.key === "Backspace" && pending.length) { event.preventDefault(); pending.pop(); updateHeading(); draw(); }
    if (event.key === "Delete") { event.preventDefault(); deleteSelection(); }
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    if (event.key.toLowerCase() === "f") fit();
    if (view === "room") {
      const shortcuts = { v: "select", w: "walkable", b: "blocked", p: "spawn", e: "entry", z: "warp" };
      if (shortcuts[event.key.toLowerCase()]) setMode(shortcuts[event.key.toLowerCase()]);
    }
  });
  document.addEventListener("keyup", event => { if (event.code === "Space") space = false; });
  window.addEventListener("blur", () => { space = false; drag = null; });
  $("add-room").onclick = () => {
    if (!discardPending()) return;
    if (project.rooms.length >= 256) return notify("방은 256개까지 가능합니다.", "error");
    const value = M.createRoom(project); project.rooms.push(value); roomIndex = project.rooms.length - 1;
    if (!project.entryRoomId) project.entryRoomId = value.id;
    selection = null; connectionSource = null; imageCache.clear(); roomCamera = null;
    touch(); renderInspector(); fit();
  };
  $("duplicate-room").onclick = () => {
    if (!room() || !discardPending()) return;
    if (project.rooms.length >= 256) return notify("방은 256개까지 가능합니다.", "error");
    const value = M.clone(room()), slot = M.createRoom(project);
    value.id = slot.id; value.name = value.name.slice(0, 253) + " 복사"; value.layout = slot.layout; value.warpZones = [];
    project.rooms.push(value); roomIndex = project.rooms.length - 1; selection = null; roomCamera = null; imageCache.clear();
    touch(); renderInspector(); fit();
  };
  $("delete-room").onclick = () => {
    if (!room() || !confirm("이 방과 관련 연결·워프존을 삭제할까요?")) return;
    const id = room().id, links = new Set(project.connections.filter(c => c.fromRoomId === id || c.toRoomId === id).map(c => c.id));
    project.rooms.splice(roomIndex, 1); project.connections = project.connections.filter(c => !links.has(c.id));
    for (const target of project.rooms) target.warpZones = target.warpZones.filter(zone => zone.targetRoomId !== id && !links.has(zone.connectionId));
    if (project.entryRoomId === id) project.entryRoomId = "";
    roomIndex = project.rooms.length ? Math.min(roomIndex, project.rooms.length - 1) : -1;
    pending = []; selection = null; connectionSource = null; roomCamera = null; pruneAssets(); touch(); renderInspector(); fit();
  };
  $("overview-tab").onclick = () => switchView("overview"); $("room-tab").onclick = () => switchView("room");
  $("fit").onclick = fit;
  $("connect").onclick = () => { connectionSource = connectionSource ? null : room()?.id || null; selection = null; updateHeading(); renderInspector(); };
  for (const item of document.querySelectorAll("[data-mode]")) item.onclick = () => setMode(item.dataset.mode);
  $("finish-polygon").onclick = finishPolygon;
  $("cancel-polygon").onclick = () => { pending = []; updateHeading(); draw(); };
  function readDataUrl(file) {
    return new Promise((resolve, reject) => {
      const reader = new FileReader(); reader.onload = () => resolve(reader.result); reader.onerror = () => reject(new Error("이미지를 읽지 못했습니다."));
      reader.readAsDataURL(file);
    });
  }
  function decode(dataUrl) {
    return new Promise((resolve, reject) => {
      const image = new Image(); image.onload = () => resolve(image); image.onerror = () => reject(new Error("이미지 파일을 해석하지 못했습니다."));
      image.src = dataUrl;
    });
  }
  $("add-image").onclick = () => { if (!unfinished() && room()) $("image-file").click(); };
  $("image-file").onchange = () => {
    const files = [...$("image-file").files], current = room(); $("image-file").value = "";
    if (!files.length || !current) return;
    operation(async () => {
      if (current.images.length + files.length > 64) throw new Error("방당 이미지는 64개까지 가능합니다.");
      const additions = [], assets = [...project.assets];
      let total = assets.reduce((sum, asset) => sum + asset.dataUrl.length, 0);
      for (const file of files) {
        if (!file.size || file.size > 15 * 1024 * 1024) throw new Error("이미지 하나는 15MB 이하여야 합니다.");
        const mime = file.type === "image/x-ms-bmp" ? "image/bmp" : file.type;
        const extension = { "image/png": "png", "image/jpeg": "jpg", "image/webp": "webp", "image/bmp": "bmp" }[mime];
        if (!extension) throw new Error("PNG, JPEG, WebP, BMP 이미지만 지원합니다.");
        let dataUrl = await readDataUrl(file);
        dataUrl = dataUrl.replace(/^data:image\/x-ms-bmp;/, "data:image/bmp;");
        const image = await decode(dataUrl);
        if (image.naturalWidth > 8192 || image.naturalHeight > 8192) throw new Error("이미지 한 변은 8192픽셀 이하여야 합니다.");
        total += dataUrl.length; if (total > 100000000 || assets.length >= 512) throw new Error("프로젝트 이미지 용량(100MB) 또는 개수(512개)를 초과했습니다.");
        const id = M.unique("Asset", assets.map(asset => asset.asset.split("/").pop().split(".")[0]));
        const asset = { asset: "Images/Dungeons/" + id + "." + extension, name: file.name.slice(0, 256), mime, dataUrl,
          width: image.naturalWidth, height: image.naturalHeight };
        assets.push(asset); additions.push({ asset: asset.asset, x: 0, y: 0, width: asset.width, height: asset.height });
      }
      project.assets = assets; current.images.push(...additions); selection = { kind: "images", index: current.images.length - 1 };
      touch(); renderInspector(); notify("이미지를 추가했습니다. 필요하면 ‘이미지에 월드 영역 맞춤’을 누르세요.");
    });
  };
  function check() {
    const issues = M.validate(project), errors = issues.filter(issue => issue.severity === "error").length;
    $("validation-summary").textContent = errors + "개 오류 · " + (issues.length - errors) + "개 경고";
    $("results").replaceChildren();
    if (!issues.length) $("results").append(element("div", "정적 검사 통과. 연결은 순환할 수 있습니다. 방 내부의 전체 경로 탐색은 검사 범위에 포함되지 않습니다.", "issue success"));
    for (const issue of issues) {
      const item = element("button", issue.message, "issue " + issue.severity);
      item.onclick = () => { const index = project.rooms.findIndex(r => r.id === issue.roomId); if (index >= 0) selectRoom(index, true); };
      $("results").append(item);
    }
    return errors === 0;
  }
  $("validate").onclick = () => { if (!unfinished()) check(); };
  function download(blob, name) {
    const url = URL.createObjectURL(blob), anchor = element("a"); anchor.href = url; anchor.download = name;
    document.body.append(anchor); anchor.click(); anchor.remove(); setTimeout(() => URL.revokeObjectURL(url), 60000);
  }
  async function writeFile(handle, blob) {
    const writable = await handle.createWritable();
    try { await writable.write(blob); await writable.close(); }
    catch (error) { try { await writable.abort(); } catch (_) { /* Preserve the original error. */ } throw error; }
  }
  async function save() {
    if (busy || unfinished()) return;
    await operation(async () => {
      let handle = projectHandle;
      if (!handle && window.showSaveFilePicker) handle = await window.showSaveFilePicker({ suggestedName: project.dungeonId + ".dungeon-project.json",
        types: [{ description: "던전 작업 파일", accept: { "application/json": [".json"] } }] });
      // Keep malformed draft fields from becoming a project that cannot be reopened.
      const text = JSON.stringify(project, null, 2) + "\n"; M.parse(text);
      const blob = new Blob([text], { type: "application/json" });
      if (blob.size > 105 * 1024 * 1024) throw new Error("작업 파일은 105MB 이하여야 합니다.");
      if (handle) {
        await writeFile(handle, blob); projectHandle = handle; filename = handle.name; dirty = false; updateStatus(); notify("작업 파일을 저장했습니다.");
      } else {
        download(blob, project.dungeonId + ".dungeon-project.json"); notify("작업 파일 다운로드를 요청했습니다. 브라우저의 다운로드 완료를 확인하세요.");
      }
    });
  }
  function replaceDocument(value, name, handle) {
    project = value; projectHandle = handle; filename = name; dirty = false; roomIndex = project.rooms.length ? 0 : -1;
    selection = null; pending = []; connectionSource = null; mode = "select"; view = "overview"; graphCamera = roomCamera = null;
    imageCache.clear(); $("results").replaceChildren(); $("validation-summary").textContent = "불러옴 · 출력 전에 검사하세요."; renderAll(); fit();
  }
  async function loadFile(file, handle) {
    if (file.size > 105 * 1024 * 1024) throw new Error("작업 파일은 105MB 이하여야 합니다.");
    const value = M.parse(await file.text()); replaceDocument(value, file.name, handle);
    notify("작업 파일을 불러왔습니다. 이미지 해석 여부는 미리보기와 출력 과정에서 확인합니다.");
  }
  $("open").onclick = () => {
    if (busy || (dirty || pending.length) && !confirm("저장하지 않은 작업을 버리고 불러올까요?")) return;
    if (!window.showOpenFilePicker) { $("project-file").click(); return; }
    operation(async () => {
      const handles = await window.showOpenFilePicker({ multiple: false, types: [{ description: "던전 작업 JSON", accept: { "application/json": [".json"] } }] });
      await loadFile(await handles[0].getFile(), handles[0]);
    });
  };
  $("project-file").onchange = () => {
    const file = $("project-file").files[0]; $("project-file").value = "";
    if (file) operation(() => loadFile(file, null));
  };
  $("new").onclick = () => {
    if (busy || (dirty || pending.length) && !confirm("저장하지 않은 작업을 버리고 새 던전을 만들까요?")) return;
    replaceDocument(M.createDocument(), "새 던전", null);
  };
  $("save").onclick = save;
  $("export").onclick = () => {
    if (busy || unfinished() || !check()) return;
    operation(async () => {
      const handle = window.showSaveFilePicker ? await window.showSaveFilePicker({ suggestedName: project.dungeonId + ".zip",
        types: [{ description: "던전과 미니맵 묶음", accept: { "application/zip": [".zip"] } }] }) : null;
      // Lock authoring during async asset decoding and writes; export is a coherent snapshot.
      const used = new Set(project.rooms.flatMap(r => r.images.map(image => image.asset)));
      for (const asset of project.assets.filter(asset => used.has(asset.asset))) {
        const image = await decode(asset.dataUrl);
        if (image.naturalWidth !== asset.width || image.naturalHeight !== asset.height) throw new Error(asset.name + ": 저장된 이미지 크기와 실제 크기가 다릅니다.");
      }
      const map = M.minimap(project), output = document.createElement("canvas"); output.width = map.width; output.height = map.height;
      drawMap(output.getContext("2d"), map);
      const png = await new Promise((resolve, reject) => output.toBlob(blob => blob ? resolve(blob) : reject(new Error("미니맵 PNG 생성에 실패했습니다.")), "image/png"));
      const files = M.runtimeFiles(project, map); files.push({ name: "Minimap.png", data: new Uint8Array(await png.arrayBuffer()) });
      const blob = window.DungeonArchive.create(files);
      if (handle) { await writeFile(handle, blob); notify("던전 맵 · 이미지 · 미니맵을 출력했습니다: " + handle.name); }
      else { download(blob, project.dungeonId + ".zip"); notify("던전 ZIP 다운로드를 요청했습니다. 브라우저의 다운로드 완료를 확인하세요."); }
    });
  };
  window.addEventListener("beforeunload", event => { if (dirty || pending.length || busy) { event.preventDefault(); event.returnValue = ""; } });
  new ResizeObserver(resize).observe(canvas);
  renderAll(); resize(); fit();
})();
