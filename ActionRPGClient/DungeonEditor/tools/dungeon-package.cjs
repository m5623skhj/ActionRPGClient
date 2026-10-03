#!/usr/bin/env node
"use strict";
const fs = require("node:fs/promises"), path = require("node:path"), vm = require("node:vm"), crypto = require("node:crypto");
const { spawn } = require("node:child_process");
const Installer = require("./install.cjs");
const { SafePath, hash, exists } = Installer;
const fail = message => { throw new Error(message); };
const ID = /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/;
const SERVER_DUNGEON_FILE = /^(?:Dungeon\.json|Maps\/[A-Za-z0-9_-]+\.json)$/;
const PACKAGE_RESOURCE_FILE = /^(?:Minimap\.(?:svg|png)|Icons\/(?:normal|boss)\.(?:svg|png))$/;

function Arguments(values) {
  const options = {};
  const valued = ["project", "out", "server-repo", "client-project", "room-runtime", "town-runtime", "client-runtime", "sharp-module", "restore"];
  for (let index = 0; index < values.length; ++index) {
    const name = values[index].replace(/^--/, "");
    if (!values[index].startsWith("--") || Object.hasOwn(options, name)) fail("인자 오류/중복: " + values[index]);
    if (["install", "help"].includes(name)) options[name] = true;
    else if (valued.includes(name) && values[index + 1] && !values[index + 1].startsWith("--")) options[name] = values[++index];
    else fail("지원하지 않거나 값이 없는 인자: " + values[index]);
  }
  return options;
}

async function Root(value, marker) {
  if (!value || !path.isAbsolute(value)) fail("루트는 절대 경로로 지정하세요: " + (value || marker));
  const root = path.resolve(value);
  await SafePath(root, marker);
  if (!await exists(path.join(root, marker)) || !(await fs.lstat(path.join(root, marker))).isFile()) fail("예상한 일반 파일이 없는 루트: " + path.join(root, marker));
  return root;
}

async function Json(file, maxBytes = 4 * 1024 * 1024) {
  const stat = await fs.lstat(file);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.size > maxBytes) fail("JSON 파일 형식/크기 오류: " + file);
  return JSON.parse((await fs.readFile(file, "utf8")).replace(/^\uFEFF/, ""));
}

async function ReadSource(root, relative, sources) {
  const file = await SafePath(root, relative), stat = await fs.stat(file);
  if (!stat.isFile() || stat.size > 15 * 1024 * 1024) fail("의존성 크기/형식 오류: " + file);
  const bytes = await fs.readFile(file); sources.set(file, hash(bytes)); return bytes;
}

async function LoadScope(editorDirectory, clientProject, sources) {
  const scope = { window: {}, atob: value => Buffer.from(value, "base64").toString("binary"),
    TextEncoder, TextDecoder, Uint8Array, Uint32Array, DataView, Blob };
  vm.createContext(scope);
  for (const name of ["model.js", "archive.js", "package.js"]) {
    const file = await SafePath(editorDirectory, name);
    const bytes = await fs.readFile(file); sources.set(file, hash(bytes));
    vm.runInContext(bytes.toString("utf8"), scope, { filename: file, timeout: 10000 });
  }
  const monsterModel = await SafePath(clientProject, "MonsterEditor/model.js");
  const monsterBytes = await fs.readFile(monsterModel); sources.set(monsterModel, hash(monsterBytes));
  vm.runInContext(monsterBytes.toString("utf8"), scope, { filename: monsterModel, timeout: 10000 });
  const characterModel = await SafePath(clientProject, "CharacterEditor/model.js"), characterBytes = await fs.readFile(characterModel);
  sources.set(characterModel, hash(characterBytes));
  vm.runInContext(characterBytes.toString("utf8"), scope, { filename: characterModel, timeout: 10000 });
  return scope.window;
}

