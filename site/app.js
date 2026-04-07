const LATEST_RELEASE_URL = "https://github.com/robertoamd90/blu-button/releases/latest";
const BOARD_QUERY_KEY = "board";
const BOARD_CATALOG_URLS = [
  new URL("./boards.json", window.location.href).href,
  new URL("../config/boards.json", window.location.href).href,
];
const MIRROR_METADATA_URL = new URL("./firmware/metadata.json", window.location.href).href;

const installButton = document.querySelector("#install-button");
const releaseDot = document.querySelector("#release-dot");
const releaseState = document.querySelector("#release-state");
const releaseVersion = document.querySelector("#release-version");
const releaseDate = document.querySelector("#release-date");
const releaseBoard = document.querySelector("#release-board");
const releaseAsset = document.querySelector("#release-asset");
const releaseSize = document.querySelector("#release-size");
const releaseLink = document.querySelector("#release-link");
const assetLink = document.querySelector("#asset-link");
const installHint = document.querySelector("#install-hint");
const boardSelect = document.querySelector("#board-select");
const boardSummary = document.querySelector("#board-summary");
const boardPills = document.querySelector("#board-pills");
const boardHint = document.querySelector("#board-hint");

let boardProfiles = {};
let boardOrder = [];
let defaultBoardId = null;
let selectedBoardId = null;
let latestMirror = null;

releaseLink.href = LATEST_RELEASE_URL;

function setStatus(kind, message) {
  releaseDot.className = `status-dot ${kind}`;
  releaseState.textContent = message;
}

function hideInstall() {
  installButton.hidden = true;
  installButton.manifest = "";
}

function getBoardProfile(boardId) {
  return boardProfiles[boardId] || boardProfiles[defaultBoardId] || null;
}

async function loadBoardCatalog() {
  for (const url of BOARD_CATALOG_URLS) {
    try {
      const response = await fetch(url, { cache: "no-store" });
      if (!response.ok) continue;

      const payload = await response.json();
      if (!payload || !Array.isArray(payload.boards) || payload.boards.length === 0) {
        continue;
      }

      boardProfiles = {};
      boardOrder = [];

      payload.boards.forEach((board) => {
        if (!board?.id || !Array.isArray(board.install_parts) || board.install_parts.length === 0) {
          return;
        }

        boardProfiles[board.id] = {
          label: board.display_name,
          summary: board.summary,
          pills: Array.isArray(board.pills) ? board.pills : [],
          hint: board.hint,
        };
        boardOrder.push(board.id);
      });

      defaultBoardId = boardProfiles[payload.default_board_id]
        ? payload.default_board_id
        : boardOrder[0];
      return Boolean(defaultBoardId);
    } catch (error) {
      // Try the next catalog location.
    }
  }

  return false;
}

async function loadMirrorMetadata() {
  try {
    const response = await fetch(MIRROR_METADATA_URL, { cache: "no-store" });
    if (!response.ok) return null;
    return await response.json();
  } catch (error) {
    return null;
  }
}

function resolveInitialBoardId() {
  const params = new URLSearchParams(window.location.search);
  const queryValue = params.get(BOARD_QUERY_KEY);
  if (queryValue && boardProfiles[queryValue]) return queryValue;
  return defaultBoardId;
}

function syncBoardQuery(boardId) {
  const url = new URL(window.location.href);
  url.searchParams.set(BOARD_QUERY_KEY, boardId);
  window.history.replaceState({}, "", url);
}

function populateBoardOptions() {
  boardSelect.innerHTML = "";
  boardOrder.forEach((boardId) => {
    const profile = getBoardProfile(boardId);
    const option = document.createElement("option");
    option.value = boardId;
    option.textContent = profile.label;
    boardSelect.append(option);
  });
  boardSelect.value = selectedBoardId;
}

function formatDate(value) {
  if (!value) return "Unknown";
  return new Intl.DateTimeFormat(undefined, {
    year: "numeric",
    month: "short",
    day: "numeric",
  }).format(new Date(value));
}

function formatBytes(value) {
  if (!Number.isFinite(value) || value <= 0) return "Unknown";
  const units = ["B", "KB", "MB", "GB"];
  let size = value;
  let idx = 0;
  while (size >= 1024 && idx < units.length - 1) {
    size /= 1024;
    idx += 1;
  }
  return `${size.toFixed(size >= 10 || idx === 0 ? 0 : 1)} ${units[idx]}`;
}

