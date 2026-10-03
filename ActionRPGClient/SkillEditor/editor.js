(function () {
  "use strict";
  const M = window.PlayerSkillModel, C = window.CharacterEditorModel, $ = id => document.getElementById(id);
  const canvas = $("canvas"), ctx = canvas.getContext("2d");
  let project = M.NewProject(), images = Object.create(null), selectedId = "", frame = 0;
  let dirty = false, busy = false, playing = false, elapsed = 0, lastTime = 0, drag = null, transform = null;
  let undo = [], redo = [], saveHandle = null;
  const MAX_JSON = 145 * 1024 * 1024, MAX_IMAGE = 15 * 1024 * 1024, MAX_IMAGES = 100 * 1024 * 1024;
  const skill = () => project.skills.find(value => value.id === selectedId);
  const mode = () => $("mode").value;
  const variant = () => skill()?.[mode()];
  const motion = () => skill() && M.Motion(project, skill(), variant());
  const rect = () => variant()?.attackRects?.[frame];
  const tell = text => { $("status").textContent = text; };
  const changed = () => { dirty = true; $("dirty").textContent = "● 미저장 변경"; };
  const pause = () => { playing = false; $("play").textContent = "▶ 모션 재생"; };
  function Remember() { undo.push(M.clone(project.skills)); if (undo.length > 30) undo.shift(); redo = []; }
  function Edit(action) { if (busy || !skill()) return; pause(); Remember(); action(); changed(); Refresh(); }
  function Read(object, path) { return path.split(".").reduce((value, key) => value?.[key], object); }
  function Write(object, path, value) {
    const keys = path.split("."); const final = keys.pop();
    const target = keys.reduce((value, key) => value[key], object); target[final] = value;
  }
  function Options(select, entries, value) {
    select.replaceChildren();
    for (const [key, label] of entries) { const option = new Option(label, key); select.add(option); }
    select.value = value;
  }
  function Refresh() {
    const current = skill(), v = variant(), m = motion();
    const character = current?.characterId || $("character").value || project.characters[0]?.id;
    Options($("character"), project.characters.map(value => [value.id, value.id + " · ID " + value.dataId]), character);
    Options($("skills"), project.skills.filter(value => value.characterId === character).map(value => [value.id, value.name + " · " + value.type]), selectedId);
    $("common").hidden = !current;
    $("variantPanel").hidden = !current;
    $("new").disabled = busy || !character || !project.animations?.characters[character];
    for (const name of ["duplicate", "delete"]) $(name).disabled = busy || !current;
    $("enabled").checked = !!v; $("enabled").disabled = busy || !current;
    $("undo").disabled = busy || !undo.length; $("redo").disabled = busy || !redo.length;
    document.querySelectorAll("[data-type]").forEach(section => { section.hidden = !current || !section.dataset.type.split(" ").includes(current.type); });
    for (const element of document.querySelectorAll("[data-bind],[data-variant],[data-effect]")) {
      const scope = element.dataset.bind ? current : element.dataset.variant ? v : v?.effect;
      const path = element.dataset.bind || element.dataset.variant || element.dataset.effect;
      const value = Read(scope, path); element.disabled = busy || !scope || value === undefined;
      if (element.type === "checkbox") element.checked = !!value; else element.value = value ?? "";
    }
    $("skillId").value = current?.id || ""; $("type").value = current?.type || "direct";
    $("command").value = current?.input?.command?.join(" ") || "";
    Options($("motion"), Object.keys(project.animations?.characters[character]?.motions || {}).map(value => [value, value]), v?.motionId);
    $("motion").disabled = busy || !v;
    frame = Math.max(0, Math.min(frame, (m?.frameCount || 1) - 1));
    $("frame").max = (m?.frameCount || 1) - 1; $("frame").value = frame;
    $("frameText").textContent = "프레임 " + frame + (m ? " / " + (m.frameCount - 1) : "");
    $("effectPanel").hidden = !v?.effect; $("effectPath").textContent = v?.effect?.image || "";
    $("addEffect").disabled = busy || !v; $("clearEffect").disabled = busy || !v?.effect;
    const r = rect(); for (const [name, key] of [["rectX", "x"], ["rectY", "y"], ["rectW", "width"], ["rectH", "height"]]) $(name).value = r?.[key] ?? "";
    $("hint").textContent = !v ? "현재 모드 사용을 켜고 모션을 선택하세요."
      : current.type === "direct" ? "빈 곳에서 드래그: 공격 영역 그리기 · 내부: 이동 · 모서리: 크기 조절 · Esc: 드래그 취소"
      : current.type === "projectile" ? "드래그: 발사 위치 X·높이 설정 · 지면 Y와 방향각은 오른쪽에서 설정"
      : "버프는 자신에게 적용합니다. 이 화면은 모션·이펙트 미리보기입니다.";
    if (current?.type === "projectile" && v) {
      const velocity = M.Velocity(v, $("flip").checked, current.execution.speed);
      $("velocity").textContent = "축별 속도 X " + velocity.x.toFixed(1) + " / 지면 Y " + velocity.y.toFixed(1) + " / 높이 " + velocity.height.toFixed(1);
    }
    Draw();
  }
  async function Task(action) {
    if (busy) return; if (drag) CancelDrag(); pause(); busy = true; document.body.classList.add("busy");
    const controls = [...document.querySelectorAll("button,input,select")]; controls.forEach(element => { element.disabled = true; });
    try { await action(); } catch (error) { if (error.name !== "AbortError") tell("오류: " + error.message); }
    finally { busy = false; document.body.classList.remove("busy"); controls.forEach(element => { element.disabled = false; }); Refresh(); }
  }
  const pathOf = file => (file.webkitRelativePath || file.name).replace(/\\/g, "/");
  function Find(files, path) {
    const matches = files.filter(file => pathOf(file) === path || pathOf(file).endsWith("/" + path));
    if (matches.length !== 1) throw new Error(path + ": 누락되었거나 후보가 여러 개입니다."); return matches[0];
  }
  async function Json(file, limit = MAX_JSON) {
    if (file.size > limit) throw new Error("JSON 크기 제한 초과"); return JSON.parse((await file.text()).replace(/^\uFEFF/, ""));
  }
  async function Decode(dataUrl) {
    return await new Promise((resolve, reject) => {
      const image = new Image(); image.onload = () => {
        if (!image.naturalWidth || image.naturalWidth > 8192 || image.naturalHeight > 8192) reject(new Error("이미지 크기 제한 8192px 초과"));
        else resolve({ image, dataUrl, width: image.naturalWidth, height: image.naturalHeight });
      }; image.onerror = () => reject(new Error("이미지 해석 실패")); image.src = dataUrl;
    });
  }
  async function ImageFile(file) {
    if (file.size > MAX_IMAGE || !/\.(png|jpe?g|webp)$/i.test(file.name)) throw new Error("15MB 이하 PNG/JPEG/WebP가 필요합니다.");
    const dataUrl = await new Promise((resolve, reject) => {
      const reader = new FileReader(); reader.onload = () => resolve(reader.result); reader.onerror = () => reject(new Error("이미지 읽기 실패"));
      const mime = /\.png$/i.test(file.name) ? "image/png" : /\.webp$/i.test(file.name) ? "image/webp" : "image/jpeg";
      reader.readAsDataURL(new Blob([file], { type: mime }));
    }); return await Decode(dataUrl);
  }
  function ImageBudget(next) {
    const total = Object.values(next).reduce((sum, value) => sum + value.dataUrl.length, 0);
    if (total > 140 * 1024 * 1024) throw new Error("이미지 저장 총량 140MB 초과");
    if (Object.values(next).reduce((sum, value) => sum + value.width * value.height, 0) > 64 * 1024 * 1024)
      throw new Error("이미지 디코딩 총량 64M 픽셀 초과");
  }
  async function Save(blob, filename, working = false) {
    if (window.showSaveFilePicker) {
      const handle = working && saveHandle ? saveHandle : await window.showSaveFilePicker({ suggestedName: filename });
      const stream = await handle.createWritable(); await stream.write(blob); await stream.close();
      if (working) { saveHandle = handle; dirty = false; $("dirty").textContent = ""; }
      tell("저장 완료: " + filename);
    } else {
      const url = URL.createObjectURL(blob), link = document.createElement("a"); link.href = url; link.download = filename; link.click();
      setTimeout(() => URL.revokeObjectURL(url), 30000); tell("다운로드 요청: " + filename + " · 저장 완료 여부는 브라우저에서 확인하세요.");
    }
  }
  const jsonBlob = value => new Blob([JSON.stringify(value, null, 2)], { type: "application/json" });
  function Bytes(image) { return Uint8Array.from(atob(image.dataUrl.slice(image.dataUrl.indexOf(",") + 1)), value => value.charCodeAt(0)); }
  function SetSelection(id) { pause(); selectedId = id; frame = 0; elapsed = 0; Refresh(); }
  $("character").onchange = () => SetSelection(project.skills.find(value => value.characterId === $("character").value)?.id || "");
  $("skills").onchange = () => SetSelection($("skills").value);
  $("new").onclick = () => {
    const id = prompt("새 스킬 고유 ID (영문으로 시작, 영문·숫자·_.-)"); if (!id) return;
    try {
      if (project.skills.some(value => value.id === id)) throw new Error("이미 존재하는 스킬 ID입니다.");
      const created = M.NewSkill(project, $("character").value, $("type").value, id);
      Remember(); project.skills.push(created); changed(); SetSelection(id);
    } catch (error) { tell(error.message); }
  };
  $("duplicate").onclick = () => {
    const current = skill(); if (!current) return; const id = prompt("복제할 스킬의 새 ID"); if (!id) return;
    if (!/^[A-Za-z][A-Za-z0-9_.-]{0,63}$/.test(id) || project.skills.some(value => value.id === id)) { tell("스킬 ID 형식 또는 중복 오류"); return; }
    Remember(); const copied = M.clone(current); copied.id = id; copied.name += " 복사"; project.skills.push(copied); changed(); SetSelection(id);
  };
  $("delete").onclick = () => { if (!skill() || !confirm("현재 스킬을 삭제할까요?")) return; Remember(); project.skills = project.skills.filter(value => value.id !== selectedId); changed(); SetSelection(project.skills[0]?.id || ""); };
  $("type").onchange = () => {
    if (!skill()) return;
    if (!confirm("유형을 바꾸면 유형별 설정과 공격 영역을 초기화합니다.")) { Refresh(); return; }
    const value = $("type").value; Edit(() => M.SetType(skill(), value));
  };
  $("command").onchange = () => { const command = $("command").value.trim().split(/\s+/); Edit(() => { skill().input.command = command; }); };
  for (const element of document.querySelectorAll("[data-bind],[data-variant],[data-effect]")) element.onchange = () => {
    const scope = element.dataset.bind ? skill() : element.dataset.variant ? variant() : variant()?.effect; if (!scope) return;
    const path = element.dataset.bind || element.dataset.variant || element.dataset.effect;
    const value = element.type === "checkbox" ? element.checked : element.type === "number" ? (element.value === "" ? null : Number(element.value)) : element.value;
    Edit(() => Write(scope, path, value));
  };
  $("mode").onchange = () => { pause(); frame = 0; elapsed = 0; Refresh(); };
  $("enabled").onchange = () => {
    const enabled = $("enabled").checked, current = skill(); if (!current) return;
    Edit(() => {
      current[mode()] = enabled ? M.Variant(project, current.characterId, $("motion").value || Object.keys(project.animations.characters[current.characterId].motions)[0], current.type) : null;
      if (enabled && mode() === "air" && current.type === "projectile") current.air.pitchDegrees = -45;
    });
  };
  $("motion").onchange = () => {
    const value = $("motion").value;
    Edit(() => { variant().motionId = value; variant().eventFrame = 0; variant().endFrame = motion().frameCount - 1;
      if (skill().type === "direct") variant().attackRects = {}; frame = 0; });
  };
  for (const name of ["flip", "zoom", "previewHeight", "hurtVisible"]) $(name).onchange = Refresh;
  $("frame").oninput = () => { pause(); frame = Number($("frame").value); Refresh(); };
  $("previous").onclick = () => { pause(); frame--; Refresh(); };
  $("next").onclick = () => { pause(); frame++; Refresh(); };
  $("play").onclick = () => { if (!motion() || busy) return; playing = !playing; elapsed = frame / motion().fps; $("play").textContent = playing ? "Ⅱ 일시 정지" : "▶ 모션 재생"; };
  $("applyRect").onclick = () => Edit(() => { if (variant()?.attackRects) variant().attackRects[frame] = { x: Number($("rectX").value), y: Number($("rectY").value), width: Number($("rectW").value), height: Number($("rectH").value) }; });
  $("clearRect").onclick = () => Edit(() => { if (variant()?.attackRects) delete variant().attackRects[frame]; });
  $("copyRange").onclick = () => {
    const current = rect(), v = variant(), m = motion(); if (!current || !m) { tell("현재 프레임에 영역을 먼저 그리세요."); return; }
    if (!Number.isInteger(v.eventFrame) || !Number.isInteger(v.endFrame) || v.eventFrame < 0 || v.endFrame >= m.frameCount || v.endFrame < v.eventFrame) { tell("활성 프레임 범위를 확인하세요."); return; }
    Edit(() => { for (let index = v.eventFrame; index <= v.endFrame; ++index) v.attackRects[index] = M.clone(current); });
  };
  function Restore(from, to) { if (!from.length || busy) return; pause(); to.push(M.clone(project.skills)); project.skills = from.pop(); selectedId = project.skills.some(value => value.id === selectedId) ? selectedId : project.skills[0]?.id || ""; changed(); Refresh(); }
  $("undo").onclick = () => Restore(undo, redo); $("redo").onclick = () => Restore(redo, undo);
  $("importPlayer").onclick = () => $("playerFiles").click();
  $("playerFiles").onchange = event => {
    const files = [...event.target.files]; event.target.value = ""; if (!files.length) return;
    Task(async () => {
      const ini = async path => { const file = Find(files, path); if (file.size > 1024 * 1024) throw new Error("INI 크기 제한 초과"); return C.ParseIni(await file.text()); };
      const definitions = await ini("Data/characters.ini"), animations = await ini("Data/animations.ini"), assets = await ini("Data/assets.ini");
      const characters = M.Characters(definitions); if (!characters.length) throw new Error("등록된 플레이어 캐릭터가 없습니다.");
      const paths = new Set();
      for (const definition of Object.values(definitions)) for (const [motionId, defaultId] of C.PLAYER_BINDINGS) {
        const animation = animations[definition[motionId + "_animation"] || defaultId];
        const path = animation && assets.Images?.[animation.image]; if (!C.safePath(path)) throw new Error("플레이어 이미지 참조 오류"); paths.add(path);
      }
      const loaded = Object.create(null); let bytes = 0;
      for (const path of paths) { const file = Find(files, path); bytes += file.size; if (bytes > MAX_IMAGES) throw new Error("원본 이미지 총량 초과"); loaded[path] = await ImageFile(file); }
      const sources = files.filter(file => pathOf(file).endsWith("/Game/Player.cpp"));
      if (sources.length > 1 || sources[0]?.size > 1024 * 1024) throw new Error("Player.cpp 후보 또는 크기 오류");
      const converted = C.ConvertPlayer(definitions, animations, assets, loaded, sources.length ? await sources[0].text() : null);
      if (project.skills.length && !confirm("플레이어 원본을 교체합니다. 기존 스킬 참조는 다시 검사해야 합니다.")) return;
      ImageBudget({ ...images, ...loaded }); project.characters = characters; project.animations = converted; project.hurtRects = null;
      images = { ...images, ...loaded }; undo = []; redo = []; changed(); tell("플레이어 ID와 모션을 불러왔습니다. 몬스터는 대상에서 제외됩니다.");
    });
  };
  $("importAnimations").onclick = () => $("animationFile").click();
  $("animationFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file) return;
    Task(async () => {
      const incoming = await Json(file, 8 * 1024 * 1024); C.ValidateAnimations(incoming);
      if (!project.characters.length) throw new Error("캐릭터 ID를 읽기 위해 플레이어 데이터 폴더를 먼저 불러오세요.");
      const next = M.clone(project.animations); const allowed = new Set(project.characters.map(value => value.id)); let count = 0;
      for (const [id, value] of Object.entries(incoming.characters)) if (allowed.has(id)) { next.characters[id] = M.clone(value); count++; }
      if (!count) throw new Error("characters.ini에 등록된 플레이어 ID와 일치하는 모션이 없습니다.");
      C.ValidateAnimations(next); project.animations = next; project.hurtRects = null; undo = []; redo = []; changed(); tell("플레이어 모션을 교체했습니다. 시점·공격 영역과 이미지 참조를 검사하세요.");
    });
  };
  $("importImages").onclick = () => $("imageFiles").click();
  $("imageFiles").onchange = event => {
    const files = [...event.target.files]; event.target.value = ""; if (!files.length) return;
    Task(async () => {
      if (!project.animations) throw new Error("모션을 먼저 불러오세요.");
      const paths = new Set(C.Entries(project.animations).map(value => value.motion.image));
      const next = { ...images }; let bytes = 0;
      for (const path of paths) {
        const file = Find(files, path); bytes += file.size; if (bytes > MAX_IMAGES) throw new Error("이미지 원본 총량 초과"); next[path] = await ImageFile(file);
      }
      ImageBudget(next); images = next; changed(); tell("모션 이미지 불러오기 완료");
    });
  };
  $("importHurt").onclick = () => $("hurtFile").click();
  $("hurtFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file) return;
    Task(async () => { if (!project.animations) throw new Error("모션을 먼저 불러오세요."); project.hurtRects = C.ReadHurtRects(await Json(file, 16 * 1024 * 1024), project.animations); changed(); tell("피격 사각형을 읽기 전용 참조로 연결했습니다."); });
  };
  $("addEffect").onclick = () => $("effectFile").click();
  $("effectFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file || !variant()) return;
    Task(async () => {
      const image = await ImageFile(file); if (!/\.png$/i.test(file.name)) throw new Error("PNG 이펙트가 필요합니다.");
      const path = "Images/Skills/" + skill().id + "-" + file.name.replace(/[^A-Za-z0-9_.-]/g, "_");
      ImageBudget({ ...images, [path]: image }); Remember(); images[path] = image;
      variant().effect = { image: path, columns: 1, rows: 1, frameCount: 1, fps: 12, scale: 1,
        pivotX: 0.5, pivotY: 1, eventFrame: variant().eventFrame, offset: { x: 0, y: 0, height: 0 }, loop: false }; changed();
    });
  };
  $("clearEffect").onclick = () => Edit(() => { variant().effect = null; });
  $("open").onclick = () => $("projectFile").click();
  $("projectFile").onchange = event => {
    const file = event.target.files[0]; event.target.value = ""; if (!file || (dirty && !confirm("미저장 변경을 버리고 작업을 열까요?"))) return;
    Task(async () => {
      const incoming = M.ReadProject(await Json(file)), loaded = Object.create(null);
      for (const [path, data] of Object.entries(incoming.images)) loaded[path] = await Decode(data);
      ImageBudget(loaded);
      project = incoming; project.images = {}; images = loaded; selectedId = project.skills[0]?.id || "";
      undo = []; redo = []; saveHandle = null; dirty = false; $("dirty").textContent = ""; frame = 0; tell("작업 불러오기 완료");
    });
  };
  $("save").onclick = () => Task(async () => {
    const output = { ...project, images: Object.fromEntries(Object.entries(images).map(([path, value]) => [path, value.dataUrl])) };
    const blob = jsonBlob(output); if (blob.size > MAX_JSON) throw new Error("작업 JSON 총량 초과"); await Save(blob, "PlayerSkills.skill-project.json", true);
  });
  $("check").onclick = () => Task(async () => {
    const result = M.Check(project, images); $("report").textContent = result.errors.length ? result.errors.join("\n")
      : "검사 통과 · 스킬 " + result.skillCount + "개 · 이미지 " + result.imageCount + "개\n게임 빌드·실행·플레이 검증은 별도입니다.";
    tell(result.errors.length ? "출력 오류를 수정하세요." : "편집 데이터 검사 통과");
  });
  $("export").onclick = () => Task(async () => {
    const built = M.Build(project, images), shared = JSON.stringify(built.shared, null, 2), client = JSON.stringify(built.client, null, 2);
    if (new TextEncoder().encode(shared).length > 4 * 1024 * 1024 || new TextEncoder().encode(client).length > 16 * 1024 * 1024) throw new Error("런타임 JSON 크기 제한 초과");
    const files = [ { name: "server/Data/PlayerSkills.json", data: shared },
      { name: "client/Assets/Data/PlayerSkills.json", data: shared }, { name: "client/Assets/Data/PlayerSkillVisuals.json", data: client } ];
    for (const path of built.paths) files.push({ name: "client/Assets/" + path, data: Bytes(images[path]) });
    files.push({ name: "REPORT.json", data: JSON.stringify({ format: "PlayerSkillPackageReport", schemaVersion: 1,
      skillCount: built.shared.skills.length, files: files.map(file => file.name), serverImageCount: 0, installed: false, gameBuildOrPlayTestPerformed: false }, null, 2) });
    await Save(window.DungeonArchive.create(files), "PlayerSkills.runtime.zip"); $("report").textContent = "출력 구성\n" + files.map(file => file.name).join("\n") + "\n자동 설치하지 않습니다.";
  });

  function Screen(x, height) { return { x: transform.x + x * transform.zoom, y: transform.y - height * transform.zoom }; }
  function World(event) {
    const bounds = canvas.getBoundingClientRect(), x = (event.clientX - bounds.left) * canvas.width / bounds.width, y = (event.clientY - bounds.top) * canvas.height / bounds.height;
    return { x: (x - transform.x) / transform.zoom * ($("flip").checked ? -1 : 1), y: (transform.y - y) / transform.zoom };
  }
  function DrawMotion(value, index, x, height, flip, alpha = 1) {
    const image = images[value.image], f = value.frames.find(item => item.index === index); if (!image || !f) return;
    const scale = M.Scale(value, f), r = f.sourceRect, pivot = f.pivot, point = Screen(x, height);
    const scaleX = value.renderSize ? value.renderSize.width / r.width : scale;
    ctx.save(); ctx.globalAlpha = alpha; ctx.translate(point.x, point.y); ctx.scale((flip ? -1 : 1) * scaleX * transform.zoom, scale * transform.zoom);
    ctx.drawImage(image.image, r.x, r.y, r.width, r.height, -pivot.x, -pivot.y, r.width, r.height); ctx.restore();
  }
  function Box(value, color, actorHeight = 0, flip = false, handles = false) {
    const x = flip ? -value.x - value.width : value.x, point = Screen(x, actorHeight + value.y + value.height);
    ctx.strokeStyle = color; ctx.fillStyle = color; ctx.globalAlpha = 0.15;
    ctx.fillRect(point.x, point.y, value.width * transform.zoom, value.height * transform.zoom); ctx.globalAlpha = 1;
    ctx.lineWidth = 2; ctx.strokeRect(point.x, point.y, value.width * transform.zoom, value.height * transform.zoom);
    if (handles) for (const px of [point.x, point.x + value.width * transform.zoom]) for (const py of [point.y, point.y + value.height * transform.zoom]) ctx.fillRect(px - 4, py - 4, 8, 8);
  }
  function Draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const zoom = Number($("zoom").value); transform = { x: canvas.width * 0.4, y: canvas.height * 0.8, zoom: Number.isFinite(zoom) && zoom >= 0.25 && zoom <= 4 ? zoom : 1 };
    ctx.strokeStyle = "#202e42"; ctx.lineWidth = 1;
    for (let x = transform.x % (32 * transform.zoom); x < canvas.width; x += 32 * transform.zoom) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, canvas.height); ctx.stroke(); }
    for (let y = transform.y % (32 * transform.zoom); y < canvas.height; y += 32 * transform.zoom) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(canvas.width, y); ctx.stroke(); }
    ctx.strokeStyle = "#526984"; ctx.beginPath(); ctx.moveTo(0, transform.y); ctx.lineTo(canvas.width, transform.y); ctx.stroke();
    const current = skill(), v = variant(), m = motion(); if (!current || !v || !m) return;
    const height = mode() === "air" ? Math.max(0, Math.min(1000, Number($("previewHeight").value) || 0)) : 0, flip = $("flip").checked;
    DrawMotion(m, frame, 0, height, flip);
    if ($("hurtVisible").checked && project.hurtRects) {
      const hurt = project.hurtRects[C.key(current.characterId, v.motionId, frame)], f = m.frames.find(value => value.index === frame);
      if (hurt && f) { const scale = M.Scale(m, f), scaleX = m.renderSize ? m.renderSize.width / f.sourceRect.width : scale;
        Box({ x: (hurt.x - f.pivot.x) * scaleX, y: (f.pivot.y - hurt.y - hurt.height) * scale, width: hurt.width * scaleX, height: hurt.height * scale }, "#63aeff", height, flip); }
    }
    if (current.type === "direct" && rect()) Box(rect(), frame >= v.eventFrame && frame <= v.endFrame ? "#ff925e" : "#72675d", height, flip, true);
    if (current.type === "projectile" && v.spawn) {
      const sign = flip ? -1 : 1, velocity = M.Velocity(v, flip, current.execution.speed);
      const start = Screen(sign * v.spawn.x, height + v.spawn.height - v.spawn.y), duration = Math.min(0.35, current.execution.range / current.execution.speed);
      const end = Screen(sign * v.spawn.x + velocity.x * duration, height + v.spawn.height + velocity.height * duration - v.spawn.y - velocity.y * duration);
      ctx.strokeStyle = "#80e3c7"; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(start.x, start.y); ctx.lineTo(end.x, end.y); ctx.stroke();
      ctx.fillStyle = "#80e3c7"; ctx.beginPath(); ctx.arc(start.x, start.y, Math.max(4, current.execution.radius * transform.zoom), 0, Math.PI * 2); ctx.fill();
      const angle = Math.atan2(end.y - start.y, end.x - start.x); ctx.beginPath(); ctx.moveTo(end.x, end.y);
      ctx.lineTo(end.x - 12 * Math.cos(angle - .4), end.y - 12 * Math.sin(angle - .4)); ctx.lineTo(end.x - 12 * Math.cos(angle + .4), end.y - 12 * Math.sin(angle + .4)); ctx.closePath(); ctx.fill();
    }
    if (v.effect) {
      try {
        const effect = v.effect, effectMotion = M.EffectMotion(effect, images), seconds = (playing ? elapsed : frame / m.fps) - effect.eventFrame / m.fps;
        if (seconds >= 0 && ((effect.loop && (!playing || elapsed < m.frameCount / m.fps)) || (!effect.loop && seconds < effectMotion.frameCount / effectMotion.fps))) {
          const index = Math.floor(seconds * effectMotion.fps) % effectMotion.frameCount;
          DrawMotion(effectMotion, index, (flip ? -1 : 1) * effect.offset.x, height + effect.offset.height - effect.offset.y, flip);
        }
      } catch (_) { /* Incomplete effect definitions remain editable and fail export validation. */ }
    }
    ctx.fillStyle = "#adbed5"; ctx.font = "14px sans-serif"; ctx.fillText("파랑: 피격 참조 / 주황: 공격 영역 / 초록: 투사체 · 월드 단위", 16, 26);
  }
  canvas.onpointerdown = event => {
    if (busy || !transform || !variant() || !["direct", "projectile"].includes(skill()?.type)) return;
    pause(); const point = World(event), actorHeight = mode() === "air" ? Math.max(0, Math.min(1000, Number($("previewHeight").value) || 0)) : 0;
    point.y -= actorHeight; const before = M.clone(project.skills), current = rect(); let kind = "draw", corner = null;
    if (skill().type === "projectile") kind = "spawn";
    else if (current) {
      const threshold = 10 / transform.zoom;
      for (const x of [current.x, current.x + current.width]) for (const y of [current.y, current.y + current.height])
        if (Math.abs(point.x - x) <= threshold && Math.abs(point.y - y) <= threshold) { kind = "resize"; corner = { x: x === current.x ? current.x + current.width : current.x, y: y === current.y ? current.y + current.height : current.y }; }
      if (kind === "draw" && point.x >= current.x && point.x <= current.x + current.width && point.y >= current.y && point.y <= current.y + current.height) kind = "move";
    }
    drag = { pointerId: event.pointerId, point, before, original: current ? M.clone(current) : null, actorHeight, kind, corner };
    canvas.setPointerCapture(event.pointerId); MoveDrag(event);
  };
  function MoveDrag(event) {
    if (!drag || drag.pointerId !== event.pointerId) return; const point = World(event); point.y -= drag.actorHeight;
    point.x = Math.max(-1000, Math.min(1000, point.x)); point.y = Math.max(-1000, Math.min(1000, point.y));
    if (drag.kind === "spawn") { variant().spawn.x = point.x; variant().spawn.height = point.y + variant().spawn.y; }
    else if (drag.kind === "move") variant().attackRects[frame] = { ...drag.original, x: drag.original.x + point.x - drag.point.x, y: drag.original.y + point.y - drag.point.y };
    else {
      const anchor = drag.kind === "resize" ? drag.corner : drag.point;
      variant().attackRects[frame] = { x: Math.min(anchor.x, point.x), y: Math.min(anchor.y, point.y), width: Math.max(.1, Math.abs(point.x - anchor.x)), height: Math.max(.1, Math.abs(point.y - anchor.y)) };
    } Draw();
  }
  canvas.onpointermove = MoveDrag;
  canvas.onpointerup = event => {
    if (!drag || event.pointerId !== drag.pointerId) return; MoveDrag(event); undo.push(drag.before); if (undo.length > 30) undo.shift(); redo = [];
    const pointerId = drag.pointerId; drag = null; canvas.releasePointerCapture(pointerId); changed(); Refresh();
  };
  function CancelDrag() { if (!drag) return; const previous = drag; drag = null; project.skills = previous.before; if (canvas.hasPointerCapture(previous.pointerId)) canvas.releasePointerCapture(previous.pointerId); Refresh(); }
  canvas.onpointercancel = CancelDrag; canvas.onlostpointercapture = CancelDrag;
  window.addEventListener("keydown", event => {
    if (event.key === "Escape") { CancelDrag(); pause(); return; }
    if (busy || /INPUT|SELECT|TEXTAREA/.test(event.target.tagName)) return;
    if (event.ctrlKey && event.key.toLowerCase() === "z") { event.preventDefault(); Restore(event.shiftKey ? redo : undo, event.shiftKey ? undo : redo); }
    else if (event.key === "ArrowLeft") { event.preventDefault(); $("previous").click(); }
    else if (event.key === "ArrowRight") { event.preventDefault(); $("next").click(); }
    else if (event.code === "Space") { event.preventDefault(); $("play").click(); }
  });
  window.addEventListener("beforeunload", event => { if (dirty) { event.preventDefault(); event.returnValue = ""; } });
  function Tick(time) {
    const delta = lastTime ? Math.min(.1, (time - lastTime) / 1000) : 0; lastTime = time;
    const m = motion(); if (playing && m && !busy && !drag) {
      let duration = m.frameCount / m.fps;
      try { const effect = variant()?.effect; if (effect && !effect.loop) { const effectMotion = M.EffectMotion(effect, images);
        duration = Math.max(duration, effect.eventFrame / m.fps + effectMotion.frameCount / effectMotion.fps); } } catch (_) { /* Draft effect. */ }
      elapsed += delta; if (elapsed >= duration) elapsed = 0; frame = Math.min(m.frameCount - 1, Math.floor(elapsed * m.fps));
      $("frame").value = frame; $("frameText").textContent = "프레임 " + frame + " / " + (m.frameCount - 1); Draw();
    }
    requestAnimationFrame(Tick);
  }
  Refresh(); requestAnimationFrame(Tick);
})();
