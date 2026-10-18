if (typeof Intl === 'undefined') (function () {
  var G = globalThis;
  function loc(l) { if (Array.isArray(l)) l = l[0]; return typeof l === 'string' && l ? l : 'en-US'; }
  function grp(s) { var neg = s[0] === '-'; if (neg) s = s.slice(1); var p = s.split('.'); p[0] = p[0].replace(/\B(?=(\d{3})+(?!\d))/g, ','); return (neg ? '-' : '') + p.join('.'); }
  var CUR = { USD: '$', EUR: '€', GBP: '£', JPY: '¥', SEK: 'kr', INR: '₹' };
  function NumberFormat(l, o) { if (!(this instanceof NumberFormat)) return new NumberFormat(l, o); o = o || {}; this._l = loc(l); this._o = o; }
  NumberFormat.prototype.format = function (n) {
    var o = this._o; n = Number(n); if (!isFinite(n)) return isNaN(n) ? 'NaN' : (n < 0 ? '-∞' : '∞');
    var st = o.style || 'decimal', v = st === 'percent' ? n * 100 : n, suf = '';
    if (o.notation === 'compact') {
      var a = Math.abs(v), U = o.compactDisplay === 'long' ? [[1e12, ' trillion'], [1e9, ' billion'], [1e6, ' million'], [1e3, ' thousand']] : [[1e12, 'T'], [1e9, 'B'], [1e6, 'M'], [1e3, 'K']];
      for (var i = 0; i < U.length; i++) if (a >= U[i][0]) { v = v / U[i][0]; suf = U[i][1]; break; }
      var r = Math.abs(v) < 10 ? Math.round(v * 10) / 10 : Math.round(v); var s = String(r); return s + suf;
    }
    var mn = o.minimumFractionDigits, mx = o.maximumFractionDigits;
    if (mx === undefined) mx = st === 'currency' ? 2 : st === 'percent' ? 0 : 3; if (mn === undefined) mn = st === 'currency' ? 2 : 0; if (mx < mn) mx = mn;
    var s2 = v.toFixed(mx); if (s2.indexOf('.') >= 0) { while (s2.length && s2[s2.length - 1] === '0' && s2.split('.')[1].length > mn) s2 = s2.slice(0, -1); if (s2[s2.length - 1] === '.') s2 = s2.slice(0, -1); }
    if (o.minimumIntegerDigits) { var neg = s2[0] === '-'; if (neg) s2 = s2.slice(1); var ip = s2.split('.'); while (ip[0].length < o.minimumIntegerDigits) ip[0] = '0' + ip[0]; s2 = (neg ? '-' : '') + ip.join('.'); }
    if (o.useGrouping !== false) s2 = grp(s2);
    if (st === 'percent') return s2 + '%';
    if (st === 'currency') { var c = o.currency || 'USD', sym = CUR[c] || c + ' '; return s2[0] === '-' ? '-' + sym + s2.slice(1) : sym + s2; }
    if (st === 'unit' && o.unit) return s2 + ' ' + o.unit;
    return s2;
  };
  NumberFormat.prototype.formatToParts = function (n) { return [{ type: 'integer', value: this.format(n) }]; };
  NumberFormat.prototype.resolvedOptions = function () { var r = { locale: this._l, numberingSystem: 'latn', style: 'decimal', minimumIntegerDigits: 1, minimumFractionDigits: 0, maximumFractionDigits: 3, useGrouping: true, notation: 'standard', signDisplay: 'auto' }; for (var k in this._o) r[k] = this._o[k]; return r; };
  NumberFormat.supportedLocalesOf = function (l) { return Array.isArray(l) ? l.slice() : l ? [l] : []; };
  var MON = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  var DAY = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  function p2(n) { return n < 10 ? '0' + n : '' + n; }
  function DateTimeFormat(l, o) { if (!(this instanceof DateTimeFormat)) return new DateTimeFormat(l, o); this._l = loc(l); o = o || {}; if (!o.year && !o.month && !o.day && !o.hour && !o.minute && !o.second && !o.weekday && !o.dateStyle && !o.timeStyle) o = Object.assign({ year: 'numeric', month: 'numeric', day: 'numeric' }, o); this._o = o; }
  DateTimeFormat.prototype.formatToParts = function (d) {
    d = d === undefined ? new Date() : new Date(d); var o = this._o, P = [], utc = o.timeZone === 'UTC';
    var Y = utc ? d.getUTCFullYear() : d.getFullYear(), M = utc ? d.getUTCMonth() : d.getMonth(), D = utc ? d.getUTCDate() : d.getDate(), W = utc ? d.getUTCDay() : d.getDay(), h = utc ? d.getUTCHours() : d.getHours(), mi = utc ? d.getUTCMinutes() : d.getMinutes(), se = utc ? d.getUTCSeconds() : d.getSeconds();
    function add(t, v) { if (P.length) P.push({ type: 'literal', value: t === 'hour' || t === 'minute' || t === 'second' ? (P[P.length - 1].type === 'hour' || P[P.length - 1].type === 'minute' ? ':' : ', ') : (t === 'day' && o.month && o.month.length > 2 ? ' ' : t === 'year' && o.month && o.month.length > 2 ? (o.day ? ', ' : ' ') : o.month && o.month.length > 2 ? ' ' : '/') }); P.push({ type: t, value: String(v) }); }
    var ds = o.dateStyle, ts = o.timeStyle;
    if (ds) { o = Object.assign({}, o, ds === 'full' ? { weekday: 'long', month: 'long', day: 'numeric', year: 'numeric' } : ds === 'long' ? { month: 'long', day: 'numeric', year: 'numeric' } : ds === 'medium' ? { month: 'short', day: 'numeric', year: 'numeric' } : { month: 'numeric', day: 'numeric', year: '2-digit' }); }
    if (ts) { o = Object.assign({}, o, { hour: 'numeric', minute: '2-digit' }, ts !== 'short' ? { second: '2-digit' } : {}); }
    if (o.weekday) { P.push({ type: 'weekday', value: o.weekday === 'long' ? DAY[W] : DAY[W].slice(0, o.weekday === 'narrow' ? 1 : 3) }); }
    if (o.month) add('month', o.month === 'long' ? MON[M] : o.month === 'short' ? MON[M].slice(0, 3) : o.month === 'narrow' ? MON[M][0] : o.month === '2-digit' ? p2(M + 1) : M + 1);
    if (o.day) add('day', o.day === '2-digit' ? p2(D) : D);
    if (o.year) add('year', o.year === '2-digit' ? p2(Y % 100) : Y);
    var h12 = o.hour12 !== false && o.hourCycle !== 'h23' && o.hourCycle !== 'h24';
    if (o.hour) add('hour', h12 ? ((h % 12) || 12) : (o.hour === '2-digit' ? p2(h) : h));
    if (o.minute) add('minute', p2(mi));
    if (o.second) add('second', p2(se));
    if (o.hour && h12) { P.push({ type: 'literal', value: ' ' }); P.push({ type: 'dayPeriod', value: h < 12 ? 'AM' : 'PM' }); }
    return P;
  };
  DateTimeFormat.prototype.format = function (d) { return this.formatToParts(d).map(function (p) { return p.value; }).join(''); };
  DateTimeFormat.prototype.formatRange = function (a, b) { return this.format(a) + ' – ' + this.format(b); };
  DateTimeFormat.prototype.resolvedOptions = function () { var r = { locale: this._l, calendar: 'gregory', numberingSystem: 'latn', timeZone: this._o.timeZone || 'UTC' }; for (var k in this._o) if (k !== 'timeZone') r[k] = this._o[k]; return r; };
  DateTimeFormat.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function PluralRules(l, o) { if (!(this instanceof PluralRules)) throw new TypeError('Constructor Intl.PluralRules requires new'); this._l = loc(l); this._o = o || {}; }
  PluralRules.prototype.select = function (n) { n = Number(n); if (this._o.type === 'ordinal') { var a = n % 10, b = n % 100; return a === 1 && b !== 11 ? 'one' : a === 2 && b !== 12 ? 'two' : a === 3 && b !== 13 ? 'few' : 'other'; } return n === 1 ? 'one' : 'other'; };
  PluralRules.prototype.resolvedOptions = function () { return { locale: this._l, type: this._o.type || 'cardinal', pluralCategories: ['one', 'other'] }; };
  PluralRules.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function Collator(l, o) { if (!(this instanceof Collator)) return new Collator(l, o); this._l = loc(l); this._o = o || {}; var self = this; this.compare = function (a, b) { a = String(a); b = String(b); if (self._o.sensitivity === 'base' || self._o.sensitivity === 'accent') { a = a.toLowerCase(); b = b.toLowerCase(); } if (self._o.numeric) { var x = parseFloat(a), y = parseFloat(b); if (!isNaN(x) && !isNaN(y) && x !== y) return x < y ? -1 : 1; } return a < b ? -1 : a > b ? 1 : 0; }; }
  Collator.prototype.resolvedOptions = function () { return { locale: this._l, usage: 'sort', sensitivity: this._o.sensitivity || 'variant' }; };
  Collator.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function RelativeTimeFormat(l, o) { if (!(this instanceof RelativeTimeFormat)) throw new TypeError('Constructor Intl.RelativeTimeFormat requires new'); this._l = loc(l); this._o = o || {}; }
  RelativeTimeFormat.prototype.format = function (v, u) { v = Number(v); u = String(u).replace(/s$/, ''); if (this._o.numeric === 'auto' && u === 'day' && Math.abs(v) <= 1) return v === 0 ? 'today' : v > 0 ? 'tomorrow' : 'yesterday'; var a = Math.abs(v), w = u + (a === 1 ? '' : 's'); return v < 0 || Object.is(v, -0) ? a + ' ' + w + ' ago' : 'in ' + a + ' ' + w; };
  RelativeTimeFormat.prototype.formatToParts = function (v, u) { return [{ type: 'literal', value: this.format(v, u) }]; };
  RelativeTimeFormat.prototype.resolvedOptions = function () { return { locale: this._l, style: this._o.style || 'long', numeric: this._o.numeric || 'always', numberingSystem: 'latn' }; };
  RelativeTimeFormat.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function ListFormat(l, o) { if (!(this instanceof ListFormat)) throw new TypeError('Constructor Intl.ListFormat requires new'); this._l = loc(l); this._o = o || {}; }
  ListFormat.prototype.format = function (a) { a = Array.from(a, String); var w = this._o.type === 'disjunction' ? 'or' : 'and'; if (a.length < 2) return a.join(''); if (a.length === 2) return a[0] + ' ' + w + ' ' + a[1]; return a.slice(0, -1).join(', ') + ', ' + w + ' ' + a[a.length - 1]; };
  ListFormat.prototype.formatToParts = function (a) { return [{ type: 'literal', value: this.format(a) }]; };
  ListFormat.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function Segmenter(l, o) { if (!(this instanceof Segmenter)) throw new TypeError('Constructor Intl.Segmenter requires new'); this._l = loc(l); this._o = o || {}; }
  Segmenter.prototype.segment = function (s) { s = String(s); var g = this._o.granularity || 'grapheme', out = [], i = 0; if (g === 'word') { var re = /\w+|\s+|[^\w\s]+/g, m; while ((m = re.exec(s))) out.push({ segment: m[0], index: m.index, input: s, isWordLike: /\w/.test(m[0]) }); } else if (g === 'sentence') { var re2 = /[^.!?]+[.!?]*\s*/g, m2; while ((m2 = re2.exec(s))) { if (!m2[0]) break; out.push({ segment: m2[0], index: m2.index, input: s }); } } else { for (var ch of s) { out.push({ segment: ch, index: i, input: s }); i += ch.length; } } out.containing = function (k) { for (var j = 0; j < out.length; j++) if (k >= out[j].index && k < out[j].index + out[j].segment.length) return out[j]; }; return out; };
  Segmenter.prototype.resolvedOptions = function () { return { locale: this._l, granularity: this._o.granularity || 'grapheme' }; };
  Segmenter.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function Locale(t, o) { if (!(this instanceof Locale)) throw new TypeError('Constructor Intl.Locale requires new'); t = String(t); var p = t.split(/[-_]/); this.baseName = t; this.language = p[0]; this.region = p.length > 1 && p[p.length - 1].length === 2 ? p[p.length - 1].toUpperCase() : undefined; this.script = p.length > 2 ? p[1] : undefined; if (o) for (var k in o) this[k] = o[k]; }
  Locale.prototype.toString = function () { return this.baseName; };
  Locale.prototype.maximize = function () { return this; }; Locale.prototype.minimize = function () { return this; };
  function DisplayNames(l, o) { if (!(this instanceof DisplayNames)) throw new TypeError('Constructor Intl.DisplayNames requires new'); this._l = loc(l); this._o = o || {}; }
  DisplayNames.prototype.of = function (c) { return String(c); };
  DisplayNames.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  G.Intl = { NumberFormat: NumberFormat, DateTimeFormat: DateTimeFormat, PluralRules: PluralRules, Collator: Collator, RelativeTimeFormat: RelativeTimeFormat, ListFormat: ListFormat, Segmenter: Segmenter, Locale: Locale, DisplayNames: DisplayNames,
    getCanonicalLocales: function (l) { return l === undefined ? [] : Array.isArray(l) ? l.map(String) : [String(l)]; }, supportedValuesOf: function () { return []; } };
  Object.defineProperty(G.Intl, Symbol.toStringTag, { value: 'Intl' });
  Number.prototype.toLocaleString = function (l, o) { return new NumberFormat(l, o).format(this); };
  Date.prototype.toLocaleDateString = function (l, o) { return new DateTimeFormat(l, o).format(this); };
  Date.prototype.toLocaleTimeString = function (l, o) { o = o || {}; if (!o.hour && !o.minute && !o.timeStyle) o = Object.assign({ hour: 'numeric', minute: '2-digit', second: '2-digit' }, o); return new DateTimeFormat(l, o).format(this); };
  Date.prototype.toLocaleString = function (l, o) { o = o || {}; if (!o.year && !o.month && !o.day && !o.hour && !o.dateStyle && !o.timeStyle) o = Object.assign({ year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: '2-digit', second: '2-digit' }, o); return new DateTimeFormat(l, o).format(this); };
  String.prototype.localeCompare = function (b, l, o) { return new Collator(l, o).compare(this, b); };
})();
(function () {
"use strict";
const N = __n, P = N.protos, G = globalThis;
const def = (o, k, get, set) => Object.defineProperty(o, k, { get, set, configurable: true, enumerable: false });
const val = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false });
const kebab = k => k.startsWith('--') ? k : k === 'cssFloat' ? 'float' : k.replace(/^(webkit|moz|ms)(?=[A-Z])/, m => '-' + m).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
const fmtArg = a => { if (typeof a === 'string') return a; try { if (a instanceof Error) return a.name + ': ' + a.message; if (a && a.nodeType) return '<' + (a.nodeName || '').toLowerCase() + '>'; const s = JSON.stringify(a); return s === undefined ? String(a) : s; } catch (e) { return String(a); } };
const fmt = args => Array.prototype.map.call(args, fmtArg).join(' ');

function EventTarget() {}
val(G, 'EventTarget', EventTarget);
const LS = Symbol('listeners');
function listeners(t) { let m = t[LS]; if (!m) { m = new Map(); try { Object.defineProperty(t, LS, { value: m, configurable: true }); } catch (e) {} } return m; }
EventTarget.prototype.addEventListener = function (type, fn, opts) {
  if (!fn) return;
  const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
  const once = !!(opts && typeof opts === 'object' && opts.once);
  const m = listeners(this);
  let a = m.get(type);
  if (!a) { a = []; m.set(type, a); }
  for (const l of a) if (l.fn === fn && l.capture === capture) return;
  const rec = { fn, capture, once };
  a.push(rec);
  if (opts && opts.signal) opts.signal.addEventListener('abort', () => this.removeEventListener(type, fn, opts));
};
EventTarget.prototype.removeEventListener = function (type, fn, opts) {
  const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
  const a = listeners(this).get(type);
  if (!a) return;
  const i = a.findIndex(l => l.fn === fn && l.capture === capture);
  if (i >= 0) a.splice(i, 1);
};
EventTarget.prototype.dispatchEvent = function (ev) { run(this, ev); return !ev.defaultPrevented; };
Object.setPrototypeOf(P.Node, EventTarget.prototype);
Object.setPrototypeOf(G, EventTarget.prototype);

class Event {
  constructor(type, init) { init = init || {}; this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.timeStamp = N.now(); this.isTrusted = false; this._stop = false; this._imm = false; this._path = []; }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
  stopImmediatePropagation() { this._stop = true; this._imm = true; }
  composedPath() { return this._path.slice(); }
  get srcElement() { return this.target; }
  get returnValue() { return !this.defaultPrevented; }
  set returnValue(v) { if (!v) this.preventDefault(); }
  get cancelBubble() { return this._stop; }
  set cancelBubble(v) { if (v) this._stop = true; }
  initEvent(t, b, c) { this.type = t; this.bubbles = !!b; this.cancelable = !!c; }
}
Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } initCustomEvent(t, b, c, d) { this.initEvent(t, b, c); this.detail = d; } }
class UIEvent extends Event { constructor(t, i) { super(t, i); this.view = G; this.detail = (i && i.detail) || 0; } }
class MouseEvent extends UIEvent {
  constructor(t, i) { super(t, i); i = i || {}; this.clientX = i.clientX || 0; this.clientY = i.clientY || 0; this.screenX = i.screenX || this.clientX; this.screenY = i.screenY || this.clientY; this.button = i.button || 0; this.buttons = i.buttons || 0; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; this.relatedTarget = i.relatedTarget || null; this.movementX = 0; this.movementY = 0; }
  get pageX() { return this.clientX + N.scrollY() * 0; } get pageY() { return this.clientY + N.scrollY(); } get x() { return this.clientX; } get y() { return this.clientY; }
  get offsetX() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : { left: 0 }; return this.clientX - r.left; }
  get offsetY() { const r = this.target && this.target.getBoundingClientRect ? this.target.getBoundingClientRect() : { top: 0 }; return this.clientY - r.top; }
  get which() { return this.button + 1; }
  getModifierState() { return false; }
}
class PointerEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.pointerId = i.pointerId || 1; this.pointerType = i.pointerType || 'mouse'; this.isPrimary = true; this.width = 1; this.height = 1; this.pressure = 0.5; } }
class WheelEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.deltaX = i.deltaX || 0; this.deltaY = i.deltaY || 0; this.deltaZ = 0; this.deltaMode = 0; } }
class KeyboardEvent extends UIEvent {
  constructor(t, i) { super(t, i); i = i || {}; this.key = i.key || ''; this.code = i.code || ''; this.keyCode = i.keyCode || 0; this.charCode = i.charCode || 0; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; this.repeat = !!i.repeat; this.isComposing = false; this.location = 0; }
  get which() { return this.keyCode; }
  getModifierState() { return false; }
}
class FocusEvent extends UIEvent { constructor(t, i) { super(t, i); this.relatedTarget = (i && i.relatedTarget) || null; } }
class InputEvent extends UIEvent { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data === undefined ? null : i.data; this.inputType = i.inputType || ''; this.isComposing = false; } }
class SubmitEvent extends Event { constructor(t, i) { super(t, i); this.submitter = (i && i.submitter) || null; } }
class ProgressEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.lengthComputable = !!i.lengthComputable; this.loaded = i.loaded || 0; this.total = i.total || 0; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.error = i.error || null; this.filename = ''; this.lineno = 0; this.colno = 0; } }
class PopStateEvent extends Event { constructor(t, i) { super(t, i); this.state = i && i.state !== undefined ? i.state : null; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.source = i.source || null; this.ports = []; } }
class AnimationEvent extends Event {}
class TransitionEvent extends Event {}
class TouchEvent extends UIEvent { constructor(t, i) { super(t, i); this.touches = []; this.targetTouches = []; this.changedTouches = []; } }
class ClipboardEvent extends Event { constructor(t, i) { super(t, i); this.clipboardData = { getData: () => '', setData() {} }; } }
class StorageEvent extends Event {}
class PageTransitionEvent extends Event { constructor(t, i) { super(t, i); this.persisted = false; } }
Object.assign(G, { Event, CustomEvent, UIEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, FocusEvent, InputEvent, SubmitEvent, ProgressEvent, ErrorEvent, PopStateEvent, HashChangeEvent, MessageEvent, AnimationEvent, TransitionEvent, TouchEvent, ClipboardEvent, StorageEvent, PageTransitionEvent });