function DungeonIds(catalog) {
  if (catalog.version !== 1 || !Array.isArray(catalog.groups) || !catalog.groups.length) fail("던전 카탈로그 형식 오류");
  const ids = new Set(), groups = new Set();
  for (const group of catalog.groups) {
    if (typeof group.id !== "string" || !group.id || groups.has(group.id) || !Array.isArray(group.dungeons)
      || !group.dungeons.length || group.dungeons.length > 64) fail("던전 그룹 중복/형식 오류");
    groups.add(group.id); const localIds = new Set();
    for (const dungeon of group.dungeons) {
      if (!Number.isInteger(dungeon.id) || dungeon.id < 1 || dungeon.id > 1000000 || localIds.has(dungeon.id)) fail("던전 그룹 안의 숫자 ID 중복/오류");
      if (typeof dungeon.name !== "string" || !dungeon.name || Buffer.byteLength(dungeon.name) > 96
        || typeof dungeon.levelRange !== "string" || Buffer.byteLength(dungeon.levelRange) > 48
        || typeof dungeon.description !== "string" || Buffer.byteLength(dungeon.description) > 256) fail("던전 표시 필드 오류");
      ids.add(dungeon.id); localIds.add(dungeon.id);
    }
  }
  return ids;
}

function MonsterIds(catalog) {
  if (catalog.version !== 1 || !Array.isArray(catalog.monsters) || catalog.monsters.length > 256) fail("몬스터 카탈로그 형식 오류");
  const ids = new Map(), characters = new Set();
  for (const monster of catalog.monsters) {
    if (!Number.isInteger(monster.dataId) || monster.dataId < 1 || monster.dataId > 1000000 || ids.has(monster.dataId)
      || !/^[A-Za-z][A-Za-z0-9_.-]*\.json$/.test(monster.definitionFile) || !ID.test(monster.monsterId)
      || characters.has(monster.monsterId)) fail("몬스터 ID/문자열 ID/정의 파일 중복 또는 오류");
    ids.set(monster.dataId, monster); characters.add(monster.monsterId);
  }
  return ids;
}

async function DecodeBmp(bytes) {
  if (process.platform !== "win32") fail("BMP 완전 해석은 Windows System.Drawing이 필요합니다.");
  const command = "$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.Drawing; $data=[Convert]::FromBase64String([Console]::In.ReadToEnd()); $stream=[System.IO.MemoryStream]::new($data); $image=$null; try { $image=[System.Drawing.Image]::FromStream($stream,$false,$true); if($image.Width -gt 8192 -or $image.Height -gt 8192){throw 'Image too large'}; @{width=$image.Width;height=$image.Height}|ConvertTo-Json -Compress } finally { if($image){$image.Dispose()}; $stream.Dispose() }";
  return await new Promise((resolve, reject) => {
    const child = spawn("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command", command], { windowsHide: true });
    let stdout = "", stderr = "";
    const timer = setTimeout(() => { child.kill(); reject(new Error("BMP 해석 시간 초과")); }, 30000);
    child.stdout.on("data", data => { stdout += data; }); child.stderr.on("data", data => { stderr += data; });
    child.on("error", error => { clearTimeout(timer); reject(error); });
    child.on("close", code => { clearTimeout(timer); if (code) reject(new Error("BMP 해석 실패: " + stderr));
      else { try { resolve(JSON.parse(stdout.replace(/^\uFEFF/, ""))); } catch (error) { reject(error); } } });
    child.stdin.on("error", error => { clearTimeout(timer); reject(error); }); child.stdin.end(bytes.toString("base64"));
  });
}

function ImageHost(sharp) {
  return {
    async decodeImage(data, mime) {
      const bytes = Buffer.from(data);
      if (mime === "image/bmp") return await DecodeBmp(bytes);
      const input = sharp(bytes, { limitInputPixels: 8192 * 8192, failOn: "warning" });
      const metadata = await input.metadata();
      if (!metadata.width || metadata.width > 8192 || metadata.height > 8192 || (metadata.pages || 1) > 1) fail("이미지 크기/다중 페이지 오류");
      const expected = { "image/png": "png", "image/jpeg": "jpeg", "image/webp": "webp" }[mime];
      if (expected && expected !== metadata.format) fail("선언 MIME과 실제 이미지 형식이 다릅니다.");
      await input.stats(); // Fully decode, rather than accepting only a readable header.
      return { width: metadata.width, height: metadata.height };
    },
    async rasterizeSvg(svg, width, height) {
      return new Uint8Array(await sharp(Buffer.from(svg), { limitInputPixels: 8192 * 8192 }).resize(width, height).png().toBuffer());
    }
  };
}

