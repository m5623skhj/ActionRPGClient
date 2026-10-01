(function () {
  "use strict";

  const SCHEMA_VERSION = 1;
  const VALIDATOR_VERSION = 1;
  const MAX_MONSTERS = 256;
  const MAX_NODES = 512;
  const MAX_EDGES = 2048;
  const MAX_CONDITION_DEPTH = 8;
  const MAX_CONDITION_TERMS = 128;
  const ID_PATTERN = /^[A-Za-z][A-Za-z0-9_.-]{0,63}$/;
  const number = (id, label, defaultValue, min, max) => ({ id, label, type: "number", defaultValue, min, max });
  const reference = (id, label, type) => ({ id, label, type, defaultValue: "" });
  const actionDefinitions = [
    { id: "Wait", label: "대기", parameters: [number("seconds", "대기 시간 (초)", 0.5, 0.001)] },
    { id: "MoveToTarget", label: "대상에게 이동", parameters: [
      number("stopDistance", "정지 거리 (월드 단위)", 70, 0),
      { id: "run", label: "달리기", type: "boolean", defaultValue: false }
    ] },
    { id: "ReturnToSpawn", label: "생성 위치로 복귀", parameters: [
      number("arrivalDistance", "도착 판정 거리", 10, 0)
    ] },
    { id: "UseSkill", label: "스킬 사용", parameters: [reference("skillId", "스킬", "skill")] },
    { id: "PlayMotion", label: "동작 재생", parameters: [reference("motionId", "동작", "motion")] }
  ];
  const conditionDefinitions = [
    { id: "Always", label: "항상", parameters: [] },
    { id: "HasTarget", label: "대상 있음", parameters: [] },
    { id: "TargetLost", label: "대상 없음 / 놓침", parameters: [] },
    { id: "TargetInRange", label: "대상이 거리 안에 있음", parameters: [number("distance", "거리", 70, 0)] },
    { id: "TargetOutOfRange", label: "대상이 거리 밖에 있음", parameters: [number("distance", "거리", 600, 0)] },
    { id: "SkillReady", label: "스킬 사용 가능", parameters: [reference("skillId", "스킬", "skill")] },
    { id: "StateTimeAtLeast", label: "상태 경과 시간", parameters: [number("seconds", "최소 경과 시간 (초)", 0.5, 0)] },
    { id: "HealthRatioAtMost", label: "HP 비율 이하", parameters: [number("ratio", "HP 비율 (0~1)", 0.3, 0, 1)] },
    { id: "AtSpawn", label: "생성 위치 도착", parameters: [number("distance", "도착 판정 거리", 10, 0)] },
    { id: "All", label: "모든 조건 (AND)", group: true },
    { id: "Any", label: "하나 이상 (OR)", group: true },
    { id: "Not", label: "조건 반전 (NOT)", group: true }
  ];
  const triggers = [
    { id: "OnUpdate", label: "다음 업데이트에서" },
    { id: "AfterAction", label: "행동 완료 후" },
    { id: "Immediate", label: "즉시 전환 (같은 업데이트)" }
  ];
  const clone = value => JSON.parse(JSON.stringify(value));
  const isObject = value => value !== null && typeof value === "object" && !Array.isArray(value);
  const defaults = definitions => Object.fromEntries(definitions.map(field => [field.id, field.defaultValue]));
  function createAction(type, document) {
    const definition = actionDefinitions.find(item => item.id === type) || actionDefinitions[0];
    const parameters = defaults(definition.parameters);
    for (const field of definition.parameters) {
      if (field.type === "skill") parameters[field.id] = document.skills[0]?.id || "";
      if (field.type === "motion") parameters[field.id] = document.motions[0]?.id || "";
    }
    return { type: definition.id, parameters };
  }
  function createCondition(type, document) {
    const definition = conditionDefinitions.find(item => item.id === type) || conditionDefinitions[0];
    if (definition.group) return { type, children: [createCondition("Always", document)] };
    const parameters = defaults(definition.parameters);
    for (const field of definition.parameters) {
      if (field.type === "skill") parameters[field.id] = document.skills[0]?.id || "";
    }
    return { type: definition.id, parameters };
  }
  function createMonster(id, name) {
    return {
      id, name, maxHp: 100,
      ai: {
        initialNodeId: "Idle",
        nodes: [{ id: "Idle", label: "대기", position: { x: 70, y: 130 }, action: { type: "Wait", parameters: { seconds: 0.5 } } }],
        edges: []
      }
    };
  }
  function createDocument() {
    return {
      schemaVersion: SCHEMA_VERSION,
      documentId: "MonsterDefinitions",
      skills: [],
      motions: [],
      monsters: [],
      approval: { status: "draft", validatorVersion: VALIDATOR_VERSION, approvedAt: null }
    };
  }
  function formatAction(action, document) {
    const definition = actionDefinitions.find(item => item.id === action?.type);
    if (!definition) return "지원하지 않는 행동";
    if (action.type === "UseSkill") return "스킬 · " + (document.skills.find(item => item.id === action.parameters?.skillId)?.label || action.parameters?.skillId || "선택 필요");
    if (action.type === "PlayMotion") return "동작 · " + (document.motions.find(item => item.id === action.parameters?.motionId)?.label || action.parameters?.motionId || "선택 필요");
    if (action.type === "Wait") return "대기 · " + action.parameters?.seconds + "초";
    return definition.label;
  }
  function formatCondition(condition) {
    if (!condition) return "조건 없음";
    if (condition.type === "All") return "AND (" + condition.children?.length + ")";
    if (condition.type === "Any") return "OR (" + condition.children?.length + ")";
    if (condition.type === "Not") return "NOT";
    return conditionDefinitions.find(item => item.id === condition.type)?.label || "알 수 없는 조건";
  }
  function resetApproval(document) {
    document.approval = { status: "draft", validatorVersion: VALIDATOR_VERSION, approvedAt: null };
  }

  /**
   * Validate definitions without running AI. OnUpdate always consumes a tick.
   * Immediate and zero-duration AfterAction paths form a separate graph: a cycle
   * in that graph can spin forever within a tick and blocks approval.
   * Conditions depending on the outside world are conservatively treated as possible.
   */
  function validateDocument(document) {
    const issues = [];
    const report = (severity, message, context = {}) => issues.push({ severity, message, ...context });
    const error = (message, context) => report("error", message, context);
    const warning = (message, context) => report("warning", message, context);
    const validId = (id, label, context) => {
      if (typeof id !== "string" || !ID_PATTERN.test(id)) {
        error(label + ": ID는 영문자로 시작하는 1~64자의 영문·숫자·밑줄·마침표·하이픈이어야 합니다.", context);
        return false;
      }
      return true;
    };
    const finite = (value, label, min, max, context) => {
      if (typeof value !== "number" || !Number.isFinite(value) || (min !== undefined && value < min) || (max !== undefined && value > max)) {
        error(label + ": 유효한 숫자가 필요합니다" + (min !== undefined ? " (최소 " + min + ")" : "") + (max !== undefined ? " (최대 " + max + ")" : "") + ".", context);
        return false;
      }
      return true;
    };
    const text = (value, label, context) => {
      if (typeof value !== "string" || !value.trim() || value.length > 256) error(label + ": 1~256자의 문자열이 필요합니다.", context);
    };
    const keys = (object, supported, label, context) => {
      for (const key of Object.keys(object)) {
        if (!supported.includes(key)) error(label + ": 지원하지 않는 필드 '" + key + "'.", context);
      }
    };
    const index = (items, label, contextOf) => {
      const result = new Map();
      for (const item of items) {
        const context = contextOf(item);
        if (!isObject(item)) { error(label + ": 항목은 객체여야 합니다.", context); continue; }
        if (validId(item.id, label, context)) {
          if (result.has(item.id)) error(label + ": 중복 ID '" + item.id + "'.", context);
          else result.set(item.id, item);
        }
      }
      return result;
    };
    if (!isObject(document)) {
      error("문서 루트는 객체여야 합니다.");
      return { issues, errors: 1, warnings: 0 };
    }
    keys(document, ["schemaVersion", "documentId", "skills", "motions", "monsters", "approval"], "문서");
    if (document.schemaVersion !== SCHEMA_VERSION) error("지원하는 schemaVersion은 " + SCHEMA_VERSION + "입니다.");
    validId(document.documentId, "문서");
    for (const field of ["skills", "motions", "monsters"]) {
      if (!Array.isArray(document[field])) error(field + ": 배열이 필요합니다.");
    }
    if (!["skills", "motions", "monsters"].every(field => Array.isArray(document[field]))) return summarize();
    if (document.monsters.length < 1 || document.monsters.length > MAX_MONSTERS) error("몬스터 수는 1~" + MAX_MONSTERS + "개여야 합니다.");
    if (document.skills.length > 2048 || document.motions.length > 2048) {
      error("스킬·동작 목록은 각각 최대 2048개까지 지원합니다.");
      return summarize();
    }
    const skillMap = index(document.skills, "스킬", item => ({ recordType: "skill", recordId: item?.id }));
    const motionMap = index(document.motions, "동작", item => ({ recordType: "motion", recordId: item?.id }));
    index(document.monsters, "몬스터", item => ({ monsterId: item?.id }));
    for (const skill of document.skills) {
      if (!isObject(skill)) continue;
      const context = { recordType: "skill", recordId: skill.id };
      keys(skill, ["id", "label", "animationId", "durationSeconds", "cooldownSeconds", "minRange", "maxRange", "hitType"], "스킬", context);
      text(skill.label, "스킬 이름", context);
      validId(skill.animationId, "스킬 애니메이션", context);
      finite(skill.durationSeconds, "스킬 동작 시간", 0.001, undefined, context);
      finite(skill.cooldownSeconds, "스킬 쿨다운", 0, undefined, context);
      finite(skill.minRange, "스킬 최소 거리", 0, undefined, context);
      finite(skill.maxRange, "스킬 최대 거리", 0, undefined, context);
      if (skill.minRange > skill.maxRange) error("스킬 최소 거리가 최대 거리보다 큽니다.", context);
      if (!["Normal", "Airborne"].includes(skill.hitType)) error("스킬 피격 유형은 Normal 또는 Airborne이어야 합니다.", context);
    }
    for (const motion of document.motions) {
      if (!isObject(motion)) continue;
      const context = { recordType: "motion", recordId: motion.id };
      keys(motion, ["id", "label", "animationId", "durationSeconds", "loop"], "동작", context);
      text(motion.label, "동작 이름", context);
      validId(motion.animationId, "동작 애니메이션", context);
      finite(motion.durationSeconds, "동작 시간", 0.001, undefined, context);
      if (typeof motion.loop !== "boolean") error("동작 loop는 true 또는 false여야 합니다.", context);
    }
    const validateParameters = (value, definitions, label, context) => {
      if (!isObject(value)) { error(label + ": parameters 객체가 필요합니다.", context); return; }
      keys(value, definitions.map(field => field.id), label, context);
      for (const field of definitions) {
        const parameter = value[field.id];
        if (field.type === "number") finite(parameter, label + " / " + field.label, field.min, field.max, context);
        else if (field.type === "boolean") {
          if (typeof parameter !== "boolean") error(label + " / " + field.label + ": true 또는 false가 필요합니다.", context);
        } else {
          const targetMap = field.type === "skill" ? skillMap : motionMap;
          if (!targetMap.has(parameter)) error(label + " / " + field.label + ": 없는 정의 '" + parameter + "'.", context);
        }
      }
    };
    const validateCondition = (condition, context, depth, budget) => {
      ++budget.count;
      if (depth > MAX_CONDITION_DEPTH || budget.count > MAX_CONDITION_TERMS) {
        if (!budget.reported) error("조건은 최대 깊이 " + MAX_CONDITION_DEPTH + ", 항목 " + MAX_CONDITION_TERMS + "개까지 지원합니다.", context);
        budget.reported = true;
        return;
      }
      if (!isObject(condition)) { error("전환 조건은 객체여야 합니다.", context); return; }
      const definition = conditionDefinitions.find(item => item.id === condition.type);
      if (!definition) { error("지원하지 않는 조건 '" + condition.type + "'.", context); return; }
      keys(condition, definition.group ? ["type", "children"] : ["type", "parameters"], "조건", context);
      if (!definition.group) validateParameters(condition.parameters, definition.parameters, "조건 " + definition.label, context);
      else if (!Array.isArray(condition.children) || !condition.children.length || (condition.type === "Not" && condition.children.length !== 1)) {
        error("AND/OR는 하나 이상의 조건, NOT은 정확히 하나의 조건이 필요합니다.", context);
      } else {
        for (const child of condition.children) {
          if (budget.count >= MAX_CONDITION_TERMS) {
            if (!budget.reported) error("조건 항목 수가 " + MAX_CONDITION_TERMS + "개를 넘습니다.", context);
            budget.reported = true;
            break;
          }
          validateCondition(child, context, depth + 1, budget);
        }
      }
    };
    for (const monster of document.monsters.slice(0, MAX_MONSTERS)) {
      if (!isObject(monster)) continue;
      const context = { monsterId: monster.id };
      keys(monster, ["id", "name", "maxHp", "ai"], "몬스터", context);
      text(monster.name, "몬스터 이름", context);
      finite(monster.maxHp, "최대 HP", 1, undefined, context);
      const ai = monster.ai;
      if (!isObject(ai) || !Array.isArray(ai.nodes) || !Array.isArray(ai.edges)) { error("AI는 nodes와 edges 배열을 가진 객체여야 합니다.", context); continue; }
      keys(ai, ["initialNodeId", "nodes", "edges"], "AI", context);
      if (!ai.nodes.length || ai.nodes.length > MAX_NODES || ai.edges.length > MAX_EDGES) {
        error("AI는 노드 1~" + MAX_NODES + "개, 연결 최대 " + MAX_EDGES + "개여야 합니다.", context);
        continue;
      }
      const nodeMap = index(ai.nodes, "노드", item => ({ ...context, nodeId: item?.id }));
      index(ai.edges, "연결", item => ({ ...context, edgeId: item?.id }));
      if (!nodeMap.has(ai.initialNodeId)) error("시작 노드 '" + ai.initialNodeId + "'가 없습니다.", context);
      for (const node of ai.nodes) {
        if (!isObject(node)) continue;
        const nodeContext = { ...context, nodeId: node.id };
        keys(node, ["id", "label", "position", "action"], "노드", nodeContext);
        text(node.label, "상태 이름", nodeContext);
        if (!isObject(node.position)) error("노드 position 객체가 필요합니다.", nodeContext);
        else {
          keys(node.position, ["x", "y"], "노드 위치", nodeContext);
          finite(node.position.x, "노드 X", -1000000, 1000000, nodeContext);
          finite(node.position.y, "노드 Y", -1000000, 1000000, nodeContext);
        }
        if (!isObject(node.action)) { error("노드 action 객체가 필요합니다.", nodeContext); continue; }
        keys(node.action, ["type", "parameters"], "행동", nodeContext);
        const definition = actionDefinitions.find(item => item.id === node.action.type);
        if (!definition) error("지원하지 않는 행동 '" + node.action.type + "'.", nodeContext);
        else validateParameters(node.action.parameters, definition.parameters, "행동 " + definition.label, nodeContext);
      }
      const outgoing = new Map(ai.nodes.filter(isObject).map(node => [node.id, []]));
      for (const edge of ai.edges) {
        if (!isObject(edge)) continue;
        const edgeContext = { ...context, edgeId: edge.id };
        keys(edge, ["id", "from", "to", "priority", "trigger", "condition"], "연결", edgeContext);
        if (!nodeMap.has(edge.from)) error("연결 출발 노드 '" + edge.from + "'가 없습니다.", edgeContext);
        if (!nodeMap.has(edge.to)) error("연결 도착 노드 '" + edge.to + "'가 없습니다.", edgeContext);
        if (!Number.isSafeInteger(edge.priority) || edge.priority < 0) error("전환 우선순위는 0 이상의 안전한 정수여야 합니다.", edgeContext);
        if (!triggers.some(item => item.id === edge.trigger)) error("지원하지 않는 전환 시점 '" + edge.trigger + "'.", edgeContext);
        validateCondition(edge.condition, edgeContext, 1, { count: 0, reported: false });
        const source = nodeMap.get(edge.from);
        if (edge.trigger === "AfterAction" && source?.action?.type === "PlayMotion" && motionMap.get(source.action.parameters?.motionId)?.loop) {
          error("반복 동작은 완료되지 않아 '행동 완료 후' 전환을 사용할 수 없습니다.", edgeContext);
        }
        if (outgoing.has(edge.from)) outgoing.get(edge.from).push(edge);
      }
      for (const [nodeId, edges] of outgoing) {
        const priorities = new Set();
        for (const edge of edges) {
          if (priorities.has(edge.priority)) error("같은 상태에서 우선순위 " + edge.priority + "가 중복됩니다.", { ...context, edgeId: edge.id });
          priorities.add(edge.priority);
        }
        const sorted = edges.slice().sort((a, b) => a.priority - b.priority);
        for (let i = 0; i < sorted.length - 1; ++i) {
          const edge = sorted[i];
          if (edge.condition?.type === "Always" && sorted.slice(i + 1).some(later => later.trigger === edge.trigger)) {
            error("항상 전환하는 연결이 같은 시점의 낮은 우선순위 연결을 가립니다.", { ...context, edgeId: edge.id });
          }
        }
        if (!edges.length) warning("상태 '" + nodeId + "'에는 나가는 연결이 없습니다. 이 상태에 계속 머뭅니다.", { ...context, nodeId });
      }
      if (nodeMap.has(ai.initialNodeId)) {
        const reached = new Set([ai.initialNodeId]);
        const queue = [ai.initialNodeId];
        for (let i = 0; i < queue.length; ++i) {
          for (const edge of outgoing.get(queue[i]) || []) {
            if (nodeMap.has(edge.to) && !reached.has(edge.to)) { reached.add(edge.to); queue.push(edge.to); }
          }
        }
        for (const nodeId of nodeMap.keys()) if (!reached.has(nodeId)) error("시작 노드에서 도달할 수 없는 상태 '" + nodeId + "'.", { ...context, nodeId });
      }
      // Invalid parameters cannot be used as evidence of a guaranteed time delay.
      const positive = value => typeof value === "number" && Number.isFinite(value) && value > 0;
      const delaysOnEntry = (condition, depth = 0) => {
        if (!isObject(condition) || depth >= MAX_CONDITION_DEPTH) return false;
        if (condition.type === "StateTimeAtLeast") return positive(condition.parameters?.seconds);
        if (condition.type === "All" && Array.isArray(condition.children)) return condition.children.slice(0, MAX_CONDITION_TERMS).some(child => delaysOnEntry(child, depth + 1));
        if (condition.type === "Any" && Array.isArray(condition.children) && condition.children.length) return condition.children.slice(0, MAX_CONDITION_TERMS).every(child => delaysOnEntry(child, depth + 1));
        return false;
      };
      const actionTakesTime = node => {
        const action = node?.action;
        if (action?.type === "Wait") return positive(action.parameters?.seconds);
        if (action?.type === "UseSkill") return positive(skillMap.get(action.parameters?.skillId)?.durationSeconds);
        if (action?.type === "PlayMotion") return positive(motionMap.get(action.parameters?.motionId)?.durationSeconds);
        return false;
      };
      const immediate = new Map([...nodeMap.keys()].map(id => [id, []]));
      for (const edge of ai.edges.filter(isObject)) {
        if (!nodeMap.has(edge.from) || !nodeMap.has(edge.to)) continue;
        if (edge.trigger === "OnUpdate" || delaysOnEntry(edge.condition)) continue;
        if (edge.trigger === "AfterAction" && actionTakesTime(nodeMap.get(edge.from))) continue;
        if (edge.trigger === "Immediate" || edge.trigger === "AfterAction") immediate.get(edge.from).push(edge);
      }
      const colors = new Map();
      const stack = [];
      const visit = nodeId => {
        colors.set(nodeId, 1);
        stack.push(nodeId);
        for (const edge of immediate.get(nodeId)) {
          if (colors.get(edge.to) === 1) {
            const cycle = stack.slice(stack.indexOf(edge.to)).concat(edge.to).join(" → ");
            error("시간을 소비하지 않는 순환이 가능합니다: " + cycle + ". 전환을 '다음 업데이트'로 바꾸거나 실제 대기를 넣으세요.", { ...context, edgeId: edge.id, nodeId });
          } else if (!colors.has(edge.to)) visit(edge.to);
        }
        stack.pop();
        colors.set(nodeId, 2);
      };
      for (const nodeId of immediate.keys()) if (!colors.has(nodeId)) visit(nodeId);
    }
    return summarize();

    function summarize() {
      return { issues, errors: issues.filter(item => item.severity === "error").length, warnings: issues.filter(item => item.severity === "warning").length };
    }
  }
  function isEditableDocument(value) {
    // Import invalid references/values for repair, but reject malformed structures
    // that the editor cannot safely represent. Detailed errors remain in validator.
    if (!isObject(value) || value.schemaVersion !== SCHEMA_VERSION || !["skills", "motions", "monsters"].every(key => Array.isArray(value[key]))) return false;
    if (value.monsters.length > MAX_MONSTERS || value.skills.length > 2048 || value.motions.length > 2048) return false;
    if (!value.skills.every(isObject) || !value.motions.every(isObject)) return false;
    const conditionShape = (condition, depth, budget) => {
      if (!isObject(condition) || depth > MAX_CONDITION_DEPTH || ++budget.count > MAX_CONDITION_TERMS) return false;
      const definition = conditionDefinitions.find(item => item.id === condition.type);
      if (definition?.group) return Array.isArray(condition.children) && condition.children.every(child => conditionShape(child, depth + 1, budget));
      return isObject(condition.parameters);
    };
    return value.monsters.every(monster => isObject(monster) && isObject(monster.ai)
      && Array.isArray(monster.ai.nodes) && monster.ai.nodes.length <= MAX_NODES
      && Array.isArray(monster.ai.edges) && monster.ai.edges.length <= MAX_EDGES
      && monster.ai.nodes.every(node => isObject(node) && isObject(node.position) && isObject(node.action) && isObject(node.action.parameters))
      && monster.ai.edges.every(edge => isObject(edge) && conditionShape(edge.condition, 1, { count: 0 })));
  }

  window.MonsterAI = Object.freeze({
    SCHEMA_VERSION, VALIDATOR_VERSION, MAX_MONSTERS, MAX_NODES, MAX_EDGES, MAX_CONDITION_DEPTH,
    MAX_CONDITION_TERMS, ID_PATTERN, actionDefinitions, conditionDefinitions, triggers,
    clone, createDocument, createMonster, createAction, createCondition, formatAction, formatCondition,
    resetApproval, validateDocument, isEditableDocument
  });
}());