const ihCache = new WeakMap();
function inlineHandler(node, type) {
  if (!node || typeof node.getAttribute !== 'function') return null;
  const code = node.getAttribute('on' + type);
  if (code == null) return null;
  let c = ihCache.get(node);
  if (!c) { c = {}; ihCache.set(node, c); }
  const e = c[type];
  if (e && e.code === code) return e.fn;
  let fn = null;
  try { fn = new Function('event', code); } catch (err) { console.error(err); }
  c[type] = { code, fn };
  return fn;
}
function invoke(node, ev, phase) {
  ev.currentTarget = node;
  ev.eventPhase = phase;
  const m = node[LS];
  const a = m && m.get(ev.type);
  if (a && a.length) {
    for (const l of a.slice()) {
      if (phase === 1 && !l.capture) continue;
      if (phase === 3 && l.capture) continue;
      if (l.once) { const i = a.indexOf(l); if (i >= 0) a.splice(i, 1); }
      try { if (typeof l.fn === 'function') l.fn.call(node, ev); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(ev); } catch (err) { reportErr(err); }
      if (ev._imm) return;
    }
  }
  if (phase !== 1) {
    let h = null;
    try { h = node['on' + ev.type]; } catch (e) {}
    if (typeof h !== 'function') { h = inlineHandler(node, ev.type); if (!h && node === G && (ev.type === 'load' || ev.type === 'unload' || ev.type === 'beforeunload' || ev.type === 'resize' || ev.type === 'hashchange' || ev.type === 'popstate')) { const b = document.body; if (b) h = inlineHandler(b, ev.type); } }
    if (typeof h === 'function') {
      try { const r = h.call(node, ev); if (r === false) ev.preventDefault(); } catch (err) { reportErr(err); }
    }
  }
}
const OH = Symbol('onh');
const EVNAMES = 'abort animationend animationiteration animationstart auxclick beforeinput beforeunload blur cancel canplay canplaythrough change click close contextmenu copy cut dblclick drag dragend dragenter dragleave dragover dragstart drop durationchange ended error focus focusin focusout formdata hashchange input invalid keydown keypress keyup load loadeddata loadedmetadata loadend loadstart message mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup offline online pagehide pageshow paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup popstate progress ratechange reset resize scroll scrollend search seeked seeking select selectionchange selectstart slotchange stalled storage submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitioncancel transitionend transitionrun transitionstart unload volumechange waiting wheel'.split(' ');
function defOn(o) {
  for (const t of EVNAMES) {
    try {
      Object.defineProperty(o, 'on' + t, {
        configurable: true, enumerable: true,
        get() { const m = this[OH]; if (m && Object.prototype.hasOwnProperty.call(m, t)) return m[t]; return inlineHandler(this, t) || null; },
        set(v) { let m = this[OH]; if (!m) { m = {}; try { Object.defineProperty(this, OH, { value: m, configurable: true }); } catch (e) { return; } } m[t] = (typeof v === 'function' || (v && typeof v === 'object')) ? v : null; }
      });
    } catch (e) {}
  }
}
function reportErr(err) { try { N.log((err && err.stack) ? (err.name + ': ' + err.message + '\n' + err.stack) : String(err), 2); } catch (e) {} }
function run(target, ev) {
  const path = [];
  let isNode = target && typeof target.nodeType === 'number';
  if (isNode) { for (let n = target; n; n = n.parentNode || (n.nodeType === 11 ? N.shadowHost(n) : null)) path.push(n); if (path[path.length - 1] === document && ev.type !== 'load') path.push(G); }
  else path.push(target);
  ev.target = target;
  ev._path = path;
  for (let i = path.length - 1; i >= 1 && !ev._stop; i--) invoke(path[i], ev, 1);
  if (!ev._stop) invoke(target, ev, 2);
  if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) invoke(path[i], ev, 3);
  ev.currentTarget = null;
  ev.eventPhase = 0;
  return ev.defaultPrevented;
}
const keyNames = { 8: ['Backspace', 8], 9: ['Tab', 9], 10: ['Enter', 13], 13: ['Enter', 13], 27: ['Escape', 27], 127: ['Delete', 46], 19: ['ArrowUp', 38], 20: ['ArrowDown', 40], 21: ['ArrowLeft', 37], 22: ['ArrowRight', 39], 23: ['PageUp', 33], 24: ['PageDown', 34], 25: ['Home', 36], 26: ['End', 35], 32: [' ', 32] };
G.__dispatch = function (t, type, bubbles, cx, cy, key) {
  let ev;
  if (/^(click|dblclick|mousedown|mouseup|mousemove|mouseover|mouseout|mouseenter|mouseleave|contextmenu|auxclick)$/.test(type)) ev = new MouseEvent(type, { bubbles: !!bubbles, cancelable: true, clientX: cx, clientY: cy, button: 0, buttons: type === 'mousedown' ? 1 : 0 });
  else if (/^pointer/.test(type)) ev = new PointerEvent(type, { bubbles: !!bubbles, cancelable: true, clientX: cx, clientY: cy });
  else if (type === 'wheel') ev = new WheelEvent(type, { bubbles: true, cancelable: true, clientX: cx, clientY: cy, deltaY: key });
  else if (/^key/.test(type)) {
    const k = keyNames[key];
    const ch = key >= 32 && key < 0x110000 && !k ? String.fromCodePoint(key) : null;
    const name = k ? k[0] : ch || '';
    const code = k ? k[1] : ch ? ch.toUpperCase().charCodeAt(0) : 0;
    ev = new KeyboardEvent(type, { bubbles: true, cancelable: true, key: name, keyCode: code, charCode: type === 'keypress' ? (ch ? ch.charCodeAt(0) : code) : 0, code: ch && /[a-z]/i.test(ch) ? 'Key' + ch.toUpperCase() : ch && /[0-9]/.test(ch) ? 'Digit' + ch : name });
  }
  else if (type === 'input' || type === 'beforeinput') { const it = key === 8 ? 'deleteContentBackward' : key === 127 ? 'deleteContentForward' : key === 10 ? 'insertLineBreak' : 'insertText'; ev = new InputEvent(type, { bubbles: true, cancelable: type === 'beforeinput', inputType: it, data: key >= 32 && key !== 127 ? String.fromCodePoint(key) : null }); }
  else if (type === 'focus' || type === 'blur' || type === 'focusin' || type === 'focusout') ev = new FocusEvent(type, { bubbles: !!bubbles });
  else if (type === 'submit') ev = new SubmitEvent(type, { bubbles: true, cancelable: true });
  else ev = new Event(type, { bubbles: !!bubbles, cancelable: true });
  ev.isTrusted = true;
  return run(t, ev);
};

function mkCtor(name, proto, parent) {
  const f = { [name]: function () { throw new TypeError('Illegal constructor'); } }[name];
  f.prototype = proto;
  val(proto, 'constructor', f);
  if (parent) Object.setPrototypeOf(f, parent);
  val(G, name, f);
  return f;
}
const NodeC = mkCtor('Node', P.Node, EventTarget);
Object.assign(NodeC, { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, PROCESSING_INSTRUCTION_NODE: 7, COMMENT_NODE: 8, DOCUMENT_NODE: 9, DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11, DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16 });
for (const k of ['ELEMENT_NODE', 'TEXT_NODE', 'COMMENT_NODE', 'DOCUMENT_NODE', 'DOCUMENT_FRAGMENT_NODE']) val(P.Node, k, NodeC[k]);
const CharacterData = mkCtor('CharacterData', P.CharacterData, NodeC);
const TextC = mkCtor('Text', P.Text, CharacterData);
const CommentC = mkCtor('Comment', P.Comment, CharacterData);
const ElementC = mkCtor('Element', P.Element, NodeC);
const SVGElementC = mkCtor('SVGElement', P.SVGElement, ElementC);
const DocumentC = mkCtor('Document', P.Document, NodeC);
const FragC = mkCtor('DocumentFragment', P.DocumentFragment, NodeC);
mkCtor('HTMLDocument', P.Document, DocumentC);
const ceByCtor = new Map(), ceByName = new Map();
const HTMLElementC = function HTMLElement() {
  if (!new.target) throw new TypeError("Failed to construct 'HTMLElement'");
  const name = ceByCtor.get(new.target);
  if (!name) throw new TypeError('Illegal constructor');
  return N.newElement(name, new.target.prototype);
};
HTMLElementC.prototype = P.HTMLElement;
val(P.HTMLElement, 'constructor', HTMLElementC);
Object.setPrototypeOf(HTMLElementC, ElementC);
val(G, 'HTMLElement', HTMLElementC);
G.Text = function Text(s) { return document.createTextNode(s === undefined ? '' : String(s)); };
G.Text.prototype = P.Text;
G.Comment = function Comment(s) { return document.createComment(s === undefined ? '' : String(s)); };
G.Comment.prototype = P.Comment;
G.DocumentFragment = function DocumentFragment() { return document.createDocumentFragment(); };
G.DocumentFragment.prototype = P.DocumentFragment;
const tagCtors = {
  HTMLAnchorElement: ['a'], HTMLImageElement: ['img'], HTMLInputElement: ['input'], HTMLDivElement: ['div'], HTMLSpanElement: ['span'], HTMLParagraphElement: ['p'],
  HTMLFormElement: ['form'], HTMLButtonElement: ['button'], HTMLSelectElement: ['select'], HTMLOptionElement: ['option'], HTMLOptGroupElement: ['optgroup'], HTMLTextAreaElement: ['textarea'],
  HTMLUListElement: ['ul'], HTMLOListElement: ['ol'], HTMLLIElement: ['li'], HTMLTableElement: ['table'], HTMLTableRowElement: ['tr'], HTMLTableCellElement: ['td', 'th'],
  HTMLTableSectionElement: ['thead', 'tbody', 'tfoot'], HTMLScriptElement: ['script'], HTMLStyleElement: ['style'], HTMLLinkElement: ['link'], HTMLCanvasElement: ['canvas'],
  HTMLVideoElement: ['video'], HTMLAudioElement: ['audio'], HTMLIFrameElement: ['iframe'], HTMLLabelElement: ['label'], HTMLTemplateElement: ['template'], HTMLBodyElement: ['body'],
  HTMLHeadElement: ['head'], HTMLHtmlElement: ['html'], HTMLHeadingElement: ['h1', 'h2', 'h3', 'h4', 'h5', 'h6'], HTMLBRElement: ['br'], HTMLHRElement: ['hr'], HTMLPreElement: ['pre'],
  HTMLMetaElement: ['meta'], HTMLTitleElement: ['title'], HTMLDetailsElement: ['details'], HTMLDialogElement: ['dialog'], HTMLSourceElement: ['source'], HTMLPictureElement: ['picture'],
  HTMLFieldSetElement: ['fieldset'], HTMLLegendElement: ['legend'], HTMLProgressElement: ['progress'], HTMLMeterElement: ['meter'], HTMLBaseElement: ['base'], HTMLObjectElement: ['object'],
  HTMLEmbedElement: ['embed'], HTMLQuoteElement: ['blockquote', 'q'], HTMLDListElement: ['dl'], HTMLTableCaptionElement: ['caption'], HTMLMapElement: ['map'], HTMLAreaElement: ['area'],
  HTMLOutputElement: ['output'], HTMLDataElement: ['data'], HTMLTimeElement: ['time'], HTMLModElement: ['ins', 'del'], HTMLMenuElement: ['menu'], HTMLSlotElement: ['slot'], HTMLUnknownElement: []
};
const tagProtos = {};
for (const [name, tags] of Object.entries(tagCtors)) {
  const proto = Object.create(P.HTMLElement);
  mkCtor(name, proto, HTMLElementC);
  for (const t of tags) { N.setProto(t, proto); tagProtos[t] = proto; }
}
function findDesc(o, k) { while (o) { const d = Object.getOwnPropertyDescriptor(o, k); if (d) return d; o = Object.getPrototypeOf(o); } return undefined; }
for (const [t, ks] of [['input', ['value', 'checked', 'defaultValue', 'defaultChecked', 'selectionStart', 'selectionEnd']], ['textarea', ['value', 'defaultValue', 'selectionStart', 'selectionEnd']], ['select', ['value', 'selectedIndex']], ['option', ['value', 'selected']]]) {
  const pr = tagProtos[t];
  for (const k of ks) { const d = findDesc(pr, k); if (d && !Object.prototype.hasOwnProperty.call(pr, k)) { try { Object.defineProperty(pr, k, d); } catch (e) {} } }
}
defOn(P.Element); defOn(P.Document); defOn(G);
G.HTMLMediaElement = G.HTMLVideoElement;
G.SVGSVGElement = SVGElementC;
G.SVGGraphicsElement = SVGElementC;