async function CatalogConflict(root, relative, source, kind) {
  const target = await SafePath(root, relative); if (!await exists(target)) return;
  const current = await Json(target);
  if (kind === "monster") {
    const expected = MonsterIds(source), actual = MonsterIds(current);
    for (const [id, entry] of actual) {
      const match = expected.get(id);
      if (!match || match.monsterId !== entry.monsterId || match.definitionFile !== entry.definitionFile) fail("실행 몬스터 등록 충돌: " + id);
    }
  } else {
    const expected = DungeonIds(source); DungeonIds(current);
    for (const group of current.groups) for (const entry of group.dungeons) {
      const match = source.groups.find(value => value.id === group.id)?.dungeons.find(value => value.id === entry.id);
      if (!expected.has(entry.id) || !match || JSON.stringify(match) !== JSON.stringify(entry)) fail("실행 던전 등록 충돌: " + entry.id);
    }
  }
}

async function CheckDungeonDirectory(root, relative, project) {
  const directory = await SafePath(root, relative);
  if (!await exists(directory)) return;
  for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
    if (entry.isSymbolicLink()) fail("던전 검색 폴더의 링크/정션은 허용하지 않습니다: " + entry.name);
    if (!entry.isDirectory()) continue;
    const candidate = await SafePath(root, relative + "/" + entry.name + "/Dungeon.json");
    if (!await exists(candidate)) continue;
    const manifest = await Json(candidate);
    const sameName = entry.name.toLowerCase() === project.dungeonId.toLowerCase();
    if ((manifest.dataId === project.dataId && (!sameName || entry.name !== project.dungeonId))
      || (sameName && (entry.name !== project.dungeonId || manifest.dataId !== project.dataId || manifest.dungeonId !== project.dungeonId)))
      fail("설치된 던전 숫자 ID/폴더 ID 충돌: " + candidate);
  }
}

