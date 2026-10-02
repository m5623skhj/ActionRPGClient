/* File installation only. No build, service control, game process or catalogue allocation. */
"use strict";
const fs = require("node:fs/promises"), path = require("node:path"), crypto = require("node:crypto");
const { promisify } = require("node:util"), execFile = promisify(require("node:child_process").execFile);
const hash = bytes => crypto.createHash("sha256").update(bytes).digest("hex");
const exists = async file => { try { await fs.lstat(file); return true; } catch (error) { if (error.code === "ENOENT") return false; throw error; } };
const fail = message => { throw new Error(message); };

async function SafePath(root, relative) {
  if (!/^[A-Za-z0-9_./-]+$/.test(relative) || relative.split("/").some(part => !part || part === "." || part === "..")) fail("안전하지 않은 설치 경로: " + relative);
  const target = path.resolve(root, ...relative.split("/"));
  const inside = path.relative(root, target);
  if (!inside || inside.startsWith("..") || path.isAbsolute(inside)) fail("설치 루트를 벗어난 경로: " + target);
  let current = path.parse(target).root;
  for (const part of target.slice(current.length).split(path.sep)) {
    current = path.join(current, part);
    if (await exists(current)) {
      const stat = await fs.lstat(current);
      if (stat.isSymbolicLink()) fail("링크/정션 경로에는 설치하지 않습니다: " + current);
      if (current !== target && !stat.isDirectory()) fail("부모 경로가 폴더가 아닙니다: " + current);
    }
  }
  return target;
}

async function Snapshot(target) {
  if (!await exists(target)) return null;
  const stat = await fs.lstat(target);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 128 * 1024 * 1024) fail("일반 파일이 아니거나 용량 제한을 초과한 대상: " + target);
  const bytes = await fs.readFile(target);
  return { bytes, hash: hash(bytes) };
}

function Allowed(root, relative) {
  const dungeon = "[A-Za-z][A-Za-z0-9_-]{0,63}/(?:Dungeon\\.json|Maps/[A-Za-z0-9_-]+\\.json|Minimap\\.(?:svg|png)|Icons/(?:normal|boss)\\.(?:svg|png))";
  if (root === "server") return new RegExp("^ActionRPGServer/GameRoomServer/Data/Dungeons/" + dungeon + "$", "i").test(relative);
  if (root === "room") return new RegExp("^Data/(?:Dungeons/" + dungeon + "|Monsters/[A-Za-z][A-Za-z0-9_.-]*\\.json)$", "i").test(relative);
  if (root === "town") return relative === "Data/DungeonCatalog.json";
  if (root === "client") return /^Assets\/Images\/(?:Dungeons\/[A-Za-z0-9_-]+\.(png|jpg|webp|bmp)|Monsters\/dummy\.png)$/i.test(relative);
  if (root === "game") return /^Assets\/Images\/(?:Dungeons\/[A-Za-z0-9_-]+\.(png|jpg|webp|bmp)|Monsters\/(?:[A-Za-z0-9_./-]+\.(png|jpg|jpeg|webp)|animations\.json))$/i.test(relative);
  return false;
}

async function BuildPlan(items, roots, runtimeTargets) {
  const seen = new Set(), operations = [];
  for (const item of items) {
    if (!Object.hasOwn(roots, item.root) || !Allowed(item.root, item.relative)) fail("허용된 데이터 설치 범위 밖입니다: " + item.relative);
    const target = await SafePath(roots[item.root], item.relative), identity = target.toLowerCase();
    if (seen.has(identity)) fail("중복 설치 대상: " + target); seen.add(identity);
    const before = await Snapshot(target), bytes = Buffer.from(item.bytes);
    if (before?.hash === hash(bytes)) continue;
    operations.push({ root: item.root, relative: item.relative, target, beforeHash: before?.hash || null, afterHash: hash(bytes), bytes });
  }
  return { roots, runtimeTargets, operations, report: operations.map(operation => ({ target: operation.target,
    action: operation.beforeHash ? "replace" : "create", beforeHash: operation.beforeHash, afterHash: operation.afterHash, bytes: operation.bytes.length })) };
}