const document = N.document;
val(G, 'document', document);
const hasCls = (el, a) => { const v = el.getAttribute(a); return v ? v.split(/\s+/).filter(Boolean) : []; };
class DOMTokenList {
  constructor(el, attr) { this._e = el; this._a = attr; }
  _set(a) { this._e.setAttribute(this._a, a.join(' ')); }
  add(...t) { const a = hasCls(this._e, this._a); let ch = false; for (const x of t) { const s = String(x); if (!a.includes(s)) { a.push(s); ch = true; } } if (ch || !this._e.hasAttribute(this._a)) this._set(a); }
  remove(...t) { const a = hasCls(this._e, this._a); const b = a.filter(x => !t.map(String).includes(x)); if (b.length !== a.length) this._set(b); }
  toggle(t, f) { t = String(t); const a = hasCls(this._e, this._a); const has = a.includes(t); const want = f === undefined ? !has : !!f; if (want && !has) { a.push(t); this._set(a); } else if (!want && has) { a.splice(a.indexOf(t), 1); this._set(a); } return want; }
  contains(t) { return hasCls(this._e, this._a).includes(String(t)); }
  replace(x, y) { const a = hasCls(this._e, this._a); const i = a.indexOf(x); if (i < 0) return false; a[i] = y; this._set(a); return true; }
  item(i) { const a = hasCls(this._e, this._a); return i < a.length ? a[i] : null; }
  supports() { return true; }
  get length() { return hasCls(this._e, this._a).length; }
  get value() { return this._e.getAttribute(this._a) || ''; }
  set value(v) { this._e.setAttribute(this._a, v); }
  toString() { return this.value; }
  forEach(f, th) { hasCls(this._e, this._a).forEach((x, i) => f.call(th, x, i, this)); }
  entries() { return hasCls(this._e, this._a).entries(); }
  keys() { return hasCls(this._e, this._a).keys(); }
  values() { return hasCls(this._e, this._a).values(); }
  [Symbol.iterator]() { return hasCls(this._e, this._a)[Symbol.iterator](); }
}
G.DOMTokenList = DOMTokenList;
const cache = (el, k, mk) => { let v = el[k]; if (!v) { v = mk(); val(el, k, v); } return v; };
def(P.Element, 'classList', function () { return cache(this, '__cl', () => new DOMTokenList(this, 'class')); }, function (v) { this.setAttribute('class', String(v)); });
def(P.Element, 'relList', function () { return cache(this, '__rl', () => new DOMTokenList(this, 'rel')); });
const dsKey = k => 'data-' + k.replace(/[A-Z]/g, m => '-' + m.toLowerCase());
const dsName = a => a.slice(5).replace(/-([a-z])/g, (m, c) => c.toUpperCase());
def(P.Element, 'dataset', function () {
  const el = this;
  return cache(this, '__ds', () => new Proxy({}, {
    get(t, k) { if (typeof k !== 'string') return undefined; const v = el.getAttribute(dsKey(k)); return v === null ? undefined : v; },
    set(t, k, v) { el.setAttribute(dsKey(k), String(v)); return true; },
    has(t, k) { return el.hasAttribute(dsKey(k)); },
    deleteProperty(t, k) { el.removeAttribute(dsKey(k)); return true; },
    ownKeys() { return el.getAttributeNames().filter(a => a.startsWith('data-')).map(dsName); },
    getOwnPropertyDescriptor(t, k) { const v = el.getAttribute(dsKey(k)); return v === null ? undefined : { value: v, writable: true, enumerable: true, configurable: true }; }
  }));
});
function styleDecl(el) {
  const target = {};
  return new Proxy(target, {
    get(t, k) {
      if (typeof k !== 'string') return undefined;
      switch (k) {
        case 'cssText': return el.__styleText;
        case 'setProperty': return (p, v, prio) => { N.styleSet(el, kebab(p), v == null ? '' : String(v) + (prio ? ' !important' : '')); };
        case 'getPropertyValue': return p => N.styleGet(el, kebab(p));
        case 'getPropertyPriority': return () => '';
        case 'removeProperty': return p => { const o = N.styleGet(el, kebab(p)); N.styleSet(el, kebab(p), ''); return o; };
        case 'length': return (el.__styleText.match(/:/g) || []).length;
        case 'item': return i => (el.__styleText.split(';')[i] || '').split(':')[0].trim();
        case 'parentRule': return null;
        case 'toString': return () => el.__styleText;
      }
      if (/^\d+$/.test(k)) return (el.__styleText.split(';')[+k] || '').split(':')[0].trim();
      return N.styleGet(el, kebab(k));
    },
    set(t, k, v) { if (typeof k !== 'string') return true; if (k === 'cssText') el.__styleText = String(v); else N.styleSet(el, kebab(k), v == null ? '' : String(v)); return true; },
    has(t, k) { return typeof k === 'string'; }
  });
}
def(P.Element, 'style', function () { return cache(this, '__st', () => styleDecl(this)); }, function (v) { this.__styleText = String(v); });
const toNode = x => (x && typeof x.nodeType === 'number') ? x : document.createTextNode(String(x));
const mixChild = {
  remove() { this.__remove(); },
  before(...n) { const p = this.parentNode; if (!p) return; for (const x of n) p.insertBefore(toNode(x), this); },
  after(...n) { const p = this.parentNode; if (!p) return; let ref = this.nextSibling; for (const x of n) p.insertBefore(toNode(x), ref); },
  replaceWith(...n) { const p = this.parentNode; if (!p) return; const ref = this.nextSibling; this.__remove(); for (const x of n) p.insertBefore(toNode(x), ref); }
};
const mixParent = {
  append(...n) { for (const x of n) this.appendChild(toNode(x)); },
  prepend(...n) { const f = this.firstChild; for (const x of n) this.insertBefore(toNode(x), f); },
  replaceChildren(...n) { while (this.firstChild) this.removeChild(this.firstChild); for (const x of n) this.appendChild(toNode(x)); }
};
for (const [k, f] of Object.entries(mixChild)) { val(P.Element, k, f); val(P.CharacterData, k, f); }
for (const [k, f] of Object.entries(mixParent)) { val(P.Element, k, f); val(P.Document, k, f); val(P.DocumentFragment, k, f); }
Object.assign(P.Node, {});
val(P.Node, 'getRootNode', function () { let n = this; while (n.parentNode) n = n.parentNode; return n; });
val(P.Node, 'isSameNode', function (o) { return this === o; });
val(P.Node, 'isEqualNode', function (o) { return !!o && this.nodeType === o.nodeType && (this.outerHTML !== undefined ? this.outerHTML === o.outerHTML : this.textContent === o.textContent); });
val(P.Node, 'normalize', function () { const w = n => { let c = n.firstChild; while (c) { const nx = c.nextSibling; if (c.nodeType === 3) { if (!c.data.length) { c.remove(); } else { while (c.nextSibling && c.nextSibling.nodeType === 3) { c.data += c.nextSibling.data; c.nextSibling.remove(); } } } else if (c.nodeType === 1) w(c); c = c.nodeType === 3 && c.parentNode ? c.nextSibling : nx; } }; w(this); });
val(P.Node, 'compareDocumentPosition', function (o) {
  if (o === this) return 0;
  const ca = [], cb = [];
  for (let n = this; n; n = n.parentNode) ca.unshift(n);
  for (let n = o; n; n = n.parentNode) cb.unshift(n);
  if (ca[0] !== cb[0]) return 1 | 32 | 4;
  let i = 0; while (i < ca.length && i < cb.length && ca[i] === cb[i]) i++;
  if (i === ca.length) return 16 | 4;
  if (i === cb.length) return 8 | 2;
  for (let n = ca[i].nextSibling; n; n = n.nextSibling) if (n === cb[i]) return 4;
  return 2;
});
def(P.Node, 'baseURI', function () { return N.url(); });
val(P.Node, 'lookupNamespaceURI', function () { return null; });
def(P.CharacterData, 'length', function () { return this.data.length; });
val(P.CharacterData, 'appendData', function (s) { this.data += s; });
val(P.CharacterData, 'insertData', function (o, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o); });
val(P.CharacterData, 'deleteData', function (o, c) { const d = this.data; this.data = d.slice(0, o) + d.slice(o + c); });
val(P.CharacterData, 'replaceData', function (o, c, s) { const d = this.data; this.data = d.slice(0, o) + s + d.slice(o + c); });
val(P.CharacterData, 'substringData', function (a, b) { return this.data.substr(a, b); });
def(P.Text, 'wholeText', function () { return this.data; });
val(P.Text, 'splitText', function (o) { const t = document.createTextNode(this.data.slice(o)); this.data = this.data.slice(0, o); this.after(t); return t; });
val(P.Element, 'toggleAttribute', function (n, f) { const h = this.hasAttribute(n); const w = f === undefined ? !h : !!f; if (w && !h) this.setAttribute(n, ''); if (!w && h) this.removeAttribute(n); return w; });
val(P.Element, 'hasAttributes', function () { return this.getAttributeNames().length > 0; });
val(P.Element, 'getAttributeNS', function (ns, n) { return this.getAttribute(n); });
val(P.Element, 'setAttributeNS', function (ns, n, v) { this.setAttribute(n.replace(/^.*:/, (m) => m.startsWith('xlink:') ? 'xlink:' : ''), v); });
val(P.Element, 'removeAttributeNS', function (ns, n) { this.removeAttribute(n); });
val(P.Element, 'hasAttributeNS', function (ns, n) { return this.hasAttribute(n); });
val(P.Element, 'getAttributeNode', function (n) { const v = this.getAttribute(n); return v === null ? null : { name: n, value: v, nodeValue: v }; });
val(P.Element, 'click', function () { N.click(this); });
val(P.Element, 'getClientRects', function () { const r = this.getBoundingClientRect(); return r.width || r.height ? [r] : []; });
val(P.Element, 'scrollTo', function (x, y) { if (x && typeof x === 'object') y = x.top; if (typeof y === 'number') this.scrollTop = y; });
val(P.Element, 'scroll', P.Element.scrollTo);
val(P.Element, 'scrollBy', function (x, y) { if (x && typeof x === 'object') y = x.top; this.scrollTop += (y || 0); });
val(P.Element, 'animate', function () { const a = { finished: Promise.resolve(), cancel() {}, play() {}, pause() {}, finish() {}, reverse() {}, onfinish: null, playState: 'finished', addEventListener() {}, removeEventListener() {} }; setTimeout(() => { a.onfinish && a.onfinish(); }, 0); return a; });
val(P.Element, 'getAnimations', function () { return []; });
const SRP = Object.create(P.DocumentFragment);
def(SRP, 'host', function () { return N.shadowHost(this); });
def(SRP, 'mode', function () { return this.__mode || 'open'; });
def(SRP, 'delegatesFocus', function () { return false; });
def(SRP, 'activeElement', function () { const a = document.activeElement; return a && this.contains(a) ? a : null; });
def(SRP, 'innerHTML', function () { let s = ''; for (const c of this.childNodes) s += c.nodeType === 1 ? c.outerHTML : c.nodeType === 3 ? c.textContent.replace(/&/g, '&amp;').replace(/</g, '&lt;') : ''; return s; }, function (v) { while (this.firstChild) this.removeChild(this.firstChild); const t = document.createElement('template'); t.innerHTML = String(v); this.appendChild(t.content); });
val(SRP, 'getElementById', function (id) { return this.querySelector('#' + CSS.escape(String(id))); });
val(SRP, 'getSelection', function () { return document.getSelection(); });
def(SRP, 'adoptedStyleSheets', function () { return this.__ass || []; }, function (v) { val(this, '__ass', v); const t = (v || []).map(s => s.__text || '').join('\n'); let st = this.__assel; if (!st) { st = document.createElement('style'); val(this, '__assel', st); this.insertBefore(st, this.firstChild); } st.textContent = t; });
G.ShadowRoot = function ShadowRoot() { throw new TypeError('Illegal constructor'); };
G.ShadowRoot.prototype = SRP;
val(P.Element, 'attachShadow', function (o) { const sr = N.attachShadow(this); if (!sr) throw new DOMException('Not supported', 'NotSupportedError'); Object.setPrototypeOf(sr, SRP); val(sr, '__mode', (o && o.mode) || 'open'); return sr; });
def(P.Element, 'shadowRoot', function () { const s = N.getShadow(this); return s && s.__mode !== 'closed' ? s : null; });
val(P.Element, 'assignedSlot', null);
val(P.Node, 'getRootNode', function (o) { let n = this; for (;;) { const p = n.parentNode; if (!p) break; n = p; } if (o && o.composed && n.nodeType === 11 && N.shadowHost(n)) return N.shadowHost(n).getRootNode(o); return n; });
let FSEL = null;
val(P.Element, 'requestFullscreen', function () { if (FSEL === this) return Promise.resolve(); if (FSEL) document.exitFullscreen(); FSEL = this; val(this, '__fsst', this.getAttribute('style')); this.setAttribute('style', (this.getAttribute('style') || '') + ';position:fixed!important;left:0!important;top:0!important;width:100vw!important;height:100vh!important;max-width:none!important;max-height:none!important;margin:0!important;z-index:2147483647!important;background:#000!important'); setTimeout(() => { run(document, new Event('fullscreenchange', { bubbles: true })); run(this, new Event('fullscreenchange', { bubbles: true })); }, 0); return Promise.resolve(); });
val(P.Element, 'webkitRequestFullscreen', function () { return this.requestFullscreen(); });
val(P.Document, 'exitFullscreen', function () { const e = FSEL; if (!e) return Promise.resolve(); FSEL = null; if (e.__fsst == null) e.removeAttribute('style'); else e.setAttribute('style', e.__fsst); setTimeout(() => { run(document, new Event('fullscreenchange', { bubbles: true })); run(e, new Event('fullscreenchange', { bubbles: true })); }, 0); return Promise.resolve(); });
val(P.Document, 'webkitExitFullscreen', function () { return this.exitFullscreen(); });
val(P.Element, 'setPointerCapture', function () {});
val(P.Element, 'releasePointerCapture', function () {});
val(P.Element, 'hasPointerCapture', function () { return false; });
val(P.Element, 'checkVisibility', function () { const r = this.getBoundingClientRect(); return r.width > 0 || r.height > 0; });
val(P.Element, 'computedStyleMap', function () { const el = this; return { get(p) { return { value: N.computed(el, p), toString() { return N.computed(el, p); } }; } }; });
def(P.Element, 'namespaceURI', function () { return this instanceof SVGElementC ? 'http://www.w3.org/2000/svg' : 'http://www.w3.org/1999/xhtml'; });
def(P.Element, 'prefix', function () { return null; });
def(P.Element, 'slot', function () { return this.getAttribute('slot') || ''; });
def(P.Element, 'part', function () { return cache(this, '__pt', () => new DOMTokenList(this, 'part')); });
def(P.HTMLElement, 'accessKey', function () { return this.getAttribute('accesskey') || ''; });
def(P.HTMLElement, 'inert', function () { return this.hasAttribute('inert'); });
def(P.HTMLElement, 'offsetParent', Object.getOwnPropertyDescriptor(P.Element, 'offsetParent').get);
def(P.HTMLElement, 'translate', function () { return true; });
val(P.HTMLElement, 'showPopover', function () { this.setAttribute('open', ''); });
val(P.HTMLElement, 'hidePopover', function () { this.removeAttribute('open'); });
val(P.HTMLElement, 'togglePopover', function () { this.toggleAttribute('open'); });
val(P.HTMLElement, 'attachInternals', function () { return { setFormValue() {}, setValidity() {}, states: new Set() }; });
const T = tagProtos;
def(T.template, 'content', function () { return cache(this, '__ct', () => N.templateContent(this)); });
def(T.script, 'text', function () { return this.textContent; }, function (v) { this.textContent = v; });
val(T.form, 'submit', function () { N.submit(this, null); });
val(T.form, 'requestSubmit', function (s) { const ev = new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter: s || null }); if (this.dispatchEvent(ev)) N.submit(this, s || null); });
val(T.form, 'reset', function () { for (const e of this.elements) { if (e.type === 'checkbox' || e.type === 'radio') e.checked = e.defaultChecked; else if ('defaultValue' in e) e.value = e.defaultValue; } this.dispatchEvent(new Event('reset', { bubbles: true })); });
val(T.form, 'checkValidity', function () { return true; });
val(T.form, 'reportValidity', function () { return true; });
for (const p of [T.input, T.textarea, T.select, T.button, T.fieldset, T.output]) {
  val(p, 'checkValidity', function () { return true; });
  val(p, 'reportValidity', function () { return true; });
  val(p, 'setCustomValidity', function () {});
  def(p, 'validity', function () { return { valid: true, valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false, tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false, customError: false }; });
  def(p, 'validationMessage', function () { return ''; });
  def(p, 'willValidate', function () { return true; });
  def(p, 'labels', function () { return this.id ? [...document.querySelectorAll('label[for="' + this.id + '"]')] : []; });
}
for (const p of [T.input, T.textarea]) {
  val(p, 'select', function () {});
  val(p, 'setSelectionRange', function (a) { this.selectionStart = a; });
  val(p, 'setRangeText', function (t) { this.value = this.value + t; });
  def(p, 'selectionDirection', function () { return 'none'; });
}
val(T.input, 'stepUp', function (n) { this.value = String((+this.value || 0) + (n || 1) * (+this.step || 1)); });
val(T.input, 'stepDown', function (n) { this.value = String((+this.value || 0) - (n || 1) * (+this.step || 1)); });
val(T.input, 'showPicker', function () {});
def(T.input, 'files', function () { return []; });
def(T.input, 'indeterminate', function () { return false; }, function () {});
val(T.select, 'item', function (i) { return this.options[i] || null; });
val(T.select, 'namedItem', function (n) { return this.options.find(o => o.id === n || o.getAttribute('name') === n) || null; });
val(T.select, 'add', function (o, b) { if (typeof b === 'number') b = this.options[b]; this.insertBefore(o, b || null); });
val(T.select, 'remove', function (i) { if (i === undefined) { this.__remove(); return; } const o = this.options[i]; if (o) o.__remove(); });
def(T.select, 'selectedOptions', function () { const o = this.options[this.selectedIndex]; return o ? [o] : []; });
def(T.label, 'control', function () { const f = this.htmlFor; return f ? document.getElementById(f) : this.querySelector('input,select,textarea,button'); });
def(T.option, 'form', function () { return this.closest('form'); });
for (const p of [T.img]) { val(p, 'decode', function () { return Promise.resolve(); }); def(p, 'x', function () { return this.getBoundingClientRect().left; }); def(p, 'y', function () { return this.getBoundingClientRect().top; }); }
val(T.dialog, 'show', function () { this.setAttribute('open', ''); });
val(T.dialog, 'showModal', function () { this.setAttribute('open', ''); });
val(T.dialog, 'close', function (r) { this.removeAttribute('open'); if (r !== undefined) this.returnValue = r; this.dispatchEvent(new Event('close')); });
def(T.iframe, 'contentWindow', function () { return null; });
def(T.iframe, 'contentDocument', function () { return null; });
for (const k of ['protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin', 'username', 'password']) {
  def(T.a, k, function () { try { return new URL(this.href)[k]; } catch (e) { return ''; } }, function (v) { try { const u = new URL(this.href); u[k] = v; this.href = u.href; } catch (e) {} });
  def(T.area, k, function () { try { return new URL(this.href)[k]; } catch (e) { return ''; } });
}
val(T.a, 'toString', function () { return this.href; });
def(T.link, 'sheet', function () { return { cssRules: [], insertRule() { return 0; }, deleteRule() {} }; });
def(T.style, 'sheet', function () { const el = this; return { get cssRules() { return []; }, insertRule(r) { el.textContent += r; return 0; }, deleteRule() {}, disabled: false }; });
const ctx2d = {
  canvas: null, fillStyle: '#000', strokeStyle: '#000', lineWidth: 1, font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', globalAlpha: 1, globalCompositeOperation: 'source-over', lineCap: 'butt', lineJoin: 'miter', shadowBlur: 0, shadowColor: 'transparent', imageSmoothingEnabled: true
};
for (const m of ['fillRect', 'strokeRect', 'clearRect', 'beginPath', 'closePath', 'moveTo', 'lineTo', 'arc', 'arcTo', 'rect', 'fill', 'stroke', 'save', 'restore', 'scale', 'rotate', 'translate', 'transform', 'setTransform', 'resetTransform', 'drawImage', 'fillText', 'strokeText', 'quadraticCurveTo', 'bezierCurveTo', 'clip', 'putImageData', 'setLineDash', 'ellipse', 'roundRect']) ctx2d[m] = function () {};
ctx2d.measureText = function (t) { return { width: String(t).length * 6, actualBoundingBoxAscent: 8, actualBoundingBoxDescent: 2 }; };
ctx2d.getImageData = function (x, y, w, h) { return { width: w, height: h, data: new Uint8ClampedArray(Math.max(0, w * h * 4)) }; };
ctx2d.createImageData = ctx2d.getImageData.bind(null, 0, 0);
ctx2d.createLinearGradient = ctx2d.createRadialGradient = ctx2d.createPattern = ctx2d.createConicGradient = function () { return { addColorStop() {} }; };
ctx2d.isPointInPath = function () { return false; };
ctx2d.getLineDash = function () { return []; };
ctx2d.getTransform = function () { return { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 }; };
val(T.canvas, 'getContext', function (t) { if (t !== '2d') return null; return cache(this, '__ctx', () => Object.assign(Object.create(ctx2d), { canvas: this })); });
val(T.canvas, 'toDataURL', function () { return 'data:,'; });
val(T.canvas, 'toBlob', function (cb) { setTimeout(() => cb(null), 0); });

def(P.Document, 'documentElement', function () { return this.firstElementChild; });
def(P.Document, 'head', function () { const h = this.documentElement; return h ? h.querySelector('head') : null; });
def(P.Document, 'body', function () { const h = this.documentElement; if (!h) return null; for (let c = h.firstElementChild; c; c = c.nextElementSibling) if (c.localName === 'body') return c; return null; }, function (b) { const h = this.documentElement; const o = this.body; if (o) h.replaceChild(b, o); else h.appendChild(b); });
def(P.Document, 'title', function () { return this === document ? N.title() : ''; }, function (v) { if (this === document) N.title(String(v)); });
def(P.Document, 'cookie', function () { return N.cookie(); }, function (v) { N.cookie(String(v)); });
def(P.Document, 'readyState', function () { return N.readyState(); });
def(P.Document, 'URL', function () { return N.url(); });
def(P.Document, 'documentURI', function () { return N.url(); });
def(P.Document, 'location', function () { return G.location; }, function (v) { G.location.href = v; });
def(P.Document, 'referrer', function () { return ''; });
def(P.Document, 'domain', function () { return G.location.hostname; }, function () {});
def(P.Document, 'defaultView', function () { return G; });
def(P.Document, 'activeElement', function () { return N.active(); });
def(P.Document, 'forms', function () { return this.querySelectorAll('form'); });
def(P.Document, 'images', function () { return this.querySelectorAll('img'); });
def(P.Document, 'links', function () { return this.querySelectorAll('a[href],area[href]'); });
def(P.Document, 'scripts', function () { return this.querySelectorAll('script'); });
def(P.Document, 'styleSheets', function () { return [...this.querySelectorAll('style,link[rel~=stylesheet]')].map(e => e.sheet); });
def(P.Document, 'characterSet', function () { return 'UTF-8'; });
def(P.Document, 'charset', function () { return 'UTF-8'; });
def(P.Document, 'inputEncoding', function () { return 'UTF-8'; });
def(P.Document, 'contentType', function () { return 'text/html'; });
def(P.Document, 'compatMode', function () { return 'CSS1Compat'; });
def(P.Document, 'hidden', function () { return false; });
def(P.Document, 'visibilityState', function () { return 'visible'; });
def(P.Document, 'currentScript', function () { return N.currentScript(); });
def(P.Document, 'scrollingElement', function () { return this.documentElement; });
def(P.Document, 'doctype', function () { return { name: 'html', publicId: '', systemId: '' }; });
def(P.Document, 'fullscreenElement', function () { return FSEL; });
def(P.Document, 'webkitFullscreenElement', function () { return FSEL; });
def(P.Document, 'fullscreenEnabled', function () { return true; });
def(P.Document, 'pointerLockElement', function () { return null; });
def(P.Document, 'designMode', function () { return 'off'; }, function () {});
def(P.Document, 'dir', function () { return this.documentElement ? this.documentElement.getAttribute('dir') || '' : ''; });
const fontsObj = { ready: Promise.resolve(), status: 'loaded', load() { return Promise.resolve([]); }, check() { return true; }, add() {}, delete() {}, forEach() {}, addEventListener() {}, removeEventListener() {}, [Symbol.iterator]: function* () {} };
fontsObj.ready = Promise.resolve(fontsObj);
def(P.Document, 'fonts', function () { return fontsObj; });
def(P.Document, 'implementation', function () { return { createHTMLDocument(t) { return N.parseDoc('<!doctype html><title>' + (t || '') + '</title>'); }, hasFeature() { return true; }, createDocument() { return N.parseDoc(''); } }; });
val(P.Document, 'hasFocus', function () { return true; });
val(P.Document, 'open', function () { return this; });
val(P.Document, 'close', function () {});
val(P.Document, 'adoptNode', function (n) { return n; });
val(P.Document, 'importNode', function (n, d) { return n.cloneNode(!!d); });
val(P.Document, 'createEvent', function (t) { const e = /Mouse/.test(t) ? new MouseEvent('') : /Keyboard/.test(t) ? new KeyboardEvent('') : /Custom/.test(t) ? new CustomEvent('') : new Event(''); return e; });
val(P.Document, 'createAttribute', function (n) { return { name: n, value: '' }; });
val(P.Document, 'createTreeWalker', function (root, what, filter) {
  const nodes = []; const walk = n => { for (let c = n.firstChild; c; c = c.nextSibling) { nodes.push(c); walk(c); } }; walk(root);
  const ok = n => (!what || what === 0xFFFFFFFF || (what & (1 << (n.nodeType - 1)))) && (!filter || (typeof filter === 'function' ? filter(n) : filter.acceptNode(n)) === 1);
  let i = -1; const w = { root, currentNode: root, nextNode() { while (++i < nodes.length) if (ok(nodes[i])) return (w.currentNode = nodes[i]); return null; }, previousNode() { while (--i >= 0) if (ok(nodes[i])) return (w.currentNode = nodes[i]); return null; }, firstChild() { return w.nextNode(); }, parentNode() { return null; } };
  return w;
});
val(P.Document, 'createNodeIterator', P.Document.createTreeWalker);
const NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 1, SHOW_TEXT: 4, SHOW_COMMENT: 128 };
G.NodeFilter = NodeFilter;