async function Dependencies(project, model, monsterAI, characterModel, roots, host) {
  const sources = new Map(), serverFiles = new Map(), clientFiles = new Map(), warnings = [];
  const readJson = async (root, relative, max = 4 * 1024 * 1024) => {
    const bytes = await ReadSource(root, relative, sources); if (bytes.length > max) fail("JSON 의존성 크기 제한 초과: " + relative);
    return { bytes, value: JSON.parse(bytes.toString("utf8").replace(/^\uFEFF/, "")) };
  };
  const town = await readJson(roots.server, "ActionRPGServer/TownServer/Data/DungeonCatalog.json");
  if (!DungeonIds(town.value).has(project.dataId)) fail("던전 숫자 ID가 원본 카탈로그에 등록되지 않았습니다: " + project.dataId);
  const monsters = await readJson(roots.server, "ActionRPGServer/GameRoomServer/Data/Monsters/MonsterCatalog.json"), ids = MonsterIds(monsters.value);
  serverFiles.set("MonsterCatalog.json", monsters.bytes);
  const documents = new Map(); let definitionBytes = 0;
  for (const entry of ids.values()) {
    if (!documents.has(entry.definitionFile)) {
      const input = await readJson(roots.server, "ActionRPGServer/GameRoomServer/Data/Monsters/" + entry.definitionFile);
      definitionBytes += input.bytes.length; if (definitionBytes > 64 * 1024 * 1024) fail("몬스터 정의 총량 64MB 제한 초과");
      if (!monsterAI.isEditableDocument(input.value)) fail("몬스터 정의 구조 오류: " + entry.definitionFile);
      const result = monsterAI.validateDocument(input.value);
      if (result.errors) fail("몬스터 정의 정적 검사 오류: " + entry.definitionFile + "\n" + result.issues.filter(issue => issue.severity === "error").map(issue => issue.message).join("\n"));
      warnings.push(...result.issues.filter(issue => issue.severity === "warning").map(issue => entry.definitionFile + ": " + issue.message));
      documents.set(entry.definitionFile, input.value); serverFiles.set(entry.definitionFile, input.bytes);
    }
    if (!documents.get(entry.definitionFile).monsters.some(monster => monster.id === entry.monsterId)) fail("카탈로그 문자열 ID가 정의에 없습니다: " + entry.monsterId);
  }
  const used = [...new Set(project.rooms.flatMap(room => room.monsters.map(monster => monster.dataId)))];
  for (const id of used) if (!ids.has(id)) fail("배치 몬스터 숫자 ID 미등록: " + id);
  let animationDocument = null;
  const dependencyRows = [];
  if (used.some(id => id !== 1)) {
    const animation = await readJson(roots.client, "Assets/Images/Monsters/animations.json", 8 * 1024 * 1024);
    animationDocument = animation.value;
    characterModel.ValidateAnimations(animationDocument);
    clientFiles.set("Images/Monsters/animations.json", animation.bytes);
    // Copy the whole shared metadata and its complete image closure; never trim/rewrite its declarations.
    for (const [characterId, character] of Object.entries(animationDocument.characters)) {
      if (!ID.test(characterId) || !character.motions || typeof character.motions !== "object") fail("모션 캐릭터 정의 오류");
      for (const [motionId, motion] of Object.entries(character.motions)) {
        if (!/^[A-Za-z0-9_./-]+\.(png|jpe?g|webp)$/.test(motion.image)) fail("모션 이미지 경로 오류: " + characterId + "/" + motionId);
        const relative = "Images/Monsters/" + motion.image;
        if (!clientFiles.has(relative)) clientFiles.set(relative, await ReadSource(roots.client, "Assets/" + relative, sources));
        const mime = /\.png$/.test(motion.image) ? "image/png" : /\.webp$/.test(motion.image) ? "image/webp" : "image/jpeg";
        const size = await host.decodeImage(clientFiles.get(relative), mime);
        if (size.width !== motion.width || size.height !== motion.height) fail("모션 실제 이미지 크기 오류: " + relative);
      }
    }
  }
  for (const id of used) {
    const entry = ids.get(id), palette = model.MONSTERS.find(monster => monster.dataId === id);
    if (!palette) fail("편집기 팔레트 미등록: " + id);
    if (id !== 1) {
      if (!animationDocument.characters[entry.monsterId] || !/^[A-Za-z0-9_-]+\.png$/.test(palette.image)) fail("카탈로그·편집기·외형 참조 불일치: " + id);
      const relative = "Images/Monsters/" + palette.image, bytes = await ReadSource(roots.client, "Assets/" + relative, sources);
      await host.decodeImage(bytes, "image/png"); clientFiles.set(relative, bytes);
    }
    dependencyRows.push({ dataId: id, monsterId: entry.monsterId, definitionFile: entry.definitionFile,
      previewImage: id === 1 ? "Images/Monsters/dummy.png" : "Images/Monsters/" + palette.image,
      motions: id === 1 ? [] : Object.keys(animationDocument.characters[entry.monsterId].motions) });
  }
  return { sources, serverFiles, clientFiles, town, monsters, warnings, dependencyRows };
}

async function RuntimeRoots(options, roots) {
  const names = [["room-runtime", "room", "GameRoomServer.exe"], ["town-runtime", "town", "TownServer.exe"], ["client-runtime", "game", "ActionRPGClient.exe"]];
  const count = names.filter(([option]) => options[option]).length;
  if (count && count !== 3) fail("실행 데이터 설치 루트는 룸 서버·타운 서버·클라이언트 세 개를 모두 지정하세요.");
  const targets = [];
  for (const [option, name, marker] of names) if (options[option]) {
    roots[name] = await Root(options[option], marker); targets.push({ root: roots[name], name: marker });
  }
  const locations = Object.values(roots);
  for (let left = 0; left < locations.length; ++left) for (let right = left + 1; right < locations.length; ++right) {
    const relative = path.relative(locations[left], locations[right]);
    if (!relative || (!relative.startsWith("..") && !path.isAbsolute(relative))) {
      // Actual runtime locations can be descendants of their own repository, but cannot coincide.
      if (!relative) fail("원본/실행 설치 루트가 같은 경로입니다.");
    }
  }
  return targets;
}

