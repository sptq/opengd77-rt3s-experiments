// SP7TEAM EMCOM site: language switch, hero screen, gallery, and the WebUSB flasher UI.

import { buildImage, firmwareLanguageVersion, languageVersion, sha256Hex, DONOR_SHA256 } from './flasher-core.js';
import { Dfu, DfuError } from './dfu.js';

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);

// ---------------------------------------------------------------- language
const T = {
  pl: {
    release: (r, d) => `Aktualna wersja: <b>${r}</b>, ${d}`,
    noLanguage: 'Tylko angielski',
    untested: ' (nietestowane)',
    donorNone: 'Brak pliku dawcy: radio będzie działać tylko na FM.',
    donorOk: (n) => `Plik dawcy zapamiętany: ${n}. DMR będzie działać.`,
    donorBad: 'To nie jest firmware MD-9600 V26.45 (suma SHA-256 się nie zgadza). Wybierz właściwy plik.',
    ownFw: (n, v) => `Własny firmware: ${n}${v}`,
    ownLang: (n) => `, język: ${n}`,
    langMatch: ' (paczka językowa pasuje)',
    langMismatch: ' (paczka językowa NIE pasuje do tego firmware i nie zostanie dołączona)',
    summary: (m, l, d) => `Wgram <b>${m}</b>, ${l}, ${d}.`,
    sumLang: (l) => `drugi język: ${l}`,
    sumNoLang: 'bez drugiego języka',
    sumDmr: 'z kodekiem DMR',
    sumFm: 'bez DMR (tylko FM)',
    choose: 'Wybierz radio „Digital Radio in USB mode”.',
    loading: 'Pobieram i przygotowuję firmware…',
    erase: (d, t) => `Kasuję pamięć: ${d} z ${t}`,
    write: (p, d, t) => `Zapisuję: ${p}% (${d} z ${t} KB)`,
    done: 'Gotowe. Radio uruchomi się samo; jeśli nie, wyłącz je i włącz.',
    cancelled: 'Nie wybrano radia.',
    claim: 'Nie mogę przejąć radia: zamknij CPS, inne karty z narzędziami OpenGD77 i skrypt wgrywania, potem spróbuj ponownie.',
    open: 'Nie mogę otworzyć połączenia z radiem. Odłącz i podłącz kabel, wejdź jeszcze raz w tryb aktualizacji.',
    dfu: (m) => `Radio przerwało zapis (${m}). Wyłącz je, wejdź jeszcze raz w tryb aktualizacji i wgraj ponownie.`,
    fetch: 'Nie mogę pobrać firmware z serwera.',
    checksum: 'Pobrany plik firmware jest uszkodzony (suma SHA-256 się nie zgadza).',
    generic: (m) => `Błąd: ${m}`,
    leave: 'Trwa wgrywanie firmware. Zamknięcie strony przerwie zapis.',
    noManifest: 'Brak plików wydania na serwerze: wgraj własny plik firmware.',
  },
  en: {
    release: (r, d) => `Current version: <b>${r}</b>, ${d}`,
    noLanguage: 'English only',
    untested: ' (untested)',
    donorNone: 'No donor file: the radio will work on FM only.',
    donorOk: (n) => `Donor file remembered: ${n}. DMR will work.`,
    donorBad: 'This is not the MD-9600 V26.45 firmware (the SHA-256 checksum does not match). Choose the right file.',
    ownFw: (n, v) => `Own firmware: ${n}${v}`,
    ownLang: (n) => `, language: ${n}`,
    langMatch: ' (the language pack matches)',
    langMismatch: ' (the language pack does NOT match this firmware and will not be merged)',
    summary: (m, l, d) => `I will flash <b>${m}</b>, ${l}, ${d}.`,
    sumLang: (l) => `second language: ${l}`,
    sumNoLang: 'no second language',
    sumDmr: 'with the DMR codec',
    sumFm: 'without DMR (FM only)',
    choose: 'Select the radio “Digital Radio in USB mode”.',
    loading: 'Downloading and preparing the firmware…',
    erase: (d, t) => `Erasing: ${d} of ${t}`,
    write: (p, d, t) => `Writing: ${p}% (${d} of ${t} KB)`,
    done: 'Done. The radio restarts by itself; if it does not, switch it off and on.',
    cancelled: 'No radio selected.',
    claim: 'Cannot claim the radio: close the CPS, other tabs with OpenGD77 tools and the flashing script, then try again.',
    open: 'Cannot open the connection to the radio. Unplug and plug the cable, enter update mode again.',
    dfu: (m) => `The radio stopped the write (${m}). Switch it off, enter update mode again and flash again.`,
    fetch: 'Cannot download the firmware from the server.',
    checksum: 'The downloaded firmware file is damaged (SHA-256 checksum mismatch).',
    generic: (m) => `Error: ${m}`,
    leave: 'The firmware is being flashed. Closing the page interrupts the write.',
    noManifest: 'No release files on the server: flash your own firmware file.',
  },
};