const nodeLen = n => n.nodeType === 3 || n.nodeType === 8 ? n.data.length : n.childNodes.length;
const idxOf = n => { let i = 0; for (let c = n.previousSibling; c; c = c.previousSibling) i++; return i; };
function cmpPt(a, ao, b, bo) {
  if (a === b) return ao === bo ? 0 : ao < bo ? -1 : 1;
  const pos = a.compareDocumentPosition ? a.compareDocumentPosition(b) : 0;
  if (pos & 8) { let c = a; while (c.parentNode !== b) c = c.parentNode; return idxOf(c) < bo ? -1 : 1; }
  if (pos & 16) { let c = b; while (c.parentNode !== a) c = c.parentNode; return ao <= idxOf(c) ? -1 : 1; }
  return pos & 4 ? -1 : 1;
}
class Range {
  constructor() { this.startContainer = document; this.startOffset = 0; this.endContainer = document; this.endOffset = 0; }
  get collapsed() { return this.startContainer === this.endContainer && this.startOffset === this.endOffset; }
  get commonAncestorContainer() { let a = this.startContainer; while (a && !(a === this.endContainer || a.contains(this.endContainer))) a = a.parentNode; return a || document; }
  _ch() { if (cmpPt(this.startContainer, this.startOffset, this.endContainer, this.endOffset) > 0) { this.endContainer = this.startContainer; this.endOffset = this.startOffset; } if (this._sel) this._sel._sync(); }
  setStart(n, o) { this.startContainer = n; this.startOffset = Math.min(o, nodeLen(n)); if (this.endContainer === document || cmpPt(this.startContainer, this.startOffset, this.endContainer, this.endOffset) > 0) { this.endContainer = n; this.endOffset = this.startOffset; } this._ch(); }
  setEnd(n, o) { this.endContainer = n; this.endOffset = Math.min(o, nodeLen(n)); if (cmpPt(this.startContainer, this.startOffset, this.endContainer, this.endOffset) > 0 || this.startContainer === document) { this.startContainer = n; this.startOffset = this.endOffset; } if (this._sel) this._sel._sync(); }
  setStartBefore(n) { this.setStart(n.parentNode, idxOf(n)); } setStartAfter(n) { this.setStart(n.parentNode, idxOf(n) + 1); }
  setEndBefore(n) { this.setEnd(n.parentNode, idxOf(n)); } setEndAfter(n) { this.setEnd(n.parentNode, idxOf(n) + 1); }
  selectNode(n) { this.setStart(n.parentNode, idxOf(n)); this.setEnd(n.parentNode, idxOf(n) + 1); }
  selectNodeContents(n) { this.startContainer = n; this.startOffset = 0; this.endContainer = n; this.endOffset = nodeLen(n); if (this._sel) this._sel._sync(); }
  collapse(toStart) { if (toStart) { this.endContainer = this.startContainer; this.endOffset = this.startOffset; } else { this.startContainer = this.endContainer; this.startOffset = this.endOffset; } if (this._sel) this._sel._sync(); }
  cloneRange() { const r = new Range(); r.startContainer = this.startContainer; r.startOffset = this.startOffset; r.endContainer = this.endContainer; r.endOffset = this.endOffset; return r; }
  detach() {}
  compareBoundaryPoints(how, r) { const a = how === 0 || how === 3 ? [this.startContainer, this.startOffset] : [this.endContainer, this.endOffset]; const b = how === 0 || how === 1 ? [r.startContainer, r.startOffset] : [r.endContainer, r.endOffset]; return cmpPt(a[0], a[1], b[0], b[1]); }
  comparePoint(n, o) { if (cmpPt(n, o, this.startContainer, this.startOffset) < 0) return -1; if (cmpPt(n, o, this.endContainer, this.endOffset) > 0) return 1; return 0; }
  isPointInRange(n, o) { return this.comparePoint(n, o) === 0; }
  intersectsNode(n) { const p = n.parentNode; if (!p) return true; const i = idxOf(n); return cmpPt(p, i, this.endContainer, this.endOffset) < 0 && cmpPt(p, i + 1, this.startContainer, this.startOffset) > 0; }
  _nodes() { const out = []; const root = this.commonAncestorContainer; const walk = n => { for (let c = n.firstChild; c; c = c.nextSibling) { if (c.nodeType === 3 && this.intersectsNode(c)) out.push(c); walk(c); } }; if (root.nodeType === 3) out.push(root); else walk(root); return out; }
  toString() { let t = ''; for (const n of this._nodes()) { const a = n === this.startContainer ? this.startOffset : 0, b = n === this.endContainer ? this.endOffset : n.data.length; t += n.data.slice(a, b); } return t; }
  deleteContents() {
    if (this.collapsed) return;
    const sc = this.startContainer, so = this.startOffset, ec = this.endContainer, eo = this.endOffset;
    if (sc === ec && sc.nodeType === 3) { sc.data = sc.data.slice(0, so) + sc.data.slice(eo); this.collapse(true); return; }
    const texts = this._nodes();
    for (const n of texts) { if (n === sc) n.data = n.data.slice(0, so); else if (n === ec) n.data = n.data.slice(eo); else n.data = ''; }
    const kill = []; const root = this.commonAncestorContainer;
    const walk = n => { for (let c = n.firstChild; c; c = c.nextSibling) { if (c.nodeType === 1 && c !== sc && c !== ec && !c.contains(sc) && !c.contains(ec) && this.intersectsNode(c) && cmpPt(c.parentNode, idxOf(c), sc, so) >= 0 && cmpPt(c.parentNode, idxOf(c) + 1, ec, eo) <= 0) kill.push(c); else walk(c); } };
    if (root.nodeType === 1) walk(root);
    for (const k of kill) k.remove();
    this.endContainer = sc; this.endOffset = so; if (this._sel) this._sel._sync();
  }
  extractContents() { const f = document.createDocumentFragment(); const t = this.toString(); if (t) f.appendChild(document.createTextNode(t)); this.deleteContents(); return f; }
  cloneContents() { const f = document.createDocumentFragment(); const t = this.toString(); if (t) f.appendChild(document.createTextNode(t)); return f; }
  insertNode(n) { const sc = this.startContainer; if (sc.nodeType === 3) { const rest = sc.splitText(this.startOffset); sc.parentNode.insertBefore(n, rest); } else sc.insertBefore(n, sc.childNodes[this.startOffset] || null); }
  surroundContents(p) { const f = this.extractContents(); p.appendChild(f); this.insertNode(p); this.selectNode(p); }
  createContextualFragment(h) { const t = document.createElement('template'); t.innerHTML = h; return t.content; }
  getClientRects() { const r = this.getBoundingClientRect(); return r.height ? [r] : []; }
  getBoundingClientRect() {
    const c = N.caretRect(this.startContainer, this.startOffset); let x = 0, y = 0, h = 0, w = 0;
    if (c) { x = c[0]; y = c[1]; h = c[2]; if (!this.collapsed) { const e = N.caretRect(this.endContainer, this.endOffset); if (e && e[1] === y) w = Math.max(0, e[0] - x); } }
    else if (this.startContainer.getBoundingClientRect && this.startContainer.nodeType === 1) return this.startContainer.getBoundingClientRect();
    return { x, y, width: w, height: h, top: y, left: x, right: x + w, bottom: y + h, toJSON() { return this; } };
  }
}
Range.START_TO_START = 0; Range.START_TO_END = 1; Range.END_TO_END = 2; Range.END_TO_START = 3;
G.Range = Range;
G.StaticRange = class StaticRange { constructor(o) { Object.assign(this, o); } get collapsed() { return this.startContainer === this.endContainer && this.startOffset === this.endOffset; } };
const mkRange = () => new Range();
val(P.Document, 'createRange', mkRange);
class Selection {
  constructor() { this._r = null; this._back = false; }
  get rangeCount() { return this._r ? 1 : 0; }
  get anchorNode() { return this._r ? (this._back ? this._r.endContainer : this._r.startContainer) : null; }
  get anchorOffset() { return this._r ? (this._back ? this._r.endOffset : this._r.startOffset) : 0; }
  get focusNode() { return this._r ? (this._back ? this._r.startContainer : this._r.endContainer) : null; }
  get focusOffset() { return this._r ? (this._back ? this._r.startOffset : this._r.endOffset) : 0; }
  get isCollapsed() { return !this._r || this._r.collapsed; }
  get type() { return !this._r ? 'None' : this._r.collapsed ? 'Caret' : 'Range'; }
  get direction() { return !this._r || this._r.collapsed ? 'none' : this._back ? 'backward' : 'forward'; }
  _sync() { const f = this.focusNode; N.setCaret(f, this.focusOffset); if (!this._pend) { this._pend = true; setTimeout(() => { this._pend = false; document.dispatchEvent(new Event('selectionchange')); }, 0); } }
  getRangeAt(i) { if (!this._r || i !== 0) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r; }
  addRange(r) { if (this._r) this._r._sel = null; this._r = r; this._back = false; r._sel = this; this._sync(); }
  removeRange() { this.removeAllRanges(); }
  removeAllRanges() { if (this._r) this._r._sel = null; this._r = null; N.setCaret(null, 0); }
  empty() { this.removeAllRanges(); }
  collapse(n, o) { if (!n) { this.removeAllRanges(); return; } const r = new Range(); r.startContainer = r.endContainer = n; r.startOffset = r.endOffset = Math.min(o || 0, nodeLen(n)); this.addRange(r); }
  setPosition(n, o) { this.collapse(n, o); }
  collapseToStart() { if (this._r) this.collapse(this._r.startContainer, this._r.startOffset); }
  collapseToEnd() { if (this._r) this.collapse(this._r.endContainer, this._r.endOffset); }
  extend(n, o) { if (!this._r) return this.collapse(n, o); const an = this.anchorNode, ao = this.anchorOffset; this.setBaseAndExtent(an, ao, n, o); }
  setBaseAndExtent(an, ao, fn, fo) { const r = new Range(); const back = cmpPt(an, ao, fn, fo) > 0; r.startContainer = back ? fn : an; r.startOffset = back ? fo : ao; r.endContainer = back ? an : fn; r.endOffset = back ? ao : fo; this.addRange(r); this._back = back; this._sync(); }
  selectAllChildren(n) { const r = new Range(); r.selectNodeContents(n); this.addRange(r); }
  deleteFromDocument() { if (this._r) this._r.deleteContents(); }
  containsNode(n, partial) { return !!this._r && (partial ? this._r.intersectsNode(n) : this._r.intersectsNode(n)); }
  modify(alter, dir, gran) { if (!this._r) return; const f = this.focusNode, o = this.focusOffset; let p = gran === 'lineboundary' || gran === 'line' ? (dir === 'backward' || dir === 'left' ? [f, 0] : [f, nodeLen(f)]) : stepCaret(f, o, dir === 'backward' || dir === 'left' ? -1 : 1, editHost(f) || document.body); if (alter === 'extend') this.extend(p[0], p[1]); else this.collapse(p[0], p[1]); }
  toString() { return this._r ? this._r.toString() : ''; }
  getComposedRanges() { return this._r ? [new G.StaticRange({ startContainer: this._r.startContainer, startOffset: this._r.startOffset, endContainer: this._r.endContainer, endOffset: this._r.endOffset })] : []; }
}
G.Selection = Selection;
const selection = new Selection();
val(P.Document, 'getSelection', function () { return selection; });
G.getSelection = () => selection;
function editHost(n) { for (let q = n && n.nodeType === 3 ? n.parentNode : n; q && q.nodeType === 1; q = q.parentNode) { const v = q.getAttribute('contenteditable'); if (v !== null) return v === 'false' ? null : q; } return null; }
const BLOCKS = /^(P|DIV|LI|H[1-6]|BLOCKQUOTE|PRE|SECTION|ARTICLE|TD|TH|DT|DD)$/;
function textNodesIn(root) { const out = []; const w = n => { for (let c = n.firstChild; c; c = c.nextSibling) { if (c.nodeType === 3) out.push(c); else if (c.nodeType === 1) w(c); } }; w(root); return out; }
function stepCaret(n, o, d, host) {
  if (n.nodeType === 3) { const t = n.data; if (d < 0 && o > 0) { let k = o - 1; if (k > 0 && /[\uDC00-\uDFFF]/.test(t[k])) k--; return [n, k]; } if (d > 0 && o < t.length) { let k = o + 1; if (k < t.length && /[\uDC00-\uDFFF]/.test(t[k])) k++; return [n, k]; } }
  const all = textNodesIn(host); let i = n.nodeType === 3 ? all.indexOf(n) : -1;
  if (i < 0) { const c = n.childNodes[o]; i = all.findIndex(t => c ? cmpPt(t, 0, n, o) >= 0 : cmpPt(t, 0, n, o) > 0); if (i < 0) i = all.length; if (d < 0) { const p = all[i - 1]; return p ? [p, p.data.length] : [n, o]; } const q = all[i]; return q ? [q, 0] : [n, o]; }
  if (d < 0) { const p = all[i - 1]; return p ? [p, Math.max(0, p.data.length - (p.parentNode === n.parentNode ? 1 : 0))] : [n, o]; }
  const q = all[i + 1]; return q ? [q, q.parentNode === n.parentNode ? 1 : 0] : [n, o];
}
function blockOf(n, host) { for (let q = n.nodeType === 3 ? n.parentNode : n; q && q !== host; q = q.parentNode) if (q.nodeType === 1 && BLOCKS.test(q.nodeName)) return q; return host; }
function caretInto(host) {
  const s = selection;
  if (s.rangeCount && host.contains(s.anchorNode)) return;
  const ts = textNodesIn(host);
  if (ts.length) { const t = ts[ts.length - 1]; s.collapse(t, t.data.length); return; }
  let e = host; while (e.firstElementChild && BLOCKS.test(e.firstElementChild.nodeName)) e = e.firstElementChild;
  s.collapse(e, 0);
}
G.__ceSet = function (n, o) {
  if (o < 0) { const host = editHost(n) || n; const ts = textNodesIn(host); if (ts.length) { const t = ts[ts.length - 1]; selection.collapse(t, t.data.length); } else { let e = host; while (e.firstElementChild && BLOCKS.test(e.firstElementChild.nodeName)) e = e.lastElementChild; selection.collapse(e, 0); } return; }
  selection.collapse(n, o);
};
function ceInsertText(host, data) {
  const r = selection.getRangeAt(0);
  if (!r.collapsed) r.deleteContents();
  let n = selection.focusNode, o = selection.focusOffset;
  if (n.nodeType === 3) { n.insertData(o, data); selection.collapse(n, o + data.length); return; }
  const before = n.childNodes[o - 1], at = n.childNodes[o];
  if (before && before.nodeType === 3) { const l = before.data.length; before.appendData(data); selection.collapse(before, l + data.length); return; }
  if (at && at.nodeType === 3) { at.insertData(0, data); selection.collapse(at, data.length); return; }
  if (n === host && at && at.nodeType === 1 && BLOCKS.test(at.nodeName)) { n = at; while (n.firstElementChild && BLOCKS.test(n.firstElementChild.nodeName)) n = n.firstElementChild; o = 0; }
  const t = document.createTextNode(data);
  const ref = n.childNodes[o] && !(n.childNodes[o].nodeName === 'BR' && n.childNodes.length === 1) ? n.childNodes[o] : null;
  if (n.childNodes.length === 1 && n.firstChild.nodeName === 'BR') n.insertBefore(t, n.firstChild); else n.insertBefore(t, ref);
  selection.collapse(t, data.length);
}
function ceDelete(host, back) {
  const r = selection.getRangeAt(0);
  if (!r.collapsed) { r.deleteContents(); return; }
  const n = selection.focusNode, o = selection.focusOffset;
  const p = stepCaret(n, o, back ? -1 : 1, host);
  if (p[0] === n && p[1] === o) return;
  const b1 = blockOf(n, host), b2 = blockOf(p[0], host);
  if (b1 !== b2 && b1 !== host && b2 !== host) {
    const [keep, gone] = back ? [b2, b1] : [b1, b2];
    const ts = textNodesIn(keep); const last = ts[ts.length - 1];
    for (const br of Array.from(keep.querySelectorAll('br'))) if (!br.nextSibling) br.remove();
    while (gone.firstChild) keep.appendChild(gone.firstChild);
    gone.remove();
    if (last) selection.collapse(last, last.data.length); else selection.collapse(keep, 0);
    return;
  }
  const rr = new Range();
  if (back) { rr.setStart(p[0], p[1]); rr.setEnd(n, o); } else { rr.setStart(n, o); rr.setEnd(p[0], p[1]); }
  if (rr.startContainer === rr.endContainer && rr.startContainer.nodeType === 3) { const t = rr.startContainer; t.deleteData(rr.startOffset, rr.endOffset - rr.startOffset); selection.collapse(t, rr.startOffset); if (!t.data.length) { const blk = blockOf(t, host); if (blk && !blk.textContent.length && !blk.querySelector('br')) { blk.appendChild(document.createElement('br')); } } return; }
  if (back && p[0].nodeType === 3) { p[0].deleteData(Math.max(0, p[1]), Math.min(1, p[0].data.length - p[1])); selection.collapse(p[0], p[1]); }
}
function ceParagraph(host) {
  const r = selection.getRangeAt(0);
  if (!r.collapsed) r.deleteContents();
  const n = selection.focusNode, o = selection.focusOffset;
  const blk = blockOf(n, host);
  if (blk === host) { const br = document.createElement('br'); r.insertNode(br); const t = document.createTextNode(''); br.after(t); if (!br.nextSibling || br.nextSibling === t) br.after(document.createElement('br')); selection.collapse(br.parentNode, idxOf(br) + 1); return; }
  const nb = document.createElement(blk.nodeName === 'LI' || /^H[1-6]$/.test(blk.nodeName) === false ? blk.nodeName : 'P');
  for (const a of Array.from(blk.attributes || [])) if (a.name !== 'id') nb.setAttribute(a.name, a.value);
  const tail = new Range(); tail.setStart(n, o); tail.setEnd(blk, blk.childNodes.length);
  if (n.nodeType === 3) { const rest = n.data.slice(o); n.data = n.data.slice(0, o); if (rest) nb.appendChild(document.createTextNode(rest)); let s = n; while (s && s !== blk) { let x = s.nextSibling; while (x) { const y = x.nextSibling; nb.appendChild(x); x = y; } s = s.parentNode; } }
  else { let x = n.childNodes[o]; while (x) { const y = x.nextSibling; nb.appendChild(x); x = y; } }
  blk.after(nb);
  if (!blk.textContent.length && !blk.querySelector('br')) blk.appendChild(document.createElement('br'));
  if (!nb.textContent.length && !nb.querySelector('br')) nb.appendChild(document.createElement('br'));
  const ts = textNodesIn(nb); if (ts.length) selection.collapse(ts[0], 0); else selection.collapse(nb, 0);
}
G.__ceKey = function (host, k) {
  caretInto(host);
  if (k === 17 || k === 18 || k === 19 || k === 20 || k === 2 || k === 5) {
    const f = selection.focusNode, o = selection.focusOffset;
    if (k === 17 || k === 18) { const p = stepCaret(f, o, k === 17 ? -1 : 1, host); selection.collapse(p[0], p[1]); }
    else { const ts = textNodesIn(blockOf(f, host)); if (k === 19 || k === 2) { if (ts.length) selection.collapse(ts[0], 0); } else if (ts.length) { const t = ts[ts.length - 1]; selection.collapse(t, t.data.length); } }
    return;
  }
  let inputType, data = null;
  if (k === 8) inputType = 'deleteContentBackward';
  else if (k === 127) inputType = 'deleteContentForward';
  else if (k === 10 || k === 13) inputType = 'insertParagraph';
  else if (k >= 32) { inputType = 'insertText'; data = String.fromCodePoint(k); }
  else return;
  const plain = host.getAttribute('contenteditable') === 'plaintext-only';
  if (plain && inputType === 'insertParagraph') { inputType = 'insertLineBreak'; }
  const tr = selection.getRangeAt(0).cloneRange();
  const tgt = selection.focusNode.nodeType === 3 ? selection.focusNode.parentNode : selection.focusNode;
  const bi = new InputEvent('beforeinput', { bubbles: true, cancelable: true, composed: true, inputType, data });
  bi.getTargetRanges = () => [new G.StaticRange({ startContainer: tr.startContainer, startOffset: tr.startOffset, endContainer: tr.endContainer, endOffset: tr.endOffset })];
  bi.isTrusted = true;
  if (!tgt.dispatchEvent(bi)) return;
  caretInto(host);
  if (inputType === 'insertText') ceInsertText(host, data);
  else if (inputType === 'deleteContentBackward') ceDelete(host, true);
  else if (inputType === 'deleteContentForward') ceDelete(host, false);
  else if (inputType === 'insertParagraph') ceParagraph(host);
  else if (inputType === 'insertLineBreak') ceInsertText(host, '\n');
  const ie = new InputEvent('input', { bubbles: true, composed: true, inputType, data });
  ie.isTrusted = true;
  host.dispatchEvent(ie);
};
val(P.Document, 'execCommand', function (cmd, ui, value) {
  const host = editHost(selection.focusNode); if (!host) return false;
  if (cmd === 'insertText') { ceInsertText(host, String(value)); host.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText', data: String(value) })); return true; }
  if (cmd === 'delete') { ceDelete(host, true); host.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'deleteContentBackward' })); return true; }
  if (cmd === 'forwardDelete') { ceDelete(host, false); return true; }
  if (cmd === 'insertParagraph') { ceParagraph(host); return true; }
  if (cmd === 'selectAll') { selection.selectAllChildren(host); return true; }
  if (cmd === 'insertHTML') { const r = selection.getRangeAt(0); r.deleteContents(); const f = r.createContextualFragment(String(value)); const last = f.lastChild; r.insertNode(f); if (last) selection.collapse(last.parentNode, idxOf(last) + 1); return true; }
  return false;
});
val(P.Document, 'queryCommandSupported', function (c) { return /^(insertText|delete|forwardDelete|insertParagraph|selectAll|insertHTML)$/.test(c); });
val(P.Document, 'elementsFromPoint', function (x, y) { const e = this.elementFromPoint(x, y); const r = []; for (let n = e; n && n.nodeType === 1; n = n.parentNode) r.push(n); return r; });
val(P.Document, 'getElementsByTagNameNS', function (ns, t) { return this.getElementsByTagName(t); });
val(P.Document, 'startViewTransition', function (cb) { const r = cb ? cb() : null; const p = Promise.resolve(r); return { finished: p, ready: p, updateCallbackDone: p, skipTransition() {} }; });
G.XMLDocument = DocumentC;

