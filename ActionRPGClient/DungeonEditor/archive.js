"use strict";
window.DungeonArchive = (() => {
  const encoder = new TextEncoder();
  const table = Uint32Array.from({ length: 256 }, (_, index) => {
    let crc = index;
    for (let bit = 0; bit < 8; ++bit) crc = crc & 1 ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
    return crc >>> 0;
  });
  function checksum(bytes) {
    let crc = 0xffffffff;
    for (const byte of bytes) crc = table[(crc ^ byte) & 255] ^ (crc >>> 8);
    return (crc ^ 0xffffffff) >>> 0;
  }
  // ZIP "store" records, UTF-8 filenames and CRC32. No compression or external library.
  function create(files) {
    if (files.length > 65535) throw new Error("ZIP 파일 수 제한을 초과했습니다.");
    const local = [], central = [], names = new Set();
    let offset = 0, directorySize = 0;
    for (const file of files) {
      if (!/^[A-Za-z0-9_./-]+$/.test(file.name) || file.name.startsWith("/") ||
          file.name.split("/").some(part => part === ".." || !part) || names.has(file.name.toLowerCase()))
        throw new Error("안전하지 않거나 중복된 출력 경로입니다: " + file.name);
      names.add(file.name.toLowerCase());
      const name = encoder.encode(file.name), bytes = typeof file.data === "string" ? encoder.encode(file.data) : file.data;
      if (!(bytes instanceof Uint8Array) || name.length > 65535) throw new Error("출력 데이터 형식이 올바르지 않습니다.");
      const crc = checksum(bytes);
      const header = new Uint8Array(30), view = new DataView(header.buffer);
      view.setUint32(0, 0x04034b50, true); view.setUint16(4, 20, true);
      view.setUint16(6, 0x800, true); view.setUint16(12, 33, true); // DOS date: 1980-01-01
      view.setUint32(14, crc, true); view.setUint32(18, bytes.length, true);
      view.setUint32(22, bytes.length, true); view.setUint16(26, name.length, true);
      const record = new Uint8Array(46), cv = new DataView(record.buffer);
      cv.setUint32(0, 0x02014b50, true); cv.setUint16(4, 20, true); cv.setUint16(6, 20, true);
      cv.setUint16(8, 0x800, true); cv.setUint16(14, 33, true);
      cv.setUint32(16, crc, true); cv.setUint32(20, bytes.length, true); cv.setUint32(24, bytes.length, true);
      cv.setUint16(28, name.length, true); cv.setUint32(42, offset, true);
      local.push(header, name, bytes); central.push(record, name);
      offset += header.length + name.length + bytes.length;
      directorySize += record.length + name.length;
      if (offset + directorySize > 128 * 1024 * 1024) throw new Error("출력 ZIP은 128MB 이하여야 합니다.");
    }
    const end = new Uint8Array(22), ev = new DataView(end.buffer);
    ev.setUint32(0, 0x06054b50, true); ev.setUint16(8, files.length, true); ev.setUint16(10, files.length, true);
    ev.setUint32(12, directorySize, true); ev.setUint32(16, offset, true);
    return new Blob([...local, ...central, end], { type: "application/zip" });
  }
  return { create };
})();