const store = {
  get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* private mode */ } },
};

let lang = store.get('lang') || ((navigator.language || '').toLowerCase().startsWith('pl') ? 'pl' : 'en');
const t = (k, ...a) => { const v = T[lang][k]; return (typeof v === 'function') ? v(...a) : v; };

function applyLanguage() {
  document.documentElement.lang = lang;
  document.documentElement.dataset.lang = lang;
  $('langToggle').textContent = (lang === 'pl') ? 'EN' : 'PL';
  document.title = (lang === 'pl') ? 'SP7TEAM EMCOM: firmware dla RT3S' : 'SP7TEAM EMCOM: RT3S firmware';
  renderAll();
}

$('langToggle').addEventListener('click', () => {
  lang = (lang === 'pl') ? 'en' : 'pl';
  store.set('lang', lang);
  applyLanguage();
});

// ---------------------------------------------------------------- hero screen
const heroButtons = [...$('heroStates').querySelectorAll('button')];
let heroTimer = null;

function showHero(btn) {
  const img = $('heroScreen');
  heroButtons.forEach((b) => b.setAttribute('aria-pressed', String(b === btn)));
  img.classList.add('swap');
  setTimeout(() => { img.src = `img/${btn.dataset.shot}.png`; img.classList.remove('swap'); }, 120);
}

heroButtons.forEach((b) => b.addEventListener('click', () => {
  clearInterval(heroTimer);
  showHero(b);
}));
heroButtons.forEach((b) => { new Image().src = `img/${b.dataset.shot}.png`; });
if (!matchMedia('(prefers-reduced-motion: reduce)').matches) {
  let i = 0;
  heroTimer = setInterval(() => { i = (i + 1) % 4; showHero(heroButtons[i]); }, 2800);
}

// ---------------------------------------------------------------- gallery
const SHOTS = [
  ['boot_sp7team', 'Ekran startowy', 'Boot screen'],
  ['menu_main_field', 'Menu główne: klawisze 1–0 otwierają kafelek', 'Main menu: keys 1–0 open a tile'],
  ['menu_options', 'Opcje', 'Options'],
  ['options_display', 'Lista opcji, wartości po prawej', 'Option list, values on the right'],
  ['emcom_standby', 'Kanał, czuwanie', 'Channel, standby'],
  ['emcom_rx', 'Odbiór FM z poziomem w dBm', 'FM receive with the level in dBm'],
  ['emcom_tx', 'Nadawanie z licznikiem', 'Transmit with the timer'],
  ['emcom_dmr', 'Połączenie DMR', 'DMR call'],
  ['emcom_vfo', 'VFO', 'VFO'],
  ['emcom_scan', 'Skanowanie', 'Scan'],
  ['emcom_sos', 'SOS', 'SOS'],
  ['emcom_night', 'Paleta nocna', 'Night palette'],
  ['field_pace', 'Plan PACE', 'PACE plan'],
  ['field_position', 'Pozycja: MGRS, UTM, słońce', 'Position: MGRS, UTM, sun'],
  ['field_coverage', 'Pomiar zasięgu', 'Coverage survey'],
  ['field_radio_check', 'Radio check', 'Radio check'],
  ['rssi', 'RSSI i polowanie na lisa', 'RSSI and fox hunting'],
  ['lastheard', 'Ostatnio słyszane', 'Last heard'],
  ['eventlog', 'Dziennik zdarzeń', 'Event log'],
  ['aprs_frames', 'Ramki APRS', 'APRS frames'],
  ['aprs_radar', 'Radar APRS', 'APRS radar'],
  ['aprs_messages', 'Wiadomości APRS', 'APRS messages'],
  ['sstv_pd120', 'Odbiór SSTV (PD 120, ISS)', 'SSTV receive (PD 120, ISS)'],
  ['tg_entry', 'Wpisywanie TG / PC', 'TG / PC entry'],
  ['lock', 'Blokada klawiatury', 'Keypad lock'],
  ['menu_night', 'Menu w nocy', 'Menu at night'],
  ['poweroff', 'Wyłączanie', 'Power off'],
];