async function Plan(bundle, dependencies, roots, runtimeTargets) {
  await CheckDungeonDirectory(roots.server, "ActionRPGServer/GameRoomServer/Data/Dungeons", bundle.project);
  if (roots.room) {
    await CheckDungeonDirectory(roots.room, "Data/Dungeons", bundle.project);
    await CatalogConflict(roots.room, "Data/Monsters/MonsterCatalog.json", dependencies.monsters.value, "monster");
    await CatalogConflict(roots.town, "Data/DungeonCatalog.json", dependencies.town.value, "dungeon");
  }
  const items = [];
  const add = (root, relative, bytes) => items.push({ root, relative, bytes });
  for (const file of bundle.files) {
    if (file.name.startsWith("Assets/")) {
      add("client", "Assets/" + file.name.slice(7), file.data);
      if (roots.game) add("game", file.name, file.data);
    } else if (SERVER_DUNGEON_FILE.test(file.name)) {
      add("server", "ActionRPGServer/GameRoomServer/Data/Dungeons/" + bundle.project.dungeonId + "/" + file.name, file.data);
      if (roots.room) add("room", "Data/Dungeons/" + bundle.project.dungeonId + "/" + file.name, file.data);
    } else if (!PACKAGE_RESOURCE_FILE.test(file.name)) {
      fail("설치 대상이 분류되지 않은 패키지 파일: " + file.name);
    } // Minimap/icons remain in the shared ZIP; no server runtime consumes them.
  }
  // Definitions/catalogues already exist in source; deploy them only to the chosen runtime snapshot.
  if (roots.room) {
    for (const [name, bytes] of dependencies.serverFiles) add("room", "Data/Monsters/" + name, bytes);
    add("town", "Data/DungeonCatalog.json", dependencies.town.bytes);
  }
  for (const [name, bytes] of dependencies.clientFiles) {
    if (roots.game) add("game", "Assets/" + name, bytes);
  }
  // Publish references after their dependencies; expose the Town entry last.
  const rank = item => item.root === "town" ? 5 : /\/Dungeon\.json$/.test(item.relative) ? 4
    : /\/MonsterCatalog\.json$/.test(item.relative) ? 3 : /\/animations\.json$/.test(item.relative) ? 2 : 1;
  items.sort((left, right) => rank(left) - rank(right));
  return await Installer.BuildPlan(items, roots, runtimeTargets);
}

