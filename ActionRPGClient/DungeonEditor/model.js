/* Offline project model. Geometry uses foot positions in world pixels. */
"use strict";
window.DungeonModel = (() => {
  const ID = /^[A-Za-z][A-Za-z0-9_-]{0,63}$/;
  const ASSET = /^Images\/Dungeons\/[A-Za-z0-9_-]+\.(png|jpg|webp|bmp)$/;
  const DATA = /^data:(image\/(?:png|jpeg|webp|bmp));base64,([A-Za-z0-9+/]*={0,2})$/;
  const EPS = 1e-6;
  const SIDES = ["north", "east", "south", "west"];
  const OPPOSITE = { north: "south", east: "west", south: "north", west: "east" };
  const clone = value => JSON.parse(JSON.stringify(value));
  const unique = (prefix, values) => {
    const ids = new Set(values.map(value => (typeof value === "string" ? value : value.id).toLowerCase()));
    let i = 1;
    while (ids.has((prefix + i).toLowerCase())) ++i;
    return prefix + i;
  };
  function createDocument() {
    return { schemaVersion: 2, dungeonId: "Dungeon", name: "새 던전", maxPlayers: 4,
      entryRoomId: "", rooms: [], connections: [], assets: [] };
  }
  function createRoom(document) {
    let x = 0;
    while (document.rooms.some(room => room.layout.x === x && room.layout.y === 0)) ++x;
    return { id: unique("Room", document.rooms), name: "새 방", kind: "normal", layout: { x, y: 0 },
      world: { left: 0, top: 0, right: 1280, bottom: 720 },
      sectorWidth: 480, sectorHeight: 480, images: [], walkablePolygons: [], blockedPolygons: [],
      playerSpawns: [], entryPoints: [], defaultEntryPointId: "", warpZones: [], objects: [] };
  }
  // Reject malformed containers before UI access; semantic errors stay editable.
  function parse(text) {
    const document = JSON.parse(text);
    const fail = () => { throw new Error("지원하지 않거나 손상된 던전 작업 파일입니다."); };
    const obj = value => value !== null && typeof value === "object" && !Array.isArray(value);
    const str = value => typeof value === "string" && value.length <= 256;
    const num = value => Number.isFinite(value) && Math.abs(value) <= 1000000;
    const list = (value, limit) => Array.isArray(value) && value.length <= limit;
    const point = value => obj(value) && num(value.x) && num(value.y);
    const polygon = value => list(value, 64) && value.every(point);
    const location = value => obj(value) && str(value.id) && point(value.position);
    const rectangle = value => obj(value) && ["x", "y", "width", "height"].every(key => num(value[key]));
    const migrated = obj(document) && document.schemaVersion === 1;
    // Old geometry is preserved. Infer a side only from its physical boundary, never the graph.
    if (obj(document) && document.schemaVersion === 1 && list(document.rooms, 256)) {
      for (const room of document.rooms) {
        if (!obj(room) || !obj(room.world) || !list(room.warpZones, 256)) fail();
        delete room.walkSpeed; delete room.runSpeed; room.objects = [];
        for (const zone of room.warpZones) {
          if (!obj(zone) || !polygon(zone.polygon)) fail();
          zone.direction = inferSide(room, zone.polygon);
          zone.visual = createVisual(zone.polygon);
        }
      }
      document.schemaVersion = 2;
    }
    if (!obj(document) || document.schemaVersion !== 2 || !str(document.dungeonId) ||
        !str(document.name) || !str(document.entryRoomId) || !num(document.maxPlayers) ||
        !list(document.rooms, 256) || !list(document.connections, 1024) || !list(document.assets, 512)) fail();
    for (const room of document.rooms) {
      if (!obj(room) || !str(room.id) || !str(room.name) || !["normal", "boss"].includes(room.kind) ||
          !point(room.layout) || !obj(room.world) ||
          !["left", "top", "right", "bottom"].every(key => num(room.world[key])) ||
          !["sectorWidth", "sectorHeight"].every(key => num(room[key])) ||
          !list(room.images, 64) || !room.images.every(image => obj(image) && str(image.asset) &&
            ["x", "y", "width", "height"].every(key => num(image[key]))) ||
          !list(room.walkablePolygons, 256) || !room.walkablePolygons.every(polygon) ||
          !list(room.blockedPolygons, 256) || !room.blockedPolygons.every(polygon) ||
          !list(room.playerSpawns, 32) || !room.playerSpawns.every(location) ||
          !list(room.entryPoints, 256) || !room.entryPoints.every(location) || !str(room.defaultEntryPointId) ||
          !list(room.warpZones, 256) || !room.warpZones.every(zone => obj(zone) && str(zone.id) &&
            polygon(zone.polygon) && ["connectionId", "targetRoomId", "targetEntryPointId"].every(key => str(zone[key])) &&
            ["", ...SIDES].includes(zone.direction) && rectangle(zone.visual) && ["gate", "pad"].includes(zone.visual.kind) && str(zone.visual.asset)) ||
          !list(room.objects, 256) || !room.objects.every(item => rectangle(item) && ["id", "name", "type", "asset"].every(key => str(item[key])) &&
            obj(item.properties) && JSON.stringify(item.properties).length <= 20000)) fail();
      delete room.walkSpeed; delete room.runSpeed;
    }
    if (!document.connections.every(connection => obj(connection) &&
        ["id", "fromRoomId", "toRoomId"].every(key => str(connection[key])) && typeof connection.bidirectional === "boolean")) fail();
    let totalBytes = 0;
    for (const asset of document.assets) {
      if (!obj(asset) || !str(asset.asset) || !ASSET.test(asset.asset) || !str(asset.name) ||
          !str(asset.mime) || typeof asset.dataUrl !== "string" || asset.dataUrl.length > 22000000 ||
          !num(asset.width) || !num(asset.height)) fail();
      const data = DATA.exec(asset.dataUrl);
      if (!data || data[1] !== asset.mime || !data[2].length || data[2].length % 4 !== 0 ||
          asset.width <= 0 || asset.height <= 0 || asset.width > 8192 || asset.height > 8192) fail();
      totalBytes += asset.dataUrl.length;
    }
    if (totalBytes > 100000000) throw new Error("이미지를 포함한 작업 파일은 100MB 이하여야 합니다.");
    Object.defineProperty(document, "wasMigrated", { value: migrated, enumerable: false });
    return document;
  }
  const cross = (a, b, c) => (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  function onSegment(p, a, b) {
    return Math.abs(cross(a, b, p)) < EPS && p.x >= Math.min(a.x, b.x) - EPS &&
      p.x <= Math.max(a.x, b.x) + EPS && p.y >= Math.min(a.y, b.y) - EPS && p.y <= Math.max(a.y, b.y) + EPS;
  }
  function contains(p, polygon) {
    let inside = false;
    for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
      const a = polygon[j], b = polygon[i];
      if (onSegment(p, a, b)) return true;
      if ((a.y > p.y) !== (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
  }
  function intersects(a, b, c, d) {
    const u = cross(a, b, c), v = cross(a, b, d), w = cross(c, d, a), z = cross(c, d, b);
    return (u * v < -EPS && w * z < -EPS) || onSegment(c, a, b) || onSegment(d, a, b) ||
      onSegment(a, c, d) || onSegment(b, c, d);
  }
  function polygonProblem(polygon) {
    if (polygon.length < 3 || polygon.length > 64) return "꼭짓점은 3~64개여야 합니다.";
    let area = 0;
    for (let i = 0; i < polygon.length; ++i) {
      const a = polygon[i], b = polygon[(i + 1) % polygon.length];
      if (Math.hypot(a.x - b.x, a.y - b.y) < EPS) return "중복된 꼭짓점이 있습니다.";
      area += a.x * b.y - b.x * a.y;
      const c = polygon[(i + 2) % polygon.length];
      if (Math.abs(cross(a, b, c)) < EPS && (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) < 0)
        return "인접한 변이 역방향으로 겹칩니다.";
      for (let j = i + 2; j < polygon.length; ++j) {
        if (i === 0 && j === polygon.length - 1) continue;
        if (intersects(a, b, polygon[j], polygon[(j + 1) % polygon.length])) return "영역의 변이 서로 교차합니다.";
      }
    }
    return Math.abs(area) < EPS ? "영역의 넓이가 0입니다." : "";
  }
  const inWorld = (p, world) => p.x >= world.left && p.x <= world.right && p.y >= world.top && p.y <= world.bottom;
  function polygonBounds(polygon) {
    if (!polygon.length) return { x: 0, y: 0, width: 0, height: 0 };
    const x = Math.min(...polygon.map(p => p.x)), y = Math.min(...polygon.map(p => p.y));
    return { x, y, width: Math.max(...polygon.map(p => p.x)) - x, height: Math.max(...polygon.map(p => p.y)) - y };
  }
  function createVisual(polygon) {
    const box = polygonBounds(polygon);
    const center = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
    const point = contains(center, polygon) ? center : polygon[0] || center;
    const width = Math.max(24, Math.min(96, box.width)), height = Math.max(24, Math.min(64, box.height));
    return { kind: "pad", asset: "", x: point.x - width / 2, y: point.y - height / 2, width, height };
  }
  // Boundary bands allow a reachable foot position inside the room, rather than on the outer line.
  function nearSide(room, point, side) {
    const w = room.world, dx = Math.min(128, (w.right - w.left) * 0.2), dy = Math.min(128, (w.bottom - w.top) * 0.2);
    return inWorld(point, w) && ({ north: point.y <= w.top + dy, south: point.y >= w.bottom - dy,
      west: point.x <= w.left + dx, east: point.x >= w.right - dx })[side] === true;
  }
  function inferSide(room, polygon) {
    const matches = SIDES.filter(side => polygon.length && polygon.every(p => nearSide(room, p, side)));
    return matches.length === 1 ? matches[0] : "";
  }
  function connectionSide(from, to) {
    if (!from || !to) return "";
    const dx = to.layout.x - from.layout.x, dy = to.layout.y - from.layout.y;
    if (dx === 1 && dy === 0) return "east";
    if (dx === -1 && dy === 0) return "west";
    if (dx === 0 && dy === 1) return "south";
    if (dx === 0 && dy === -1) return "north";
    return "";
  }
  function usedAssets(document) {
    return new Set(document.rooms.flatMap(room => [...room.images.map(image => image.asset),
      ...room.warpZones.map(zone => zone.visual.asset), ...room.objects.map(item => item.asset)]).filter(Boolean));
  }
  // Transform the foot ellipse (32 x 18) to a unit circle and measure each edge.
  function edgeDistance(p, polygon) {
    let distance = Infinity;
    for (let i = 0; i < polygon.length; ++i) {
      const a = polygon[i], b = polygon[(i + 1) % polygon.length];
      const ax = (a.x - p.x) / 32, ay = (a.y - p.y) / 18;
      const dx = (b.x - a.x) / 32, dy = (b.y - a.y) / 18;
      const length = dx * dx + dy * dy;
      const t = length ? Math.max(0, Math.min(1, -(ax * dx + ay * dy) / length)) : 0;
      distance = Math.min(distance, Math.hypot(ax + t * dx, ay + t * dy));
    }
    return distance;
  }
  function movable(room, p) {
    const w = room.world;
    if (p.x - 32 < w.left || p.x + 32 > w.right || p.y - 18 < w.top || p.y + 18 > w.bottom) return false;
    return room.walkablePolygons.some(poly => contains(p, poly) && edgeDistance(p, poly) >= 1 - EPS) &&
      !room.blockedPolygons.some(poly => contains(p, poly) || edgeDistance(p, poly) <= 1 + EPS);
  }
  function warpCandidates(polygon) {
    const points = [];
    if (!polygon.length) return points;
    points.push({ x: polygon.reduce((sum, p) => sum + p.x, 0) / polygon.length,
      y: polygon.reduce((sum, p) => sum + p.y, 0) / polygon.length });
    for (let i = 0; i < polygon.length; ++i) {
      const a = polygon[i], b = polygon[(i + 1) % polygon.length];
      points.push(a, { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 });
    }
    const minX = Math.min(...polygon.map(p => p.x)), maxX = Math.max(...polygon.map(p => p.x));
    const minY = Math.min(...polygon.map(p => p.y)), maxY = Math.max(...polygon.map(p => p.y));
    for (let x = 0; x <= 8; ++x) for (let y = 0; y <= 8; ++y)
      points.push({ x: minX + (maxX - minX) * x / 8, y: minY + (maxY - minY) * y / 8 });
    return points;
  }
  function validate(document) {
    const issues = [];
    const add = (message, roomId = "", severity = "error") => issues.push({ message, roomId, severity });
    const checkIds = (values, label, roomId = "") => {
      const seen = new Set();
      for (const value of values) {
        if (!ID.test(value.id) || seen.has(value.id.toLowerCase())) add(label + ": ID는 영문자로 시작하는 고유한 영문·숫자·_- (64자 이하)여야 합니다. 대소문자만 다른 ID는 중복입니다.", roomId);
        seen.add(value.id.toLowerCase());
      }
    };
    if (!ID.test(document.dungeonId)) add("던전 ID 형식이 올바르지 않습니다.");
    if (!document.name.trim()) add("던전 이름을 입력하세요.");
    if (!Number.isInteger(document.maxPlayers) || document.maxPlayers < 1 || document.maxPlayers > 32) add("최대 인원은 1~32명이어야 합니다.");
    if (!document.rooms.length) add("방을 하나 이상 추가하세요.");
    if (document.rooms.length > 256 || document.connections.length > 1024) add("방 256개 / 연결 1024개 제한을 초과했습니다.");
    checkIds(document.rooms, "방");
    checkIds(document.connections, "연결");
    const rooms = new Map(document.rooms.map(room => [room.id, room]));
    const connections = new Map(document.connections.map(connection => [connection.id, connection]));
    const assets = new Map(document.assets.map(asset => [asset.asset, asset]));
    if (new Set(document.assets.map(asset => asset.asset.toLowerCase())).size !== document.assets.length) add("이미지 파일 경로가 중복됩니다.");
    if (!rooms.has(document.entryRoomId)) add("최초 입장 방을 지정하세요.");
    const layouts = new Set();
    for (const room of document.rooms) {
      const label = room.name + " (" + room.id + ") · ";
      const error = message => add(label + message, room.id);
      if (!ID.test(document.dungeonId + "_" + room.id)) error("던전 ID와 방 ID를 합친 맵 ID가 64자를 초과합니다.");
      if (!room.name.trim()) error("이름을 입력하세요.");
      const grid = room.layout.x + "," + room.layout.y;
      if (!Number.isInteger(room.layout.x) || !Number.isInteger(room.layout.y) ||
          Math.abs(room.layout.x) > 1000 || Math.abs(room.layout.y) > 1000) error("미니맵 위치는 -1000~1000의 정수여야 합니다.");
      if (layouts.has(grid)) error("다른 방과 미니맵 위치가 겹칩니다.");
      layouts.add(grid);
      const w = room.world;
      if (w.right <= w.left || w.bottom <= w.top || w.right - w.left > 100000 || w.bottom - w.top > 100000) error("월드 영역 크기가 올바르지 않습니다.");
      if (room.sectorWidth <= 0 || room.sectorHeight <= 0) error("섹터 크기가 올바르지 않습니다.");
      if (Math.ceil((w.right - w.left) / room.sectorWidth) * Math.ceil((w.bottom - w.top) / room.sectorHeight) > 65536)
        error("섹터 수가 65536개를 초과합니다. 섹터 크기를 늘리세요.");
      if (!room.images.length) error("배경 이미지를 추가하세요.");
      if (room.images.length > 64 || room.walkablePolygons.length > 256 || room.blockedPolygons.length > 256 ||
          room.entryPoints.length > 256 || room.warpZones.length > 256 || room.playerSpawns.length > 32 || room.objects.length > 256) error("방의 요소 수 제한을 초과했습니다.");
      for (const image of room.images) {
        if (!assets.has(image.asset)) error("배경 이미지 파일을 찾을 수 없습니다: " + image.asset);
        if (image.width <= 0 || image.height <= 0 || image.width > 100000 || image.height > 100000) error("이미지 표시 크기가 올바르지 않습니다.");
      }
      if (!room.walkablePolygons.length) error("이동 가능 영역을 그리세요.");
      let valid = true;
      for (const [type, polygons] of [["이동 가능", room.walkablePolygons], ["진입 불가", room.blockedPolygons], ["워프존", room.warpZones.map(zone => zone.polygon)]]) {
        polygons.forEach((polygon, index) => {
          const problem = polygonProblem(polygon);
          if (problem) { error(type + " " + (index + 1) + ": " + problem); valid = false; }
          if (polygon.some(p => !inWorld(p, w))) { error(type + " " + (index + 1) + ": 월드 영역 밖의 점이 있습니다."); valid = false; }
        });
      }
      checkIds(room.entryPoints, label + "도착점", room.id);
      const vertices = [...room.walkablePolygons, ...room.blockedPolygons, ...room.warpZones.map(zone => zone.polygon)]
        .reduce((sum, polygon) => sum + polygon.length, 0);
      if (vertices > 32768) error("방의 전체 꼭짓점 수가 맵 형식 제한(32768개)을 초과했습니다.");
      checkIds(room.playerSpawns, label + "입장 위치", room.id);
      checkIds(room.warpZones, label + "워프존", room.id);
      checkIds(room.objects, label + "특수 오브젝트", room.id);
      const checkVisual = (visual, name) => {
        if (visual.width <= 0 || visual.height <= 0 || visual.width > 100000 || visual.height > 100000 ||
            !inWorld(visual, w) || !inWorld({ x: visual.x + visual.width, y: visual.y + visual.height }, w))
          error(name + ": 표시 이미지의 크기 또는 월드 내 배치가 올바르지 않습니다.");
        if (visual.asset && !assets.has(visual.asset)) error(name + ": 표시 이미지 파일을 찾을 수 없습니다.");
      };
      for (const item of room.objects) {
        checkVisual(item, item.id);
        if (!ID.test(item.type) || !item.name.trim()) error(item.id + ": 오브젝트 종류와 이름을 입력하세요.");
      }
      if (!room.entryPoints.some(entry => entry.id === room.defaultEntryPointId)) error("기본 워프 도착점을 지정하세요.");
      for (const point of [...room.entryPoints, ...room.playerSpawns])
        if (valid && !movable(room, point.position)) error(point.id + ": 발 영역(가로 반경 32, 세로 반경 18)이 이동 가능한 지형 안에 있어야 합니다.");
      if (room.id === document.entryRoomId && room.playerSpawns.length < document.maxPlayers) error("최대 인원만큼 최초 입장 위치를 지정하세요.");
      for (let i = 0; i < room.playerSpawns.length; ++i) for (let j = i + 1; j < room.playerSpawns.length; ++j) {
        const a = room.playerSpawns[i].position, b = room.playerSpawns[j].position;
        if (Math.hypot((a.x - b.x) / 64, (a.y - b.y) / 36) < 1) error("입장 위치 " + (i + 1) + " / " + (j + 1) + "의 발 영역이 겹칩니다.");
      }
      for (const zone of room.warpZones) {
        const connection = connections.get(zone.connectionId);
        if (!connection || !((connection.fromRoomId === room.id && connection.toRoomId === zone.targetRoomId) ||
            (connection.bidirectional && connection.toRoomId === room.id && connection.fromRoomId === zone.targetRoomId))) error(zone.id + ": 연결 방향과 워프 목적지가 일치하지 않습니다.");
        const target = rooms.get(zone.targetRoomId);
        const entry = target?.entryPoints.find(entry => entry.id === zone.targetEntryPointId);
        if (!target || !entry) error(zone.id + ": 목적지 방 또는 도착점이 없습니다.");
        const expected = connectionSide(room, target);
        if (!expected || zone.direction !== expected) error(zone.id + ": 게이트 방향이 미니맵의 목적지 방향과 다릅니다. 목적지 방을 상하좌우 한 칸에 배치하고 출구 방향을 맞추세요.");
        if (!SIDES.includes(zone.direction) || !zone.polygon.every(p => nearSide(room, p, zone.direction)))
          error(zone.id + ": 워프 영역을 지정한 출구 방향의 경계 구간 안에 배치하세요.");
        if (entry && expected && !nearSide(target, entry.position, OPPOSITE[expected]))
          error(zone.id + ": 목적지 도착점은 반대편 입구 경계에 있어야 합니다.");
        checkVisual(zone.visual, zone.id);
        if (!contains({ x: zone.visual.x + zone.visual.width / 2, y: zone.visual.y + zone.visual.height / 2 }, zone.polygon))
          error(zone.id + ": 게이트·발판 이미지의 중심이 워프 영역 밖에 있습니다.");
        if (valid && !warpCandidates(zone.polygon).some(p => contains(p, zone.polygon) && movable(room, p)))
          error(zone.id + ": 이동 가능한 워프 접촉 지점을 찾지 못했습니다. 영역을 넓히거나 지형 안으로 옮기세요.");
      }
      for (const entry of [...room.entryPoints, ...room.playerSpawns]) if (room.warpZones.some(zone => contains(entry.position, zone.polygon)))
        add(label + entry.id + ": 도착 위치가 워프존 안입니다. 즉시 재이동을 막으려면 도착점을 밖에 두세요.", room.id, "warning");
    }
    const pairs = new Set();
    for (const connection of document.connections) {
      const from = rooms.get(connection.fromRoomId), to = rooms.get(connection.toRoomId);
      const pair = [connection.fromRoomId, connection.toRoomId].sort().join("|");
      if (!from || !to || from === to || pairs.has(pair)) { add(connection.id + ": 없는 방, 자기 자신 또는 중복 연결입니다."); continue; }
      pairs.add(pair);
      if (!connectionSide(from, to)) add(connection.id + ": 연결된 방은 미니맵에서 상하좌우 한 칸씩 인접해야 합니다.", from.id);
      const outbound = (a, b) => a.warpZones.some(zone => zone.connectionId === connection.id && zone.targetRoomId === b.id);
      if (!outbound(from, to)) add(connection.id + ": " + from.id + " → " + to.id + " 워프존이 필요합니다.", from.id);
      if (connection.bidirectional && !outbound(to, from)) add(connection.id + ": " + to.id + " → " + from.id + " 워프존이 필요합니다.", to.id);
    }
    const reached = new Set([document.entryRoomId]);
    const queue = [document.entryRoomId];
    while (queue.length) {
      const id = queue.shift();
      for (const connection of document.connections) {
        const next = connection.fromRoomId === id ? connection.toRoomId : connection.bidirectional && connection.toRoomId === id ? connection.fromRoomId : null;
        if (next !== null && !reached.has(next)) { reached.add(next); queue.push(next); }
      }
    }
    for (const room of document.rooms) if (!reached.has(room.id)) add(room.id + ": 입장 방에서 연결을 따라 도달할 수 없습니다.", room.id);
    if (minimap(document).scale < 0.25) add("미니맵 배치 간격이 너무 넓습니다. 방을 가까이 배치하세요.", "", "warning");
    return issues;
  }
  // The same primitives render editor icons, exported PNGs and SVGs.
  function iconShapes(kind) {
    const rect = (x, y, width, height, fill, stroke = "", lineWidth = 4) => ({ type: "rect", x, y, width, height, fill, stroke, lineWidth });
    const ellipse = (x, y, rx, ry, fill, stroke = "", lineWidth = 4) => ({ type: "ellipse", x, y, rx, ry, fill, stroke, lineWidth });
    const polygon = (points, fill, stroke = "", lineWidth = 4) => ({ type: "polygon", points, fill, stroke, lineWidth });
    if (kind === "boss") return [
      rect(6, 6, 88, 88, "#49351c", "#ffd36d", 6),
      ellipse(50, 43, 27, 25, "#ffd36d"), rect(34, 55, 32, 23, "#ffd36d"),
      ellipse(39, 44, 6, 8, "#49351c"), ellipse(61, 44, 6, 8, "#49351c"),
      polygon([[50, 51], [45, 60], [55, 60]], "#49351c"),
      rect(41, 68, 4, 10, "#49351c"), rect(55, 68, 4, 10, "#49351c") ];
    if (kind === "gate") return [
      rect(12, 6, 76, 88, "#133342", "#7bdde8", 5), rect(25, 20, 50, 65, "#285a6e"),
      polygon([[50, 28], [72, 50], [50, 72], [28, 50]], "#ffe3a4"),
      rect(5, 90, 90, 8, "#7bdde8") ];
    if (kind === "pad") return [
      ellipse(50, 65, 44, 22, "#254d6c", "#96d8ef", 5),
      ellipse(50, 52, 40, 19, "#6caaad", "#d8f4ec", 4),
      polygon([[50, 38], [69, 53], [50, 68], [31, 53]], "#ffe3a4") ];
    if (kind === "object") return [
      polygon([[50, 8], [92, 32], [92, 76], [50, 98], [8, 76], [8, 32]], "#394659", "#c6d1e2", 4),
      polygon([[50, 24], [73, 50], [50, 76], [27, 50]], "#bd9b60"),
      ellipse(50, 50, 8, 8, "#fff0c4") ];
    return [rect(6, 6, 88, 88, "#243e53", "#bbd4e3", 6), rect(24, 24, 52, 52, "#162b3b")];
  }
  function paintIcon(context, kind, x, y, width, height) {
    context.save(); context.translate(x, y); context.scale(width / 100, height / 100);
    for (const shape of iconShapes(kind)) {
      context.beginPath();
      if (shape.type === "rect") context.rect(shape.x, shape.y, shape.width, shape.height);
      else if (shape.type === "ellipse") context.ellipse(shape.x, shape.y, shape.rx, shape.ry, 0, 0, Math.PI * 2);
      else { shape.points.forEach((point, index) => index ? context.lineTo(...point) : context.moveTo(...point)); context.closePath(); }
      if (shape.fill) { context.fillStyle = shape.fill; context.fill(); }
      if (shape.stroke) { context.strokeStyle = shape.stroke; context.lineWidth = shape.lineWidth; context.stroke(); }
    }
    context.restore();
  }
  function iconMarkup(kind) {
    return iconShapes(kind).map(shape => {
      const style = ' fill="' + (shape.fill || "none") + '" stroke="' + (shape.stroke || "none") + '" stroke-width="' + shape.lineWidth + '"';
      if (shape.type === "rect") return '<rect x="' + shape.x + '" y="' + shape.y + '" width="' + shape.width + '" height="' + shape.height + '"' + style + '/>';
      if (shape.type === "ellipse") return '<ellipse cx="' + shape.x + '" cy="' + shape.y + '" rx="' + shape.rx + '" ry="' + shape.ry + '"' + style + '/>';
      return '<polygon points="' + shape.points.map(point => point.join(",")).join(" ") + '"' + style + '/>';
    }).join("");
  }
  const iconSvg = kind => '<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 100 100">' + iconMarkup(kind) + '</svg>';
  function withPorts(map, document) {
    const nodes = new Map(map.rooms.map(room => [room.id, room]));
    const rooms = new Map(document.rooms.map(room => [room.id, room]));
    const port = (node, side) => ({ x: side === "west" ? node.x : side === "east" ? node.x + node.width : node.center.x,
      y: side === "north" ? node.y : side === "south" ? node.y + node.height : node.center.y });
    map.connections = map.connections.filter(c => nodes.has(c.fromRoomId) && nodes.has(c.toRoomId)).map(c => {
      const side = connectionSide(rooms.get(c.fromRoomId), rooms.get(c.toRoomId));
      const style = id => rooms.get(id)?.warpZones.find(zone => zone.connectionId === c.id)?.visual.kind || "pad";
      return { ...c, fromSide: side, toSide: OPPOSITE[side] || "", validLayout: !!side,
        fromPort: port(nodes.get(c.fromRoomId), side), toPort: port(nodes.get(c.toRoomId), OPPOSITE[side]),
        fromStyle: style(c.fromRoomId), toStyle: style(c.toRoomId) };
    });
    return map;
  }
  function minimap(document) {
    if (!document.rooms.length) return { width: 240, height: 150, scale: 1, rooms: [], connections: [] };
    const xs = document.rooms.map(room => room.layout.x * 72), ys = document.rooms.map(room => room.layout.y * 72);
    const left = Math.min(...xs) - 20, top = Math.min(...ys) - 20;
    const width = Math.max(...xs) - left + 56, height = Math.max(...ys) - top + 56;
    const scale = Math.min(1, 4096 / Math.max(width, height));
    const rooms = document.rooms.map(room => ({ id: room.id, name: room.name, kind: room.kind,
      isEntry: room.id === document.entryRoomId, x: (room.layout.x * 72 - left) * scale,
      y: (room.layout.y * 72 - top) * scale, width: 36 * scale, height: 36 * scale,
      icon: room.kind === "boss" ? "boss" : "normal",
      center: { x: (room.layout.x * 72 - left + 18) * scale, y: (room.layout.y * 72 - top + 18) * scale } }));
    return withPorts({ width: Math.ceil(width * scale), height: Math.ceil(height * scale), scale, rooms,
      connections: document.connections }, document);
  }
  function svg(map) {
    const s = map.scale;
    const out = ['<svg xmlns="http://www.w3.org/2000/svg" width="' + map.width + '" height="' + map.height + '" viewBox="0 0 ' + map.width + ' ' + map.height + '">'];
    for (const connection of map.connections) {
      const a = connection.fromPort, b = connection.toPort;
      out.push('<path d="M' + a.x + ',' + a.y + ' L' + b.x + ',' + b.y + '" fill="none" stroke="#8ba4b8" stroke-width="' + 3 * s + '"/>');
      if (!connection.bidirectional) {
        const dx = b.x - a.x, dy = b.y - a.y, length = Math.hypot(dx, dy);
        if (length) {
          const x = (a.x + b.x) / 2, y = (a.y + b.y) / 2, ux = dx / length * 8 * s, uy = dy / length * 8 * s;
          out.push('<path d="M' + (x - ux - uy / 2) + ',' + (y - uy + ux / 2) + ' L' + (x + ux) + ',' + (y + uy) + ' L' + (x - ux + uy / 2) + ',' + (y - uy - ux / 2) + '" fill="none" stroke="#d8e5ee" stroke-width="' + 2 * s + '"/>');
        }
      }
    }
    for (const room of map.rooms) {
      out.push('<g transform="translate(' + room.x + ' ' + room.y + ') scale(' + room.width / 100 + ' ' + room.height / 100 + ')">' + iconMarkup(room.kind === "boss" ? "boss" : "normal") + '</g>');
      if (room.isEntry) out.push('<circle cx="' + room.center.x + '" cy="' + (room.y + room.height - 3 * s) + '" r="' + 3 * s + '" fill="#56e3a0"/>');
    }
    for (const connection of map.connections) for (const end of ["from", "to"]) {
      const point = connection[end + "Port"], fill = connection[end + "Style"] === "gate" ? "#ffe3a4" : "#96d8ef";
      out.push('<circle cx="' + point.x + '" cy="' + point.y + '" r="' + 2 * s + '" fill="' + fill + '"/>');
    }
    return out.join("") + "</svg>";
  }
  const builtinAssetPath = (document, kind) => "Images/Dungeons/" + document.dungeonId + "_Editor_" + kind + ".png";
  function runtimeFiles(document, map) {
    const mapId = roomId => document.dungeonId + "_" + roomId;
    const assetPath = path => "Images/Dungeons/" + document.dungeonId + "_" + path.split("/").pop();
    const visual = (value, kind) => ({ ...value, asset: value.asset ? assetPath(value.asset) : builtinAssetPath(document, kind) });
    const manifest = { version: 2, dungeonId: document.dungeonId, name: document.name, maxPlayers: document.maxPlayers,
      entryRoomId: document.entryRoomId, entryMapId: mapId(document.entryRoomId),
      rooms: document.rooms.map(room => ({ id: room.id, name: room.name, kind: room.kind, mapId: mapId(room.id),
        mapPath: "Maps/" + mapId(room.id) + ".json", layout: clone(room.layout), playerSpawns: clone(room.playerSpawns) })),
      connections: map.connections.map(connection => ({ ...connection, gates: document.rooms.flatMap(room => room.warpZones
        .filter(zone => zone.connectionId === connection.id).map(zone => ({ roomId: room.id, warpId: zone.id, direction: zone.direction, style: zone.visual.kind }))) })),
      minimap: { svg: "Minimap.svg", png: "Minimap.png", icons: { normal: "Icons/normal.png", boss: "Icons/boss.png" }, ...map } };
    const files = [{ name: "Dungeon.json", data: JSON.stringify(manifest, null, 2) + "\n" }, { name: "Minimap.svg", data: svg(map) },
      { name: "Icons/normal.svg", data: iconSvg("normal") }, { name: "Icons/boss.svg", data: iconSvg("boss") }];
    for (const room of document.rooms) {
      const entry = room.entryPoints.find(point => point.id === room.defaultEntryPointId);
      const gates = room.warpZones.map(zone => ({ ...zone, visual: visual(zone.visual, zone.visual.kind) }));
      const objects = room.objects.map(item => visual(item, "object"));
      const decorations = [...objects, ...gates.map(zone => zone.visual)].map(item => ({
        asset: item.asset, x: item.x, y: item.y, width: item.width, height: item.height }));
      files.push({ name: "Maps/" + mapId(room.id) + ".json", data: JSON.stringify({ version: 4, format: "DungeonRoom", mapId: mapId(room.id),
        world: room.world, sectorWidth: room.sectorWidth, sectorHeight: room.sectorHeight,
        spawn: room.id === document.entryRoomId ? room.playerSpawns[0].position : entry.position,
        images: [...room.images.map(image => ({ ...image, asset: assetPath(image.asset) })), ...decorations],
        walkablePolygons: room.walkablePolygons, blockedPolygons: room.blockedPolygons, entryPoints: room.entryPoints,
        objects, transitionZones: gates.map(zone => ({ id: zone.id, polygon: zone.polygon, connectionId: zone.connectionId,
          direction: zone.direction, visual: zone.visual,
          action: { type: "MapTransfer", targetMapId: mapId(zone.targetRoomId), targetEntryPointId: zone.targetEntryPointId } })) }, null, 2) + "\n" });
    }
    const used = usedAssets(document);
    for (const asset of document.assets.filter(asset => used.has(asset.asset))) {
      const raw = atob(asset.dataUrl.substring(asset.dataUrl.indexOf(",") + 1));
      const bytes = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; ++i) bytes[i] = raw.charCodeAt(i);
      files.push({ name: "Assets/" + assetPath(asset.asset), data: bytes });
    }
    return files;
  }
  return { createDocument, createRoom, clone, unique, parse, contains, polygonProblem, movable, validate, minimap, svg, runtimeFiles,
    createVisual, inferSide, connectionSide, nearSide, polygonBounds, usedAssets, withPorts, paintIcon, iconSvg, builtinAssetPath, OPPOSITE };
})();