async function CheckStopped(targets) {
  if (!targets.length) return;
  if (process.platform !== "win32") fail("실행 폴더 설치는 Windows 프로세스 확인이 필요합니다.");
  // Constant command: no interpolated filenames or shell-built user input.
  const command = "[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { $_.Name -in @('GameRoomServer.exe','TownServer.exe','ActionRPGClient.exe') } | Select-Object Name,ExecutablePath,ProcessId) | ConvertTo-Json -Compress";
  const result = await execFile("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command", command], { windowsHide: true, timeout: 30000 });
  const parsed = result.stdout.trim() ? JSON.parse(result.stdout.replace(/^\uFEFF/, "")) : [];
  for (const processInfo of (Array.isArray(parsed) ? parsed : [parsed])) for (const target of targets) {
    if (processInfo.Name.toLowerCase() !== target.name.toLowerCase()) continue;
    if (!processInfo.ExecutablePath) fail("실행 경로를 확인할 수 없는 프로세스가 있습니다: " + target.name);
    if (path.resolve(processInfo.ExecutablePath).toLowerCase() === path.join(target.root, target.name).toLowerCase())
      fail("실행 중인 대상에는 설치하지 않습니다: " + processInfo.ExecutablePath);
  }
}

async function WriteJournal(directory, journal) {
  const temporary = await SafePath(directory, "journal.next.json"), destination = await SafePath(directory, "journal.json");
  const handle = await fs.open(temporary, "w");
  try { await handle.writeFile(JSON.stringify(journal, null, 2) + "\n"); await handle.sync(); } finally { await handle.close(); }
  await fs.rename(temporary, destination);
}

async function GetLocks(roots, directory, recovery = false) {
  const locks = [];
  try {
    for (const root of [...new Set(Object.values(roots))].sort()) {
      const file = await SafePath(root, ".dungeon-install.lock");
      if (recovery && await exists(file)) {
        const lock = JSON.parse(await fs.readFile(file, "utf8"));
        if (lock.directory !== directory || !Number.isInteger(lock.pid)) fail("다른 설치의 잠금입니다: " + file);
        let alive = true;
        try { process.kill(lock.pid, 0); } catch (error) { if (error.code === "ESRCH") alive = false; else throw error; }
        if (alive) fail("설치 프로세스가 아직 실행 중입니다: PID " + lock.pid);
        await fs.unlink(file);
      }
      const handle = await fs.open(file, "wx");
      locks.push({ file, handle });
      await handle.writeFile(JSON.stringify({ pid: process.pid, directory })); await handle.sync();
    }
    return locks;
  } catch (error) { await ReleaseLocks(locks); throw error; }
}

async function ReleaseLocks(locks) {
  for (const lock of [...locks].reverse()) { await lock.handle.close(); await fs.unlink(lock.file); }
}

async function EnsureParent(root, relative, journal, directory) {
  const parts = relative.split("/"); parts.pop();
  let local = "";
  for (const part of parts) {
    local = local ? local + "/" + part : part;
    const target = await SafePath(root, local);
    if (!await exists(target)) {
      journal.createdDirectories.push({ root: Object.keys(journal.roots).find(name => journal.roots[name] === root), relative: local });
      await WriteJournal(directory, journal); // Persist intent before directory creation.
      await fs.mkdir(target);
    }
  }
}