function describeInstallSet(entry) {
  return entry?.full_image?.asset_name || "Unavailable";
}

function renderBoardProfile(boardId) {
  const profile = getBoardProfile(boardId);
  if (!profile) return;

  releaseBoard.textContent = profile.label;
  boardSummary.textContent = profile.summary;
  boardHint.textContent = profile.hint;
  boardPills.innerHTML = "";
  profile.pills.forEach((pill) => {
    const el = document.createElement("span");
    el.className = "pill";
    el.textContent = pill;
    boardPills.append(el);
  });
}

function getMirrorBoardEntry(boardId) {
  if (!latestMirror || !latestMirror.assets) return null;
  return latestMirror.assets[boardId] || null;
}

function renderMirrorState() {
  const mirrorEntry = getMirrorBoardEntry(selectedBoardId);
  renderBoardProfile(selectedBoardId);

  releaseVersion.textContent = latestMirror?.tag || "Unavailable";
  releaseDate.textContent = formatDate(latestMirror?.published_at);
  releaseLink.href = latestMirror?.html_url || LATEST_RELEASE_URL;

  if (!mirrorEntry || !mirrorEntry.manifest_path || !mirrorEntry.full_image) {
    hideInstall();
    releaseAsset.textContent = "Unavailable";
    releaseSize.textContent = "Unavailable";
    assetLink.href = latestMirror?.html_url || LATEST_RELEASE_URL;
    assetLink.textContent = "Open latest release";
    installHint.textContent = "The Pages installer payload for the selected board is incomplete, so browser install is unavailable.";
    setStatus("error", "Pages installer metadata is incomplete for the selected board.");
    return;
  }

  installButton.manifest = new URL(mirrorEntry.manifest_path, window.location.href).href;
  installButton.hidden = false;
  releaseAsset.textContent = describeInstallSet(mirrorEntry);
  releaseSize.textContent = formatBytes(mirrorEntry.full_image.asset_size);
  assetLink.href = mirrorEntry.full_image.browser_download_url || latestMirror?.html_url || LATEST_RELEASE_URL;
  assetLink.textContent = "Download release full image";
  installHint.textContent = "This installer uses the mirrored full image for the selected board without requesting a whole-device erase during ordinary reinstalls.";
  setStatus("ready", "Latest mirrored release ready for browser install.");
}

function applyBoardSelection(boardId) {
  selectedBoardId = boardProfiles[boardId] ? boardId : defaultBoardId;
  boardSelect.value = selectedBoardId;
  syncBoardQuery(selectedBoardId);
  renderMirrorState();
}

async function init() {
  hideInstall();
  setStatus("warning", "Loading board catalog...");
  const boardCatalogOk = await loadBoardCatalog();
  if (!boardCatalogOk) {
    releaseVersion.textContent = "Unavailable";
    releaseDate.textContent = "Unavailable";
    releaseBoard.textContent = "Unavailable";
    releaseAsset.textContent = "Unavailable";
    releaseSize.textContent = "Unavailable";
    assetLink.href = LATEST_RELEASE_URL;
    assetLink.textContent = "Open latest release";
    installHint.textContent = "The board catalog could not be loaded, so browser install is unavailable.";
    setStatus("error", "Board catalog could not be loaded.");
    return;
  }

  setStatus("warning", "Loading Pages installer metadata...");
  latestMirror = await loadMirrorMetadata();
  if (!latestMirror) {
    releaseVersion.textContent = "Unavailable";
    releaseDate.textContent = "Unavailable";
    releaseBoard.textContent = "Unavailable";
    releaseAsset.textContent = "Unavailable";
    releaseSize.textContent = "Unavailable";
    assetLink.href = LATEST_RELEASE_URL;
    assetLink.textContent = "Open latest release";
    installHint.textContent = "The mirrored installer metadata could not be loaded, so browser install is unavailable.";
    setStatus("error", "Pages installer metadata could not be loaded.");
    return;
  }

  selectedBoardId = resolveInitialBoardId();
  populateBoardOptions();
  syncBoardQuery(selectedBoardId);
  boardSelect.addEventListener("change", (event) => {
    applyBoardSelection(event.target.value);
  });
  renderMirrorState();
}

init();