const urlSplit = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/(?:([^:@\/]*)(?::([^@\/]*))?@)?([^:\/?#]*)(?::(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/;
class URLSearchParams {
  constructor(init) {
    this._l = [];
    this._u = null;
    if (init == null) return;
    if (typeof init === 'string') { this._parse(init); return; }
    if (init instanceof URLSearchParams) { this._l = init._l.map(p => [p[0], p[1]]); return; }
    if (Array.isArray(init) || (init && typeof init[Symbol.iterator] === 'function')) { for (const [k, v] of init) this._l.push([String(k), String(v)]); return; }
    for (const k of Object.keys(init)) this._l.push([k, String(init[k])]);
  }
  _parse(s) { this._l = []; s = s.replace(/^\?/, ''); if (!s) return; for (const part of s.split('&')) { if (!part) continue; const i = part.indexOf('='); const k = i < 0 ? part : part.slice(0, i), v = i < 0 ? '' : part.slice(i + 1); const d = x => { try { return decodeURIComponent(x.replace(/\+/g, ' ')); } catch (e) { return x; } }; this._l.push([d(k), d(v)]); } }
  _sync() { if (this._u) { const s = this.toString(); this._u._search = s ? '?' + s : ''; } }
  append(k, v) { this._l.push([String(k), String(v)]); this._sync(); }
  delete(k) { this._l = this._l.filter(p => p[0] !== k); this._sync(); }
  get(k) { const p = this._l.find(p => p[0] === k); return p ? p[1] : null; }
  getAll(k) { return this._l.filter(p => p[0] === k).map(p => p[1]); }
  has(k) { return this._l.some(p => p[0] === k); }
  set(k, v) { const i = this._l.findIndex(p => p[0] === k); if (i < 0) this._l.push([String(k), String(v)]); else { this._l[i][1] = String(v); this._l = this._l.filter((p, j) => j <= i || p[0] !== k); } this._sync(); }
  sort() { this._l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this._sync(); }
  forEach(f, t) { for (const [k, v] of this._l) f.call(t, v, k, this); }
  keys() { return this._l.map(p => p[0])[Symbol.iterator](); }
  values() { return this._l.map(p => p[1])[Symbol.iterator](); }
  entries() { return this._l.map(p => [p[0], p[1]])[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
  get size() { return this._l.length; }
  toString() { const e = x => encodeURIComponent(x).replace(/%20/g, '+'); return this._l.map(p => e(p[0]) + '=' + e(p[1])).join('&'); }
}
G.URLSearchParams = URLSearchParams;
class URL {
  constructor(u, base) {
    u = String(u);
    let abs = u;
    if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(u)) { if (base === undefined) throw new TypeError("Failed to construct 'URL': Invalid URL"); abs = N.resolve(u, String(base instanceof URL ? base.href : base)); }
    const m = urlSplit.exec(abs);
    if (!m) throw new TypeError("Failed to construct 'URL': Invalid URL");
    this._protocol = m[1].toLowerCase(); this.username = m[2] || ''; this.password = m[3] || ''; this._hostname = (m[4] || '').toLowerCase(); this._port = m[5] || '';
    this._pathname = m[6] || (m[4] !== undefined ? '/' : ''); this._search = m[7] && m[7] !== '?' ? m[7] : ''; this._hash = m[8] && m[8] !== '#' ? m[8] : '';
    if ((this._protocol === 'http:' && this._port === '80') || (this._protocol === 'https:' && this._port === '443')) this._port = '';
    this._sp = null;
  }
  get protocol() { return this._protocol; } set protocol(v) { this._protocol = String(v).replace(/:?$/, ':'); }
  get hostname() { return this._hostname; } set hostname(v) { this._hostname = String(v); }
  get port() { return this._port; } set port(v) { this._port = String(v); }
  get host() { return this._hostname + (this._port ? ':' + this._port : ''); } set host(v) { const [h, p] = String(v).split(':'); this._hostname = h; this._port = p || ''; }
  get origin() { return /^(https?|ftp|wss?):$/.test(this._protocol) ? this._protocol + '//' + this.host : 'null'; }
  get pathname() { return this._pathname; } set pathname(v) { v = String(v); this._pathname = v.startsWith('/') ? v : '/' + v; }
  get search() { return this._search; } set search(v) { v = String(v); this._search = v && v !== '?' ? (v.startsWith('?') ? v : '?' + v) : ''; if (this._sp) this._sp._parse(this._search); }
  get hash() { return this._hash; } set hash(v) { v = String(v); this._hash = v && v !== '#' ? (v.startsWith('#') ? v : '#' + v) : ''; }
  get searchParams() { if (!this._sp) { this._sp = new URLSearchParams(this._search); this._sp._u = this; } return this._sp; }
  get href() { const auth = this.username ? this.username + (this.password ? ':' + this.password : '') + '@' : ''; const hier = /^(https?|ftp|file|wss?):$/.test(this._protocol) ? '//' + auth + this.host : ''; return this._protocol + hier + this._pathname + this._search + this._hash; }
  set href(v) { const n = new URL(v); Object.assign(this, n); }
  toString() { return this.href; }
  toJSON() { return this.href; }
  static createObjectURL() { return 'blob:' + N.origin() + '/' + Math.random().toString(36).slice(2); }
  static revokeObjectURL() {}
  static canParse(u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }
}
G.URL = URL;
G.webkitURL = URL;

class Location {
  get href() { return N.url(); } set href(v) { this.assign(v); }
  get protocol() { return new URL(N.url()).protocol; } get host() { return new URL(N.url()).host; } get hostname() { return new URL(N.url()).hostname; }
  get port() { return new URL(N.url()).port; } get pathname() { return new URL(N.url()).pathname; } set pathname(v) { const u = new URL(N.url()); u.pathname = v; this.assign(u.href); }
  get search() { return new URL(N.url()).search; } set search(v) { const u = new URL(N.url()); u.search = v; this.assign(u.href); }
  get hash() { return new URL(N.url()).hash; }
  set hash(v) { const u = new URL(N.url()); const old = u.href; u.hash = v; N.setUrl(u.href); const id = decodeURIComponent(u.hash.slice(1)); const el = id && (document.getElementById(id) || document.querySelector('a[name="' + CSS.escape(id) + '"]')); if (el) el.scrollIntoView(); else if (!id) N.scrollTo(0); run(G, new HashChangeEvent('hashchange', { oldURL: old, newURL: u.href })); }
  get origin() { return new URL(N.url()).origin; }
  assign(v) { const t = N.resolve(String(v)); const cur = new URL(N.url()); let nu; try { nu = new URL(t); } catch (e) { return; } if (nu.protocol === 'javascript:') { try { (0, eval)(decodeURIComponent(t.slice(11))); } catch (e) { reportErr(e); } return; } if (nu.hash && nu.href.split('#')[0] === cur.href.split('#')[0]) { this.hash = nu.hash; return; } N.navigate(t); }
  replace(v) { this.assign(v); }
  reload() { N.navigate(N.url()); }
  toString() { return N.url(); }
  get ancestorOrigins() { return []; }
}
const location = new Location();
def(G, 'location', () => location, v => location.assign(v));
let histState = null;
const history = {
  get length() { return 1; }, get state() { return histState; }, scrollRestoration: 'auto',
  pushState(s, t, u) { histState = s === undefined ? null : s; if (u != null) N.setUrl(N.resolve(String(u))); },
  replaceState(s, t, u) { histState = s === undefined ? null : s; if (u != null) N.setUrl(N.resolve(String(u))); },
  back() {}, forward() {}, go() {}
};
G.history = history;
G.History = function History() {};

const UA = 'Mozilla/5.0 (X11; NoovexOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Noovex/9.08';
G.navigator = {
  userAgent: UA, appName: 'Netscape', appCodeName: 'Mozilla', appVersion: UA.slice(8), platform: 'Linux x86_64', product: 'Gecko', productSub: '20030107', vendor: 'Google Inc.', vendorSub: '',
  language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, hardwareConcurrency: 4, deviceMemory: 4, maxTouchPoints: 0, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
  clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve(''), write: () => Promise.resolve(), read: () => Promise.resolve([]) },
  sendBeacon() { return true; }, vibrate() { return false; }, javaEnabled() { return false; }, share: undefined,
  geolocation: { getCurrentPosition(s, e) { if (e) setTimeout(() => e({ code: 1, message: 'User denied Geolocation' }), 0); }, watchPosition() { return 0; }, clearWatch() {} },
  permissions: { query: () => Promise.resolve({ state: 'denied', onchange: null, addEventListener() {} }) },
  connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
  mediaSession: { setActionHandler() {}, metadata: null }, plugins: [], mimeTypes: [],
  userAgentData: { brands: [{ brand: 'Chromium', version: '124' }], mobile: false, platform: 'Linux', getHighEntropyValues: () => Promise.resolve({}) },
  storage: { estimate: () => Promise.resolve({ quota: 1e8, usage: 0 }), persist: () => Promise.resolve(false) },
  locks: { request: (n, o, cb) => Promise.resolve((cb || o)()) }
};
G.clientInformation = G.navigator;
def(G, 'innerWidth', () => N.vw()); def(G, 'innerHeight', () => N.vh());
def(G, 'outerWidth', () => N.vw()); def(G, 'outerHeight', () => N.vh());
def(G, 'scrollX', () => 0); def(G, 'pageXOffset', () => 0);
def(G, 'scrollY', () => N.scrollY()); def(G, 'pageYOffset', () => N.scrollY());
def(G, 'screenX', () => 0); def(G, 'screenY', () => 0); def(G, 'screenLeft', () => 0); def(G, 'screenTop', () => 0);
G.devicePixelRatio = 1;
G.screen = { get width() { return N.vw(); }, get height() { return N.vh(); }, get availWidth() { return N.vw(); }, get availHeight() { return N.vh(); }, colorDepth: 24, pixelDepth: 24, availLeft: 0, availTop: 0, orientation: { type: 'landscape-primary', angle: 0, addEventListener() {}, removeEventListener() {} } };
G.window = G; G.self = G; G.top = G; G.parent = G; G.frames = G; G.opener = null; G.closed = false; G.name = ''; G.length = 0; G.frameElement = null; G.isSecureContext = true; G.origin = N.origin(); G.crossOriginIsolated = false;
G.status = ''; G.visualViewport = { get width() { return N.vw(); }, get height() { return N.vh(); }, scale: 1, offsetLeft: 0, offsetTop: 0, pageLeft: 0, get pageTop() { return N.scrollY(); }, addEventListener() {}, removeEventListener() {} };
const scrollArgs = (x, y) => (x && typeof x === 'object') ? (x.top !== undefined ? x.top : N.scrollY()) : (y !== undefined ? y : N.scrollY());
G.scrollTo = (x, y) => N.scrollTo(scrollArgs(x, y));
G.scroll = G.scrollTo;
G.scrollBy = (x, y) => N.scrollTo(N.scrollY() + ((x && typeof x === 'object') ? (x.top || 0) : (y || 0)));
const wrapFn = (f, args) => { if (typeof f === 'string') { const code = f; f = () => (0, eval)(code); } return args.length ? () => f(...args) : f; };
G.setTimeout = (f, ms, ...a) => N.setTimeout(wrapFn(f, a), ms);
G.setInterval = (f, ms, ...a) => N.setInterval(wrapFn(f, a), ms);
G.clearTimeout = id => N.clearTimer(id | 0);
G.clearInterval = id => N.clearTimer(id | 0);
G.requestAnimationFrame = f => N.raf(f);
G.cancelAnimationFrame = id => N.clearTimer(id | 0);
G.webkitRequestAnimationFrame = G.requestAnimationFrame;
G.requestIdleCallback = (f, o) => N.setTimeout(() => f({ didTimeout: false, timeRemaining: () => 12 }), 1);
G.cancelIdleCallback = id => N.clearTimer(id | 0);
G.queueMicrotask = f => { Promise.resolve().then(f).catch(reportErr); };
G.setImmediate = f => N.setTimeout(f, 0);
G.alert = m => N.alert(m === undefined ? '' : String(m));
G.confirm = m => { N.alert(m === undefined ? '' : String(m)); return true; };
G.prompt = () => null;
G.print = () => {}; G.stop = () => {}; G.focus = () => {}; G.blur = () => {}; G.close = () => {}; G.moveTo = () => {}; G.resizeTo = () => {};
G.open = (u) => { if (u) N.navigate(N.resolve(String(u))); return null; };
G.postMessage = (data, origin) => { setTimeout(() => run(G, new MessageEvent('message', { data, origin: N.origin(), source: G })), 0); };
G.reportError = reportErr;
G.matchMedia = q => { q = String(q); return { get matches() { return N.media(q); }, media: q, onchange: null, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {}, dispatchEvent() { return true; } }; };
G.getComputedStyle = (el, pseudo) => new Proxy({}, {
  get(t, k) {
    if (typeof k !== 'string') return undefined;
    if (k === 'getPropertyValue') return p => pseudo ? '' : N.computed(el, kebab(p));
    if (k === 'getPropertyPriority') return () => '';
    if (k === 'length') return 0;
    if (k === 'item') return () => '';
    if (k === 'cssText') return '';
    if (pseudo) return k === 'content' ? 'none' : '';
    return N.computed(el, kebab(k));
  }
});
const levels = { log: 0, info: 0, debug: 0, trace: 0, dir: 0, table: 0, warn: 1, error: 2, assert: 2 };
const counts = {}, timesM = {};
G.console = {};
for (const [k, l] of Object.entries(levels)) G.console[k] = function (...a) { if (k === 'assert') { if (a[0]) return; a = ['Assertion failed:', ...a.slice(1)]; } N.log(fmt(a), l); };
Object.assign(G.console, { group() {}, groupCollapsed() {}, groupEnd() {}, clear() {}, time(l = 'default') { timesM[l] = N.now(); }, timeEnd(l = 'default') { N.log(l + ': ' + (N.now() - (timesM[l] || 0)).toFixed(1) + 'ms', 0); }, timeLog() {}, count(l = 'default') { counts[l] = (counts[l] || 0) + 1; N.log(l + ': ' + counts[l], 0); }, countReset(l = 'default') { counts[l] = 0; } });
G.__fmt = v => { if (typeof v === 'string') return JSON.stringify(v); if (typeof v === 'function') return '[Function ' + (v.name || 'anonymous') + ']'; if (typeof v === 'symbol' || typeof v === 'bigint') return String(v) + (typeof v === 'bigint' ? 'n' : ''); try { const s = JSON.stringify(v, (k, x) => typeof x === 'bigint' ? x + 'n' : x, 2); return s === undefined ? String(v) : s; } catch (e) { return String(v); } };

class Storage {
  constructor(session) { val(this, '_s', session); }
  getItem(k) { return N.storeGet(this._s, String(k)); }
  setItem(k, v) { N.storeSet(this._s, String(k), String(v)); }
  removeItem(k) { N.storeRemove(this._s, String(k)); }
  clear() { N.storeClear(this._s); }
  key(i) { return N.storeKey(this._s, i | 0); }
  get length() { return N.storeLen(this._s); }
}
const mkStore = s => new Proxy(new Storage(s), {
  get(t, k) { if (typeof k !== 'string' || k in t) { const v = t[k]; return typeof v === 'function' ? v.bind(t) : v; } const v = t.getItem(k); return v === null ? undefined : v; },
  set(t, k, v) { if (typeof k === 'string') t.setItem(k, v); return true; },
  deleteProperty(t, k) { t.removeItem(k); return true; },
  has(t, k) { return k in t || t.getItem(k) !== null; },
  ownKeys(t) { const r = []; for (let i = 0; i < t.length; i++) r.push(t.key(i)); return r; },
  getOwnPropertyDescriptor(t, k) { const v = t.getItem(k); return v === null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; }
});
G.Storage = Storage;
G.localStorage = mkStore(false);
G.sessionStorage = mkStore(true);

class Headers {
  constructor(init) { this._h = new Map(); if (init) { if (init instanceof Headers) init.forEach((v, k) => this.append(k, v)); else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v); else for (const k of Object.keys(init)) this.append(k, init[k]); } }
  append(k, v) { k = String(k).toLowerCase(); const o = this._h.get(k); this._h.set(k, o ? o + ', ' + v : String(v)); }
  set(k, v) { this._h.set(String(k).toLowerCase(), String(v)); }
  get(k) { const v = this._h.get(String(k).toLowerCase()); return v === undefined ? null : v; }
  has(k) { return this._h.has(String(k).toLowerCase()); }
  delete(k) { this._h.delete(String(k).toLowerCase()); }
  forEach(f, t) { for (const [k, v] of this._h) f.call(t, v, k, this); }
  entries() { return this._h.entries(); } keys() { return this._h.keys(); } values() { return this._h.values(); }
  [Symbol.iterator]() { return this._h.entries(); }
}
const te = new (class { encode(s) { return new Uint8Array(N.utf8enc(String(s))); } })();
const u8of = b => b instanceof Uint8Array ? b : b instanceof ArrayBuffer ? new Uint8Array(b) : b && b.buffer instanceof ArrayBuffer ? new Uint8Array(b.buffer, b.byteOffset || 0, b.byteLength) : null;
class TextEncoder { get encoding() { return 'utf-8'; } encode(s) { return te.encode(s === undefined ? '' : s); } encodeInto(s, a) { const b = te.encode(s); a.set(b.subarray(0, a.length)); return { read: s.length, written: Math.min(b.length, a.length) }; } }
class TextDecoder {
  constructor(enc) { this.encoding = (enc || 'utf-8').toLowerCase(); }
  decode(b) {
    if (!b) return '';
    const a = b instanceof Uint8Array ? b : b.buffer ? new Uint8Array(b.buffer, b.byteOffset || 0, b.byteLength) : new Uint8Array(b);
    if (this.encoding !== 'utf-8' && this.encoding !== 'utf8') { let s = ''; for (let i = 0; i < a.length; i++) s += String.fromCharCode(a[i]); return s; }
    return N.utf8dec(a);
    let s = '', i = 0;
    while (i < a.length) {
      const c = a[i++];
      if (c < 0x80) s += String.fromCharCode(c);
      else if (c < 0xE0) s += String.fromCharCode(((c & 31) << 6) | (a[i++] & 63));
      else if (c < 0xF0) s += String.fromCharCode(((c & 15) << 12) | ((a[i++] & 63) << 6) | (a[i++] & 63));
      else { const cp = ((c & 7) << 18) | ((a[i++] & 63) << 12) | ((a[i++] & 63) << 6) | (a[i++] & 63); s += String.fromCodePoint(cp); }
    }
    return s;
  }
}
G.TextEncoder = TextEncoder; G.TextDecoder = TextDecoder;
class Blob {
  constructor(parts, opts) {
    const ps = parts || [];
    this.type = (opts && opts.type) || '';
    if (ps.every(p => typeof p === 'string' || (p instanceof Blob && p._u8 === null) || (p && typeof p === 'object' && p.byteLength === undefined && !(p instanceof Blob)))) { this._str = ps.map(p => typeof p === 'string' ? p : p instanceof Blob ? p._str : String(p)).join(''); this._u8 = null; return; }
    const bs = ps.map(p => p instanceof Blob ? p._bytes() : u8of(p) || te.encode(String(p)));
    let n = 0; for (const b of bs) n += b.length;
    const o = new Uint8Array(n); let k = 0; for (const b of bs) { o.set(b, k); k += b.length; }
    this._u8 = o; this._str = null;
  }
  _bytes() { return this._u8 !== null ? this._u8 : te.encode(this._str); }
  get _s() { return this._u8 !== null ? new TextDecoder().decode(this._u8) : this._str; }
  get size() { return this._bytes().length; }
  text() { return Promise.resolve(this._s); }
  arrayBuffer() { const b = this._bytes(); return Promise.resolve(b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength)); }
  bytes() { return Promise.resolve(new Uint8Array(this._bytes())); }
  slice(a, b, t) { return new Blob([this._bytes().slice(a, b)], { type: t || '' }); }
  stream() { const b = this._bytes(); return new ReadableStream({ start(c) { if (b.length) c.enqueue(b); c.close(); } }); }
}
class File extends Blob { constructor(p, n, o) { super(p, o); this.name = n; this.lastModified = Date.now(); } }
class FormData {
  constructor(form) { this._l = []; if (form) for (const e of form.elements) { const n = e.name; if (!n || e.disabled) continue; const t = e.type; if ((t === 'checkbox' || t === 'radio') && !e.checked) continue; if (t === 'submit' || t === 'button' || t === 'reset' || t === 'image' || t === 'file') continue; this._l.push([n, e.value]); } }
  append(k, v) { this._l.push([String(k), v instanceof Blob ? v : String(v)]); }
  set(k, v) { this.delete(k); this.append(k, v); }
  get(k) { const p = this._l.find(p => p[0] === k); return p ? p[1] : null; }
  getAll(k) { return this._l.filter(p => p[0] === k).map(p => p[1]); }
  has(k) { return this._l.some(p => p[0] === k); }
  delete(k) { this._l = this._l.filter(p => p[0] !== k); }
  forEach(f, t) { for (const [k, v] of this._l) f.call(t, v, k, this); }
  entries() { return this._l[Symbol.iterator](); } keys() { return this._l.map(p => p[0])[Symbol.iterator](); } values() { return this._l.map(p => p[1])[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
}
Object.assign(G, { Headers, Blob, File, FormData });
const bodyRaw = b => b == null ? null : b instanceof Blob ? (b._u8 !== null ? b._u8 : b._str) : u8of(b) || bodyToString(b);
const bodyToString = b => b == null ? null : typeof b === 'string' ? b : b instanceof URLSearchParams ? b.toString() : b instanceof FormData ? new URLSearchParams(b._l.map(p => [p[0], String(p[1])])).toString() : b instanceof Blob ? b._s : b.byteLength !== undefined ? new TextDecoder().decode(b) : String(b);
const bodyType = b => b instanceof URLSearchParams || b instanceof FormData ? 'application/x-www-form-urlencoded;charset=UTF-8' : typeof b === 'string' ? 'text/plain;charset=UTF-8' : b instanceof Blob ? b.type : null;
class ReadableStream {
  constructor(src, strat) { this._q = []; this._done = false; this._err = null; this._w = []; this.locked = false; const self = this;
    const ctl = { enqueue(c) { self._q.push(c); self._wake(); }, close() { self._done = true; self._wake(); }, error(e) { self._err = e || new Error('stream error'); self._wake(); }, get desiredSize() { return 1; } };
    this._ctl = ctl; this._src = src || {};
    if (this._src.start) { try { const r = this._src.start(ctl); if (r && r.then) r.then(null, e => ctl.error(e)); } catch (e) { ctl.error(e); } }
  }
  _wake() { const w = this._w; this._w = []; for (const f of w) f(); }
  _pull() { if (this._src.pull && !this._done && !this._q.length && !this._pulling) { this._pulling = true; try { const r = this._src.pull(this._ctl); Promise.resolve(r).then(() => { this._pulling = false; this._wake(); }, e => { this._pulling = false; this._ctl.error(e); }); } catch (e) { this._pulling = false; this._ctl.error(e); } } }
  _read() { return new Promise((res, rej) => { const tryr = () => { if (this._q.length) return res({ value: this._q.shift(), done: false }); if (this._err) return rej(this._err); if (this._done) return res({ value: undefined, done: true }); this._w.push(tryr); this._pull(); }; tryr(); }); }
  getReader() { this.locked = true; const st = this; return { read: () => st._read(), releaseLock() { st.locked = false; }, cancel(r) { st._done = true; st._q = []; if (st._src.cancel) st._src.cancel(r); st._wake(); return Promise.resolve(); }, get closed() { return new Promise(r => { const c = () => st._done ? r() : st._w.push(c); c(); }); } }; }
  cancel(r) { this._done = true; this._q = []; if (this._src.cancel) this._src.cancel(r); this._wake(); return Promise.resolve(); }
  pipeThrough(t) { const rd = this.getReader(); const w = t.writable.getWriter(); (async () => { try { for (;;) { const { value, done } = await rd.read(); if (done) break; await w.write(value); } await w.close(); } catch (e) { w.abort(e); } })(); return t.readable; }
  async pipeTo(ws) { const rd = this.getReader(); const w = ws.getWriter(); for (;;) { const { value, done } = await rd.read(); if (done) break; await w.write(value); } await w.close(); }
  tee() { const a = [], b = []; let done = false; const rd = this.getReader(); const mk = q => new ReadableStream({ async pull(c) { if (q.length) { c.enqueue(q.shift()); return; } if (done) { c.close(); return; } const r = await rd.read(); if (r.done) { done = true; c.close(); return; } (q === a ? b : a).push(r.value); c.enqueue(r.value); } }); return [mk(a), mk(b)]; }
  [Symbol.asyncIterator]() { const rd = this.getReader(); return { next: () => rd.read(), return: () => { rd.releaseLock(); return Promise.resolve({ done: true }); }, [Symbol.asyncIterator]() { return this; } }; }
  values() { return this[Symbol.asyncIterator](); }
  static from(it) { return new ReadableStream({ async start(c) { for await (const x of it) c.enqueue(x); c.close(); } }); }
}
class WritableStream {
  constructor(sink) { this._s = sink || {}; this.locked = false; if (this._s.start) this._s.start({ error() {} }); }
  getWriter() { this.locked = true; const s = this._s; return { write: c => Promise.resolve(s.write ? s.write(c, { error() {} }) : undefined), close: () => Promise.resolve(s.close ? s.close() : undefined), abort: r => Promise.resolve(s.abort ? s.abort(r) : undefined), releaseLock() {}, get ready() { return Promise.resolve(); }, get closed() { return Promise.resolve(); }, get desiredSize() { return 1; } }; }
  close() { return Promise.resolve(this._s.close && this._s.close()); }
  abort(r) { return Promise.resolve(this._s.abort && this._s.abort(r)); }
}
class TransformStream {
  constructor(tr) { tr = tr || {}; let ctl; this.readable = new ReadableStream({ start(c) { ctl = c; } }); const c2 = { enqueue: x => ctl.enqueue(x), error: e => ctl.error(e), terminate: () => ctl.close(), get desiredSize() { return 1; } }; if (tr.start) tr.start(c2);
    this.writable = new WritableStream({ write: ch => tr.transform ? tr.transform(ch, c2) : c2.enqueue(ch), close: async () => { if (tr.flush) await tr.flush(c2); ctl.close(); }, abort: e => ctl.error(e) }); }
}
class TextDecoderStream extends TransformStream { constructor(enc) { const d = new TextDecoder(enc); super({ transform(ch, c) { const s = typeof ch === 'string' ? ch : d.decode(ch, { stream: true }); if (s) c.enqueue(s); } }); this.encoding = 'utf-8'; } }
class TextEncoderStream extends TransformStream { constructor() { const e = new TextEncoder(); super({ transform(ch, c) { c.enqueue(e.encode(ch)); } }); this.encoding = 'utf-8'; } }
Object.assign(G, { ReadableStream, WritableStream, TransformStream, TextDecoderStream, TextEncoderStream, ByteLengthQueuingStrategy: class { constructor(o) { this.highWaterMark = o && o.highWaterMark; } size(c) { return c.byteLength; } }, CountQueuingStrategy: class { constructor(o) { this.highWaterMark = o && o.highWaterMark; } size() { return 1; } } });
class Response {
  constructor(body, init) { init = init || {}; this._u8 = null; if (body instanceof ReadableStream) { this._bs = body; this._b = ''; this._pend = (async () => { const rd = body.getReader(); const ch = []; let n = 0; for (;;) { const { value, done } = await rd.read(); if (done) break; const u = typeof value === 'string' ? te.encode(value) : u8of(value); ch.push(u); n += u.length; } const o = new Uint8Array(n); let k = 0; for (const c of ch) { o.set(c, k); k += c.length; } this._u8 = o; this._b = null; })(); } else if (body != null && (u8of(body) || (body instanceof Blob && body._u8 !== null))) { this._u8 = body instanceof Blob ? body._u8 : u8of(body); this._b = null; } else this._b = body == null ? '' : bodyToString(body); this.status = init.status === undefined ? 200 : init.status; this.statusText = init.statusText || ''; this.headers = new Headers(init.headers); this.url = init.url || ''; this.type = 'basic'; this.redirected = false; this.bodyUsed = false; }
  get ok() { return this.status >= 200 && this.status < 300; }
  _bytes() { return this._u8 !== null ? this._u8 : te.encode(this._b); }
  _text() { return this._u8 !== null ? new TextDecoder().decode(this._u8) : this._b; }
  get body() { if (this._bs) return this._bs; if (this._b === null && this._u8 === null) return null; const b = this._bytes(); this._bs = new ReadableStream({ start(c) { if (b.length) c.enqueue(b); c.close(); } }); return this._bs; }
  text() { this.bodyUsed = true; return this._pend ? this._pend.then(() => this._text()) : Promise.resolve(this._text()); }
  json() { return this.text().then(t => JSON.parse(t)); }
  blob() { const go = () => new Blob([this._u8 !== null ? this._u8 : this._b], { type: this.headers.get('content-type') || '' }); return this._pend ? this._pend.then(go) : Promise.resolve(go()); }
  arrayBuffer() { const go = () => { const b = this._bytes(); return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength); }; return this._pend ? this._pend.then(go) : Promise.resolve(go()); }
  bytes() { return this.arrayBuffer().then(b => new Uint8Array(b)); }
  formData() { return Promise.resolve(new FormData()); }
  clone() { return new Response(this._u8 !== null ? this._u8 : this._b, { status: this.status, statusText: this.statusText, headers: this.headers, url: this.url }); }
  static json(d, i) { return new Response(JSON.stringify(d), Object.assign({ headers: { 'content-type': 'application/json' } }, i)); }
  static error() { return new Response(null, { status: 0 }); }
  static redirect(u, s) { return new Response(null, { status: s || 302, headers: { location: u } }); }
}
class Request {
  constructor(input, init) { init = init || {}; this.url = input instanceof Request ? input.url : N.resolve(String(input)); this.method = (init.method || (input instanceof Request ? input.method : 'GET')).toUpperCase(); this.headers = new Headers(init.headers || (input instanceof Request ? input.headers : undefined)); this._body = init.body !== undefined ? init.body : (input instanceof Request ? input._body : null); this.signal = init.signal || null; this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors'; this.cache = init.cache || 'default'; this.redirect = 'follow'; this.referrer = ''; }
  clone() { return new Request(this); }
  text() { return Promise.resolve(bodyToString(this._body) || ''); }
  json() { return this.text().then(JSON.parse); }
}
class AbortSignal extends EventTarget {
  constructor() { super(); this.aborted = false; this.reason = undefined; this.onabort = null; }
  throwIfAborted() { if (this.aborted) throw this.reason; }
  static abort(r) { const c = new AbortController(); c.abort(r); return c.signal; }
  static timeout(ms) { const c = new AbortController(); setTimeout(() => c.abort(new DOMException('Timeout', 'TimeoutError')), ms); return c.signal; }
  static any(list) { const c = new AbortController(); for (const s of list) { if (s.aborted) { c.abort(s.reason); break; } s.addEventListener('abort', () => c.abort(s.reason)); } return c.signal; }
}
class AbortController { constructor() { this.signal = new AbortSignal(); } abort(r) { const s = this.signal; if (s.aborted) return; s.aborted = true; s.reason = r === undefined ? new DOMException('The operation was aborted.', 'AbortError') : r; run(s, new Event('abort')); } }
class DOMException extends Error { constructor(m, n) { super(m); this.name = n || 'Error'; this.code = 0; } }
Object.assign(G, { Response, Request, AbortController, AbortSignal, DOMException });
G.fetch = (input, init) => new Promise((resolve, reject) => {
  init = init || {};
  const req = input instanceof Request ? input : new Request(input, init);
  const body = init.body !== undefined ? init.body : req._body;
  const signal = init.signal || req.signal;
  N.setTimeout(() => {
    if (signal && signal.aborted) { reject(signal.reason); return; }
    const h = new Headers(init.headers || req.headers);
    const ct = h.get('content-type') || bodyType(body);
    const r = N.fetch(req.url, (init.method || req.method || 'GET').toUpperCase(), bodyRaw(body), ct, true);
    if (!r[0]) { reject(new TypeError('Failed to fetch')); return; }
    const res = new Response(new Uint8Array(r[1]), { status: r[0], statusText: r[0] === 200 ? 'OK' : '', headers: { 'content-type': r[2] }, url: r[3] });
    res.redirected = r[3] !== req.url;
    resolve(res);
  }, 0);
});
class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = ''; this.responseType = ''; this.responseURL = ''; this.responseXML = null; this.timeout = 0; this.withCredentials = false; this._h = {}; this._rh = ''; this.upload = new EventTarget(); this.onreadystatechange = null; this.onload = null; this.onerror = null; this.onloadend = null; this.onabort = null; this.ontimeout = null; this.onprogress = null; this.onloadstart = null; }
  open(m, u, async) { this._m = String(m).toUpperCase(); this._u = N.resolve(String(u)); this._async = async !== false; this.readyState = 1; this._fire('readystatechange'); }
  setRequestHeader(k, v) { this._h[String(k).toLowerCase()] = String(v); }
  getResponseHeader(k) { return String(k).toLowerCase() === 'content-type' ? this._rh : null; }
  getAllResponseHeaders() { return this._rh ? 'content-type: ' + this._rh + '\r\n' : ''; }
  overrideMimeType() {}
  abort() { this._aborted = true; this.readyState = 0; this._fire('abort'); }
  _fire(t) { const ev = new ProgressEvent(t, { lengthComputable: false }); run(this, ev); }
  send(body) {
    const go = () => {
      if (this._aborted) return;
      this._fire('loadstart');
      const bin = this.responseType === 'arraybuffer' || this.responseType === 'blob';
      const r = N.fetch(this._u, this._m, bodyRaw(body), this._h['content-type'] || bodyType(body), bin);
      this.status = r[0]; this.statusText = r[0] === 200 ? 'OK' : ''; this.responseText = bin ? '' : r[1]; this._rh = r[2]; this.responseURL = r[3];
      if (this.responseType === 'json') { try { this.response = JSON.parse(r[1]); } catch (e) { this.response = null; } }
      else if (this.responseType === 'document') { this.response = this.responseXML = N.parseDoc(r[1]); }
      else if (this.responseType === 'arraybuffer') this.response = r[1];
      else if (this.responseType === 'blob') this.response = new Blob([new Uint8Array(r[1])], { type: r[2] });
      else this.response = r[1];
      this.readyState = 2; this._fire('readystatechange');
      this.readyState = 3; this._fire('readystatechange');
      this.readyState = 4; this._fire('readystatechange');
      if (!r[0]) this._fire('error'); else { this._fire('progress'); this._fire('load'); }
      this._fire('loadend');
    };
    if (this._async) N.setTimeout(go, 0); else go();
  }
}
XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;
G.XMLHttpRequest = XMLHttpRequest;
G.WebSocket = class WebSocket extends EventTarget { constructor(u) { super(); this.url = u; this.readyState = 3; this.onopen = this.onclose = this.onerror = this.onmessage = null; setTimeout(() => { run(this, new Event('error')); run(this, new Event('close')); }, 0); } send() {} close() {} };
G.WebSocket.CONNECTING = 0; G.WebSocket.OPEN = 1; G.WebSocket.CLOSING = 2; G.WebSocket.CLOSED = 3;
G.EventSource = class EventSource extends EventTarget { constructor(u) { super(); this.url = u; this.readyState = 2; } close() {} };
G.BroadcastChannel = class BroadcastChannel extends EventTarget { constructor(n) { super(); this.name = n; } postMessage() {} close() {} };
G.MessageChannel = class MessageChannel { constructor() { const mk = () => ({ onmessage: null, postMessage(d) { const o = this._o; setTimeout(() => { if (o.onmessage) o.onmessage(new MessageEvent('message', { data: d })); }, 0); }, start() {}, close() {}, addEventListener(t, f) { if (t === 'message') this.onmessage = f; }, removeEventListener() {} }); this.port1 = mk(); this.port2 = mk(); this.port1._o = this.port2; this.port2._o = this.port1; } };
G.Notification = class Notification { constructor() {} static requestPermission() { return Promise.resolve('denied'); } close() {} };
G.Notification.permission = 'denied';