async function Restore(directory, journal) {
  const failures = [];
  for (let index = journal.operations.length - 1; index >= 0; --index) {
    const operation = journal.operations[index]; if (!operation.started) continue;
    try {
      const target = await SafePath(journal.roots[operation.root], operation.relative), current = await Snapshot(target);
      if (operation.temporary) {
        const temporary = await SafePath(journal.roots[operation.root], operation.temporary);
        const staged = await Snapshot(temporary);
        if (staged) {
          if (staged.hash !== operation.afterHash) {
            journal.preservedTemporaryFiles ||= [];
            if (!journal.preservedTemporaryFiles.includes(temporary)) journal.preservedTemporaryFiles.push(temporary);
          } else await fs.unlink(temporary);
        }
      }
      if ((current?.hash || null) === operation.beforeHash) continue;
      if (current?.hash !== operation.afterHash) fail("설치 이후 바뀐 파일은 자동 복원하지 않습니다: " + target);
      if (operation.beforeHash) {
        const backup = await fs.readFile(await SafePath(directory, index + ".before"));
        if (hash(backup) !== operation.beforeHash) fail("백업 해시 불일치: " + target);
        const temporaryRelative = operation.relative + "." + journal.id + ".restore";
        const temporary = await SafePath(journal.roots[operation.root], temporaryRelative);
        const prior = await Snapshot(temporary);
        if (prior) { if (prior.hash !== operation.beforeHash) fail("기존 복원 임시 파일 해시 오류: " + temporary); await fs.unlink(temporary); }
        await fs.writeFile(temporary, backup, { flag: "wx" }); await fs.rename(temporary, target);
      } else await fs.unlink(target);
    } catch (error) { failures.push(error.message); }
  }
  if (!failures.length) {
    for (const entry of [...journal.createdDirectories].reverse()) {
      try { const target = await SafePath(journal.roots[entry.root], entry.relative); if (await exists(target)) await fs.rmdir(target); }
      catch (error) { if (error.code !== "ENOTEMPTY" && error.code !== "ENOENT") failures.push(error.message); }
    }
  }
  journal.status = failures.length ? "rollback-incomplete" : "rolled-back"; journal.rollbackErrors = failures;
  await WriteJournal(directory, journal);
  if (failures.length) fail("복원이 완료되지 않았습니다. journal.json을 확인하세요.\n" + failures.join("\n"));
}

async function Install(plan, backupRoot) {
  if (!plan.operations.length) return { status: "already-installed", changedFiles: 0, restartRequired: false };
  await CheckStopped(plan.runtimeTargets);
  const id = crypto.randomUUID(), directory = await SafePath(backupRoot, id);
  await fs.mkdir(directory);
  const journal = { version: 1, format: "DungeonInstallJournal", id, roots: plan.roots, runtimeTargets: plan.runtimeTargets,
    status: "preparing", createdDirectories: [], operations: plan.operations.map(operation => ({ root: operation.root,
      relative: operation.relative, beforeHash: operation.beforeHash, afterHash: operation.afterHash, started: false })) };
  const locks = await GetLocks(plan.roots, directory);
  let retainLocks = false;
  try {
    await WriteJournal(directory, journal);
    for (let index = 0; index < plan.operations.length; ++index) {
      const operation = plan.operations[index], target = await SafePath(plan.roots[operation.root], operation.relative), before = await Snapshot(target);
      if ((before?.hash || null) !== operation.beforeHash) fail("검토 후 대상 파일이 변경되었습니다: " + target);
      if (before) await fs.writeFile(path.join(directory, index + ".before"), before.bytes, { flag: "wx" });
      await fs.writeFile(path.join(directory, index + ".after"), operation.bytes, { flag: "wx" });
    }
    await CheckStopped(plan.runtimeTargets); journal.status = "installing"; await WriteJournal(directory, journal);
    for (let index = 0; index < plan.operations.length; ++index) {
      const operation = plan.operations[index], root = plan.roots[operation.root];
      const target = await SafePath(root, operation.relative), before = await Snapshot(target);
      if ((before?.hash || null) !== operation.beforeHash) fail("설치 직전에 대상 파일이 변경되었습니다: " + target);
      await EnsureParent(root, operation.relative, journal, directory);
      const temporaryRelative = operation.relative + "." + id + ".install", temporary = await SafePath(root, temporaryRelative);
      journal.operations[index].temporary = temporaryRelative; journal.operations[index].started = true; await WriteJournal(directory, journal);
      await fs.copyFile(path.join(directory, index + ".after"), temporary, require("node:fs").constants.COPYFILE_EXCL);
      if (hash(await fs.readFile(temporary)) !== operation.afterHash) fail("스테이징 해시 오류: " + temporary);
      // Recheck immediately before replacement; do not overwrite edits made during copying.
      if (((await Snapshot(target))?.hash || null) !== operation.beforeHash) fail("교체 전에 대상 파일이 변경되었습니다: " + target);
      await fs.rename(temporary, target);
      if ((await Snapshot(target))?.hash !== operation.afterHash) fail("설치 결과 해시 오류: " + target);
    }
    journal.status = "installed"; await WriteJournal(directory, journal);
    return { status: "installed", changedFiles: plan.operations.length, journal: path.join(directory, "journal.json"),
      restartRequired: plan.runtimeTargets.length > 0, playableVerified: false };
  } catch (error) {
    journal.installError = error.message;
    try { await Restore(directory, journal); }
    catch (restoreError) { retainLocks = true; throw new Error(error.message + "\n" + restoreError.message + "\n복원 이력: " + path.join(directory, "journal.json")); }
    throw new Error(error.message + "\n변경 복원 완료. 이력: " + path.join(directory, "journal.json"));
  } finally {
    if (!retainLocks) await ReleaseLocks(locks);
    else for (const lock of locks) await lock.handle.close();
  }
}