const zoom = document.createElement('dialog');
zoom.className = 'zoom';
zoom.innerHTML = '<img class="lcd" alt=""><p></p>';
zoom.addEventListener('click', () => zoom.close());
document.body.append(zoom);

function renderGallery() {
  const box = $('shots');
  box.textContent = '';
  for (const [file, pl, en] of SHOTS) {
    const caption = (lang === 'pl') ? pl : en;
    const fig = document.createElement('figure');
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.setAttribute('aria-label', caption);
    btn.innerHTML = `<img class="lcd" src="img/${file}.png" width="480" height="384" alt="" loading="lazy">`;
    btn.addEventListener('click', () => {
      zoom.querySelector('img').src = `img/${file}.png`;
      zoom.querySelector('p').textContent = caption;
      zoom.showModal();
    });
    const cap = document.createElement('figcaption');
    cap.textContent = caption;
    fig.append(btn, cap);
    box.append(fig);
  }
}

// ---------------------------------------------------------------- release files
let manifest = null;

async function loadManifest() {
  try {
    const r = await fetch('firmware/manifest.json', { cache: 'no-cache' });
    if (!r.ok) throw new Error(r.status);
    manifest = await r.json();
  } catch (e) {
    manifest = null;
  }
}

function fmtDate(iso) {
  try {
    return new Date(iso + 'T12:00:00').toLocaleDateString(lang === 'pl' ? 'pl-PL' : 'en-GB', { day: 'numeric', month: 'long', year: 'numeric' });
  } catch (e) { return iso; }
}

function renderRelease() {
  const line = $('releaseLine');
  const files = $('files');
  const model = $('model');
  const language = $('language');
  const keepModel = model.value;
  const keepLang = language.value;
  files.textContent = '';
  model.textContent = '';
  language.textContent = '';

  if (!manifest) {
    line.textContent = '';
    model.disabled = true;
    language.disabled = true;
    return;
  }
  line.innerHTML = t('release', manifest.release, fmtDate(manifest.date));

  for (const m of manifest.models) {
    model.append(new Option(m.name + (m.tested ? '' : t('untested')), m.id));
    const li = document.createElement('li');
    li.innerHTML = `<a href="firmware/${m.file}" download>${m.file}</a><span class="meta">${m.name} · ${Math.round(m.size / 1024)} KB</span>`;
    files.append(li);
  }
  language.append(new Option(t('noLanguage'), ''));
  for (const l of manifest.languages) language.append(new Option(l.name, l.id));
  if (manifest.languagesZip) {
    const li = document.createElement('li');
    li.innerHTML = `<a href="firmware/${manifest.languagesZip}" download>${manifest.languagesZip}</a><span class="meta">${manifest.languages.length} .gla</span>`;
    files.append(li);
  }
  if (manifest.notes) {
    const li = document.createElement('li');
    li.innerHTML = `<a href="${manifest.notes}">${manifest.release}</a><span class="meta">${lang === 'pl' ? 'opis wydania na GitHubie' : 'release notes on GitHub'}</span>`;
    files.append(li);
  }

  model.value = keepModel || store.get('model') || manifest.models[0].id;
  if (!model.value) model.value = manifest.models[0].id;
  const savedLang = (keepLang !== '' && keepLang) || store.get('flashLang');
  const def = (savedLang !== null && savedLang !== undefined) ? savedLang : ((navigator.language || '').toLowerCase().startsWith('pl') ? 'Polish' : '');
  language.value = def;
  if (language.value !== def) language.value = '';
}