const MO_LIST = [];
class MutationRecord {}
class MutationObserver {
  constructor(cb) { this._cb = cb; this._t = []; this._q = []; }
  observe(target, o) {
    o = Object.assign({}, o || {});
    if ((o.attributeOldValue || o.attributeFilter) && o.attributes === undefined) o.attributes = true;
    if (o.characterDataOldValue && o.characterData === undefined) o.characterData = true;
    if (!o.childList && !o.attributes && !o.characterData) throw new TypeError("The options object must set at least one of 'attributes', 'characterData', or 'childList' to true.");
    const ex = this._t.find(x => x.t === target); if (ex) ex.o = o; else this._t.push({ t: target, o });
    if (!MO_LIST.includes(this)) MO_LIST.push(this);
    N.moOn(1);
  }
  disconnect() { this._t = []; this._q = []; const i = MO_LIST.indexOf(this); if (i >= 0) MO_LIST.splice(i, 1); if (!MO_LIST.length) N.moOn(0); }
  takeRecords() { const q = this._q; this._q = []; return q; }
}
const mkList = a => { a.item = i => a[i] || null; return a; };
G.__mut = function (kind, target, child, prev, next, name, old) {
  for (const mo of MO_LIST) {
    for (const { t, o } of mo._t) {
      if (target !== t && !(o.subtree && t.contains(target))) continue;
      if (kind <= 2 && !o.childList) continue;
      if (kind === 3 && (!o.attributes || (o.attributeFilter && !o.attributeFilter.includes(name)))) continue;
      if (kind === 4 && !o.characterData) continue;
      const rec = Object.create(MutationRecord.prototype);
      Object.assign(rec, { type: kind <= 2 ? 'childList' : kind === 3 ? 'attributes' : 'characterData', target, addedNodes: mkList(kind === 1 ? [child] : []), removedNodes: mkList(kind === 2 ? [child] : []), previousSibling: prev, nextSibling: next, attributeName: kind === 3 ? name : null, attributeNamespace: null, oldValue: (kind === 3 && o.attributeOldValue) || (kind === 4 && o.characterDataOldValue) ? old : null });
      mo._q.push(rec);
      if (!mo._s) { mo._s = true; queueMicrotask(() => { mo._s = false; const q = mo.takeRecords(); if (q.length) { try { mo._cb.call(mo, q, mo); } catch (e) { reportErr(e); } } }); }
      break;
    }
  }
};
G.MutationRecord = MutationRecord;
class IntersectionObserver {
  constructor(cb, opts) { this._cb = cb; this._t = new Set(); this.root = (opts && opts.root) || null; this.rootMargin = (opts && opts.rootMargin) || '0px'; this.thresholds = [0]; }
  observe(el) {
    if (this._t.has(el)) return;
    this._t.add(el);
    setTimeout(() => { if (!this._t.has(el)) return; const r = el.getBoundingClientRect(); try { this._cb([{ target: el, isIntersecting: true, intersectionRatio: 1, boundingClientRect: r, intersectionRect: r, rootBounds: { x: 0, y: 0, width: N.vw(), height: N.vh(), top: 0, left: 0, right: N.vw(), bottom: N.vh() }, time: N.now() }], this); } catch (e) { reportErr(e); } }, 0);
  }
  unobserve(el) { this._t.delete(el); }
  disconnect() { this._t.clear(); }
  takeRecords() { return []; }
}
class ResizeObserver {
  constructor(cb) { this._cb = cb; this._t = new Set(); }
  observe(el) { this._t.add(el); setTimeout(() => { if (!this._t.has(el)) return; const r = el.getBoundingClientRect(); const box = [{ inlineSize: r.width, blockSize: r.height }]; try { this._cb([{ target: el, contentRect: { x: 0, y: 0, width: r.width, height: r.height, top: 0, left: 0, right: r.width, bottom: r.height }, borderBoxSize: box, contentBoxSize: box, devicePixelContentBoxSize: box }], this); } catch (e) { reportErr(e); } }, 0); }
  unobserve(el) { this._t.delete(el); }
  disconnect() { this._t.clear(); }
}
class PerformanceObserver { constructor() {} observe() {} disconnect() {} takeRecords() { return []; } }
PerformanceObserver.supportedEntryTypes = [];
Object.assign(G, { MutationObserver, WebKitMutationObserver: MutationObserver, IntersectionObserver, ResizeObserver, PerformanceObserver });
const marks = [];
G.performance = { now: () => N.now(), timeOrigin: N.epoch() - N.now(), mark(n) { const m = { name: n, entryType: 'mark', startTime: N.now(), duration: 0 }; marks.push(m); return m; }, measure(n) { return { name: n, entryType: 'measure', startTime: 0, duration: 0 }; }, clearMarks() {}, clearMeasures() {}, getEntriesByType(t) { return t === 'mark' ? marks.slice() : []; }, getEntriesByName(n) { return marks.filter(m => m.name === n); }, getEntries() { return marks.slice(); }, setResourceTimingBufferSize() {}, clearResourceTimings() {}, timing: { navigationStart: N.epoch() - N.now(), domContentLoadedEventEnd: 0, loadEventEnd: 0 }, navigation: { type: 0, redirectCount: 0 }, memory: { usedJSHeapSize: 0, totalJSHeapSize: 0, jsHeapSizeLimit: 0 }, toJSON() { return {}; } };