async function Recover(directory, roots, runtimeTargets) {
  const journalPath = path.join(directory, "journal.json");
  if ((await fs.stat(journalPath)).size > 16 * 1024 * 1024) fail("복원 이력 크기 제한 초과");
  const journal = JSON.parse(await fs.readFile(journalPath, "utf8"));
  if (journal.format !== "DungeonInstallJournal" || journal.version !== 1 || !/^[a-f0-9-]{36}$/.test(journal.id)
    || path.basename(directory) !== journal.id || !Array.isArray(journal.operations) || journal.operations.length > 65535
    || JSON.stringify(journal.roots) !== JSON.stringify(roots) || !Array.isArray(journal.createdDirectories)
    || !["preparing", "installing", "rollback-incomplete", "installed"].includes(journal.status)) fail("복원 이력 또는 명시한 설치 루트가 다릅니다.");
  const seen = new Set();
  for (const operation of journal.operations) {
    if (!Object.hasOwn(roots, operation.root) || !Allowed(operation.root, operation.relative) || typeof operation.started !== "boolean" || !/^[a-f0-9]{64}$/.test(operation.afterHash)
      || (operation.beforeHash !== null && !/^[a-f0-9]{64}$/.test(operation.beforeHash))) fail("복원 항목 형식 오류");
    const target = await SafePath(roots[operation.root], operation.relative);
    if (operation.temporary && operation.temporary !== operation.relative + "." + journal.id + ".install") fail("복원 임시 경로 오류");
    if (seen.has(target.toLowerCase())) fail("중복 복원 대상"); seen.add(target.toLowerCase());
  }
  for (const entry of journal.createdDirectories) {
    if (!Object.hasOwn(roots, entry.root) || !journal.operations.some(operation => operation.root === entry.root && operation.relative.startsWith(entry.relative + "/"))) fail("복원 폴더 루트/범위 오류");
    await SafePath(roots[entry.root], entry.relative);
  }
  await CheckStopped(runtimeTargets);
  const locks = await GetLocks(roots, directory, true);
  let restored = false;
  try { await Restore(directory, journal); restored = true; }
  finally { if (restored) await ReleaseLocks(locks); else for (const lock of locks) await lock.handle.close(); }
  return { status: "rolled-back", journal: journalPath, playableVerified: false };
}

module.exports = { SafePath, Snapshot, BuildPlan, CheckStopped, Install, Recover, hash, exists };