$('model').addEventListener('change', () => { store.set('model', $('model').value); renderSummary(); });
$('language').addEventListener('change', () => { store.set('flashLang', $('language').value); renderSummary(); });

// ---------------------------------------------------------------- donor (kept in IndexedDB)
let donor = null; // { name, data }

function idb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open('sp7team-flasher', 1);
    req.onupgradeneeded = () => req.result.createObjectStore('files');
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
async function idbDo(mode, fn) {
  const db = await idb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction('files', mode);
    const req = fn(tx.objectStore('files'));
    tx.oncomplete = () => resolve(req?.result);
    tx.onerror = () => reject(tx.error);
  });
}

async function loadDonor() {
  try {
    const d = await idbDo('readonly', (s) => s.get('donor'));
    if (d && d.data) donor = { name: d.name, data: new Uint8Array(d.data) };
  } catch (e) { donor = null; }
}

let donorError = false;

function renderDonor() {
  const el = $('donorState');
  if (donorError) {
    el.className = 'donor-state bad';
    el.textContent = t('donorBad');
  } else if (donor) {
    el.className = 'donor-state ok';
    el.textContent = t('donorOk', donor.name);
  } else {
    el.className = 'donor-state warn';
    el.textContent = t('donorNone');
  }
  $('donorForget').hidden = !donor;
}

$('donorFile').addEventListener('change', async (ev) => {
  const f = ev.target.files[0];
  ev.target.value = '';
  if (!f) return;
  const data = new Uint8Array(await f.arrayBuffer());
  if ((await sha256Hex(data)) !== DONOR_SHA256) {
    donorError = true;
  } else {
    donorError = false;
    donor = { name: f.name, data };
    try { await idbDo('readwrite', (s) => s.put({ name: f.name, data: data.buffer }, 'donor')); } catch (e) { /* kept for this visit */ }
  }
  renderDonor();
  renderSummary();
});

$('donorForget').addEventListener('click', async () => {
  donor = null;
  donorError = false;
  try { await idbDo('readwrite', (s) => s.delete('donor')); } catch (e) { /* nothing stored */ }
  renderDonor();
  renderSummary();
});

// ---------------------------------------------------------------- own files
let own = { fw: null, gla: null }; // { name, data }

async function readFile(input) {
  const f = input.files[0];
  input.value = '';
  return f ? { name: f.name, data: new Uint8Array(await f.arrayBuffer()) } : null;
}

$('ownFirmware').addEventListener('change', async (ev) => { own.fw = await readFile(ev.target); renderOwn(); renderSummary(); });
$('ownLanguage').addEventListener('change', async (ev) => { own.gla = await readFile(ev.target); renderOwn(); renderSummary(); });
$('ownClear').addEventListener('click', () => { own = { fw: null, gla: null }; renderOwn(); renderSummary(); });

function ownLanguageOk() {
  if (!own.fw || !own.gla) return null;
  const a = firmwareLanguageVersion(own.fw.data);
  const b = languageVersion(own.gla.data);
  return (a !== null) && (a === b);
}

function renderOwn() {
  const el = $('ownState');
  $('ownClear').hidden = !(own.fw || own.gla);
  if (!own.fw && !own.gla) { el.textContent = ''; return; }
  const ok = ownLanguageOk();
  el.className = 'own-state ' + ((ok === false) ? 'bad' : 'ok');
  el.textContent = t('ownFw', own.fw ? own.fw.name : '—', '')
    + (own.gla ? t('ownLang', own.gla.name) + ((ok === null) ? '' : (ok ? t('langMatch') : t('langMismatch'))) : '');
  $('model').disabled = false;
  $('language').disabled = !!own.gla || !manifest;
}

// ---------------------------------------------------------------- summary + flashing
function selectedModel() {
  if (manifest) return manifest.models.find((m) => m.id === $('model').value) || manifest.models[0];
  return null;
}