const b64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
G.btoa = s => { s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); if (a > 255 || b > 255 || c > 255) throw new DOMException('Invalid character', 'InvalidCharacterError'); const n = (a << 16) | ((b || 0) << 8) | (c || 0); o += b64[n >> 18 & 63] + b64[n >> 12 & 63] + (i + 1 < s.length ? b64[n >> 6 & 63] : '=') + (i + 2 < s.length ? b64[n & 63] : '='); } return o; };
G.atob = s => { s = String(s).replace(/[\s=]/g, ''); let o = '', bits = 0, acc = 0; for (const ch of s) { const v = b64.indexOf(ch === '-' ? '+' : ch === '_' ? '/' : ch); if (v < 0) throw new DOMException('Invalid character', 'InvalidCharacterError'); acc = (acc << 6) | v; bits += 6; if (bits >= 8) { bits -= 8; o += String.fromCharCode((acc >> bits) & 255); } } return o; };
G.crypto = { getRandomValues(a) { const r = N.random(a.length * (a.BYTES_PER_ELEMENT || 1)); const u8 = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); for (let i = 0; i < u8.length; i++) u8[i] = r[i]; return a; }, randomUUID() { const r = N.random(16); r[6] = (r[6] & 15) | 64; r[8] = (r[8] & 63) | 128; const h = r.map(x => x.toString(16).padStart(2, '0')).join(''); return h.slice(0, 8) + '-' + h.slice(8, 12) + '-' + h.slice(12, 16) + '-' + h.slice(16, 20) + '-' + h.slice(20); }, subtle: {} };
G.structuredClone = v => v === undefined ? undefined : JSON.parse(JSON.stringify(v));
{
  const sp = P.HTMLElement;
  val(sp, 'assignedNodes', function (o) { let r = this.getRootNode(); const h = r && r.nodeType === 11 ? N.shadowHost(r) : null; if (!h) return []; const nm = this.getAttribute('name') || ''; const out = []; for (const c of h.childNodes) { const sn = c.nodeType === 1 ? (c.getAttribute('slot') || '') : ''; if (sn === nm && (c.nodeType === 1 || c.nodeType === 3)) out.push(c); } return out; });
  val(sp, 'assignedElements', function (o) { return this.assignedNodes(o).filter(n => n.nodeType === 1); });
  val(sp, 'assign', function () {});
}
if (!G.CSSStyleSheet || !G.CSSStyleSheet.prototype.replaceSync) {
  G.CSSStyleSheet = class CSSStyleSheet { constructor() { this.__text = ''; this.cssRules = []; this.disabled = false; } replaceSync(t) { this.__text = String(t); } replace(t) { this.__text = String(t); return Promise.resolve(this); } insertRule(r, i) { this.__text += '\n' + r; this.cssRules.push({ cssText: r }); return i || 0; } deleteRule() {} };
}
def(P.Document, 'adoptedStyleSheets', function () { return this.__ass || []; }, function (v) { val(this, '__ass', v); const t = (v || []).map(s => s.__text || '').join('\n'); let st = this.__assel; if (!st) { st = document.createElement('style'); val(this, '__assel', st); (document.head || document.documentElement).appendChild(st); } st.textContent = t; });
G.CSS = { supports(p, v) { return true; }, escape(s) { return String(s).replace(/[^a-zA-Z0-9_ -￿-]/g, c => '\\' + c); }, registerProperty() {} };
G.Image = function Image(w, h) { const i = document.createElement('img'); if (w !== undefined) i.width = w; if (h !== undefined) i.height = h; return i; };
G.Option = function Option(t, v, d, s) { const o = document.createElement('option'); if (t !== undefined) o.text = t; if (v !== undefined) o.value = v; if (d) o.defaultSelected = true; if (s) o.selected = true; return o; };
G.Audio = function Audio(src) { const a = document.createElement('audio'); if (src) a.src = src; return a; };
G.DOMParser = class DOMParser { parseFromString(s) { return N.parseDoc(String(s)); } };
G.XMLSerializer = class XMLSerializer { serializeToString(n) { return n.outerHTML !== undefined ? n.outerHTML : n.textContent; } };
G.DOMRect = class DOMRect { constructor(x = 0, y = 0, w = 0, h = 0) { Object.assign(this, { x, y, width: w, height: h, left: x, top: y, right: x + w, bottom: y + h }); } static fromRect(r) { return new DOMRect(r.x, r.y, r.width, r.height); } };
G.DOMRectReadOnly = G.DOMRect;
G.DOMPoint = class DOMPoint { constructor(x = 0, y = 0, z = 0, w = 1) { Object.assign(this, { x, y, z, w }); } };
G.DOMMatrix = class DOMMatrix { constructor() { Object.assign(this, { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0, m41: 0, m42: 0, is2D: true, isIdentity: true }); } translate() { return this; } scale() { return this; } multiply() { return this; } inverse() { return this; } };
G.WebKitCSSMatrix = G.DOMMatrix;
G.NodeList = Array; G.HTMLCollection = Array;
G.Worker = undefined; G.SharedWorker = undefined; G.ServiceWorker = undefined;
G.indexedDB = undefined;
G.caches = undefined;
G.speechSynthesis = undefined;
G.trustedTypes = undefined;
G.FileReader = class FileReader extends EventTarget { constructor() { super(); this.result = null; this.readyState = 0; this.onload = null; this.onloadend = null; this.onerror = null; } _done(v) { this.result = v; this.readyState = 2; setTimeout(() => { run(this, new ProgressEvent('load')); run(this, new ProgressEvent('loadend')); }, 0); } readAsText(b) { this._done(b ? b._s : ''); } readAsDataURL(b) { this._done('data:' + (b && b.type || '') + ';base64,' + btoa(unescape(encodeURIComponent(b ? b._s : '')))); } readAsArrayBuffer(b) { const u = b ? b._bytes() : new Uint8Array(0); this._done(u.buffer.slice(u.byteOffset, u.byteOffset + u.byteLength)); } abort() {} };
G.customElements = {
  define(name, ctor) {
    name = String(name).toLowerCase();
    if (ceByName.has(name)) throw new DOMException("'" + name + "' has already been defined", 'NotSupportedError');
    ceByName.set(name, ctor); ceByCtor.set(ctor, name);
    for (const el of document.querySelectorAll(name)) { Object.setPrototypeOf(el, ctor.prototype); try { if (el.connectedCallback) el.connectedCallback(); } catch (e) { reportErr(e); } }
    const w = whenDef.get(name); if (w) { w.res(ctor); }
  },
  get(name) { return ceByName.get(String(name).toLowerCase()); },
  whenDefined(name) { name = String(name).toLowerCase(); if (ceByName.has(name)) return Promise.resolve(ceByName.get(name)); let w = whenDef.get(name); if (!w) { let res; const p = new Promise(r => res = r); w = { p, res }; whenDef.set(name, w); } return w.p; },
  upgrade() {}, getName(c) { return ceByCtor.get(c) || null; }
};
const whenDef = new Map();
const origCreate = P.Document.createElement;
val(P.Document, 'createElement', function (name, opts) { const n = String(name).toLowerCase(); const c = ceByName.get(n) || (opts && opts.is && ceByName.get(opts.is)); if (c) return new c(); return origCreate.call(this, name); });
G.__connected = el => { const c = ceByName.get(el.localName); if (c && Object.getPrototypeOf(el) !== c.prototype) { Object.setPrototypeOf(el, c.prototype); } try { if (el.connectedCallback) el.connectedCallback(); } catch (e) { reportErr(e); } };
G.__fireLoad = function () {
  const d = new Event('DOMContentLoaded', { bubbles: true });
  d.isTrusted = true;
  run(document, d);
  const rs = new Event('readystatechange');
  run(document, rs);
  const l = new Event('load');
  l.isTrusted = true;
  run(G, l);
  run(G, new PageTransitionEvent('pageshow'));
};
G.onerror = null;
G.onunhandledrejection = null;
class TimeRanges {
  constructor(r) { this._r = r || []; }
  get length() { return this._r.length; }
  start(i) { if (i < 0 || i >= this._r.length) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r[i][0]; }
  end(i) { if (i < 0 || i >= this._r.length) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r[i][1]; }
}
G.TimeRanges = TimeRanges;
const rangesAdd = (rs, a, b) => { if (!(b > a)) return rs; const o = rs.concat([[a, b]]).sort((x, y) => x[0] - y[0]); const m = []; for (const r of o) { const l = m[m.length - 1]; if (l && r[0] <= l[1] + 0.25) l[1] = Math.max(l[1], r[1]); else m.push([r[0], r[1]]); } return m; };
const rangesCut = (rs, a, b) => { const o = []; for (const [x, y] of rs) { if (y <= a || x >= b) o.push([x, y]); else { if (x < a) o.push([x, a]); if (y > b) o.push([b, y]); } } return o; };
const MCMD = { PLAY: 1, PAUSE: 2, SEEK: 3, VOLUME: 4, SIZE: 5, QUIT: 6, LOOP: 7 };
const MF = { PLAYING: 1, PAUSED: 2, ENDED: 4, ERROR: 8, VIDEO: 16, AUDIO: 32, WAITING: 64, READY: 128 };
const OBJURL = new Map();
const urlCreate = URL.createObjectURL;
URL.createObjectURL = function (o) { const u = urlCreate(); OBJURL.set(u, o); return u; };
URL.revokeObjectURL = function (u) { OBJURL.delete(u); };
const MEDIA = new Set();
const mst = el => { let s = el.__ms; if (!s) { s = { id: -1, paused: true, ended: false, cur: 0, dur: NaN, vol: 1, muted: false, rate: 1, ready: 0, net: 0, seeking: false, src: '', ms: null, vw: 0, vh: 0, flags: 0, err: null, lastTU: 0, playFired: false, started: false, seekTo: -1, meta: false, pend: [], dropped: 0, frames: 0, waiting: false, srcObj: null }; val(el, '__ms', s); } return s; };
const fire = (el, t) => { try { el.dispatchEvent(new Event(t)); } catch (e) { reportErr(e); } };
const mediaSrc = el => {
  const s = mst(el);
  if (s.srcObj) return s.srcObj;
  const a = el.getAttribute('src');
  if (a) return N.resolve(a);
  for (const c of el.children) if (c.tagName === 'SOURCE' && c.getAttribute('src')) { const t = c.getAttribute('type'); if (!t || el.canPlayType(t)) return N.resolve(c.getAttribute('src')); }
  return '';
};
class MediaError { constructor(c, m) { this.code = c; this.message = m || ''; } }
MediaError.MEDIA_ERR_ABORTED = 1; MediaError.MEDIA_ERR_NETWORK = 2; MediaError.MEDIA_ERR_DECODE = 3; MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED = 4;
G.MediaError = MediaError;
function mediaStop(el) { const s = mst(el); if (s.id >= 0 || el.__mid) N.mediaOp(6, el); s.id = -1; if (s.ms) { s.ms._detach(); s.ms = null; } MEDIA.delete(el); }
function mediaLoad(el) {
  const s = mst(el);
  const had = s.id >= 0 || s.ms;
  mediaStop(el);
  Object.assign(s, { paused: !s.wantPlay, ended: false, cur: 0, dur: NaN, ready: 0, net: 0, seeking: false, vw: 0, vh: 0, flags: 0, err: null, playFired: false, started: false, meta: false, seekTo: -1, waiting: false });
  if (had) fire(el, 'emptied');
  const src = mediaSrc(el);
  s.src = typeof src === 'string' ? src : '';
  if (!src) { s.net = 3; return; }
  s.net = 2;
  fire(el, 'loadstart');
  const obj = typeof src === 'string' ? OBJURL.get(src) : src;
  const paused = s.paused ? 1 : 0;
  MEDIA.add(el);
  if (obj instanceof MediaSource) { s.ms = obj; obj._attach(el); }
  else if (obj instanceof Blob) {
    s.id = N.mediaOp(0, el, '', 0, 0, paused);
    if (s.id >= 0) { N.mediaOp(3, el, 0, obj._bytes()); N.mediaOp(4, el, 0); }
  } else if (/^data:/.test(src)) {
    const r = N.fetch(src, 'GET', null, null, true);
    s.id = N.mediaOp(0, el, '', 0, 0, paused);
    if (s.id >= 0) { N.mediaOp(3, el, 0, new Uint8Array(r[1])); N.mediaOp(4, el, 0); }
  } else s.id = N.mediaOp(0, el, src, 0, 0, paused);
  if (s.id < 0 && !s.ms) { s.err = new MediaError(4, 'unsupported'); s.net = 3; setTimeout(() => fire(el, 'error'), 0); return; }
  mediaApply(el);
  mediaArm();
}
function mediaApply(el) { const s = mst(el); if (s.id < 0) return; N.mediaOp(1, el, MCMD.VOLUME, s.muted ? 0 : Math.round(s.vol * 1000), 0); if (el.hasAttribute('loop')) N.mediaOp(1, el, MCMD.LOOP, 1, 0); }
let mediaTimer = 0;
function mediaArm() { if (!mediaTimer) mediaTimer = N.setInterval(mediaPoll, 100); }
function mediaPoll() {
  if (!MEDIA.size) { N.clearInterval(mediaTimer); mediaTimer = 0; return; }
  for (const el of MEDIA) {
    const s = mst(el);
    if (s.id < 0) continue;
    const v = N.mediaOp(2, el);
    if (!v) continue;
    const [cur, dur, fl, vw, vh] = v;
    s.flags = fl;
    if (fl & MF.ERROR) { if (!s.err) { s.err = new MediaError(3, 'decode error'); s.paused = true; fire(el, 'error'); } continue; }
    if (!s.seeking || s.seekTo < 0 || Math.abs(cur / 1000 - s.seekTo) < 1.5 || (fl & MF.PLAYING)) {
      if (s.seeking && (fl & MF.READY)) { s.seeking = false; s.seekTo = -1; fire(el, 'seeked'); fire(el, 'timeupdate'); }
      if (!s.seeking) s.cur = cur / 1000;
    }
    const d = dur > 0 ? dur / 1000 : (s.ms && s.ms._dur > 0 ? s.ms._dur : NaN);
    if ((fl & MF.READY) && !s.meta) {
      s.meta = true; s.vw = vw; s.vh = vh; if (!isNaN(d)) s.dur = d;
      s.ready = 1; fire(el, 'durationchange'); fire(el, 'loadedmetadata');
    }
    if (!(isNaN(d) && isNaN(s.dur)) && d !== s.dur && !isNaN(d)) { s.dur = d; fire(el, 'durationchange'); }
    if ((vw && vw !== s.vw) || (vh && vh !== s.vh)) { s.vw = vw; s.vh = vh; fire(el, 'resize'); }
    if (s.meta && s.ready < 4 && (cur > 0 || (fl & MF.PLAYING) || (fl & MF.PAUSED))) { s.ready = 4; s.net = 1; fire(el, 'loadeddata'); fire(el, 'canplay'); fire(el, 'canplaythrough'); }
    const waiting = !!(fl & MF.WAITING);
    if (waiting && !s.waiting) { s.waiting = true; s.ready = 2; fire(el, 'waiting'); }
    if (!waiting && s.waiting) { s.waiting = false; s.ready = 4; if (!s.paused) fire(el, 'playing'); }
    if ((fl & MF.PLAYING) && !s.paused && !s.playFired) { s.playFired = true; s.started = true; fire(el, 'playing'); }
    if ((fl & MF.ENDED) && !s.ended && !el.hasAttribute('loop')) { s.ended = true; s.paused = true; if (!isNaN(s.dur)) s.cur = s.dur; fire(el, 'timeupdate'); fire(el, 'pause'); fire(el, 'ended'); }
    const now = Date.now();
    if (!s.paused && now - s.lastTU >= 250) { s.lastTU = now; s.frames += 8; fire(el, 'timeupdate'); }
    if (s.ms) s.ms._poll();
  }
}
function mediaPlay(el) {
  const s = mst(el);
  s.wantPlay = true;
  if (s.id < 0 && !s.ms) mediaLoad(el);
  if (s.ended) { s.ended = false; s.cur = 0; if (s.id >= 0) N.mediaOp(1, el, MCMD.SEEK, 0, 0); }
  const was = s.paused;
  s.paused = false;
  if (s.id >= 0) N.mediaOp(1, el, MCMD.PLAY, 0, 0);
  if (was) { s.playFired = false; fire(el, 'play'); }
  mediaArm();
  return Promise.resolve();
}
function mediaPause(el) {
  const s = mst(el);
  s.wantPlay = false;
  if (s.paused) return;
  s.paused = true;
  if (s.id >= 0) N.mediaOp(1, el, MCMD.PAUSE, 0, 0);
  fire(el, 'timeupdate'); fire(el, 'pause');
}
function mediaSeek(el, t) {
  const s = mst(el);
  t = +t; if (!isFinite(t) || t < 0) t = 0;
  if (!isNaN(s.dur) && t > s.dur) t = s.dur;
  s.cur = t; s.seekTo = t; s.seeking = true; s.ended = false;
  fire(el, 'seeking');
  if (s.ms) s.ms._seek(t);
  else if (s.id >= 0) N.mediaOp(1, el, MCMD.SEEK, Math.round(t * 1000), 0);
  else { s.seeking = false; s.seekTo = -1; }
  fire(el, 'timeupdate');
}
G.__mediaUI = (el, code) => { if (code === -1) { if (el.paused) el.play(); else el.pause(); } else if (code >= 0) { const d = el.duration; if (isFinite(d)) el.currentTime = d * code / 10000; } };
const CAN = t => {
  t = String(t || '').toLowerCase();
  const m = /^\s*(video|audio)\/([\w.+-]+)/.exec(t);
  if (!m) return '';
  const cont = m[2];
  if (!['mp4', 'webm', 'ogg', 'mpeg', 'mp3', 'x-matroska', 'matroska', 'wav', 'aac', 'x-m4a', 'mp4a-latm', 'quicktime'].includes(cont)) return '';
  const cm = /codecs\s*=\s*"?([^";]+)"?/.exec(t);
  if (cm) for (const c of cm[1].split(',').map(x => x.trim())) if (!/^(avc1|avc3|hev1|hvc1|mp4a|vp8|vp08|vp9|vp09|opus|vorbis|mp3|aac|flac)/.test(c) || /^(vp09\.0[1-3]|mp4a\.40\.(42|29)|ec-3|ac-3)/.test(c)) return '';
  const wm = /width\s*=\s*(\d+)/.exec(t), hm = /height\s*=\s*(\d+)/.exec(t), fm = /framerate\s*=\s*([\d.]+)/.exec(t);
  if ((wm && +wm[1] > 1920) || (hm && +hm[1] > 1088) || (fm && +fm[1] > 60)) return '';
  if (/channels\s*=\s*([3-9])/.test(t)) return '';
  return cm ? 'probably' : 'maybe';
};
for (const p of [T.video, T.audio]) {
  val(p, 'play', function () { return mediaPlay(this); });
  val(p, 'pause', function () { mediaPause(this); });
  val(p, 'load', function () { mediaLoad(this); });
  val(p, 'canPlayType', function (t) { return CAN(t); });
  val(p, 'fastSeek', function (t) { mediaSeek(this, t); });
  val(p, 'addTextTrack', function () { return { mode: 'hidden', cues: [], addCue() {}, removeCue() {}, addEventListener() {}, removeEventListener() {} }; });
  val(p, 'setSinkId', function () { return Promise.resolve(); });
  val(p, 'captureStream', function () { return null; });
  def(p, 'src', function () { const a = this.getAttribute('src'); return a ? N.resolve(a) : ''; }, function (v) { mst(this).srcObj = null; this.setAttribute('src', String(v)); mediaLoad(this); if (this.autoplay || mst(this).wantPlay) mediaPlay(this); });
  def(p, 'srcObject', function () { return mst(this).srcObj; }, function (v) { mst(this).srcObj = v || null; mediaLoad(this); if (this.autoplay) mediaPlay(this); });
  def(p, 'currentSrc', function () { return mst(this).src; });
  def(p, 'paused', function () { return mst(this).paused; });
  def(p, 'ended', function () { return mst(this).ended; });
  def(p, 'seeking', function () { return mst(this).seeking; });
  def(p, 'currentTime', function () { return mst(this).cur; }, function (t) { mediaSeek(this, t); });
  def(p, 'duration', function () { const s = mst(this); if (s.ms) { const d = s.ms.duration; if (d >= 0) return d; } return s.dur; });
  def(p, 'readyState', function () { return mst(this).ready; });
  def(p, 'networkState', function () { return mst(this).net; });
  def(p, 'error', function () { return mst(this).err; });
  def(p, 'volume', function () { return mst(this).vol; }, function (v) { const s = mst(this); v = +v; if (!(v >= 0 && v <= 1)) throw new DOMException('volume out of range', 'IndexSizeError'); s.vol = v; mediaApply(this); fire(this, 'volumechange'); });
  def(p, 'muted', function () { return mst(this).muted; }, function (v) { const s = mst(this); s.muted = !!v; mediaApply(this); fire(this, 'volumechange'); });
  def(p, 'defaultMuted', function () { return this.hasAttribute('muted'); }, function (v) { if (v) this.setAttribute('muted', ''); else this.removeAttribute('muted'); });
  def(p, 'playbackRate', function () { return mst(this).rate; }, function (v) { mst(this).rate = +v || 1; fire(this, 'ratechange'); });
  def(p, 'defaultPlaybackRate', function () { return 1; }, function () {});
  def(p, 'preservesPitch', function () { return true; }, function () {});
  def(p, 'buffered', function () { const s = mst(this); if (s.ms) return new TimeRanges(s.ms._buffered()); if (s.id >= 0 && !isNaN(s.dur)) { const v = N.mediaOp(2, this); return new TimeRanges([[0, s.dur * (v ? v[5] : 0) / 100]]); } return new TimeRanges([]); });
  def(p, 'seekable', function () { const d = this.duration; return new TimeRanges(isFinite(d) ? [[0, d]] : []); });
  def(p, 'played', function () { return new TimeRanges(mst(this).started ? [[0, mst(this).cur]] : []); });
  def(p, 'textTracks', function () { const a = []; a.addEventListener = () => {}; a.removeEventListener = () => {}; a.getTrackById = () => null; return a; });
  def(p, 'audioTracks', function () { const a = []; a.addEventListener = () => {}; a.removeEventListener = () => {}; return a; });
  def(p, 'videoTracks', function () { const a = []; a.addEventListener = () => {}; a.removeEventListener = () => {}; return a; });
  def(p, 'mediaKeys', function () { return null; });
  val(p, 'setMediaKeys', function () { return Promise.reject(new DOMException('EME not supported', 'NotSupportedError')); });
  for (const k of ['autoplay', 'loop', 'controls', 'playsInline']) def(p, k, function () { return this.hasAttribute(k.toLowerCase()); }, function (v) { if (v) this.setAttribute(k.toLowerCase(), ''); else this.removeAttribute(k.toLowerCase()); if (k === 'loop' && mst(this).id >= 0) N.mediaOp(1, this, MCMD.LOOP, v ? 1 : 0, 0); });
  for (const k of ['preload', 'crossOrigin', 'poster']) def(p, k, function () { const a = this.getAttribute(k.toLowerCase()); return k === 'poster' && a ? N.resolve(a) : a || ''; }, function (v) { this.setAttribute(k.toLowerCase(), String(v)); });
  for (const [k, n] of [['HAVE_NOTHING', 0], ['HAVE_METADATA', 1], ['HAVE_CURRENT_DATA', 2], ['HAVE_FUTURE_DATA', 3], ['HAVE_ENOUGH_DATA', 4], ['NETWORK_EMPTY', 0], ['NETWORK_IDLE', 1], ['NETWORK_LOADING', 2], ['NETWORK_NO_SOURCE', 3]]) val(p, k, n);
}
def(T.video, 'videoWidth', function () { return mst(this).vw; });
def(T.video, 'videoHeight', function () { return mst(this).vh; });
val(T.video, 'getVideoPlaybackQuality', function () { const s = mst(this); return { creationTime: performance.now(), totalVideoFrames: s.frames, droppedVideoFrames: s.dropped, corruptedVideoFrames: 0 }; });
val(T.video, 'requestVideoFrameCallback', function (cb) { const el = this; return setTimeout(() => cb(performance.now(), { mediaTime: el.currentTime, width: el.videoWidth, height: el.videoHeight, presentedFrames: mst(el).frames }), 33); });
val(T.video, 'cancelVideoFrameCallback', function (h) { clearTimeout(h); });
val(T.video, 'requestPictureInPicture', function () { return Promise.reject(new DOMException('PiP not supported', 'NotSupportedError')); });
G.Audio = function Audio(src) { const a = document.createElement('audio'); if (src !== undefined) a.src = src; return a; };
G.Audio.prototype = T.audio;
function mediaScan() {
  for (const el of document.querySelectorAll('video,audio')) {
    const s = mst(el);
    if (s.srcObj) continue;
    const src = mediaSrc(el);
    if (src && src !== s.src && !s.ms) { mediaLoad(el); if (el.hasAttribute('autoplay')) mediaPlay(el); }
  }
}
G.addEventListener('DOMContentLoaded', () => { mediaScan(); N.setInterval(mediaScan, 700); });