async function Main() {
  const options = Arguments(process.argv.slice(2));
  if (options.help) {
    console.log("node tools/dungeon-package.cjs --project <absolute.json> --out <new-directory> --server-repo <absolute-root> --client-project <absolute-root> [--sharp-module <absolute-module>] [--room-runtime <root> --town-runtime <root> --client-runtime <root>] [--install]\n복원: --restore <journal-directory> --server-repo <root> --client-project <root> [같은 세 실행 루트]"); return;
  }
  const roots = { server: await Root(options["server-repo"], "ActionRPGServer/GameRoomServer/Data/Monsters/MonsterCatalog.json"),
    client: await Root(options["client-project"], "Assets/Data/assets.ini") };
  const runtimeTargets = await RuntimeRoots(options, roots);
  const backupRelative = ".dungeon-installs", backupRoot = await SafePath(roots.server, backupRelative);
  if (options.restore) {
    if (options.install || options.project || options.out) fail("복원과 패키지 생성/설치는 한 번에 실행하지 않습니다.");
    const directory = path.resolve(options.restore);
    if (path.dirname(directory) !== backupRoot || !/^[a-f0-9-]{36}$/.test(path.basename(directory))) fail("서버 저장소의 설치 이력 폴더를 지정하세요.");
    await SafePath(roots.server, backupRelative + "/" + path.basename(directory) + "/journal.json");
    console.log(JSON.stringify(await Installer.Recover(directory, roots, runtimeTargets), null, 2)); return;
  }
  if (!options.project || !path.isAbsolute(options.project) || !options.out || !path.isAbsolute(options.out)) fail("--project와 --out은 절대 경로로 지정하세요.");
  const output = path.resolve(options.out);
  if (await exists(output)) fail("기존 출력 폴더를 덮어쓰지 않습니다. 새 --out 폴더를 지정하세요.");
  await SafePath(path.dirname(output), path.basename(output));
  const projectPath = path.resolve(options.project), projectStat = await fs.lstat(projectPath);
  if (!projectStat.isFile() || projectStat.isSymbolicLink() || projectStat.size > 105 * 1024 * 1024) fail("작업 JSON 파일 형식/105MB 제한 오류");
  const projectBytes = await fs.readFile(projectPath);
  if (projectBytes.length > 105 * 1024 * 1024) fail("읽는 중 작업 JSON 크기가 변경되었습니다.");
  const codeSources = new Map();
  const window = await LoadScope(path.resolve(__dirname, ".."), roots.client, codeSources);
  let sharp;
  try { sharp = require(options["sharp-module"] || "sharp"); }
  catch (_) { fail("Sharp 모듈을 찾지 못했습니다. 설치된 경로를 --sharp-module로 지정하세요. 자동 설치는 하지 않습니다."); }
  const host = ImageHost(sharp), bundle = await window.DungeonPackage.Build(projectBytes.toString("utf8"), host);
  if (options.install && bundle.project.wasMigrated) fail("이전 버전 작업 파일은 UI에서 변환 결과와 Data ID를 확인·저장한 뒤 설치하세요.");
  const dependencies = await Dependencies(bundle.project, window.DungeonModel, window.MonsterAI, window.CharacterEditorModel, roots, host);
  for (const [file, digest] of codeSources) dependencies.sources.set(file, digest);
  dependencies.sources.set(projectPath, hash(projectBytes));
  const plan = await Plan(bundle, dependencies, roots, runtimeTargets);
  bundle.report.sourceProject = projectPath; bundle.report.sourceHash = hash(projectBytes);
  bundle.report.dependencies = dependencies.dependencyRows; bundle.report.dependencyWarnings = dependencies.warnings;
  bundle.report.deploymentProfile = "server-json-client-assets";
  bundle.report.serverDungeonFiles = bundle.files.filter(file => SERVER_DUNGEON_FILE.test(file.name)).map(file => file.name);
  bundle.report.packageOnlyFiles = bundle.files.filter(file => PACKAGE_RESOURCE_FILE.test(file.name)).map(file => file.name);
  bundle.report.runtimeInstallationRequested = !!roots.room; bundle.report.installation = { status: "not-installed", plannedChanges: plan.report.length };
  bundle.report.runtimeIntegration = "ID 2/3 rendering, AI, battle and rewards are separate work; deployment does not verify playability.";
  // Publish output only after all checks. Never extract or trust an arbitrary input ZIP.
  const temporary = await SafePath(path.dirname(output), ".dungeon-package-" + crypto.randomUUID());
  await fs.mkdir(temporary);
  await fs.writeFile(path.join(temporary, bundle.project.dungeonId + ".zip"), Buffer.from(await bundle.blob.arrayBuffer()));
  await fs.writeFile(path.join(temporary, "INSTALL_PLAN.json"), JSON.stringify({ roots, runtimeTargets, files: plan.report }, null, 2) + "\n");
  await fs.writeFile(path.join(temporary, "REPORT.json"), JSON.stringify(bundle.report, null, 2) + "\n");
  await fs.rename(temporary, output);
  if (options.install) {
    try {
      for (const [source, expectedHash] of dependencies.sources) if (hash(await fs.readFile(source)) !== expectedHash) fail("검사 이후 원본이 변경되었습니다: " + source);
      if (!await exists(backupRoot)) await fs.mkdir(backupRoot);
      bundle.report.installation = await Installer.Install(plan, backupRoot);
      bundle.report.installed = ["installed", "already-installed"].includes(bundle.report.installation.status);
    } catch (error) {
      bundle.report.installation = { status: "failed", error: error.message };
      await fs.writeFile(path.join(output, "REPORT.json"), JSON.stringify(bundle.report, null, 2) + "\n"); throw error;
    }
    await fs.writeFile(path.join(output, "REPORT.json"), JSON.stringify(bundle.report, null, 2) + "\n");
  }
  console.log(JSON.stringify({ output, ...bundle.report }, null, 2));
}

Main().catch(error => { console.error(error.stack || error.message); process.exitCode = 1; });