function renderSummary() {
  const m = selectedModel();
  const name = esc(own.fw ? own.fw.name : (m ? m.name : '—'));
  let l;
  if (own.gla) l = t('sumLang', esc(own.gla.name));
  else if (!own.fw && manifest && $('language').value) l = t('sumLang', $('language').selectedOptions[0].text);
  else l = t('sumNoLang');
  $('summary').innerHTML = (own.fw || m) ? t('summary', name, l, donor ? t('sumDmr') : t('sumFm')) : t('noManifest');
  $('flashButton').disabled = busy || !(own.fw || m) || !Dfu.supported();
}

let busy = false;

async function fetchBytes(path) {
  const r = await fetch(path);
  if (!r.ok) throw new Error('fetch');
  return new Uint8Array(await r.arrayBuffer());
}

async function prepare() {
  const m = selectedModel();
  // own builds are for the RT3S family unless a DM-1701 model is selected
  const platform = m ? m.platform : 'MD-UV380';
  let firmware;
  if (own.fw) {
    firmware = own.fw.data;
  } else {
    firmware = await fetchBytes(`firmware/${m.file}`);
    if ((await sha256Hex(firmware)) !== m.sha256) throw new Error('checksum');
  }
  let language = null;
  if (own.gla) {
    language = (ownLanguageOk() !== false) ? own.gla.data : null;
  } else if (!own.fw && manifest && $('language').value) {
    const l = manifest.languages.find((x) => x.id === $('language').value);
    if (l) language = await fetchBytes(`firmware/${l.file}`);
  }
  return buildImage({ firmware, language, donor: donor ? donor.data : null, platform });
}

function setProgress(p) {
  const box = $('progress');
  const bar = $('progressBar');
  box.hidden = false;
  box.classList.toggle('erasing', p.phase === 'erase');
  if (p.phase === 'erase') {
    bar.style.width = `${Math.round((p.done / p.total) * 100)}%`;
    $('progressText').textContent = t('erase', p.done, p.total);
  } else {
    const pct = Math.round((p.done / p.total) * 100);
    bar.style.width = `${pct}%`;
    $('progressText').textContent = t('write', pct, p.done, p.total);
  }
}

function result(text, cls) {
  const el = $('result');
  el.className = 'result ' + (cls || '');
  el.textContent = text;
}

const guard = (ev) => { ev.preventDefault(); ev.returnValue = t('leave'); return ev.returnValue; };

$('flashButton').addEventListener('click', async () => {
  if (busy) return;
  result('');
  $('progress').hidden = true;

  // The device chooser needs the click, so it comes before anything slow
  let dfu;
  try {
    result(t('choose'));
    dfu = await Dfu.request();
  } catch (e) {
    result(t('cancelled'), 'warn');
    return;
  }

  busy = true;
  renderSummary();
  window.addEventListener('beforeunload', guard);
  try {
    result(t('loading'));
    const { image } = await prepare();
    try {
      await dfu.open();
    } catch (e) {
      throw (e instanceof DfuError) ? e : new DfuError('open');
    }
    result('');
    await dfu.flash(image, setProgress);
    result(t('done'), 'ok');
  } catch (e) {
    console.error(e);
    let msg;
    if (e instanceof DfuError) msg = (e.message === 'claim') ? t('claim') : ((e.message === 'open') ? t('open') : t('dfu', e.message));
    else if (e.message === 'fetch') msg = t('fetch');
    else if (e.message === 'checksum') msg = t('checksum');
    else msg = t('generic', e.message || e);
    result(msg, 'bad');
    await dfu.close();
  } finally {
    busy = false;
    window.removeEventListener('beforeunload', guard);
    renderSummary();
  }
});

// ---------------------------------------------------------------- start
function renderAll() {
  renderGallery();
  renderRelease();
  renderDonor();
  renderOwn();
  renderSummary();
}

$('noUsb').hidden = Dfu.supported();
applyLanguage();
loadManifest().then(renderAll);
loadDonor().then(() => { renderDonor(); renderSummary(); });