class SourceBuffer extends EventTarget {
  constructor(ms, type, idx) { super(); this._ms = ms; this._type = type; this._idx = idx; this.updating = false; this.mode = 'segments'; this.timestampOffset = 0; this.appendWindowStart = 0; this.appendWindowEnd = Infinity; this._r = []; this._q = []; this._ts = 0; this._dflt = 0; this._mp4 = /mp4/.test(type); this._tcs = 1e6; this._init = false; this._bytes = 0; this.onupdatestart = this.onupdate = this.onupdateend = this.onerror = this.onabort = null; this.audioTracks = []; this.videoTracks = []; this.textTracks = []; }
  get buffered() { return new TimeRanges(this._r); }
  _ev(t) { const e = new Event(t); run(this, e); }
  appendBuffer(d) {
    if (this.updating) throw new DOMException('SourceBuffer is updating', 'InvalidStateError');
    if (this._ms.readyState === 'closed') throw new DOMException('MediaSource is closed', 'InvalidStateError');
    const u = u8of(d);
    if (!u) throw new TypeError('appendBuffer needs an ArrayBuffer or view');
    const copy = new Uint8Array(u);
    this.updating = true;
    if (this._ms.readyState === 'ended') { this._ms.readyState = 'open'; this._ms._ev('sourceopen'); }
    this._ev('updatestart');
    N.setTimeout(() => {
      if (!this.updating) return;
      try { this._scan(copy); } catch (e) {}
      this._ms._push(this._idx, copy);
      this._bytes += copy.length;
      this.updating = false;
      this._ev('update'); this._ev('updateend');
    }, 0);
  }
  abort() { if (this.updating) { this.updating = false; this._ev('abort'); this._ev('updateend'); } }
  remove(a, b) { if (this.updating) throw new DOMException('SourceBuffer is updating', 'InvalidStateError'); this.updating = true; this._ev('updatestart'); N.setTimeout(() => { this._r = rangesCut(this._r, a, b); this.updating = false; this._ev('update'); this._ev('updateend'); }, 0); }
  changeType(t) { this._type = t; this._mp4 = /mp4/.test(t); }
  _add(a, b) { a += this.timestampOffset; b += this.timestampOffset; this._r = rangesAdd(this._r, a, b); }
  _scan(u) {
    const dv = new DataView(u.buffer, u.byteOffset, u.byteLength);
    if (this._mp4) {
      const box = (o, e, f) => { while (o + 8 <= e) { let sz = dv.getUint32(o); const ty = String.fromCharCode(u[o + 4], u[o + 5], u[o + 6], u[o + 7]); let h = 8; if (sz === 1) { sz = dv.getUint32(o + 8) * 4294967296 + dv.getUint32(o + 12); h = 16; } else if (sz === 0) sz = e - o; if (sz < h) break; f(ty, o + h, Math.min(o + sz, e)); o += sz; } };
      box(0, u.length, (t, s, e) => {
        if (t === 'moov') { this._init = true; box(s, e, (t2, s2, e2) => {
          if (t2 === 'trak') box(s2, e2, (t3, s3, e3) => { if (t3 === 'mdia') box(s3, e3, (t4, s4) => { if (t4 === 'mdhd') { const v = u[s4]; this._ts = v === 1 ? dv.getUint32(s4 + 20) : dv.getUint32(s4 + 12); } }); });
          if (t2 === 'mvex') box(s2, e2, (t3, s3) => { if (t3 === 'trex') this._dflt = dv.getUint32(s3 + 12); });
        }); }
        if (t === 'moof') {
          let base = 0, dd = this._dflt, total = 0;
          box(s, e, (t2, s2, e2) => { if (t2 === 'traf') box(s2, e2, (t3, s3) => {
            const fl = dv.getUint32(s3) & 0xFFFFFF;
            if (t3 === 'tfhd') { let o = s3 + 8; if (fl & 1) o += 8; if (fl & 2) o += 4; if (fl & 8) dd = dv.getUint32(o); }
            if (t3 === 'tfdt') base = u[s3] === 1 ? dv.getUint32(s3 + 4) * 4294967296 + dv.getUint32(s3 + 8) : dv.getUint32(s3 + 4);
            if (t3 === 'trun') { const n = dv.getUint32(s3 + 4); let o = s3 + 8; if (fl & 1) o += 4; if (fl & 4) o += 4; if (fl & 0x100) { const st = (fl & 0x200 ? 4 : 0) + (fl & 0x400 ? 4 : 0) + (fl & 0x800 ? 4 : 0) + 4; for (let i = 0; i < n && o + 4 <= u.length; i++) { total += dv.getUint32(o); o += st; } } else total += n * dd; }
          }); });
          if (this._ts > 0) this._add(base / this._ts, (base + total) / this._ts);
        }
      });
    } else {
      const vint = (o, keep) => { const b = u[o]; let l = 1, m = 0x80; while (l <= 8 && !(b & m)) { l++; m >>= 1; } let v = keep ? b : b & (m - 1); for (let i = 1; i < l; i++) v = v * 256 + u[o + i]; return [v, l]; };
      const uint = (o, n) => { let v = 0; for (let i = 0; i < n; i++) v = v * 256 + u[o + i]; return v; };
      const walk = (o, e, f) => { while (o < e) { const [id, il] = vint(o, true); if (o + il >= u.length) break; const [sz, sl] = vint(o + il, false); const s = o + il + sl; const unk = sz === Math.pow(2, 7 * sl) - 1; const end = unk ? e : Math.min(s + sz, e); f(id, s, end, unk); o = end; } };
      let lastClusterStart = -1;
      const cluster = (s, e) => { let tc = 0, mx = 0, seen = false; walk(s, e, (id, s2, e2) => { if (id === 0xE7) tc = uint(s2, e2 - s2); else if (id === 0xA3 || id === 0xA0) { let bo = s2; if (id === 0xA0) { let found = -1; walk(s2, e2, (i3, s3) => { if (i3 === 0xA1) found = s3; }); if (found < 0) return; bo = found; } const [, tl] = vint(bo, false); const rel = dv.getInt16(bo + tl); if (rel > mx) mx = rel; seen = true; } }); if (seen || tc) { const a = tc * this._tcs / 1e9, b = (tc + mx) * this._tcs / 1e9 + 0.04; this._add(a, b); lastClusterStart = a; } };
      walk(0, u.length, (id, s, e) => {
        if (id === 0x18538067) { this._init = true; walk(s, e, (i2, s2, e2) => { if (i2 === 0x1549A966) walk(s2, e2, (i3, s3, e3) => { if (i3 === 0x2AD7B1) this._tcs = uint(s3, e3 - s3); else if (i3 === 0x4489) { const l = e3 - s3; this._ms._edur = (l === 4 ? dv.getFloat32(s3) : dv.getFloat64(s3)) * this._tcs / 1e9; } }); else if (i2 === 0x1F43B675) cluster(s2, e2); }); }
        else if (id === 0x1F43B675) cluster(s, e);
      });
    }
  }
}
class SourceBufferList extends EventTarget { constructor() { super(); this._l = []; } get length() { return this._l.length; } [Symbol.iterator]() { return this._l[Symbol.iterator](); } }
class MediaSource extends EventTarget {
  constructor() { super(); this.readyState = 'closed'; this.sourceBuffers = new SourceBufferList(); this.activeSourceBuffers = this.sourceBuffers; this._dur = NaN; this._el = null; this._started = false; this._pending = [[], []]; this._edur = NaN; this.onsourceopen = this.onsourceended = this.onsourceclose = null; this.handle = this; }
  static isTypeSupported(t) { return CAN(t) !== ''; }
  static get canConstructInDedicatedWorker() { return false; }
  get duration() { return isNaN(this._dur) && !isNaN(this._edur) ? this._edur : this._dur; }
  set duration(v) { this._dur = +v; if (this._el) { if (this._dur > 0 && isFinite(this._dur)) N.mediaOp(1, this._el, 8, Math.round(this._dur * 1000), 0); fire(this._el, 'durationchange'); } }
  _ev(t) { run(this, new Event(t)); }
  _attach(el) { this._el = el; N.setTimeout(() => { if (this._el !== el) return; this.readyState = 'open'; this._ev('sourceopen'); }, 0); }
  _detach() { if (this._el) { this._el = null; this.readyState = 'closed'; this._started = false; this._ev('sourceclose'); } }
  addSourceBuffer(t) {
    if (!CAN(t)) throw new DOMException('Unsupported type ' + t, 'NotSupportedError');
    if (this.readyState !== 'open') throw new DOMException('MediaSource is not open', 'InvalidStateError');
    if (this.sourceBuffers._l.length >= 2 || this._started) throw new DOMException('Too many source buffers', 'QuotaExceededError');
    const sb = new SourceBuffer(this, t, this.sourceBuffers._l.length);
    this.sourceBuffers._l.push(sb);
    run(this.sourceBuffers, new Event('addsourcebuffer'));
    return sb;
  }
  removeSourceBuffer(sb) { const i = this.sourceBuffers._l.indexOf(sb); if (i >= 0) this.sourceBuffers._l.splice(i, 1); }
  endOfStream(err) { if (this.readyState !== 'open') return; this.readyState = 'ended'; if (this._started && this._el) for (let i = 0; i < this.sourceBuffers._l.length; i++) N.mediaOp(4, this._el, i); this._ev('sourceended'); }
  setLiveSeekableRange() {} clearLiveSeekableRange() {}
  _start() {
    const el = this._el; if (!el) return;
    const s = mst(el);
    const n = this.sourceBuffers._l.length;
    s.id = N.mediaOp(0, el, '', n > 1 ? 1 : 0, Math.round((s.seekTo > 0 ? s.seekTo : 0) * 1000), s.paused ? 1 : 0);
    this._started = s.id >= 0;
    if (!this._started) { s.err = new MediaError(4, 'unsupported'); fire(el, 'error'); return; }
    mediaApply(el);
    const dd = this.duration; if (dd > 0 && isFinite(dd)) N.mediaOp(1, el, 8, Math.round(dd * 1000), 0);
    for (let i = 0; i < 2; i++) { for (const c of this._pending[i]) N.mediaOp(3, el, i, c); this._pending[i] = []; }
    mediaArm();
  }
  _push(idx, u) {
    if (!this._el) return;
    if (!this._started) {
      this._pending[idx].push(u);
      const n = this.sourceBuffers._l.length;
      const ready = this.sourceBuffers._l.every(b => b._init);
      if (ready || this._pending[idx].length > 3) this._start();
      return;
    }
    N.mediaOp(3, this._el, idx, u);
  }
  _seek(t) {
    for (const sb of this.sourceBuffers._l) { sb._r = sb._r.filter(r => t >= r[0] - 0.5 && t <= r[1] + 0.5); }
    if (this._started && this._el) { const inb = this.sourceBuffers._l.every(sb => sb._r.length > 0); N.mediaOp(5, this._el, Math.round(t * 1000), inb ? 1 : 0); }
  }
  _buffered() { const l = this.sourceBuffers._l; if (!l.length) return []; let r = l[0]._r; for (let i = 1; i < l.length; i++) { const o = []; for (const [a, b] of r) for (const [c, d] of l[i]._r) { const x = Math.max(a, c), y = Math.min(b, d); if (y > x) o.push([x, y]); } r = o; } return r; }
  _poll() {}
}
Object.assign(G, { MediaSource, SourceBuffer, SourceBufferList, WebKitMediaSource: MediaSource });
G.ManagedMediaSource = undefined;
for (const k of Object.keys(G)) { }

AbortSignal.abort = function (r) { const c = new AbortController(); c.abort(r); return c.signal; };
AbortSignal.timeout = function (ms) { const c = new AbortController(); setTimeout(() => c.abort(new DOMException('The operation timed out.', 'TimeoutError')), ms); return c.signal; };
AbortSignal.any = function (list) { const c = new AbortController(); for (const s of list) { if (s.aborted) { c.abort(s.reason); break; } s.addEventListener('abort', () => c.abort(s.reason)); } return c.signal; };
if (!AbortSignal.prototype.throwIfAborted) AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
if (!G.scheduler) G.scheduler = {
  postTask(cb, o) { o = o || {}; return new Promise((res, rej) => { const run = () => { if (o.signal && o.signal.aborted) { rej(o.signal.reason); return; } try { res(cb()); } catch (e) { rej(e); } }; setTimeout(run, o.delay || 0); }); },
  yield() { return new Promise(r => setTimeout(r, 0)); }
};
if (G.navigator && !G.navigator.scheduling) G.navigator.scheduling = { isInputPending() { return false; } };
if (G.navigator && !G.navigator.getBattery) G.navigator.getBattery = () => Promise.resolve({ charging: true, level: 1, chargingTime: 0, dischargingTime: Infinity, addEventListener() {}, removeEventListener() {} });
if (!G.MediaMetadata) G.MediaMetadata = class MediaMetadata { constructor(o) { o = o || {}; this.title = o.title || ''; this.artist = o.artist || ''; this.album = o.album || ''; this.artwork = o.artwork || []; } };
if (G.document && !G.document.timeline) G.document.timeline = { get currentTime() { return G.performance ? G.performance.now() : Date.now(); } };
if (!G.Path2D) G.Path2D = class Path2D { constructor() { this.ops = []; } moveTo(x, y) { this.ops.push(['m', x, y]); } lineTo(x, y) { this.ops.push(['l', x, y]); } closePath() {} rect() {} arc() {} arcTo() {} bezierCurveTo() {} quadraticCurveTo() {} ellipse() {} addPath() {} roundRect() {} };
if (!G.createImageBitmap) G.createImageBitmap = (src) => Promise.resolve({ width: (src && src.width) || 0, height: (src && src.height) || 0, close() {} });
if (!G.XPathResult) G.XPathResult = { ANY_TYPE: 0, NUMBER_TYPE: 1, STRING_TYPE: 2, BOOLEAN_TYPE: 3, UNORDERED_NODE_ITERATOR_TYPE: 4, ORDERED_NODE_ITERATOR_TYPE: 5, UNORDERED_NODE_SNAPSHOT_TYPE: 6, ORDERED_NODE_SNAPSHOT_TYPE: 7, ANY_UNORDERED_NODE_TYPE: 8, FIRST_ORDERED_NODE_TYPE: 9 };
if (G.document && !G.document.evaluate) G.document.evaluate = function () { return { snapshotLength: 0, snapshotItem() { return null; }, iterateNext() { return null; }, singleNodeValue: null, resultType: 0 }; };
if (!G.reportError) G.reportError = (e) => { try { console.error(e && e.stack ? e.stack : String(e)); } catch (x) {} };
})();
