if (typeof Intl === 'undefined') (function () {
  var G = globalThis;
  function loc(l) { if (Array.isArray(l)) l = l[0]; return typeof l === 'string' && l ? l : 'en-US'; }
  function grp(s) { var neg = s[0] === '-'; if (neg) s = s.slice(1); var p = s.split('.'); p[0] = p[0].replace(/\B(?=(\d{3})+(?!\d))/g, ','); return (neg ? '-' : '') + p.join('.'); }
  var CUR = { USD: '$', EUR: '€', GBP: '£', JPY: '¥', SEK: 'kr', INR: '₹' };
  var NB = '\u00a0', NNB = '\u202f';
  var LT = {
    en: { g: ',', d: '.', o: 'MDY', s: '/', h12: 1 }, 'en-GB': { g: ',', d: '.', o: 'DMY', s: '/', p: 1, h12: 0 }, 'en-AU': { g: ',', d: '.', o: 'DMY', s: '/', p: 1, h12: 1 }, 'en-CA': { g: ',', d: '.', o: 'YMD', s: '-', p: 1, h12: 1 }, 'en-IN': { g: ',', d: '.', o: 'DMY', s: '/', h12: 1 },
    sv: { g: NB, d: ',', o: 'YMD', s: '-', p: 1, h12: 0, cp: 1, pc: NB, mon: ['januari', 'februari', 'mars', 'april', 'maj', 'juni', 'juli', 'augusti', 'september', 'oktober', 'november', 'december'], ms: ['jan.', 'feb.', 'mars', 'apr.', 'maj', 'juni', 'juli', 'aug.', 'sep.', 'okt.', 'nov.', 'dec.'], day: ['söndag', 'måndag', 'tisdag', 'onsdag', 'torsdag', 'fredag', 'lördag'], dsh: ['sön', 'mån', 'tis', 'ons', 'tors', 'fre', 'lör'], ld: '' },
    de: { g: '.', d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB, mon: ['Januar', 'Februar', 'März', 'April', 'Mai', 'Juni', 'Juli', 'August', 'September', 'Oktober', 'November', 'Dezember'], day: ['Sonntag', 'Montag', 'Dienstag', 'Mittwoch', 'Donnerstag', 'Freitag', 'Samstag'], ld: '.' },
    fr: { g: NNB, d: ',', o: 'DMY', s: '/', p: 1, h12: 0, cp: 1, pc: NNB, mon: ['janvier', 'février', 'mars', 'avril', 'mai', 'juin', 'juillet', 'août', 'septembre', 'octobre', 'novembre', 'décembre'], day: ['dimanche', 'lundi', 'mardi', 'mercredi', 'jeudi', 'vendredi', 'samedi'], ld: '' },
    es: { g: '.', d: ',', o: 'DMY', s: '/', h12: 0, cp: 1, pc: NB, mg: 2, mon: ['enero', 'febrero', 'marzo', 'abril', 'mayo', 'junio', 'julio', 'agosto', 'septiembre', 'octubre', 'noviembre', 'diciembre'], day: ['domingo', 'lunes', 'martes', 'miércoles', 'jueves', 'viernes', 'sábado'], ld: '', de: ' de ' },
    it: { g: '.', d: ',', o: 'DMY', s: '/', h12: 0, cp: 1, pc: NB }, pt: { g: '.', d: ',', o: 'DMY', s: '/', p: 1, h12: 0, cp: 0, pc: NB }, nl: { g: '.', d: ',', o: 'DMY', s: '-', h12: 0, cp: 0, pc: '' },
    da: { g: '.', d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB }, nb: { g: NB, d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB }, no: { g: NB, d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB }, fi: { g: NB, d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB },
    pl: { g: NB, d: ',', o: 'DMY', s: '.', h12: 0, cp: 1, pc: NB, mg: 2 }, ru: { g: NB, d: ',', o: 'DMY', s: '.', p: 1, h12: 0, cp: 1, pc: NB }, ja: { g: ',', d: '.', o: 'YMD', s: '/', h12: 0 }, zh: { g: ',', d: '.', o: 'YMD', s: '/', h12: 0 }, ko: { g: ',', d: '.', o: 'YMD', s: '. ', h12: 1 }
  };
  function lt(l) { var t = LT[l]; if (t) return t; var b = String(l).split('-')[0].toLowerCase(); return LT[b] || LT.en; }
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
    var L = lt(this._l);
    if (o.useGrouping !== false && !(L.mg && Math.abs(v) < 10000)) s2 = grp(s2);
    if (L !== LT.en && L.d !== '.') s2 = s2.replace(/[,.]/g, function (c) { return c === ',' ? '\u0001' : L.d; }).replace(/\u0001/g, L.g);
    if (s2[0] === '-' && L.cp) s2 = '\u2212' + s2.slice(1);
    if (st === 'percent') return s2 + (L.pc || '') + '%';
    if (st === 'currency') { var c = o.currency || 'USD', sym = CUR[c] || c + ' '; if (L.cp) { if (c === 'USD' && L !== LT.en) sym = 'US$'; return s2 + NB + sym; } if (L.pc !== undefined && L !== LT.en && L.d !== '.') return sym + NB + s2; return s2[0] === '-' ? '-' + sym + s2.slice(1) : sym + s2; }
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
    var L = lt(this._l);
    if (L !== LT.en) return locParts(L, this._o, d === undefined ? new Date() : new Date(d));
    d = d === undefined ? new Date() : new Date(d); var o = this._o, P = [], utc = o.timeZone === 'UTC';
    var Y = utc ? d.getUTCFullYear() : d.getFullYear(), M = utc ? d.getUTCMonth() : d.getMonth(), D = utc ? d.getUTCDate() : d.getDate(), W = utc ? d.getUTCDay() : d.getDay(), h = utc ? d.getUTCHours() : d.getHours(), mi = utc ? d.getUTCMinutes() : d.getMinutes(), se = utc ? d.getUTCSeconds() : d.getSeconds();
    function add(t, v) { var NM = o.month === 'long' || o.month === 'short' || o.month === 'narrow'; if (P.length) P.push({ type: 'literal', value: t === 'hour' || t === 'minute' || t === 'second' ? (P[P.length - 1].type === 'hour' || P[P.length - 1].type === 'minute' ? ':' : ', ') : (t === 'day' && NM ? ' ' : t === 'year' && NM ? (o.day ? ', ' : ' ') : NM ? ' ' : '/') }); P.push({ type: t, value: String(v) }); }
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
  function locParts(L, o, d) {
    var utc = o.timeZone === 'UTC', P = [];
    var Y = utc ? d.getUTCFullYear() : d.getFullYear(), M = utc ? d.getUTCMonth() : d.getMonth(), D = utc ? d.getUTCDate() : d.getDate(), W = utc ? d.getUTCDay() : d.getDay(), h = utc ? d.getUTCHours() : d.getHours(), mi = utc ? d.getUTCMinutes() : d.getMinutes(), se = utc ? d.getUTCSeconds() : d.getSeconds();
    var ds = o.dateStyle, ts = o.timeStyle;
    if (ds) o = Object.assign({}, o, ds === 'full' ? { weekday: 'long', month: 'long', day: 'numeric', year: 'numeric' } : ds === 'long' ? { month: 'long', day: 'numeric', year: 'numeric' } : ds === 'medium' ? { month: 'short', day: 'numeric', year: 'numeric' } : { month: '2-digit', day: '2-digit', year: L.o === 'YMD' ? 'numeric' : '2-digit' });
    if (ts) o = Object.assign({}, o, { hour: '2-digit', minute: '2-digit' }, ts !== 'short' ? { second: '2-digit' } : {});
    var lit = function (v) { P.push({ type: 'literal', value: v }); }, put = function (t, v) { P.push({ type: t, value: String(v) }); };
    var named = o.month === 'long' || o.month === 'short' || o.month === 'narrow';
    var mons = L.mon || MON, days = L.day || DAY;
    if (o.weekday) { put('weekday', o.weekday === 'long' ? days[W] : (L.dsh ? L.dsh[W] : days[W].slice(0, 3))); if (o.month || o.day || o.year) lit(L === LT.de ? ', ' : ' '); }
    if (named) {
      var mn = o.month === 'long' ? mons[M] : o.month === 'short' ? (L.ms ? L.ms[M] : mons[M].slice(0, 3)) : mons[M][0];
      if (o.day) { put('day', D); lit((L.ld || '') + (L.de || ' ')); }
      put('month', mn);
      if (o.year) { lit(L.de || ' '); put('year', Y); }
    } else if (o.month || o.day || o.year) {
      var pd = function (n, two) { return two ? p2(n) : String(n); };
      var two = L.p || L.o === 'YMD' && L.s === '-';
      var parts = { Y: o.year ? ['year', o.year === '2-digit' ? p2(Y % 100) : Y] : null, M: o.month ? ['month', pd(M + 1, o.month === '2-digit' || two)] : null, D: o.day ? ['day', pd(D, o.day === '2-digit' || two)] : null };
      var first = 1;
      for (var i = 0; i < 3; i++) { var x = parts[L.o[i]]; if (!x) continue; if (!first) lit(L.s); put(x[0], x[1]); first = 0; }
    }
    if (o.hour || o.minute || o.second) {
      if (P.length) lit(L === LT.sv || L === LT.de || L === LT.fr ? ' ' : ', ');
      var h12 = o.hour12 !== undefined ? o.hour12 : (o.hourCycle ? o.hourCycle === 'h12' || o.hourCycle === 'h11' : !!L.h12);
      var f = 1;
      if (o.hour) { put('hour', h12 ? ((h % 12) || 12) : p2(h)); f = 0; }
      if (o.minute) { if (!f) lit(':'); put('minute', p2(mi)); f = 0; }
      if (o.second) { if (!f) lit(':'); put('second', p2(se)); }
      if (o.hour && h12) { lit(' '); put('dayPeriod', h < 12 ? 'AM' : 'PM'); }
    }
    return P;
  }
  DateTimeFormat.prototype.format = function (d) { return this.formatToParts(d).map(function (p) { return p.value; }).join(''); };
  DateTimeFormat.prototype.formatRange = function (a, b) { return this.format(a) + ' – ' + this.format(b); };
  DateTimeFormat.prototype.resolvedOptions = function () { var r = { locale: this._l, calendar: 'gregory', numberingSystem: 'latn', timeZone: this._o.timeZone || (typeof __n !== 'undefined' && typeof __n.tzName === 'function' ? __n.tzName() : 'UTC') }; for (var k in this._o) if (k !== 'timeZone') r[k] = this._o[k]; return r; };
  DateTimeFormat.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function PluralRules(l, o) { if (!(this instanceof PluralRules)) throw new TypeError('Constructor Intl.PluralRules requires new'); this._l = loc(l); this._o = o || {}; }
  PluralRules.prototype.select = function (n) { n = Number(n); if (this._o.type === 'ordinal') { var a = n % 10, b = n % 100; return a === 1 && b !== 11 ? 'one' : a === 2 && b !== 12 ? 'two' : a === 3 && b !== 13 ? 'few' : 'other'; } return n === 1 ? 'one' : 'other'; };
  PluralRules.prototype.resolvedOptions = function () { return { locale: this._l, type: this._o.type || 'cardinal', pluralCategories: ['one', 'other'] }; };
  PluralRules.supportedLocalesOf = NumberFormat.supportedLocalesOf;
  function Collator(l, o) { if (!(this instanceof Collator)) return new Collator(l, o); this._l = loc(l); this._o = o || {}; var self = this; this.compare = function (a, b) { return collCmp(String(a), String(b), self._o); }; }
  function collKey(s) { return s.normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase(); }
  function collCmp(a, b, o) { o = o || {}; if (o.numeric) { var ra = /^(\D*)(\d+)(.*)$/.exec(a), rb = /^(\D*)(\d+)(.*)$/.exec(b); if (ra && rb && collKey(ra[1]) === collKey(rb[1]) && ra[2] !== rb[2]) { var x = +ra[2], y = +rb[2]; if (x !== y) return x < y ? -1 : 1; } } var ka = collKey(a), kb = collKey(b); if (ka !== kb) return ka < kb ? -1 : 1; if (o.sensitivity === 'base') return 0; var na = a.normalize('NFD').toLowerCase(), nb = b.normalize('NFD').toLowerCase(); if (na !== nb) return na < nb ? -1 : 1; if (o.sensitivity === 'accent') return 0; if (a === b) return 0; for (var i = 0; i < a.length && i < b.length; i++) { var ca = a[i], cb = b[i]; if (ca !== cb) { var la = ca === ca.toLowerCase(), lb = cb === cb.toLowerCase(); if (la !== lb) return la ? -1 : 1; return ca < cb ? -1 : 1; } } return a.length < b.length ? -1 : 1; }
  String.prototype.localeCompare = function (that, l, o) { return collCmp(String(this), String(that), o); };
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
const kebab0 = k => k.startsWith('--') ? k : k === 'cssFloat' ? 'float' : k.replace(/^(webkit|moz|ms)(?=[A-Z])/, m => '-' + m).replace(/[A-Z]/g, m => '-' + m.toLowerCase());
const kbCache = new Map();
const kebab = k => { let r = kbCache.get(k); if (r === undefined) { r = kebab0(k); if (kbCache.size < 2048) kbCache.set(k, r); } return r; };
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
  const tg = this == null ? G : this;
  const passive = opts && typeof opts === 'object' && opts.passive !== undefined ? !!opts.passive : ((type === 'wheel' || type === 'mousewheel' || type === 'touchstart' || type === 'touchmove') && (tg === G || tg === G.document || (G.document && (tg === G.document.documentElement || tg === G.document.body))));
  const m = listeners(this == null ? G : this);
  let a = m.get(type);
  if (!a) { a = []; m.set(type, a); }
  for (const l of a) if (l.fn === fn && l.capture === capture) return;
  const rec = { fn, capture, once, passive };
  a.push(rec);
  if (opts && opts.signal) opts.signal.addEventListener('abort', () => (this == null ? G : this).removeEventListener(type, fn, opts));
};
EventTarget.prototype.removeEventListener = function (type, fn, opts) {
  const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
  const a = listeners(this == null ? G : this).get(type);
  if (!a) return;
  const i = a.findIndex(l => l.fn === fn && l.capture === capture);
  if (i >= 0) a.splice(i, 1);
};
EventTarget.prototype.dispatchEvent = function (ev) { run(this == null ? G : this, ev); return !ev.defaultPrevented; };
Object.setPrototypeOf(P.Node, EventTarget.prototype);
Object.setPrototypeOf(G, EventTarget.prototype);

class Event {
  constructor(type, init) { init = init || {}; this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.eventPhase = 0; this.timeStamp = N.now(); this.isTrusted = false; this._stop = false; this._imm = false; this._path = []; }
  preventDefault() { if (this.cancelable && !this._pas) this.defaultPrevented = true; }
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
class PointerEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.pointerId = i.pointerId || 1; this.pointerType = i.pointerType || 'mouse'; this.isPrimary = i.isPrimary !== undefined ? !!i.isPrimary : true; this.width = 1; this.height = 1; this.pressure = i.pressure !== undefined ? i.pressure : 0; this.tiltX = 0; this.tiltY = 0; this.twist = 0; this.tangentialPressure = 0; } getCoalescedEvents() { return [this]; } getPredictedEvents() { return []; } }
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
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message !== undefined ? String(i.message) : ''; this.error = i.error !== undefined ? i.error : null; this.filename = i.filename !== undefined ? String(i.filename) : ''; this.lineno = i.lineno >>> 0; this.colno = i.colno >>> 0; } }
class PopStateEvent extends Event { constructor(t, i) { super(t, i); this.state = i && i.state !== undefined ? i.state : null; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.source = i.source || null; this.ports = Array.isArray(i.ports) ? i.ports.slice() : []; this.lastEventId = i.lastEventId !== undefined ? String(i.lastEventId) : ''; } }
class AnimationEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.animationName = i.animationName !== undefined ? String(i.animationName) : ''; this.elapsedTime = +i.elapsedTime || 0; this.pseudoElement = i.pseudoElement !== undefined ? String(i.pseudoElement) : ''; } }
class TransitionEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.propertyName = i.propertyName !== undefined ? String(i.propertyName) : ''; this.elapsedTime = +i.elapsedTime || 0; this.pseudoElement = i.pseudoElement !== undefined ? String(i.pseudoElement) : ''; } }
class TouchEvent extends UIEvent { constructor(t, i) { super(t, i); this.touches = []; this.targetTouches = []; this.changedTouches = []; } }
class ClipboardEvent extends Event { constructor(t, i) { super(t, i); this.clipboardData = (i && i.clipboardData) || null; } }
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
const nvxL = (f) => { try { Object.defineProperty(f, '__nvx', { value: true }); } catch (e) {} return f; };
const LEGACY_EV = { animationend: 'webkitAnimationEnd', animationiteration: 'webkitAnimationIteration', animationstart: 'webkitAnimationStart', transitionend: 'webkitTransitionEnd' };
function invoke(node, ev, phase, on, ih) {
  ev.currentTarget = node;
  ev.eventPhase = phase;
  const m = node[LS];
  const a = m && m.get(ev.type);
  let legacy = null;
  if (ev.isTrusted && LEGACY_EV[ev.type]) {
    let real = !!(a && a.some((l) => !(l.fn && l.fn.__nvx)));
    if (!real) { try { real = typeof node[on || ('on' + ev.type)] === 'function' || (ih !== false && !!inlineHandler(node, ev.type)); } catch (e) {} }
    if (!real) legacy = LEGACY_EV[ev.type];
  }
  invokeIn(node, ev, phase, on, ih, a);
  if (legacy && !ev._imm) {
    const la = m && m.get(legacy), lo = 'on' + legacy.toLowerCase();
    let lh = null; if (phase !== 1) { try { lh = node[lo]; } catch (e) {} }
    if ((la && la.length) || typeof lh === 'function') { const t0 = ev.type; ev.type = legacy; try { invokeIn(node, ev, phase, lo, false, la); } finally { ev.type = t0; } }
  }
}
function invokeIn(node, ev, phase, on, ih, a) {
  if (a && a.length) {
    for (const l of a.slice()) {
      if (phase === 1 && !l.capture) continue;
      if (phase === 3 && l.capture) continue;
      if (l.once) { const i = a.indexOf(l); if (i >= 0) a.splice(i, 1); }
      const pev = G.event; G.event = ev;
      if (l.passive) ev._pas = true;
      try { if (typeof l.fn === 'function') l.fn.call(node, ev); else if (l.fn && typeof l.fn.handleEvent === 'function') l.fn.handleEvent(ev); } catch (err) { reportErr(err); }
      if (l.passive) ev._pas = false;
      G.event = pev;
      if (ev._imm) return;
    }
  }
  if (phase !== 1) {
    let h = null;
    try { h = node[on || ('on' + ev.type)]; } catch (e) {}
    if (typeof h !== 'function' && ih !== false) { h = inlineHandler(node, ev.type); if (!h && node === G && (ev.type === 'load' || ev.type === 'unload' || ev.type === 'beforeunload' || ev.type === 'resize' || ev.type === 'hashchange' || ev.type === 'popstate')) { const b = document.body; if (b) h = inlineHandler(b, ev.type); } }
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
        get() { const m = this[OH]; if (m && Object.prototype.hasOwnProperty.call(m, t)) return m[t]; return (N.onSeen && !N.onSeen(t)) ? null : (inlineHandler(this, t) || null); },
        set(v) { let m = this[OH]; if (!m) { m = {}; try { Object.defineProperty(this, OH, { value: m, configurable: true }); } catch (e) { return; } } m[t] = (typeof v === 'function' || (v && typeof v === 'object')) ? v : null; }
      });
    } catch (e) {}
  }
}
function reportErr(err) { try { N.log((err && err.stack) ? (err.name + ': ' + err.message + '\n' + err.stack) : String(err), 3); } catch (e) {} }
function run(target, ev) {
  const path = [];
  let isNode = target && typeof target.nodeType === 'number';
  if (isNode) { for (let n = target; n; n = n.parentNode || (n.nodeType === 11 ? N.shadowHost(n) : null)) path.push(n); if (path[path.length - 1] === document && ev.type !== 'load') path.push(G); }
  else path.push(target);
  ev.target = target;
  ev._path = path;
  const on = 'on' + ev.type, ih = N.onSeen ? N.onSeen(ev.type) : true;
  for (let i = path.length - 1; i >= 1 && !ev._stop; i--) invoke(path[i], ev, 1, on, ih);
  if (!ev._stop) invoke(target, ev, 2, on, ih);
  if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) invoke(path[i], ev, 3, on, ih);
  ev.currentTarget = null;
  ev.eventPhase = 0;
  return ev.defaultPrevented;
}
const keyNames = { 8: ['Backspace', 8], 9: ['Tab', 9], 10: ['Enter', 13], 13: ['Enter', 13], 27: ['Escape', 27], 127: ['Delete', 46], 19: ['ArrowUp', 38], 20: ['ArrowDown', 40], 21: ['ArrowLeft', 37], 22: ['ArrowRight', 39], 23: ['PageUp', 33], 24: ['PageDown', 34], 25: ['Home', 36], 26: ['End', 35], 32: [' ', 32] };
G.__animEv = function (t, type, name, el) {
  const ev = type[0] === 'a' ? new AnimationEvent(type, { bubbles: true, animationName: name, elapsedTime: el }) : new TransitionEvent(type, { bubbles: true, propertyName: name, elapsedTime: el });
  ev.isTrusted = true;
  run(t, ev);
};
G.__dispatch = function (t, type, bubbles, cx, cy, key) {
  let ev;
  if (pcap && /^(pointermove|pointerup|mousemove|mouseup)$/.test(type)) { if (pcap.isConnected) t = pcap; else pcap = null; }
  if (pcap && type === 'pointerup') setTimeout(() => { pcap = null; }, 0);
  const mb = (key & 0x40000000) !== 0;
  const mbtn = mb ? key & 7 : 0, mbuttons = mb ? (key >> 4) & 7 : (type === 'mousedown' || type === 'pointerdown' ? 1 : 0), mdet = mb ? (key >> 8) & 3 : (type === 'click' || type === 'mousedown' || type === 'mouseup' ? 1 : 0);
  if (/^(click|dblclick|mousedown|mouseup|mousemove|mouseover|mouseout|mouseenter|mouseleave|contextmenu|auxclick)$/.test(type)) ev = new MouseEvent(type, { bubbles: !!bubbles, cancelable: true, clientX: cx, clientY: cy, button: mbtn, buttons: mbuttons, detail: type === 'dblclick' ? 2 : mdet || (type === 'click' ? 1 : 0) });
  else if (/^pointer/.test(type)) ev = new PointerEvent(type, { bubbles: !!bubbles, cancelable: true, clientX: cx, clientY: cy, button: /move/.test(type) ? -1 : mbtn, buttons: mbuttons, pointerId: 1, pointerType: 'mouse', isPrimary: true, pressure: mbuttons ? 0.5 : 0 });
  else if (type === 'wheel') ev = new WheelEvent(type, { bubbles: true, cancelable: true, clientX: cx, clientY: cy, deltaY: (key << 16) >> 16, deltaX: key >> 16 });
  else if (/^key/.test(type)) {
    const mods = key >>> 24; key &= 0xFFFFFF;
    const k = keyNames[key];
    const ch = key >= 32 && key < 0x110000 && !k ? String.fromCodePoint(key) : null;
    const name = k ? k[0] : ch || '';
    const SH0 = { '!': 49, '@': 50, '#': 51, '$': 52, '%': 53, '^': 54, '&': 55, '*': 56, '(': 57, ')': 48 };
    const code = k ? k[1] : ch ? (SH0[ch] || ch.toUpperCase().charCodeAt(0)) : 0;
    const PC = { ' ': 'Space', '-': 'Minus', '=': 'Equal', '[': 'BracketLeft', ']': 'BracketRight', '\\': 'Backslash', ';': 'Semicolon', "'": 'Quote', ',': 'Comma', '.': 'Period', '/': 'Slash', '`': 'Backquote' };
    const SH = { '!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': '-', '+': '=', '{': '[', '}': ']', '|': '\\', ':': ';', '"': "'", '<': ',', '>': '.', '?': '/', '~': '`' };
    const base = ch && SH[ch] ? SH[ch] : ch;
    const kcode = ch && /[a-z]/i.test(ch) ? 'Key' + ch.toUpperCase() : base && /[0-9]/.test(base) ? 'Digit' + base : base && PC[base] ? PC[base] : name;
    const shifted = !!(mods & 2) || (ch && ((ch >= 'A' && ch <= 'Z') || SH[ch] !== undefined));
    ev = new KeyboardEvent(type, { bubbles: true, cancelable: true, key: name, keyCode: code, which: code, charCode: type === 'keypress' ? (ch ? ch.charCodeAt(0) : code) : 0, code: kcode, ctrlKey: !!(mods & 1), shiftKey: shifted, altKey: !!(mods & 4), metaKey: !!(mods & 8), repeat: !!(mods & 16) });
    try { Object.defineProperty(ev, 'which', { value: code }); } catch (e) {}
    ev.isTrusted = true;
    ev.getModifierState = m => m === 'Control' ? !!(mods & 1) : m === 'Shift' ? shifted : m === 'Alt' ? !!(mods & 4) : m === 'Meta' ? !!(mods & 8) : false;
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
let ceUpg = null;
const ceDone = new WeakSet(), ceConn = new WeakSet();
const HTMLElementC = function HTMLElement() {
  if (!new.target) throw new TypeError("Failed to construct 'HTMLElement'");
  if (ceUpg) { const u = ceUpg; ceUpg = null; return u; }
  const name = ceByCtor.get(new.target);
  if (!name) throw new TypeError('Illegal constructor');
  const el = N.newElement(name, new.target.prototype);
  ceDone.add(el);
  return el;
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
  HTMLOutputElement: ['output'], HTMLDataElement: ['data'], HTMLTimeElement: ['time'], HTMLModElement: ['ins', 'del'], HTMLMenuElement: ['menu'], HTMLSlotElement: ['slot'],
  HTMLTableColElement: ['col', 'colgroup'], HTMLDataListElement: ['datalist'], HTMLParamElement: ['param'], HTMLTrackElement: ['track'], HTMLMarqueeElement: ['marquee'], HTMLFontElement: ['font'],
  HTMLFrameSetElement: ['frameset'], HTMLFrameElement: ['frame'], HTMLDirectoryElement: ['dir'], HTMLUnknownElement: []
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
{
  const MV = {"name": "a button fieldset form iframe img input map meta object output select slot textarea param embed frame", "type": "a button embed input link object ol ul li script source style select textarea output fieldset menu", "alt": "area img input", "placeholder": "input textarea", "htmlFor": "label output", "action": "form", "method": "form", "enctype": "form", "noValidate": "form", "target": "a area base form", "rel": "a area link form", "content": "meta", "label": "option optgroup track", "download": "a area", "accept": "input", "autocomplete": "form input select textarea", "pattern": "input", "srcset": "img source", "sizes": "img source link", "loading": "img iframe", "media": "link meta source style", "hreflang": "a area link", "charset": "script a link", "async": "script", "defer": "script", "noModule": "script", "cols": "textarea frameset", "rows": "textarea frameset", "colSpan": "td th", "rowSpan": "td th", "poster": "video", "crossOrigin": "img link script audio video", "referrerPolicy": "a area iframe img link script", "integrity": "link script", "value": "input button option li meter progress select textarea output data param", "valueAsNumber": "input", "checked": "input", "defaultChecked": "input", "readOnly": "input textarea", "required": "input select textarea", "multiple": "input select", "maxLength": "input textarea", "min": "input meter", "max": "input meter progress", "step": "input", "selectedIndex": "select", "selected": "option", "defaultSelected": "option", "index": "option", "text": "a option script title", "href": "a area base link", "src": "img script iframe input source track embed audio video frame", "currentSrc": "img audio video", "form": "button fieldset input label object output select textarea option legend", "naturalWidth": "img", "naturalHeight": "img", "complete": "img", "width": "img canvas iframe video input embed object source", "height": "img canvas iframe video input embed object source", "defaultValue": "input textarea output", "open": "details dialog", "selectionStart": "input textarea", "selectionEnd": "input textarea", "length": "select form", "options": "select datalist", "elements": "form fieldset", "disabled": "button fieldset input optgroup option select textarea link style"};
  const moved = [];
  for (const k of Object.keys(MV)) {
    const d = Object.getOwnPropertyDescriptor(P.Element, k);
    if (!d) continue;
    for (const t of MV[k].split(' ')) { const tp = tagProtos[t]; if (tp && !Object.prototype.hasOwnProperty.call(tp, k)) { try { Object.defineProperty(tp, k, d); } catch (e) {} } }
    moved.push([k, d]);
  }
  for (const [k] of moved) { try { delete P.Element[k]; } catch (e) {} }
  G.__elemOnly = new Map(moved);
}
defOn(P.Element); defOn(P.Document); defOn(G);
G.HTMLMediaElement = G.HTMLVideoElement;
G.SVGSVGElement = SVGElementC;
G.SVGGraphicsElement = SVGElementC;

const document = N.document;
val(G, 'document', document);
const splitTok = (v) => { const o = []; let s = -1; for (let i = 0; i <= v.length; i++) { const c = i < v.length ? v.charCodeAt(i) : 32; if (c === 32 || c === 9 || c === 10 || c === 13 || c === 12) { if (s >= 0) { const t = v.slice(s, i); if (o.indexOf(t) < 0) o.push(t); s = -1; } } else if (s < 0) s = i; } return o; };
const hasCls = (el, a) => { const v = el.getAttribute(a); return v ? splitTok(v) : []; };
const clsChk = (t) => { if (t === '') throw new DOMException("Failed to execute 'add' on 'DOMTokenList': The token provided must not be empty.", 'SyntaxError'); if (/[\s]/.test(t)) throw new DOMException("Failed to execute 'add' on 'DOMTokenList': The token provided ('" + t + "') contains HTML space characters, which are not valid in tokens.", 'InvalidCharacterError'); };
const clsDo = (l, op, t) => { if (N.clsOp) { const r = N.clsOp(l._e, l._a, op, t); if (r >= 0) return r; if (r !== -3) clsChk(t); } clsChk(t); const a = hasCls(l._e, l._a); const has = a.includes(t); const want = op === 1 ? true : op === 2 ? false : op === 3 ? !has : op === 4; if (want && !has) a.push(t); else if (!want && has) a.splice(a.indexOf(t), 1); if (want !== has || ((op === 1 || op === 2) && l._e.hasAttribute(l._a))) l._set(a); return want ? 1 : 0; };
class DOMTokenList {
  constructor(el, attr) { this._e = el; this._a = attr; }
  _set(a) { this._e.setAttribute(this._a, a.join(' ')); }
  add(...t) { if (t.length > 1) for (const x of t) clsChk(String(x)); if (!t.length) { this._set(hasCls(this._e, this._a)); return; } for (const x of t) clsDo(this, 1, String(x)); }
  remove(...t) { if (t.length > 1) for (const x of t) clsChk(String(x)); if (!t.length) { if (this._e.hasAttribute(this._a)) this._set(hasCls(this._e, this._a)); return; } for (const x of t) clsDo(this, 2, String(x)); }
  toggle(t, f) { t = String(t); return !!clsDo(this, f === undefined ? 3 : f ? 4 : 5, t); }
  contains(t) { return N.clsOp ? N.clsOp(this._e, this._a, 0, String(t)) === 1 : hasCls(this._e, this._a).includes(String(t)); }
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
const dsCache = new Map();
const dsKey = k => { let r = dsCache.get(k); if (r === undefined) { r = 'data-' + k.replace(/[A-Z]/g, m => '-' + m.toLowerCase()); if (dsCache.size < 4096) dsCache.set(k, r); } return r; };
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
{
  const natAttrs = Object.getOwnPropertyDescriptor(P.Element, 'attributes');
  const rawList = (el) => { try { const l = natAttrs && natAttrs.get ? natAttrs.get.call(el) : []; return l.some((x) => x.name.startsWith('nvx-')) ? l.filter((x) => !x.name.startsWith('nvx-')) : l; } catch (e) { return []; } };
  const AC = new WeakMap(), MC = new WeakMap();
  function Attr() { throw new TypeError('Illegal constructor'); }
  const AP = Attr.prototype;
  const vget = function () { if (this._e) { const v = this._e.getAttribute(this._n); if (v !== null) this._v = v; } return this._v; };
  const vset = function (v) { v = String(v); this._v = v; if (this._e) this._e.setAttribute(this._n, v); };
  Object.defineProperties(AP, {
    name: { get() { return this._n; }, configurable: true }, localName: { get() { return this._n.replace(/^[^:]*:/, ''); }, configurable: true }, nodeName: { get() { return this._n; }, configurable: true },
    value: { get: vget, set: vset, configurable: true }, nodeValue: { get: vget, set: vset, configurable: true }, textContent: { get: vget, set: vset, configurable: true },
    ownerElement: { get() { return this._e || null; }, configurable: true }, ownerDocument: { get() { return document; }, configurable: true },
    namespaceURI: { get() { const m = /^(xlink|xml|xmlns):/.exec(this._n); return m ? { xlink: 'http://www.w3.org/1999/xlink', xml: 'http://www.w3.org/XML/1998/namespace', xmlns: 'http://www.w3.org/2000/xmlns/' }[m[1]] : null; }, configurable: true },
    prefix: { get() { const m = /^([^:]+):/.exec(this._n); return m ? m[1] : null; }, configurable: true },
    specified: { get() { return true; }, configurable: true }, nodeType: { get() { return 2; }, configurable: true },
    parentNode: { get() { return null; }, configurable: true }, childNodes: { get() { return []; }, configurable: true }, isConnected: { get() { return !!(this._e && this._e.isConnected); }, configurable: true }
  });
  AP.cloneNode = function () { return mkAttr(null, this._n, this.value); };
  AP.isEqualNode = function (o) { return !!o && o.nodeType === 2 && o.name === this._n && o.value === this.value; };
  AP.isSameNode = function (o) { return o === this; };
  AP.hasChildNodes = function () { return false; };
  Object.defineProperty(AP, Symbol.toStringTag, { value: 'Attr', configurable: true });
  Attr.ATTRIBUTE_NODE = 2;
  function mkAttr(el, n, v) { const a = Object.create(AP); Object.defineProperty(a, '_e', { value: el, writable: true }); Object.defineProperty(a, '_n', { value: String(n) }); Object.defineProperty(a, '_v', { value: v == null ? '' : String(v), writable: true }); return a; }
  const htmlish = (el) => { const ns = el.namespaceURI; return !ns || ns === 'http://www.w3.org/1999/xhtml'; };
  const SVGN = {}; for (const k of 'viewBox preserveAspectRatio gradientUnits gradientTransform patternUnits patternContentUnits patternTransform clipPathUnits maskUnits maskContentUnits filterUnits primitiveUnits markerWidth markerHeight markerUnits refX refY stdDeviation textLength lengthAdjust spreadMethod baseFrequency numOctaves kernelMatrix kernelUnitLength startOffset pathLength attributeName attributeType repeatCount repeatDur keyTimes keySplines keyPoints calcMode tableValues specularExponent specularConstant surfaceScale diffuseConstant xChannelSelector yChannelSelector edgeMode targetX targetY limitingConeAngle pointsAtX pointsAtY pointsAtZ stitchTiles viewTarget zoomAndPan requiredExtensions systemLanguage glyphRef'.split(' ')) SVGN[k.toLowerCase()] = k;
  const shown = (el, n) => htmlish(el) ? n : (SVGN[n] || n);
  const attrOf = (el, name) => {
    const list = rawList(el); let hit = null;
    for (const r of list) if (r.name === name) { hit = r; break; }
    if (!hit) { const ln = String(name).toLowerCase(); for (const r of list) if (r.name === ln) { hit = r; break; } }
    if (!hit) return null;
    let m = AC.get(el); if (!m) { m = new Map(); AC.set(el, m); }
    let a = m.get(hit.name); if (!a) { a = mkAttr(el, shown(el, hit.name), hit.value); m.set(hit.name, a); } else { a._e = el; a._v = hit.value; }
    return a;
  };
  const detach = (el, name) => { const m = AC.get(el); name = String(name).toLowerCase(); if (m) { const a = m.get(name); if (a) { a._v = el.getAttribute(name) ?? a._v; a._e = null; m.delete(name); } } };
  function NamedNodeMap() { throw new TypeError('Illegal constructor'); }
  const NP = NamedNodeMap.prototype;
  NP.item = function (i) { const el = this.__el; const l = rawList(el); i = i >>> 0; return i < l.length ? attrOf(el, l[i].name) : null; };
  NP.getNamedItem = function (n) { return attrOf(this.__el, String(n)); };
  NP.getNamedItemNS = function (ns, n) { return attrOf(this.__el, String(n)); };
  NP.setNamedItem = function (a) { return this.__el.setAttributeNode(a); };
  NP.setNamedItemNS = NP.setNamedItem;
  NP.removeNamedItem = function (n) { const a = attrOf(this.__el, String(n)); if (!a) throw new DOMException("Failed to execute 'removeNamedItem' on 'NamedNodeMap': No item with name '" + n + "' was found.", 'NotFoundError'); const nm = a.name; const v = a.value; this.__el.removeAttribute(nm); a._v = v; detach(this.__el, nm); return a; };
  NP.removeNamedItemNS = function (ns, n) { return this.removeNamedItem(n); };
  NP[Symbol.iterator] = function* () { const el = this.__el; for (const r of rawList(el)) yield attrOf(el, r.name); };
  Object.defineProperty(NP, 'length', { get() { return rawList(this.__el).length; }, configurable: true });
  Object.defineProperty(NP, Symbol.toStringTag, { value: 'NamedNodeMap', configurable: true });
  const mkMap = (el) => {
    const t = Object.create(NP); Object.defineProperty(t, '__el', { value: el });
    return new Proxy(t, {
      get(o, k, r) { if (typeof k === 'string') { if (/^\d+$/.test(k)) return o.item(+k); if (!(k in NP) && k !== '__el') { const a = attrOf(el, k); if (a) return a; } } return Reflect.get(o, k, o); },
      has(o, k) { if (typeof k === 'string') { if (/^\d+$/.test(k)) return +k < rawList(el).length; if (attrOf(el, k)) return true; } return k in o; },
      ownKeys(o) { const l = rawList(el); const ks = l.map((_, i) => String(i)); for (const r of l) { const sn = shown(el, r.name); if (!ks.includes(sn)) ks.push(sn); } return ks.concat(Reflect.ownKeys(o)); },
      getOwnPropertyDescriptor(o, k) { if (typeof k === 'string') { if (/^\d+$/.test(k)) { const a = o.item(+k); return a ? { value: a, enumerable: true, configurable: true, writable: false } : undefined; } const a = attrOf(el, k); if (a) return { value: a, enumerable: false, configurable: true, writable: false }; } return Reflect.getOwnPropertyDescriptor(o, k); },
      set() { return true; }
    });
  };
  if (natAttrs && natAttrs.get) Object.defineProperty(P.Element, 'attributes', { get() { let m = MC.get(this); if (!m) { m = mkMap(this); MC.set(this, m); } return m; }, configurable: true, enumerable: true });
  val(P.Element, 'getAttributeNode', function (n) { return attrOf(this, String(n)); });
  val(P.Element, 'getAttributeNodeNS', function (ns, n) { return attrOf(this, String(n)); });
  val(P.Element, 'setAttributeNode', function (a) { if (!a || a.nodeType !== 2) throw new TypeError("Failed to execute 'setAttributeNode' on 'Element': parameter 1 is not of type 'Attr'."); if (a._e && a._e !== this) throw new DOMException("Failed to execute 'setAttributeNode' on 'Element': The node provided is an attribute node that is already an attribute of another Element; attribute nodes must be explicitly cloned.", 'InUseAttributeError'); if (a._e === this) return a; const old = attrOf(this, a._n); if (old) { const ov = old.value; detach(this, old._n); old._v = ov; } this.setAttribute(a._n, a._v); let m = AC.get(this); if (!m) { m = new Map(); AC.set(this, m); } a._e = this; m.set(a._n.toLowerCase(), a); return old; });
  val(P.Element, 'setAttributeNodeNS', function (a) { return this.setAttributeNode(a); });
  val(P.Element, 'removeAttributeNode', function (a) { if (!a || a._e !== this) throw new DOMException("Failed to execute 'removeAttributeNode' on 'Element': The node provided is owned by another element.", 'NotFoundError'); const v = a.value; this.removeAttribute(a._n); a._v = v; detach(this, a._n); return a; });
  G.__mkAttr = (n, ns) => mkAttr(null, n, '');
  G.Attr = Attr; G.NamedNodeMap = NamedNodeMap;
}
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
def(SRP, 'adoptedStyleSheets', function () { return this.__ass || []; }, function (v) { const a = Array.from(v || []); for (const sh of this.__ass || []) if (sh.__roots) sh.__roots.delete(this); val(this, '__ass', a); for (const sh of a) { if (!sh.__roots) val(sh, '__roots', new Set()); sh.__roots.add(this); } N.setAdopted(this, a.map(s => s.__text || '').join('\n')); });
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
let pcap = null;
val(P.Element, 'setPointerCapture', function () { pcap = this; });
val(P.Element, 'releasePointerCapture', function () { if (pcap === this) pcap = null; });
val(P.Element, 'hasPointerCapture', function () { return pcap === this; });
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
{
  const POP = 'nvx-popover-open';
  const isOpen = (el) => el.hasAttribute(POP);
  const kind = (el) => { const v = (el.getAttribute('popover') || '').toLowerCase(); return v === 'manual' ? 'manual' : v === 'hint' ? 'hint' : 'auto'; };
  const openAuto = [];
  const fireToggle = (el, o, n, cancelable) => { const ev = new Event('beforetoggle', { cancelable }); ev.oldState = o; ev.newState = n; el.dispatchEvent(ev); return !ev.defaultPrevented; };
  const after = (el, o, n) => setTimeout(() => { const ev = new Event('toggle'); ev.oldState = o; ev.newState = n; el.dispatchEvent(ev); }, 0);
  const check = (el, who) => { if (!el.hasAttribute('popover')) throw new DOMException("Failed to execute '" + who + "' on 'HTMLElement': Not supported on elements that do not have a valid value for the 'popover' attribute.", 'NotSupportedError'); if (!el.isConnected) throw new DOMException("Failed to execute '" + who + "' on 'HTMLElement': Invalid on disconnected popover elements.", 'InvalidStateError'); };
  const hide = (el, ev) => { if (!isOpen(el)) return; if (ev !== false) fireToggle(el, 'open', 'closed', false); el.removeAttribute(POP); const i = openAuto.indexOf(el); if (i >= 0) openAuto.splice(i, 1); after(el, 'open', 'closed'); };
  const show = (el) => {
    if (G.__docFlush) G.__docFlush();
    if (isOpen(el)) return;
    if (!fireToggle(el, 'closed', 'open', true)) return;
    if (kind(el) === 'auto') { for (let i = openAuto.length - 1; i >= 0; i--) { const o = openAuto[i]; if (!o.contains(el)) hide(o); } openAuto.push(el); }
    el.setAttribute(POP, '');
    after(el, 'closed', 'open');
  };
  val(P.HTMLElement, 'showPopover', function () { check(this, 'showPopover'); show(this); });
  val(P.HTMLElement, 'hidePopover', function () { check(this, 'hidePopover'); hide(this); });
  val(P.HTMLElement, 'togglePopover', function (f) { check(this, 'togglePopover'); const want = typeof f === 'boolean' ? f : (f && typeof f.force === 'boolean' ? f.force : !isOpen(this)); if (want) show(this); else hide(this); return isOpen(this); });
  def(P.HTMLElement, 'popover', function () { if (!this.hasAttribute('popover')) return null; return kind(this); }, function (v) { if (v === null || v === undefined) this.removeAttribute('popover'); else this.setAttribute('popover', String(v)); });
  const tgt = (b) => { const id = b.getAttribute('popovertarget'); return b.popoverTargetElement !== undefined && b.__pte ? b.__pte : (id ? document.getElementById(id) : null); };
  for (const p of [tagProtos.button, tagProtos.input]) if (p) {
    def(p, 'popoverTargetElement', function () { return this.__pte || (this.getAttribute('popovertarget') ? document.getElementById(this.getAttribute('popovertarget')) : null); }, function (v) { this.__pte = v; });
    def(p, 'popoverTargetAction', function () { const a = (this.getAttribute('popovertargetaction') || '').toLowerCase(); return a === 'show' || a === 'hide' ? a : 'toggle'; }, function (v) { this.setAttribute('popovertargetaction', String(v)); });
  }
  const docPend = [];
  const onDoc = (t, f) => { const d = N.document; if (d) d.addEventListener(t, f); else docPend.push([t, f]); };
  G.__docFlush = () => { const d = N.document; if (!d) return; while (docPend.length) { const [t, f] = docPend.shift(); d.addEventListener(t, f); } };
  G.__onDoc = onDoc;
  onDoc('click', (e) => {
    const b = e.target && e.target.closest ? e.target.closest('button[popovertarget],input[popovertarget]') : null;
    if (b && !b.disabled) {
      const t = b.popoverTargetElement;
      if (t && t.hasAttribute('popover')) { const a = b.popoverTargetAction; if (a === 'show') show(t); else if (a === 'hide') hide(t); else if (isOpen(t)) hide(t); else show(t); return; }
    }
    if (openAuto.length) { for (let i = openAuto.length - 1; i >= 0; i--) { const o = openAuto[i]; if (e.target && o.contains(e.target)) break; hide(o); } }
  });
  onDoc('keydown', (e) => { if (e.key === 'Escape' && openAuto.length) hide(openAuto[openAuto.length - 1]); });
}
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
  val(p, 'select', function () { const L = String(this.value).length; this.setSelectionRange(0, L); });
  val(p, 'setSelectionRange', function (a, b) { a = a >>> 0; b = b === undefined ? a : b >>> 0; this.selectionEnd = Math.max(a, b); this.selectionStart = Math.min(a, b); });
  val(p, 'setRangeText', function (t) { this.value = this.value + t; });
  def(p, 'selectionDirection', function () { return 'none'; });
}
val(T.input, 'stepUp', function (n) { this.value = String((+this.value || 0) + (n || 1) * (+this.step || 1)); });
val(T.input, 'stepDown', function (n) { this.value = String((+this.value || 0) - (n || 1) * (+this.step || 1)); });
val(T.input, 'showPicker', function () {});
def(T.input, 'files', function () { return this._files || []; }, function (v) { val(this, '_files', v); });
def(T.input, 'indeterminate', function () { return false; }, function () {});
{
  const kids = (el, tags) => { const o = []; for (let c = el.firstElementChild; c; c = c.nextElementSibling) if (tags.includes(c.localName)) o.push(c); return o; };
  const coll = a => { a.item = i => a[i] || null; a.namedItem = n => a.find(e => e.id === n || e.getAttribute('name') === n) || null; return a; };
  const tblRows = t => { const o = []; const th = kids(t, ['thead']), tf = kids(t, ['tfoot']); for (const s of th) o.push(...kids(s, ['tr'])); for (let c = t.firstElementChild; c; c = c.nextElementSibling) { if (c.localName === 'tr') o.push(c); else if (c.localName === 'tbody') o.push(...kids(c, ['tr'])); } for (const s of tf) o.push(...kids(s, ['tr'])); return o; };
  for (const tg of ['table', 'thead', 'tbody', 'tfoot', 'tr', 'td', 'th', 'caption']) if (!T[tg]) T[tg] = Object.getPrototypeOf(document.createElement(tg));
  def(T.table, 'rows', function () { return coll(tblRows(this)); });
  def(T.table, 'tBodies', function () { return coll(kids(this, ['tbody'])); });
  def(T.table, 'tHead', function () { return kids(this, ['thead'])[0] || null; }, function (v) { const o = this.tHead; if (o) o.remove(); if (v) this.insertBefore(v, this.firstElementChild); });
  def(T.table, 'tFoot', function () { return kids(this, ['tfoot'])[0] || null; }, function (v) { const o = this.tFoot; if (o) o.remove(); if (v) this.appendChild(v); });
  def(T.table, 'caption', function () { return kids(this, ['caption'])[0] || null; });
  val(T.table, 'createTHead', function () { let h = this.tHead; if (!h) { h = document.createElement('thead'); this.insertBefore(h, this.firstElementChild); } return h; });
  val(T.table, 'createTFoot', function () { let h = this.tFoot; if (!h) { h = document.createElement('tfoot'); this.appendChild(h); } return h; });
  val(T.table, 'createTBody', function () { const b = document.createElement('tbody'); const bs = this.tBodies; const last = bs[bs.length - 1]; this.insertBefore(b, last ? last.nextSibling : null); return b; });
  val(T.table, 'createCaption', function () { let c = this.caption; if (!c) { c = document.createElement('caption'); this.insertBefore(c, this.firstChild); } return c; });
  val(T.table, 'deleteTHead', function () { const h = this.tHead; if (h) h.remove(); });
  val(T.table, 'deleteTFoot', function () { const h = this.tFoot; if (h) h.remove(); });
  val(T.table, 'deleteCaption', function () { const h = this.caption; if (h) h.remove(); });
  const idxErr = () => new DOMException('The index provided is outside the range', 'IndexSizeError');
  val(T.table, 'insertRow', function (i = -1) { const rows = tblRows(this); if (i < -1 || i > rows.length) throw idxErr(); const tr = document.createElement('tr'); if (i === -1 || i === rows.length) { const ref = rows[rows.length - 1]; if (ref) ref.parentNode.insertBefore(tr, ref.nextSibling); else { let b = this.tBodies[this.tBodies.length - 1]; if (!b) b = this.appendChild(document.createElement('tbody')); b.appendChild(tr); } } else rows[i].parentNode.insertBefore(tr, rows[i]); return tr; });
  val(T.table, 'deleteRow', function (i) { const rows = tblRows(this); if (i === -1) i = rows.length - 1; if (i < 0 || i >= rows.length) { if (i === -1 || rows.length === 0 && i === 0) return; throw idxErr(); } rows[i].remove(); });
  for (const sec of ['thead', 'tbody', 'tfoot']) {
    def(T[sec], 'rows', function () { return coll(kids(this, ['tr'])); });
    val(T[sec], 'insertRow', function (i = -1) { const r = kids(this, ['tr']); if (i < -1 || i > r.length) throw idxErr(); const tr = document.createElement('tr'); this.insertBefore(tr, i === -1 || i === r.length ? null : r[i]); return tr; });
    val(T[sec], 'deleteRow', function (i) { const r = kids(this, ['tr']); if (i === -1) i = r.length - 1; if (i < 0 || i >= r.length) { if (i === -1) return; throw idxErr(); } r[i].remove(); });
  }
  def(T.tr, 'cells', function () { return coll(kids(this, ['td', 'th'])); });
  def(T.tr, 'rowIndex', function () { let t = this.parentNode; if (t && t.localName !== 'table') t = t.parentNode; if (!t || t.localName !== 'table') return -1; return tblRows(t).indexOf(this); });
  def(T.tr, 'sectionRowIndex', function () { const p = this.parentNode; return p ? kids(p, ['tr']).indexOf(this) : -1; });
  val(T.tr, 'insertCell', function (i = -1) { const c = kids(this, ['td', 'th']); if (i < -1 || i > c.length) throw idxErr(); const td = document.createElement('td'); this.insertBefore(td, i === -1 || i === c.length ? null : c[i]); return td; });
  val(T.tr, 'deleteCell', function (i) { const c = kids(this, ['td', 'th']); if (i === -1) i = c.length - 1; if (i < 0 || i >= c.length) { if (i === -1) return; throw idxErr(); } c[i].remove(); });
  for (const cl of ['td', 'th']) {
    def(T[cl], 'cellIndex', function () { const p = this.parentNode; return p && p.localName === 'tr' ? kids(p, ['td', 'th']).indexOf(this) : -1; });
    def(T[cl], 'colSpan', function () { return Math.max(1, parseInt(this.getAttribute('colspan')) || 1); }, function (v) { this.setAttribute('colspan', String(v)); });
    def(T[cl], 'rowSpan', function () { const v = parseInt(this.getAttribute('rowspan')); return isNaN(v) ? 1 : v; }, function (v) { this.setAttribute('rowspan', String(v)); });
  }
}
val(T.select, 'item', function (i) { return this.options[i] || null; });
val(T.select, 'namedItem', function (n) { return this.options.find(o => o.id === n || o.getAttribute('name') === n) || null; });
val(T.select, 'add', function (o, b) { if (typeof b === 'number') b = this.options[b]; this.insertBefore(o, b || null); });
val(T.select, 'remove', function (i) { if (i === undefined) { this.__remove(); return; } const o = this.options[i]; if (o) o.__remove(); });
def(T.select, 'selectedOptions', function () { if (this.hasAttribute('multiple')) return Array.prototype.filter.call(this.options, (o) => o.selected); const o = this.options[this.selectedIndex]; return o ? [o] : []; });
def(T.label, 'control', function () { const f = this.htmlFor; return f ? document.getElementById(f) : this.querySelector('input,select,textarea,button'); });
def(T.option, 'form', function () { return this.closest('form'); });
for (const p of [T.img]) { val(p, 'decode', function () { return Promise.resolve(); }); def(p, 'x', function () { return this.getBoundingClientRect().left; }); def(p, 'y', function () { return this.getBoundingClientRect().top; }); }
val(T.dialog, 'show', function () { if (this.hasAttribute('open')) return; this.setAttribute('open', ''); });
val(T.dialog, 'showModal', function () { if (this.hasAttribute('open')) { if (this.hasAttribute('nvx-modal')) return; throw new DOMException("Failed to execute 'showModal' on 'HTMLDialogElement': The dialog is already open as a non-modal dialog, and therefore cannot be opened as a modal dialog.", 'InvalidStateError'); } if (!this.isConnected) throw new DOMException("Failed to execute 'showModal' on 'HTMLDialogElement': The element is not in a Document.", 'InvalidStateError'); this.setAttribute('nvx-modal', ''); this.setAttribute('open', ''); const f = this.querySelector('[autofocus]') || this.querySelector('button,input,select,textarea,a[href],[tabindex]'); if (f && f.focus) f.focus(); });
val(T.dialog, 'close', function (r) { if (!this.hasAttribute('open')) return; this.removeAttribute('open'); this.removeAttribute('nvx-modal'); if (r !== undefined) this.returnValue = String(r); setTimeout(() => this.dispatchEvent(new Event('close')), 0); });
val(T.dialog, 'requestClose', function (r) { const ev = new Event('cancel', { cancelable: true }); this.dispatchEvent(ev); if (!ev.defaultPrevented) this.close(r); });
G.__onDoc('keydown', (e) => { if (e.key !== 'Escape') return; const ds = document.querySelectorAll('dialog[nvx-modal]'); const d = ds[ds.length - 1]; if (d) { const ev = new Event('cancel', { cancelable: true }); d.dispatchEvent(ev); if (!ev.defaultPrevented) d.close(); } });
{
  const mkList = (a) => { const l = a.slice(); l.item = (i) => l[i] || null; return l; };
  class DataTransfer { constructor() { this._d = new Map(); this.dropEffect = 'none'; this.effectAllowed = 'all'; this.files = mkList([]); this.items = mkList([]); } setData(t, v) { t = String(t).toLowerCase(); if (t === 'text') t = 'text/plain'; this._d.set(t, String(v)); } getData(t) { t = String(t).toLowerCase(); if (t === 'text') t = 'text/plain'; const v = this._d.get(t); return v === undefined ? '' : v; } clearData(t) { if (t === undefined) this._d.clear(); else this._d.delete(String(t).toLowerCase()); } get types() { const k = [...this._d.keys()]; if (this.files.length) k.push('Files'); return k; } setDragImage() {} }
  class DragEvent extends MouseEvent { constructor(t, i) { super(t, i); this.dataTransfer = (i && i.dataTransfer) || null; } }
  G.DataTransfer = DataTransfer; G.DragEvent = DragEvent;
  let src = null, sx = 0, sy = 0, dt = null, active = false, over = null, accepted = false;
  const fire = (el, type, e, cancelable) => { const ev = new DragEvent(type, { bubbles: true, cancelable, clientX: e.clientX, clientY: e.clientY, dataTransfer: dt }); el.dispatchEvent(ev); return ev.defaultPrevented; };
  let tsrc = null;
  const isField = (el) => el && ((el.tagName === 'INPUT' && !/^(checkbox|radio|range|color|file|submit|button|reset|image|hidden)$/i.test(el.type || '')) || el.tagName === 'TEXTAREA');
  const editHost = (el) => { for (let x = el; x && x.nodeType === 1; x = x.parentNode) { const c = x.getAttribute && x.getAttribute('contenteditable'); if (c === '' || c === 'true' || c === 'plaintext-only') return x; if (c === 'false') return null; } return null; };
  G.__tdragArm = function (f) { const a = f.selectionStart, b = f.selectionEnd; if (a === b) return; tsrc = { field: f, a, b, text: String(f.value).slice(a, b) }; src = f; };
  G.__tdragArmPage = function (r) { const t = r.toString(); if (!t) return; let el = r.commonAncestorContainer; if (el && el.nodeType !== 1) el = el.parentNode; tsrc = { field: null, range: r, text: t }; src = el; };
  const fireInput = (el, it, data) => { try { el.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: it, data: data == null ? null : data })); } catch (e) { el.dispatchEvent(new Event('input', { bubbles: true })); } };
  const dropText = (el, e) => {
    const text = tsrc.text;
    if (isField(el)) {
      if (el.readOnly || el.disabled) return;
      let v = String(el.value), pos = byteToUtf16(v, N.caretAt(el, e.clientX));
      if (el.tagName === 'TEXTAREA') pos = v.length;
      let t = el.tagName === 'INPUT' ? text.replace(/[\r\n]+/g, ' ') : text;
      if (tsrc.field === el) { if (pos >= tsrc.a && pos <= tsrc.b) return; v = v.slice(0, tsrc.a) + v.slice(tsrc.b); if (pos > tsrc.b) pos -= tsrc.b - tsrc.a; }
      else if (tsrc.field && !tsrc.field.readOnly) { const sv = String(tsrc.field.value); tsrc.field.value = sv.slice(0, tsrc.a) + sv.slice(tsrc.b); fireInput(tsrc.field, 'deleteByDrag'); }
      el.value = v.slice(0, pos) + t + v.slice(pos);
      if (el.focus) el.focus();
      if (el.setSelectionRange) el.setSelectionRange(pos, pos + t.length);
      fireInput(el, 'insertFromDrop', t);
      return;
    }
    const h = editHost(el);
    if (h) { h.focus && h.focus(); if (tsrc.field) { const sv = String(tsrc.field.value); tsrc.field.value = sv.slice(0, tsrc.a) + sv.slice(tsrc.b); fireInput(tsrc.field, 'deleteByDrag'); } const r = document.createRange(); r.selectNodeContents(h); r.collapse(false); const s2 = getSelection(); s2.removeAllRanges(); s2.addRange(r); document.execCommand('insertText', false, text); }
  };
  G.__onDoc('mousedown', (e) => { if (e.button !== 0) return; tsrc = null; src = e.target && e.target.closest ? e.target.closest('[draggable="true"]') : null; sx = e.clientX; sy = e.clientY; active = false; });
  G.__onDoc('mousemove', (e) => {
    if (!src || !(e.buttons & 1)) return;
    if (!active) { if (Math.abs(e.clientX - sx) + Math.abs(e.clientY - sy) < 5) return; dt = new DataTransfer(); if (tsrc) { dt.setData('text/plain', tsrc.text); dt.effectAllowed = tsrc.field ? 'copyMove' : 'copy'; } if (fire(src, 'dragstart', e, true)) { src = null; tsrc = null; return; } active = true; }
    fire(src, 'drag', e, true);
    const el = document.elementFromPoint(e.clientX, e.clientY);
    if (el !== over) { if (over) fire(over, 'dragleave', e, false); over = el; if (el) fire(el, 'dragenter', e, true); }
    accepted = el ? fire(el, 'dragover', e, true) : false;
  });
  G.__onDoc('mouseup', (e) => {
    if (src && active) {
      const el = document.elementFromPoint(e.clientX, e.clientY);
      const ed = tsrc && el && (isField(el) || editHost(el));
      if (el && (accepted || ed)) { if (dt.dropEffect === 'none') dt.dropEffect = tsrc && !tsrc.field ? 'copy' : 'move'; const pv = fire(el, 'drop', e, true); if (!pv && ed) dropText(el, e); } else if (dt) dt.dropEffect = 'none';
      if (over && over !== el) fire(over, 'dragleave', e, false);
      fire(src, 'dragend', e, false);
    }
    src = null; active = false; over = null; accepted = false; tsrc = null;
  });
  const mimeOf = (n) => { const x = (n.split('.').pop() || '').toLowerCase(); return ({ txt: 'text/plain', md: 'text/markdown', html: 'text/html', htm: 'text/html', css: 'text/css', js: 'text/javascript', json: 'application/json', csv: 'text/csv', png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', webp: 'image/webp', avif: 'image/avif', jxl: 'image/jxl', svg: 'image/svg+xml', bmp: 'image/bmp', pdf: 'application/pdf', zip: 'application/zip', mp3: 'audio/mpeg', wav: 'audio/wav', mp4: 'video/mp4', py: 'text/x-python', c: 'text/x-c' })[x] || 'application/octet-stream'; };
  G.__fileDrop = function (n, xy) {
    const d = N.dropData(); const name = d[0], bytes = d[1];
    const f = new File([bytes], name, { type: mimeOf(name) });
    const x = (xy >>> 16) & 0xFFFF, y = xy & 0xFFFF;
    dt = new DataTransfer(); dt.files = mkList([f]); dt.items = mkList([{ kind: 'file', type: f.type, getAsFile: () => f, getAsString: () => {}, webkitGetAsEntry: () => null }]); dt.effectAllowed = 'all';
    const e = { clientX: x, clientY: y };
    let el = n && n.nodeType === 1 ? n : (n && n.parentNode) || document.body;
    fire(el, 'dragenter', e, true);
    const ok = fire(el, 'dragover', e, true);
    let fi = el.closest ? el.closest('input[type=file]') : null;
    if (!fi && el.closest) { const lb = el.closest('label'); if (lb) fi = lb.control || lb.querySelector('input[type=file]'); }
    if (ok) { dt.dropEffect = 'copy'; fire(el, 'drop', e, true); }
    else if (fi && !fi.disabled) { val(fi, '_files', mkList([f])); fi.dispatchEvent(new Event('input', { bubbles: true })); fi.dispatchEvent(new Event('change', { bubbles: true })); }
    else fire(el, 'dragleave', e, false);
    dt = null;
  };
}
const XP = new Map();
let XPn = 0;
const xpTr = (tr) => (Array.isArray(tr) ? tr : (tr && Array.isArray(tr.transfer) ? tr.transfer : [])).filter((x) => x instanceof G.MessagePort);
const xpOut = (ports, route) => ports.map((pt) => { const id = (N.isFrame() ? 'c' : 'p') + (++XPn) + '_' + Math.floor(Math.random() * 1e9).toString(36); pt._rel = { id, route }; XP.set(id, pt); return id; });
const xpIn = (ids, route) => (Array.isArray(ids) ? ids : []).map((id) => { const R = new G.MessagePort(); R._rp = { id, route }; XP.set(id, R); return R; });
const xpSend = (route, id, d, tr) => { let payload; try { payload = JSON.stringify({ k: 'p', i: id, d: d === undefined ? null : d, p: xpOut(xpTr(tr), route) }); } catch (e) { throw new DOMException('The object could not be cloned', 'DataCloneError'); } route._send(payload); };
const xpRecv = (o, route) => {
  const pt = XP.get(o.i);
  if (!pt || pt._dead) return;
  const ports = xpIn(o.p, route);
  const ev = new MessageEvent('message', { data: o.d, ports });
  const dst = pt._rp ? pt : pt._o;
  if (!dst || dst._dead) return;
  if (dst._on) dst._deliver(ev); else dst._q.push(ev);
};
class FrameWindowProxy {
  constructor(el) { val(this, '_el', el); }
  _send(payload) {
    const el = this._el;
    if (!el) { N.framePost(payload); FR.up++; return; }
    FR.sent++;
    const si = frSlotOf(el);
    if (si >= 0 && FR.S[si].loaded && !FR.S[si].failed) { N.frameSlot(si); N.framePost(payload); FR.S[si].act = Date.now(); FR.deliv++; return; }
    if (!el.__fq) val(el, '__fq', []);
    if (el.__fq.length < 64) el.__fq.push(payload); else FR.dropped++;
    if (FR.want.indexOf(el) < 0) FR.want.push(el);
  }
  postMessage(data, targetOrigin, transfer) {
    let to = targetOrigin, tr = transfer;
    if (to && typeof to === 'object') { tr = to.transfer; to = to.targetOrigin; }
    if (to !== undefined && to !== '*' && to !== '/') { try { new URL(String(to)); } catch (e) { throw new DOMException("Failed to execute 'postMessage' on 'Window': Invalid target origin '" + to + "' in a call to 'postMessage'.", 'SyntaxError'); } }
    let payload;
    const ports = xpTr(tr);
    try { payload = JSON.stringify({ d: data === undefined ? null : data, o: N.origin(), t: to === undefined ? '*' : String(to), p: ports.length ? xpOut(ports, this) : undefined }); } catch (e) { throw new DOMException('The object could not be cloned', 'DataCloneError'); }
    this._send(payload);
  }
  get closed() { return false; }
  get window() { return this; }
  get self() { return this; }
  get frames() { return this; }
  get length() { return 0; }
  focus() {}
  blur() {}
  close() {}
}
let FRAME_PARENT = null, FRAME_CHILD = null;
const PSLOT = 4;
const PU = { win: null, owner: -1, name: '', seen: 0 };
const puActive = () => { const t = G.__actT; return !!t && t > 0 && Date.now() - t < 5000; };
const puConsume = () => { if (G.__actT) G.__actT = -1; };
const puFeat = (f) => { const o = {}; for (const kv of String(f || '').split(/[,\s]+/)) { if (!kv) continue; const i = kv.indexOf('='); const k = (i < 0 ? kv : kv.slice(0, i)).toLowerCase(); const v = i < 0 ? 'yes' : kv.slice(i + 1).toLowerCase(); o[k] = v; } return o; };
class PopupProxy {
  constructor(url, name, viaFrame) { val(this, '_url', url); val(this, '_name', name); val(this, '_vf', viaFrame); val(this, '_gone', false); }
  _send(payload) { if (this._gone) return; if (this._vf) { N.framePost(JSON.stringify({ k: 'pm', m: payload })); return; } N.frameSlot(PSLOT); N.framePost(payload); }
  postMessage(data, targetOrigin, transfer) {
    let to = targetOrigin, tr = transfer;
    if (to && typeof to === 'object') { tr = to.transfer; to = to.targetOrigin; }
    if (to !== undefined && to !== '*' && to !== '/') { try { new URL(String(to)); } catch (e) { throw new DOMException("Failed to execute 'postMessage' on 'Window': Invalid target origin '" + to + "' in a call to 'postMessage'.", 'SyntaxError'); } }
    let payload;
    const ports = xpTr(tr);
    try { payload = JSON.stringify({ d: data === undefined ? null : data, o: N.origin(), t: to === undefined ? '*' : String(to), p: ports.length ? xpOut(ports, this) : undefined }); } catch (e) { throw new DOMException('The object could not be cloned.', 'DataCloneError'); }
    this._send(payload);
  }
  get closed() { if (this._gone) return true; if (!this._vf && N.popupInfo() === 0) { this._gone = true; if (PU.win === this) PU.win = null; } return this._gone; }
  get opener() { return G; }
  get name() { return this._name; }
  get window() { return this; }
  get self() { return this; }
  get frames() { return this; }
  get length() { return 0; }
  get parent() { return this; }
  get top() { return this; }
  get location() { const me = this; return { get href() { return me._url; }, set href(v) { me.location.assign(v); }, assign(v) { const u = N.resolve(String(v)); me._url = u; if (me._vf) N.framePost(JSON.stringify({ k: 'po', u, n: me._name, w: 0, h: 0, r: 1 })); else N.popupOpen(u, 0, 0); }, replace(v) { this.assign(v); }, reload() {}, toString() { return me._url; } }; }
  set location(v) { this.location.assign(v); }
  get document() { throw new DOMException("Blocked a frame from accessing a cross-origin frame.", 'SecurityError'); }
  focus() {}
  blur() {}
  close() { if (this._gone) return; this._gone = true; if (this._vf) N.framePost(JSON.stringify({ k: 'px' })); else N.popupClose(); if (PU.win === this) PU.win = null; }
}
const puOpenTop = (url, name, w, h, owner) => { const reuse = PU.win && !PU.win.closed && name && PU.name === name && PU.owner === owner; if (!N.popupOpen(url, w, h)) return null; PU.owner = owner; PU.name = name; PU.seen = 1; if (!reuse) { if (PU.win) PU.win._gone = true; PU.win = owner < 0 ? new PopupProxy(url, name, false) : null; } else PU.win._url = url; return PU.win; };
const puTick = () => { if (!PU.seen) return; if (N.popupInfo() === 0) { PU.seen = 0; if (PU.win) { PU.win._gone = true; PU.win = null; } if (PU.owner >= 0) { N.frameSlot(PU.owner); N.framePost(JSON.stringify({ k: 'pc' })); } PU.owner = -1; } };
let FRAME_POPUP = null;
const frMk = () => ({ run: null, key: '', loaded: false, t0: 0, act: 0, failed: false });
const FR = { S: [frMk(), frMk(), frMk(), frMk()], want: [], sent: 0, deliv: 0, up: 0, recv: 0, dropped: 0, switches: 0 };
const frSlotOf = (el) => { for (let i = 0; i < FR.S.length; i++) if (FR.S[i].run === el) return i; return -1; };
const frameBlank = (el) => { const u = el.getAttribute('src'); return !el.hasAttribute('srcdoc') && (!u || /^about:blank/i.test(u)); };
def(T.iframe, 'contentWindow', function () { if (!this.isConnected) return null; if (frameBlank(this)) return cache(this, '__bw', () => { const d = this.contentDocument; const w = new FrameWindowProxy(this); val(w, 'document', d); val(w, 'location', { href: 'about:blank', origin: location.origin, toString() { return 'about:blank'; }, replace() {}, assign() {}, reload() {} }); val(w, 'parent', G); val(w, 'top', G.top || G); val(w, 'frameElement', this); for (const k of ['setTimeout', 'clearTimeout', 'setInterval', 'clearInterval', 'requestAnimationFrame', 'cancelAnimationFrame', 'getComputedStyle', 'Object', 'Array', 'JSON', 'Math', 'Date', 'Promise', 'Error', 'Function', 'String', 'Number', 'Boolean', 'RegExp', 'Symbol', 'Map', 'Set', 'WeakMap', 'Uint8Array', 'navigator', 'console', 'crypto', 'performance', 'Event', 'CustomEvent', 'XMLHttpRequest', 'fetch', 'TextEncoder', 'TextDecoder', 'URL', 'Blob', 'Node', 'Element', 'HTMLElement', 'Document']) { try { val(w, k, G[k]); } catch (e) {} } for (const k of Object.getOwnPropertyNames(G)) { if (k in w || k === 'document' || k === 'location' || k === 'parent' || k === 'top' || k === 'frameElement' || k === 'window' || k === 'self') continue; const dd = Object.getOwnPropertyDescriptor(G, k); if (!dd) continue; try { if ('value' in dd) val(w, k, dd.value); else if (dd.get) Object.defineProperty(w, k, { get: () => G[k], configurable: true }); } catch (e) {} } const ls = new Map(); val(w, 'addEventListener', (t, fn) => { if (!ls.has(t)) ls.set(t, []); ls.get(t).push(fn); }); val(w, 'removeEventListener', (t, fn) => { const a = ls.get(t); if (a) { const i = a.indexOf(fn); if (i >= 0) a.splice(i, 1); } }); val(w, 'postMessage', (data, to, trn) => { const d = (() => { try { return structuredClone(data); } catch (e) { return data; } })(); const bp = xpTr(to && typeof to === 'object' ? to.transfer : trn); setTimeout(() => { const ev = new MessageEvent('message', { data: d, origin: location.origin, source: G, ports: bp }); for (const fn of (ls.get('message') || []).slice()) { try { fn.call(w, ev); } catch (e) { reportErr(e); } } if (typeof w.onmessage === 'function') { try { w.onmessage(ev); } catch (e) { reportErr(e); } } }, 0); }); val(w, 'eval', (c) => (0, eval)(c)); return w; }); return cache(this, '__cw', () => { const p = new FrameWindowProxy(this); if (!FRAME_CHILD) FRAME_CHILD = p; return p; }); });
def(T.iframe, 'contentDocument', function () { if (!this.isConnected || !frameBlank(this)) return null; return cache(this, '__bd', () => { const d = N.parseDoc('<html><head></head><body></body></html>'); try { val(d, 'defaultView', null); } catch (e) {} const buf = []; val(d, 'open', function () { buf.length = 0; return d; }); val(d, 'write', function (...a) { buf.push(a.join('')); }); val(d, 'writeln', function (...a) { buf.push(a.join('') + '\n'); }); val(d, 'close', function () { const h = buf.join(''); buf.length = 0; if (h) { const nd = N.parseDoc(h); const db = d.body, nb = nd.body; if (db && nb) { db.innerHTML = nb.innerHTML; } const dh = d.head, nh = nd.head; if (dh && nh) dh.innerHTML = nh.innerHTML; } }); return d; }); });
for (const k of ['protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin', 'username', 'password']) {
  def(T.a, k, function () { try { return new URL(this.href)[k]; } catch (e) { return ''; } }, function (v) { try { const u = new URL(this.href); u[k] = v; this.href = u.href; } catch (e) {} });
  def(T.area, k, function () { try { return new URL(this.href)[k]; } catch (e) { return ''; } });
}
val(T.a, 'toString', function () { return this.href; });
def(T.link, 'sheet', function () { return { cssRules: [], insertRule() { return 0; }, deleteRule() {} }; });
{
  const has = (p, k) => !!findDesc(p, k);
  const rS = (p, k, at) => { if (!p || has(p, k)) return; at = at || k.toLowerCase(); def(p, k, function () { const v = this.getAttribute(at); return v === null ? '' : v; }, function (v) { this.setAttribute(at, String(v)); }); };
  const rB = (p, k, at) => { if (!p || has(p, k)) return; at = at || k.toLowerCase(); def(p, k, function () { return this.hasAttribute(at); }, function (v) { if (v) this.setAttribute(at, ''); else this.removeAttribute(at); }); };
  const rU = (p, k, at) => { if (!p || has(p, k)) return; at = at || k.toLowerCase(); def(p, k, function () { const v = this.getAttribute(at); if (v === null) return ''; try { return N.resolve(v); } catch (e) { return v; } }, function (v) { this.setAttribute(at, String(v)); }); };
  const rI = (p, k, d, at, min) => { if (!p || has(p, k)) return; at = at || k.toLowerCase(); def(p, k, function () { const v = parseInt(this.getAttribute(at), 10); return isNaN(v) || (min !== undefined && v < min) ? d : v; }, function (v) { this.setAttribute(at, String(v | 0)); }); };
  const rE = (p, k, vals, miss, inv, at) => { if (!p || has(p, k)) return; at = at || k.toLowerCase(); def(p, k, function () { const v = this.getAttribute(at); if (v === null) return miss; const l = v.toLowerCase(); return vals.indexOf(l) >= 0 ? l : inv; }, function (v) { this.setAttribute(at, String(v)); }); };
  const SUP = {
    link: ['alternate', 'dns-prefetch', 'expect', 'icon', 'manifest', 'modulepreload', 'next', 'preconnect', 'prefetch', 'preload', 'search', 'stylesheet', 'compression-dictionary'],
    a: ['noreferrer', 'noopener', 'opener'],
    sandbox: ['allow-downloads', 'allow-forms', 'allow-modals', 'allow-orientation-lock', 'allow-pointer-lock', 'allow-popups', 'allow-popups-to-escape-sandbox', 'allow-presentation', 'allow-same-origin', 'allow-scripts', 'allow-storage-access-by-user-activation', 'allow-top-navigation', 'allow-top-navigation-by-user-activation', 'allow-top-navigation-to-custom-protocols'],
    blocking: ['render']
  };
  val(DOMTokenList.prototype, 'supports', function supports(t) {
    let l = null; const tg = this._e && this._e.localName;
    if (this._a === 'rel') l = tg === 'link' ? SUP.link : (tg === 'a' || tg === 'area' || tg === 'form') ? SUP.a : null;
    else if (this._a === 'sandbox') l = SUP.sandbox;
    else if (this._a === 'blocking') l = SUP.blocking;
    if (!l) throw new TypeError("Failed to execute 'supports' on 'DOMTokenList': DOMTokenList has no supported tokens.");
    return l.indexOf(String(t).toLowerCase()) >= 0;
  });
  if (T.iframe) {
    if (!has(T.iframe, 'sandbox')) def(T.iframe, 'sandbox', function () { return cache(this, '__sb', () => new DOMTokenList(this, 'sandbox')); }, function (v) { this.setAttribute('sandbox', String(v)); });
    rS(T.iframe, 'srcdoc'); rS(T.iframe, 'allow'); rB(T.iframe, 'allowFullscreen'); rB(T.iframe, 'credentialless'); rB(T.iframe, 'allowPaymentRequest');
    rS(T.iframe, 'name'); rS(T.iframe, 'width'); rS(T.iframe, 'height'); rS(T.iframe, 'csp'); rS(T.iframe, 'frameBorder'); rS(T.iframe, 'scrolling'); rU(T.iframe, 'longDesc');
    rE(T.iframe, 'loading', ['lazy', 'eager'], 'eager', 'eager');
    rE(T.iframe, 'referrerPolicy', ['no-referrer', 'no-referrer-when-downgrade', 'same-origin', 'origin', 'strict-origin', 'origin-when-cross-origin', 'strict-origin-when-cross-origin', 'unsafe-url'], '', '', 'referrerpolicy');
    if (!has(T.iframe, 'featurePolicy')) def(T.iframe, 'featurePolicy', function () { const el = this; return { allowsFeature() { return true; }, features() { return []; }, allowedFeatures() { return []; }, getAllowlistForFeature() { return ['*']; } }; });
  }
  const fp = (p) => rE(p, 'fetchPriority', ['high', 'low', 'auto'], 'auto', 'auto', 'fetchpriority');
  const bl = (p) => { if (p && !has(p, 'blocking')) def(p, 'blocking', function () { return cache(this, '__bk', () => new DOMTokenList(this, 'blocking')); }, function (v) { this.setAttribute('blocking', String(v)); }); };
  fp(T.script); fp(T.link); fp(T.img); bl(T.script); bl(T.link); bl(T.style);
  rS(T.script, 'event'); rS(T.script, 'htmlFor', 'for');
  rE(T.link, 'as', ['fetch', 'audio', 'audioworklet', 'document', 'embed', 'font', 'image', 'manifest', 'object', 'paintworklet', 'report', 'script', 'serviceworker', 'sharedworker', 'style', 'track', 'video', 'webidentity', 'worker', 'xslt', 'json'], '', '');
  rS(T.link, 'imageSrcset', 'imagesrcset'); rS(T.link, 'imageSizes', 'imagesizes');
  rE(T.img, 'decoding', ['sync', 'async', 'auto'], 'auto', 'auto'); rS(T.img, 'useMap', 'usemap'); rB(T.img, 'isMap', 'ismap');
  rI(T.col, 'span', 1, 'span', 1);
  rS(T.param, 'name'); rS(T.param, 'value'); rS(T.param, 'type'); rS(T.param, 'valueType', 'valuetype');
  rE(T.track, 'kind', ['subtitles', 'captions', 'descriptions', 'chapters', 'metadata'], 'subtitles', 'metadata'); rU(T.track, 'src'); rS(T.track, 'srclang'); rS(T.track, 'label'); rB(T.track, 'default');
  if (T.track && !has(T.track, 'readyState')) { def(T.track, 'readyState', function () { return 0; }); for (const [k, v] of [['NONE', 0], ['LOADING', 1], ['LOADED', 2], ['ERROR', 3]]) { T.track[k] = v; if (G.HTMLTrackElement) G.HTMLTrackElement[k] = v; } def(T.track, 'track', function () { let t = this.__tt; if (!t) { const el = this; t = { get kind() { return el.kind; }, get label() { return el.label; }, get language() { return el.srclang; }, id: '', mode: 'disabled', cues: null, activeCues: null, addCue() {}, removeCue() {}, addEventListener() {}, removeEventListener() {}, oncuechange: null }; val(this, '__tt', t); } return t; }); }
  rS(T.font, 'color'); rS(T.font, 'face'); rS(T.font, 'size');
  rS(T.marquee, 'behavior'); rS(T.marquee, 'direction'); rS(T.marquee, 'bgColor', 'bgcolor'); rI(T.marquee, 'scrollAmount', 6, 'scrollamount', 0); rI(T.marquee, 'scrollDelay', 85, 'scrolldelay', 0); rI(T.marquee, 'loop', -1, 'loop'); rB(T.marquee, 'trueSpeed', 'truespeed');
  if (T.marquee) { if (!has(T.marquee, 'start')) val(T.marquee, 'start', function () {}); if (!has(T.marquee, 'stop')) val(T.marquee, 'stop', function () {}); }
  rS(T.frameset, 'cols'); rS(T.frameset, 'rows'); rS(T.frame, 'name'); rU(T.frame, 'src'); rS(T.frame, 'scrolling'); rB(T.frame, 'noResize', 'noresize'); rB(T.dir, 'compact');
  if (T.slot) { if (!has(T.slot, 'assignedNodes')) val(T.slot, 'assignedNodes', function (o) { const host = (() => { let r = this; while (r.parentNode) r = r.parentNode; return r.host || null; })(); if (!host) return []; const nm = this.getAttribute('name') || ''; const out = []; for (const c of host.childNodes) { if (c.nodeType === 1 ? (c.getAttribute('slot') || '') === nm : (!nm && c.nodeType === 3)) out.push(c); } if (!out.length && o && o.flatten) return Array.from(this.childNodes); return out; }); if (!has(T.slot, 'assignedElements')) val(T.slot, 'assignedElements', function (o) { return this.assignedNodes(o).filter((n) => n.nodeType === 1); }); if (!has(T.slot, 'assign')) val(T.slot, 'assign', function () {}); }
  if (T.a) rS(T.a, 'ping'); if (T.area) rS(T.area, 'ping');
  const ime = (p) => { rE(p, 'inputMode', ['none', 'text', 'tel', 'url', 'email', 'numeric', 'decimal', 'search'], '', '', 'inputmode'); rE(p, 'enterKeyHint', ['enter', 'done', 'go', 'next', 'previous', 'search', 'send'], '', '', 'enterkeyhint'); rS(p, 'autocapitalize'); rS(p, 'writingSuggestions', 'writingsuggestions'); };
  ime(P.HTMLElement);
  if (T.input) { rI(T.input, 'minLength', -1, 'minlength', 0); rS(T.input, 'dirName', 'dirname'); rS(T.input, 'capture');
    if (!has(T.input, 'list')) def(T.input, 'list', function () { const i = this.getAttribute('list'); return i ? document.getElementById(i) : null; });
    if (!has(T.input, 'valueAsDate')) def(T.input, 'valueAsDate', function () { if (!/^(date|month|week|time|datetime-local)$/.test(this.type)) return null; const d = new Date(this.value + (this.type === 'date' ? 'T00:00:00Z' : '')); return isNaN(d) ? null : d; }, function (v) { if (v === null) { this.value = ''; return; } if (!(v instanceof Date)) throw new TypeError("Failed to set the 'valueAsDate' property on 'HTMLInputElement': The provided value is not a Date."); if (this.type === 'date') this.value = v.toISOString().slice(0, 10); else if (this.type === 'month') this.value = v.toISOString().slice(0, 7); else if (this.type === 'time') this.value = v.toISOString().slice(11, 16); });
  }
  for (const t of ['input', 'button']) { const p = T[t]; if (!p) continue; rU(p, 'formAction', 'formaction'); rE(p, 'formMethod', ['get', 'post', 'dialog'], '', 'get', 'formmethod'); rB(p, 'formNoValidate', 'formnovalidate'); rS(p, 'formTarget', 'formtarget'); rS(p, 'formEnctype', 'formenctype'); }
  if (T.button) { rS(T.button, 'command'); if (!has(T.button, 'commandForElement')) def(T.button, 'commandForElement', function () { const i = this.getAttribute('commandfor'); return i ? document.getElementById(i) : null; }, function (v) { if (v && v.id) this.setAttribute('commandfor', v.id); else this.removeAttribute('commandfor'); }); }
  if (T.form) { if (!has(T.form, 'encoding')) def(T.form, 'encoding', function () { return this.enctype; }, function (v) { this.enctype = v; }); rS(T.form, 'acceptCharset', 'accept-charset'); }
  if (T.textarea) { rE(T.textarea, 'wrap', ['soft', 'hard', 'off'], '', '', 'wrap'); rI(T.textarea, 'minLength', -1, 'minlength', 0); rS(T.textarea, 'dirName', 'dirname'); if (!has(T.textarea, 'textLength')) def(T.textarea, 'textLength', function () { return String(this.value).length; }); }
  if (T.select) rI(T.select, 'size', 0, 'size', 0);
  rB(T.ol, 'reversed'); rI(T.ol, 'start', 1, 'start'); rS(T.ol, 'type');
  const rF = (p, k, d, at) => { if (!p) return; at = at || k.toLowerCase(); def(p, k, function () { const v = parseFloat(this.getAttribute(at)); return isFinite(v) ? v : (typeof d === 'function' ? d.call(this) : d); }, function (v) { v = Number(v); if (!isFinite(v)) throw new TypeError("The provided double value is non-finite."); this.setAttribute(at, String(v)); }); };
  if (T.meter) { rF(T.meter, 'min', 0); rF(T.meter, 'max', function () { return Math.max(1, this.min); }); const clampv = function (v) { const mn = this.min, mx = this.max; return v < mn ? mn : v > mx ? mx : v; };
    def(T.meter, 'value', function () { const v = parseFloat(this.getAttribute('value')); return clampv.call(this, isFinite(v) ? v : 0); }, function (v) { this.setAttribute('value', String(Number(v))); });
    rF(T.meter, 'low', function () { return this.min; }); rF(T.meter, 'high', function () { return this.max; }); rF(T.meter, 'optimum', function () { return (this.min + this.max) / 2; }); }
  if (T.progress) { def(T.progress, 'max', function () { const v = parseFloat(this.getAttribute('max')); return isFinite(v) && v > 0 ? v : 1; }, function (v) { v = Number(v); if (v > 0) this.setAttribute('max', String(v)); });
    def(T.progress, 'value', function () { const v = parseFloat(this.getAttribute('value')); if (!isFinite(v) || v < 0) return 0; return Math.min(v, this.max); }, function (v) { this.setAttribute('value', String(Number(v))); });
    def(T.progress, 'position', function () { return this.hasAttribute('value') ? this.value / this.max : -1; }); }
  if (T.output) { def(T.output, 'defaultValue', function () { return this.__dv !== undefined ? this.__dv : this.textContent; }, function (v) { this.__dv = String(v); }); def(T.output, 'value', function () { return this.textContent; }, function (v) { if (this.__dv === undefined) this.__dv = this.textContent; this.textContent = String(v); }); def(T.output, 'type', function () { return 'output'; }); }
  if (T.input) {
    const dParse = (t, v) => { let m;
      if (t === 'date' && (m = /^(\d{4,})-(\d\d)-(\d\d)$/.exec(v))) return Date.UTC(+m[1], +m[2] - 1, +m[3]);
      if (t === 'month' && (m = /^(\d{4,})-(\d\d)$/.exec(v))) return (+m[1] - 1970) * 12 + (+m[2] - 1);
      if (t === 'week' && (m = /^(\d{4,})-W(\d\d)$/.exec(v))) { const j4 = Date.UTC(+m[1], 0, 4); const d = new Date(j4).getUTCDay() || 7; return j4 - (d - 1) * 864e5 + (+m[2] - 1) * 7 * 864e5; }
      if (t === 'time' && (m = /^(\d\d):(\d\d)(?::(\d\d)(?:\.(\d{1,3}))?)?$/.exec(v))) return ((+m[1] * 60 + +m[2]) * 60 + (+(m[3] || 0))) * 1000 + (+((m[4] || '0') + '00').slice(0, 3));
      if (t === 'datetime-local' && (m = /^(\d{4,})-(\d\d)-(\d\d)[T ](\d\d):(\d\d)(?::(\d\d)(?:\.(\d{1,3}))?)?$/.exec(v))) return Date.UTC(+m[1], +m[2] - 1, +m[3], +m[4], +m[5], +(m[6] || 0), +((m[7] || '0') + '00').slice(0, 3));
      return NaN; };
    const pad = (n, w) => String(n).padStart(w || 2, '0');
    def(T.input, 'valueAsNumber', function () { const t = this.type, v = this.value;
      if (t === 'number' || t === 'range') { if (v === '' || !/^-?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/.test(v.trim())) return NaN; return parseFloat(v); }
      if (/^(date|month|week|time|datetime-local)$/.test(t)) return v === '' ? NaN : dParse(t, v);
      return NaN; }, function (n) { const t = this.type; n = Number(n);
      if (t === 'number' || t === 'range') { this.value = isNaN(n) ? '' : String(n); return; }
      if (!/^(date|month|week|time|datetime-local)$/.test(t)) throw new DOMException("Failed to set the 'valueAsNumber' property on 'HTMLInputElement': This input element does not support Number values.", 'InvalidStateError');
      if (isNaN(n)) { this.value = ''; return; }
      if (t === 'month') { const y = Math.floor(n / 12) + 1970, mo = ((n % 12) + 12) % 12 + 1; this.value = y + '-' + pad(mo); return; }
      const d = new Date(n);
      if (t === 'date') this.value = pad(d.getUTCFullYear(), 4) + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate());
      else if (t === 'time') this.value = pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + (d.getUTCSeconds() || d.getUTCMilliseconds() ? ':' + pad(d.getUTCSeconds()) : '') + (d.getUTCMilliseconds() ? '.' + pad(d.getUTCMilliseconds(), 3) : '');
      else if (t === 'datetime-local') this.value = pad(d.getUTCFullYear(), 4) + '-' + pad(d.getUTCMonth() + 1) + '-' + pad(d.getUTCDate()) + 'T' + pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + (d.getUTCSeconds() ? ':' + pad(d.getUTCSeconds()) : '');
      else this.value = ''; });
  }
  if (T.video) rB(T.video, 'disablePictureInPicture', 'disablepictureinpicture');
  if (G.HTMLScriptElement && !G.HTMLScriptElement.supports) val(G.HTMLScriptElement, 'supports', function supports(t) { t = String(t); return t === 'classic' || t === 'module' || t === 'importmap'; });
}
def(T.style, 'sheet', function () { const el = this; return { get cssRules() { return []; }, insertRule(r) { el.textContent += r; return 0; }, deleteRule() {}, disabled: false }; });
const cvFont = (f) => {
  const s = String(f).toLowerCase();
  const b = /\bbold(er)?\b|\b[6-9]00\b/.test(s), it = /\bitalic\b|\boblique\b/.test(s);
  const m = /(\d*\.?\d+)(px|pt|em|rem|%)/.exec(s);
  let z = m ? +m[1] : 10;
  if (m && m[2] === 'pt') z = z * 4 / 3; else if (m && (m[2] === 'em' || m[2] === 'rem')) z *= 16; else if (m && m[2] === '%') z = z * 16 / 100;
  const mono = /mono|courier|consol|menlo/.test(s), serif = !mono && /serif|times|georgia|garamond|cambria/.test(s) && !/sans-serif/.test(s);
  return [mono ? (b ? 8 : 7) : serif ? (b ? 5 : it ? 6 : 4) : b ? (it ? 3 : 1) : it ? 2 : 0, z];
};
class CanvasGradient {
  constructor(t, a) { this._t = t; this._a = a; this._s = []; }
  addColorStop(o, c) { o = +o; if (!(o >= 0 && o <= 1)) throw new DOMException('offset out of range', 'IndexSizeError'); const k = N.cvColor(String(c)); if (!k) throw new DOMException('bad color', 'SyntaxError'); this._s.push([o, k[0], k[1], k[2], k[3]]); this._s.sort((x, y) => x[0] - y[0]); }
  _arr(m) {
    const a = this._a, tp = (x, y) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]], sc = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
    let h;
    if (this._t === 1) { const p = tp(a[0], a[1]), q = tp(a[2], a[3]); h = [1, p[0], p[1], q[0], q[1], this._s.length]; }
    else { const p = tp(a[0], a[1]), q = tp(a[3], a[4]); h = [2, p[0], p[1], a[2] * sc, q[0], q[1], a[5] * sc, this._s.length]; }
    if (!this._s.length) return new Float32Array([0, 0, 0, 0, 0]);
    for (const s of this._s) h.push(s[0], s[1], s[2], s[3], s[4]);
    return new Float32Array(h);
  }
}
class CanvasPattern { constructor(src) { this._src = src; } setTransform() {} _arr() { return new Float32Array([0, 0.6, 0.6, 0.6, 1]); } }
G.CanvasGradient = CanvasGradient; G.CanvasPattern = CanvasPattern;
G.ImageData = class ImageData {
  constructor(a, b, c) {
    if (a instanceof Uint8ClampedArray) { this.data = a; this.width = b >>> 0; this.height = c !== undefined ? c >>> 0 : (a.length / 4 / this.width) >>> 0; }
    else { this.width = Math.max(0, a >>> 0); this.height = Math.max(0, b >>> 0); this.data = new Uint8ClampedArray(this.width * this.height * 4); }
    this.colorSpace = 'srgb';
  }
};
G.Path2D = class Path2D {
  constructor(p) { this._o = []; if (p instanceof Path2D) this._o = p._o.slice(); else if (typeof p === 'string') this._svg(p); }
  addPath(p) { if (p && p._o) this._o.push(...p._o); }
  _svg(d) {
    const t = d.match(/[a-zA-Z]|-?\d*\.?\d+(?:e[-+]?\d+)?/g) || [];
    let i = 0, cmd = '', x = 0, y = 0, sx = 0, sy = 0;
    const num = () => +t[i++];
    while (i < t.length) {
      if (/[a-zA-Z]/.test(t[i])) cmd = t[i++];
      const r = cmd === cmd.toLowerCase(), C = cmd.toUpperCase();
      if (C === 'Z') { this.closePath(); x = sx; y = sy; continue; }
      if (C === 'M') { x = (r ? x : 0) + num(); y = (r ? y : 0) + num(); this.moveTo(x, y); sx = x; sy = y; cmd = r ? 'l' : 'L'; }
      else if (C === 'L') { x = (r ? x : 0) + num(); y = (r ? y : 0) + num(); this.lineTo(x, y); }
      else if (C === 'H') { x = (r ? x : 0) + num(); this.lineTo(x, y); }
      else if (C === 'V') { y = (r ? y : 0) + num(); this.lineTo(x, y); }
      else if (C === 'C') { const a = [num(), num(), num(), num(), num(), num()]; if (r) for (let k = 0; k < 6; k++) a[k] += k & 1 ? y : x; this.bezierCurveTo(...a); x = a[4]; y = a[5]; }
      else if (C === 'Q') { const a = [num(), num(), num(), num()]; if (r) for (let k = 0; k < 4; k++) a[k] += k & 1 ? y : x; this.quadraticCurveTo(...a); x = a[2]; y = a[3]; }
      else { i++; }
    }
  }
};
for (const k of ['moveTo', 'lineTo', 'closePath', 'rect', 'arc', 'arcTo', 'bezierCurveTo', 'quadraticCurveTo', 'ellipse', 'roundRect']) val(G.Path2D.prototype, k, function (...a) { this._o.push([k, a]); });
const cvState = () => ({ m: [1, 0, 0, 1, 0, 0], fillStyle: '#000000', strokeStyle: '#000000', lineWidth: 1, font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', globalAlpha: 1, globalCompositeOperation: 'source-over', lineCap: 'butt', lineJoin: 'miter', miterLimit: 10, shadowBlur: 0, shadowColor: 'rgba(0, 0, 0, 0)', shadowOffsetX: 0, shadowOffsetY: 0, imageSmoothingEnabled: true, imageSmoothingQuality: 'low', lineDashOffset: 0, direction: 'inherit', filter: 'none', letterSpacing: '0px', clip: null, dash: [] });
const CV_PROPS = ['fillStyle', 'strokeStyle', 'lineWidth', 'font', 'textAlign', 'textBaseline', 'globalAlpha', 'globalCompositeOperation', 'lineCap', 'lineJoin', 'miterLimit', 'shadowBlur', 'shadowColor', 'shadowOffsetX', 'shadowOffsetY', 'imageSmoothingEnabled', 'imageSmoothingQuality', 'lineDashOffset', 'direction', 'filter', 'letterSpacing'];
class CanvasRenderingContext2D {
  constructor(c) { val(this, 'canvas', c); val(this, '_s', cvState()); val(this, '_st', []); val(this, '_sp', []); val(this, '_cur', null); val(this, '_lx', 0); val(this, '_ly', 0); }
  get _m() { return this._s.m; }
  _tp(x, y) { const m = this._s.m; return [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]; }
  _sc() { const m = this._s.m; return Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2])) || 1; }
  _paint(st) { if (st && st._arr) return st._arr(this._s.m); const c = N.cvColor(String(st)) || [0, 0, 0, 1]; return new Float32Array([0, c[0], c[1], c[2], c[3]]); }
  _fill(polys, st, mode) {
    const n = polys.reduce((a, p) => a + p.length, 0);
    if (!n) return;
    const pts = new Float32Array(n), cnt = new Int32Array(polys.length);
    let k = 0;
    polys.forEach((p, i) => { pts.set(p, k); k += p.length; cnt[i] = p.length / 2; });
    N.cvFill(this.canvas, pts, cnt, mode ? new Float32Array([0, 0, 0, 0, 1]) : this._paint(st), +this._s.globalAlpha, mode || (this._s.globalCompositeOperation === 'destination-out' ? 1 : 0), this._s.clip);
  }
  beginPath() { this._sp = []; this._cur = null; }
  moveTo(x, y) { x = +x; y = +y; if (!isFinite(x) || !isFinite(y)) return; this._cur = []; this._cur.closed = false; this._cur.ux = x; this._cur.uy = y; this._sp.push(this._cur); const p = this._tp(x, y); this._cur.push(p[0], p[1]); this._lx = x; this._ly = y; }
  lineTo(x, y) { x = +x; y = +y; if (!isFinite(x) || !isFinite(y)) return; if (!this._cur) return this.moveTo(x, y); const p = this._tp(x, y); this._cur.push(p[0], p[1]); this._lx = x; this._ly = y; }
  closePath() { const c = this._cur; if (!c || !c.length) return; c.closed = true; this._cur = [c[0], c[1]]; this._cur.closed = false; this._cur.ux = c.ux; this._cur.uy = c.uy; this._sp.push(this._cur); this._lx = c.ux; this._ly = c.uy; }
  _seg(n) { return Math.max(4, Math.min(256, Math.ceil(n))); }
  quadraticCurveTo(cx, cy, x, y) { if (!this._cur) this.moveTo(cx, cy); const x0 = this._lx, y0 = this._ly, n = this._seg(Math.hypot(cx - x0, cy - y0) + Math.hypot(x - cx, y - cy)) * this._sc() / 4; const s = this._seg(n); for (let i = 1; i <= s; i++) { const t = i / s, u = 1 - t; this.lineTo(u * u * x0 + 2 * u * t * cx + t * t * x, u * u * y0 + 2 * u * t * cy + t * t * y); } }
  bezierCurveTo(c1x, c1y, c2x, c2y, x, y) { if (!this._cur) this.moveTo(c1x, c1y); const x0 = this._lx, y0 = this._ly, L = Math.hypot(c1x - x0, c1y - y0) + Math.hypot(c2x - c1x, c2y - c1y) + Math.hypot(x - c2x, y - c2y); const s = this._seg(L * this._sc() / 4); for (let i = 1; i <= s; i++) { const t = i / s, u = 1 - t; this.lineTo(u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * x, u * u * u * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * y); } }
  ellipse(x, y, rx, ry, rot, a0, a1, ccw) {
    if (rx < 0 || ry < 0) throw new DOMException('negative radius', 'IndexSizeError');
    const T2 = Math.PI * 2;
    let sw = a1 - a0;
    if (!ccw) { if (sw >= T2) sw = T2; else { sw = sw % T2; if (sw < 0) sw += T2; } }
    else { if (-sw >= T2) sw = -T2; else { sw = sw % T2; if (sw > 0) sw -= T2; } }
    const s = this._seg(Math.abs(sw) * Math.max(rx, ry) * this._sc() / 3), cr = Math.cos(rot || 0), sr = Math.sin(rot || 0);
    for (let i = 0; i <= s; i++) { const a = a0 + sw * i / s, ex = rx * Math.cos(a), ey = ry * Math.sin(a), px = x + ex * cr - ey * sr, py = y + ex * sr + ey * cr; if (i === 0 && !this._cur) this.moveTo(px, py); else this.lineTo(px, py); }
  }
  arc(x, y, r, a0, a1, ccw) { this.ellipse(x, y, r, r, 0, a0, a1, ccw); }
  arcTo(x1, y1, x2, y2, r) {
    if (r < 0) throw new DOMException('negative radius', 'IndexSizeError');
    if (!this._cur) this.moveTo(x1, y1);
    const x0 = this._lx, y0 = this._ly, ax = x0 - x1, ay = y0 - y1, bx = x2 - x1, by = y2 - y1, la = Math.hypot(ax, ay), lb = Math.hypot(bx, by);
    const cr = ax * by - ay * bx;
    if (!la || !lb || !r || Math.abs(cr) < 1e-9) return this.lineTo(x1, y1);
    const cos = (ax * bx + ay * by) / (la * lb), ang = Math.acos(Math.max(-1, Math.min(1, cos))), d = r / Math.tan(ang / 2);
    const tx0 = x1 + ax / la * d, ty0 = y1 + ay / la * d, tx1 = x1 + bx / lb * d, ty1 = y1 + by / lb * d;
    const ux = ax / la + bx / lb, uy = ay / la + by / lb, ul = Math.hypot(ux, uy), dc = Math.hypot(d, r), cx = x1 + ux / ul * dc, cy = y1 + uy / ul * dc;
    this.lineTo(tx0, ty0);
    const s0 = Math.atan2(ty0 - cy, tx0 - cx), s1 = Math.atan2(ty1 - cy, tx1 - cx);
    this.arc(cx, cy, r, s0, s1, cr > 0);
  }
  rect(x, y, w, h) { this.moveTo(x, y); this.lineTo(x + w, y); this.lineTo(x + w, y + h); this.lineTo(x, y + h); this.closePath(); }
  roundRect(x, y, w, h, rr) {
    let r = Array.isArray(rr) ? rr.map((v) => +(v && v.x !== undefined ? v.x : v) || 0) : [+(rr || 0)];
    if (r.length === 1) r = [r[0], r[0], r[0], r[0]]; else if (r.length === 2) r = [r[0], r[1], r[0], r[1]]; else if (r.length === 3) r = [r[0], r[1], r[2], r[1]];
    const k = Math.min(1, Math.abs(w) / Math.max(1e-9, r[0] + r[1], r[2] + r[3]), Math.abs(h) / Math.max(1e-9, r[0] + r[3], r[1] + r[2]));
    r = r.map((v) => v * k);
    const P = Math.PI;
    this.moveTo(x + r[0], y); this.lineTo(x + w - r[1], y); if (r[1]) this.arc(x + w - r[1], y + r[1], r[1], -P / 2, 0);
    this.lineTo(x + w, y + h - r[2]); if (r[2]) this.arc(x + w - r[2], y + h - r[2], r[2], 0, P / 2);
    this.lineTo(x + r[3], y + h); if (r[3]) this.arc(x + r[3], y + h - r[3], r[3], P / 2, P);
    this.lineTo(x, y + r[0]); if (r[0]) this.arc(x + r[0], y + r[0], r[0], P, P * 1.5);
    this.closePath();
  }
  _use(p) { if (!(p instanceof G.Path2D)) return null; const sp = this._sp, cur = this._cur, lx = this._lx, ly = this._ly; this._sp = []; this._cur = null; for (const [k, a] of p._o) this[k](...a); const r = this._sp; this._sp = sp; this._cur = cur; this._lx = lx; this._ly = ly; return r; }
  _polys(sp) { return (sp || this._sp).filter((s) => s.length >= 4).map((s) => s.slice()); }
  fill(a) { const sp = this._use(a); this._fill(this._polys(sp), this._s.fillStyle, 0); }
  _strokePolys(sp) {
    const hw = Math.max(0.35, (+this._s.lineWidth || 1) * this._sc() / 2), out = [], join = this._s.lineJoin, cap = this._s.lineCap, ml = +this._s.miterLimit || 10;
    const orient = (p) => { let a = 0; for (let i = 0; i < p.length; i += 2) { const j = (i + 2) % p.length; a += p[i] * p[j + 1] - p[j] * p[i + 1]; } if (a < 0) { const q = []; for (let i = p.length - 2; i >= 0; i -= 2) q.push(p[i], p[i + 1]); return q; } return p; };
    const circ = (x, y) => { const n = Math.max(8, Math.min(48, Math.ceil(hw * 2))), p = []; for (let i = 0; i < n; i++) { const a = i / n * Math.PI * 2; p.push(x + Math.cos(a) * hw, y + Math.sin(a) * hw); } return p; };
    let dash = this._s.dash.length ? this._s.dash.map((v) => v * this._sc()) : null;
    if (dash && dash.reduce((a, b) => a + b, 0) <= 0) dash = null;
    for (const s0 of sp) {
      let s = s0.slice();
      if (s0.closed && s.length >= 4) s.push(s[0], s[1]);
      const pts = [];
      for (let i = 0; i < s.length; i += 2) if (!pts.length || Math.hypot(s[i] - pts[pts.length - 2], s[i + 1] - pts[pts.length - 1]) > 1e-6) pts.push(s[i], s[i + 1]);
      if (pts.length < 4) { if (pts.length === 2 && cap === 'round') out.push(circ(pts[0], pts[1])); continue; }
      const runs = [];
      if (dash) {
        let di = 0, left = dash[0], on = true, run = [pts[0], pts[1]], off = (+this._s.lineDashOffset || 0) * this._sc(), tot = dash.reduce((a, b) => a + b, 0);
        off = ((off % tot) + tot) % tot;
        while (off > 0) { if (off >= left) { off -= left; di = (di + 1) % dash.length; on = !on; left = dash[di]; } else { left -= off; off = 0; } }
        if (!on) run = null;
        for (let i = 0; i + 2 < pts.length; i += 2) {
          let x0 = pts[i], y0 = pts[i + 1];
          const x1 = pts[i + 2], y1 = pts[i + 3];
          let L = Math.hypot(x1 - x0, y1 - y0);
          while (L > 0) {
            const t = Math.min(left, L), nx = x0 + (x1 - x0) * (t / L), ny = y0 + (y1 - y0) * (t / L);
            if (on) { if (!run) run = [x0, y0]; run.push(nx, ny); }
            L -= t; left -= t; x0 = nx; y0 = ny;
            if (left <= 1e-9) { if (on && run) { runs.push(run); run = null; } on = !on; di = (di + 1) % dash.length; left = dash[di]; if (on) run = [x0, y0]; }
          }
        }
        if (run && run.length >= 4) runs.push(run);
      } else runs.push(pts);
      const closed = s0.closed && !dash;
      for (const r of runs) {
        const n = r.length / 2;
        for (let i = 0; i < n - 1; i++) {
          let x0 = r[i * 2], y0 = r[i * 2 + 1], x1 = r[i * 2 + 2], y1 = r[i * 2 + 3];
          const L = Math.hypot(x1 - x0, y1 - y0);
          if (L < 1e-9) continue;
          const ux = (x1 - x0) / L, uy = (y1 - y0) / L, nx = -uy * hw, ny = ux * hw;
          if (cap === 'square' && !closed) { if (i === 0) { x0 -= ux * hw; y0 -= uy * hw; } if (i === n - 2) { x1 += ux * hw; y1 += uy * hw; } }
          out.push(orient([x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny]));
        }
        const J = [];
        for (let i = 1; i < n - 1; i++) J.push([i - 1, i, i + 1]);
        if (closed && n >= 3) J.push([n - 2, n - 1, 1]);
        for (const [a, b, c] of J) {
          const px = r[b * 2], py = r[b * 2 + 1];
          if (join === 'round') { out.push(circ(px, py)); continue; }
          const ax = r[a * 2], ay = r[a * 2 + 1], qx = r[c * 2], qy = r[c * 2 + 1];
          const l1 = Math.hypot(px - ax, py - ay), l2 = Math.hypot(qx - px, qy - py);
          if (!l1 || !l2) continue;
          const u1x = (px - ax) / l1, u1y = (py - ay) / l1, u2x = (qx - px) / l2, u2y = (qy - py) / l2;
          const crs = u1x * u2y - u1y * u2x, sg = crs > 0 ? -1 : 1;
          const n1x = -u1y * hw * sg, n1y = u1x * hw * sg, n2x = -u2y * hw * sg, n2y = u2x * hw * sg;
          const poly = [px, py, px + n1x, py + n1y];
          if (join !== 'bevel') { const cosT = u1x * u2x + u1y * u2y, mlen = 1 / Math.max(1e-6, Math.sqrt((1 + cosT) / 2)); if (mlen <= ml) { const bx = n1x + n2x, by = n1y + n2y, bl = Math.hypot(bx, by); if (bl > 1e-9) poly.push(px + bx / bl * hw * mlen, py + by / bl * hw * mlen); } }
          poly.push(px + n2x, py + n2y);
          out.push(orient(poly));
        }
        if (cap === 'round' && !closed) { out.push(circ(r[0], r[1])); out.push(circ(r[r.length - 2], r[r.length - 1])); }
      }
    }
    return out;
  }
  stroke(a) { const sp = this._use(a) || this._sp; this._fill(this._strokePolys(sp), this._s.strokeStyle, 0); }
  clip(a) { const sp = this._use(a) || this._sp; let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; for (const s of sp) for (let i = 0; i < s.length; i += 2) { x0 = Math.min(x0, s[i]); x1 = Math.max(x1, s[i]); y0 = Math.min(y0, s[i + 1]); y1 = Math.max(y1, s[i + 1]); } if (x0 > x1) { x0 = y0 = 0; x1 = y1 = 0; } let c = [Math.floor(x0), Math.floor(y0), Math.ceil(x1), Math.ceil(y1)]; const o = this._s.clip; if (o) c = [Math.max(c[0], o[0]), Math.max(c[1], o[1]), Math.min(c[2], o[2]), Math.min(c[3], o[3])]; this._s.clip = new Int32Array(c); }
  isPointInPath(a, b, c) { let sp = this._sp, x = a, y = b; if (a instanceof G.Path2D) { sp = this._use(a); x = b; y = c; } let ins = false; for (const s of sp) { const n = s.length / 2; for (let i = 0, j = n - 1; i < n; j = i++) { const xi = s[i * 2], yi = s[i * 2 + 1], xj = s[j * 2], yj = s[j * 2 + 1]; if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) ins = !ins; } } return ins; }
  isPointInStroke() { return false; }
  _rectPoly(x, y, w, h) { const a = this._tp(x, y), b = this._tp(x + w, y), c = this._tp(x + w, y + h), d = this._tp(x, y + h); return [a[0], a[1], b[0], b[1], c[0], c[1], d[0], d[1]]; }
  fillRect(x, y, w, h) { if (!isFinite(x + y + w + h)) return; this._fill([this._rectPoly(x, y, w, h)], this._s.fillStyle, 0); }
  clearRect(x, y, w, h) { if (!isFinite(x + y + w + h)) return; this._fill([this._rectPoly(x, y, w, h)], null, 1); }
  strokeRect(x, y, w, h) { if (!isFinite(x + y + w + h)) return; const sp = this._sp, cur = this._cur; this._sp = []; this._cur = null; this.rect(x, y, w, h); this.stroke(); this._sp = sp; this._cur = cur; }
  save() { const s = Object.assign({}, this._s); s.m = this._s.m.slice(); s.dash = this._s.dash.slice(); this._st.push(s); }
  restore() { if (this._st.length) this._s = this._st.pop(); }
  reset() { this._s = cvState(); this._st = []; this.beginPath(); this.clearRect(0, 0, this.canvas.width, this.canvas.height); }
  isContextLost() { return false; }
  getContextAttributes() { return { alpha: true, colorSpace: 'srgb', desynchronized: false, willReadFrequently: false }; }
  transform(a, b, c, d, e, f) { const m = this._s.m; this._s.m = [m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * d, m[1] * c + m[3] * d, m[0] * e + m[2] * f + m[4], m[1] * e + m[3] * f + m[5]]; }
  setTransform(a, b, c, d, e, f) { if (a && typeof a === 'object') { const o = a; a = o.a ?? o.m11 ?? 1; b = o.b ?? o.m12 ?? 0; c = o.c ?? o.m21 ?? 0; d = o.d ?? o.m22 ?? 1; e = o.e ?? o.m41 ?? 0; f = o.f ?? o.m42 ?? 0; } else if (a === undefined) { a = d = 1; b = c = e = f = 0; } this._s.m = [+a, +b, +c, +d, +e, +f]; }
  resetTransform() { this._s.m = [1, 0, 0, 1, 0, 0]; }
  getTransform() { const m = this._s.m, r = new G.DOMMatrix(); Object.assign(r, { a: m[0], b: m[1], c: m[2], d: m[3], e: m[4], f: m[5], m11: m[0], m12: m[1], m21: m[2], m22: m[3], m41: m[4], m42: m[5], isIdentity: m[0] === 1 && !m[1] && !m[2] && m[3] === 1 && !m[4] && !m[5] }); return r; }
  translate(x, y) { this.transform(1, 0, 0, 1, +x, +y); }
  scale(x, y) { this.transform(+x, 0, 0, +y, 0, 0); }
  rotate(a) { const c = Math.cos(a), s = Math.sin(a); this.transform(c, s, -s, c, 0, 0); }
  setLineDash(a) { if (!Array.isArray(a) || a.some((v) => !(v >= 0) || !isFinite(v))) return; this._s.dash = a.length % 2 ? a.concat(a) : a.slice(); }
  getLineDash() { return this._s.dash.slice(); }
  measureText(t) {
    const [f, z] = cvFont(this._s.font), w = N.textWidth(f, z, String(t));
    const al = this._s.textAlign, l = al === 'center' ? w / 2 : al === 'right' || al === 'end' ? w : 0;
    return { width: w, actualBoundingBoxLeft: l, actualBoundingBoxRight: w - l, actualBoundingBoxAscent: z * 0.72, actualBoundingBoxDescent: z * 0.21, fontBoundingBoxAscent: z * 0.91, fontBoundingBoxDescent: z * 0.21, emHeightAscent: z * 0.8, emHeightDescent: z * 0.2, hangingBaseline: z * 0.64, alphabeticBaseline: 0, ideographicBaseline: -z * 0.21 };
  }
  _text(t, x, y, mw, st) {
    t = String(t).replace(/[\t\n\f\r]/g, ' ');
    if (!isFinite(x) || !isFinite(y)) return;
    let [f, z] = cvFont(this._s.font);
    const w = N.textWidth(f, z, t), al = this._s.textAlign, bl = this._s.textBaseline;
    let sx = 1;
    if (mw !== undefined && w > mw && mw > 0) sx = mw / w;
    let dx = al === 'center' ? -w * sx / 2 : al === 'right' || al === 'end' ? -w * sx : 0;
    let dy = bl === 'top' ? z * 0.8 : bl === 'hanging' ? z * 0.64 : bl === 'middle' ? z * 0.3 : bl === 'bottom' || bl === 'ideographic' ? -z * 0.21 : 0;
    const p = this._tp(x + dx, y + dy), sc = this._sc();
    N.cvText(this.canvas, t, p[0], p[1], f, z * sc * Math.min(1, sx), this._paint(st), +this._s.globalAlpha, this._s.clip);
  }
  fillText(t, x, y, mw) { this._text(t, x, y, mw, this._s.fillStyle); }
  strokeText(t, x, y, mw) { this._text(t, x, y, mw, this._s.strokeStyle); }
  drawImage(img, ...a) {
    if (!img) throw new TypeError('drawImage: no image');
    const el = img.__el || img;
    const iw = el.naturalWidth || el.videoWidth || el.width || 0, ih = el.naturalHeight || el.videoHeight || el.height || 0;
    let sx = 0, sy = 0, sw = iw, sh = ih, dx, dy, dw = iw, dh = ih;
    if (a.length >= 8) [sx, sy, sw, sh, dx, dy, dw, dh] = a; else if (a.length >= 4) [dx, dy, dw, dh] = a; else [dx, dy] = a;
    if (!iw || !ih) return;
    if (el.tagName === 'CANVAS') N.cvInit(el);
    const p = this._tp(dx, dy), m = this._s.m;
    N.cvImage(this.canvas, el, sx, sy, sw, sh, p[0], p[1], dw * m[0], dh * m[3], +this._s.globalAlpha, this._s.clip);
  }
  createLinearGradient(x0, y0, x1, y1) { return new CanvasGradient(1, [+x0, +y0, +x1, +y1]); }
  createRadialGradient(x0, y0, r0, x1, y1, r1) { return new CanvasGradient(2, [+x0, +y0, +r0, +x1, +y1, +r1]); }
  createConicGradient(a, x, y) { return new CanvasGradient(2, [+x, +y, 0, +x, +y, 1]); }
  createPattern(src) { return new CanvasPattern(src); }
  createImageData(w, h) { if (w && w.width !== undefined) return new G.ImageData(w.width, w.height); return new G.ImageData(Math.abs(w), Math.abs(h)); }
  getImageData(x, y, w, h) { w = Math.floor(w); h = Math.floor(h); if (!w || !h) throw new DOMException('zero size', 'IndexSizeError'); if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; } const b = N.cvGet(this.canvas, Math.floor(x), Math.floor(y), w, h); return new G.ImageData(new Uint8ClampedArray(b), w, h); }
  putImageData(d, x, y) { if (!d || !d.data) return; N.cvPut(this.canvas, d.data, d.width, d.height, Math.floor(x), Math.floor(y)); }
}
for (const k of CV_PROPS) Object.defineProperty(CanvasRenderingContext2D.prototype, k, { get() { return this._s[k]; }, set(v) {
  if (k === 'fillStyle' || k === 'strokeStyle') { if (v && v._arr) { this._s[k] = v; return; } const c = N.cvColor(String(v)); if (!c) return; this._s[k] = c[3] >= 1 ? '#' + c.slice(0, 3).map((q) => Math.round(q * 255).toString(16).padStart(2, '0')).join('') : String(v); return; }
  if (k === 'lineWidth' || k === 'miterLimit') { v = +v; if (!(v > 0) || !isFinite(v)) return; }
  if (k === 'globalAlpha') { v = +v; if (!(v >= 0 && v <= 1)) return; }
  this._s[k] = v;
}, configurable: true });
G.CanvasRenderingContext2D = CanvasRenderingContext2D;
const ctx2d = CanvasRenderingContext2D.prototype;
def(T.canvas, 'width', function () { const v = parseInt(this.getAttribute('width'), 10); return v >= 0 ? v : 300; }, function (v) { this.setAttribute('width', String(Math.max(0, v >>> 0))); const c = this.__ctx; if (c && c.beginPath) { c._s = cvState(); c._st = []; c.beginPath(); } N.cvInit(this); N.cvFill(this, new Float32Array([0, 0, 1e5, 0, 1e5, 1e5, 0, 1e5]), new Int32Array([4]), new Float32Array([0, 0, 0, 0, 1]), 1, 1, null); });
def(T.canvas, 'height', function () { const v = parseInt(this.getAttribute('height'), 10); return v >= 0 ? v : 150; }, function (v) { this.setAttribute('height', String(Math.max(0, v >>> 0))); const c = this.__ctx; if (c && c.beginPath) { c._s = cvState(); c._st = []; c.beginPath(); } N.cvInit(this); N.cvFill(this, new Float32Array([0, 0, 1e5, 0, 1e5, 1e5, 0, 1e5]), new Int32Array([4]), new Float32Array([0, 0, 0, 0, 1]), 1, 1, null); });
val(T.canvas, 'getContext', function (t) { if (t !== '2d') return null; N.cvInit(this); return cache(this, '__ctx', () => new CanvasRenderingContext2D(this)); });
val(T.canvas, 'toDataURL', function () { return 'data:,'; });
val(T.canvas, 'toBlob', function (cb) { setTimeout(() => cb(null), 0); });
val(T.canvas, 'transferControlToOffscreen', function () { throw new DOMException('Not supported', 'NotSupportedError'); });

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
def(P.Document, 'designMode', function () { const r = this.documentElement; return r && r.hasAttribute('nvx-design') ? 'on' : 'off'; }, function (v) { const r = this.documentElement; if (!r) return; v = String(v).toLowerCase(); if (v === 'on') { if (!r.hasAttribute('nvx-design')) { r.setAttribute('nvx-design', ''); const b = this.body; if (b && this === document) try { const sel = getSelection(); if (sel && !sel.rangeCount) { const rg = this.createRange(); rg.setStart(b, 0); rg.collapse(true); sel.addRange(rg); } } catch (e) {} } } else if (v === 'off') r.removeAttribute('nvx-design'); });
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
val(P.Document, 'createAttribute', function (n) { n = String(n); if (!/^[^\s"'>/=]+$/.test(n)) throw new DOMException("Failed to execute 'createAttribute' on 'Document': The localName provided ('" + n + "') contains an invalid character.", 'InvalidCharacterError'); return G.__mkAttr(n.toLowerCase()); });
val(P.Document, 'createAttributeNS', function (ns, n) { return G.__mkAttr(String(n)); });
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
if (!InputEvent.prototype.getTargetRanges) val(InputEvent.prototype, 'getTargetRanges', function getTargetRanges() { return []; });
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
  _sync() { const f = this.focusNode; if (this._r && !this._r.collapsed && !editHost(this._r.startContainer)) { selHiRange(this._r); N.setCaret(null, 0); } else { if (hiOn) { hiOn = false; N.selHi(null, 0, null, 0); } N.setCaret(f, this.focusOffset); } if (!this._pend) { this._pend = true; setTimeout(() => { this._pend = false; document.dispatchEvent(new Event('selectionchange')); }, 0); } }
  getRangeAt(i) { if (!this._r || i !== 0) throw new DOMException('Index out of range', 'IndexSizeError'); return this._r; }
  addRange(r) { if (this._r) this._r._sel = null; this._r = r; this._back = false; r._sel = this; this._sync(); }
  removeRange() { this.removeAllRanges(); }
  removeAllRanges() { if (this._r) this._r._sel = null; this._r = null; N.setCaret(null, 0); if (hiOn) { hiOn = false; N.selHi(null, 0, null, 0); } }
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
  toString() {
    const r = this._r; if (!r) return '';
    if (r.startContainer === r.endContainer && r.startContainer.nodeType === 3) return r.toString();
    const root = r.commonAncestorContainer || document.body; let out = '', lastBlk = null;
    const skip = /^(SCRIPT|STYLE|NOSCRIPT|TEMPLATE|HEAD|TITLE)$/;
    const blk = (n) => { for (let q = n.parentNode; q && q.nodeType === 1; q = q.parentNode) { const d = getComputedStyle(q).display; if (d !== 'inline' && d !== 'contents') return q; } return null; };
    const walk = (n) => {
      if (n.nodeType === 1) { if (skip.test(n.tagName)) return; if (getComputedStyle(n).display === 'none') return; if (n.tagName === 'BR' && r.intersectsNode(n)) out += '\n'; for (let c = n.firstChild; c; c = c.nextSibling) walk(c); return; }
      if (n.nodeType !== 3 || !r.intersectsNode(n)) return;
      let t = n.data; if (n === r.endContainer) t = t.slice(0, r.endOffset); if (n === r.startContainer) t = t.slice(r.startOffset);
      const b = blk(n); if (lastBlk && b !== lastBlk && out && !out.endsWith('\n')) out += '\n'; lastBlk = b;
      out += t.replace(/[\t\n ]+/g, ' ');
    };
    walk(root.nodeType === 3 ? root.parentNode : root);
    return out.replace(/ *\n */g, '\n').replace(/^\s+|\s+$/g, '');
  }
  getComposedRanges() { return this._r ? [new G.StaticRange({ startContainer: this._r.startContainer, startOffset: this._r.startOffset, endContainer: this._r.endContainer, endOffset: this._r.endOffset })] : []; }
}
G.Selection = Selection;
let hiOn = false;
function firstText(n, after) { const w = (q) => { if (q.nodeType === 3) return q; for (let c = q.firstChild; c; c = c.nextSibling) { const t = w(c); if (t) return t; } return null; }; for (let q = n; q; ) { const t = w(q); if (t) return t; while (q && !q.nextSibling) q = q.parentNode; if (!q) return null; q = q.nextSibling; } return null; }
function lastText(n) { const w = (q) => { if (q.nodeType === 3) return q; for (let c = q.lastChild; c; c = c.previousSibling) { const t = w(c); if (t) return t; } return null; }; for (let q = n; q; ) { const t = w(q); if (t) return t; while (q && !q.previousSibling) q = q.parentNode; if (!q) return null; q = q.previousSibling; } return null; }
function selHiRange(r) {
  let sn = r.startContainer, so = r.startOffset, en = r.endContainer, eo = r.endOffset;
  if (sn.nodeType !== 3) { const c = sn.childNodes[so]; const t = c ? firstText(c) : (() => { let q = sn; while (q && !q.nextSibling) q = q.parentNode; return q ? firstText(q.nextSibling) : null; })(); sn = t; so = 0; }
  if (en.nodeType !== 3) { const c = eo > 0 ? en.childNodes[eo - 1] : null; let t = c ? lastText(c) : null; if (!t && !c) { let q = en; while (q && !q.previousSibling) q = q.parentNode; t = q ? lastText(q.previousSibling) : null; } en = t; eo = t ? t.data.length : 0; }
  if (!sn || !en) { if (hiOn) { hiOn = false; N.selHi(null, 0, null, 0); } return; }
  hiOn = true;
  N.selHi(sn, utf16ToByte(sn.data, so), en, utf16ToByte(en.data, eo));
}
function utf16ToByte(str, i) { let b = 0; for (let k = 0; k < i && k < str.length; k++) { const c = str.charCodeAt(k); if (c < 0x80) b += 1; else if (c < 0x800) b += 2; else if (c >= 0xD800 && c < 0xDC00) { b += 4; k++; } else b += 3; } return b; }
function byteToUtf16(str, b) { let i = 0, acc = 0; while (i < str.length && acc < b) { const c = str.charCodeAt(i); if (c < 0x80) acc += 1; else if (c < 0x800) acc += 2; else if (c >= 0xD800 && c < 0xDC00) { acc += 4; i++; } else acc += 3; i++; } return i; }
const selection = new Selection();
{
  let anc = null, ancOff = 0;
  const isWord = (ch) => /[\p{L}\p{N}_]/u.test(ch);
  G.__pageSel = function (n, v) {
    if (v < 0) { selection.removeAllRanges(); anc = null; return; }
    const kind = v & 1, cnt = (v >> 1) & 3, off = byteToUtf16(n.data || '', v >> 3);
    if (kind === 0) {
      if (cnt === 2 && n.nodeType === 3) { const d = n.data; let a = off, b = off; while (a > 0 && isWord(d[a - 1])) a--; while (b < d.length && isWord(d[b])) b++; if (a === b && b < d.length) b++; selection.setBaseAndExtent(n, a, n, b); anc = null; return; }
      if (cnt === 3) { let b = n.parentNode; while (b && b.nodeType === 1 && getComputedStyle(b).display === 'inline') b = b.parentNode; if (b) selection.selectAllChildren(b); anc = null; return; }
      if (cnt === 1 && selection.rangeCount && !selection.isCollapsed) { const r = selection.getRangeAt(0); try { if (r.comparePoint(n, off) === 0 && !(r.startContainer === n && r.startOffset === off) && !(r.endContainer === n && r.endOffset === off)) { anc = null; if (G.__tdragArmPage) G.__tdragArmPage(r); return; } } catch (e) {} }
      selection.removeAllRanges(); anc = n; ancOff = off; return;
    }
    if (!anc) return;
    if (anc === n && ancOff === off) { selection.removeAllRanges(); return; }
    selection.setBaseAndExtent(anc, ancOff, n, off);
  };
  G.__pageSelAll = function () { const b = document.body; if (b) selection.selectAllChildren(b); };
  G.__copySel = function (t, cut) {
    const txt = selection.toString();
    const dt = new DataTransfer();
    const ev = new ClipboardEvent(cut ? 'cut' : 'copy', { bubbles: true, cancelable: true }); ev.clipboardData = dt;
    (t || document).dispatchEvent(ev);
    if (ev.defaultPrevented) { const d = dt.getData('text/plain'); if (d) N.clipSet(d); return; }
    if (txt) N.clipSet(txt);
    if (cut && txt && editHost(selection.anchorNode)) document.execCommand('delete');
  };
  G.__pasteSel = function (t) {
    const txt = N.clipGet();
    const dt = new DataTransfer(); dt.setData('text/plain', txt);
    const ev = new ClipboardEvent('paste', { bubbles: true, cancelable: true }); ev.clipboardData = dt;
    (t || document).dispatchEvent(ev);
    if (!ev.defaultPrevented && txt && editHost(t)) document.execCommand('insertText', false, txt);
  };
}
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
  { const c0 = String(cmd).toLowerCase(); if (c0 === 'copy' || c0 === 'cut') { const ae = document.activeElement; if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA')) { const v = String(ae.value).slice(ae.selectionStart, ae.selectionEnd); if (v) N.clipSet(v); if (c0 === 'cut' && v) { const a = ae.selectionStart; ae.value = ae.value.slice(0, a) + ae.value.slice(ae.selectionEnd); ae.selectionStart = ae.selectionEnd = a; } return !!v; } G.__copySel(null, c0 === 'cut'); return true; } if (c0 === 'paste') return false; }
  const host = editHost(selection.focusNode); if (!host) return false;
  if (cmd === 'insertText') { ceInsertText(host, String(value)); host.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'insertText', data: String(value) })); return true; }
  if (cmd === 'delete') { ceDelete(host, true); host.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: 'deleteContentBackward' })); return true; }
  if (cmd === 'forwardDelete') { ceDelete(host, false); return true; }
  if (cmd === 'insertParagraph') { ceParagraph(host); return true; }
  if (cmd === 'selectAll') { selection.selectAllChildren(host); return true; }
  if (cmd === 'insertHTML') { const r = selection.getRangeAt(0); r.deleteContents(); const f = r.createContextualFragment(String(value)); const last = f.lastChild; r.insertNode(f); if (last) selection.collapse(last.parentNode, idxOf(last) + 1); return true; }
  return false;
});
{
  const ex = P.Document.execCommand;
  const INL = { bold: ['b', 'strong'], italic: ['i', 'em'], underline: ['u'], strikethrough: ['s', 'strike', 'del'], subscript: ['sub'], superscript: ['sup'] };
  const BLK = /^(p|div|h[1-6]|pre|blockquote|li|address)$/;
  const BLKW = /^(p|div|h[1-6]|pre|blockquote|address)$/;
  const hist = new WeakMap();
  let lastHost = null;
  const snap = h => { let a = hist.get(h); if (!a) { a = { u: [], r: [] }; hist.set(h, a); } a.u.push(h.innerHTML); if (a.u.length > 100) a.u.shift(); a.r.length = 0; };
  const rangeIn = () => { if (!selection.rangeCount) return null; const r = selection.getRangeAt(0); const h = editHost(r.commonAncestorContainer.nodeType === 3 ? r.commonAncestorContainer.parentNode : r.commonAncestorContainer); return h ? [r, h] : null; };
  const textsIn = (r) => { const out = []; const root = r.commonAncestorContainer; if (root.nodeType === 3) return [root]; const w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT); let n; while ((n = w.nextNode())) { if (r.intersectsNode(n) && n.data.length) out.push(n); } return out; };
  const inTag = (n, tags, host) => { for (let q = n.nodeType === 3 ? n.parentNode : n; q && q !== host; q = q.parentNode) if (q.nodeType === 1 && tags.includes(q.localName)) return q; return null; };
  const stateOf = (c) => { const x = rangeIn(); if (!x) return false; const [r, h] = x; const tags = INL[c]; if (tags) { const ts = textsIn(r); if (!ts.length) return !!inTag(r.startContainer, tags, h); return ts.every(t => inTag(t, tags, h)); } if (c === 'insertunorderedlist' || c === 'insertorderedlist') return !!inTag(r.startContainer, [c === 'insertunorderedlist' ? 'ul' : 'ol'], h); if (c.startsWith('justify')) { const b = inTag(r.startContainer, ['p', 'div', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'li'], h) || h; const ta = getComputedStyle(b).textAlign; const want = c.slice(7); return ta === want || (want === 'left' && (ta === 'start' || ta === 'left')) || (want === 'full' && ta === 'justify'); } return false; };
  const strip = (frag, tags) => { for (const el of Array.from(frag.querySelectorAll(tags.join(',')))) { while (el.firstChild) el.parentNode.insertBefore(el.firstChild, el); el.remove(); } };
  function toggleInline(c, r, h) {
    const tags = INL[c];
    const on = stateOf(c);
    if (r.collapsed) return true;
    const frag = r.extractContents();
    strip(frag, tags);
    let ins;
    if (on) {
      const anc = inTag(r.startContainer, tags, h);
      if (anc) { const after = document.createRange(); after.setStart(r.startContainer, r.startOffset); after.setEndAfter(anc); const tail = after.extractContents(); const first = frag.firstChild, last = frag.lastChild; anc.parentNode.insertBefore(frag, anc.nextSibling); const lastIns = last || anc; if (tail.textContent) { lastIns.parentNode.insertBefore(tail, lastIns.nextSibling); } if (first) { const nr = document.createRange(); nr.setStartBefore(first); nr.setEndAfter(last); selection.removeAllRanges(); selection.addRange(nr); } if (!anc.textContent) anc.remove(); return true; }
      ins = frag;
      const first = frag.firstChild, last = frag.lastChild; r.insertNode(frag); if (first) { const nr = document.createRange(); nr.setStartBefore(first); nr.setEndAfter(last); selection.removeAllRanges(); selection.addRange(nr); } return true;
    }
    const w = document.createElement(tags[0]); w.appendChild(frag); r.insertNode(w);
    for (const t of tags) for (const e of Array.from(w.parentNode.querySelectorAll(':scope > ' + t))) { const nx = e.nextSibling; if (nx && nx.nodeType === 1 && nx.localName === t) { while (nx.firstChild) e.appendChild(nx.firstChild); nx.remove(); } }
    const nr = document.createRange(); nr.selectNodeContents(w); selection.removeAllRanges(); selection.addRange(nr);
    return true;
  }
  const blocksIn = (r, h) => { const set = []; const add = b => { if (b && !set.includes(b)) set.push(b); }; const blk = n => { for (let q = n.nodeType === 3 ? n.parentNode : n; q && q !== h; q = q.parentNode) if (q.nodeType === 1 && BLK.test(q.localName)) return q; return null; }; const ts = textsIn(r); if (!ts.length) add(blk(r.startContainer)); for (const t of ts) add(blk(t)); if (!set.length) { const p = document.createElement('p'); while (h.firstChild) p.appendChild(h.firstChild); h.appendChild(p); set.push(p); } return set; };
  const reselect = (els) => { if (!els.length) return; const nr = document.createRange(); nr.setStart(els[0], 0); const l = els[els.length - 1]; nr.setEnd(l, l.childNodes.length); selection.removeAllRanges(); selection.addRange(nr); };
  function formatBlock(tag, r, h) { tag = String(tag || 'p').replace(/[<>]/g, '').toLowerCase(); const out = []; for (const b of blocksIn(r, h)) { if (b.localName === tag) { out.push(b); continue; } const nb = document.createElement(tag); for (const a of Array.from(b.attributes)) if (a.name === 'style' || a.name === 'class') nb.setAttribute(a.name, a.value); while (b.firstChild) nb.appendChild(b.firstChild); b.replaceWith(nb); out.push(nb); } reselect(out); return true; }
  function list(tag, r, h) { const bs = blocksIn(r, h); const inList = bs.every(b => b.localName === 'li' && b.parentNode.localName === tag); if (inList) { for (const li of bs) { const p = document.createElement('p'); while (li.firstChild) p.appendChild(li.firstChild); const L = li.parentNode; L.parentNode.insertBefore(p, L); li.remove(); if (!L.children.length) L.remove(); } return true; } const L = document.createElement(tag); const lis = []; bs[0].parentNode.insertBefore(L, bs[0]); for (const b of bs) { const li = document.createElement('li'); if (b.localName === 'li') { while (b.firstChild) li.appendChild(b.firstChild); const pl = b.parentNode; b.remove(); if (pl && !pl.children.length && pl !== h) pl.remove(); } else { while (b.firstChild) li.appendChild(b.firstChild); b.remove(); } L.appendChild(li); lis.push(li); } reselect(lis); return true; }
  function styleSpan(prop, val, r) { if (r.collapsed) return true; const sp = document.createElement('span'); sp.style.setProperty(prop, val); sp.appendChild(r.extractContents()); r.insertNode(sp); const nr = document.createRange(); nr.selectNodeContents(sp); selection.removeAllRanges(); selection.addRange(nr); return true; }
  val(P.Document, 'execCommand', function (cmd, ui, value) {
    const c = String(cmd).toLowerCase();
    const x = rangeIn();
    if (c === 'undo' || c === 'redo') { const hh = x ? x[1] : lastHost; if (!hh || !hh.isConnected) return false; const a = hist.get(hh); if (!a) return false; const from = c === 'undo' ? a.u : a.r, to = c === 'undo' ? a.r : a.u; if (!from.length) return false; to.push(hh.innerHTML); hh.innerHTML = from.pop(); try { const nr = document.createRange(); nr.selectNodeContents(hh); nr.collapse(false); selection.removeAllRanges(); selection.addRange(nr); } catch (e) {} hh.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: c === 'undo' ? 'historyUndo' : 'historyRedo' })); return true; }
    if (c === 'copy' || c === 'cut' || c === 'paste') { return false; }
    if (!x) return ex.call(this, cmd, ui, value);
    const [r, h] = x;
    lastHost = h;
    snap(h);
    let ok = false, it = 'format' + c;
    if (INL[c]) { ok = toggleInline(c, r, h); it = 'format' + c[0].toUpperCase() + c.slice(1); }
    else if (c === 'formatblock' || c === 'heading') ok = formatBlock(value, r, h);
    else if (c === 'insertunorderedlist') ok = list('ul', r, h);
    else if (c === 'insertorderedlist') ok = list('ol', r, h);
    else if (c === 'createlink' && value) { if (r.collapsed) { const a = document.createElement('a'); a.href = value; a.textContent = value; r.insertNode(a); } else { const a = document.createElement('a'); a.href = value; a.appendChild(r.extractContents()); r.insertNode(a); } ok = true; }
    else if (c === 'unlink') { for (const t of textsIn(r)) { const a = inTag(t, ['a'], h); if (a) { while (a.firstChild) a.parentNode.insertBefore(a.firstChild, a); a.remove(); } } ok = true; }
    else if (c === 'removeformat') { const f = r.extractContents(); strip(f, ['b', 'strong', 'i', 'em', 'u', 's', 'strike', 'del', 'sub', 'sup', 'font', 'span', 'mark', 'code']); r.insertNode(f); ok = true; }
    else if (c === 'forecolor') ok = styleSpan('color', value, r);
    else if (c === 'hilitecolor' || c === 'backcolor') ok = styleSpan('background-color', value, r);
    else if (c === 'fontname') ok = styleSpan('font-family', value, r);
    else if (c === 'fontsize') ok = styleSpan('font-size', ['', '10px', '13px', '16px', '18px', '24px', '32px', '48px'][Math.max(1, Math.min(7, +value || 3))], r);
    else if (c.startsWith('justify')) { const a = { justifyleft: 'left', justifycenter: 'center', justifyright: 'right', justifyfull: 'justify' }[c]; if (a) { for (const b of blocksIn(r, h)) b.style.textAlign = a; ok = true; } }
    else if (c === 'indent') { for (const b of blocksIn(r, h)) b.style.marginLeft = ((parseFloat(b.style.marginLeft) || 0) + 40) + 'px'; ok = true; }
    else if (c === 'outdent') { for (const b of blocksIn(r, h)) b.style.marginLeft = Math.max(0, (parseFloat(b.style.marginLeft) || 0) - 40) + 'px'; ok = true; }
    else if (c === 'inserthorizontalrule') { r.deleteContents(); r.insertNode(document.createElement('hr')); ok = true; }
    else if (c === 'insertimage' && value) { r.deleteContents(); const im = document.createElement('img'); im.src = value; r.insertNode(im); ok = true; }
    else if (c === 'insertlinebreak') { r.deleteContents(); const br = document.createElement('br'); r.insertNode(br); selection.collapse(br.parentNode, idxOf(br) + 1); ok = true; }
    else { const a = hist.get(h); if (a) a.u.pop(); return ex.call(this, cmd, ui, value); }
    if (ok) h.dispatchEvent(new InputEvent('input', { bubbles: true, inputType: it }));
    return ok;
  });
  val(P.Document, 'queryCommandState', function (c) { return stateOf(String(c).toLowerCase()); });
  val(P.Document, 'queryCommandValue', function (c) { c = String(c).toLowerCase(); const x = rangeIn(); if (!x) return ''; const [r, h] = x; if (c === 'formatblock') { for (let q = r.startContainer; q && q !== h; q = q.parentNode) if (q.nodeType === 1 && BLK.test(q.localName)) return q.localName; return ''; } const el = r.startContainer.nodeType === 3 ? r.startContainer.parentNode : r.startContainer; const cs = getComputedStyle(el); if (c === 'forecolor') return cs.color; if (c === 'backcolor' || c === 'hilitecolor') return cs.backgroundColor; if (c === 'fontname') return cs.fontFamily; if (c === 'fontsize') return cs.fontSize; return stateOf(c) ? 'true' : 'false'; });
  val(P.Document, 'queryCommandEnabled', function (c) { return !!rangeIn() || /^(copy|selectall)$/i.test(c); });
  val(P.Document, 'queryCommandIndeterm', function () { return false; });
  val(P.Document, 'queryCommandSupported', function (c) { return /^(insertText|delete|forwardDelete|insertParagraph|selectAll|insertHTML|bold|italic|underline|strikeThrough|subscript|superscript|formatBlock|heading|insertUnorderedList|insertOrderedList|createLink|unlink|removeFormat|foreColor|hiliteColor|backColor|fontName|fontSize|justifyLeft|justifyCenter|justifyRight|justifyFull|indent|outdent|insertHorizontalRule|insertImage|insertLineBreak|undo|redo)$/i.test(c); });
}
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

const UA = 'Mozilla/5.0 (X11; NoovexOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Noovex/10.60';
G.navigator = {
  userAgent: UA, appName: 'Netscape', appCodeName: 'Mozilla', appVersion: UA.slice(8), platform: 'Linux x86_64', product: 'Gecko', productSub: '20030107', vendor: 'Google Inc.', vendorSub: '',
  language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, hardwareConcurrency: 4, deviceMemory: 4, maxTouchPoints: 0, doNotTrack: null, webdriver: false, pdfViewerEnabled: false,
  clipboard: { writeText: (t) => { N.clipSet(String(t)); return Promise.resolve(); }, readText: () => Promise.resolve(N.clipGet()), write: async (items) => { for (const it of items || []) { if (it && it.types && it.types.includes('text/plain')) { const b = await it.getType('text/plain'); N.clipSet(await b.text()); } } }, read: () => Promise.resolve([{ types: ['text/plain'], getType: (t) => Promise.resolve(new Blob([N.clipGet()], { type: 'text/plain' })) }]), addEventListener() {}, removeEventListener() {} },
  sendBeacon(u, d) { if (u === undefined) throw new TypeError("Failed to execute 'sendBeacon' on 'Navigator': 1 argument required, but only 0 present."); let url; try { url = new URL(String(u), document.baseURI).href; } catch (e) { throw new TypeError("Failed to execute 'sendBeacon' on 'Navigator': The URL argument is ill-formed or unsupported."); } if (!/^https?:/.test(url)) throw new TypeError("Failed to execute 'sendBeacon' on 'Navigator': Beacons are only supported over HTTP(S)."); try { if (G.__repSeen) G.__repSeen(url, d); const o = { method: 'POST', keepalive: true, credentials: 'include', mode: 'no-cors' }; if (d !== undefined && d !== null) o.body = d; G.fetch(url, o).catch(() => {}); } catch (e) {} return true; }, vibrate() { return false; }, javaEnabled() { return false; }, share: undefined,
  geolocation: { getCurrentPosition(s, e) { permAsk('geolocation', true).then((st) => { if (!e) return; if (st === 'granted') e({ code: 2, message: 'Position unavailable: this computer has no location source', PERMISSION_DENIED: 1, POSITION_UNAVAILABLE: 2, TIMEOUT: 3 }); else e({ code: 1, message: 'User denied Geolocation', PERMISSION_DENIED: 1, POSITION_UNAVAILABLE: 2, TIMEOUT: 3 }); }); }, watchPosition(s, e) { this.getCurrentPosition(s, e); return 1; }, clearWatch() {} },
  permissions: { query(d) { const n = d && d.name; if (typeof n !== 'string') return Promise.reject(new TypeError("Failed to execute 'query' on 'Permissions': Failed to read the 'name' property from 'PermissionDescriptor': Required member is undefined.")); const known = ['geolocation', 'notifications', 'camera', 'microphone', 'clipboard-read', 'clipboard-write', 'persistent-storage', 'push', 'midi', 'background-sync', 'storage-access', 'screen-wake-lock', 'accelerometer', 'gyroscope', 'magnetometer', 'local-fonts', 'window-management']; if (!known.includes(n)) return Promise.reject(new TypeError("Failed to execute 'query' on 'Permissions': The provided value '" + n + "' is not a valid enum value of type PermissionName.")); const st = new PermissionStatus(n); return Promise.resolve(st); } },
  connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener() {}, removeEventListener() {} },
  mediaSession: { setActionHandler() {}, metadata: null }, plugins: [], mimeTypes: [],
  userAgentData: { brands: [{ brand: 'Chromium', version: '124' }], mobile: false, platform: 'Linux', getHighEntropyValues: () => Promise.resolve({}) },
  storage: { estimate: () => Promise.resolve({ quota: 1e8, usage: 0, usageDetails: {} }), persist: () => Promise.resolve(false), persisted: () => Promise.resolve(false), getDirectory: () => Promise.reject(new DOMException('The request is not allowed.', 'SecurityError')) },
  locks: (() => {
    const held = new Map(), queue = [];
    class Lock { constructor(n, m) { this.name = n; this.mode = m; } }
    const grantable = (r) => { const h = held.get(r.name) || []; if (r.mode === 'exclusive') return h.length === 0; return h.every((l) => l.mode === 'shared'); };
    const pump = () => { for (let i = 0; i < queue.length; i++) { const r = queue[i]; if (r.dead) { queue.splice(i--, 1); continue; } const blocked = queue.slice(0, i).some((q) => q.name === r.name && !q.dead); if (blocked || !grantable(r)) continue; queue.splice(i--, 1); run(r); } };
    const run = (r) => {
      const l = new Lock(r.name, r.mode); const h = held.get(r.name) || []; h.push(l); held.set(r.name, h);
      let p; try { p = Promise.resolve(r.cb(l)); } catch (e) { p = Promise.reject(e); }
      p.then((v) => { rel(l); r.res(v); }, (e) => { rel(l); r.rej(e); });
    };
    const rel = (l) => { const h = held.get(l.name) || []; const i = h.indexOf(l); if (i >= 0) h.splice(i, 1); if (!h.length) held.delete(l.name); pump(); };
    return {
      request(name, o, cb) {
        if (typeof o === 'function') { cb = o; o = {}; } o = o || {};
        if (typeof cb !== 'function') return Promise.reject(new TypeError("Failed to execute 'request' on 'LockManager': The callback is not a function."));
        name = String(name); const mode = o.mode === 'shared' ? 'shared' : 'exclusive';
        if (name.startsWith('-')) return Promise.reject(new DOMException("Failed to execute 'request' on 'LockManager': Names cannot start with '-'.", 'NotSupportedError'));
        return new Promise((res, rej) => {
          const r = { name, mode, cb, res, rej, dead: false };
          if (o.signal) { if (o.signal.aborted) { rej(o.signal.reason || new DOMException('The request was aborted.', 'AbortError')); return; } o.signal.addEventListener('abort', () => { if (queue.includes(r)) { r.dead = true; rej(o.signal.reason || new DOMException('The request was aborted.', 'AbortError')); pump(); } }); }
          if (o.steal) { held.delete(name); run(r); return; }
          if (o.ifAvailable) { if (grantable(r) && !queue.some((q) => q.name === name && !q.dead)) run(r); else { Promise.resolve(cb(null)).then(res, rej); } return; }
          queue.push(r); pump();
        });
      },
      query() { const h = [], p = []; for (const [n, ls] of held) for (const l of ls) h.push({ name: n, mode: l.mode, clientId: 'nvx' }); for (const r of queue) if (!r.dead) p.push({ name: r.name, mode: r.mode, clientId: 'nvx' }); return Promise.resolve({ held: h, pending: p }); }
    };
  })()
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
G.open = (u, target, features) => {
  const t = target === undefined || target === null ? '' : String(target);
  if (t === '_self' || t === '_top' || t === '_parent') { if (u) G.location.assign(String(u)); return G; }
  const f = puFeat(features);
  const popup = features !== undefined && features !== null && String(features) !== '' || (t && t !== '_blank');
  const url = u ? N.resolve(String(u)) : 'about:blank';
  if (!popup) { if (u) G.location.assign(String(u)); return null; }
  const pAllowed = N.permGet && N.permGet('popups') === 1;
  if (!pAllowed && !puActive()) { N.log('Pop-up blocked: window.open was called without a click or key press (' + url + ')', 1); if (N.permAsk && !N.isFrame() && N.permGet('popups') !== 2) N.permAsk('popups'); return null; }
  if (!pAllowed) puConsume();
  const W = parseInt(f.width || f.innerwidth || '500', 10) || 500, H = parseInt(f.height || f.innerheight || '600', 10) || 600;
  const nop = f.noopener === 'yes' || f.noopener === '1' || f.noreferrer === 'yes' || f.noreferrer === '1';
  const isF = N.isFrame();
  if (isF === 2) { if (url !== 'about:blank') G.location.assign(url); return nop ? null : G; }
  if (isF) { N.framePost(JSON.stringify({ k: 'po', u: url, n: t, w: W, h: H })); if (FRAME_POPUP) FRAME_POPUP._gone = true; FRAME_POPUP = new PopupProxy(url, t, true); return nop ? null : FRAME_POPUP; }
  const w = puOpenTop(url, t, W, H, -1);
  return nop ? null : w;
};
G.postMessage = (data, origin, transfer) => { let d = data; try { d = typeof structuredClone === 'function' ? structuredClone(data) : data; } catch (e) {} const to = typeof origin === 'object' && origin ? (origin.targetOrigin || '*') : (origin || '*'); const me = N.origin(); if (to !== '*' && to !== '/') { let t2; try { t2 = new URL(String(to)).origin; } catch (e) { throw new DOMException("Failed to execute 'postMessage' on 'Window': Invalid target origin '" + to + "'.", 'SyntaxError'); } if (t2 !== me && me !== 'null') return; } const tl = Array.isArray(transfer) ? transfer : origin && typeof origin === 'object' && Array.isArray(origin.transfer) ? origin.transfer : []; const ports = tl.filter((x) => G.MessagePort && x instanceof G.MessagePort); setTimeout(() => run(G, new MessageEvent('message', { data: d, origin: me, source: G, ports })), 0); };
G.reportError = reportErr;
if (N.isFrame() === 2) { FRAME_PARENT = new FrameWindowProxy(null); G.opener = FRAME_PARENT; G.close = () => N.popupClose(); } else if (N.isFrame()) { FRAME_PARENT = new FrameWindowProxy(null); G.parent = FRAME_PARENT; G.top = FRAME_PARENT; }
const framePoll = () => {
  const isF = N.isFrame();
  const ns = isF ? 1 : FR.S.length + 1;
  if (!isF) puTick();
  for (let si0 = 0; si0 < ns; si0++) {
    const si = !isF && si0 === FR.S.length ? PSLOT : si0;
    if (!isF) N.frameSlot(si);
    for (let k = 0; k < 32; k++) {
      const m = N.frameRecv();
      if (m === null) break;
      let o;
      try { o = JSON.parse(m); } catch (e) { continue; }
      let rt = FRAME_PARENT;
      if (!isF && si !== PSLOT) { const S = FR.S[si]; if (S.run && S.run.isConnected) rt = S.run.contentWindow; }
      if (o && o.k === 'po' && !isF && si !== PSLOT) { puOpenTop(String(o.u), String(o.n || ''), o.w | 0, o.h | 0, si); continue; }
      if (o && o.k === 'pm' && !isF && si !== PSLOT) { if (PU.owner === si && N.popupInfo() !== 0) { N.frameSlot(PSLOT); N.framePost(String(o.m)); N.frameSlot(si); } continue; }
      if (o && o.k === 'px' && !isF && si !== PSLOT) { if (PU.owner === si) N.popupClose(); continue; }
      if (o && o.k === 'pc' && isF) { if (FRAME_POPUP) FRAME_POPUP._gone = true; continue; }
      if (o && o.k === 'pf' && isF) { let q; try { q = JSON.parse(String(o.m)); } catch (e) { continue; } const me2 = N.origin(); if (q.t && q.t !== '*') { let tg = q.t; if (tg === '/') tg = q.o; else { try { tg = new URL(q.t).origin; } catch (e) { continue; } } if (tg !== me2) continue; } run(G, new MessageEvent('message', { data: q.d, origin: q.o, source: FRAME_POPUP, ports: xpIn(q.p, FRAME_POPUP) })); continue; }
      if (!isF && si === PSLOT && PU.owner >= 0) { if (o && o.k === 'p') continue; N.frameSlot(PU.owner); N.framePost(JSON.stringify({ k: 'pf', m })); N.frameSlot(si); continue; }
      if (!isF && si === PSLOT) rt = PU.win;
      if (o && o.k === 'p') { try { xpRecv(o, rt); } catch (e) { reportErr(e); } continue; }
      const me = N.origin();
      if (o.t && o.t !== '*') { let tgo = o.t; if (tgo === '/') tgo = o.o; else { try { tgo = new URL(o.t).origin; } catch (e) { continue; } } if (tgo !== me) continue; }
      let src = FRAME_PARENT;
      if (!isF && si === PSLOT) src = PU.win;
      else if (!isF) { FR.recv++; const S = FR.S[si]; S.act = Date.now(); if (S.run && S.run.isConnected) src = S.run.contentWindow; else { if (!FRAME_CHILD) { const f = document.querySelector('iframe[src],iframe[srcdoc]'); if (f) f.contentWindow; } src = FRAME_CHILD; } }
      run(G, new MessageEvent('message', { data: o.d, origin: o.o, source: src, ports: xpIn(o.p, src || rt) }));
    }
  }
};
let frameSeen = '';
const frameLoaded = new WeakMap();
const frFailed = new WeakMap();
const frBlocked = (f) => { const r = frFailed.get(f); if (!r || r.k !== frKey(f)) return false; return !(r.n < 3 && !r.v && frVis(f)); };
const frKey = (f) => (f.getAttribute('src') || '') + '\u0001' + (f.hasAttribute('srcdoc') ? 'd' + f.getAttribute('srcdoc').length : '');
const frVisC = new WeakMap();
const frVis = (f) => { const now = Date.now(); const c = frVisC.get(f); if (c && now - c[0] < 300) return c[1]; const v = frVis0(f); frVisC.set(f, [now, v]); return v; };
const frVis0 = (f) => { try { if (!f.isConnected) return false; const r = f.getBoundingClientRect(); if (r.width < 24 || r.height < 24) return false; if (r.bottom < 0 || r.right < 0 || r.top > innerHeight || r.left > innerWidth) return false; for (let e = f; e && e.nodeType === 1; e = e.parentElement) { const cs = getComputedStyle(e); if (cs.display === 'none' || cs.visibility === 'hidden' || cs.opacity === '0') return false; } return true; } catch (e) { return false; } };
const frRunnable = (f) => f.isConnected && (f.hasAttribute('srcdoc') || (f.getAttribute('src') && !/^\s*(about:|javascript:)/i.test(f.getAttribute('src'))));
const frOpen = (f, si) => {
  const S = FR.S[si];
  N.frameSlot(si);
  if (S.run) FR.switches++;
  N.frameSwitch();
  S.run = f; S.key = frKey(f); S.loaded = false; S.failed = false; S.t0 = Date.now(); S.act = S.t0; frameSeen = S.key;
  let sbx = 0;
  if (f.hasAttribute('sandbox')) { sbx = 0x100; const SBB = { 'allow-scripts': 1, 'allow-same-origin': 2, 'allow-forms': 4, 'allow-popups': 8, 'allow-top-navigation': 16, 'allow-modals': 32, 'allow-top-navigation-by-user-activation': 16 }; for (const t of String(f.getAttribute('sandbox')).toLowerCase().split(/\s+/)) if (SBB[t]) sbx |= SBB[t]; }
  if (f.hasAttribute('srcdoc')) N.frameOpen('about:srcdoc', f.getAttribute('srcdoc'), sbx); else N.frameOpen(f.getAttribute('src'), undefined, sbx);
};
const frFlush = (f, si) => { const q = f.__fq; if (!q || !q.length) return; f.__fq = []; N.frameSlot(si); for (const p of q) { N.framePost(p); FR.deliv++; } FR.S[si].act = Date.now(); const i = FR.want.indexOf(f); if (i >= 0) FR.want.splice(i, 1); };
let frAll = null, frGen = -1;
const frameScan = () => {
  if (!document.querySelectorAll) return;
  const fg = N.domGen ? N.domGen() : -2;
  if (fg !== frGen || fg === -2 || !frAll) { frAll = document.querySelectorAll('iframe'); frGen = fg; }
  const all = frAll;
  const isF = N.isFrame();
  const freeSlot = !isF && FR.S.some((S) => !S.run);
  for (const f of all) {
    if (!f.isConnected) continue;
    const key = frKey(f);
    if (frameLoaded.get(f) === key) continue;
    if (!isF) { const si = frSlotOf(f); if (si >= 0 && FR.S[si].key === key && !FR.S[si].loaded) continue; if (si < 0 && freeSlot && frRunnable(f)) continue; }
    frameLoaded.set(f, key);
    const ev = new Event('load'); ev.isTrusted = true; run(f, ev);
  }
  if (isF) return;
  for (let i = FR.want.length - 1; i >= 0; i--) if (!FR.want[i].isConnected) FR.want.splice(i, 1);
  const now = Date.now();
  for (let si = 0; si < FR.S.length; si++) {
    const S = FR.S[si];
    if (S.run && (!S.run.isConnected || frKey(S.run) !== S.key)) { const old = S.run; S.run = null; S.loaded = false; N.frameSlot(si); N.frameSwitch(); FR.switches++; if (old.isConnected && frRunnable(old) && frSlotOf(old) < 0) frOpen(old, si); continue; }
    if (S.run && !S.loaded) {
      N.frameSlot(si);
      const st = N.frameInfo();
      if (st === 2 || st === 3 || now - S.t0 > 60000) { S.loaded = true; S.failed = st !== 2; if (S.failed) { if (st !== 3) N.frameFail('Took longer than 60 s to load'); const r0 = frFailed.get(S.run); frFailed.set(S.run, { k: S.key, n: r0 && r0.k === S.key ? r0.n + 1 : 1, v: frVis0(S.run) }); } const fresh = frameLoaded.get(S.run) !== S.key; frameLoaded.set(S.run, S.key); if (fresh) { const ev = new Event('load'); ev.isTrusted = true; run(S.run, ev); } if (st === 2) frFlush(S.run, si); }
      continue;
    }
    if (S.run && S.loaded && !S.failed && S.run.__fq && S.run.__fq.length) frFlush(S.run, si);
  }
  const cand = [];
  for (const f of all) if (frSlotOf(f) < 0 && frRunnable(f) && frVis(f) && !frBlocked(f)) cand.push([f, 2]);
  for (const w of FR.want) if (frSlotOf(w) < 0 && frRunnable(w) && !frBlocked(w) && cand.every((c) => c[0] !== w)) cand.push([w, 1]);
  for (const f of all) if (frSlotOf(f) < 0 && frRunnable(f) && !frBlocked(f) && cand.every((c) => c[0] !== f)) cand.push([f, 0]);
  for (const [f, wanted] of cand) {
    let si = FR.S.findIndex((S) => !S.run);
    if (si < 0 && wanted === 2) { for (let k = 0; k < FR.S.length; k++) { const S = FR.S[k]; if (S.run && S.loaded && S.failed) { si = k; break; } } }
    if (si < 0 && wanted === 2) { for (let k = 0; k < FR.S.length; k++) { const S = FR.S[k]; if (S.run && !frVis(S.run) && (S.loaded || now - S.t0 > 3000)) { si = k; break; } } }
    if (si < 0 && wanted) { let best = -1; for (let k = 0; k < FR.S.length; k++) { const S = FR.S[k]; if (!S.loaded) continue; if (!S.failed && now - S.act < 1500) continue; if (wanted === 1 && S.run && frVis(S.run)) continue; if (best < 0 || S.act < FR.S[best].act) best = k; } si = best; }
    if (si < 0) continue;
    frOpen(f, si);
  }
};
G.setInterval(() => { frameScan(); framePoll(); }, 40);
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
const fdBound = (fd) => { if (!fd.__mb) { let r = ''; const cs = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'; for (let i = 0; i < 16; i++) r += cs[(Math.random() * cs.length) | 0]; val(fd, '__mb', '----NoovexFormBoundary' + r); } return fd.__mb; };
const fdBytes = (fd) => {
  const B = fdBound(fd), parts = [], esc = (x) => String(x).replace(/"/g, '%22').replace(/\r/g, '%0D').replace(/\n/g, '%0A');
  for (const [k, v] of fd._l) {
    if (v instanceof Blob) { parts.push(te.encode('--' + B + '\r\nContent-Disposition: form-data; name="' + esc(k) + '"; filename="' + esc(v.name !== undefined ? v.name : 'blob') + '"\r\nContent-Type: ' + (v.type || 'application/octet-stream') + '\r\n\r\n')); parts.push(v._bytes()); parts.push(te.encode('\r\n')); }
    else parts.push(te.encode('--' + B + '\r\nContent-Disposition: form-data; name="' + esc(k) + '"\r\n\r\n' + String(v).replace(/\r?\n|\r/g, '\r\n') + '\r\n'));
  }
  parts.push(te.encode('--' + B + '--\r\n'));
  let n = 0; for (const p of parts) n += p.length; const o = new Uint8Array(n); let k = 0; for (const p of parts) { o.set(p, k); k += p.length; } return o;
};
const bodyRaw = b => b == null ? null : b instanceof FormData ? fdBytes(b) : b instanceof Blob ? (b._u8 !== null ? b._u8 : b._str) : u8of(b) || bodyToString(b);
const bodyToString = b => b == null ? null : typeof b === 'string' ? b : b instanceof URLSearchParams ? b.toString() : b instanceof FormData ? new URLSearchParams(b._l.map(p => [p[0], String(p[1])])).toString() : b instanceof Blob ? b._s : b.byteLength !== undefined ? new TextDecoder().decode(b) : String(b);
const bodyType = b => b instanceof FormData ? 'multipart/form-data; boundary=' + fdBound(b) : b instanceof URLSearchParams ? 'application/x-www-form-urlencoded;charset=UTF-8' : typeof b === 'string' ? 'text/plain;charset=UTF-8' : b instanceof Blob ? b.type : null;
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
  constructor(input, init) { init = init || {}; this.url = input instanceof Request ? input.url : N.resolve(String(input)); this.method = (init.method || (input instanceof Request ? input.method : 'GET')).toUpperCase(); this.headers = new Headers(init.headers || (input instanceof Request ? input.headers : undefined)); this._body = init.body !== undefined ? init.body : (input instanceof Request ? input._body : null); this.signal = init.signal || null; this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors'; this.cache = init.cache || 'default'; const src = input instanceof Request ? input : {}; this.redirect = init.redirect || src.redirect || 'follow'; this.referrer = init.referrer !== undefined ? String(init.referrer) : (src.referrer || 'about:client'); this.referrerPolicy = init.referrerPolicy || src.referrerPolicy || ''; this.integrity = init.integrity || src.integrity || ''; this.keepalive = !!(init.keepalive !== undefined ? init.keepalive : src.keepalive); this.priority = init.priority || src.priority || 'auto'; this.destination = ''; this.isHistoryNavigation = false; this.duplex = init.duplex || 'half'; }
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
const hdrBlock = (h) => { let o = ''; const add = (k, v) => { k = String(k); v = String(v).replace(/[\r\n]+/g, ' '); if (/^[A-Za-z0-9!#$%&'*+.^_`|~-]+$/.test(k)) o += k + ': ' + v + '\r\n'; }; if (h instanceof Headers) h.forEach((v, k) => add(k, v)); else if (h) for (const k of Object.keys(h)) add(k, h[k]); return o; };
const parseHdrs = (raw) => { const m = new Map(); const order = []; for (const line of String(raw || '').split(/\r?\n/)) { const c = line.indexOf(':'); if (c <= 0) continue; const k = line.slice(0, c).trim().toLowerCase(); const v = line.slice(c + 1).trim(); if (m.has(k)) m.set(k, m.get(k) + (k === 'set-cookie' ? '\n' : ', ') + v); else { m.set(k, v); order.push(k); } } return m; };
const STATUS_TEXT = { 200: 'OK', 201: 'Created', 202: 'Accepted', 204: 'No Content', 206: 'Partial Content', 301: 'Moved Permanently', 302: 'Found', 303: 'See Other', 304: 'Not Modified', 307: 'Temporary Redirect', 308: 'Permanent Redirect', 400: 'Bad Request', 401: 'Unauthorized', 403: 'Forbidden', 404: 'Not Found', 405: 'Method Not Allowed', 409: 'Conflict', 410: 'Gone', 412: 'Precondition Failed', 413: 'Payload Too Large', 415: 'Unsupported Media Type', 429: 'Too Many Requests', 500: 'Internal Server Error', 501: 'Not Implemented', 502: 'Bad Gateway', 503: 'Service Unavailable', 504: 'Gateway Timeout' };
const NETQ = new Map();
let netT = 0;
const netPoll = () => {
  netT = 0;
  let evs = [];
  try { evs = N.netPoll(); } catch (e) {}
  for (const e of evs) {
    const h = NETQ.get(e[0]);
    if (!h) continue;
    try {
      if (e[1] === 1) h.hdr(e[2], e[3], e[4], e[5]);
      else if (e[1] === 2) h.data(new Uint8Array(e[2]));
      else { NETQ.delete(e[0]); h.done(!!e[2]); }
    } catch (x) { try { N.log('Uncaught ' + (x && x.stack ? x.stack : x), 3); } catch (y) {} }
  }
  if (NETQ.size && !netT) netT = N.setTimeout(netPoll, 16);
};
const netKick = () => { if (!netT) netT = N.setTimeout(netPoll, 0); };
G.__netQueue = NETQ;
const utf8Cut = (u8) => { const n = u8.length; let k = n - 1, back = 0; while (k >= 0 && back < 3 && (u8[k] & 0xC0) === 0x80) { k--; back++; } if (k < 0) return n; const c = u8[k]; const need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1; return n - k < need ? k : n; };
const u8cat = (list) => { let n = 0; for (const c of list) n += c.length; const o = new Uint8Array(n); let k = 0; for (const c of list) { o.set(c, k); k += c.length; } return o; };
G.fetch = (input, init) => new Promise((resolve, reject) => {
  init = init || {};
  const req = input instanceof Request ? input : new Request(input, init);
  const body = init.body !== undefined ? init.body : req._body;
  const signal = init.signal || req.signal;
  if (signal && signal.aborted) { reject(signal.reason); return; }
  const go = () => {
    if (signal && signal.aborted) { reject(signal.reason); return; }
    const h = new Headers(init.headers || req.headers);
    const ct = h.get('content-type') || bodyType(body);
    const m = (init.method || req.method || 'GET').toUpperCase();
    if (/^https?:/i.test(req.url)) {
      let id = 0;
      try { id = N.netStart(req.url, m, bodyRaw(body), ct, hdrBlock(h), init.keepalive ? 1 : 0); } catch (e) { id = 0; }
      if (id > 0) {
        let res = null, ctl = null, stream = null, fin = false, doneRes = null, doneRej = null;
        const chunks = [];
        const fail = (e) => { if (!res) reject(e); else { if (ctl) { try { ctl.error(e); } catch (x) {} } if (doneRej) doneRej(e); } };
        const onAbort = () => { N.netAbort(id); NETQ.delete(id); fail(signal.reason); };
        if (signal) signal.addEventListener('abort', onAbort);
        NETQ.set(id, {
          hdr: (st, ctp, fu, hs) => {
            const hm = parseHdrs(hs); const H = new Headers(); for (const [k, v] of hm) if (k !== 'set-cookie' && k !== 'set-cookie2') H.set(k, v); if (!H.has('content-type') && ctp) H.set('content-type', ctp);
            res = new Response(null, { status: st, statusText: STATUS_TEXT[st] || '', headers: H, url: fu });
            res.redirected = fu !== req.url; res.type = 'basic';
            if (!(m === 'HEAD' || st === 204 || st === 304)) {
              res._b = null; res._u8 = null;
              res._pend = new Promise((a, b) => { doneRes = a; doneRej = b; });
              res._pend.catch(() => {});
              Object.defineProperty(res, 'body', { configurable: true, get() { if (!stream) stream = new ReadableStream({ start(c) { ctl = c; if (fin) { if (res._u8 && res._u8.length) c.enqueue(res._u8); c.close(); } else for (const ch of chunks) c.enqueue(ch); } }); return stream; } });
            }
            resolve(res);
          },
          data: (u8) => { chunks.push(u8); if (ctl) { try { ctl.enqueue(u8); } catch (e) {} } },
          done: (ok) => {
            if (signal) signal.removeEventListener('abort', onAbort);
            if (!ok) { fail(new TypeError('Failed to fetch')); return; }
            fin = true;
            if (res && doneRes) { res._u8 = u8cat(chunks); chunks.length = 0; if (ctl) { try { ctl.close(); } catch (e) {} } doneRes(); }
            else if (!res) reject(new TypeError('Failed to fetch'));
          }
        });
        netKick();
        return;
      }
    }
    let r;
    try { r = N.fetch(req.url, m, bodyRaw(body), ct, true, hdrBlock(h)); } catch (e) { reject(new TypeError('Failed to fetch')); return; }
    if (!r[0]) { reject(new TypeError('Failed to fetch')); return; }
    if (signal && signal.aborted) { reject(signal.reason); return; }
    const hm = parseHdrs(r[4]); const hs = new Headers(); for (const [k, v] of hm) if (k !== 'set-cookie' && k !== 'set-cookie2') hs.set(k, v); if (!hs.has('content-type') && r[2]) hs.set('content-type', r[2]);
    const res = new Response(m === 'HEAD' || r[0] === 204 || r[0] === 304 ? null : new Uint8Array(r[1]), { status: r[0], statusText: STATUS_TEXT[r[0]] || '', headers: hs, url: r[3] });
    try { Object.defineProperty(res, 'url', { value: r[3], configurable: true }); } catch (e) {}
    res.redirected = r[3] !== req.url; res.type = 'basic';
    resolve(res);
  };
  if (init.keepalive) go(); else N.setTimeout(go, 0);
});
class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.statusText = ''; this._rt = ''; this.response = ''; this.responseType = ''; this.responseURL = ''; this.responseXML = null; this.timeout = 0; this.withCredentials = false; this._h = {}; this._hn = {}; this._rhm = new Map(); this.upload = new EventTarget(); this.onreadystatechange = null; this.onload = null; this.onerror = null; this.onloadend = null; this.onabort = null; this.ontimeout = null; this.onprogress = null; this.onloadstart = null; this._mime = null; }
  get responseText() { if (this.responseType !== '' && this.responseType !== 'text') throw new DOMException("Failed to read the 'responseText' property from 'XMLHttpRequest': The value is only accessible if the object's 'responseType' is '' or 'text' (was '" + this.responseType + "').", 'InvalidStateError'); return this._rt; }
  set responseText(v) { this._rt = v; }
  open(m, u, async) { this._m = String(m).toUpperCase(); this._u = N.resolve(String(u)); this._async = async !== false; this._h = {}; this._hn = {}; this._rhm = new Map(); this._aborted = false; this._sent = false; this.status = 0; this.statusText = ''; this._rt = ''; this.response = this.responseType === '' || this.responseType === 'text' ? '' : null; this.readyState = 1; this._fire('readystatechange'); }
  setRequestHeader(k, v) { if (this.readyState !== 1 || this._sent) throw new DOMException("Failed to execute 'setRequestHeader' on 'XMLHttpRequest': The object's state must be OPENED.", 'InvalidStateError'); const lk = String(k).toLowerCase(); const vv = String(v).trim(); if (this._h[lk] !== undefined) this._h[lk] += ', ' + vv; else { this._h[lk] = vv; this._hn[lk] = String(k); } }
  getResponseHeader(k) { if (this.readyState < 2) return null; const lk = String(k).toLowerCase(); if (lk === 'set-cookie' || lk === 'set-cookie2') return null; const v = this._rhm.get(lk); return v === undefined ? null : v; }
  getAllResponseHeaders() { if (this.readyState < 2) return ''; const ks = [...this._rhm.keys()].filter((k) => k !== 'set-cookie' && k !== 'set-cookie2').sort(); return ks.map((k) => k + ': ' + this._rhm.get(k) + '\r\n').join(''); }
  overrideMimeType(m) { this._mime = String(m); }
  _net(id, bin, blen, hasUp) {
    this._nid = id;
    const chunks = []; let total = 0, carry = null, lastP = 0, tl = 0;
    const dec = (u8) => { if (carry) { const m2 = new Uint8Array(carry.length + u8.length); m2.set(carry); m2.set(u8, carry.length); u8 = m2; carry = null; } const cut = utf8Cut(u8); if (cut < u8.length) { carry = u8.slice(cut); u8 = u8.subarray(0, cut); } return u8.length ? N.utf8dec(u8) : ''; };
    if (this.timeout > 0) tl = N.setTimeout(() => { if (this._nid !== id) return; N.netAbort(id); NETQ.delete(id); this._nid = 0; this._aborted = true; this.readyState = 4; this.status = 0; this._fire('readystatechange'); this._fire('timeout'); this._fire('loadend'); }, this.timeout);
    this._rt = '';
    NETQ.set(id, {
      hdr: (st, ct, fu, hs) => {
        if (this._aborted) return;
        if (hasUp) { this._fire('progress', blen, blen, this.upload); this._fire('load', blen, blen, this.upload); this._fire('loadend', blen, blen, this.upload); }
        this.status = st; this.statusText = STATUS_TEXT[st] || ''; this.responseURL = fu;
        this._rhm = parseHdrs(hs); if (!this._rhm.has('content-type') && ct) this._rhm.set('content-type', ct);
        this._ct = ct;
        this.readyState = 2; this._fire('readystatechange');
      },
      data: (u8) => {
        if (this._aborted) return;
        chunks.push(u8); total += u8.length;
        if (!bin) this._rt += dec(u8);
        this.readyState = 3; this._fire('readystatechange');
        const now = Date.now(); if (now - lastP >= 50) { lastP = now; this._fire('progress', total, 0); }
      },
      done: (ok) => {
        if (tl) N.clearTimer(tl);
        this._nid = 0;
        if (this._aborted) return;
        if (!ok) { this.readyState = 4; this.status = 0; this._fire('readystatechange'); this._fire('error'); this._fire('loadend'); return; }
        if (carry && !bin) { this._rt += N.utf8dec(carry); carry = null; }
        const ctp = this._ct || '';
        if (this.readyState < 3) { this.readyState = 3; this._fire('readystatechange'); }
        const text = bin ? '' : this._rt;
        if (this.responseType === 'json') { try { this.response = JSON.parse(text); } catch (e) { this.response = null; } }
        else if (this.responseType === 'document') { this.response = this.responseXML = N.parseDoc(text); }
        else if (this.responseType === 'arraybuffer') { const a = u8cat(chunks); this.response = a.buffer; }
        else if (this.responseType === 'blob') this.response = new Blob([u8cat(chunks)], { type: this._mime || ctp });
        else this.response = text;
        chunks.length = 0;
        if (!bin && this.responseType !== 'document' && /xml/.test(this._mime || ctp)) { try { this.responseXML = N.parseDoc(text); } catch (e) {} }
        this._fire('progress', total, total);
        this.readyState = 4; this._fire('readystatechange');
        this._fire('load', total, total);
        this._fire('loadend', total, total);
      }
    });
    netKick();
  }
  abort() { if (this._nid) { N.netAbort(this._nid); NETQ.delete(this._nid); this._nid = 0; } if (this._sent && this.readyState !== 4 && !this._aborted) { this._aborted = true; this.readyState = 4; this._fire('readystatechange'); this._fire('abort'); this._fire('loadend'); } this._aborted = true; this.readyState = 0; this.status = 0; }
  _fire(t, l, tot, tgt) { const ev = new ProgressEvent(t, { lengthComputable: tot > 0, loaded: l || 0, total: tot || 0 }); run(tgt || this, ev); }
  send(body) {
    if (this.readyState !== 1 || this._sent) throw new DOMException("Failed to execute 'send' on 'XMLHttpRequest': The object's state must be OPENED.", 'InvalidStateError');
    if (this._m === 'GET' || this._m === 'HEAD') body = null;
    this._sent = true;
    const hasUp = body != null && body !== '';
    const go = () => {
      if (this._aborted) return;
      const bin = this.responseType === 'arraybuffer' || this.responseType === 'blob';
      const raw = bodyRaw(body);
      const blen = raw == null ? 0 : (typeof raw === 'string' ? raw.length : raw.byteLength || 0);
      if (hasUp) { this._fire('loadstart', 0, blen, this.upload); }
      const hb = {}; for (const k of Object.keys(this._h)) if (k !== 'content-type') hb[this._hn[k] || k] = this._h[k];
      if (this._async && /^https?:/i.test(this._u)) {
        let id = 0;
        try { id = N.netStart(this._u, this._m, raw, this._h['content-type'] || bodyType(body), hdrBlock(hb)); } catch (e) { id = 0; }
        if (id > 0) { this._net(id, bin, blen, hasUp); return; }
      }
      let r;
      try { r = N.fetch(this._u, this._m, raw, this._h['content-type'] || bodyType(body), bin, hdrBlock(hb)); } catch (e) { r = [0, bin ? new ArrayBuffer(0) : '', '', this._u, '']; }
      if (this._aborted) return;
      if (hasUp) { this._fire('progress', blen, blen, this.upload); this._fire('load', blen, blen, this.upload); this._fire('loadend', blen, blen, this.upload); }
      if (!r[0]) { this.readyState = 4; this.status = 0; this._fire('readystatechange'); this._fire('error'); this._fire('loadend'); return; }
      this.status = r[0]; this.statusText = STATUS_TEXT[r[0]] || ''; this.responseURL = r[3];
      this._rhm = parseHdrs(r[4]); if (!this._rhm.has('content-type') && r[2]) this._rhm.set('content-type', r[2]);
      this.readyState = 2; this._fire('readystatechange');
      if (this._aborted) return;
      const text = bin ? '' : r[1];
      const total = bin ? (r[1].byteLength || 0) : text.length;
      this.readyState = 3;
      if (!bin) this._rt = text;
      this._fire('readystatechange');
      if (this._aborted) return;
      this._fire('progress', total, total);
      if (this.responseType === 'json') { try { this.response = JSON.parse(text); } catch (e) { this.response = null; } }
      else if (this.responseType === 'document') { this.response = this.responseXML = N.parseDoc(text); }
      else if (this.responseType === 'arraybuffer') this.response = r[1];
      else if (this.responseType === 'blob') this.response = new Blob([new Uint8Array(r[1])], { type: this._mime || r[2] });
      else this.response = text;
      if (!bin && this.responseType !== 'document' && /xml/.test(this._mime || r[2] || '')) { try { this.responseXML = N.parseDoc(text); } catch (e) {} }
      this.readyState = 4; this._fire('readystatechange');
      this._fire('load', total, total);
      this._fire('loadend', total, total);
    };
    this._fire('loadstart', 0, 0);
    if (this._async) N.setTimeout(go, 0); else go();
  }
}
XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;
G.XMLHttpRequest = XMLHttpRequest;
class CloseEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.code = i.code || 0; this.reason = i.reason || ''; this.wasClean = !!i.wasClean; } }
G.CloseEvent = CloseEvent;
const WSOCKS = new Set();
let wsTimer = 0;
const wsPoll = () => {
  for (const w of WSOCKS) w._poll();
  if (!WSOCKS.size && wsTimer) { G.clearInterval(wsTimer); wsTimer = 0; }
};
G.WebSocket = class WebSocket extends EventTarget {
  constructor(u, protocols) {
    super();
    let url;
    try { url = new URL(String(u), location.href); } catch (e) { throw new DOMException('Invalid URL', 'SyntaxError'); }
    if (url.protocol === 'http:') url.protocol = 'ws:'; else if (url.protocol === 'https:') url.protocol = 'wss:';
    if (url.protocol !== 'ws:' && url.protocol !== 'wss:') throw new DOMException('The URL scheme must be ws or wss', 'SyntaxError');
    if (url.hash) throw new DOMException('The URL must not contain a fragment', 'SyntaxError');
    const ps = protocols === undefined ? [] : Array.isArray(protocols) ? protocols.map(String) : [String(protocols)];
    val(this, '_url', url.href); val(this, '_ps', ps); val(this, '_id', -1); val(this, '_rs', 0); val(this, '_bt', 'blob'); val(this, '_proto', ''); val(this, '_closeSent', 0);
    this.onopen = this.onclose = this.onerror = this.onmessage = null;
    setTimeout(() => this._open(), 0);
  }
  get url() { return this._url; }
  get readyState() { return this._rs; }
  get protocol() { return this._proto; }
  get extensions() { return ''; }
  get bufferedAmount() { return 0; }
  get binaryType() { return this._bt; }
  set binaryType(v) { if (v === 'blob' || v === 'arraybuffer') this._bt = v; }
  _open() {
    if (this._rs !== 0) return;
    const id = N.wsOpen(this._url, this._ps.join(', '), location.origin);
    if (id < 0) { this._rs = 3; run(this, new Event('error')); run(this, new CloseEvent('close', { code: 1006, reason: '', wasClean: false })); return; }
    this._id = id;
    const inf = N.wsInfo(id);
    this._proto = inf ? inf[1] : '';
    this._rs = 1;
    WSOCKS.add(this);
    if (!wsTimer) wsTimer = G.setInterval(wsPoll, 30);
    run(this, new Event('open'));
    this._poll();
  }
  _poll() {
    if (this._id < 0) return;
    for (let k = 0; k < 64; k++) {
      const r = N.wsRecv(this._id);
      if (!r) break;
      if (r[0] === 3) { this._finish(); return; }
      let data = r[1];
      if (r[0] === 2) data = this._bt === 'arraybuffer' ? data : new Blob([new Uint8Array(data)]);
      run(this, new MessageEvent('message', { data, origin: new URL(this._url).origin }));
      if (this._id < 0) return;
    }
  }
  _finish() {
    const inf = N.wsInfo(this._id) || [0, '', 1006, ''];
    N.wsClose(this._id, 0, 1);
    this._id = -1;
    WSOCKS.delete(this);
    const code = inf[2] || 1006;
    this._rs = 3;
    if (code === 1006) run(this, new Event('error'));
    run(this, new CloseEvent('close', { code, reason: inf[3] || '', wasClean: code !== 1006 }));
  }
  send(d) {
    if (this._rs === 0) throw new DOMException('Still in CONNECTING state', 'InvalidStateError');
    if (this._rs !== 1) return;
    let ok;
    if (typeof d === 'string') ok = N.wsSend(this._id, d, 0);
    else if (d instanceof Blob) ok = N.wsSend(this._id, d._bytes(), 1);
    else if (d && (d instanceof ArrayBuffer || ArrayBuffer.isView(d))) ok = N.wsSend(this._id, d, 1);
    else ok = N.wsSend(this._id, String(d), 0);
    if (ok < 0) this._finish();
  }
  close(code, reason) {
    if (code !== undefined && code !== 1000 && !(code >= 3000 && code <= 4999)) throw new DOMException('Invalid close code', 'InvalidAccessError');
    if (reason !== undefined && te.encode(String(reason)).length > 123) throw new DOMException('Reason too long', 'SyntaxError');
    if (this._rs === 2 || this._rs === 3) return;
    if (this._rs === 0) { this._rs = 3; setTimeout(() => { run(this, new Event('error')); run(this, new CloseEvent('close', { code: 1006, wasClean: false })); }, 0); return; }
    this._rs = 2;
    N.wsClose(this._id, code === undefined ? 1000 : code, 0);
    setTimeout(() => { if (this._id >= 0) this._finish(); }, 0);
  }
};
G.WebSocket.CONNECTING = 0; G.WebSocket.OPEN = 1; G.WebSocket.CLOSING = 2; G.WebSocket.CLOSED = 3;
G.EventSource = class EventSource extends EventTarget { constructor(u) { super(); this.url = u; this.readyState = 2; } close() {} };
const BCH = new Map(); G.BroadcastChannel = class BroadcastChannel extends EventTarget { constructor(n) { super(); this.name = String(n); this._dead = false; this.onmessage = null; this.onmessageerror = null; if (!BCH.has(this.name)) BCH.set(this.name, new Set()); BCH.get(this.name).add(this); } postMessage(d) { if (this._dead) throw new DOMException('Channel is closed', 'InvalidStateError'); const c = structuredClone(d); for (const o of [...BCH.get(this.name)]) { if (o === this) continue; setTimeout(() => { if (o._dead) return; const ev = new MessageEvent('message', { data: c, origin: N.origin() }); o.dispatchEvent(ev); }, 0); } } close() { this._dead = true; const s = BCH.get(this.name); if (s) s.delete(this); } };
G.MessagePort = class MessagePort extends EventTarget { constructor() { super(); this._o = null; this._q = []; this._on = false; this._dead = false; this._h = null; } get onmessage() { return this._h; } set onmessage(f) { this._h = typeof f === 'function' ? f : null; if (this._h) this.start(); } start() { if (this._on || this._dead) return; this._on = true; const q = this._q; this._q = []; for (const e of q) this._deliver(e); } _deliver(ev) { setTimeout(() => { if (this._dead) return; this.dispatchEvent(ev); }, 0); } postMessage(d, tr) { if (this._dead) return; if (this._rp) { xpSend(this._rp.route, this._rp.id, d, tr); return; } const o = this._o; if (!o) return; if (o._rel) { xpSend(o._rel.route, o._rel.id, d, tr); return; } let c = d; try { c = structuredClone(d); } catch (e) { if (!(d && typeof d === 'object')) throw e; } const ports = Array.isArray(tr) ? tr.filter((x) => x instanceof G.MessagePort) : (tr && Array.isArray(tr.transfer) ? tr.transfer.filter((x) => x instanceof G.MessagePort) : []); const ev = new MessageEvent('message', { data: c, ports }); if (o._on) o._deliver(ev); else o._q.push(ev); } close() { this._dead = true; this._q = []; } };
G.MessageChannel = class MessageChannel { constructor() { this.port1 = new G.MessagePort(); this.port2 = new G.MessagePort(); this.port1._o = this.port2; this.port2._o = this.port1; } };
const permState = (n) => { const r = N.permGet ? N.permGet(n) : 0; return r === 1 ? 'granted' : r === 2 ? 'denied' : 'prompt'; };
const PERM_ST = new Set();
class PermissionStatus extends EventTarget { constructor(n) { super(); val(this, '_n', n); val(this, '_s', n === 'clipboard-write' || n === 'persistent-storage' || n === 'screen-wake-lock' ? 'granted' : n === 'push' || n === 'midi' || n === 'background-sync' || n === 'local-fonts' || n === 'window-management' || n === 'accelerometer' || n === 'gyroscope' || n === 'magnetometer' || n === 'storage-access' ? 'prompt' : permState(n)); this.onchange = null; PERM_ST.add(this); } get name() { return this._n; } get state() { return this._s; } _upd() { const v = permState(this._n); if (['geolocation', 'notifications', 'camera', 'microphone', 'clipboard-read'].includes(this._n) && v !== this._s) { this._s = v; const ev = new Event('change'); this.dispatchEvent(ev); if (typeof this.onchange === 'function') this.onchange.call(this, ev); } } }
G.PermissionStatus = PermissionStatus;
G.Permissions = class Permissions {};
const permAsk = (n, needAct) => new Promise((res) => {
  const cur = permState(n); if (cur !== 'prompt') { res(cur); return; }
  if (needAct && !(G.__actT && G.__actT > 0 && Date.now() - G.__actT < 5000)) { res('prompt'); return; }
  let tries = 0;
  const ask = () => {
    const id = N.permAsk ? N.permAsk(n) : -2;
    if (id === -3 && tries++ < 400) { setTimeout(ask, 250); return; }
    if (id < 0) { res(id === -1 ? 'granted' : id === -3 ? 'default' : 'denied'); return; }
    const t = setInterval(() => { const a = N.permPoll(id); if (a === 0) return; clearInterval(t); for (const s of PERM_ST) s._upd(); res(a === 1 ? 'granted' : a === 2 && permState(n) === 'denied' ? 'denied' : a === 1 ? 'granted' : 'default'); }, 120);
  };
  ask();
});
G.Notification = class Notification extends EventTarget {
  constructor(title, o) { super(); if (arguments.length < 1) throw new TypeError("Failed to construct 'Notification': 1 argument required, but only 0 present."); o = o || {}; this.title = String(title); this.body = o.body === undefined ? '' : String(o.body); this.tag = o.tag || ''; this.icon = o.icon || ''; this.data = o.data === undefined ? null : o.data; this.dir = o.dir || 'auto'; this.lang = o.lang || ''; this.silent = !!o.silent; this.requireInteraction = !!o.requireInteraction; this.onshow = this.onclick = this.onclose = this.onerror = null;
    setTimeout(() => { if (G.Notification.permission !== 'granted') { this.dispatchEvent(new Event('error')); return; } try { N.notify(this.title, this.body); } catch (e) {} this.dispatchEvent(new Event('show')); }, 0); }
  static requestPermission(cb) { return permAsk('notifications', false).then((st) => { const r = st === 'granted' ? 'granted' : st === 'denied' ? 'denied' : 'default'; if (typeof cb === 'function') { try { cb(r); } catch (e) { reportErr(e); } } return r; }); }
  static get permission() { const s = permState('notifications'); return s === 'granted' ? 'granted' : s === 'denied' ? 'denied' : 'default'; }
  close() { setTimeout(() => this.dispatchEvent(new Event('close')), 0); }
  static get maxActions() { return 0; }
};

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
  disconnect() { this._t = []; this._q = []; const i = MO_LIST.indexOf(this); if (i >= 0) MO_LIST.splice(i, 1); if (!MO_LIST.length && !ceByName.size) N.moOn(0); }
  takeRecords() { const q = this._q; this._q = []; return q; }
}
const mkList = a => { a.item = i => a[i] || null; return a; };
G.__mut = function (kind, target, child, prev, next, name, old) {
  if (ceByName.size) G.__ceMut(kind, target, child, name, old);
  if (kind === 5) return;
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
(function () {
  const K256 = new Uint32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
  function pad(m, be64) {
    const l = m.length, n = ((l + 9 + 63) >> 6) << 6, b = new Uint8Array(n); b.set(m); b[l] = 0x80;
    const dv = new DataView(b.buffer); const bits = l * 8; dv.setUint32(n - 4, bits >>> 0); dv.setUint32(n - 8, Math.floor(bits / 4294967296)); return dv;
  }
  function sha256(m) {
    const dv = pad(m); const h = new Uint32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]); const w = new Uint32Array(64);
    for (let o = 0; o < dv.byteLength; o += 64) {
      for (let i = 0; i < 16; i++) w[i] = dv.getUint32(o + i * 4);
      for (let i = 16; i < 64; i++) { const a = w[i - 15], b = w[i - 2]; w[i] = (((a >>> 7 | a << 25) ^ (a >>> 18 | a << 14) ^ (a >>> 3)) + w[i - 7] + ((b >>> 17 | b << 15) ^ (b >>> 19 | b << 13) ^ (b >>> 10)) + w[i - 16]) >>> 0; }
      let [a, b, c, d, e, f, g, k] = h;
      for (let i = 0; i < 64; i++) {
        const t1 = (k + ((e >>> 6 | e << 26) ^ (e >>> 11 | e << 21) ^ (e >>> 25 | e << 7)) + ((e & f) ^ (~e & g)) + K256[i] + w[i]) >>> 0;
        const t2 = (((a >>> 2 | a << 30) ^ (a >>> 13 | a << 19) ^ (a >>> 22 | a << 10)) + ((a & b) ^ (a & c) ^ (b & c))) >>> 0;
        k = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    const out = new Uint8Array(32), od = new DataView(out.buffer); for (let i = 0; i < 8; i++) od.setUint32(i * 4, h[i]); return out;
  }
  function sha1(m) {
    const dv = pad(m); const h = new Uint32Array([0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0]); const w = new Uint32Array(80);
    for (let o = 0; o < dv.byteLength; o += 64) {
      for (let i = 0; i < 16; i++) w[i] = dv.getUint32(o + i * 4);
      for (let i = 16; i < 80; i++) { const x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >>> 31; }
      let [a, b, c, d, e] = h;
      for (let i = 0; i < 80; i++) {
        const f = i < 20 ? (b & c) | (~b & d) : i < 40 ? b ^ c ^ d : i < 60 ? (b & c) | (b & d) | (c & d) : b ^ c ^ d;
        const k = i < 20 ? 0x5a827999 : i < 40 ? 0x6ed9eba1 : i < 60 ? 0x8f1bbcdc : 0xca62c1d6;
        const t = ((a << 5 | a >>> 27) + f + e + k + w[i]) >>> 0; e = d; d = c; c = (b << 30 | b >>> 2) >>> 0; b = a; a = t;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    const out = new Uint8Array(20), od = new DataView(out.buffer); for (let i = 0; i < 5; i++) od.setUint32(i * 4, h[i]); return out;
  }
  const HASH = { 'SHA-256': [sha256, 64], 'SHA-1': [sha1, 64] };
  function bytes(d) {
    if (d instanceof ArrayBuffer) return new Uint8Array(d.slice(0));
    if (ArrayBuffer.isView(d)) return new Uint8Array(d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength));
    throw new TypeError('Failed to execute: parameter is not of type BufferSource');
  }
  function algName(a) { const n = String(typeof a === 'string' ? a : a && a.name || '').toUpperCase(); return n === 'SHA256' ? 'SHA-256' : n === 'SHA1' ? 'SHA-1' : n; }
  function hashOf(a) { const n = algName(a); if (!HASH[n]) throw new DOMException('Algorithm: Unrecognized name', 'NotSupportedError'); return n; }
  function hmac(hn, key, msg) {
    const [f, bs] = HASH[hn]; let k = key; if (k.length > bs) k = f(k); const kp = new Uint8Array(bs); kp.set(k);
    const ip = new Uint8Array(bs + msg.length), op = new Uint8Array(bs + (hn === 'SHA-256' ? 32 : 20));
    for (let i = 0; i < bs; i++) { ip[i] = kp[i] ^ 0x36; op[i] = kp[i] ^ 0x5c; }
    ip.set(msg, bs); op.set(f(ip), bs); return f(op);
  }
  class CryptoKey { constructor(type, ext, alg, usages, raw) { this.type = type; this.extractable = ext; this.algorithm = alg; this.usages = usages; Object.defineProperty(this, '__raw', { value: raw }); } }
  const P = (fn) => { try { return Promise.resolve(fn()); } catch (e) { return Promise.reject(e); } };
  const subtle = {
    digest(a, d) { return P(() => { const n = hashOf(a); return HASH[n][0](bytes(d)).buffer; }); },
    importKey(fmt, data, alg, ext, usages) { return P(() => {
      const n = algName(alg); if (fmt !== 'raw') throw new DOMException('Unsupported key format', 'NotSupportedError');
      if (n === 'HMAC') { const h = hashOf(alg.hash); return new CryptoKey('secret', !!ext, { name: 'HMAC', hash: { name: h }, length: bytes(data).length * 8 }, usages || [], bytes(data)); }
      if (n === 'PBKDF2') return new CryptoKey('secret', false, { name: 'PBKDF2' }, usages || [], bytes(data));
      throw new DOMException('Algorithm: Unrecognized name', 'NotSupportedError'); }); },
    exportKey(fmt, key) { return P(() => { if (fmt !== 'raw' || !key.extractable) throw new DOMException('key is not extractable', 'InvalidAccessError'); return key.__raw.slice().buffer; }); },
    sign(alg, key, d) { return P(() => { if (algName(alg) !== 'HMAC' || key.algorithm.name !== 'HMAC') throw new DOMException('Unsupported', 'NotSupportedError'); return hmac(key.algorithm.hash.name, key.__raw, bytes(d)).buffer; }); },
    verify(alg, key, sig, d) { return P(() => { const a = hmac(key.algorithm.hash.name, key.__raw, bytes(d)), b = bytes(sig); if (a.length !== b.length) return false; let x = 0; for (let i = 0; i < a.length; i++) x |= a[i] ^ b[i]; return x === 0; }); },
    deriveBits(alg, key, len) { return P(() => {
      if (algName(alg) !== 'PBKDF2') throw new DOMException('Unsupported', 'NotSupportedError');
      const hn = hashOf(alg.hash), salt = bytes(alg.salt), it = alg.iterations | 0, hl = hn === 'SHA-256' ? 32 : 20, n = len >> 3, out = new Uint8Array(n);
      for (let blk = 1, o = 0; o < n; blk++) { const s = new Uint8Array(salt.length + 4); s.set(salt); new DataView(s.buffer).setUint32(salt.length, blk); let u = hmac(hn, key.__raw, s); const t = u.slice(); for (let i = 1; i < it; i++) { u = hmac(hn, key.__raw, u); for (let j = 0; j < hl; j++) t[j] ^= u[j]; } out.set(t.subarray(0, Math.min(hl, n - o)), o); o += hl; }
      return out.buffer; }); },
    generateKey(alg, ext, usages) { return P(() => { if (algName(alg) !== 'HMAC') throw new DOMException('Unsupported', 'NotSupportedError'); const h = hashOf(alg.hash); const raw = new Uint8Array((alg.length || (h === 'SHA-256' ? 512 : 512)) >> 3); G.crypto.getRandomValues(raw); return new CryptoKey('secret', !!ext, { name: 'HMAC', hash: { name: h }, length: raw.length * 8 }, usages || [], raw); }); },
    encrypt() { return Promise.reject(new DOMException('Algorithm not supported', 'NotSupportedError')); },
    decrypt() { return Promise.reject(new DOMException('Algorithm not supported', 'NotSupportedError')); }
  };
  G.CryptoKey = CryptoKey; G.SubtleCrypto = function SubtleCrypto() { throw new TypeError('Illegal constructor'); };
  if (G.crypto) G.crypto.subtle = subtle;
  function sc(v, seen) {
    if (v === null || typeof v !== 'object') { if (typeof v === 'function' || typeof v === 'symbol') throw new DOMException('could not be cloned', 'DataCloneError'); return v; }
    if (seen.has(v)) return seen.get(v);
    let r;
    if (v instanceof Date) r = new Date(v.getTime());
    else if (v instanceof RegExp) r = new RegExp(v.source, v.flags);
    else if (v instanceof ArrayBuffer) r = v.slice(0);
    else if (ArrayBuffer.isView(v)) { const b = sc(v.buffer, seen); r = v instanceof DataView ? new DataView(b, v.byteOffset, v.byteLength) : new v.constructor(b, v.byteOffset, v.length); }
    else if (v instanceof Map) { r = new Map(); seen.set(v, r); for (const [k, x] of v) r.set(sc(k, seen), sc(x, seen)); return r; }
    else if (v instanceof Set) { r = new Set(); seen.set(v, r); for (const x of v) r.add(sc(x, seen)); return r; }
    else if (v instanceof Error) { r = new v.constructor(v.message); if (v.stack) r.stack = v.stack; }
    else if (typeof Blob !== 'undefined' && v instanceof Blob) r = v;
    else if (v instanceof Boolean || v instanceof Number || v instanceof String) r = Object(v.valueOf());
    else if (Array.isArray(v)) { r = new Array(v.length); seen.set(v, r); for (let i = 0; i < v.length; i++) if (i in v) r[i] = sc(v[i], seen); return r; }
    else { if (typeof Node !== 'undefined' && v instanceof Node) throw new DOMException('could not be cloned', 'DataCloneError'); r = {}; seen.set(v, r); for (const k of Object.keys(v)) r[k] = sc(v[k], seen); return r; }
    seen.set(v, r); return r;
  }
  G.structuredClone = (v, o) => sc(v, new Map());
  if (typeof WeakRef === 'function') { const WR = WeakRef; const kd = []; let kq = 0; const keep = o => { kd.push(o); if (!kq) { kq = 1; setTimeout(() => { kd.length = 0; kq = 0; }, 0); } }; const W = class WeakRef extends WR { constructor(t) { super(t); keep(t); } deref() { const v = super.deref(); if (v !== undefined) keep(v); return v; } }; G.WeakRef = W; }
})();
{
  const sp = P.HTMLElement;
  val(sp, 'assignedNodes', function (o) { let r = this.getRootNode(); const h = r && r.nodeType === 11 ? N.shadowHost(r) : null; if (!h) return []; const nm = this.getAttribute('name') || ''; const out = []; for (const c of h.childNodes) { const sn = c.nodeType === 1 ? (c.getAttribute('slot') || '') : ''; if (sn === nm && (c.nodeType === 1 || c.nodeType === 3)) out.push(c); } return out; });
  val(sp, 'assignedElements', function (o) { return this.assignedNodes(o).filter(n => n.nodeType === 1); });
  val(sp, 'assign', function () {});
}
if (!G.CSSStyleSheet || !G.CSSStyleSheet.prototype.replaceSync) {
  const reapply = sh => { if (sh.__roots) for (const r of sh.__roots) N.setAdopted(r, (r.__ass || []).map(s => s.__text || '').join('\n')); if (sh.__docs) document.adoptedStyleSheets = document.adoptedStyleSheets; };
  G.CSSStyleSheet = class CSSStyleSheet { constructor() { this.__text = ''; this.cssRules = []; this.disabled = false; } replaceSync(t) { this.__text = String(t); this.cssRules = []; reapply(this); } replace(t) { this.replaceSync(t); return Promise.resolve(this); } insertRule(r, i) { this.__text += '\n' + r; this.cssRules.push({ cssText: r }); reapply(this); return i || 0; } deleteRule() {} };
}
def(P.Document, 'adoptedStyleSheets', function () { return this.__ass || []; }, function (v) { v = Array.from(v || []); for (const sh of v) val(sh, '__docs', true); val(this, '__ass', v); const t = (v || []).map(s => s.__text || '').join('\n'); let st = this.__assel; if (!st) { st = document.createElement('style'); val(this, '__assel', st); (document.head || document.documentElement).appendChild(st); } st.textContent = t; });
const cssDeclOk = (p, v) => { p = String(p).trim(); v = String(v).trim(); if (!p || !v) return false; if (!(N.cssKnown ? N.cssKnown(p.startsWith('--') ? p : p.toLowerCase()) : true)) return false; if (p.startsWith('--')) return true; let d = 0; for (const ch of v) { if (ch === '(') d++; else if (ch === ')') { d--; if (d < 0) return false; } } if (d) return false; return !/[;{}!]/.test(v.replace(/!important\s*$/i, '')); };
const cssCondOk = (c) => {
  c = String(c).trim();
  const split = (str, word) => { const out = []; let d = 0, st = 0; const re = new RegExp('^\\s+' + word + '\\s+', 'i'); for (let i = 0; i < str.length; i++) { const ch = str[i]; if (ch === '(') d++; else if (ch === ')') d--; else if (d === 0 && /\s/.test(ch)) { const m = re.exec(str.slice(i)); if (m) { out.push(str.slice(st, i)); i += m[0].length - 1; st = i + 1; } } } out.push(str.slice(st)); return out.length > 1 ? out.map((x) => x.trim()) : null; };
  if (/^not\s/i.test(c)) return !cssCondOk(c.slice(3));
  const ors = split(c, 'or'); if (ors) return ors.some(cssCondOk);
  const ands = split(c, 'and'); if (ands) return ands.every(cssCondOk);
  let m = /^selector\(([\s\S]*)\)$/i.exec(c); if (m) { try { document.createDocumentFragment().querySelector(m[1]); return true; } catch (e) { return false; } }
  if (/^font-tech\(|^font-format\(/i.test(c)) return true;
  if (c[0] !== '(' && /^[-a-zA-Z]+\s*:/.test(c)) { const i = c.indexOf(':'); return cssDeclOk(c.slice(0, i), c.slice(i + 1)); }
  if (c[0] === '(' && c[c.length - 1] === ')') { const inner = c.slice(1, -1).trim(); const i = inner.indexOf(':'); if (i < 0 || inner[0] === '(' || /^(not|selector)\b/i.test(inner)) return cssCondOk(inner); return cssDeclOk(inner.slice(0, i), inner.slice(i + 1)); }
  return false;
};
G.CSS = { supports(p, v) { if (arguments.length >= 2) return cssDeclOk(p, v); return cssCondOk(p); }, escape(s) { return String(s).replace(/[^a-zA-Z0-9_ -￿-]/g, c => '\\' + c); }, registerProperty(def) { if (!def || typeof def.name !== 'string' || !def.name.startsWith('--')) throw new DOMException("Failed to execute 'registerProperty' on 'CSS': The name provided is not a valid custom property name.", 'SyntaxError'); if (def.initialValue !== undefined && N.cssRegProp) N.cssRegProp(def.name, String(def.initialValue)); } };
{
  const SP = P.SVGElement;
  const num = (el, a, d = 0) => { const v = parseFloat(el.getAttribute(a)); return isNaN(v) ? d : v; };
  const fontOf = el => { let cs; try { cs = getComputedStyle(el); } catch (e) { cs = {}; } const fw = cs.fontWeight, bold = fw === 'bold' || +fw >= 600; const fs = el.getAttribute && el.getAttribute('font-size'); return cvFont((bold ? 'bold ' : '') + (cs.fontStyle === 'italic' ? 'italic ' : '') + (fs ? (/[a-z%]/.test(fs) ? fs : fs + 'px') : (cs.fontSize || '16px')) + ' ' + (cs.fontFamily || 'sans-serif')); };
  const txtw = (el, t) => { const [f, z] = fontOf(el); return N.textWidth(f, z, t === undefined ? (el.textContent || '') : t); };
  function parseT(t) {
    const m = [1, 0, 0, 1, 0, 0];
    if (!t) return m;
    const mul = (a, b) => [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]];
    let r = m, x; const re = /(matrix|translate|scale|rotate|skewX|skewY)\s*\(([^)]*)\)/g;
    while ((x = re.exec(t))) {
      const a = x[2].split(/[\s,]+/).filter(Boolean).map(Number);
      let k;
      if (x[1] === 'matrix') k = a.length >= 6 ? a.slice(0, 6) : m;
      else if (x[1] === 'translate') k = [1, 0, 0, 1, a[0] || 0, a[1] || 0];
      else if (x[1] === 'scale') k = [a[0] || 1, 0, 0, a.length > 1 ? a[1] : (a[0] || 1), 0, 0];
      else if (x[1] === 'rotate') { const g = (a[0] || 0) * Math.PI / 180, c = Math.cos(g), s = Math.sin(g); k = [c, s, -s, c, 0, 0]; if (a.length > 2) k = mul(mul([1, 0, 0, 1, a[1], a[2]], k), [1, 0, 0, 1, -a[1], -a[2]]); }
      else if (x[1] === 'skewX') k = [1, 0, Math.tan((a[0] || 0) * Math.PI / 180), 1, 0, 0];
      else k = [1, Math.tan((a[0] || 0) * Math.PI / 180), 0, 1, 0, 0];
      r = mul(r, k);
    }
    return r;
  }
  function pathPts(d) {
    const pts = []; const toks = String(d || '').match(/[a-zA-Z]|-?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?/g) || [];
    let i = 0, cx = 0, cy = 0, sx = 0, sy = 0, cmd = '';
    const n = () => +toks[i++];
    while (i < toks.length) {
      if (/[a-zA-Z]/.test(toks[i])) cmd = toks[i++];
      const rel = cmd === cmd.toLowerCase(), C = cmd.toUpperCase();
      if (C === 'Z') { cx = sx; cy = sy; pts.push([cx, cy, 1]); if (i < toks.length && !/[a-zA-Z]/.test(toks[i])) i++; continue; }
      if (i >= toks.length || /[a-zA-Z]/.test(toks[i])) { if (C !== 'Z') i++; continue; }
      const ox = rel ? cx : 0, oy = rel ? cy : 0;
      if (C === 'M' || C === 'L' || C === 'T') { cx = ox + n(); cy = oy + n(); if (C === 'M') { sx = cx; sy = cy; pts.push([cx, cy, 0]); cmd = rel ? 'l' : 'L'; } else pts.push([cx, cy, 1]); }
      else if (C === 'H') { cx = ox + n(); pts.push([cx, cy, 1]); }
      else if (C === 'V') { cy = oy + n(); pts.push([cx, cy, 1]); }
      else if (C === 'C') { const a = [ox + n(), oy + n(), ox + n(), oy + n()]; const ex = ox + n(), ey = oy + n(); for (let t = 1; t <= 8; t++) { const u = t / 8, v = 1 - u; pts.push([v * v * v * cx + 3 * v * v * u * a[0] + 3 * v * u * u * a[2] + u * u * u * ex, v * v * v * cy + 3 * v * v * u * a[1] + 3 * v * u * u * a[3] + u * u * u * ey, 1]); } cx = ex; cy = ey; }
      else if (C === 'S' || C === 'Q') { const a = [ox + n(), oy + n()]; const ex = ox + n(), ey = oy + n(); for (let t = 1; t <= 8; t++) { const u = t / 8, v = 1 - u; pts.push([v * v * cx + 2 * v * u * a[0] + u * u * ex, v * v * cy + 2 * v * u * a[1] + u * u * ey, 1]); } cx = ex; cy = ey; }
      else if (C === 'A') { n(); n(); n(); n(); n(); const ex = ox + n(), ey = oy + n(); pts.push([(cx + ex) / 2, (cy + ey) / 2, 1]); pts.push([ex, ey, 1]); cx = ex; cy = ey; }
      else i++;
    }
    return pts;
  }
  const EMPTY = () => ({ x: 0, y: 0, width: 0, height: 0, e: 1 });
  function boxOf(pts) { if (!pts.length) return EMPTY(); let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity; for (const p of pts) { if (p[0] < x0) x0 = p[0]; if (p[0] > x1) x1 = p[0]; if (p[1] < y0) y0 = p[1]; if (p[1] > y1) y1 = p[1]; } return { x: x0, y: y0, width: x1 - x0, height: y1 - y0 }; }
  function xform(b, m) { if (b.e) return b; const c = [[b.x, b.y], [b.x + b.width, b.y], [b.x, b.y + b.height], [b.x + b.width, b.y + b.height]].map(([x, y]) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]); return boxOf(c); }
  function unionB(a, b) { if (a.e) return b; if (b.e) return a; const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y); return { x, y, width: Math.max(a.x + a.width, b.x + b.width) - x, height: Math.max(a.y + a.height, b.y + b.height) - y }; }
  function bbox(el) {
    const t = el.localName;
    if (t === 'rect' || t === 'image' || t === 'foreignObject' || t === 'use') { let w = num(el, 'width'), h = num(el, 'height'); if (t === 'foreignObject' && !w && !h && el.firstElementChild) { const r = el.firstElementChild.getBoundingClientRect(); w = r.width; h = r.height; } return { x: num(el, 'x'), y: num(el, 'y'), width: w, height: h }; }
    if (t === 'circle') { const r = num(el, 'r'); return { x: num(el, 'cx') - r, y: num(el, 'cy') - r, width: 2 * r, height: 2 * r }; }
    if (t === 'ellipse') { const rx = num(el, 'rx'), ry = num(el, 'ry'); return { x: num(el, 'cx') - rx, y: num(el, 'cy') - ry, width: 2 * rx, height: 2 * ry }; }
    if (t === 'line') return boxOf([[num(el, 'x1'), num(el, 'y1')], [num(el, 'x2'), num(el, 'y2')]]);
    if (t === 'polyline' || t === 'polygon') { const a = (el.getAttribute('points') || '').split(/[\s,]+/).filter(Boolean).map(Number); const p = []; for (let i = 0; i + 1 < a.length; i += 2) p.push([a[i], a[i + 1]]); return boxOf(p); }
    if (t === 'path') return boxOf(pathPts(el.getAttribute('d')));
    if (t === 'text' || t === 'tspan' || t === 'textPath') {
      const [f, z] = fontOf(el);
      const kids = el.children ? Array.from(el.children).filter(c => c.localName === 'tspan') : [];
      if (t === 'text' && kids.length) { let b = EMPTY(); let y = num(el, 'y'); for (const k of kids) { const kb = bbox(k); if (!k.hasAttribute('y') && !k.hasAttribute('dy')) {} b = unionB(b, kb); } const own = Array.from(el.childNodes).filter(n => n.nodeType === 3).map(n => n.data).join('').trim(); if (own) b = unionB(b, { x: num(el, 'x'), y: y - z * 0.8, width: N.textWidth(f, z, own), height: z * 1.2 }); return b; }
      const w = N.textWidth(f, z, (el.textContent || '').replace(/\s+/g, ' ').trim());
      let x = num(el, 'x', t === 'tspan' && el.parentNode ? num(el.parentNode, 'x') : 0); let y = num(el, 'y', t === 'tspan' && el.parentNode ? num(el.parentNode, 'y') : 0) + num(el, 'dy') * (/em/.test(el.getAttribute('dy') || '') ? z : 1);
      let anc = el.getAttribute('text-anchor') || (el.parentNode && el.parentNode.getAttribute && el.parentNode.getAttribute('text-anchor')); if (!anc) { try { anc = getComputedStyle(el).textAnchor; } catch (e) {} }
      if (anc === 'middle') x -= w / 2; else if (anc === 'end') x -= w;
      const db = el.getAttribute('dominant-baseline') || el.getAttribute('alignment-baseline');
      const top = db === 'middle' || db === 'central' ? y - z * 0.6 : db === 'hanging' || db === 'text-before-edge' ? y : y - z * 0.9;
      return { x, y: top, width: w, height: z * 1.2 };
    }
    let b = EMPTY();
    for (const c of el.children || []) { if (c.localName === 'defs' || c.localName === 'title' || c.localName === 'desc' || c.localName === 'style' || c.localName === 'clipPath' || c.localName === 'mask' || c.localName === 'marker' || c.localName === 'linearGradient' || c.localName === 'radialGradient' || c.localName === 'pattern' || c.localName === 'symbol') continue; let cb = bbox(c); const tr = c.getAttribute && c.getAttribute('transform'); if (tr) cb = xform(cb, parseT(tr)); b = unionB(b, cb); }
    return b;
  }
  val(SP, 'getBBox', function () { const b = bbox(this); return new DOMRect(b.x || 0, b.y || 0, b.width || 0, b.height || 0); });
  val(SP, 'getComputedTextLength', function () { return txtw(this, (this.textContent || '').replace(/\s+/g, ' ').trim()); });
  val(SP, 'getSubStringLength', function (s, n) { return txtw(this, (this.textContent || '').substr(s, n)); });
  val(SP, 'getNumberOfChars', function () { return (this.textContent || '').length; });
  const polyOf = el => { const t = el.localName; if (t === 'path') return pathPts(el.getAttribute('d')); if (t === 'line') return [[num(el, 'x1'), num(el, 'y1'), 0], [num(el, 'x2'), num(el, 'y2'), 1]]; if (t === 'polyline' || t === 'polygon') { const a = (el.getAttribute('points') || '').split(/[\s,]+/).filter(Boolean).map(Number); const p = []; for (let i = 0; i + 1 < a.length; i += 2) p.push([a[i], a[i + 1], i ? 1 : 0]); if (t === 'polygon' && p.length) p.push([p[0][0], p[0][1], 1]); return p; } if (t === 'rect') { const x = num(el, 'x'), y = num(el, 'y'), w = num(el, 'width'), h = num(el, 'height'); return [[x, y, 0], [x + w, y, 1], [x + w, y + h, 1], [x, y + h, 1], [x, y, 1]]; } if (t === 'circle' || t === 'ellipse') { const cx = num(el, 'cx'), cy = num(el, 'cy'), rx = t === 'circle' ? num(el, 'r') : num(el, 'rx'), ry = t === 'circle' ? rx : num(el, 'ry'); const p = []; for (let i = 0; i <= 64; i++) { const g = i / 64 * 2 * Math.PI; p.push([cx + rx * Math.cos(g), cy + ry * Math.sin(g), i ? 1 : 0]); } return p; } return []; };
  val(SP, 'getTotalLength', function () { const p = polyOf(this); let l = 0; for (let i = 1; i < p.length; i++) if (p[i][2]) l += Math.hypot(p[i][0] - p[i - 1][0], p[i][1] - p[i - 1][1]); return l; });
  val(SP, 'getPointAtLength', function (d) { const p = polyOf(this); if (!p.length) return new DOMPoint(0, 0); let l = 0; for (let i = 1; i < p.length; i++) { if (!p[i][2]) continue; const s = Math.hypot(p[i][0] - p[i - 1][0], p[i][1] - p[i - 1][1]); if (l + s >= d && s > 0) { const u = (d - l) / s; return new DOMPoint(p[i - 1][0] + (p[i][0] - p[i - 1][0]) * u, p[i - 1][1] + (p[i][1] - p[i - 1][1]) * u); } l += s; } const q = p[p.length - 1]; return new DOMPoint(q[0], q[1]); });
  val(SP, 'isPointInFill', function () { return false; });
  val(SP, 'isPointInStroke', function () { return false; });
  const mat = m => ({ a: m[0], b: m[1], c: m[2], d: m[3], e: m[4], f: m[5], multiply(o) { return mat([m[0] * o.a + m[2] * o.b, m[1] * o.a + m[3] * o.b, m[0] * o.c + m[2] * o.d, m[1] * o.c + m[3] * o.d, m[0] * o.e + m[2] * o.f + m[4], m[1] * o.e + m[3] * o.f + m[5]]); }, inverse() { const det = m[0] * m[3] - m[1] * m[2] || 1; return mat([m[3] / det, -m[1] / det, -m[2] / det, m[0] / det, (m[2] * m[5] - m[3] * m[4]) / det, (m[1] * m[4] - m[0] * m[5]) / det]); }, translate(x = 0, y = 0) { return this.multiply(mat([1, 0, 0, 1, x, y])); }, scale(s = 1) { return this.multiply(mat([s, 0, 0, s, 0, 0])); } });
  const ctm = (el, screen) => { let m = [1, 0, 0, 1, 0, 0]; const chain = []; for (let q = el; q && q.localName !== 'svg' && q.nodeType === 1; q = q.parentNode) chain.unshift(q); const root = el.ownerSVGElement || (el.localName === 'svg' ? el : null); let r = mat(m); if (screen && root) { const br = root.getBoundingClientRect(); r = mat([1, 0, 0, 1, br.left, br.top]); } for (const q of chain) { const tr = q.getAttribute('transform'); if (tr) r = r.multiply(mat(parseT(tr))); } return r; };
  val(SP, 'getCTM', function () { return ctm(this, false); });
  val(SP, 'getScreenCTM', function () { return ctm(this, true); });
  def(SP, 'ownerSVGElement', function () { let q = this.parentNode; while (q && q.nodeType === 1) { if (q.localName === 'svg') return q; q = q.parentNode; } return null; });
  def(SP, 'viewportElement', function () { return this.ownerSVGElement; });
  val(SP, 'createSVGPoint', function () { return { x: 0, y: 0, matrixTransform(m) { return { x: m.a * this.x + m.c * this.y + m.e, y: m.b * this.x + m.d * this.y + m.f, matrixTransform: this.matrixTransform }; } }; });
  val(SP, 'createSVGMatrix', function () { return mat([1, 0, 0, 1, 0, 0]); });
  val(SP, 'createSVGRect', function () { return new DOMRect(); });
  val(SP, 'createSVGTransform', function () { return { matrix: mat([1, 0, 0, 1, 0, 0]), setTranslate() {}, setScale() {}, setRotate() {} }; });
  if (!Object.getOwnPropertyDescriptor(SP, 'transform')) def(SP, 'transform', function () { const el = this; return { baseVal: { numberOfItems: el.getAttribute('transform') ? 1 : 0, consolidate() { const t = el.getAttribute('transform'); return t ? { matrix: mat(parseT(t)) } : null; }, getItem() { return { matrix: mat(parseT(el.getAttribute('transform'))) }; }, appendItem() {}, clear() {}, createSVGTransformFromMatrix(m) { return { matrix: m }; } } }; });
  const gbcr = P.Element.getBoundingClientRect;
  val(P.Element, 'getBoundingClientRect', function () {
    const r = gbcr.call(this);
    if (r && (r.width || r.height)) return r;
    let fo = null; for (let q = this; q && q.nodeType === 1; q = q.parentNode) if (q.localName === 'foreignObject') { fo = q; break; }
    if (!fo && this.namespaceURI === 'http://www.w3.org/2000/svg' && this.localName !== 'svg') { const b = bbox(this); const root = this.ownerSVGElement; const o = root ? gbcr.call(root) : { left: 0, top: 0 }; const m = ctm(this, false); const tb = xform(b, [m.a, m.b, m.c, m.d, m.e, m.f]); return new DOMRect((o.left || 0) + (tb.x || 0), (o.top || 0) + (tb.y || 0), tb.width || 0, tb.height || 0); }
    if (!fo) return r;
    const text = (this.textContent || '').replace(/\s+/g, ' ').trim(); if (!text) return r;
    const [f, z] = fontOf(this); const mw = parseFloat((this.style && this.style.maxWidth) || '') || 200;
    let w = N.textWidth(f, z, text), lines = 1; if (w > mw && /white-space:\s*break-spaces|normal/.test(this.getAttribute('style') || 'normal')) { lines = Math.ceil(w / mw); w = mw; }
    return new DOMRect(0, 0, w, z * 1.5 * lines);
  });
}
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
G.Worker = undefined; G.SharedWorker = undefined;
(function () {
  const ERR = (n, m) => new DOMException(m || n, n);
  const typeRank = k => typeof k === 'number' ? 1 : k instanceof Date ? 2 : typeof k === 'string' ? 3 : (k instanceof ArrayBuffer || ArrayBuffer.isView(k)) ? 4 : Array.isArray(k) ? 5 : 0;
  function valid(k) { const t = typeRank(k); if (!t) return false; if (t === 1) return !isNaN(k); if (t === 2) return !isNaN(k.getTime()); if (t === 5) return k.every(valid); return true; }
  function cmp(a, b) {
    const ta = typeRank(a), tb = typeRank(b);
    if (!ta || !tb) throw ERR('DataError', 'The parameter is not a valid key.');
    if (ta !== tb) return ta < tb ? -1 : 1;
    if (ta === 1) return a < b ? -1 : a > b ? 1 : 0;
    if (ta === 2) { const x = a.getTime(), y = b.getTime(); return x < y ? -1 : x > y ? 1 : 0; }
    if (ta === 3) return a < b ? -1 : a > b ? 1 : 0;
    if (ta === 4) { const x = new Uint8Array(a.buffer || a, a.byteOffset || 0, a.byteLength), y = new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength); for (let i = 0; i < Math.min(x.length, y.length); i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1; return x.length - y.length < 0 ? -1 : x.length > y.length ? 1 : 0; }
    for (let i = 0; i < Math.min(a.length, b.length); i++) { const c = cmp(a[i], b[i]); if (c) return c; }
    return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
  }
  const kclone = k => k instanceof Date ? new Date(k.getTime()) : Array.isArray(k) ? k.map(kclone) : k;
  class IDBKeyRange {
    constructor(l, u, lo, uo) { this.lower = l; this.upper = u; this.lowerOpen = !!lo; this.upperOpen = !!uo; }
    static only(v) { if (!valid(v)) throw ERR('DataError'); return new IDBKeyRange(v, v, false, false); }
    static lowerBound(v, o) { if (!valid(v)) throw ERR('DataError'); return new IDBKeyRange(v, undefined, o, true); }
    static upperBound(v, o) { if (!valid(v)) throw ERR('DataError'); return new IDBKeyRange(undefined, v, true, o); }
    static bound(l, u, lo, uo) { if (!valid(l) || !valid(u)) throw ERR('DataError'); if (cmp(l, u) > 0 || (cmp(l, u) === 0 && (lo || uo))) throw ERR('DataError'); return new IDBKeyRange(l, u, lo, uo); }
    includes(k) { if (this.lower !== undefined) { const c = cmp(k, this.lower); if (c < 0 || (c === 0 && this.lowerOpen)) return false; } if (this.upper !== undefined) { const c = cmp(k, this.upper); if (c > 0 || (c === 0 && this.upperOpen)) return false; } return true; }
  }
  const toRange = q => q === undefined || q === null ? null : q instanceof IDBKeyRange ? q : IDBKeyRange.only(q);
  const inR = (r, k) => !r || r.includes(k);
  function keyAt(v, kp) {
    if (Array.isArray(kp)) { const o = []; for (const p of kp) { const x = keyAt(v, p); if (x === undefined) return undefined; o.push(x); } return o; }
    if (kp === '') return v;
    let o = v; for (const s of String(kp).split('.')) { if (o === null || o === undefined) return undefined; if (s === 'length' && (typeof o === 'string' || Array.isArray(o))) o = o.length; else o = o[s]; }
    return o;
  }
  function setKeyAt(v, kp, k) { const parts = String(kp).split('.'); let o = v; for (let i = 0; i < parts.length - 1; i++) { if (o[parts[i]] === undefined) o[parts[i]] = {}; o = o[parts[i]]; } o[parts[parts.length - 1]] = k; }
  function bsearch(arr, k) { let lo = 0, hi = arr.length; while (lo < hi) { const m = (lo + hi) >> 1; if (cmp(arr[m].k, k) < 0) lo = m + 1; else hi = m; } return lo; }
  const DBS = new Map();
  const strList = a => { const l = Array.from(a).sort(); l.contains = n => l.includes(n); l.item = i => l[i] || null; return l; };
  class IDBRequest extends EventTarget {
    constructor(src, tx) { super(); this.source = src; this.transaction = tx; this.readyState = 'pending'; this._r = undefined; this._e = null; this.onsuccess = null; this.onerror = null; }
    get result() { if (this.readyState !== 'done') throw ERR('InvalidStateError'); return this._r; }
    get error() { if (this.readyState !== 'done') throw ERR('InvalidStateError'); return this._e; }
  }
  class IDBOpenDBRequest extends IDBRequest { constructor() { super(null, null); this.onupgradeneeded = null; this.onblocked = null; } }
  class IDBVersionChangeEvent extends Event { constructor(t, o) { super(t, o); this.oldVersion = (o && o.oldVersion) || 0; this.newVersion = o && o.newVersion !== undefined ? o.newVersion : null; } }
  function fire(target, type, opts) { const e = type === 'upgradeneeded' || type === 'versionchange' || type === 'blocked' ? new IDBVersionChangeEvent(type, opts) : new Event(type, opts || {}); target.dispatchEvent(e); return e; }
  class IDBTransaction extends EventTarget {
    constructor(db, names, mode) { super(); this.db = db; this.mode = mode; this._names = names; this.objectStoreNames = strList(names); this.error = null; this._q = []; this._busy = false; this._done = false; this._aborted = false; this.oncomplete = null; this.onerror = null; this.onabort = null; this.durability = 'default'; this._snap = null; if (mode !== 'readonly') this._snap = snapshot(db._d, names); setTimeout(() => this._pump(), 0); }
    objectStore(n) { if (this._done) throw ERR('InvalidStateError', 'The transaction has finished.'); if (!this._names.includes(n)) throw ERR('NotFoundError', 'No objectStore named ' + n + ' in this transaction'); return new IDBObjectStore(this, n); }
    _req(src, fn) { if (this._done) throw ERR('TransactionInactiveError', 'The transaction has finished.'); const r = new IDBRequest(src, this); this._q.push([r, fn]); return r; }
    _pump() {
      if (this._done) return;
      while (this._q.length) {
        const [r, fn] = this._q.shift();
        let ok = true, val;
        try { val = fn(); } catch (e) { ok = false; r._e = e instanceof DOMException ? e : ERR('UnknownError', String(e)); }
        r.readyState = 'done';
        if (ok) { r._r = val; fire(r, 'success'); }
        else { const ev = fire(r, 'error', { bubbles: true, cancelable: true }); if (!ev.defaultPrevented) { this.error = r._e; this._abort(r._e); return; } }
        if (this._done) return;
      }
      setTimeout(() => { if (this._q.length) this._pump(); else this._finish(); }, 0);
    }
    _finish() { if (this._done) return; this._done = true; this._snap = null; fire(this, 'complete'); if (this.db._closePending && !this.db._txs) {} }
    _abort(err) { if (this._done) return; this._done = true; this._aborted = true; if (this._snap) restore(this.db._d, this._snap); this.error = err || null; const q = this._q.splice(0); setTimeout(() => { for (const [r] of q) { r.readyState = 'done'; r._e = ERR('AbortError', 'The transaction was aborted.'); fire(r, 'error', { bubbles: true, cancelable: true }); } fire(this, 'abort', { bubbles: true }); }, 0); }
    abort() { if (this._done) throw ERR('InvalidStateError'); this._abort(ERR('AbortError')); }
    commit() {}
  }
  function snapshot(d, names) { const s = new Map(); for (const n of names) { const st = d.stores.get(n); if (st) s.set(n, { recs: st.recs.slice(), next: st.next }); } return s; }
  function restore(d, snap) { for (const [n, v] of snap) { const st = d.stores.get(n); if (st) { st.recs = v.recs; st.next = v.next; } } }
  class IDBObjectStore {
    constructor(tx, n) { this.transaction = tx; this.name = n; }
    get _s() { const s = this.transaction.db._d.stores.get(this.name); if (!s) throw ERR('InvalidStateError', 'The object store has been deleted.'); return s; }
    get keyPath() { return this._s.keyPath; }
    get autoIncrement() { return this._s.auto; }
    get indexNames() { return strList(this._s.idx.keys()); }
    _w() { if (this.transaction.mode === 'readonly') throw ERR('ReadOnlyError', 'The transaction is read-only.'); }
    _put(value, key, noOverwrite) {
      const s = this._s;
      let v = structuredClone(value), k;
      if (s.keyPath !== null && s.keyPath !== undefined) { if (key !== undefined) throw ERR('DataError', 'The object store uses in-line keys and the key parameter was provided.'); k = keyAt(v, s.keyPath); if (k === undefined && s.auto) { k = s.next++; setKeyAt(v, s.keyPath, k); } }
      else { k = key; if (k === undefined && s.auto) k = s.next++; }
      if (!valid(k)) throw ERR('DataError', 'Evaluating the object store\'s key path did not yield a value.');
      if (typeof k === 'number' && s.auto && k >= s.next) s.next = Math.floor(k) + 1;
      const i = bsearch(s.recs, k);
      const exists = i < s.recs.length && cmp(s.recs[i].k, k) === 0;
      if (exists && noOverwrite) throw ERR('ConstraintError', 'Key already exists in the object store.');
      for (const [, ix] of s.idx) if (ix.unique) { const ik = keyAt(v, ix.kp); if (ik !== undefined && valid(ik)) for (const r of s.recs) { if (exists && cmp(r.k, k) === 0) continue; const ok2 = keyAt(r.v, ix.kp); if (ok2 !== undefined && valid(ok2) && cmp(ok2, ik) === 0) throw ERR('ConstraintError', 'Unable to add key to index: at least one key does not satisfy the uniqueness requirements.'); } }
      if (exists) s.recs[i].v = v; else s.recs.splice(i, 0, { k: kclone(k), v });
      return kclone(k);
    }
    put(v, k) { this._w(); return this.transaction._req(this, () => this._put(v, k, false)); }
    add(v, k) { this._w(); return this.transaction._req(this, () => this._put(v, k, true)); }
    _sel(q, count, dir) { const r = toRange(q); let out = this._s.recs.filter(x => inR(r, x.k)); if (dir === 'prev' || dir === 'prevunique') out = out.reverse(); if (count) out = out.slice(0, count); return out; }
    get(q) { if (q === undefined) throw new TypeError("Failed to execute 'get' on 'IDBObjectStore': 1 argument required."); return this.transaction._req(this, () => { const x = this._sel(q, 1)[0]; return x ? structuredClone(x.v) : undefined; }); }
    getKey(q) { return this.transaction._req(this, () => { const x = this._sel(q, 1)[0]; return x ? kclone(x.k) : undefined; }); }
    getAll(q, c) { return this.transaction._req(this, () => this._sel(q, c).map(x => structuredClone(x.v))); }
    getAllKeys(q, c) { return this.transaction._req(this, () => this._sel(q, c).map(x => kclone(x.k))); }
    count(q) { return this.transaction._req(this, () => this._sel(q).length); }
    delete(q) { this._w(); return this.transaction._req(this, () => { const r = toRange(q); const s = this._s; s.recs = s.recs.filter(x => !inR(r, x.k)); return undefined; }); }
    clear() { this._w(); return this.transaction._req(this, () => { this._s.recs = []; return undefined; }); }
    openCursor(q, dir) { return openCur(this, this, q, dir, false); }
    openKeyCursor(q, dir) { return openCur(this, this, q, dir, true); }
    createIndex(n, kp, o) { if (this.transaction.mode !== 'versionchange') throw ERR('InvalidStateError', 'Indexes can only be created during a version change.'); const s = this._s; if (s.idx.has(n)) throw ERR('ConstraintError', 'An index with the specified name already exists.'); s.idx.set(n, { kp, unique: !!(o && o.unique), multi: !!(o && o.multiEntry) }); return new IDBIndex(this, n); }
    deleteIndex(n) { if (this.transaction.mode !== 'versionchange') throw ERR('InvalidStateError'); if (!this._s.idx.delete(n)) throw ERR('NotFoundError'); }
    index(n) { if (!this._s.idx.has(n)) throw ERR('NotFoundError', 'The specified index was not found.'); return new IDBIndex(this, n); }
  }
  class IDBIndex {
    constructor(st, n) { this.objectStore = st; this.name = n; }
    get _i() { return this.objectStore._s.idx.get(this.name); }
    get keyPath() { return this._i.kp; } get unique() { return this._i.unique; } get multiEntry() { return this._i.multi; }
    _rows() { const ix = this._i, out = []; for (const r of this.objectStore._s.recs) { const ik = keyAt(r.v, ix.kp); if (ik === undefined) continue; if (ix.multi && Array.isArray(ik)) { for (const e of new Set(ik.filter(valid))) out.push({ ik: e, k: r.k, v: r.v }); } else if (valid(ik)) out.push({ ik, k: r.k, v: r.v }); } out.sort((a, b) => cmp(a.ik, b.ik) || cmp(a.k, b.k)); return out; }
    _sel(q, c, dir) { const r = toRange(q); let o = this._rows().filter(x => inR(r, x.ik)); if (dir === 'prev' || dir === 'prevunique') o.reverse(); if (dir === 'nextunique' || dir === 'prevunique') { const seen = []; o = o.filter(x => { if (seen.some(s => cmp(s, x.ik) === 0)) return false; seen.push(x.ik); return true; }); } if (c) o = o.slice(0, c); return o; }
    get(q) { const st = this.objectStore; return st.transaction._req(this, () => { const x = this._sel(q, 1)[0]; return x ? structuredClone(x.v) : undefined; }); }
    getKey(q) { const st = this.objectStore; return st.transaction._req(this, () => { const x = this._sel(q, 1)[0]; return x ? kclone(x.k) : undefined; }); }
    getAll(q, c) { return this.objectStore.transaction._req(this, () => this._sel(q, c).map(x => structuredClone(x.v))); }
    getAllKeys(q, c) { return this.objectStore.transaction._req(this, () => this._sel(q, c).map(x => kclone(x.k))); }
    count(q) { return this.objectStore.transaction._req(this, () => this._sel(q).length); }
    openCursor(q, dir) { return openCur(this.objectStore, this, q, dir, false); }
    openKeyCursor(q, dir) { return openCur(this.objectStore, this, q, dir, true); }
  }
  class IDBCursor {
    constructor(st, src, rows, dir, keyOnly, req) { this._st = st; this.source = src; this._rows = rows; this._i = -1; this.direction = dir || 'next'; this._ko = keyOnly; this.request = req; this._cur = null; }
    get key() { return this._cur ? kclone(this._cur.ik !== undefined ? this._cur.ik : this._cur.k) : undefined; }
    get primaryKey() { return this._cur ? kclone(this._cur.k) : undefined; }
    _step(n, target) {
      const tx = this._st.transaction, req = this.request;
      req.readyState = 'pending';
      tx._q.push([req, () => {
        let i = this._i + n;
        if (target !== undefined) { while (i < this._rows.length) { const kk = this._rows[i].ik !== undefined ? this._rows[i].ik : this._rows[i].k; const c = cmp(kk, target); if ((this.direction.startsWith('prev') ? c <= 0 : c >= 0)) break; i++; } }
        this._i = i; this._cur = this._rows[i] || null;
        if (this._cur && this._cur.v !== undefined) { const live = this._st._s.recs.find(r => cmp(r.k, this._cur.k) === 0); if (!live) { return this._step0(); } this._cur.v = live.v; }
        return this._cur ? this : null;
      }]);
    }
    _step0() { this._i++; this._cur = this._rows[this._i] || null; return this._cur ? this : null; }
    continue(k) { if (k !== undefined && !valid(k)) throw ERR('DataError'); this._step(1, k); }
    advance(n) { if (!(n > 0)) throw new TypeError('count must be > 0'); this._step(n); }
    continuePrimaryKey(k, pk) { this._step(1, k); }
    update(v) { if (this._st.transaction.mode === 'readonly') throw ERR('ReadOnlyError'); const st = this._st, pk = this._cur.k; return st.transaction._req(this, () => st._put(v, st._s.keyPath === null || st._s.keyPath === undefined ? pk : undefined, false)); }
    delete() { if (this._st.transaction.mode === 'readonly') throw ERR('ReadOnlyError'); const st = this._st, pk = this._cur.k; return st.transaction._req(this, () => { const s = st._s; s.recs = s.recs.filter(x => cmp(x.k, pk) !== 0); return undefined; }); }
  }
  class IDBCursorWithValue extends IDBCursor { get value() { return this._cur ? structuredClone(this._cur.v) : undefined; } }
  function openCur(st, src, q, dir, ko) {
    const tx = st.transaction;
    const req = tx._req(src, () => { const rows = src._sel(q, 0, dir).map(x => ({ ik: x.ik, k: x.k, v: ko ? undefined : x.v })); const c = ko ? new IDBCursor(st, src, rows, dir, ko, req) : new IDBCursorWithValue(st, src, rows, dir, ko, req); c._i = 0; c._cur = rows[0] || null; return c._cur ? c : null; });
    return req;
  }
  class IDBDatabase extends EventTarget {
    constructor(d) { super(); this._d = d; this.name = d.name; this.onversionchange = null; this.onclose = null; this.onabort = null; this.onerror = null; this._closed = false; }
    get version() { return this._d.version; }
    get objectStoreNames() { return strList(this._d.stores.keys()); }
    createObjectStore(n, o) { if (!this._vtx || this._vtx._done) throw ERR('InvalidStateError', 'createObjectStore must be called during a version change.'); if (this._d.stores.has(n)) throw ERR('ConstraintError', 'An object store with the specified name already exists.'); const kp = o && o.keyPath !== undefined ? o.keyPath : null; const auto = !!(o && o.autoIncrement); if (auto && (kp === '' || Array.isArray(kp))) throw ERR('InvalidAccessError'); this._d.stores.set(n, { keyPath: kp, auto, next: 1, recs: [], idx: new Map() }); this._vtx._names.push(n); this._vtx.objectStoreNames = strList(this._vtx._names); return new IDBObjectStore(this._vtx, n); }
    deleteObjectStore(n) { if (!this._vtx || this._vtx._done) throw ERR('InvalidStateError'); if (!this._d.stores.delete(n)) throw ERR('NotFoundError'); }
    transaction(names, mode) { if (this._closed) throw ERR('InvalidStateError', 'The database connection is closing.'); const a = typeof names === 'string' ? [names] : Array.from(names); if (!a.length) throw ERR('InvalidAccessError', 'The storeNames parameter was empty.'); for (const n of a) if (!this._d.stores.has(n)) throw ERR('NotFoundError', 'One of the specified object stores was not found.'); mode = mode || 'readonly'; if (mode !== 'readonly' && mode !== 'readwrite') throw new TypeError('invalid mode'); return new IDBTransaction(this, a, mode); }
    close() { this._closed = true; const i = this._d.conns.indexOf(this); if (i >= 0) this._d.conns.splice(i, 1); }
  }
  const key = n => (location.origin || 'null') + '\u0000' + n;
  class IDBFactory {
    open(name, version) {
      if (version !== undefined && !(version >= 1)) throw new TypeError("Failed to execute 'open' on 'IDBFactory': The version provided must not be 0.");
      name = String(name);
      const req = new IDBOpenDBRequest();
      setTimeout(() => {
        let d = DBS.get(key(name));
        const isNew = !d;
        if (!d) { d = { name, version: 0, stores: new Map(), conns: [] }; }
        const nv = version === undefined ? (d.version || 1) : Math.floor(version);
        if (nv < d.version) { req.readyState = 'done'; req._e = ERR('VersionError', 'The requested version (' + nv + ') is less than the existing version (' + d.version + ').'); fire(req, 'error', { bubbles: true, cancelable: true }); return; }
        const db = new IDBDatabase(d);
        if (nv > d.version) {
          for (const c of d.conns.slice()) fire(c, 'versionchange', { oldVersion: d.version, newVersion: nv });
          const old = d.version;
          if (isNew) DBS.set(key(name), d);
          const snapStores = new Map(Array.from(d.stores, ([n, s]) => [n, { keyPath: s.keyPath, auto: s.auto, next: s.next, recs: s.recs.slice(), idx: new Map(s.idx) }]));
          d.version = nv;
          const tx = new IDBTransaction(db, Array.from(d.stores.keys()), 'versionchange');
          db._vtx = tx; req.transaction = tx;
          req.readyState = 'done'; req._r = db;
          tx.addEventListener('complete', () => { db._vtx = null; req.transaction = null; d.conns.push(db); fire(req, 'success'); });
          tx.addEventListener('abort', () => { d.version = old; d.stores = snapStores; if (isNew) DBS.delete(key(name)); db._closed = true; req._r = undefined; req._e = ERR('AbortError', 'Version change transaction was aborted in upgradeneeded event handler.'); fire(req, 'error', { bubbles: true, cancelable: true }); });
          try { fire(req, 'upgradeneeded', { oldVersion: old, newVersion: nv }); } catch (e) { reportErr(e); }
        } else { d.conns.push(db); req.readyState = 'done'; req._r = db; fire(req, 'success'); }
      }, 0);
      return req;
    }
    deleteDatabase(name) { const req = new IDBOpenDBRequest(); setTimeout(() => { const d = DBS.get(key(String(name))); const ov = d ? d.version : 0; if (d) { for (const c of d.conns.slice()) fire(c, 'versionchange', { oldVersion: ov, newVersion: null }); DBS.delete(key(String(name))); } req.readyState = 'done'; req._r = undefined; fire(req, 'success', { oldVersion: ov, newVersion: null }); }, 0); return req; }
    databases() { const o = []; const p = (location.origin || 'null') + '\u0000'; for (const [k, d] of DBS) if (k.startsWith(p)) o.push({ name: d.name, version: d.version }); return Promise.resolve(o); }
    cmp(a, b) { return cmp(a, b); }
  }
  Object.assign(G, { indexedDB: new IDBFactory(), IDBFactory, IDBDatabase, IDBTransaction, IDBObjectStore, IDBIndex, IDBCursor, IDBCursorWithValue, IDBKeyRange, IDBRequest, IDBOpenDBRequest, IDBVersionChangeEvent });
})();
G.speechSynthesis = undefined;
G.trustedTypes = undefined;
G.FileReader = class FileReader extends EventTarget { constructor() { super(); this.result = null; this.readyState = 0; this.onload = null; this.onloadend = null; this.onerror = null; } _done(v) { this.result = v; this.readyState = 2; setTimeout(() => { run(this, new ProgressEvent('load')); run(this, new ProgressEvent('loadend')); }, 0); } readAsText(b) { this._done(b ? b._s : ''); } readAsDataURL(b) { this._done('data:' + (b && b.type || '') + ';base64,' + btoa(unescape(encodeURIComponent(b ? b._s : '')))); } readAsArrayBuffer(b) { const u = b ? b._bytes() : new Uint8Array(0); this._done(u.buffer.slice(u.byteOffset, u.byteOffset + u.byteLength)); } abort() {} };
const ceWait = [];
function ceCb(el, fn, args) { const f = el[fn]; if (typeof f !== 'function') return; try { f.apply(el, args); } catch (e) { reportErr(e); } }
function ceUpgrade(el) {
  if (ceDone.has(el)) return true;
  const c = ceByName.get(el.localName);
  if (!c) return false;
  ceDone.add(el);
  Object.setPrototypeOf(el, c.prototype);
  ceUpg = el;
  try { const r = new c(); if (r !== el) {} } catch (e) { reportErr(e); }
  ceUpg = null;
  const oa = c.observedAttributes;
  if (oa && typeof el.attributeChangedCallback === 'function') for (const a of oa) { const v = el.getAttribute(a); if (v !== null) ceCb(el, 'attributeChangedCallback', [a, null, v]); }
  return true;
}
function ceConnect(el) {
  if (!ceUpgrade(el)) return;
  if (el.isConnected && !ceConn.has(el)) { ceConn.add(el); ceCb(el, 'connectedCallback', []); }
}
function ceWalk(root, f) {
  if (root.nodeType === 1 && root.localName.indexOf('-') > 0) f(root);
  if (root.nodeType === 1 || root.nodeType === 11 || root.nodeType === 9) { const l = root.querySelectorAll('*'); for (let i = 0; i < l.length; i++) if (l[i].localName.indexOf('-') > 0) f(l[i]); }
  if (root.nodeType === 1 && root.shadowRoot) ceWalk(root.shadowRoot, f);
}
function ceFlush() {
  if (!ceByName.size) return;
  const p = N.cePending();
  if (p.over) { ceWalk(document, el => { if (!ceDone.has(el)) ceWait.push(el); }); }
  for (let i = 0; i < p.length; i++) ceWait.push(p[i]);
  if (!ceWait.length) return;
  const keep = [];
  const list = ceWait.splice(0, ceWait.length);
  for (const el of list) { if (ceDone.has(el)) continue; let inT = false; for (let q = el.parentNode; q; q = q.parentNode) if (q.localName === 'template') { inT = true; break; } if (inT) continue; if (ceByName.has(el.localName)) { ceUpgrade(el); if (el.isConnected) ceConnect(el); } else keep.push(el); }
  if (keep.length > 20000) keep.splice(0, keep.length - 20000);
  for (const el of keep) ceWait.push(el);
}
G.__ceMut = function (kind, target, child, name, old) {
  if (!ceByName.size) return;
  if (kind === 1 && child) { ceFlush(); if (child.isConnected) ceWalk(child, ceConnect); }
  else if (kind === 2 && child) { if (!child.isConnected) ceWalk(child, el => { if (ceConn.has(el)) { ceConn.delete(el); ceCb(el, 'disconnectedCallback', []); } }); }
  else if (kind === 5 && ceDone.has(target)) { const c = ceByName.get(target.localName); const oa = c && c.observedAttributes; if (oa && oa.includes(name)) ceCb(target, 'attributeChangedCallback', [name, old === undefined ? null : old, target.getAttribute(name)]); }
};
G.customElements = {
  define(name, ctor, opts) {
    name = String(name).toLowerCase();
    if (typeof ctor !== 'function') throw new TypeError("Failed to execute 'define' on 'CustomElementRegistry': The provided value is not of type 'Function'.");
    if (!/^[a-z][a-z0-9._]*-[a-z0-9._-]*$/.test(name)) throw new DOMException("'" + name + "' is not a valid custom element name", 'SyntaxError');
    if (ceByName.has(name)) throw new DOMException("'" + name + "' has already been defined", 'NotSupportedError');
    if (ceByCtor.has(ctor)) throw new DOMException('this constructor has already been used with this registry', 'NotSupportedError');
    let oa = ctor.observedAttributes; if (oa && typeof oa !== 'string') { try { oa = Array.from(oa, String); } catch (e) { oa = []; } Object.defineProperty(ctor, 'observedAttributes', { value: oa, configurable: true }); }
    ceByName.set(name, ctor); ceByCtor.set(ctor, name);
    N.moOn(1);
    ceFlush();
    for (const el of document.querySelectorAll(name)) ceConnect(el);
    const w = whenDef.get(name); if (w) { whenDef.delete(name); w.res(ctor); }
  },
  get(name) { return ceByName.get(String(name).toLowerCase()); },
  whenDefined(name) { name = String(name).toLowerCase(); if (ceByName.has(name)) return Promise.resolve(ceByName.get(name)); let w = whenDef.get(name); if (!w) { let res; const p = new Promise(r => res = r); w = { p, res }; whenDef.set(name, w); } return w.p; },
  upgrade(root) { ceFlush(); if (root) ceWalk(root, el => ceUpgrade(el)); }, getName(c) { return ceByCtor.get(c) || null; }
};
G.CustomElementRegistry = function CustomElementRegistry() { throw new TypeError('Illegal constructor'); };
const whenDef = new Map();
const origCreate = P.Document.createElement;
val(P.Document, 'createElement', function (name, opts) { const n = String(name).toLowerCase(); const c = ceByName.get(n) || (opts && opts.is && ceByName.get(opts.is)); if (c) return new c(); return origCreate.call(this, name); });
for (const [pr, key] of [[P.Element, 'innerHTML'], [P.Element, 'outerHTML'], [SRP, 'innerHTML']]) {
  const d = Object.getOwnPropertyDescriptor(pr, key);
  if (d && d.set) Object.defineProperty(pr, key, { get: d.get, set(v) {
    let old = null;
    if (ceByName.size) { old = []; ceWalk(this, el => { if (ceConn.has(el) && el !== this) old.push(el); }); if (key === 'outerHTML' && ceConn.has(this)) old.push(this); }
    const par = this.parentNode;
    d.set.call(this, v);
    if (ceByName.size && this.localName !== 'template') {
      for (const el of old) if (!el.isConnected && ceConn.has(el)) { ceConn.delete(el); ceCb(el, 'disconnectedCallback', []); }
      ceFlush(); const root = key === 'outerHTML' ? (par || document) : this; if (root.isConnected) ceWalk(root, ceConnect);
    } }, configurable: true, enumerable: d.enumerable });
}
{ const f = P.Element.insertAdjacentHTML; if (f) val(P.Element, 'insertAdjacentHTML', function (w, h) { const r = f.call(this, w, h); if (ceByName.size) { ceFlush(); const t = this.parentNode || this; if (t.isConnected) ceWalk(t, ceConnect); } return r; }); }
G.__connected = el => ceConnect(el);
G.__ceFlush = () => { ceFlush(); if (ceByName.size) ceWalk(document, ceConnect); };
let ldStage = 0;
G.__fireStage = function (n) {
  if (n === 1 && ldStage < 1) { ldStage = 1; const rs = new Event('readystatechange'); rs.isTrusted = true; run(document, rs); return; }
  if (n === 2 && ldStage < 2) { ldStage = 2; if (G.__docFlush) G.__docFlush(); G.__ceFlush(); const d = new Event('DOMContentLoaded', { bubbles: true }); d.isTrusted = true; run(document, d); return; }
  if (n === 3 && ldStage < 3) { if (ldStage < 2) G.__fireStage(2); ldStage = 3; const rs = new Event('readystatechange'); rs.isTrusted = true; run(document, rs); const l = new Event('load'); l.isTrusted = true; run(G, l); const ps = new PageTransitionEvent('pageshow', { persisted: false }); ps.isTrusted = true; run(G, ps); }
};
G.__fireLoad = function () { G.__fireStage(2); G.__fireStage(3); };
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
let msAll = null, msGen = -1;
function mediaScan() {
  const mg = N.domGen ? N.domGen() : -2;
  if (mg !== msGen || mg === -2 || !msAll) { msAll = document.querySelectorAll('video,audio'); msGen = mg; }
  for (const el of msAll) {
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
  endOfStream(err) { if (this.readyState !== 'open') return; if (!err) { let mx = 0; for (const sb of this.sourceBuffers._l) { try { const b = sb.buffered; if (b && b.length) mx = Math.max(mx, b.end(b.length - 1)); } catch (e) {} } if (mx > 0 && (isNaN(this.duration) || Math.abs(this.duration - mx) > 0.001)) this.duration = mx; } this.readyState = 'ended'; if (this._started && this._el) for (let i = 0; i < this.sourceBuffers._l.length; i++) N.mediaOp(4, this._el, i); this._ev('sourceended'); }
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
if (G.navigator) {
  let BM = null;
  const rd = () => { let r = [0, -1, 1, -1]; try { r = N.battery(); } catch (e) {} return r; };
  class BatteryManager extends EventTarget {
    constructor() { super(); this.onchargingchange = null; this.onlevelchange = null; this.onchargingtimechange = null; this.ondischargingtimechange = null; this._s = rd(); }
    get charging() { return this._s[0] ? !!this._s[2] : true; }
    get level() { return this._s[0] ? Math.max(0, Math.min(1, this._s[1] / 100)) : 1; }
    get chargingTime() { if (!this._s[0]) return 0; if (!this._s[2]) return Infinity; return this._s[1] >= 100 ? 0 : this._s[3] >= 0 ? this._s[3] * 60 : Infinity; }
    get dischargingTime() { if (!this._s[0] || this._s[2]) return Infinity; return this._s[3] >= 0 ? this._s[3] * 60 : Infinity; }
    _poll() { const o = this._s, n = rd(); this._s = n; const fire = (t) => this.dispatchEvent(new Event(t)); if (!!o[2] !== !!n[2]) fire('chargingchange'); if (o[1] !== n[1]) fire('levelchange'); if (o[3] !== n[3]) fire(n[2] ? 'chargingtimechange' : 'dischargingtimechange'); }
  }
  G.BatteryManager = BatteryManager;
  try { Object.defineProperty(G.navigator, 'getBattery', { value: function getBattery() { if (!BM) { BM = new BatteryManager(); if (BM._s[0]) N.setInterval(() => BM._poll(), 30000); } return Promise.resolve(BM); }, writable: true, configurable: true }); } catch (e) {}
}
if (!G.MediaMetadata) G.MediaMetadata = class MediaMetadata { constructor(o) { o = o || {}; this.title = o.title || ''; this.artist = o.artist || ''; this.album = o.album || ''; this.artwork = o.artwork || []; } };
if (G.document && !G.document.timeline) G.document.timeline = { get currentTime() { return G.performance ? G.performance.now() : Date.now(); } };
if (!G.createImageBitmap) G.createImageBitmap = (src) => Promise.resolve({ width: (src && src.width) || 0, height: (src && src.height) || 0, close() {} });
{
  const XR = { ANY_TYPE: 0, NUMBER_TYPE: 1, STRING_TYPE: 2, BOOLEAN_TYPE: 3, UNORDERED_NODE_ITERATOR_TYPE: 4, ORDERED_NODE_ITERATOR_TYPE: 5, UNORDERED_NODE_SNAPSHOT_TYPE: 6, ORDERED_NODE_SNAPSHOT_TYPE: 7, ANY_UNORDERED_NODE_TYPE: 8, FIRST_ORDERED_NODE_TYPE: 9 };
  const xerr = (m) => new DOMException("Failed to execute 'evaluate' on 'Document': " + m, 'SyntaxError');
  const tokenize = (s) => {
    const t = []; let i = 0;
    const NAME = /[A-Za-z_À-￿][\w.\-·À-￿]*/y;
    while (i < s.length) {
      const c = s[i];
      if (/\s/.test(c)) { i++; continue; }
      if (c === '"' || c === "'") { const j = s.indexOf(c, i + 1); if (j < 0) throw xerr('The string \'' + s + '\' is not a valid XPath expression.'); t.push({ k: 'str', v: s.slice(i + 1, j) }); i = j + 1; continue; }
      if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(s[i + 1] || ''))) { const m = /\d*\.?\d*/y; m.lastIndex = i; const r = m.exec(s); t.push({ k: 'num', v: parseFloat(r[0]) }); i += r[0].length; continue; }
      const two = s.substr(i, 2);
      if (two === '//' || two === '::' || two === '..' || two === '!=' || two === '<=' || two === '>=') { t.push({ k: 'op', v: two }); i += 2; continue; }
      if ('/()[]@,|+-=<>*.$'.includes(c)) { t.push({ k: 'op', v: c }); i++; continue; }
      NAME.lastIndex = i; const r = NAME.exec(s);
      if (r) { let n = r[0]; i += n.length; if (s[i] === ':' && s[i + 1] !== ':') { if (s[i + 1] === '*') { n += ':*'; i += 2; } else { NAME.lastIndex = i + 1; const r2 = NAME.exec(s); if (r2) { n += ':' + r2[0]; i += 1 + r2[0].length; } } } t.push({ k: 'name', v: n }); continue; }
      throw xerr('The string \'' + s + '\' is not a valid XPath expression.');
    }
    const ops = new Set(['and', 'or', 'mod', 'div']);
    for (let k = 0; k < t.length; k++) {
      const p = t[k - 1];
      const prevIsOperand = !!p && (p.k === 'num' || p.k === 'str' || p.k === 'name' || (p.k === 'op' && [')', ']', '.', '..', '*'].includes(p.v)));
      if (t[k].k === 'op' && t[k].v === '*' && prevIsOperand) t[k] = { k: 'mul' };
      else if (t[k].k === 'name' && ops.has(t[k].v) && prevIsOperand) t[k] = { k: 'kw', v: t[k].v };
    }
    return t;
  };
  const AXES = new Set(['ancestor', 'ancestor-or-self', 'attribute', 'child', 'descendant', 'descendant-or-self', 'following', 'following-sibling', 'namespace', 'parent', 'preceding', 'preceding-sibling', 'self']);
  const NTYPES = new Set(['node', 'text', 'comment', 'processing-instruction']);
  const parse = (src) => {
    const t = tokenize(src); let p = 0;
    const pk = (o) => t[p] && (t[p].v === o) && (t[p].k === 'op' || t[p].k === 'kw');
    const eat = (o) => { if (pk(o)) { p++; return true; } return false; };
    const need = (o) => { if (!eat(o)) throw xerr('The string \'' + src + '\' is not a valid XPath expression.'); };
    const bin = (next, opsl) => () => { let l = next(); for (;;) { const tk = t[p]; if (!tk) return l; let op = null; if (tk.k === 'kw' && opsl.includes(tk.v)) op = tk.v; else if (tk.k === 'op' && opsl.includes(tk.v)) op = tk.v; else if (tk.k === 'mul' && opsl.includes('*')) op = '*'; if (!op) return l; p++; l = { t: 'bin', op, l, r: next() }; } };
    let orE;
    const step = () => {
      if (eat('.')) return { axis: 'self', test: { k: 'node' }, preds: [] };
      if (eat('..')) return { axis: 'parent', test: { k: 'node' }, preds: [] };
      let axis = 'child';
      if (eat('@')) axis = 'attribute';
      else if (t[p] && t[p].k === 'name' && t[p + 1] && t[p + 1].v === '::') { axis = t[p].v; if (!AXES.has(axis)) throw xerr('The string \'' + src + '\' is not a valid XPath expression.'); p += 2; }
      let test;
      const tk = t[p];
      if (tk && tk.k === 'op' && tk.v === '*') { p++; test = { k: 'any' }; }
      else if (tk && (tk.k === 'name' || tk.k === 'kw')) {
        p++;
        if (NTYPES.has(tk.v) && pk('(')) { p++; let lit = null; if (t[p] && t[p].k === 'str') { lit = t[p].v; p++; } need(')'); test = { k: tk.v, lit }; }
        else if (tk.v.endsWith(':*')) test = { k: 'any' };
        else test = { k: 'name', v: tk.v };
      } else throw xerr('The string \'' + src + '\' is not a valid XPath expression.');
      const preds = []; while (eat('[')) { preds.push(orE()); need(']'); }
      return { axis, test, preds };
    };
    const relPath = (steps) => { steps.push(step()); for (;;) { if (eat('/')) steps.push(step()); else if (eat('//')) { steps.push({ axis: 'descendant-or-self', test: { k: 'node' }, preds: [] }); steps.push(step()); } else return steps; } };
    const isStepStart = () => { const tk = t[p]; if (!tk) return false; if (tk.k === 'op') return ['.', '..', '@', '*'].includes(tk.v); return tk.k === 'name' || tk.k === 'kw'; };
    const primary = () => {
      const tk = t[p];
      if (!tk) throw xerr('The string \'' + src + '\' is not a valid XPath expression.');
      if (tk.k === 'str') { p++; return { t: 'lit', v: tk.v }; }
      if (tk.k === 'num') { p++; return { t: 'lit', v: tk.v }; }
      if (tk.k === 'op' && tk.v === '(') { p++; const e = orE(); need(')'); return e; }
      if (tk.k === 'op' && tk.v === '$') { p++; const n = t[p++]; return { t: 'var', v: n && n.v }; }
      if (tk.k === 'name' && t[p + 1] && t[p + 1].v === '(' && !NTYPES.has(tk.v)) { p += 2; const args = []; if (!pk(')')) { do args.push(orE()); while (eat(',')); } need(')'); return { t: 'fn', n: tk.v, args }; }
      return null;
    };
    const pathE = () => {
      if (pk('/')) { p++; const steps = isStepStart() ? relPath([]) : []; return { t: 'path', abs: true, steps }; }
      if (pk('//')) { p++; return { t: 'path', abs: true, steps: relPath([{ axis: 'descendant-or-self', test: { k: 'node' }, preds: [] }]) }; }
      const save = p; const pr = primary();
      if (pr) {
        const preds = []; while (eat('[')) { preds.push(orE()); need(']'); }
        let e = preds.length ? { t: 'filter', e: pr, preds } : pr;
        if (pk('/')) { p++; e = { t: 'path', base: e, steps: relPath([]) }; }
        else if (pk('//')) { p++; e = { t: 'path', base: e, steps: relPath([{ axis: 'descendant-or-self', test: { k: 'node' }, preds: [] }]) }; }
        return e;
      }
      p = save;
      return { t: 'path', abs: false, steps: relPath([]) };
    };
    const unionE = bin(pathE, ['|']);
    const unary = () => { if (pk('-')) { p++; return { t: 'neg', e: unary() }; } return unionE(); };
    const mulE = bin(unary, ['*', 'div', 'mod']);
    const addE = bin(mulE, ['+', '-']);
    const relE = bin(addE, ['<', '<=', '>', '>=']);
    const eqE = bin(relE, ['=', '!=']);
    const andE = bin(eqE, ['and']);
    orE = bin(andE, ['or']);
    const e = orE();
    if (p < t.length) throw xerr('The string \'' + src + '\' is not a valid XPath expression.');
    return e;
  };
  const strv = (n) => { if (!n) return ''; const ty = n.nodeType; if (ty === 1 || ty === 9 || ty === 11) return n.textContent || ''; if (ty === 2) return n.value; return n.nodeValue != null ? n.nodeValue : (n.data || ''); };
  const ord = (a, b) => { if (a === b) return 0; if (a.nodeType === 2 || b.nodeType === 2) { const ea = a.nodeType === 2 ? a.ownerElement : a, eb = b.nodeType === 2 ? b.ownerElement : b; if (ea === eb) { if (a.nodeType === 2 && b.nodeType === 2) { const l = Array.from(ea.attributes); return l.indexOf(a) - l.indexOf(b); } return a.nodeType === 2 ? 1 : -1; } if (!ea || !eb) return 0; return ord(ea, eb); } const c = a.compareDocumentPosition(b); return c & 4 ? -1 : c & 2 ? 1 : 0; };
  const uniq = (arr) => { const s = new Set(); const o = []; for (const n of arr) if (!s.has(n)) { s.add(n); o.push(n); } return o.sort(ord); };
  const kids = (n) => n.nodeType === 2 ? [] : Array.from(n.childNodes || []);
  const descend = (n, out) => { for (const c of kids(n)) { out.push(c); descend(c, out); } };
  const parentOf = (n) => n.nodeType === 2 ? n.ownerElement : n.parentNode;
  const axisNodes = (axis, n) => {
    const o = [];
    switch (axis) {
      case 'child': return kids(n);
      case 'descendant': descend(n, o); return o;
      case 'descendant-or-self': o.push(n); descend(n, o); return o;
      case 'self': return [n];
      case 'parent': { const p = parentOf(n); return p ? [p] : []; }
      case 'ancestor': for (let p = parentOf(n); p; p = parentOf(p)) o.push(p); return o;
      case 'ancestor-or-self': o.push(n); for (let p = parentOf(n); p; p = parentOf(p)) o.push(p); return o;
      case 'attribute': return n.nodeType === 1 ? Array.from(n.attributes) : [];
      case 'following-sibling': if (n.nodeType === 2) return []; for (let s = n.nextSibling; s; s = s.nextSibling) o.push(s); return o;
      case 'preceding-sibling': if (n.nodeType === 2) return []; for (let s = n.previousSibling; s; s = s.previousSibling) o.push(s); return o;
      case 'following': { let c = n.nodeType === 2 ? n.ownerElement : n; if (n.nodeType === 2 && c) { descend(c, o); } for (; c; c = c.parentNode) for (let s = c.nextSibling; s; s = s.nextSibling) { o.push(s); descend(s, o); } return o; }
      case 'preceding': { const anc = new Set(); let c = n.nodeType === 2 ? n.ownerElement : n; for (let p = c; p; p = p.parentNode) anc.add(p); const root = (() => { let r = c; while (r && r.parentNode) r = r.parentNode; return r; })(); const all = []; if (root) descend(root, all); const res = []; for (const x of all) { if (x === c) break; if (!anc.has(x)) res.push(x); } return res.reverse(); }
      case 'namespace': return [];
    }
    return o;
  };
  const testOk = (test, n, axis) => {
    const ty = n.nodeType;
    switch (test.k) {
      case 'node': return true;
      case 'text': return ty === 3 || ty === 4;
      case 'comment': return ty === 8;
      case 'processing-instruction': return ty === 7 && (test.lit == null || n.nodeName === test.lit);
      case 'any': return axis === 'attribute' ? ty === 2 : ty === 1;
      case 'name': {
        if (axis === 'attribute') { if (ty !== 2) return false; return n.name.toLowerCase() === test.v.toLowerCase(); }
        if (ty !== 1) return false;
        const want = test.v.replace(/^[^:]*:/, '');
        const ln = n.localName || '';
        const html = !n.namespaceURI || n.namespaceURI === 'http://www.w3.org/1999/xhtml';
        return html ? ln.toLowerCase() === want.toLowerCase() : ln === want;
      }
    }
    return false;
  };
  const toNum = (v) => { if (typeof v === 'number') return v; if (typeof v === 'boolean') return v ? 1 : 0; if (Array.isArray(v)) v = v.length ? strv(v[0]) : ''; const s = String(v).trim(); return /^-?(\d+(\.\d*)?|\.\d+)$/.test(s) ? parseFloat(s) : NaN; };
  const toStr = (v) => { if (Array.isArray(v)) return v.length ? strv(v[0]) : ''; if (typeof v === 'number') { if (Number.isNaN(v)) return 'NaN'; if (v === Infinity) return 'Infinity'; if (v === -Infinity) return '-Infinity'; if (Number.isInteger(v)) return String(v); return String(v); } if (typeof v === 'boolean') return v ? 'true' : 'false'; return String(v); };
  const toBool = (v) => Array.isArray(v) ? v.length > 0 : typeof v === 'number' ? v !== 0 && !Number.isNaN(v) : typeof v === 'boolean' ? v : String(v).length > 0;
  const cmp = (op, a, b) => {
    const f = (x, y) => { switch (op) { case '=': return x === y; case '!=': return x !== y; case '<': return x < y; case '<=': return x <= y; case '>': return x > y; case '>=': return x >= y; } };
    const A = Array.isArray(a), B = Array.isArray(b);
    if (A && B) { for (const x of a) for (const y of b) { const sx = strv(x), sy = strv(y); if (op === '=' || op === '!=' ? f(sx, sy) : f(toNum(sx), toNum(sy))) return true; } return false; }
    if (A || B) { const ns = A ? a : b, o = A ? b : a; for (const x of ns) { const sx = strv(x); let l, r; if (typeof o === 'number') { l = toNum(sx); r = o; } else if (typeof o === 'boolean') { l = toBool([x]); r = o; } else if (op === '=' || op === '!=') { l = sx; r = String(o); } else { l = toNum(sx); r = toNum(o); } if (A ? f(l, r) : f(r, l)) return true; } return false; }
    if (op === '=' || op === '!=') { if (typeof a === 'boolean' || typeof b === 'boolean') return f(toBool(a), toBool(b)); if (typeof a === 'number' || typeof b === 'number') return f(toNum(a), toNum(b)); return f(toStr(a), toStr(b)); }
    return f(toNum(a), toNum(b));
  };
  const FN = {
    last: (c) => c.size, position: (c) => c.pos,
    count: (c, a) => { if (!Array.isArray(a[0])) throw new TypeError('count() needs a node-set'); return a[0].length; },
    'local-name': (c, a) => { const n = a.length ? a[0][0] : c.node; if (!n) return ''; return n.nodeType === 2 ? n.localName : n.nodeType === 1 ? (n.localName || '') : n.nodeType === 7 ? n.nodeName : ''; },
    name: (c, a) => { const n = a.length ? a[0][0] : c.node; if (!n) return ''; return n.nodeType === 2 ? n.name : n.nodeType === 1 ? (n.namespaceURI && n.namespaceURI !== 'http://www.w3.org/1999/xhtml' ? n.tagName : (n.localName || '')) : n.nodeType === 7 ? n.nodeName : ''; },
    'namespace-uri': (c, a) => { const n = a.length ? a[0][0] : c.node; return (n && n.namespaceURI) || ''; },
    string: (c, a) => a.length ? toStr(a[0]) : strv(c.node),
    concat: (c, a) => a.map(toStr).join(''),
    'starts-with': (c, a) => toStr(a[0]).startsWith(toStr(a[1])),
    contains: (c, a) => toStr(a[0]).includes(toStr(a[1])),
    'substring-before': (c, a) => { const s = toStr(a[0]), t = toStr(a[1]); const i = s.indexOf(t); return i < 0 ? '' : s.slice(0, i); },
    'substring-after': (c, a) => { const s = toStr(a[0]), t = toStr(a[1]); const i = s.indexOf(t); return i < 0 ? '' : s.slice(i + t.length); },
    substring: (c, a) => { const s = toStr(a[0]); const st = Math.round(toNum(a[1])); const ln = a.length > 2 ? Math.round(toNum(a[2])) : Infinity; let o = ''; for (let i = 1; i <= s.length; i++) if (i >= st && i < st + ln) o += s[i - 1]; return o; },
    'string-length': (c, a) => (a.length ? toStr(a[0]) : strv(c.node)).length,
    'normalize-space': (c, a) => (a.length ? toStr(a[0]) : strv(c.node)).replace(/[\x20\t\r\n]+/g, ' ').trim(),
    translate: (c, a) => { const s = toStr(a[0]), f = toStr(a[1]), t2 = toStr(a[2]); let o = ''; for (const ch of s) { const i = f.indexOf(ch); if (i < 0) o += ch; else if (i < t2.length) o += t2[i]; } return o; },
    boolean: (c, a) => toBool(a[0]), not: (c, a) => !toBool(a[0]), true: () => true, false: () => false,
    lang: (c, a) => { const want = toStr(a[0]).toLowerCase(); for (let n = c.node; n; n = parentOf(n)) { if (n.nodeType === 1 && n.hasAttribute('lang')) { const l = n.getAttribute('lang').toLowerCase(); return l === want || l.startsWith(want + '-'); } } return false; },
    number: (c, a) => a.length ? toNum(a[0]) : toNum(strv(c.node)),
    sum: (c, a) => a[0].reduce((s, n) => s + toNum(strv(n)), 0),
    floor: (c, a) => Math.floor(toNum(a[0])), ceiling: (c, a) => Math.ceil(toNum(a[0])),
    round: (c, a) => { const v = toNum(a[0]); return Number.isNaN(v) || !Number.isFinite(v) ? v : Math.floor(v + 0.5); },
    id: (c, a) => { const ids = Array.isArray(a[0]) ? a[0].map(strv).join(' ') : toStr(a[0]); const doc = c.node.nodeType === 9 ? c.node : c.node.ownerDocument || document; const o = []; for (const s of ids.split(/\s+/)) { if (!s) continue; const e = doc.getElementById(s); if (e) o.push(e); } return uniq(o); }
  };
  const ev = (e, c) => {
    switch (e.t) {
      case 'lit': return e.v;
      case 'var': throw xerr('variables are not supported.');
      case 'neg': return -toNum(ev(e.e, c));
      case 'fn': { const f = FN[e.n]; if (!f) throw new DOMException("Failed to execute 'evaluate' on 'Document': The function '" + e.n + "' is not supported.", 'NotSupportedError'); return f(c, e.args.map((x) => ev(x, c))); }
      case 'bin': {
        if (e.op === 'or') return toBool(ev(e.l, c)) || toBool(ev(e.r, c));
        if (e.op === 'and') return toBool(ev(e.l, c)) && toBool(ev(e.r, c));
        const l = ev(e.l, c), r = ev(e.r, c);
        switch (e.op) {
          case '|': if (!Array.isArray(l) || !Array.isArray(r)) throw new TypeError('| needs node-sets'); return uniq(l.concat(r));
          case '+': return toNum(l) + toNum(r); case '-': return toNum(l) - toNum(r); case '*': return toNum(l) * toNum(r);
          case 'div': return toNum(l) / toNum(r); case 'mod': return toNum(l) % toNum(r);
          default: return cmp(e.op, l, r);
        }
      }
      case 'filter': { let ns = ev(e.e, c); if (!Array.isArray(ns)) throw new TypeError('predicate on a non node-set'); for (const pr of e.preds) ns = applyPred(ns, pr); return ns; }
      case 'path': {
        let ns;
        if (e.base) { ns = ev(e.base, c); if (!Array.isArray(ns)) throw new TypeError('path on a non node-set'); }
        else if (e.abs) { let r = c.node; while (parentOf(r)) r = parentOf(r); ns = [r]; }
        else ns = [c.node];
        for (const st of e.steps) {
          const out = [];
          for (const n of ns) {
            let cand = axisNodes(st.axis, n).filter((x) => testOk(st.test, x, st.axis));
            for (const pr of st.preds) cand = applyPred(cand, pr);
            for (const x of cand) out.push(x);
          }
          ns = uniq(out);
        }
        return ns;
      }
    }
    throw xerr('unknown expression');
  };
  const applyPred = (ns, pr) => {
    const size = ns.length; const o = [];
    for (let i = 0; i < size; i++) { const v = ev(pr, { node: ns[i], pos: i + 1, size }); if (typeof v === 'number' ? v === i + 1 : toBool(v)) o.push(ns[i]); }
    return o;
  };
  class XPathResult {
    constructor(v, type) {
      if (type === 0) type = Array.isArray(v) ? 4 : typeof v === 'number' ? 1 : typeof v === 'boolean' ? 3 : 2;
      if (type >= 4 && !Array.isArray(v)) throw new TypeError("Failed to execute 'evaluate' on 'Document': The result is not a node set, and therefore cannot be converted to the desired type.");
      Object.defineProperty(this, '_t', { value: type }); Object.defineProperty(this, '_v', { value: v }); Object.defineProperty(this, '_i', { value: 0, writable: true });
    }
    get resultType() { return this._t; }
    get numberValue() { if (this._t !== 1) throw new TypeError('The result type is not a number.'); return toNum(this._v); }
    get stringValue() { if (this._t !== 2) throw new TypeError('The result type is not a string.'); return toStr(this._v); }
    get booleanValue() { if (this._t !== 3) throw new TypeError('The result type is not a boolean.'); return toBool(this._v); }
    get singleNodeValue() { if (this._t !== 8 && this._t !== 9) throw new TypeError('The result type is not a single node.'); return this._v[0] || null; }
    get snapshotLength() { if (this._t !== 6 && this._t !== 7) throw new TypeError('The result type is not a snapshot.'); return this._v.length; }
    get invalidIteratorState() { return false; }
    snapshotItem(i) { if (this._t !== 6 && this._t !== 7) throw new TypeError('The result type is not a snapshot.'); return this._v[i >>> 0] || null; }
    iterateNext() { if (this._t !== 4 && this._t !== 5) throw new TypeError('The result type is not an iterator.'); return this._v[this._i++] || null; }
  }
  for (const k in XR) { XPathResult[k] = XR[k]; XPathResult.prototype[k] = XR[k]; }
  const coerce = (v, type) => type === 1 ? toNum(v) : type === 2 ? toStr(v) : type === 3 ? toBool(v) : v;
  const run = (expr, node, type) => { const v = ev(expr, { node: node || document, pos: 1, size: 1 }); return new XPathResult(type >= 1 && type <= 3 ? coerce(v, type) : v, type | 0); };
  class XPathExpression { constructor(src) { Object.defineProperty(this, '_e', { value: parse(String(src)) }); } evaluate(node, type) { return run(this._e, node, type); } }
  class XPathEvaluator { createExpression(s) { return new XPathExpression(s); } createNSResolver(n) { return n; } evaluate(s, node, ns, type) { return run(parse(String(s)), node, type); } }
  const docEval = function evaluate(s, node, ns, type) { if (node == null) throw new TypeError("Failed to execute 'evaluate' on 'Document': parameter 2 is not of type 'Node'."); return run(parse(String(s)), node, type); };
  G.XPathResult = XPathResult; G.XPathExpression = XPathExpression; G.XPathEvaluator = XPathEvaluator;
  const DP = G.Document && G.Document.prototype;
  for (const o of [DP, G.document]) if (o) { try { Object.defineProperty(o, 'evaluate', { value: docEval, writable: true, configurable: true }); Object.defineProperty(o, 'createExpression', { value: function createExpression(s) { return new XPathExpression(s); }, writable: true, configurable: true }); Object.defineProperty(o, 'createNSResolver', { value: function createNSResolver(n) { return n; }, writable: true, configurable: true }); } catch (e) {} }
}
if (!G.reportError) G.reportError = (e) => { try { console.error(e && e.stack ? e.stack : String(e)); } catch (x) {} };
{
  const H = Symbol('wasm');
  const mkErr = (n) => { const C = class extends Error { constructor(m) { super(m); } }; Object.defineProperty(C, 'name', { value: n }); Object.defineProperty(C.prototype, 'name', { value: n, writable: true, configurable: true }); return C; };
  const CompileError = mkErr('CompileError'), LinkError = mkErr('LinkError'), RuntimeError = mkErr('RuntimeError');
  const FNS = new Map(), MEMS = new Map(), TABS = new Map(), GLOBS = new Map();
  const mkFn = (id) => {
    let f = FNS.get(id);
    if (f) return f;
    const [na, idx] = N.wasmFnInfo(id);
    const C = N.wasmCall;
    f = na === 0 ? function () { return C(id); } : na === 1 ? function (a) { return C(id, a); } : na === 2 ? function (a, b) { return C(id, a, b); } : na === 3 ? function (a, b, c) { return C(id, a, b, c); } : na === 4 ? function (a, b, c, d) { return C(id, a, b, c, d); } : function (...a) { return C(id, ...a); };
    Object.defineProperty(f, 'name', { value: String(idx) });
    Object.defineProperty(f, 'length', { value: na });
    Object.defineProperty(f, '__wfid', { value: id });
    FNS.set(id, f);
    return f;
  };
  N.wasmInit(CompileError, LinkError, RuntimeError, mkFn);
  const toU8 = (b, who) => {
    if (b instanceof ArrayBuffer) return new Uint8Array(b);
    if (ArrayBuffer.isView(b)) return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
    throw new TypeError(who + ': Argument 0 must be a buffer source');
  };
  const KN = ['function', 'table', 'memory', 'global', 'tag'];
  function scan(u8) {
    let p = 8;
    const imports = [], exports = [], custom = [];
    const leb = () => { let r = 0, m = 1, b; do { b = u8[p++]; r += (b & 0x7f) * m; m *= 128; } while (b & 0x80 && p <= u8.length); return r; };
    const td = new TextDecoder();
    const str = () => { const n = leb(); const s = td.decode(u8.subarray(p, p + n)); p += n; return s; };
    const limits = () => { const f = leb(); leb(); if (f & 1) leb(); return f; };
    while (p < u8.length) {
      const id = u8[p++]; const sz = leb(); const end = p + sz;
      if (id === 0) { const nm = str(); custom.push([nm, u8.slice(p, end)]); }
      else if (id === 2) {
        const n = leb();
        for (let i = 0; i < n; i++) {
          const module = str(), name = str(); const k = u8[p++];
          let type;
          if (k === 0) leb();
          else if (k === 1) { const rt = u8[p++]; limits(); type = rt === 0x6f ? 'externref' : 'funcref'; }
          else if (k === 2) limits();
          else if (k === 3) { const vt = u8[p++]; const mu = u8[p++]; type = { t: vt, m: !!mu }; }
          else if (k === 4) { p++; leb(); }
          imports.push({ module, name, kind: KN[k] || 'function', _t: type });
        }
      } else if (id === 7) {
        const n = leb();
        for (let i = 0; i < n; i++) { const name = str(); const k = u8[p++]; leb(); exports.push({ name, kind: KN[k] || 'function' }); }
      }
      p = end;
    }
    return { imports, exports, custom };
  }
  const memObj = (id) => { let m = MEMS.get(id); if (!m) { m = Object.create(Memory.prototype); m[H] = id; MEMS.set(id, m); } return m; };
  const tabObj = (id) => { let t = TABS.get(id); if (!t) { t = Object.create(Table.prototype); t[H] = id; TABS.set(id, t); } return t; };
  const globObj = (id) => { let g = GLOBS.get(id); if (!g) { g = Object.create(Global.prototype); g[H] = id; GLOBS.set(id, g); } return g; };
  const pages = (v, who, n) => { if (v === undefined) return undefined; const x = Number(v); if (!isFinite(x) || x < 0 || x > 4294967295) throw new TypeError(who + ": Property '" + n + "': value " + v + ' is out of bounds'); return Math.floor(x); };
  class Module {
    constructor(bytes) {
      if (!new.target) throw new TypeError("WebAssembly.Module must be invoked with 'new'");
      const u8 = toU8(bytes, 'WebAssembly.Module()');
      if (u8.length < 8) throw new CompileError('WebAssembly.Module(): BufferSource argument is empty');
      this[H] = N.wasmParse(u8);
      this._d = scan(u8);
    }
    static imports(m) { if (!(m instanceof Module)) throw new TypeError('WebAssembly.Module.imports(): Argument 0 must be a WebAssembly.Module'); return m._d.imports.map(x => ({ module: x.module, name: x.name, kind: x.kind })); }
    static exports(m) { if (!(m instanceof Module)) throw new TypeError('WebAssembly.Module.exports(): Argument 0 must be a WebAssembly.Module'); return m._d.exports.map(x => ({ name: x.name, kind: x.kind })); }
    static customSections(m, n) { if (!(m instanceof Module)) throw new TypeError('WebAssembly.Module.customSections(): Argument 0 must be a WebAssembly.Module'); n = String(n); return m._d.custom.filter(c => c[0] === n).map(c => c[1].slice().buffer); }
  }
  Object.defineProperty(Module.prototype, Symbol.toStringTag, { value: 'WebAssembly.Module' });
  class Instance {
    constructor(module, imports) {
      if (!new.target) throw new TypeError("WebAssembly.Instance must be invoked with 'new'");
      if (!(module instanceof Module)) throw new TypeError('WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module');
      const im = module._d.imports;
      if (im.length && (imports === undefined || imports === null || typeof imports !== 'object')) throw new TypeError('WebAssembly.Instance(): Imports argument must be present and must be an object');
      const fn = [], mem = [], tab = [], gid = [], gval = [];
      im.forEach((d, i) => {
        const o = imports[d.module];
        if (o === undefined || o === null || (typeof o !== 'object' && typeof o !== 'function')) throw new TypeError('WebAssembly.Instance(): Import #' + i + ' "' + d.module + '": module is not an object or function');
        const v = o[d.name];
        const at = 'WebAssembly.Instance(): Import #' + i + ' "' + d.module + '" "' + d.name + '": ';
        if (d.kind === 'function') { if (typeof v !== 'function') throw new LinkError(at + 'function import requires a callable'); fn.push(v); }
        else if (d.kind === 'memory') { if (!(v instanceof Memory)) throw new LinkError(at + 'memory import must be a WebAssembly.Memory object'); mem.push(v[H]); }
        else if (d.kind === 'table') { if (!(v instanceof Table)) throw new LinkError(at + 'table import requires a WebAssembly.Table'); tab.push(v[H]); }
        else if (d.kind === 'global') {
          if (v instanceof Global) { gid.push(v[H]); gval.push(0); }
          else {
            const t = d._t || {};
            if (t.m) throw new LinkError(at + 'imported mutable global must be a WebAssembly.Global object');
            if (t.t === 0x7e ? typeof v !== 'bigint' : (t.t === 0x6f || t.t === 0x70) ? false : typeof v !== 'number') throw new LinkError(at + 'global import must be a number, valid Wasm reference, or WebAssembly.Global object');
            gid.push(-1); gval.push(v);
          }
        } else throw new LinkError(at + 'tag import requires a WebAssembly.Tag');
      });
      const ex = N.wasmInst(module[H], fn, mem, tab, gid, gval);
      const out = Object.create(null);
      for (const [name, kind, id] of ex) {
        if (kind === 10) out[name] = fn[id];
        else if (kind === 0) out[name] = mkFn(id);
        else if (kind === 2) out[name] = memObj(id);
        else if (kind === 1) out[name] = tabObj(id);
        else if (kind === 3) out[name] = globObj(id);
      }
      Object.defineProperty(this, 'exports', { value: Object.freeze(out), enumerable: true, configurable: true });
    }
  }
  Object.defineProperty(Instance.prototype, Symbol.toStringTag, { value: 'WebAssembly.Instance' });
  class Memory {
    constructor(d) {
      if (!new.target) throw new TypeError("WebAssembly.Memory must be invoked with 'new'");
      if (d === null || typeof d !== 'object') throw new TypeError('WebAssembly.Memory(): Argument 0 must be a memory descriptor');
      const ini = pages(d.initial, 'WebAssembly.Memory()', 'initial'), mx = pages(d.maximum, 'WebAssembly.Memory()', 'maximum');
      if (ini === undefined) throw new TypeError("WebAssembly.Memory(): Property 'initial' is required");
      if (ini > 65536) throw new RangeError("WebAssembly.Memory(): Property 'initial': value " + ini + ' is above the upper bound 65536');
      if (mx !== undefined && mx < ini) throw new RangeError("WebAssembly.Memory(): Property 'maximum': value " + mx + ' is below the lower bound ' + ini);
      const id = N.wasmNewMem(ini, mx === undefined ? -1 : Math.min(mx, 65536));
      this[H] = id; MEMS.set(id, this);
    }
    get buffer() { return N.wasmMemBuf(this[H]); }
    grow(d) { const x = pages(d, 'WebAssembly.Memory.grow()', 'delta'); if (x === undefined) throw new TypeError('WebAssembly.Memory.grow(): Argument 0 must be convertible to a valid number'); return N.wasmMemGrow(this[H], x); }
  }
  Object.defineProperty(Memory.prototype, Symbol.toStringTag, { value: 'WebAssembly.Memory' });
  const refKind = (e) => { e = String(e); if (e === 'anyfunc' || e === 'funcref') return 0; if (e === 'externref') return 1; throw new TypeError("WebAssembly.Table(): Descriptor property 'element' must be a WebAssembly reference type"); };
  class Table {
    constructor(d, v) {
      if (!new.target) throw new TypeError("WebAssembly.Table must be invoked with 'new'");
      if (d === null || typeof d !== 'object') throw new TypeError('WebAssembly.Table(): Argument 0 must be a table descriptor');
      const k = refKind(d.element);
      const ini = pages(d.initial, 'WebAssembly.Table()', 'initial'), mx = pages(d.maximum, 'WebAssembly.Table()', 'maximum');
      if (ini === undefined) throw new TypeError("WebAssembly.Table(): Property 'initial' is required");
      if (ini > 10000000) throw new RangeError("WebAssembly.Table(): Property 'initial': value " + ini + ' is above the upper bound 10000000');
      if (mx !== undefined && mx < ini) throw new RangeError("WebAssembly.Table(): Property 'maximum': value " + mx + ' is below the lower bound ' + ini);
      const id = N.wasmNewTab(k, ini, mx === undefined ? -1 : mx);
      this[H] = id; TABS.set(id, this);
      if (v !== undefined && v !== null) for (let i = 0; i < ini; i++) N.wasmTabSet(id, i, v);
    }
    get length() { return N.wasmTabInfo(this[H])[0]; }
    get(i) { return N.wasmTabGet(this[H], Number(i)); }
    set(i, v) { N.wasmTabSet(this[H], Number(i), arguments.length < 2 ? (N.wasmTabInfo(this[H])[1] ? undefined : null) : v); }
    grow(d, v) { return N.wasmTabGrow(this[H], Number(d), v === undefined ? null : v); }
  }
  Object.defineProperty(Table.prototype, Symbol.toStringTag, { value: 'WebAssembly.Table' });
  const VT = { i32: 1, i64: 2, f32: 3, f64: 4, anyfunc: 6, funcref: 6, externref: 7 };
  const VN = { 1: 'i32', 2: 'i64', 3: 'f32', 4: 'f64', 6: 'anyfunc', 7: 'externref' };
  class Global {
    constructor(d, v) {
      if (!new.target) throw new TypeError("WebAssembly.Global must be invoked with 'new'");
      if (d === null || typeof d !== 'object') throw new TypeError('WebAssembly.Global(): Argument 0 must be a global descriptor');
      const t = VT[String(d.value)];
      if (!t) throw new TypeError("WebAssembly.Global(): Descriptor property 'value' must be a WebAssembly type");
      const id = N.wasmNewGlob(t, d.mutable ? 1 : 0);
      this[H] = id; GLOBS.set(id, this);
      const init = v === undefined ? (t === 2 ? 0n : t === 7 ? undefined : t === 6 ? null : 0) : v;
      if (t === 7 && v === undefined) N.wasmGlobSet(id, null); else N.wasmGlobSet(id, init);
    }
    get value() { return N.wasmGlobGet(this[H]); }
    set value(v) { if (!N.wasmGlobInfo(this[H])[1]) throw new TypeError("set WebAssembly.Global.value): Can't set the value of an immutable global."); N.wasmGlobSet(this[H], v); }
    valueOf() { return N.wasmGlobGet(this[H]); }
    get type() { const [t, m] = N.wasmGlobInfo(this[H]); return { value: VN[t], mutable: m }; }
  }
  Object.defineProperty(Global.prototype, Symbol.toStringTag, { value: 'WebAssembly.Global' });
  const later = (f) => new Promise((res, rej) => setTimeout(() => { try { res(f()); } catch (e) { rej(e); } }, 0));
  const fromResp = async (r, who) => {
    r = await r;
    if (!(r instanceof Response)) throw new TypeError(who + ': Argument 0 must be provided and must be a Response');
    if (!r.ok) throw new TypeError(who + ': HTTP status code is not ok');
    return r.arrayBuffer();
  };
  const WA = {
    validate(b) { const u8 = toU8(b, 'WebAssembly.validate()'); try { new Module(u8); return true; } catch (e) { if (e instanceof CompileError) return false; throw e; } },
    compile(b) { let u8; try { u8 = toU8(b, 'WebAssembly.compile()').slice(); } catch (e) { return Promise.reject(e); } return later(() => new Module(u8)); },
    instantiate(b, imp) {
      if (b instanceof Module) return later(() => new Instance(b, imp));
      let u8; try { u8 = toU8(b, 'WebAssembly.instantiate()').slice(); } catch (e) { return Promise.reject(e); }
      return later(() => { const module = new Module(u8); return { module, instance: new Instance(module, imp) }; });
    },
    compileStreaming(r) { return fromResp(r, 'WebAssembly.compileStreaming()').then(b => new Module(b)); },
    instantiateStreaming(r, imp) { return fromResp(r, 'WebAssembly.instantiateStreaming()').then(b => { const module = new Module(b); return new Promise((res, rej) => setTimeout(() => { try { res({ module, instance: new Instance(module, imp) }); } catch (e) { rej(e); } }, 0)); }); }
  };
  for (const [k, v] of Object.entries({ Module, Instance, Table, Memory, Global, CompileError, LinkError, RuntimeError })) Object.defineProperty(WA, k, { value: v, writable: true, configurable: true, enumerable: false });
  Object.defineProperty(WA, Symbol.toStringTag, { value: 'WebAssembly' });
  Object.defineProperty(G, 'WebAssembly', { value: WA, writable: true, configurable: true, enumerable: false });
}
{
  const td = new TextDecoder();
  const loadText = (u, base) => {
    u = String(u);
    if (u.startsWith('blob:')) { const b = OBJURL.get(u); if (!b) throw new DOMException('Failed to load worker script: ' + u, 'NetworkError'); return typeof b._s === 'string' ? b._s : td.decode(b._bytes()); }
    if (u.startsWith('data:')) { const i = u.indexOf(','); const meta = u.slice(5, i), d = u.slice(i + 1); return /;base64/i.test(meta) ? td.decode(Uint8Array.from(atob(d), c => c.charCodeAt(0))) : decodeURIComponent(d); }
    const full = new URL(u, base || document.baseURI).href;
    const r = N.fetch(full, 'GET', null, null, true);
    if (!r[0] || r[0] >= 400) throw new DOMException('Failed to load worker script: ' + full, 'NetworkError');
    return td.decode(new Uint8Array(r[1]));
  };
  const AP_BAD = /\b(?:WebAssembly|indexedDB|OffscreenCanvas|caches|XMLHttpRequest|Blob|File|FileReader|FileReaderSync|URL|URLSearchParams|WebSocket|BroadcastChannel|MessageChannel|MessagePort|createImageBitmap|Atomics|SharedArrayBuffer|crypto|Intl|EventSource|locks|Request|FormData|ReadableStream|WritableStream|TransformStream|CompressionStream|DecompressionStream|ImageData|ImageBitmap|AbortController|AbortSignal|requestAnimationFrame|Notification|serviceWorker|EventTarget\s*\.prototype)\b|\bimport\s*\(|^\s*import\s+[\w{*'"]|^\s*import\s*[{*'"]|^\s*export\s/m;
  const AW = new Map();
  let awT = 0;
  const awPoll = () => {
    awT = 0;
    let evs = [];
    try { evs = N.wkPoll(); } catch (e) {}
    for (const [wid, ty, v] of evs) { const w = AW.get(wid); if (w) { try { w(ty, v); } catch (e) { reportErr(e); } } }
    if (AW.size) awT = N.setTimeout(awPoll, evs.length ? 0 : 6);
  };
  const awKick = () => { if (!awT) awT = N.setTimeout(awPoll, 0); };
  G.__nvxWorkerCores = () => ({ running: AW.size, secondCore: !!(N.wkOk && N.wkOk()) });
  class Worker extends EventTarget {
    constructor(url, opts) {
      super();
      if (url === undefined) throw new TypeError("Failed to construct 'Worker': 1 argument required, but only 0 present.");
      this.onmessage = null; this.onerror = null; this.onmessageerror = null;
      const w = this; let dead = false, mode = 0, wid = 0;
      const pend = [];
      const href = String(url).startsWith('blob:') || String(url).startsWith('data:') ? String(url) : new URL(String(url), document.baseURI).href;
      const ls = {};
      const sc = Object.create(null);
      const fireIn = (ev) => { const h = sc.onmessage; if (ev.type === 'message' && typeof h === 'function') { try { h.call(sc, ev); } catch (e) { wErr(e); } } for (const f of (ls[ev.type] || []).slice()) { try { typeof f === 'function' ? f.call(sc, ev) : f.handleEvent(ev); } catch (e) { wErr(e); } } };
      const wErr = (e) => { setTimeout(() => { const ev = new ErrorEvent('error', { message: e && e.message ? e.message : String(e), error: e, cancelable: true }); w.dispatchEvent(ev); if (!ev.defaultPrevented) reportErr(e); }, 0); };
      Object.assign(sc, {
        self: sc, globalThis: sc, name: (opts && opts.name) || '', onmessage: null, onmessageerror: null, onerror: null,
        location: { href, origin: N.origin(), toString() { return href; } },
        postMessage(d) { if (dead) return; let c; try { c = structuredClone(d); } catch (e) { throw e; } setTimeout(() => { if (!dead) w.dispatchEvent(new MessageEvent('message', { data: c })); }, 0); },
        close() { dead = true; },
        importScripts(...us) { for (const u of us) (0, eval)(loadText(u, href)); },
        addEventListener(t, f) { (ls[t] = ls[t] || []).push(f); }, removeEventListener(t, f) { const a = ls[t]; if (a) { const i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } }, dispatchEvent(ev) { fireIn(ev); return true; },
        setTimeout: (f, t, ...a) => setTimeout(() => { if (!dead) f(...a); }, t), clearTimeout, setInterval: (f, t, ...a) => { const id = setInterval(() => { if (dead) clearInterval(id); else f(...a); }, t); return id; }, clearInterval,
        console, navigator, performance, crypto, fetch: (...a) => fetch(...a), structuredClone, TextEncoder, TextDecoder, Blob, URL, Response, Request, Headers, indexedDB: G.indexedDB, queueMicrotask, atob, btoa, Promise, WebAssembly: G.WebAssembly
      });
      this._sc = sc;
      const apSend = (c) => { if (N.wkPost(wid, c) < 0) { pend.push(c); setTimeout(flush, 4); } };
      const flush = () => { if (dead || mode !== 2) return; while (pend.length) { const c = pend.shift(); if (N.wkPost(wid, c) < 0) { pend.unshift(c); setTimeout(flush, 4); return; } } awKick(); };
      this._post = (d) => {
        if (dead) return;
        const c = structuredClone(d);
        if (mode === 2) { if (pend.length) pend.push(c); else apSend(c); awKick(); return; }
        if (mode === 0) { pend.push(c); return; }
        setTimeout(() => { if (!dead) fireIn(new MessageEvent('message', { data: c })); }, 0);
      };
      this._kill = () => { dead = true; if (mode === 2) { N.wkTerm(wid); AW.delete(wid); } };
      const onAp = (ty, v) => {
        if (dead) return;
        if (ty === 11) w.dispatchEvent(new MessageEvent('message', { data: v }));
        else if (ty === 12) {
          const s = String(v), i = s.indexOf('\n'), msg = i < 0 ? s : s.slice(0, i), st = i < 0 ? '' : s.slice(i + 1);
          const lm = /:(\d+)(?::(\d+))?\)?\s*$/m.exec(st.split('\n')[0] || '');
          const ev = new ErrorEvent('error', { message: msg, filename: href, lineno: lm ? +lm[1] : 0, colno: lm && lm[2] ? +lm[2] : 0, cancelable: true });
          w.dispatchEvent(ev);
          if (!ev.defaultPrevented) N.log(msg + (st ? '\n' + st : ''), 3);
        } else if (ty === 13) { const s = String(v); N.log(s.slice(1), +s[0] || 0); }
        else if (ty === 14) {
          const [rid, q] = v || [];
          const kind = q && q[0], u = q && q[1], o = (q && q[2]) || {};
          if (kind === 'import') { let t = null; try { t = loadText(u, href); } catch (e) {} N.wkReply(wid, rid, t); awKick(); }
          else if (kind === 'fetch') {
            let full; try { full = new URL(String(u), href.startsWith('blob:') || href.startsWith('data:') ? document.baseURI : href).href; } catch (e) { N.wkReply(wid, rid, ['err', 'bad url']); return; }
            fetch(full, { method: o.method || 'GET', headers: o.headers || [], body: o.body === undefined ? null : o.body })
              .then(r => r.arrayBuffer().then(b => { if (!dead) N.wkReply(wid, rid, ['ok', r.status, r.statusText, r.url, [...r.headers], b]); }))
              .catch(e => { if (!dead) N.wkReply(wid, rid, ['err', String(e)]); })
              .finally(awKick);
          } else N.wkReply(wid, rid, null);
        } else if (ty === 15) { dead = true; N.wkTerm(wid); AW.delete(wid); }
      };
      setTimeout(() => {
        if (dead) return;
        let code;
        try { code = loadText(url); } catch (e) { const ev = new Event('error'); w.dispatchEvent(ev); return; }
        if (opts && opts.type === 'module' && /^\s*(import|export)\b/m.test(code)) { const ev = new ErrorEvent('error', { message: 'Module workers are not supported' }); w.dispatchEvent(ev); return; }
        let ap = false;
        try { ap = !!(N.wkOk && !N.isFrame() && !AP_BAD.test(code) && N.wkOk()); } catch (e) { ap = false; }
        if (ap) {
          wid = N.wkStart(code, href, (opts && opts.name) || '', navigator.userAgent);
          if (wid > 0) { mode = 2; AW.set(wid, onAp); flush(); awKick(); return; }
        }
        mode = 1;
        const q = pend.splice(0);
        for (const c of q) setTimeout(() => { if (!dead) fireIn(new MessageEvent('message', { data: c })); }, 0);
        try { (new Function('__wsc', 'with (__wsc) {\n' + code + '\n}')).call(sc, sc); } catch (e) { wErr(e); }
      }, 0);
    }
    postMessage(d) { this._post(d); }
    terminate() { this._kill(); }
  }
  G.Worker = Worker;
  {
    const pfetch = G.fetch;
    const keyOf = (r) => { const q = r instanceof Request ? r : new Request(r); return [q.method, q.url.split('#')[0]]; };
    const mkRes = (e) => { const r = new Response(e.b.slice(), { status: e.s, statusText: e.t, headers: e.h }); r.url = e.u; return r; };
    class Cache {
      constructor() { this._m = new Map(); }
      _find(r, o) { o = o || {}; const [m, u] = keyOf(r); if (m !== 'GET' && !o.ignoreMethod) return []; const out = []; for (const [k, e] of this._m) { let a = k, b = u; if (o.ignoreSearch) { a = a.split('?')[0]; b = b.split('?')[0]; } if (a === b) out.push([k, e]); } return out; }
      match(r, o) { const f = this._find(r, o); return Promise.resolve(f.length ? mkRes(f[0][1]) : undefined); }
      matchAll(r, o) { if (r === undefined) return Promise.resolve([...this._m.values()].map(mkRes)); return Promise.resolve(this._find(r, o).map(x => mkRes(x[1]))); }
      async put(r, res) {
        const q = r instanceof Request ? r : new Request(r);
        if (q.method !== 'GET') throw new TypeError("Failed to execute 'put' on 'Cache': Request method '" + q.method + "' is unsupported");
        if (!/^https?:/.test(q.url)) throw new TypeError("Failed to execute 'put' on 'Cache': Request scheme '" + q.url.split(':')[0] + "' is unsupported");
        if (!(res instanceof Response)) throw new TypeError("Failed to execute 'put' on 'Cache': parameter 2 is not of type 'Response'.");
        if (res.status === 206) throw new TypeError("Failed to execute 'put' on 'Cache': Partial response (status code 206) is unsupported");
        if (res.bodyUsed) throw new TypeError("Failed to execute 'put' on 'Cache': Response body is already used");
        const h = {}; res.headers.forEach((v, k) => { h[k] = v; });
        const b = new Uint8Array(await res.arrayBuffer());
        this._m.set(q.url.split('#')[0], { b, s: res.status, t: res.statusText, h, u: res.url || q.url });
      }
      async add(r) { return this.addAll([r]); }
      async addAll(list) {
        const rs = await Promise.all(list.map(async (x) => { const q = x instanceof Request ? x : new Request(x); const res = await pfetch(q); if (!res.ok) throw new TypeError("Failed to execute 'addAll' on 'Cache': Request failed"); return [q, res]; }));
        for (const [q, res] of rs) await this.put(q, res);
      }
      delete(r, o) { const f = this._find(r, o); for (const [k] of f) this._m.delete(k); return Promise.resolve(f.length > 0); }
      keys(r, o) { const ks = r === undefined ? [...this._m.keys()] : this._find(r, o).map(x => x[0]); return Promise.resolve(ks.map(u => new Request(u))); }
    }
    class CacheStorage {
      constructor() { this._c = new Map(); }
      open(n) { n = String(n); let c = this._c.get(n); if (!c) { c = new Cache(); this._c.set(n, c); } return Promise.resolve(c); }
      has(n) { return Promise.resolve(this._c.has(String(n))); }
      delete(n) { return Promise.resolve(this._c.delete(String(n))); }
      keys() { return Promise.resolve([...this._c.keys()]); }
      async match(r, o) { if (o && o.cacheName) { const c = this._c.get(o.cacheName); return c ? c.match(r, o) : undefined; } for (const c of this._c.values()) { const m = await c.match(r, o); if (m) return m; } return undefined; }
    }
    const caches = new CacheStorage();
    G.caches = caches; G.Cache = Cache; G.CacheStorage = CacheStorage;
    class ExtendableEvent extends Event { constructor(t, i) { super(t, i); this._w = []; } waitUntil(p) { this._w.push(Promise.resolve(p)); } }
    class ExtendableMessageEvent extends ExtendableEvent { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.source = i.source || null; this.ports = []; this.lastEventId = ''; } }
    class FetchEvent extends ExtendableEvent {
      constructor(t, i) { super(t, i); i = i || {}; this.request = i.request; this.clientId = i.clientId || ''; this.resultingClientId = ''; this.preloadResponse = Promise.resolve(undefined); this.handled = Promise.resolve(); this._rw = null; }
      respondWith(p) { if (this._rw) throw new DOMException("Failed to execute 'respondWith' on 'FetchEvent': The event has already been responded to.", 'InvalidStateError'); this._rw = Promise.resolve(p); }
    }
    Object.assign(G, { ExtendableEvent, ExtendableMessageEvent, FetchEvent });
    const REGK = 'registrations';
    const loadRegs = () => { try { return JSON.parse(N.storeGet(2, REGK) || '[]'); } catch (e) { return []; } };
    const saveRegs = (l) => { N.storeSet(2, REGK, JSON.stringify(l)); };
    const hashOf = (s) => { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return (h >>> 0).toString(16) + ':' + s.length; };
    const container = new EventTarget();
    container.oncontrollerchange = null; container.onmessage = null; container.onmessageerror = null;
    let readyRes; const readyP = new Promise(r => { readyRes = r; });
    const regs = new Map();
    const clientId = 'c' + Math.random().toString(36).slice(2);
    const client = { id: clientId, type: 'window', frameType: 'top-level', get url() { return location.href; }, focused: true, visibilityState: 'visible',
      postMessage(d) { const c = structuredClone(d); setTimeout(() => { const ev = new MessageEvent('message', { data: c, origin: location.origin, source: container.controller }); if (typeof container.onmessage === 'function') container.onmessage(ev); container.dispatchEvent(ev); }, 0); },
      focus() { return Promise.resolve(client); }, navigate(u) { location.href = u; return Promise.resolve(client); } };
    class ServiceWorker extends EventTarget {
      constructor(reg, url) { super(); this._reg = reg; this.scriptURL = url; this.state = 'parsed'; this.onstatechange = null; this.onerror = null; }
      postMessage(d) { const c = structuredClone(d); const g = this._reg._g; setTimeout(() => { if (g) g._fire(new ExtendableMessageEvent('message', { data: c, origin: location.origin, source: client })); }, 0); }
      _set(st) { this.state = st; const ev = new Event('statechange'); if (typeof this.onstatechange === 'function') this.onstatechange(ev); this.dispatchEvent(ev); }
    }
    class ServiceWorkerRegistration extends EventTarget {
      constructor(scope, url) { super(); this.scope = scope; this._url = url; this.installing = null; this.waiting = null; this.active = null; this.onupdatefound = null; this.updateViaCache = 'imports'; this.navigationPreload = { enable() { return Promise.resolve(); }, disable() { return Promise.resolve(); }, setHeaderValue() { return Promise.resolve(); }, getState() { return Promise.resolve({ enabled: false, headerValue: 'true' }); } }; this._g = null; this._gone = false; }
      unregister() { if (this._gone) return Promise.resolve(false); this._gone = true; regs.delete(this.scope); saveRegs(loadRegs().filter(r => r.scope !== this.scope)); return Promise.resolve(true); }
      update() { return install(this, true).then(() => this); }
      showNotification() { return Promise.reject(new TypeError('No notification permission has been granted for this origin.')); }
      getNotifications() { return Promise.resolve([]); }
    }
    const mkGlobal = (reg, sw, href) => {
      const ls = {};
      const g = Object.create(null);
      const errOut = (e) => reportErr(e);
      g._fire = (ev) => { const on = g['on' + ev.type]; if (typeof on === 'function') { try { on.call(g, ev); } catch (e) { errOut(e); } } for (const f of (ls[ev.type] || []).slice()) { try { typeof f === 'function' ? f.call(g, ev) : f.handleEvent(ev); } catch (e) { errOut(e); } } };
      g._has = (t) => typeof g['on' + t] === 'function' || (ls[t] && ls[t].length > 0);
      Object.assign(g, {
        self: g, globalThis: g, registration: reg, serviceWorker: sw, caches, oninstall: null, onactivate: null, onfetch: null, onmessage: null, onmessageerror: null, onerror: null, onpush: null, onsync: null, onnotificationclick: null,
        location: { href, origin: N.origin(), protocol: location.protocol, host: location.host, hostname: location.hostname, pathname: new URL(href).pathname, toString() { return href; } },
        clients: {
          claim() { if (reg.active && !container.controller) { container.controller = reg.active; reg._ctl = true; setTimeout(() => { const ev = new Event('controllerchange'); if (typeof container.oncontrollerchange === 'function') container.oncontrollerchange(ev); container.dispatchEvent(ev); }, 0); } return Promise.resolve(); },
          matchAll(o) { const inc = o && o.includeUncontrolled; return Promise.resolve(inc || container.controller === reg.active ? [client] : []); },
          get(id) { return Promise.resolve(id === clientId ? client : undefined); },
          openWindow() { return Promise.reject(new TypeError('Not allowed to open a window.')); }
        },
        skipWaiting() { return Promise.resolve(); },
        importScripts(...us) { for (const u of us) (0, eval)(loadText(u, href)); },
        addEventListener(t, f) { if (f) (ls[t] = ls[t] || []).push(f); }, removeEventListener(t, f) { const a = ls[t]; if (a) { const i = a.indexOf(f); if (i >= 0) a.splice(i, 1); } }, dispatchEvent(ev) { g._fire(ev); return true; },
        setTimeout, clearTimeout, setInterval, clearInterval, console, navigator, performance, crypto, fetch: (...a) => pfetch(...a), structuredClone, TextEncoder, TextDecoder, Blob, URL, URLSearchParams, Response, Request, Headers, indexedDB: G.indexedDB, queueMicrotask, atob, btoa, Promise,
        ExtendableEvent, ExtendableMessageEvent, FetchEvent, Cache, CacheStorage, Event, EventTarget, DOMException, AbortController, AbortSignal, ReadableStream, WebAssembly: G.WebAssembly
      });
      return g;
    };
    const waitAll = async (ev) => { for (let i = 0; i < ev._w.length; i++) await ev._w[i]; };
    const runScript = (g, code) => { (new Function('__wsc', 'with (__wsc) {\n' + code + '\n}')).call(g, g); };
    async function install(reg, isUpdate) {
      let code;
      try { code = loadText(reg._url); } catch (e) { throw new TypeError("Failed to register a ServiceWorker for scope ('" + reg.scope + "') with script ('" + reg._url + "'): A bad HTTP response code (404) was received when fetching the script."); }
      const h = hashOf(code);
      if (isUpdate && reg._hash === h) return;
      const sw = new ServiceWorker(reg, reg._url);
      const g = mkGlobal(reg, sw, reg._url);
      try { runScript(g, code); } catch (e) { throw new TypeError("Failed to register a ServiceWorker for scope ('" + reg.scope + "') with script ('" + reg._url + "'): ServiceWorker script evaluation failed"); }
      reg.installing = sw; sw._set('installing');
      const fu = new Event('updatefound'); if (typeof reg.onupdatefound === 'function') reg.onupdatefound(fu); reg.dispatchEvent(fu);
      await new Promise(r => setTimeout(r, 0));
      const iev = new ExtendableEvent('install'); g._fire(iev);
      try { await waitAll(iev); } catch (e) { reg.installing = null; sw._set('redundant'); return; }
      const old = reg.active;
      reg.installing = null; reg.waiting = sw; sw._set('installed');
      reg.waiting = null; reg.active = sw; reg._g = g; reg._hash = h; sw._set('activating');
      if (old) old._set('redundant');
      if (container.controller === old && old) container.controller = sw;
      if (inScope(reg)) readyRes(reg);
      const aev = new ExtendableEvent('activate'); g._fire(aev);
      try { await waitAll(aev); } catch (e) {}
      sw._set('activated');
      const l = loadRegs().filter(r => r.scope !== reg.scope); l.push({ scope: reg.scope, url: reg._url, hash: h }); saveRegs(l);
    }
    const inScope = (reg) => location.href.startsWith(reg.scope);
    const best = () => { let b = null; for (const r of regs.values()) if (!r._gone && r.active && inScope(r) && (!b || r.scope.length > b.scope.length)) b = r; return b; };
    container.controller = null;
    container.register = function (url, opts) {
      return new Promise((resolve, reject) => {
        let su, sc;
        try { su = new URL(String(url), document.baseURI); } catch (e) { reject(new TypeError("Failed to register a ServiceWorker: Invalid script URL")); return; }
        if (!/^https?:$/.test(su.protocol)) { reject(new TypeError("Failed to register a ServiceWorker: The URL protocol of the script ('" + su.href + "') is not supported.")); return; }
        if (su.origin !== location.origin) { reject(new DOMException("Failed to register a ServiceWorker: The origin of the provided scriptURL ('" + su.origin + "') does not match the current origin ('" + location.origin + "').", 'SecurityError')); return; }
        try { sc = new URL(opts && opts.scope !== undefined ? String(opts.scope) : './', su.href).href; } catch (e) { reject(new TypeError('Invalid scope')); return; }
        const maxScope = su.href.slice(0, su.href.lastIndexOf('/') + 1);
        if (!sc.startsWith(maxScope)) { reject(new DOMException("Failed to register a ServiceWorker for scope ('" + sc + "') with script ('" + su.href + "'): The path of the provided scope ('" + new URL(sc).pathname + "') is not under the max scope allowed ('" + new URL(maxScope).pathname + "').", 'SecurityError')); return; }
        let reg = regs.get(sc);
        if (reg && !reg._gone) {
          if (reg._url === su.href && reg.active) { resolve(reg); return; }
          const prev = reg._url; reg._url = su.href;
          setTimeout(() => { install(reg, false).then(() => resolve(reg), (e) => { reg._url = prev; reject(e); }); }, 0);
          return;
        }
        reg = new ServiceWorkerRegistration(sc, su.href); regs.set(sc, reg);
        let done = false;
        setTimeout(() => { install(reg, false).then(() => { if (!done) { done = true; resolve(reg); } }, (e) => { regs.delete(sc); if (!done) { done = true; reject(e); } }); }, 0);
        setTimeout(() => { if (!done && reg.installing) { done = true; resolve(reg); } }, 0);
      });
    };
    container.getRegistration = (u) => { const href = new URL(u === undefined ? location.href : String(u), document.baseURI).href; let b; for (const r of regs.values()) if (!r._gone && href.startsWith(r.scope) && (!b || r.scope.length > b.scope.length)) b = r; return Promise.resolve(b); };
    container.getRegistrations = () => Promise.resolve([...regs.values()].filter(r => !r._gone));
    container.ready = readyP;
    container.startMessages = () => {};
    G.ServiceWorker = ServiceWorker; G.ServiceWorkerRegistration = ServiceWorkerRegistration; G.ServiceWorkerContainer = EventTarget;
    for (const r of loadRegs()) {
      if (!r || typeof r.scope !== 'string' || !r.scope.startsWith(location.origin + '/')) continue;
      const reg = new ServiceWorkerRegistration(r.scope, r.url); reg._hash = r.hash;
      const sw = new ServiceWorker(reg, r.url); sw.state = 'activated'; reg.active = sw; reg._lazy = true; regs.set(r.scope, reg);
    }
    const ctl = best();
    if (ctl) { container.controller = ctl.active; ctl._ctl = true; readyRes(ctl); }
    const wake = (reg) => {
      if (!reg._lazy) return;
      reg._lazy = false;
      let code; try { code = loadText(reg._url); } catch (e) { return; }
      const g = mkGlobal(reg, reg.active, reg._url);
      try { runScript(g, code); reg._g = g; } catch (e) { reportErr(e); }
      if (hashOf(code) !== reg._hash) setTimeout(() => install(reg, true).catch(() => {}), 0);
    };
    for (const r of regs.values()) if (r._lazy) { const orig = r.active.postMessage; r.active.postMessage = function (d) { wake(r); return orig.call(this, d); }; }
    G.fetch = function (input, init) {
      const c = container.controller;
      if (!c) return pfetch(input, init);
      const reg = c._reg;
      let req;
      try { req = input instanceof Request && !init ? input : new Request(input, init); } catch (e) { return Promise.reject(e); }
      if (!req.url.startsWith(location.origin + '/') && !/^https?:/.test(req.url)) return pfetch(input, init);
      wake(reg);
      const g = reg._g;
      if (!g || !g._has('fetch')) return pfetch(input, init);
      const ev = new FetchEvent('fetch', { request: req, clientId, cancelable: true });
      g._fire(ev);
      if (!ev._rw) return pfetch(input, init);
      return ev._rw.then((r) => { if (!(r instanceof Response)) throw new TypeError('Failed to fetch'); if (r.status === 0 && r.type !== 'opaque') throw new TypeError('Failed to fetch'); return r; }, () => { throw new TypeError('Failed to fetch'); });
    };
    try { Object.defineProperty(navigator, 'serviceWorker', { value: container, configurable: true, enumerable: true }); } catch (e) { navigator.serviceWorker = container; }
  }
}

{
  const def0 = (n, v) => { if (G[n] === undefined) { try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true, enumerable: false }); } catch (e) { G[n] = v; } } };
  const stub = (n, base) => { const C = base ? class extends base {} : class {}; Object.defineProperty(C, 'name', { value: n }); def0(n, C); return C; };
  const ev = (n, f) => { const C = class extends Event { constructor(t, i) { super(t, i); i = i || {}; for (const k of f) this[k] = i[k] !== undefined ? i[k] : null; } }; Object.defineProperty(C, 'name', { value: n }); def0(n, C); };
  ev('ToggleEvent', ['oldState', 'newState']); ev('PromiseRejectionEvent', ['promise', 'reason']); ev('FormDataEvent', ['formData']); ev('BeforeUnloadEvent', []); ev('PageTransitionEvent', ['persisted']); ev('TrackEvent', ['track']); ev('BlobEvent', ['data']); ev('SecurityPolicyViolationEvent', ['violatedDirective', 'blockedURI']); ev('AnimationPlaybackEvent', ['currentTime', 'timelineTime']); ev('PageRevealEvent', ['viewTransition']); ev('PageSwapEvent', ['viewTransition', 'activation']); ev('CommandEvent', ['command', 'source']); ev('SnapEvent', ['snapTargetBlock', 'snapTargetInline']); ev('NavigateEvent', ['navigationType', 'destination', 'canIntercept']);
  if (G.UIEvent) { const C = class CompositionEvent extends UIEvent { constructor(t, i) { super(t, i); this.data = (i && i.data) || ''; } }; def0('CompositionEvent', C); const T = class TextEvent extends UIEvent { constructor(t, i) { super(t, i); this.data = (i && i.data) || ''; } initTextEvent(t, b, c, v, d) { this.initEvent(t, b, c); this.data = d; } }; def0('TextEvent', T); }
  for (const n of ['TreeWalker', 'NodeIterator', 'Attr', 'NamedNodeMap', 'DOMStringMap', 'DOMStringList', 'DOMRectList', 'ValidityState', 'FileList', 'TextMetrics', 'BarProp', 'MessagePort', 'DOMImplementation', 'DocumentType', 'ProcessingInstruction', 'CDATASection', 'AbstractRange', 'StaticRange', 'CaretPosition', 'CustomStateSet', 'DOMQuad', 'Location', 'History', 'Navigator', 'Screen', 'Crypto', 'SubtleCrypto', 'Permissions', 'PermissionStatus', 'Geolocation', 'GeolocationPosition', 'GeolocationCoordinates', 'GeolocationPositionError', 'NetworkInformation', 'UserActivation', 'VisualViewport', 'StyleSheet', 'StyleSheetList', 'CSSRule', 'CSSRuleList', 'CSSStyleDeclaration', 'CSSStyleRule', 'CSSGroupingRule', 'CSSConditionRule', 'CSSMediaRule', 'CSSSupportsRule', 'CSSContainerRule', 'CSSImportRule', 'CSSFontFaceRule', 'CSSKeyframeRule', 'CSSKeyframesRule', 'CSSPageRule', 'CSSNamespaceRule', 'CSSLayerBlockRule', 'CSSLayerStatementRule', 'CSSPropertyRule', 'CSSScopeRule', 'CSSStartingStyleRule', 'CSSCounterStyleRule', 'CSSStyleValue', 'StylePropertyMap', 'StylePropertyMapReadOnly', 'ResizeObserverEntry', 'ResizeObserverSize', 'IntersectionObserverEntry', 'ReadableStreamDefaultReader', 'ReadableStreamDefaultController', 'ReadableByteStreamController', 'ReadableStreamBYOBReader', 'ReadableStreamBYOBRequest', 'WritableStreamDefaultWriter', 'WritableStreamDefaultController', 'TransformStreamDefaultController', 'XMLHttpRequestEventTarget', 'XMLHttpRequestUpload', 'TextTrack', 'TextTrackList', 'TextTrackCue', 'TextTrackCueList', 'VTTCue', 'TimeRanges', 'RadioNodeList', 'HTMLAllCollection', 'HTMLFormControlsCollection', 'HTMLOptionsCollection', 'DataTransferItem', 'DataTransferItemList', 'Worklet', 'MathMLElement', 'Highlight', 'XPathEvaluator', 'XPathExpression', 'DOMMatrixReadOnly', 'DOMPointReadOnly', 'QuotaExceededError', 'OverconstrainedError', 'DOMError', 'FeaturePolicy', 'InputDeviceCapabilities']) {
    if (n === 'DOMMatrixReadOnly' && G.DOMMatrix) { def0(n, G.DOMMatrix); continue; }
    if (n === 'DOMPointReadOnly' && G.DOMPoint) { def0(n, G.DOMPoint); continue; }
    if (n === 'CSSStyleValue' || n === 'Highlight') { stub(n); continue; }
    stub(n);
  }
  if (G.SVGElement) for (const n of ['SVGAElement', 'SVGCircleElement', 'SVGClipPathElement', 'SVGDefsElement', 'SVGDescElement', 'SVGEllipseElement', 'SVGFilterElement', 'SVGForeignObjectElement', 'SVGGElement', 'SVGGeometryElement', 'SVGGradientElement', 'SVGImageElement', 'SVGLineElement', 'SVGLinearGradientElement', 'SVGMarkerElement', 'SVGMaskElement', 'SVGMetadataElement', 'SVGPathElement', 'SVGPatternElement', 'SVGPolygonElement', 'SVGPolylineElement', 'SVGRadialGradientElement', 'SVGRectElement', 'SVGScriptElement', 'SVGStopElement', 'SVGStyleElement', 'SVGSwitchElement', 'SVGSymbolElement', 'SVGTSpanElement', 'SVGTextContentElement', 'SVGTextElement', 'SVGTextPathElement', 'SVGTextPositioningElement', 'SVGTitleElement', 'SVGUseElement', 'SVGViewElement', 'SVGAnimationElement', 'SVGAnimateElement', 'SVGSetElement']) def0(n, G.SVGElement);
  for (const n of ['SVGLength', 'SVGNumber', 'SVGAngle', 'SVGTransform', 'SVGTransformList', 'SVGPointList', 'SVGLengthList', 'SVGNumberList', 'SVGStringList', 'SVGPreserveAspectRatio', 'SVGAnimatedLength', 'SVGAnimatedString', 'SVGAnimatedNumber', 'SVGAnimatedRect', 'SVGAnimatedTransformList', 'SVGAnimatedBoolean', 'SVGAnimatedEnumeration', 'SVGAnimatedInteger', 'SVGAnimatedAngle', 'SVGAnimatedLengthList', 'SVGAnimatedNumberList', 'SVGAnimatedPreserveAspectRatio', 'SVGUnitTypes']) stub(n);
  { const SVGP = G.SVGElement && G.SVGElement.prototype; if (SVGP && !('href' in SVGP)) { const HT = new Set(['a', 'image', 'use', 'lineargradient', 'radialgradient', 'pattern', 'textpath', 'feimage', 'mpath', 'script', 'filter']); const AS = G.SVGAnimatedString; const WM = new WeakMap(); const XL = 'http://www.w3.org/1999/xlink';
    Object.defineProperty(SVGP, 'href', { configurable: true, enumerable: true, get() { if (!HT.has(String(this.localName).toLowerCase())) return undefined; let o = WM.get(this); if (o) return o; const el = this; o = Object.create(AS ? AS.prototype : Object.prototype); Object.defineProperty(o, 'baseVal', { enumerable: true, get() { const v = el.getAttribute('href'); if (v !== null) return v; const x = el.getAttributeNS ? el.getAttributeNS(XL, 'href') : null; return x !== null ? x : (el.getAttribute('xlink:href') || ''); }, set(v) { el.setAttribute('href', String(v)); } }); Object.defineProperty(o, 'animVal', { enumerable: true, get() { return o.baseVal; } }); WM.set(this, o); return o; } }); } }
  if (G.DOMMatrix) def0('SVGMatrix', G.DOMMatrix);
  if (G.DOMPoint) def0('SVGPoint', G.DOMPoint);
  if (G.DOMRect) def0('SVGRect', G.DOMRect);
  if (!G.TaskSignal && G.AbortSignal) {
    class TaskSignal extends AbortSignal { constructor() { super(); this.priority = 'user-visible'; this.onprioritychange = null; } static any(l, o) { const s = AbortSignal.any(l); s.priority = (o && o.priority && o.priority.priority) || 'user-visible'; return s; } }
    class TaskController extends AbortController { constructor(o) { super(); this.signal.priority = (o && o.priority) || 'user-visible'; } setPriority(p) { const o = this.signal.priority; this.signal.priority = p; const e = new Event('prioritychange'); e.previousPriority = o; this.signal.dispatchEvent(e); } }
    def0('TaskSignal', TaskSignal); def0('TaskController', TaskController);
    class Scheduler { postTask(cb, o) { return G.scheduler.postTask(cb, o); } yield() { return G.scheduler.yield(); } }
    def0('Scheduler', Scheduler); def0('Scheduling', class Scheduling { isInputPending() { return false; } });
  }
  if (G.document && !document.getAnimations) document.getAnimations = () => [];
  if (G.navigator) {
    if (!navigator.share) { navigator.share = () => Promise.reject(new DOMException('Share is not supported on this device.', 'NotAllowedError')); navigator.canShare = () => false; }
    if (!navigator.userActivation || !('isActive' in Object.getPrototypeOf(navigator.userActivation) || Object.getOwnPropertyDescriptor(navigator.userActivation, 'isActive').get)) { const ua = {}; Object.defineProperty(ua, 'isActive', { get() { return !!G.__actT && Date.now() - G.__actT < 5000; }, enumerable: true }); Object.defineProperty(ua, 'hasBeenActive', { get() { return !!G.__actT; }, enumerable: true }); try { Object.defineProperty(navigator, 'userActivation', { value: ua, configurable: true }); } catch (e) {} }
    for (const t of ['pointerdown', 'mousedown', 'keydown', 'click', 'touchend']) G.addEventListener(t, (e) => { if (e.isTrusted && !(t === 'keydown' && (e.key === 'Escape' || e.altKey || e.ctrlKey || e.metaKey))) G.__actT = Date.now(); }, true);
    if (!navigator.connection) navigator.connection = { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, type: 'ethernet', addEventListener() {}, removeEventListener() {} };
  }
  if (!G.CloseWatcher) { class CloseWatcher extends EventTarget { constructor() { super(); this.oncancel = null; this.onclose = null; this._on = true; CW.push(this); } requestClose() { if (!this._on) return; const c = new Event('cancel', { cancelable: true }); if (this.oncancel) this.oncancel(c); this.dispatchEvent(c); if (!c.defaultPrevented) this.close(); } close() { if (!this._on) return; this._on = false; const i = CW.indexOf(this); if (i >= 0) CW.splice(i, 1); const e = new Event('close'); if (this.onclose) this.onclose(e); this.dispatchEvent(e); } destroy() { this._on = false; const i = CW.indexOf(this); if (i >= 0) CW.splice(i, 1); } } const CW = []; def0('CloseWatcher', CloseWatcher); if (G.__onDoc) G.__onDoc('keydown', (e) => { if (e.key === 'Escape' && CW.length) CW[CW.length - 1].requestClose(); }); }
  const crcT = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  const crc32 = (u) => { let c = 0xFFFFFFFF; for (let i = 0; i < u.length; i++) c = crcT[(c ^ u[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
  const adler = (u) => { let a = 1, b = 0; for (let i = 0; i < u.length; i++) { a = (a + u[i]) % 65521; b = (b + a) % 65521; } return ((b << 16) | a) >>> 0; };
  const stored0 = (u) => { const nb = Math.max(1, Math.ceil(u.length / 65535)); const o = new Uint8Array(u.length + nb * 5); let p = 0; for (let i = 0; i < nb; i++) { const s = i * 65535, l = Math.min(65535, u.length - s); o[p++] = i === nb - 1 ? 1 : 0; o[p++] = l & 255; o[p++] = l >> 8; o[p++] = ~l & 255; o[p++] = (~l >> 8) & 255; o.set(u.subarray(s, s + l), p); p += l; } return o; };
  const stored = (u) => { try { const r = N.deflateRaw(u); if (r) return r; } catch (e) {} return stored0(u); };
  const fmtOk = (f) => { if (f !== 'gzip' && f !== 'deflate' && f !== 'deflate-raw') throw new TypeError("Failed to construct: Unsupported compression format: '" + f + "'"); };
  const cat = (ch) => { let n = 0; for (const c of ch) n += c.length; const o = new Uint8Array(n); let k = 0; for (const c of ch) { o.set(c, k); k += c.length; } return o; };
  const mkTS = (fn) => { const ch = []; return new TransformStream({ transform(c) { ch.push(c instanceof Uint8Array ? c : new Uint8Array(c.buffer ? c.buffer.slice(c.byteOffset, c.byteOffset + c.byteLength) : c)); }, flush(ctl) { ctl.enqueue(fn(cat(ch))); } }); };
  if (!G.CompressionStream && G.TransformStream) {
    class CompressionStream { constructor(f) { fmtOk(f); const ts = mkTS((u) => { const d = stored(u); if (f === 'deflate-raw') return d; if (f === 'deflate') { const o = new Uint8Array(d.length + 6); o[0] = 0x78; o[1] = 0x01; o.set(d, 2); const a = adler(u); o[d.length + 2] = a >>> 24; o[d.length + 3] = (a >>> 16) & 255; o[d.length + 4] = (a >>> 8) & 255; o[d.length + 5] = a & 255; return o; } const o = new Uint8Array(d.length + 18); o.set([0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255]); o.set(d, 10); const c = crc32(u), L = u.length >>> 0; const q = d.length + 10; o[q] = c & 255; o[q + 1] = (c >>> 8) & 255; o[q + 2] = (c >>> 16) & 255; o[q + 3] = c >>> 24; o[q + 4] = L & 255; o[q + 5] = (L >>> 8) & 255; o[q + 6] = (L >>> 16) & 255; o[q + 7] = L >>> 24; return o; }); this.readable = ts.readable; this.writable = ts.writable; } }
    class DecompressionStream { constructor(f) { fmtOk(f); const ts = mkTS((u) => { let s = 0; if (f === 'gzip') { if (u[0] !== 0x1f || u[1] !== 0x8b) throw new TypeError('The compressed data was not valid'); const fl = u[3]; s = 10; if (fl & 4) s += 2 + (u[s] | (u[s + 1] << 8)); if (fl & 8) { while (s < u.length && u[s]) s++; s++; } if (fl & 16) { while (s < u.length && u[s]) s++; s++; } if (fl & 2) s += 2; } else if (f === 'deflate') s = 2; const r = N.inflateRaw(u.subarray(s)); if (!r) throw new TypeError('The compressed data was not valid'); return r; }); this.readable = ts.readable; this.writable = ts.writable; } }
    def0('CompressionStream', CompressionStream); def0('DecompressionStream', DecompressionStream);
  }
}
{
  const def0 = (n, v) => { if (G[n] === undefined) { try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true, enumerable: false }); } catch (e) { G[n] = v; } } };
  const defR = (n, v) => { try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true, enumerable: false }); } catch (e) { G[n] = v; } };
  const named = (n, C) => { Object.defineProperty(C, 'name', { value: n }); def0(n, C); return C; };
  const rej = (n, m) => Promise.reject(new DOMException(m || 'Not supported', n));
  {
    const ease = (s) => { s = String(s || 'linear').trim(); if (s === 'linear') return (t) => t; const bz = (a, b, c, d) => (t) => { let lo = 0, hi = 1, x = t; for (let i = 0; i < 24; i++) { const m = (lo + hi) / 2; const xm = 3 * a * m * (1 - m) * (1 - m) + 3 * c * m * m * (1 - m) + m * m * m; if (xm < t) lo = m; else hi = m; x = m; } return 3 * b * x * (1 - x) * (1 - x) + 3 * d * x * x * (1 - x) + x * x * x; };
      if (s === 'ease') return bz(0.25, 0.1, 0.25, 1); if (s === 'ease-in') return bz(0.42, 0, 1, 1); if (s === 'ease-out') return bz(0, 0, 0.58, 1); if (s === 'ease-in-out') return bz(0.42, 0, 0.58, 1);
      const m = /^cubic-bezier\(([^)]*)\)$/.exec(s); if (m) { const v = m[1].split(',').map(Number); if (v.length === 4 && v.every(isFinite)) return bz(v[0], v[1], v[2], v[3]); }
      const st = /^steps\((\d+)(?:\s*,\s*([a-z-]+))?\)$/.exec(s); if (st) { const n = +st[1] || 1, start = st[2] === 'start' || st[2] === 'jump-start'; return (t) => Math.min(1, (start ? Math.ceil(t * n) : Math.floor(t * n)) / n); }
      if (s === 'step-start') return (t) => (t > 0 ? 1 : 0); if (s === 'step-end') return (t) => (t >= 1 ? 1 : 0); return (t) => t; };
    const camel = (p) => p === 'float' ? 'cssFloat' : p.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
    const kebab = (p) => p === 'cssFloat' ? 'float' : p.replace(/[A-Z]/g, (c) => '-' + c.toLowerCase());
    const col = (s) => { s = String(s).trim(); let m = /^#([0-9a-f]{3,8})$/i.exec(s); if (m) { let h = m[1]; if (h.length <= 4) h = h.split('').map((c) => c + c).join(''); const v = parseInt(h.slice(0, 6), 16); return [(v >> 16) & 255, (v >> 8) & 255, v & 255, h.length === 8 ? parseInt(h.slice(6), 16) / 255 : 1]; } m = /^rgba?\(([^)]*)\)$/i.exec(s); if (m) { const p = m[1].split(/[\s,/]+/).filter(Boolean).map((x) => x.endsWith('%') ? parseFloat(x) * 2.55 : parseFloat(x)); if (p.length >= 3) return [p[0], p[1], p[2], p.length > 3 ? (m[1].includes('%') && p[3] > 1 ? p[3] / 255 : p[3]) : 1]; } if (s === 'transparent') return [0, 0, 0, 0]; return null; };
    const numRe = /-?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?/gi;
    const lerpStr = (a, b, t) => {
      a = String(a); b = String(b);
      const ca = col(a), cb = col(b);
      if (ca && cb) { const r = ca.map((v, i) => v + (cb[i] - v) * t); return 'rgba(' + Math.round(r[0]) + ', ' + Math.round(r[1]) + ', ' + Math.round(r[2]) + ', ' + Math.round(r[3] * 1000) / 1000 + ')'; }
      const na = a.match(numRe) || [], nb = b.match(numRe) || [];
      const sa = a.split(numRe), sb = b.split(numRe);
      if (na.length && na.length === nb.length && sa.join('|') === sb.join('|')) { let o = sa[0]; for (let i = 0; i < na.length; i++) o += (+na[i] + (+nb[i] - +na[i]) * t).toFixed(4).replace(/\.?0+$/, '') + sa[i + 1]; return o; }
      if (a === 'none' && /\(/.test(b)) { const z = b.replace(/(translate[XYZ3d]*|rotate[XYZ3d]*|skew[XY]*)\(([^)]*)\)/g, (m0, f, args) => f + '(' + args.replace(numRe, '0') + ')').replace(/(scale[XYZ3d]*)\(([^)]*)\)/g, (m0, f, args) => f + '(' + args.replace(numRe, '1') + ')'); return lerpStr(z, b, t); }
      if (b === 'none' && /\(/.test(a)) { const z = a.replace(/(translate[XYZ3d]*|rotate[XYZ3d]*|skew[XY]*)\(([^)]*)\)/g, (m0, f, args) => f + '(' + args.replace(numRe, '0') + ')').replace(/(scale[XYZ3d]*)\(([^)]*)\)/g, (m0, f, args) => f + '(' + args.replace(numRe, '1') + ')'); return lerpStr(a, z, t); }
      return t < 0.5 ? a : b;
    };
    const normFrames = (kf) => {
      if (!kf) return [];
      let frames;
      if (Array.isArray(kf)) frames = kf.map((f) => Object.assign({}, f));
      else { const keys = Object.keys(kf).filter((k) => k !== 'offset' && k !== 'easing' && k !== 'composite'); let n = 0; for (const k of keys) n = Math.max(n, Array.isArray(kf[k]) ? kf[k].length : 1); frames = []; for (let i = 0; i < n; i++) { const f = {}; for (const k of keys) { const v = Array.isArray(kf[k]) ? kf[k] : [kf[k]]; if (i < v.length) f[k] = v[i]; } frames.push(f); } if (Array.isArray(kf.offset)) kf.offset.forEach((o, i) => { if (frames[i]) frames[i].offset = o; }); }
      const n = frames.length;
      frames.forEach((f, i) => { if (f.offset == null) f._o = n === 1 ? 1 : i / (n - 1); else f._o = +f.offset; });
      return frames;
    };
    const anims = new Set();
    let ticking = false;
    const tick = () => { ticking = false; for (const a of [...anims]) a._step(); if (anims.size) { ticking = true; requestAnimationFrame(tick); } };
    const kick = () => { if (!ticking) { ticking = true; requestAnimationFrame(tick); } };
    class AnimationEffect { constructor(t) { this._t = t; } getTiming() { return Object.assign({}, this._t); } getComputedTiming() { return Object.assign({ activeDuration: this._t.duration * this._t.iterations, endTime: this._t.delay + this._t.duration * this._t.iterations + this._t.endDelay }, this._t); } updateTiming(t) { Object.assign(this._t, t || {}); } }
    class KeyframeEffect extends AnimationEffect {
      constructor(target, kf, opt) { const o = typeof opt === 'number' ? { duration: opt } : Object.assign({}, opt || {}); super({ duration: +o.duration || 0, delay: +o.delay || 0, endDelay: +o.endDelay || 0, iterations: o.iterations === undefined ? 1 : +o.iterations, direction: o.direction || 'normal', easing: o.easing || 'linear', fill: o.fill || 'auto', iterationStart: +o.iterationStart || 0 }); this.target = target; this.composite = o.composite || 'replace'; this.pseudoElement = o.pseudoElement || null; this._kf = normFrames(kf); }
      getKeyframes() { return this._kf.map((f) => { const o = Object.assign({}, f); o.offset = f._o; delete o._o; if (!o.easing) o.easing = 'linear'; return o; }); }
      setKeyframes(kf) { this._kf = normFrames(kf); }
    }
    let animId = 0;
    class Animation extends EventTarget {
      constructor(effect, timeline) { super(); this.effect = effect || null; this.timeline = timeline || (document && document.timeline) || null; this.id = ''; this._rate = 1; this._start = null; this._hold = 0; this._state = 'idle'; this._saved = null; this.onfinish = null; this.oncancel = null; this.onremove = null; this.replaceState = 'active'; this._n = ++animId; this._mk(); }
      _mk() { this.finished = new Promise((res, rj) => { this._fres = res; this._frej = rj; }); this.finished.catch(() => {}); this.ready = Promise.resolve(this); }
      get playState() { return this._state; }
      get pending() { return false; }
      get playbackRate() { return this._rate; }
      set playbackRate(v) { const ct = this.currentTime; this._rate = +v || 0; if (ct != null) this.currentTime = ct; }
      updatePlaybackRate(v) { this.playbackRate = v; }
      _now() { return performance.now(); }
      _total() { const t = this.effect ? this.effect._t : { delay: 0, duration: 0, iterations: 1, endDelay: 0 }; return t.delay + t.duration * (t.iterations === Infinity ? 1e12 : t.iterations) + t.endDelay; }
      get currentTime() { if (this._state === 'idle') return null; if (this._state === 'paused' || this._state === 'finished') return this._hold; return (this._now() - this._start) * this._rate; }
      set currentTime(v) { if (v == null) return; v = +v; if (this._state === 'running') this._start = this._now() - v / (this._rate || 1); else { this._hold = v; if (this._state === 'idle') this._state = 'paused'; } this._apply(v); }
      get startTime() { return this._state === 'running' ? this._start : null; }
      set startTime(v) { if (v == null) return; this._start = +v; this._state = 'running'; anims.add(this); kick(); }
      _save() { if (this._saved || !this.effect || !this.effect.target || !this.effect.target.style) return; const st = this.effect.target.style; if (N.noTrans) N.noTrans(this.effect.target, 1); this._saved = {}; for (const f of this.effect._kf) for (const k of Object.keys(f)) if (k !== 'offset' && k !== '_o' && k !== 'easing' && k !== 'composite') { const p = kebab(k); if (!(p in this._saved)) this._saved[p] = st.getPropertyValue(p); } }
      _restore() { if (!this._saved || !this.effect || !this.effect.target) return; const tg = this.effect.target; const st = tg.style; for (const p in this._saved) { if (this._saved[p]) st.setProperty(p, this._saved[p]); else st.removeProperty(p); } this._saved = null; if (N.noTrans) { try { getComputedStyle(tg).opacity; } catch (e) {} N.noTrans(tg, 0); } }
      _apply(time) {
        const ef = this.effect; if (!ef || !ef.target || !ef.target.style) return;
        const t = ef._t, fr = ef._kf; if (!fr.length) return;
        const local = time - t.delay; const dur = t.duration; const its = t.iterations;
        const fill = t.fill === 'auto' ? 'none' : t.fill;
        let p;
        if (local < 0) { if (fill !== 'backwards' && fill !== 'both') { this._restore(); return; } p = t.iterationStart % 1; }
        else if (dur <= 0 || local >= dur * its) { if (local >= dur * its && fill !== 'forwards' && fill !== 'both' && this._state !== 'paused') { this._restore(); return; } const end = its === Infinity ? 1 : its + t.iterationStart; p = end % 1 === 0 && end > 0 ? 1 : end % 1; var itn = Math.ceil(end) - 1; }
        else { const raw = local / dur + t.iterationStart; itn = Math.floor(raw); p = raw - itn; }
        let dir = t.direction, fwd = true; if (dir === 'reverse') fwd = false; else if (dir === 'alternate') fwd = ((itn || 0) % 2) === 0; else if (dir === 'alternate-reverse') fwd = ((itn || 0) % 2) === 1;
        if (!fwd) p = 1 - p;
        p = ease(t.easing)(p);
        this._save();
        const st = ef.target.style;
        const props = new Set(); for (const f of fr) for (const k of Object.keys(f)) if (k !== 'offset' && k !== '_o' && k !== 'easing' && k !== 'composite') props.add(k);
        for (const k of props) {
          const ks = fr.filter((f) => f[k] !== undefined);
          if (!ks.length) continue;
          const pk = kebab(k);
          const base = this._saved && this._saved[pk] ? this._saved[pk] : getComputedStyle(ef.target).getPropertyValue(pk);
          const list = ks.slice(); if (list[0]._o > 0) list.unshift({ _o: 0, [k]: base }); if (list[list.length - 1]._o < 1) list.push({ _o: 1, [k]: base });
          let i = 0; while (i < list.length - 2 && p > list[i + 1]._o) i++;
          const a = list[i], b = list[Math.min(i + 1, list.length - 1)];
          const span = b._o - a._o; let lt = span > 0 ? (p - a._o) / span : 1; if (a.easing) lt = ease(a.easing)(lt);
          st.setProperty(pk, lerpStr(a[k], b[k], Math.max(0, Math.min(1, lt))));
        }
      }
      _step() {
        if (this._state !== 'running') { anims.delete(this); return; }
        const ct = this.currentTime; const total = this._total();
        if ((this._rate >= 0 && ct >= total) || (this._rate < 0 && ct <= 0)) { this._hold = this._rate >= 0 ? total : 0; this._state = 'finished'; anims.delete(this); this._apply(this._hold); const e = new Event('finish'); try { e.currentTime = this._hold; e.timelineTime = performance.now(); } catch (x) {} this._fres(this); N.setTimeout(() => this.dispatchEvent(e), 0); return; }
        this._apply(ct);
      }
      play() { const ct = this._state === 'idle' || this._state === 'finished' ? (this._rate >= 0 ? (this._state === 'finished' ? 0 : this._hold || 0) : this._total()) : this.currentTime; if (this._state === 'finished') this._mk(); this._start = this._now() - (ct || 0) / (this._rate || 1); this._state = 'running'; anims.add(this); this._apply(ct || 0); kick(); }
      pause() { if (this._state === 'paused') return; this._hold = this.currentTime || 0; this._state = 'paused'; anims.delete(this); }
      finish() { this._start = this._now() - (this._rate >= 0 ? this._total() : 0) / (this._rate || 1); this._state = 'running'; this._step(); }
      cancel() { if (this._state === 'idle') return; anims.delete(this); this._state = 'idle'; this._restore(); const e = new Event('cancel'); this._frej(new DOMException('The user aborted a request.', 'AbortError')); this._mk(); if (this.oncancel) this.oncancel(e); this.dispatchEvent(e); }
      reverse() { const ct = this.currentTime; this._rate = -this._rate; if (this._state === 'finished' || this._state === 'idle') { this._state = 'idle'; this._hold = this._rate < 0 ? this._total() : 0; } else if (ct != null) this.currentTime = ct; this.play(); }
      persist() { this.replaceState = 'persisted'; }
      commitStyles() { if (!this.effect || !this.effect.target) return; const keep = this._saved; this._saved = null; this._apply(this.currentTime || 0); this._saved = null; void keep; }
    }
    class DocumentTimeline { constructor(o) { this._o = (o && o.originTime) || 0; } get currentTime() { return performance.now() - this._o; } }
    for (const [n, C] of [['AnimationEffect', AnimationEffect], ['KeyframeEffect', KeyframeEffect], ['Animation', Animation], ['DocumentTimeline', DocumentTimeline], ['AnimationTimeline', DocumentTimeline]]) { try { Object.defineProperty(G, n, { value: C, writable: true, configurable: true }); } catch (e) {} }
    if (G.document) { try { Object.defineProperty(document, 'timeline', { value: new DocumentTimeline(), configurable: true }); } catch (e) {} }
    const P0 = G.Element && G.Element.prototype;
    if (P0) {
      Object.defineProperty(P0, 'animate', { value: function (kf, opt) { const a = new Animation(new KeyframeEffect(this, kf, opt)); if (opt && typeof opt === 'object' && opt.id) a.id = String(opt.id); a.play(); return a; }, writable: true, configurable: true });
      Object.defineProperty(P0, 'getAnimations', { value: function (o) { const out = []; for (const a of anims) { const t = a.effect && a.effect.target; if (t === this || (o && o.subtree && t && this.contains(t))) out.push(a); } return out; }, writable: true, configurable: true });
    }
    if (G.document) Object.defineProperty(document, 'getAnimations', { value: () => [...anims], writable: true, configurable: true });
  }
  {
    class TrustedHTML { constructor(v) { Object.defineProperty(this, '_v', { value: String(v) }); } toString() { return this._v; } toJSON() { return this._v; } }
    class TrustedScript extends TrustedHTML {}
    class TrustedScriptURL extends TrustedHTML {}
    const mk = (C) => (v) => new C(v);
    class TrustedTypePolicy { constructor(name, rules) { this.name = name; this._r = rules || {}; } createHTML(s, ...a) { if (!this._r.createHTML) throw new TypeError("Policy " + this.name + "'s TrustedTypePolicyOptions did not specify a 'createHTML' member."); return mk(TrustedHTML)(this._r.createHTML(String(s), ...a)); } createScript(s, ...a) { if (!this._r.createScript) throw new TypeError("Policy " + this.name + "'s TrustedTypePolicyOptions did not specify a 'createScript' member."); return mk(TrustedScript)(this._r.createScript(String(s), ...a)); } createScriptURL(s, ...a) { if (!this._r.createScriptURL) throw new TypeError("Policy " + this.name + "'s TrustedTypePolicyOptions did not specify a 'createScriptURL' member."); return mk(TrustedScriptURL)(this._r.createScriptURL(String(s), ...a)); } }
    const pols = new Map(); let defp = null;
    class TrustedTypePolicyFactory { createPolicy(name, rules) { const p = new TrustedTypePolicy(String(name), rules); pols.set(p.name, p); if (p.name === 'default') defp = p; return p; } isHTML(v) { return v instanceof TrustedHTML && !(v instanceof TrustedScript) && !(v instanceof TrustedScriptURL); } isScript(v) { return v instanceof TrustedScript; } isScriptURL(v) { return v instanceof TrustedScriptURL; } get emptyHTML() { return new TrustedHTML(''); } get emptyScript() { return new TrustedScript(''); } get defaultPolicy() { return defp; } getAttributeType(tag, attr) { attr = String(attr).toLowerCase(); if (/^on/.test(attr)) return 'TrustedScript'; if ((attr === 'src' && /^(script|iframe|embed)$/i.test(tag)) || (attr === 'data' && /^object$/i.test(tag))) return 'TrustedScriptURL'; if (attr === 'srcdoc' && /^iframe$/i.test(tag)) return 'TrustedHTML'; return null; } getPropertyType(tag, prop) { if (prop === 'innerHTML' || prop === 'outerHTML') return 'TrustedHTML'; if (prop === 'src' && /^script$/i.test(tag)) return 'TrustedScriptURL'; if (prop === 'text' || prop === 'textContent' || prop === 'innerText') return /^script$/i.test(tag) ? 'TrustedScript' : null; return null; } }
    for (const [n, C] of [['TrustedHTML', TrustedHTML], ['TrustedScript', TrustedScript], ['TrustedScriptURL', TrustedScriptURL], ['TrustedTypePolicy', TrustedTypePolicy], ['TrustedTypePolicyFactory', TrustedTypePolicyFactory]]) defR(n, C);
    def0('trustedTypes', new TrustedTypePolicyFactory());
  }
  {
    class CustomStateSet extends Set {}
    class ValidityState0 { constructor() { this._f = {}; this._m = ''; } get valid() { return !Object.keys(this._f).some((k) => this._f[k]); } }
    for (const k of ['valueMissing', 'typeMismatch', 'patternMismatch', 'tooLong', 'tooShort', 'rangeUnderflow', 'rangeOverflow', 'stepMismatch', 'badInput', 'customError']) Object.defineProperty(ValidityState0.prototype, k, { get() { return !!this._f[k]; } });
    class ElementInternals {
      constructor(el) { Object.defineProperty(this, '_el', { value: el }); this._val = null; this._state = null; this._v = new ValidityState0(); this.states = new CustomStateSet(); this.shadowRoot = el.shadowRoot && el.shadowRoot.__mode !== 'closed' ? el.shadowRoot : (el.shadowRoot || null); }
      setFormValue(v, st) { this._val = v; this._state = st === undefined ? v : st; }
      get form() { return this._el.closest ? this._el.closest('form') : null; }
      get labels() { const id = this._el.id; return id ? [...document.querySelectorAll('label[for="' + id + '"]')] : []; }
      get willValidate() { return true; }
      get validity() { return this._v; }
      get validationMessage() { return this._v._m; }
      setValidity(f, msg) { this._v._f = Object.assign({}, f || {}); this._v._m = msg ? String(msg) : ''; }
      checkValidity() { const ok = this._v.valid; if (!ok) this._el.dispatchEvent(new Event('invalid', { cancelable: true })); return ok; }
      reportValidity() { return this.checkValidity(); }
    }
    for (const k of ['role', 'ariaLabel', 'ariaDescription', 'ariaChecked', 'ariaDisabled', 'ariaExpanded', 'ariaHidden', 'ariaSelected', 'ariaPressed', 'ariaValueNow', 'ariaValueMin', 'ariaValueMax', 'ariaValueText', 'ariaLive', 'ariaBusy', 'ariaHasPopup', 'ariaInvalid', 'ariaRequired', 'ariaReadOnly', 'ariaMultiLine', 'ariaPlaceholder', 'ariaOrientation', 'ariaLevel', 'ariaModal', 'ariaAtomic', 'ariaAutoComplete', 'ariaCurrent', 'ariaPosInSet', 'ariaSetSize', 'ariaSort', 'ariaRoleDescription']) Object.defineProperty(ElementInternals.prototype, k, { get() { return this['_' + k] === undefined ? null : this['_' + k]; }, set(v) { this['_' + k] = v; }, configurable: true });
    defR('ElementInternals', ElementInternals); defR('CustomStateSet', CustomStateSet);
    const HP = G.HTMLElement && G.HTMLElement.prototype;
    if (HP) Object.defineProperty(HP, 'attachInternals', { value: function () { if (this.__internals) throw new DOMException("Failed to execute 'attachInternals' on 'HTMLElement': ElementInternals for the specified element was already attached.", 'NotSupportedError'); const i = new ElementInternals(this); Object.defineProperty(this, '__internals', { value: i }); return i; }, writable: true, configurable: true });
    if (G.FormData && !G.FormData.__fai) { const FD = G.FormData; const W = function FormData(form, sub) { const fd = new FD(form, sub); if (form && form.querySelectorAll) for (const el of form.querySelectorAll('*')) { const i = el.__internals; const nm = el.getAttribute && el.getAttribute('name'); if (i && nm && i._val != null && el.constructor && el.constructor.formAssociated) { if (i._val instanceof FD) for (const [k, v] of i._val) fd.append(k, v); else fd.append(nm, i._val); } } return fd; }; W.prototype = FD.prototype; W.__fai = true; try { Object.defineProperty(G, 'FormData', { value: W, writable: true, configurable: true }); } catch (e) {} }
  }
  {
    class Credential { constructor(i) { this.id = (i && i.id) || ''; this.type = ''; } static isConditionalMediationAvailable() { return Promise.resolve(false); } }
    class PasswordCredential extends Credential { constructor(i) { super(i); this.type = 'password'; this.password = (i && i.password) || ''; this.name = (i && i.name) || ''; this.iconURL = (i && i.iconURL) || ''; } }
    class FederatedCredential extends Credential { constructor(i) { super(i); this.type = 'federated'; this.provider = (i && i.provider) || ''; this.protocol = (i && i.protocol) || null; } }
    class PublicKeyCredential extends Credential { constructor() { super(); throw new TypeError('Illegal constructor'); } static isUserVerifyingPlatformAuthenticatorAvailable() { return Promise.resolve(false); } static isConditionalMediationAvailable() { return Promise.resolve(false); } static getClientCapabilities() { return Promise.resolve({ conditionalCreate: false, conditionalGet: false, hybridTransport: false, passkeyPlatformAuthenticator: false, userVerifyingPlatformAuthenticator: false, relatedOrigins: false, signalAllAcceptedCredentials: false, signalCurrentUserDetails: false, signalUnknownCredential: false }); } static parseCreationOptionsFromJSON(o) { return o; } static parseRequestOptionsFromJSON(o) { return o; } }
    class IdentityCredential extends Credential { constructor() { super(); this.type = 'identity'; this.token = ''; } }
    class CredentialsContainer { get(o) { if (o && o.signal && o.signal.aborted) return rej('AbortError', 'The operation was aborted.'); if (o && (o.publicKey || o.identity || o.otp)) return rej('NotAllowedError', 'The operation either timed out or was not allowed.'); return Promise.resolve(null); } create(o) { if (o && o.password) return Promise.resolve(new PasswordCredential(o.password)); if (o && o.federated) return Promise.resolve(new FederatedCredential(o.federated)); return rej('NotSupportedError', 'This browser cannot create this kind of credential.'); } store(c) { return Promise.resolve(c); } preventSilentAccess() { return Promise.resolve(); } }
    for (const [n, C] of [['Credential', Credential], ['PasswordCredential', PasswordCredential], ['FederatedCredential', FederatedCredential], ['PublicKeyCredential', PublicKeyCredential], ['IdentityCredential', IdentityCredential], ['CredentialsContainer', CredentialsContainer]]) defR(n, C);
    const nav = G.navigator;
    if (nav && !nav.credentials) { const cc = new CredentialsContainer(); try { Object.defineProperty(nav, 'credentials', { get: () => cc, configurable: true }); } catch (e) {} }
  }
  {
    class MediaStreamTrack extends EventTarget { constructor(kind, label) { super(); this.kind = kind || 'video'; this.label = label || ''; this.id = (crypto.randomUUID ? crypto.randomUUID() : String(Math.random())); this.enabled = true; this.muted = false; this.readyState = 'live'; this.onended = null; this.onmute = null; this.onunmute = null; this.contentHint = ''; } stop() { this.readyState = 'ended'; } clone() { return new MediaStreamTrack(this.kind, this.label); } getSettings() { return {}; } getCapabilities() { return {}; } getConstraints() { return {}; } applyConstraints() { return Promise.resolve(); } }
    if (!G.MediaStream || !G.MediaStream.prototype.getTracks) { class MediaStream extends EventTarget { constructor(t) { super(); this._t = Array.isArray(t) ? t.slice() : (t && t.getTracks ? t.getTracks() : []); this.id = (crypto.randomUUID ? crypto.randomUUID() : String(Math.random())); this.active = this._t.length > 0; } getTracks() { return this._t.slice(); } getAudioTracks() { return this._t.filter((x) => x.kind === 'audio'); } getVideoTracks() { return this._t.filter((x) => x.kind === 'video'); } addTrack(x) { this._t.push(x); } removeTrack(x) { this._t = this._t.filter((y) => y !== x); } getTrackById(id) { return this._t.find((x) => x.id === id) || null; } clone() { return new MediaStream(this._t.map((x) => x.clone())); } } defR('MediaStream', MediaStream); }
    defR('MediaStreamTrack', MediaStreamTrack);
    class MediaDeviceInfo { constructor(k) { this.deviceId = ''; this.groupId = ''; this.kind = k; this.label = ''; } toJSON() { return { deviceId: this.deviceId, groupId: this.groupId, kind: this.kind, label: this.label }; } }
    class MediaDevices extends EventTarget { constructor() { super(); this.ondevicechange = null; } enumerateDevices() { return Promise.resolve([]); } getUserMedia(c) { if (!c || (!c.audio && !c.video)) return Promise.reject(new TypeError("Failed to execute 'getUserMedia' on 'MediaDevices': At least one of audio and video must be requested")); return rej('NotFoundError', 'Requested device not found'); } getDisplayMedia() { return rej('NotAllowedError', 'Permission denied'); } getSupportedConstraints() { return { aspectRatio: true, autoGainControl: true, deviceId: true, echoCancellation: true, facingMode: true, frameRate: true, groupId: true, height: true, noiseSuppression: true, sampleRate: true, width: true }; } }
    defR('MediaDeviceInfo', MediaDeviceInfo); defR('MediaDevices', MediaDevices); def0('InputDeviceInfo', MediaDeviceInfo);
    const nav = G.navigator;
    if (nav && !nav.mediaDevices) { const md = new MediaDevices(); try { Object.defineProperty(nav, 'mediaDevices', { get: () => md, configurable: true }); } catch (e) {} }
    if (G.HTMLCanvasElement && !G.HTMLCanvasElement.prototype.captureStream) Object.defineProperty(G.HTMLCanvasElement.prototype, 'captureStream', { value: function () { return new G.MediaStream([new MediaStreamTrack('video', 'canvas')]); }, writable: true, configurable: true });
  }
  {
    class AudioParam { constructor(v, mn, mx) { this.defaultValue = v; this.value = v; this.minValue = mn === undefined ? -3.4028234663852886e38 : mn; this.maxValue = mx === undefined ? 3.4028234663852886e38 : mx; this.automationRate = 'a-rate'; } setValueAtTime(v) { this.value = v; return this; } linearRampToValueAtTime(v) { this.value = v; return this; } exponentialRampToValueAtTime(v) { this.value = v; return this; } setTargetAtTime(v) { this.value = v; return this; } setValueCurveAtTime(c) { if (c && c.length) this.value = c[c.length - 1]; return this; } cancelScheduledValues() { return this; } cancelAndHoldAtTime() { return this; } }
    class AudioNode extends EventTarget { constructor(ctx, ni, no) { super(); this.context = ctx; this.numberOfInputs = ni === undefined ? 1 : ni; this.numberOfOutputs = no === undefined ? 1 : no; this.channelCount = 2; this.channelCountMode = 'max'; this.channelInterpretation = 'speakers'; this._out = []; } connect(d) { this._out.push(d); return d instanceof AudioNode ? d : undefined; } disconnect() { this._out = []; } }
    class AudioScheduledSourceNode extends AudioNode { constructor(c) { super(c, 0, 1); this.onended = null; } start() { this._st = 1; } stop() { if (this._st === 2) return; this._st = 2; setTimeout(() => { const e = new Event('ended'); if (this.onended) this.onended(e); this.dispatchEvent(e); }, 0); } }
    class OscillatorNode extends AudioScheduledSourceNode { constructor(c, o) { super(c); this.type = (o && o.type) || 'sine'; this.frequency = new AudioParam((o && o.frequency) || 440, -24000, 24000); this.detune = new AudioParam(0); } setPeriodicWave() {} }
    class ConstantSourceNode extends AudioScheduledSourceNode { constructor(c) { super(c); this.offset = new AudioParam(1); } }
    class AudioBuffer { constructor(o) { this.sampleRate = o.sampleRate || 44100; this.length = o.length >>> 0; this.numberOfChannels = o.numberOfChannels || 1; this.duration = this.length / this.sampleRate; this._d = []; for (let i = 0; i < this.numberOfChannels; i++) this._d.push(new Float32Array(this.length)); } getChannelData(i) { if (i >= this.numberOfChannels) throw new DOMException('Index out of range', 'IndexSizeError'); return this._d[i]; } copyFromChannel(dst, ch, off) { dst.set(this._d[ch].subarray(off || 0, (off || 0) + dst.length)); } copyToChannel(src, ch, off) { this._d[ch].set(src, off || 0); } }
    class AudioBufferSourceNode extends AudioScheduledSourceNode { constructor(c, o) { super(c); this.buffer = (o && o.buffer) || null; this.loop = false; this.loopStart = 0; this.loopEnd = 0; this.playbackRate = new AudioParam(1); this.detune = new AudioParam(0); } start(w, off, dur) { super.start(); if (!this.loop) { const d = dur !== undefined ? dur : (this.buffer ? this.buffer.duration - (off || 0) : 0); setTimeout(() => this.stop(), Math.max(0, d * 1000)); } } }
    class GainNode extends AudioNode { constructor(c, o) { super(c); this.gain = new AudioParam(o && o.gain !== undefined ? o.gain : 1); } }
    class DelayNode extends AudioNode { constructor(c) { super(c); this.delayTime = new AudioParam(0, 0, 180); } }
    class BiquadFilterNode extends AudioNode { constructor(c) { super(c); this.type = 'lowpass'; this.frequency = new AudioParam(350); this.detune = new AudioParam(0); this.Q = new AudioParam(1); this.gain = new AudioParam(0); } getFrequencyResponse() {} }
    class DynamicsCompressorNode extends AudioNode { constructor(c) { super(c); this.threshold = new AudioParam(-24); this.knee = new AudioParam(30); this.ratio = new AudioParam(12); this.attack = new AudioParam(0.003); this.release = new AudioParam(0.25); this.reduction = 0; } }
    class StereoPannerNode extends AudioNode { constructor(c) { super(c); this.pan = new AudioParam(0, -1, 1); } }
    class PannerNode extends AudioNode { constructor(c) { super(c); for (const k of ['positionX', 'positionY', 'positionZ', 'orientationX', 'orientationY', 'orientationZ']) this[k] = new AudioParam(k === 'orientationX' ? 1 : 0); this.panningModel = 'equalpower'; this.distanceModel = 'inverse'; } setPosition() {} setOrientation() {} }
    class WaveShaperNode extends AudioNode { constructor(c) { super(c); this.curve = null; this.oversample = 'none'; } }
    class ConvolverNode extends AudioNode { constructor(c) { super(c); this.buffer = null; this.normalize = true; } }
    class ChannelSplitterNode extends AudioNode {}
    class ChannelMergerNode extends AudioNode {}
    class AnalyserNode extends AudioNode { constructor(c) { super(c); this.fftSize = 2048; this.minDecibels = -100; this.maxDecibels = -30; this.smoothingTimeConstant = 0.8; } get frequencyBinCount() { return this.fftSize / 2; } getFloatFrequencyData(a) { a.fill(-Infinity); } getByteFrequencyData(a) { a.fill(0); } getFloatTimeDomainData(a) { a.fill(0); } getByteTimeDomainData(a) { a.fill(128); } }
    class MediaElementAudioSourceNode extends AudioNode { constructor(c, o) { super(c, 0, 1); this.mediaElement = (o && o.mediaElement) || null; } }
    class MediaStreamAudioSourceNode extends AudioNode { constructor(c, o) { super(c, 0, 1); this.mediaStream = (o && o.mediaStream) || null; } }
    class MediaStreamAudioDestinationNode extends AudioNode { constructor(c) { super(c, 1, 0); this.stream = new G.MediaStream([new G.MediaStreamTrack('audio', 'audio')]); } }
    class AudioDestinationNode extends AudioNode { constructor(c) { super(c, 1, 0); this.maxChannelCount = 2; } }
    class AudioListener { constructor() { for (const k of ['positionX', 'positionY', 'positionZ', 'forwardX', 'forwardY', 'forwardZ', 'upX', 'upY', 'upZ']) this[k] = new AudioParam(k === 'forwardZ' ? -1 : k === 'upY' ? 1 : 0); } setPosition() {} setOrientation() {} }
    class PeriodicWave { constructor() {} }
    class AudioWorklet { addModule() { return Promise.resolve(); } }
    class BaseAudioContext extends EventTarget {
      constructor(sr) { super(); this.sampleRate = sr || 48000; this.destination = new AudioDestinationNode(this); this.listener = new AudioListener(); this.audioWorklet = new AudioWorklet(); this.onstatechange = null; this._t0 = performance.now(); this._state = 'suspended'; }
      get state() { return this._state; }
      get currentTime() { return this._state === 'running' ? (performance.now() - this._t0) / 1000 : 0; }
      _set(s) { if (this._state === s) return; this._state = s; const e = new Event('statechange'); if (this.onstatechange) this.onstatechange(e); this.dispatchEvent(e); }
      createGain() { return new GainNode(this); } createOscillator() { return new OscillatorNode(this); } createBufferSource() { return new AudioBufferSourceNode(this); } createConstantSource() { return new ConstantSourceNode(this); } createBiquadFilter() { return new BiquadFilterNode(this); } createDelay() { return new DelayNode(this); } createDynamicsCompressor() { return new DynamicsCompressorNode(this); } createStereoPanner() { return new StereoPannerNode(this); } createPanner() { return new PannerNode(this); } createWaveShaper() { return new WaveShaperNode(this); } createConvolver() { return new ConvolverNode(this); } createAnalyser() { return new AnalyserNode(this); } createChannelSplitter(n) { return new ChannelSplitterNode(this, 1, n || 6); } createChannelMerger(n) { return new ChannelMergerNode(this, n || 6, 1); } createPeriodicWave() { return new PeriodicWave(); } createScriptProcessor() { return new AudioNode(this); } createIIRFilter() { return new AudioNode(this); }
      createBuffer(ch, len, sr) { return new AudioBuffer({ numberOfChannels: ch, length: len, sampleRate: sr }); }
      decodeAudioData(buf, ok, bad) { const e = new DOMException('Unable to decode audio data', 'EncodingError'); if (bad) setTimeout(() => bad(e), 0); return Promise.reject(e); }
    }
    class AudioContext extends BaseAudioContext { constructor(o) { super(o && o.sampleRate); this.baseLatency = 0.01; this.outputLatency = 0.02; this.sinkId = ''; setTimeout(() => this._set('running'), 0); } resume() { this._set('running'); return Promise.resolve(); } suspend() { this._set('suspended'); return Promise.resolve(); } close() { this._set('closed'); return Promise.resolve(); } createMediaElementSource(el) { return new MediaElementAudioSourceNode(this, { mediaElement: el }); } createMediaStreamSource(s) { return new MediaStreamAudioSourceNode(this, { mediaStream: s }); } createMediaStreamDestination() { return new MediaStreamAudioDestinationNode(this); } getOutputTimestamp() { return { contextTime: this.currentTime, performanceTime: performance.now() }; } setSinkId() { return Promise.resolve(); } }
    class OfflineAudioContext extends BaseAudioContext { constructor(a, b, c) { const o = typeof a === 'object' ? a : { numberOfChannels: a, length: b, sampleRate: c }; super(o.sampleRate); this.length = o.length; this._ch = o.numberOfChannels || 1; this.oncomplete = null; } startRendering() { const b = new AudioBuffer({ numberOfChannels: this._ch, length: this.length, sampleRate: this.sampleRate }); this._set('closed'); const e = new Event('complete'); e.renderedBuffer = b; setTimeout(() => { if (this.oncomplete) this.oncomplete(e); this.dispatchEvent(e); }, 0); return Promise.resolve(b); } resume() { return Promise.resolve(); } suspend() { return Promise.resolve(); } }
    const list = { AudioParam, AudioNode, AudioScheduledSourceNode, OscillatorNode, ConstantSourceNode, AudioBuffer, AudioBufferSourceNode, GainNode, DelayNode, BiquadFilterNode, DynamicsCompressorNode, StereoPannerNode, PannerNode, WaveShaperNode, ConvolverNode, ChannelSplitterNode, ChannelMergerNode, AnalyserNode, MediaElementAudioSourceNode, MediaStreamAudioSourceNode, MediaStreamAudioDestinationNode, AudioDestinationNode, AudioListener, PeriodicWave, AudioWorklet, BaseAudioContext, AudioContext, OfflineAudioContext };
    for (const n in list) defR(n, list[n]);
    def0('webkitAudioContext', AudioContext);
  }
  {
    class SpeechSynthesisUtterance extends EventTarget { constructor(t) { super(); this.text = t === undefined ? '' : String(t); this.lang = ''; this.voice = null; this.volume = 1; this.rate = 1; this.pitch = 1; for (const k of ['onstart', 'onend', 'onerror', 'onpause', 'onresume', 'onmark', 'onboundary']) this[k] = null; } }
    class SpeechSynthesisErrorEvent extends Event { constructor(t, i) { super(t, i); this.error = (i && i.error) || ''; this.utterance = (i && i.utterance) || null; this.charIndex = 0; this.elapsedTime = 0; this.name = ''; } }
    class SpeechSynthesis extends EventTarget { constructor() { super(); this.onvoiceschanged = null; } get pending() { return false; } get speaking() { return false; } get paused() { return false; } getVoices() { return []; } speak(u) { setTimeout(() => { const e = new SpeechSynthesisErrorEvent('error', { error: 'synthesis-unavailable', utterance: u }); if (u.onerror) u.onerror(e); u.dispatchEvent(e); }, 0); } cancel() {} pause() {} resume() {} }
    defR('SpeechSynthesisUtterance', SpeechSynthesisUtterance); defR('SpeechSynthesisErrorEvent', SpeechSynthesisErrorEvent); defR('SpeechSynthesis', SpeechSynthesis); defR('speechSynthesis', new SpeechSynthesis());
  }
  {
    let ffn = 0;
    class FontFace {
      constructor(family, source, d) { this.family = String(family).replace(/^['"]|['"]$/g, ''); d = d || {}; this.style = d.style || 'normal'; this.weight = d.weight || 'normal'; this.stretch = d.stretch || 'normal'; this.unicodeRange = d.unicodeRange || 'U+0-10FFFF'; this.display = d.display || 'auto'; this.featureSettings = d.featureSettings || 'normal'; this.ascentOverride = 'normal'; this.descentOverride = 'normal'; this.lineGapOverride = 'normal'; this._src = source; this.status = 'unloaded'; this._n = ++ffn; this.loaded = new Promise((r, j) => { this._res = r; this._rej = j; }); this.loaded.catch(() => {}); if (typeof source !== 'string') this.load(); }
      load() {
        if (this.status === 'loaded' || this.status === 'loading') return this.loaded;
        this.status = 'loading';
        let src = this._src;
        const done = (css) => { try { const st = document.createElement('style'); st.setAttribute('data-nvx-fontface', String(this._n)); st.textContent = '@font-face{font-family:"' + this.family.replace(/"/g, '') + '";src:' + css + ';font-style:' + this.style + ';font-weight:' + this.weight + ';font-display:' + this.display + '}'; (document.head || document.documentElement).appendChild(st); this.status = 'loaded'; this._res(this); } catch (e) { this.status = 'error'; this._rej(e); } };
        if (typeof src === 'string') done(src);
        else { try { const u8 = src instanceof ArrayBuffer ? new Uint8Array(src) : new Uint8Array(src.buffer, src.byteOffset, src.byteLength); let b = ''; for (let i = 0; i < u8.length; i += 8192) b += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); done('url(data:font/ttf;base64,' + btoa(b) + ')'); } catch (e) { this.status = 'error'; this._rej(new DOMException('A network error occurred.', 'NetworkError')); } }
        return this.loaded;
      }
    }
    defR('FontFace', FontFace);
    class FontFaceSetLoadEvent extends Event { constructor(t, i) { super(t, i); this.fontfaces = (i && i.fontfaces) || []; } }
    defR('FontFaceSetLoadEvent', FontFaceSetLoadEvent);
    const fs = G.document && document.fonts;
    if (fs) {
      const set = new Set();
      const wrapF = (k, f) => { try { Object.defineProperty(fs, k, { value: f, writable: true, configurable: true }); } catch (e) {} };
      const oldAdd = fs.add, oldDel = fs.delete;
      wrapF('add', function (f) { set.add(f); if (f && f.status === 'unloaded' && f.load) f.load(); if (oldAdd) try { oldAdd.call(fs, f); } catch (e) {} return fs; });
      wrapF('delete', function (f) { const had = set.delete(f); if (f && f._n) { const st = document.querySelector('style[data-nvx-fontface="' + f._n + '"]'); if (st) st.remove(); } if (oldDel) try { oldDel.call(fs, f); } catch (e) {} return had; });
      wrapF('has', function (f) { return set.has(f); });
      wrapF('clear', function () { for (const f of [...set]) fs.delete(f); });
      wrapF('forEach', function (cb, t) { set.forEach((f) => cb.call(t, f, f, fs)); });
      wrapF('values', function () { return set.values(); });
      wrapF('keys', function () { return set.values(); });
      wrapF('entries', function () { return set.entries(); });
      wrapF(Symbol.iterator, function () { return set.values(); });
      try { Object.defineProperty(fs, 'size', { get: () => set.size, configurable: true }); } catch (e) {}
      const fl = {};
      wrapF('addEventListener', function (t, cb) { if (typeof cb === 'function' || (cb && cb.handleEvent)) (fl[t] = fl[t] || []).push(cb); });
      wrapF('removeEventListener', function (t, cb) { const a = fl[t]; if (a) { const i = a.indexOf(cb); if (i >= 0) a.splice(i, 1); } });
      wrapF('dispatchEvent', function (e) { const a = (fl[e.type] || []).slice(); const h = fs['on' + e.type]; if (typeof h === 'function') a.unshift(h); for (const cb of a) { try { typeof cb === 'function' ? cb.call(fs, e) : cb.handleEvent(e); } catch (x) { N.setTimeout(() => { throw x; }, 0); } } return true; });
      for (const k of ['onloading', 'onloadingdone', 'onloadingerror']) if (!(k in fs)) try { fs[k] = null; } catch (e) {}
      const famOf = (spec) => { const m = String(spec).match(/(?:^|\s)(?:\d+(?:\.\d+)?(?:px|pt|em|rem|%)|small|medium|large|x-large|xx-large|smaller|larger)(?:\/\S+)?\s+(.+)$/); return (m ? m[1] : String(spec)).split(',').map((s) => s.trim().replace(/^['"]|['"]$/g, '').toLowerCase()); };
      wrapF('load', function (spec) { const fams = famOf(spec); const hit = [...set].filter((f) => fams.includes(String(f.family).toLowerCase())); if (!hit.length) return Promise.resolve([]); fs.dispatchEvent(new FontFaceSetLoadEvent('loading', { fontfaces: hit })); return Promise.all(hit.map((f) => f.load())).then((r) => { fs.dispatchEvent(new FontFaceSetLoadEvent('loadingdone', { fontfaces: r })); return r; }, (e) => { fs.dispatchEvent(new FontFaceSetLoadEvent('loadingerror', { fontfaces: hit })); throw e; }); });
    }
  }
  {
    if (!G.OffscreenCanvas || !G.OffscreenCanvas.prototype.getContext) {
      class OffscreenCanvas extends EventTarget {
        constructor(w, h) { super(); const c = document.createElement('canvas'); c.width = w >>> 0; c.height = h >>> 0; Object.defineProperty(this, '_c', { value: c }); }
        get width() { return this._c.width; } set width(v) { this._c.width = v; }
        get height() { return this._c.height; } set height(v) { this._c.height = v; }
        getContext(t, o) { const x = this._c.getContext(t, o); if (x) { try { Object.defineProperty(x, 'canvas', { value: this, configurable: true }); } catch (e) {} } return x; }
        convertToBlob(o) { return new Promise((res, j) => { try { this._c.toBlob((b) => b ? res(b) : j(new DOMException('Failed', 'EncodingError')), (o && o.type) || 'image/png', o && o.quality); } catch (e) { j(e); } }); }
        transferToImageBitmap() { return G.createImageBitmap ? this._c : this._c; }
      }
      defR('OffscreenCanvas', OffscreenCanvas);
      def0('OffscreenCanvasRenderingContext2D', G.CanvasRenderingContext2D);
    }
    if (G.createImageBitmap && !G.ImageBitmap) { class ImageBitmap { close() {} } def0('ImageBitmap', ImageBitmap); }
  }
  {
    const perf = G.performance;
    if (perf) {
      class PerformanceEntry { constructor(n, t, s, d) { this.name = n; this.entryType = t; this.startTime = s; this.duration = d; } toJSON() { return { name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration }; } }
      class PerformanceMark extends PerformanceEntry { constructor(n, o) { super(String(n), 'mark', o && o.startTime !== undefined ? +o.startTime : perf.now(), 0); this.detail = o && o.detail !== undefined ? o.detail : null; } }
      class PerformanceMeasure extends PerformanceEntry { constructor(n, s, d, det) { super(n, 'measure', s, d); this.detail = det === undefined ? null : det; } }
      class PerformanceResourceTiming extends PerformanceEntry { constructor(n, s, d) { super(n, 'resource', s, d); this.initiatorType = 'other'; this.nextHopProtocol = 'http/1.1'; this.transferSize = 0; this.encodedBodySize = 0; this.decodedBodySize = 0; this.responseStatus = 200; for (const k of ['workerStart', 'redirectStart', 'redirectEnd', 'fetchStart', 'domainLookupStart', 'domainLookupEnd', 'connectStart', 'connectEnd', 'secureConnectionStart', 'requestStart', 'responseStart', 'responseEnd']) this[k] = 0; this.fetchStart = s; this.responseEnd = s + d; } }
      class PerformanceNavigationTiming extends PerformanceResourceTiming { constructor() { super(location.href, 0, 0); this.entryType = 'navigation'; this.initiatorType = 'navigation'; this.type = 'navigate'; this.redirectCount = 0; this.unloadEventStart = 0; this.unloadEventEnd = 0; this.domInteractive = 0; this.domContentLoadedEventStart = 0; this.domContentLoadedEventEnd = 0; this.domComplete = 0; this.loadEventStart = 0; this.loadEventEnd = 0; this.activationStart = 0; } }
      class PerformancePaintTiming extends PerformanceEntry {}
      class PerformanceObserverEntryList { constructor(l) { this._l = l; } getEntries() { return this._l.slice(); } getEntriesByType(t) { return this._l.filter((e) => e.entryType === t); } getEntriesByName(n, t) { return this._l.filter((e) => e.name === n && (!t || e.entryType === t)); } }
      const buf = [];
      const nav = new PerformanceNavigationTiming();
      const obs = new Set();
      const push = (e) => { buf.push(e); if (buf.length > 1000) buf.shift(); for (const o of obs) if (o._types.has(e.entryType)) { o._q.push(e); if (!o._s) { o._s = true; setTimeout(() => { o._s = false; const q = o._q; o._q = []; if (q.length) try { o._cb(new PerformanceObserverEntryList(q), o); } catch (x) { setTimeout(() => { throw x; }); } }, 0); } } };
      const fire = () => { const t = perf.now(); nav.domInteractive = nav.domContentLoadedEventStart = nav.domContentLoadedEventEnd = t; };
      if (G.document) { document.addEventListener('DOMContentLoaded', fire); G.addEventListener('load', () => { const t = perf.now(); nav.domComplete = nav.loadEventStart = nav.loadEventEnd = t; nav.duration = t; if (!buf.some((e) => e.entryType === 'paint')) { push(new PerformancePaintTiming('first-paint', 'paint', t, 0)); push(new PerformancePaintTiming('first-contentful-paint', 'paint', t, 0)); } }); }
      const wr = (k, f) => { try { Object.defineProperty(perf, k, { value: f, writable: true, configurable: true }); } catch (e) { perf[k] = f; } };
      wr('mark', function (n, o) { const m = new PerformanceMark(n, o); push(m); return m; });
      const at = (v) => { if (v === undefined || v === null) return undefined; if (typeof v === 'number') return v; const m = [...buf].reverse().find((e) => e.name === v && e.entryType === 'mark'); if (m) return m.startTime; if (v in nav && typeof nav[v] === 'number') return nav[v]; throw new DOMException("Failed to execute 'measure' on 'Performance': The mark '" + v + "' does not exist.", 'SyntaxError'); };
      wr('measure', function (n, a, b) { let s, e, det; if (a && typeof a === 'object') { s = at(a.start); e = at(a.end); if (a.duration !== undefined) { if (s === undefined && e !== undefined) s = e - a.duration; else e = (s || 0) + a.duration; } det = a.detail; } else { s = at(a); e = at(b); } if (s === undefined) s = 0; if (e === undefined) e = perf.now(); const m = new PerformanceMeasure(String(n), s, e - s, det); push(m); return m; });
      wr('getEntries', function () { return [nav].concat(buf); });
      wr('getEntriesByType', function (t) { return t === 'navigation' ? [nav] : buf.filter((e) => e.entryType === t); });
      wr('getEntriesByName', function (n, t) { return [nav].concat(buf).filter((e) => e.name === n && (!t || e.entryType === t)); });
      wr('clearMarks', function (n) { for (let i = buf.length - 1; i >= 0; i--) if (buf[i].entryType === 'mark' && (n === undefined || buf[i].name === n)) buf.splice(i, 1); });
      wr('clearMeasures', function (n) { for (let i = buf.length - 1; i >= 0; i--) if (buf[i].entryType === 'measure' && (n === undefined || buf[i].name === n)) buf.splice(i, 1); });
      wr('toJSON', function () { return { timeOrigin: perf.timeOrigin, timing: perf.timing, navigation: perf.navigation }; });
      if (!perf.eventCounts) wr('eventCounts', new Map());
      const PO = class PerformanceObserver { constructor(cb) { if (typeof cb !== 'function') throw new TypeError("Failed to construct 'PerformanceObserver': The callback provided as parameter 1 is not a function."); this._cb = cb; this._types = new Set(); this._q = []; this._s = false; } observe(o) { o = o || {}; const ts = o.entryTypes || (o.type ? [o.type] : []); for (const t of ts) this._types.add(t); obs.add(this); if (o.buffered && o.type) { const old = (o.type === 'navigation' ? [nav] : buf.filter((e) => e.entryType === o.type)); if (old.length) { this._q.push(...old); if (!this._s) { this._s = true; setTimeout(() => { this._s = false; const q = this._q; this._q = []; if (q.length) this._cb(new PerformanceObserverEntryList(q), this); }, 0); } } } } disconnect() { obs.delete(this); this._q = []; } takeRecords() { const q = this._q; this._q = []; return q; } static get supportedEntryTypes() { return ['mark', 'measure', 'navigation', 'paint', 'resource']; } };
      try { Object.defineProperty(G, 'PerformanceObserver', { value: PO, writable: true, configurable: true }); } catch (e) {}
      for (const [n, C] of [['PerformanceEntry', PerformanceEntry], ['PerformanceMark', PerformanceMark], ['PerformanceMeasure', PerformanceMeasure], ['PerformanceResourceTiming', PerformanceResourceTiming], ['PerformanceNavigationTiming', PerformanceNavigationTiming], ['PerformancePaintTiming', PerformancePaintTiming], ['PerformanceObserverEntryList', PerformanceObserverEntryList]]) defR(n, C);
      if (!G.Performance || !(perf instanceof G.Performance)) { const Perf = function Performance() { throw new TypeError('Illegal constructor'); }; Perf.prototype = Object.getPrototypeOf(perf) === Object.prototype ? Object.create(EventTarget.prototype) : Object.getPrototypeOf(perf); try { Object.setPrototypeOf(perf, Perf.prototype); } catch (e) {} Object.defineProperty(Perf, Symbol.hasInstance, { value: (o) => o === perf }); defR('Performance', Perf); }
    }
  }
  {
    {
      const gp = Object.getPrototypeOf(G);
      const NP = Object.create(gp);
      try { Object.setPrototypeOf(G, NP); } catch (e) {}
      const seen = new Set();
      let lastGen = -1;
      G.__ids = function () {
        if (!G.document || Object.getPrototypeOf(G) !== NP) return;
        const g = N.domGen ? N.domGen() : 0; if (g === lastGen) return; lastGen = g;
        const els = document.querySelectorAll('[id],[name]');
        for (let i = 0; i < els.length; i++) {
          const el = els[i];
          for (const nm of [el.id, (el.tagName === 'IFRAME' || el.tagName === 'FORM' || el.tagName === 'IMG' || el.tagName === 'EMBED' || el.tagName === 'OBJECT') ? el.getAttribute('name') : '']) {
            if (!nm || seen.has(nm) || nm in G) continue;
            seen.add(nm);
            const key = nm;
            Object.defineProperty(NP, key, { get() { const hit = document.getElementById(key) || document.querySelector('[name="' + key.replace(/"/g, '\\"') + '"]'); return hit || undefined; }, set(v) { Object.defineProperty(G, key, { value: v, writable: true, configurable: true, enumerable: true }); }, configurable: true, enumerable: false });
          }
        }
      };
      if (G.document) { document.addEventListener('DOMContentLoaded', () => G.__ids()); G.addEventListener('load', () => G.__ids()); }
    }
    if (!G.Window || !(G instanceof G.Window)) { const W = function Window() { throw new TypeError('Illegal constructor'); }; Object.defineProperty(W, Symbol.hasInstance, { value: (o) => o === G || !!(o && o.window === o && o.document) }); defR('Window', W); }
    const bar = () => ({ visible: true });
    for (const n of ['locationbar', 'menubar', 'personalbar', 'scrollbars', 'statusbar', 'toolbar']) def0(n, bar());
    def0('BarProp', class BarProp {});
    def0('external', { AddSearchProvider() {}, IsSearchProviderInstalled() { return 0; } });
    def0('styleMedia', { type: 'screen', matchMedium: (q) => (G.matchMedia ? matchMedia(q).matches : false) });
    if (!('offscreenBuffering' in G)) def0('offscreenBuffering', false);
    if (!('originAgentCluster' in G)) def0('originAgentCluster', false);
    if (!('credentialless' in G)) def0('credentialless', false);
    if (!('event' in G)) { try { Object.defineProperty(G, 'event', { value: undefined, writable: true, configurable: true }); } catch (e) {} }
    for (const n of ['onafterprint', 'onbeforeprint', 'onbeforetoggle', 'oncontextlost', 'oncontextrestored', 'oncuechange', 'ondevicemotion', 'ondeviceorientation', 'ondeviceorientationabsolute', 'onemptied', 'ongotpointercapture', 'onlanguagechange', 'onlostpointercapture', 'onmessageerror', 'onmousewheel', 'onpagereveal', 'onpageswap', 'onpointerrawupdate', 'onrejectionhandled', 'onscrollsnapchange', 'onscrollsnapchanging', 'onsecuritypolicyviolation', 'onwebkitanimationend', 'onwebkitanimationiteration', 'onwebkitanimationstart', 'onwebkittransitionend', 'onappinstalled', 'onbeforeinstallprompt', 'onbeforematch', 'oncommand', 'oncontentvisibilityautostatechange']) if (!(n in G)) { try { Object.defineProperty(G, n, { value: null, writable: true, configurable: true }); } catch (e) {} }
    def0('releaseEvents', function () {}); def0('captureEvents', function () {});
    def0('moveBy', function () {}); def0('resizeBy', function () {});
    def0('webkitRequestAnimationFrame', G.requestAnimationFrame); def0('webkitCancelAnimationFrame', G.cancelAnimationFrame); def0('webkitURL', G.URL);
    const nav = G.navigator;
    if (nav) {
      const dn = (k, v) => { if (!(k in nav)) { try { Object.defineProperty(nav, k, { value: v, writable: true, configurable: true }); } catch (e) {} } };
      dn('getGamepads', function () { return [null, null, null, null]; });
      dn('wakeLock', { request() { return rej('NotAllowedError', 'Wake Lock permission request denied'); } });
      dn('keyboard', { getLayoutMap() { return Promise.resolve(new Map()); }, lock() { return Promise.resolve(); }, unlock() {} });
      dn('virtualKeyboard', Object.assign(new EventTarget(), { overlaysContent: false, boundingRect: { x: 0, y: 0, width: 0, height: 0, top: 0, left: 0, right: 0, bottom: 0 }, show() {}, hide() {}, ongeometrychange: null }));
      { const sb = function (n) { const v = n === undefined ? 1 : Number(n); if (!(v >= 0) || !isFinite(v)) return Promise.reject(new TypeError("Failed to execute 'setAppBadge' on 'Navigator': Value is outside the 'unsigned long long' value range.")); try { N.badge(Math.floor(v)); } catch (e) {} return Promise.resolve(); }; const cb2 = function () { try { N.badge(0); } catch (e) {} return Promise.resolve(); }; try { Object.defineProperty(navigator, 'setAppBadge', { value: sb, configurable: true, writable: true }); Object.defineProperty(navigator, 'clearAppBadge', { value: cb2, configurable: true, writable: true }); } catch (e) { dn('setAppBadge', sb); dn('clearAppBadge', cb2); } }
      dn('registerProtocolHandler', function () {}); dn('unregisterProtocolHandler', function () {});
    }
    class Touch { constructor(i) { i = i || {}; this.identifier = i.identifier || 0; this.target = i.target || null; for (const k of ['clientX', 'clientY', 'screenX', 'screenY', 'pageX', 'pageY', 'radiusX', 'radiusY', 'rotationAngle', 'force', 'altitudeAngle', 'azimuthAngle']) this[k] = i[k] || 0; this.touchType = i.touchType || 'direct'; } }
    class TouchList { constructor(a) { this._a = a || []; for (let i = 0; i < this._a.length; i++) this[i] = this._a[i]; } get length() { return this._a.length; } item(i) { return this._a[i] || null; } [Symbol.iterator]() { return this._a[Symbol.iterator](); } }
    defR('Touch', Touch); defR('TouchList', TouchList);
    if (G.HTMLElement) { for (const n of ['HTMLTableColElement', 'HTMLDataListElement', 'HTMLTrackElement', 'HTMLParamElement', 'HTMLFontElement', 'HTMLFrameSetElement', 'HTMLFrameElement', 'HTMLMarqueeElement', 'HTMLDirectoryElement', 'HTMLFencedFrameElement', 'HTMLSelectedContentElement']) { if (G[n]) continue; const C = function () { throw new TypeError('Illegal constructor'); }; C.prototype = Object.create(G.HTMLElement.prototype, { constructor: { value: C, writable: true, configurable: true } }); Object.defineProperty(C, 'name', { value: n }); def0(n, C); } }
    if (G.SVGElement) { for (const n of ['SVGAnimateMotionElement', 'SVGAnimateTransformElement', 'SVGComponentTransferFunctionElement', 'SVGFEBlendElement', 'SVGFEColorMatrixElement', 'SVGFEComponentTransferElement', 'SVGFECompositeElement', 'SVGFEConvolveMatrixElement', 'SVGFEDiffuseLightingElement', 'SVGFEDisplacementMapElement', 'SVGFEDistantLightElement', 'SVGFEDropShadowElement', 'SVGFEFloodElement', 'SVGFEFuncAElement', 'SVGFEFuncBElement', 'SVGFEFuncGElement', 'SVGFEFuncRElement', 'SVGFEGaussianBlurElement', 'SVGFEImageElement', 'SVGFEMergeElement', 'SVGFEMergeNodeElement', 'SVGFEMorphologyElement', 'SVGFEOffsetElement', 'SVGFEPointLightElement', 'SVGFESpecularLightingElement', 'SVGFESpotLightElement', 'SVGFETileElement', 'SVGFETurbulenceElement', 'SVGMPathElement']) def0(n, G.SVGElement); }
    if (!G.ClipboardItem || !G.ClipboardItem.prototype.getType) { class ClipboardItem { constructor(items, o) { this._i = Object.assign({}, items); this.presentationStyle = (o && o.presentationStyle) || 'unspecified'; } get types() { return Object.keys(this._i); } getType(t) { if (!(t in this._i)) return rej('NotFoundError', 'The type was not found'); return Promise.resolve(this._i[t]).then((v) => v instanceof Blob ? v : new Blob([String(v)], { type: t })); } static supports(t) { return /^(text\/plain|text\/html|image\/png)$/.test(t); } } defR('ClipboardItem', ClipboardItem);
      const cb = nav && nav.clipboard; if (cb) { def0('Clipboard', class Clipboard extends EventTarget {}); if (!cb.write) cb.write = function (items) { const it = items && items[0]; if (!it) return Promise.resolve(); if (it.types.includes('text/plain')) return it.getType('text/plain').then((b) => b.text()).then((t) => cb.writeText(t)); return rej('NotAllowedError', 'Only text can be copied'); }; if (!cb.read) cb.read = function () { return cb.readText().then((t) => [new ClipboardItem({ 'text/plain': new Blob([t], { type: 'text/plain' }) })]); }; } }
    if (G.document && !(G.cookieStore && G.cookieStore.getAll)) {
      const parse = () => document.cookie.split(/;\s*/).filter(Boolean).map((p) => { const i = p.indexOf('='); return { name: i < 0 ? '' : p.slice(0, i), value: i < 0 ? p : p.slice(i + 1), domain: null, path: '/', expires: null, secure: location.protocol === 'https:', sameSite: 'strict', partitioned: false }; });
      class CookieStore extends EventTarget {
        constructor() { super(); this.onchange = null; }
        get(n) { const name = typeof n === 'object' ? n.name : n; return Promise.resolve(parse().find((c) => c.name === name) || null); }
        getAll(n) { const name = n === undefined ? undefined : typeof n === 'object' ? n.name : n; return Promise.resolve(parse().filter((c) => name === undefined || c.name === name)); }
        set(a, b) { const o = typeof a === 'object' ? a : { name: a, value: b }; let s = o.name + '=' + (o.value == null ? '' : o.value); if (o.expires) s += '; expires=' + new Date(o.expires).toUTCString(); if (o.domain) s += '; domain=' + o.domain; s += '; path=' + (o.path || '/'); if (o.sameSite) s += '; samesite=' + o.sameSite; if (location.protocol === 'https:') s += '; secure'; document.cookie = s; const e = new Event('change'); e.changed = [{ name: o.name, value: String(o.value) }]; e.deleted = []; setTimeout(() => { if (this.onchange) this.onchange(e); this.dispatchEvent(e); }, 0); return Promise.resolve(); }
        delete(a) { const o = typeof a === 'object' ? a : { name: a }; document.cookie = o.name + '=; expires=Thu, 01 Jan 1970 00:00:00 GMT; path=' + (o.path || '/') + (o.domain ? '; domain=' + o.domain : ''); const e = new Event('change'); e.changed = []; e.deleted = [{ name: o.name }]; setTimeout(() => { if (this.onchange) this.onchange(e); this.dispatchEvent(e); }, 0); return Promise.resolve(); }
      }
      defR('CookieStore', CookieStore); def0('cookieStore', new CookieStore());
      class CookieChangeEvent extends Event { constructor(t, i) { super(t, i); this.changed = (i && i.changed) || []; this.deleted = (i && i.deleted) || []; } } defR('CookieChangeEvent', CookieChangeEvent);
    }
    if (G.document) {
      const D = document;
      if (!D.hasStorageAccess) D.hasStorageAccess = () => Promise.resolve(true);
      if (!D.requestStorageAccess) D.requestStorageAccess = () => Promise.resolve();
      if (!D.caretPositionFromPoint) D.caretPositionFromPoint = function (x, y) { const r = N.textAt ? N.textAt(+x, +y) : null; if (!r) return null; return { offsetNode: r[0], offset: byteToUtf16(r[0].data || '', r[1]), getClientRect() { const g = document.createRange(); g.setStart(r[0], byteToUtf16(r[0].data || '', r[1])); return g.getBoundingClientRect(); } }; };
      if (!D.caretRangeFromPoint) D.caretRangeFromPoint = function (x, y) { const p = D.caretPositionFromPoint(x, y); if (!p) return null; const g = document.createRange(); g.setStart(p.offsetNode, p.offset); g.collapse(true); return g; };
    }
    if (!G.URLPattern || !G.URLPattern.prototype.exec) {
      const esc = (s) => s.replace(/[.+?^${}()|[\]\\]/g, '\\$&');
      const comp = (pat) => { if (pat === undefined || pat === '*') return { re: /^.*$/, names: ['0'] }; const names = []; let src = ''; const re = /:([A-Za-z_][A-Za-z0-9_]*)(\([^)]*\))?([?*+])?|\*|\{([^}]*)\}([?*+])?|([^:*{]+)/g; let m; let anon = 0; while ((m = re.exec(pat))) { if (m[1]) { names.push(m[1]); const body = m[2] ? m[2].slice(1, -1) : '[^/]+'; if (m[3] === '?') src += '(' + body + ')?'; else if (m[3] === '*') src += '((?:' + body + ')(?:/(?:' + body + '))*)?'; else if (m[3] === '+') src += '((?:' + body + ')(?:/(?:' + body + '))*)'; else src += '(' + body + ')'; } else if (m[0] === '*') { names.push(String(anon++)); src += '(.*)'; } else if (m[4] !== undefined) { const inner = esc(m[4]); src += '(?:' + inner + ')' + (m[5] || ''); } else src += esc(m[6]); } return { re: new RegExp('^' + src + '$'), names }; };
      const PARTS = ['protocol', 'username', 'password', 'hostname', 'port', 'pathname', 'search', 'hash'];
      class URLPattern {
        constructor(input, base) { let o = {}; if (typeof input === 'string') { try { const u = new URL(input.replace(/\*/g, '__STAR__').replace(/:([A-Za-z_])/g, '__COLON__$1'), base); for (const p of PARTS) { let v = u[p]; if (p === 'protocol') v = v.replace(/:$/, ''); if (p === 'search') v = v.replace(/^\?/, ''); if (p === 'hash') v = v.replace(/^#/, ''); o[p] = v.replace(/__STAR__/g, '*').replace(/__COLON__/g, ':'); } if (!/\?/.test(input)) o.search = '*'; if (!/#/.test(input)) o.hash = '*'; } catch (e) { throw new TypeError("Failed to construct 'URLPattern': Invalid URL pattern"); } } else o = Object.assign({}, input || {}); this._p = {}; for (const p of PARTS) { const v = o[p] === undefined ? '*' : String(o[p]); this[p] = v; this._p[p] = comp(v); } }
        test(i, b) { return this.exec(i, b) !== null; }
        exec(i, b) { let u; try { u = typeof i === 'string' ? new URL(i, b) : null; } catch (e) { return null; } const val = (p) => { if (!u) return i && i[p] !== undefined ? String(i[p]) : ''; let v = u[p]; if (p === 'protocol') v = v.replace(/:$/, ''); if (p === 'search') v = v.replace(/^\?/, ''); if (p === 'hash') v = v.replace(/^#/, ''); return v; }; const res = { inputs: [i] }; for (const p of PARTS) { const s = val(p); const m = this._p[p].re.exec(s); if (!m) return null; const g = {}; this._p[p].names.forEach((n, k) => { g[n] = m[k + 1]; }); res[p] = { input: s, groups: g }; } return res; }
        get hasRegExpGroups() { return false; }
      }
      defR('URLPattern', URLPattern);
    }
    {
      const GLC = { DEPTH_BUFFER_BIT: 0x100, STENCIL_BUFFER_BIT: 0x400, COLOR_BUFFER_BIT: 0x4000, POINTS: 0, LINES: 1, LINE_LOOP: 2, LINE_STRIP: 3, TRIANGLES: 4, TRIANGLE_STRIP: 5, TRIANGLE_FAN: 6, ZERO: 0, ONE: 1, SRC_COLOR: 0x300, ONE_MINUS_SRC_COLOR: 0x301, SRC_ALPHA: 0x302, ONE_MINUS_SRC_ALPHA: 0x303, DST_ALPHA: 0x304, ONE_MINUS_DST_ALPHA: 0x305, DST_COLOR: 0x306, ONE_MINUS_DST_COLOR: 0x307, SRC_ALPHA_SATURATE: 0x308, FUNC_ADD: 0x8006, BLEND_EQUATION: 0x8009, BLEND_EQUATION_RGB: 0x8009, BLEND_EQUATION_ALPHA: 0x883D, FUNC_SUBTRACT: 0x800A, FUNC_REVERSE_SUBTRACT: 0x800B, BLEND_DST_RGB: 0x80C8, BLEND_SRC_RGB: 0x80C9, BLEND_DST_ALPHA: 0x80CA, BLEND_SRC_ALPHA: 0x80CB, CONSTANT_COLOR: 0x8001, ONE_MINUS_CONSTANT_COLOR: 0x8002, CONSTANT_ALPHA: 0x8003, ONE_MINUS_CONSTANT_ALPHA: 0x8004, BLEND_COLOR: 0x8005, ARRAY_BUFFER: 0x8892, ELEMENT_ARRAY_BUFFER: 0x8893, ARRAY_BUFFER_BINDING: 0x8894, ELEMENT_ARRAY_BUFFER_BINDING: 0x8895, STREAM_DRAW: 0x88E0, STATIC_DRAW: 0x88E4, DYNAMIC_DRAW: 0x88E8, BUFFER_SIZE: 0x8764, BUFFER_USAGE: 0x8765, CURRENT_VERTEX_ATTRIB: 0x8626, FRONT: 0x404, BACK: 0x405, FRONT_AND_BACK: 0x408, CULL_FACE: 0xB44, BLEND: 0xBE2, DITHER: 0xBD0, STENCIL_TEST: 0xB90, DEPTH_TEST: 0xB71, SCISSOR_TEST: 0xC11, POLYGON_OFFSET_FILL: 0x8037, SAMPLE_ALPHA_TO_COVERAGE: 0x809E, SAMPLE_COVERAGE: 0x80A0, NO_ERROR: 0, INVALID_ENUM: 0x500, INVALID_VALUE: 0x501, INVALID_OPERATION: 0x502, OUT_OF_MEMORY: 0x505, CW: 0x900, CCW: 0x901, LINE_WIDTH: 0xB21, ALIASED_POINT_SIZE_RANGE: 0x846D, ALIASED_LINE_WIDTH_RANGE: 0x846E, CULL_FACE_MODE: 0xB45, FRONT_FACE: 0xB46, DEPTH_RANGE: 0xB70, DEPTH_WRITEMASK: 0xB72, DEPTH_CLEAR_VALUE: 0xB73, DEPTH_FUNC: 0xB74, STENCIL_CLEAR_VALUE: 0xB91, STENCIL_FUNC: 0xB92, STENCIL_FAIL: 0xB94, STENCIL_PASS_DEPTH_FAIL: 0xB95, STENCIL_PASS_DEPTH_PASS: 0xB96, STENCIL_REF: 0xB97, STENCIL_VALUE_MASK: 0xB93, STENCIL_WRITEMASK: 0xB98, STENCIL_BACK_FUNC: 0x8800, STENCIL_BACK_FAIL: 0x8801, STENCIL_BACK_PASS_DEPTH_FAIL: 0x8802, STENCIL_BACK_PASS_DEPTH_PASS: 0x8803, STENCIL_BACK_REF: 0x8CA3, STENCIL_BACK_VALUE_MASK: 0x8CA4, STENCIL_BACK_WRITEMASK: 0x8CA5, VIEWPORT: 0xBA2, SCISSOR_BOX: 0xC10, COLOR_CLEAR_VALUE: 0xC22, COLOR_WRITEMASK: 0xC23, UNPACK_ALIGNMENT: 0xCF5, PACK_ALIGNMENT: 0xD05, MAX_TEXTURE_SIZE: 0xD33, MAX_VIEWPORT_DIMS: 0xD3A, SUBPIXEL_BITS: 0xD50, RED_BITS: 0xD52, GREEN_BITS: 0xD53, BLUE_BITS: 0xD54, ALPHA_BITS: 0xD55, DEPTH_BITS: 0xD56, STENCIL_BITS: 0xD57, POLYGON_OFFSET_UNITS: 0x2A00, POLYGON_OFFSET_FACTOR: 0x8038, TEXTURE_BINDING_2D: 0x8069, SAMPLE_BUFFERS: 0x80A8, SAMPLES: 0x80A9, SAMPLE_COVERAGE_VALUE: 0x80AA, SAMPLE_COVERAGE_INVERT: 0x80AB, COMPRESSED_TEXTURE_FORMATS: 0x86A3, DONT_CARE: 0x1100, FASTEST: 0x1101, NICEST: 0x1102, GENERATE_MIPMAP_HINT: 0x8192, BYTE: 0x1400, UNSIGNED_BYTE: 0x1401, SHORT: 0x1402, UNSIGNED_SHORT: 0x1403, INT: 0x1404, UNSIGNED_INT: 0x1405, FLOAT: 0x1406, DEPTH_COMPONENT: 0x1902, ALPHA: 0x1906, RGB: 0x1907, RGBA: 0x1908, LUMINANCE: 0x1909, LUMINANCE_ALPHA: 0x190A, UNSIGNED_SHORT_4_4_4_4: 0x8033, UNSIGNED_SHORT_5_5_5_1: 0x8034, UNSIGNED_SHORT_5_6_5: 0x8363, FRAGMENT_SHADER: 0x8B30, VERTEX_SHADER: 0x8B31, MAX_VERTEX_ATTRIBS: 0x8869, MAX_VERTEX_UNIFORM_VECTORS: 0x8DFB, MAX_VARYING_VECTORS: 0x8DFC, MAX_COMBINED_TEXTURE_IMAGE_UNITS: 0x8B4D, MAX_VERTEX_TEXTURE_IMAGE_UNITS: 0x8B4C, MAX_TEXTURE_IMAGE_UNITS: 0x8872, MAX_FRAGMENT_UNIFORM_VECTORS: 0x8DFD, SHADER_TYPE: 0x8B4F, DELETE_STATUS: 0x8B80, LINK_STATUS: 0x8B82, VALIDATE_STATUS: 0x8B83, ATTACHED_SHADERS: 0x8B85, ACTIVE_UNIFORMS: 0x8B86, ACTIVE_ATTRIBUTES: 0x8B89, SHADING_LANGUAGE_VERSION: 0x8B8C, CURRENT_PROGRAM: 0x8B8D, NEVER: 0x200, LESS: 0x201, EQUAL: 0x202, LEQUAL: 0x203, GREATER: 0x204, NOTEQUAL: 0x205, GEQUAL: 0x206, ALWAYS: 0x207, KEEP: 0x1E00, REPLACE: 0x1E01, INCR: 0x1E02, DECR: 0x1E03, INVERT: 0x150A, INCR_WRAP: 0x8507, DECR_WRAP: 0x8508, VENDOR: 0x1F00, RENDERER: 0x1F01, VERSION: 0x1F02, NEAREST: 0x2600, LINEAR: 0x2601, NEAREST_MIPMAP_NEAREST: 0x2700, LINEAR_MIPMAP_NEAREST: 0x2701, NEAREST_MIPMAP_LINEAR: 0x2702, LINEAR_MIPMAP_LINEAR: 0x2703, TEXTURE_MAG_FILTER: 0x2800, TEXTURE_MIN_FILTER: 0x2801, TEXTURE_WRAP_S: 0x2802, TEXTURE_WRAP_T: 0x2803, TEXTURE_2D: 0xDE1, TEXTURE: 0x1702, TEXTURE_CUBE_MAP: 0x8513, TEXTURE_BINDING_CUBE_MAP: 0x8514, TEXTURE_CUBE_MAP_POSITIVE_X: 0x8515, TEXTURE_CUBE_MAP_NEGATIVE_X: 0x8516, TEXTURE_CUBE_MAP_POSITIVE_Y: 0x8517, TEXTURE_CUBE_MAP_NEGATIVE_Y: 0x8518, TEXTURE_CUBE_MAP_POSITIVE_Z: 0x8519, TEXTURE_CUBE_MAP_NEGATIVE_Z: 0x851A, MAX_CUBE_MAP_TEXTURE_SIZE: 0x851C, TEXTURE0: 0x84C0, ACTIVE_TEXTURE: 0x84E0, REPEAT: 0x2901, CLAMP_TO_EDGE: 0x812F, MIRRORED_REPEAT: 0x8370, FLOAT_VEC2: 0x8B50, FLOAT_VEC3: 0x8B51, FLOAT_VEC4: 0x8B52, INT_VEC2: 0x8B53, INT_VEC3: 0x8B54, INT_VEC4: 0x8B55, BOOL: 0x8B56, BOOL_VEC2: 0x8B57, BOOL_VEC3: 0x8B58, BOOL_VEC4: 0x8B59, FLOAT_MAT2: 0x8B5A, FLOAT_MAT3: 0x8B5B, FLOAT_MAT4: 0x8B5C, SAMPLER_2D: 0x8B5E, SAMPLER_CUBE: 0x8B60, VERTEX_ATTRIB_ARRAY_ENABLED: 0x8622, VERTEX_ATTRIB_ARRAY_SIZE: 0x8623, VERTEX_ATTRIB_ARRAY_STRIDE: 0x8624, VERTEX_ATTRIB_ARRAY_TYPE: 0x8625, VERTEX_ATTRIB_ARRAY_NORMALIZED: 0x886A, VERTEX_ATTRIB_ARRAY_POINTER: 0x8645, VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: 0x889F, IMPLEMENTATION_COLOR_READ_TYPE: 0x8B9A, IMPLEMENTATION_COLOR_READ_FORMAT: 0x8B9B, COMPILE_STATUS: 0x8B81, LOW_FLOAT: 0x8DF0, MEDIUM_FLOAT: 0x8DF1, HIGH_FLOAT: 0x8DF2, LOW_INT: 0x8DF3, MEDIUM_INT: 0x8DF4, HIGH_INT: 0x8DF5, FRAMEBUFFER: 0x8D40, RENDERBUFFER: 0x8D41, RGBA4: 0x8056, RGB5_A1: 0x8057, RGB565: 0x8D62, DEPTH_COMPONENT16: 0x81A5, STENCIL_INDEX8: 0x8D48, DEPTH_STENCIL: 0x84F9, RENDERBUFFER_WIDTH: 0x8D42, RENDERBUFFER_HEIGHT: 0x8D43, RENDERBUFFER_INTERNAL_FORMAT: 0x8D44, FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE: 0x8CD0, FRAMEBUFFER_ATTACHMENT_OBJECT_NAME: 0x8CD1, COLOR_ATTACHMENT0: 0x8CE0, DEPTH_ATTACHMENT: 0x8D00, STENCIL_ATTACHMENT: 0x8D20, DEPTH_STENCIL_ATTACHMENT: 0x821A, NONE: 0, FRAMEBUFFER_COMPLETE: 0x8CD5, FRAMEBUFFER_INCOMPLETE_ATTACHMENT: 0x8CD6, FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: 0x8CD7, FRAMEBUFFER_INCOMPLETE_DIMENSIONS: 0x8CD9, FRAMEBUFFER_UNSUPPORTED: 0x8CDD, FRAMEBUFFER_BINDING: 0x8CA6, RENDERBUFFER_BINDING: 0x8CA7, MAX_RENDERBUFFER_SIZE: 0x84E8, INVALID_FRAMEBUFFER_OPERATION: 0x506, UNPACK_FLIP_Y_WEBGL: 0x9240, UNPACK_PREMULTIPLY_ALPHA_WEBGL: 0x9241, CONTEXT_LOST_WEBGL: 0x9242, UNPACK_COLORSPACE_CONVERSION_WEBGL: 0x9243, BROWSER_DEFAULT_WEBGL: 0x9244 };
      const glObj = (n) => { const C = class { constructor() { this._dead = false; } }; Object.defineProperty(C, 'name', { value: n }); def0(n, C); return C; };
      const WBuffer = glObj('WebGLBuffer'), WShader = glObj('WebGLShader'), WProgram = glObj('WebGLProgram'), WTexture = glObj('WebGLTexture'), WFramebuffer = glObj('WebGLFramebuffer'), WRenderbuffer = glObj('WebGLRenderbuffer'), WLoc = glObj('WebGLUniformLocation');
      class WebGLActiveInfo { constructor(n, t, s) { this.name = n; this.type = t; this.size = s; } }
      class WebGLShaderPrecisionFormat { constructor(a, b, p) { this.rangeMin = a; this.rangeMax = b; this.precision = p; } }
      class WebGLContextEvent extends Event { constructor(t, i) { super(t, i); this.statusMessage = (i && i.statusMessage) || ''; } }
      def0('WebGLActiveInfo', WebGLActiveInfo); def0('WebGLShaderPrecisionFormat', WebGLShaderPrecisionFormat); def0('WebGLContextEvent', WebGLContextEvent);
      const u8v = (d) => d instanceof Uint8Array ? d : d instanceof ArrayBuffer ? new Uint8Array(d) : ArrayBuffer.isView(d) ? new Uint8Array(d.buffer, d.byteOffset, d.byteLength) : null;
      class WebGLRenderingContext {
        constructor(cv, attrs) {
          const a = attrs || {};
          this.canvas = cv;
          this._attrs = { alpha: a.alpha !== false, depth: a.depth !== false, stencil: !!a.stencil, antialias: false, premultipliedAlpha: a.premultipliedAlpha !== false, preserveDrawingBuffer: !!a.preserveDrawingBuffer, powerPreference: a.powerPreference || 'default', failIfMajorPerformanceCaveat: false, desynchronized: false, xrCompatible: false };
          this._err = 0; this._buf = { 0x8892: null }; this._vao = { el: null, attr: [] }; this._vao0 = this._vao; for (let i = 0; i < 16; i++) this._vao.attr.push({ on: 0, buf: null, size: 4, type: 0x1406, norm: 0, stride: 0, off: 0, k: [0, 0, 0, 1] });
          this._prog = null; this._unit = 0; this._tex = []; for (let i = 0; i < 32; i++) this._tex.push({ t2: null, tc: null }); this._fb = null; this._rb = null;
          this._st = { vp: [0, 0, 0, 0], dtest: 0, dfunc: 0x201, dmask: 1, blend: 0, bf: [1, 0, 1, 0], beq: [0x8006, 0x8006], cull: 0, cullface: 0x405, front: 0x901, scis: 0, sb: [0, 0, 0, 0], cm: [1, 1, 1, 1], bc: [0, 0, 0, 0], lw: 1, po: [0, 0], pofill: 0, dr: [0, 1], cc: [0, 0, 0, 0], cd: 1, cs: 0, flip: 0, premul: 0, align: 4, palign: 4, dither: 1 };
          this._resize(); this._st.vp = [0, 0, this._w, this._h]; this._st.sb = [0, 0, this._w, this._h];
          this._sa = new Float32Array(36);
        }
        _resize() { const w = Math.max(1, this.canvas.width | 0), h = Math.max(1, this.canvas.height | 0); if (w === this._w && h === this._h) return; this._w = w; this._h = h; this._col = new Uint8Array(w * h * 4); this._dep = this._attrs.depth ? new Float32Array(w * h).fill(1) : null; }
        get drawingBufferWidth() { this._resize(); return this._w; }
        get drawingBufferHeight() { this._resize(); return this._h; }
        get drawingBufferColorSpace() { return 'srgb'; }
        _e(c) { if (!this._err) this._err = c; }
        getError() { const e = this._err; this._err = 0; return e; }
        getContextAttributes() { return Object.assign({}, this._attrs); }
        isContextLost() { return false; }
        getSupportedExtensions() { return ['OES_element_index_uint', 'OES_vertex_array_object', 'ANGLE_instanced_arrays', 'WEBGL_lose_context', 'OES_texture_float_linear']; }
        getExtension(n) {
          n = String(n); if (!this._ext) this._ext = {}; if (n in this._ext) return this._ext[n]; const r = this._getExt(n); this._ext[n] = r; return r;
        }
        _getExt(n) {
          const g = this;
          if (n === 'OES_element_index_uint' || n === 'OES_texture_float_linear') return {};
          if (n === 'WEBGL_lose_context') return { loseContext() {}, restoreContext() {} };
          if (n === 'OES_vertex_array_object') { const V = glObj('WebGLVertexArrayObjectOES'); return { VERTEX_ARRAY_BINDING_OES: 0x85B5, createVertexArrayOES() { const v = new V(); v._s = { el: null, attr: [] }; for (let i = 0; i < 16; i++) v._s.attr.push({ on: 0, buf: null, size: 4, type: 0x1406, norm: 0, stride: 0, off: 0, k: g._vao.attr[i].k }); return v; }, bindVertexArrayOES(v) { g._vao = v ? v._s : g._vao0; }, deleteVertexArrayOES() {}, isVertexArrayOES(v) { return v instanceof V; } }; }
          if (n === 'ANGLE_instanced_arrays') return { VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE: 0x88FE, vertexAttribDivisorANGLE(i, d) { g._vao.attr[i].div = d; }, drawArraysInstancedANGLE(m, f, c, n2) { g._inst(n2, () => g.drawArrays(m, f, c)); }, drawElementsInstancedANGLE(m, c, t, o, n2) { g._inst(n2, () => g.drawElements(m, c, t, o)); } };
          return null;
        }
        _inst(n, fn) { const A = this._vao.attr; const sv = A.map((a) => ({ on: a.on, k: a.k.slice() })); for (let i = 0; i < n; i++) { for (let l = 0; l < 16; l++) { const a = A[l]; if (a.on && a.div) { const es = a.type === 0x1406 ? 4 : a.type === 0x1402 || a.type === 0x1403 ? 2 : 1; const st = a.stride || es * a.size; const idx = Math.floor(i / a.div); const dv = new DataView(a.buf._d.buffer); const k = [0, 0, 0, 1]; for (let c = 0; c < a.size; c++) { const o = a.off + idx * st + c * es; k[c] = a.type === 0x1406 ? dv.getFloat32(o, true) : a.type === 0x1401 ? (a.norm ? dv.getUint8(o) / 255 : dv.getUint8(o)) : dv.getInt16(o, true); } a.on = 0; a.k = k; } } fn(); for (let l = 0; l < 16; l++) { A[l].on = sv[l].on; } } for (let l = 0; l < 16; l++) A[l].k = sv[l].k; }
        getParameter(p) {
          const s = this._st;
          switch (p) {
          case 0x1F00: return 'WebKit'; case 0x1F01: return 'WebKit WebGL'; case 0x1F02: return 'WebGL 1.0 (OpenGL ES 2.0 NoovexOS software)'; case 0x8B8C: return 'WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 NoovexOS)';
          case 0xD33: return 2048; case 0x851C: return 1024; case 0x84E8: return 2048; case 0xD3A: return new Int32Array([4096, 4096]); case 0x8869: return 16; case 0x8DFB: return 1024; case 0x8DFC: return 30; case 0x8B4D: return 32; case 0x8B4C: return 16; case 0x8872: return 16; case 0x8DFD: return 1024;
          case 0xD52: case 0xD53: case 0xD54: case 0xD55: return 8; case 0xD56: return this._attrs.depth ? 24 : 0; case 0xD57: return 0; case 0xD50: return 4;
          case 0xBA2: return new Int32Array(s.vp); case 0xC10: return new Int32Array(s.sb); case 0xC22: return new Float32Array(s.cc); case 0xC23: return s.cm.map((x) => !!x); case 0xB73: return s.cd;
          case 0xB71: return !!s.dtest; case 0xBE2: return !!s.blend; case 0xB44: return !!s.cull; case 0xC11: return !!s.scis; case 0x8037: return !!s.pofill; case 0xB90: return false; case 0xBD0: return !!s.dither;
          case 0xB74: return s.dfunc; case 0xB72: return !!s.dmask; case 0xB45: return s.cullface; case 0xB46: return s.front; case 0xB70: return new Float32Array(s.dr); case 0xB21: return s.lw; case 0x846D: return new Float32Array([1, 256]); case 0x846E: return new Float32Array([1, 1]);
          case 0x80C9: return s.bf[0]; case 0x80C8: return s.bf[1]; case 0x80CB: return s.bf[2]; case 0x80CA: return s.bf[3]; case 0x8009: return s.beq[0]; case 0x883D: return s.beq[1]; case 0x8005: return new Float32Array(s.bc);
          case 0x8B8D: return this._prog; case 0x8894: return this._buf[0x8892]; case 0x8895: return this._vao.el; case 0x84E0: return 0x84C0 + this._unit; case 0x8069: return this._tex[this._unit].t2; case 0x8514: return this._tex[this._unit].tc; case 0x8CA6: return this._fb; case 0x8CA7: return this._rb;
          case 0xCF5: return s.align; case 0xD05: return s.palign; case 0x9240: return !!s.flip; case 0x9241: return !!s.premul; case 0x9243: return 0x9244; case 0x2A00: return s.po[1]; case 0x8038: return s.po[0]; case 0x80A8: return 0; case 0x80A9: return 0; case 0x86A3: return new Uint32Array(0); case 0x8B9A: return 0x1401; case 0x8B9B: return 0x1908;
          case 0x8192: return 0x1100;
          }
          return null;
        }
        getShaderPrecisionFormat(st, pt) { return pt >= 0x8DF3 ? new WebGLShaderPrecisionFormat(31, 30, 0) : new WebGLShaderPrecisionFormat(127, 127, 23); }
        activeTexture(t) { const u = t - 0x84C0; if (u < 0 || u >= 32) return this._e(0x500); this._unit = u; }
        viewport(x, y, w, h) { this._st.vp = [x | 0, y | 0, w | 0, h | 0]; }
        scissor(x, y, w, h) { this._st.sb = [x | 0, y | 0, w | 0, h | 0]; }
        clearColor(r, g, b, a) { this._st.cc = [+r, +g, +b, +a]; }
        clearDepth(d) { this._st.cd = +d; }
        clearStencil(s) { this._st.cs = s | 0; }
        colorMask(r, g, b, a) { this._st.cm = [r ? 1 : 0, g ? 1 : 0, b ? 1 : 0, a ? 1 : 0]; }
        depthMask(f) { this._st.dmask = f ? 1 : 0; }
        depthFunc(f) { this._st.dfunc = f; }
        depthRange(n, f) { this._st.dr = [+n, +f]; }
        blendFunc(s, d) { this._st.bf = [s, d, s, d]; }
        blendFuncSeparate(a, b, c, d) { this._st.bf = [a, b, c, d]; }
        blendEquation(m) { this._st.beq = [m, m]; }
        blendEquationSeparate(a, b) { this._st.beq = [a, b]; }
        blendColor(r, g, b, a) { this._st.bc = [+r, +g, +b, +a]; }
        cullFace(m) { this._st.cullface = m; }
        frontFace(m) { this._st.front = m; }
        lineWidth(w) { this._st.lw = +w; }
        polygonOffset(f, u) { this._st.po = [+f, +u]; }
        hint() {} sampleCoverage() {} stencilFunc() {} stencilFuncSeparate() {} stencilMask() {} stencilMaskSeparate() {} stencilOp() {} stencilOpSeparate() {} flush() {} finish() {}
        _cap(c, v) { const s = this._st; switch (c) { case 0xB71: s.dtest = v; break; case 0xBE2: s.blend = v; break; case 0xB44: s.cull = v; break; case 0xC11: s.scis = v; break; case 0x8037: s.pofill = v; break; case 0xBD0: s.dither = v; break; case 0xB90: case 0x809E: case 0x80A0: break; default: this._e(0x500); } }
        enable(c) { this._cap(c, 1); } disable(c) { this._cap(c, 0); }
        isEnabled(c) { const v = this.getParameter(c); return !!v; }
        createBuffer() { const b = new WBuffer(); b._d = new Uint8Array(0); b._u = 0x88E4; return b; }
        deleteBuffer(b) { if (b) b._dead = true; }
        isBuffer(b) { return b instanceof WBuffer && !b._dead; }
        bindBuffer(t, b) { if (t === 0x8893) this._vao.el = b; else if (t === 0x8892) this._buf[0x8892] = b; else this._e(0x500); }
        _bb(t) { return t === 0x8893 ? this._vao.el : this._buf[0x8892]; }
        bufferData(t, d, u) { const b = this._bb(t); if (!b) return this._e(0x502); if (typeof d === 'number') b._d = new Uint8Array(d); else { const v = u8v(d); if (!v) return this._e(0x501); b._d = new Uint8Array(v); } b._u = u; }
        bufferSubData(t, o, d) { const b = this._bb(t); const v = u8v(d); if (!b || !v) return this._e(0x502); if (o + v.length > b._d.length) return this._e(0x501); b._d.set(v, o); }
        getBufferParameter(t, p) { const b = this._bb(t); if (!b) return null; return p === 0x8764 ? b._d.length : p === 0x8765 ? b._u : null; }
        createShader(t) { if (t !== 0x8B31 && t !== 0x8B30) { this._e(0x500); return null; } const s = new WShader(); s._t = t; s._src = ''; s._id = -1; s._log = ''; s._ok = false; return s; }
        shaderSource(s, src) { if (s) s._src = String(src); }
        getShaderSource(s) { return s ? s._src : null; }
        compileShader(s) { if (!s) return; if (s._id >= 0) N.glDelShader(s._id); const r = N.glCompile(s._t, s._src); s._id = r[0]; s._ok = r[0] >= 0; s._log = r[1] || ''; }
        getShaderParameter(s, p) { if (!s) return null; return p === 0x8B81 ? s._ok : p === 0x8B4F ? s._t : p === 0x8B80 ? s._dead : null; }
        getShaderInfoLog(s) { return s ? s._log : null; }
        _shfree(s) { if (s._dead && !s._att && s._id >= 0) { N.glDelShader(s._id); s._id = -1; } }
        deleteShader(s) { if (s && !s._dead) { s._dead = true; this._shfree(s); } }
        isShader(s) { return s instanceof WShader && !s._dead; }
        createProgram() { const p = new WProgram(); p._sh = []; p._id = -1; p._ok = false; p._log = ''; p._bind = {}; p._at = []; p._un = []; p._uv = null; return p; }
        attachShader(p, s) { if (p && s && !p._sh.includes(s)) { p._sh.push(s); s._att = (s._att || 0) + 1; } }
        detachShader(p, s) { if (p && s && p._sh.includes(s)) { p._sh = p._sh.filter((x) => x !== s); s._att--; this._shfree(s); } }
        getAttachedShaders(p) { return p ? p._sh.slice() : null; }
        bindAttribLocation(p, i, n) { if (p) p._bind[String(n)] = i; }
        linkProgram(p) {
          if (!p) return;
          const vs = p._sh.find((s) => s._t === 0x8B31), fs = p._sh.find((s) => s._t === 0x8B30);
          if (p._id >= 0) N.glDelProgram(p._id);
          p._id = -1; p._ok = false;
          if (!vs || !fs || !vs._ok || !fs._ok) { p._log = 'ERROR: a vertex and a fragment shader must be attached and compiled'; return; }
          const r = N.glLink(vs._id, fs._id, p._bind);
          p._id = r[0]; p._ok = r[0] >= 0; p._log = r[1] || '';
          if (!p._ok) return;
          p._at = r[2].map((a) => ({ name: a[0], type: a[1], size: a[2], loc: a[3] }));
          p._un = r[3].map((u) => ({ name: u[0], type: u[1], size: u[2], comps: u[3] }));
          p._uv = p._un.map((u) => u.type === 0x8B5E || u.type === 0x8B60 ? new Int32Array(u.size) : null);
        }
        getProgramParameter(p, q) { if (!p) return null; switch (q) { case 0x8B82: return p._ok; case 0x8B83: return true; case 0x8B80: return !!p._dead; case 0x8B85: return p._sh.length; case 0x8B86: return p._un.length; case 0x8B89: return p._at.length; } return null; }
        getProgramInfoLog(p) { return p ? p._log : null; }
        validateProgram() {}
        _pfree(p) { if (p._id >= 0) N.glDelProgram(p._id); p._id = -1; for (const x of p._sh) { x._att--; this._shfree(x); } p._sh = []; }
        deleteProgram(p) { if (!p || p._dead) return; p._dead = true; if (this._prog !== p) this._pfree(p); }
        isProgram(p) { return p instanceof WProgram && !p._dead; }
        useProgram(p) { if (p && (!p._ok || p._dead)) return this._e(0x502); const o = this._prog; this._prog = p; if (o && o !== p && o._dead) this._pfree(o); }
        getAttribLocation(p, n) { if (!p || !p._ok) return -1; const a = p._at.find((x) => x.name === n); return a ? a.loc : -1; }
        getActiveAttrib(p, i) { const a = p && p._at[i]; return a ? new WebGLActiveInfo(a.name, a.type, a.size) : null; }
        getActiveUniform(p, i) { const u = p && p._un[i]; return u ? new WebGLActiveInfo(u.name, u.type, u.size) : null; }
        getUniformLocation(p, n) {
          if (!p || !p._ok) return null;
          n = String(n);
          for (let i = 0; i < p._un.length; i++) {
            const u = p._un[i];
            const base = u.name.endsWith('[0]') ? u.name.slice(0, -3) : null;
            let el = -1;
            if (u.name === n) el = 0;
            else if (base !== null && n === base) el = 0;
            else if (base !== null && n.startsWith(base + '[') && n.endsWith(']')) { const k = +n.slice(base.length + 1, -1); if (Number.isInteger(k) && k >= 0 && k < u.size) el = k; }
            if (el >= 0) { const l = new WLoc(); l._p = p; l._i = i; l._o = el; return l; }
          }
          return null;
        }
        getUniform(p, l) { return null; }
        _u(l, data, isInt) {
          if (!l) return;
          if (!this._prog || l._p !== this._prog) return this._e(0x502);
          const u = l._p._un[l._i];
          if (u.type === 0x8B5E || u.type === 0x8B60) { const sv = l._p._uv[l._i]; for (let k = 0; k < data.length && l._o + k < sv.length; k++) sv[l._o + k] = data[k]; }
          N.glUniform(l._p._id, l._i, data instanceof Float32Array ? data : new Float32Array(data), l._o);
        }
        uniform1f(l, x) { this._u(l, [x]); } uniform2f(l, x, y) { this._u(l, [x, y]); } uniform3f(l, x, y, z) { this._u(l, [x, y, z]); } uniform4f(l, x, y, z, w) { this._u(l, [x, y, z, w]); }
        uniform1i(l, x) { this._u(l, [x]); } uniform2i(l, x, y) { this._u(l, [x, y]); } uniform3i(l, x, y, z) { this._u(l, [x, y, z]); } uniform4i(l, x, y, z, w) { this._u(l, [x, y, z, w]); }
        uniform1fv(l, v) { this._u(l, Float32Array.from(v)); } uniform2fv(l, v) { this._u(l, Float32Array.from(v)); } uniform3fv(l, v) { this._u(l, Float32Array.from(v)); } uniform4fv(l, v) { this._u(l, Float32Array.from(v)); }
        uniform1iv(l, v) { this._u(l, Float32Array.from(v)); } uniform2iv(l, v) { this._u(l, Float32Array.from(v)); } uniform3iv(l, v) { this._u(l, Float32Array.from(v)); } uniform4iv(l, v) { this._u(l, Float32Array.from(v)); }
        uniformMatrix2fv(l, t, v) { this._u(l, Float32Array.from(v)); } uniformMatrix3fv(l, t, v) { this._u(l, Float32Array.from(v)); } uniformMatrix4fv(l, t, v) { this._u(l, Float32Array.from(v)); }
        enableVertexAttribArray(i) { if (i < 16) this._vao.attr[i].on = 1; else this._e(0x501); }
        disableVertexAttribArray(i) { if (i < 16) this._vao.attr[i].on = 0; }
        vertexAttribPointer(i, size, type, norm, stride, off) { const b = this._buf[0x8892]; if (i >= 16) return this._e(0x501); if (!b) return this._e(0x502); Object.assign(this._vao.attr[i], { buf: b, size, type, norm: norm ? 1 : 0, stride, off }); }
        vertexAttrib1f(i, x) { this._vao.attr[i].k = [x, 0, 0, 1]; } vertexAttrib2f(i, x, y) { this._vao.attr[i].k = [x, y, 0, 1]; } vertexAttrib3f(i, x, y, z) { this._vao.attr[i].k = [x, y, z, 1]; } vertexAttrib4f(i, x, y, z, w) { this._vao.attr[i].k = [x, y, z, w]; }
        vertexAttrib1fv(i, v) { this.vertexAttrib1f(i, v[0]); } vertexAttrib2fv(i, v) { this.vertexAttrib2f(i, v[0], v[1]); } vertexAttrib3fv(i, v) { this.vertexAttrib3f(i, v[0], v[1], v[2]); } vertexAttrib4fv(i, v) { this.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
        getVertexAttrib(i, p) { const a = this._vao.attr[i]; switch (p) { case 0x8622: return !!a.on; case 0x8623: return a.size; case 0x8624: return a.stride; case 0x8625: return a.type; case 0x886A: return !!a.norm; case 0x889F: return a.buf; case 0x8626: return new Float32Array(a.k); } return null; }
        getVertexAttribOffset(i) { return this._vao.attr[i].off; }
        createTexture() { const t = new WTexture(); t._w = 0; t._h = 0; t._d = null; t._p = { 0x2801: 0x2702, 0x2800: 0x2601, 0x2802: 0x2901, 0x2803: 0x2901 }; return t; }
        deleteTexture(t) { if (t) t._dead = true; }
        isTexture(t) { return t instanceof WTexture && !t._dead; }
        bindTexture(tg, t) { const u = this._tex[this._unit]; if (tg === 0xDE1) u.t2 = t; else if (tg === 0x8513) u.tc = t; else this._e(0x500); }
        _bt(tg) { const u = this._tex[this._unit]; return tg === 0xDE1 ? u.t2 : tg >= 0x8513 && tg <= 0x851A ? u.tc : null; }
        texParameteri(tg, p, v) { const t = this._bt(tg); if (t) t._p[p] = v; } texParameterf(tg, p, v) { this.texParameteri(tg, p, v); }
        getTexParameter(tg, p) { const t = this._bt(tg); return t ? t._p[p] : null; }
        generateMipmap() {} copyTexImage2D() {} copyTexSubImage2D() {} compressedTexImage2D() { this._e(0x500); } compressedTexSubImage2D() { this._e(0x500); }
        pixelStorei(p, v) { const s = this._st; if (p === 0x9240) s.flip = v ? 1 : 0; else if (p === 0x9241) s.premul = v ? 1 : 0; else if (p === 0xCF5) s.align = v; else if (p === 0xD05) s.palign = v; }
        _toRGBA(w, h, fmt, type, src) {
          const out = new Uint8Array(w * h * 4);
          const ch = fmt === 0x1908 || fmt === 0x8D99 ? 4 : fmt === 0x1907 || fmt === 0x8D98 ? 3 : fmt === 0x190A || fmt === 0x8227 || fmt === 0x8228 ? 2 : 1;
          const red = fmt === 0x1903 || fmt === 0x8D94, rg = fmt === 0x8227 || fmt === 0x8228;
          if ((type === 0x140B || type === 0x8D61) && src) { const h16 = src instanceof Uint16Array ? src : new Uint16Array(src.buffer, src.byteOffset, src.byteLength >> 1); const f = new Float32Array(h16.length); for (let i = 0; i < h16.length; i++) { const v = h16[i], e = (v >> 10) & 31, m = v & 1023; const mag = e === 0 ? m * 5.960464477539063e-8 : e === 31 ? (m ? NaN : Infinity) : (1 + m / 1024) * Math.pow(2, e - 15); f[i] = v & 0x8000 ? -mag : mag; } src = f; type = 0x1406; }
          if (type === 0x1406 && src) { const f = src instanceof Float32Array ? src : new Float32Array(src.buffer, src.byteOffset, src.byteLength >> 2); src = new Uint8Array(f.length); for (let i = 0; i < f.length; i++) src[i] = Math.max(0, Math.min(255, Math.round(f[i] * 255))); type = 0x1401; }
          if (!src) return out;
          if (type === 0x8363 || type === 0x8033 || type === 0x8034) { const s16 = src instanceof Uint16Array ? src : new Uint16Array(src.buffer, src.byteOffset, src.byteLength >> 1); for (let i = 0; i < w * h && i < s16.length; i++) { const v = s16[i]; let r, g, b, a = 255; if (type === 0x8363) { r = (v >> 11) * 255 / 31; g = ((v >> 5) & 63) * 255 / 63; b = (v & 31) * 255 / 31; } else if (type === 0x8033) { r = (v >> 12) * 17; g = ((v >> 8) & 15) * 17; b = ((v >> 4) & 15) * 17; a = (v & 15) * 17; } else { r = (v >> 11) * 255 / 31; g = ((v >> 6) & 31) * 255 / 31; b = ((v >> 1) & 31) * 255 / 31; a = (v & 1) * 255; } out[i * 4] = r; out[i * 4 + 1] = g; out[i * 4 + 2] = b; out[i * 4 + 3] = a; } return out; }
          const s = u8v(src); const al = this._st.align; const rowb = Math.ceil(w * ch / al) * al;
          for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const si = y * rowb + x * ch, di = (y * w + x) * 4; if (si + ch > s.length) continue; if (ch === 4) { out[di] = s[si]; out[di + 1] = s[si + 1]; out[di + 2] = s[si + 2]; out[di + 3] = s[si + 3]; } else if (ch === 3) { out[di] = s[si]; out[di + 1] = s[si + 1]; out[di + 2] = s[si + 2]; out[di + 3] = 255; } else if (ch === 2 && rg) { out[di] = s[si]; out[di + 1] = s[si + 1]; out[di + 3] = 255; } else if (ch === 2) { out[di] = out[di + 1] = out[di + 2] = s[si]; out[di + 3] = s[si + 1]; } else if (red) { out[di] = s[si]; out[di + 3] = 255; } else if (fmt === 0x1906) { out[di + 3] = s[si]; } else { out[di] = out[di + 1] = out[di + 2] = s[si]; out[di + 3] = 255; } }
          return out;
        }
        _srcPixels(src) {
          if (!src) return null;
          if (src instanceof ImageData || (src.data && src.width && src.height && !src.nodeType)) return [src.width, src.height, new Uint8Array(src.data.buffer, src.data.byteOffset, src.data.byteLength)];
          if (src._c && src._c.nodeType) src = src._c;
          if (G.ImageBitmap && src instanceof G.ImageBitmap && src._src) src = src._src;
          if (src.nodeType === 1) { const r = N.glImagePixels(src); if (r) return r; if (src.__gl && src.__gl._col) { const g = src.__gl; const w = g._w, h = g._h, o = new Uint8Array(w * h * 4); for (let y = 0; y < h; y++) o.set(g._col.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4); return [w, h, o]; } }
          return null;
        }
        _post(px, w, h) { const s = this._st; if (s.premul) for (let i = 0; i < px.length; i += 4) { const a = px[i + 3] / 255; px[i] = px[i] * a; px[i + 1] = px[i + 1] * a; px[i + 2] = px[i + 2] * a; } if (s.flip) { const row = w * 4, t = new Uint8Array(row); for (let y = 0; y < (h >> 1); y++) { const a = y * row, b = (h - 1 - y) * row; t.set(px.subarray(a, a + row)); px.copyWithin(a, b, b + row); px.set(t, b); } } return px; }
        texImage2D(tg, lvl, ifmt, a, b, c, fmt, type, data) {
          const t = this._bt(tg); if (!t) return this._e(0x502);
          if (lvl > 0) return;
          let w, h, px;
          if (arguments.length >= 9) { w = a; h = b; px = this._toRGBA(w, h, fmt, type, data); }
          else { const r = this._srcPixels(c); if (!r) { w = 1; h = 1; px = new Uint8Array(4); } else { w = r[0]; h = r[1]; px = new Uint8Array(r[2]); } }
          if (tg !== 0xDE1 && tg !== 0x8515) { if (!t._faces) t._faces = {}; t._faces[tg] = px; return; }
          t._w = w; t._h = h; t._d = this._post(px, w, h);
        }
        texSubImage2D(tg, lvl, x, y, a, b, c, d, e) {
          const t = this._bt(tg); if (!t || !t._d || lvl > 0) return;
          let w, h, px;
          if (arguments.length >= 9) { w = a; h = b; px = this._toRGBA(w, h, c, d, e); } else { const r = this._srcPixels(c); if (!r) return; w = r[0]; h = r[1]; px = new Uint8Array(r[2]); }
          px = this._post(px, w, h);
          for (let yy = 0; yy < h; yy++) { if (y + yy >= t._h) break; const n = Math.min(w, t._w - x); if (n <= 0) break; t._d.set(px.subarray(yy * w * 4, yy * w * 4 + n * 4), ((y + yy) * t._w + x) * 4); }
        }
        createFramebuffer() { const f = new WFramebuffer(); f._c = null; f._d = null; return f; }
        deleteFramebuffer(f) { if (f) f._dead = true; if (this._fb === f) this._fb = null; }
        isFramebuffer(f) { return f instanceof WFramebuffer && !f._dead; }
        bindFramebuffer(t, f) { this._fb = f; }
        framebufferTexture2D(t, at, tg, tex) { const f = this._fb; if (!f) return this._e(0x502); if (at === 0x8CE0) f._c = tex; }
        framebufferRenderbuffer(t, at, rt, rb) { const f = this._fb; if (!f) return this._e(0x502); if (at === 0x8D00 || at === 0x821A) f._d = rb; else if (at === 0x8CE0) f._crb = rb; }
        checkFramebufferStatus() { const f = this._fb; if (!f) return 0x8CD5; return f._c || f._crb ? 0x8CD5 : 0x8CD7; }
        getFramebufferAttachmentParameter() { return null; }
        createRenderbuffer() { const r = new WRenderbuffer(); r._w = 0; r._h = 0; r._f = 0x81A5; return r; }
        deleteRenderbuffer(r) { if (r) r._dead = true; }
        isRenderbuffer(r) { return r instanceof WRenderbuffer && !r._dead; }
        bindRenderbuffer(t, r) { this._rb = r; }
        renderbufferStorage(t, f, w, h) { const r = this._rb; if (!r) return this._e(0x502); r._w = w; r._h = h; r._f = f; r._dd = f === 0x81A5 || f === 0x84F9 ? new Float32Array(w * h).fill(1) : null; r._cd = f === 0x8056 || f === 0x8057 || f === 0x8D62 ? new Uint8Array(w * h * 4) : null; }
        getRenderbufferParameter(t, p) { const r = this._rb; return !r ? null : p === 0x8D42 ? r._w : p === 0x8D43 ? r._h : p === 0x8D44 ? r._f : null; }
        _target() {
          const f = this._fb;
          if (!f) { this._resize(); return [this._w, this._h, this._col, this._dep]; }
          const tx = f._c;
          if (tx && tx._w) { if (!tx._d) tx._d = new Uint8Array(tx._w * tx._h * 4); const dp = f._d && f._d._dd && f._d._w === tx._w && f._d._h === tx._h ? f._d._dd : null; return [tx._w, tx._h, tx._d, dp]; }
          if (f._crb && f._crb._cd) return [f._crb._w, f._crb._h, f._crb._cd, f._d && f._d._dd ? f._d._dd : null];
          return null;
        }
        _dirty() { if (this._fb) return; if (!this._pend) { this._pend = true; Promise.resolve().then(() => this._present()); } }
        _present() { this._pend = false; this._resize(); this.canvas.__gl = this; N.glPresent(this.canvas, this._col, this._w, this._h, this._attrs.premultipliedAlpha ? 1 : 0, this._attrs.alpha ? 1 : 0); }
        clear(m) {
          const tg = this._target(); if (!tg) return this._e(0x506);
          const s = this._st;
          N.glClear(tg, m, s.cc[0], s.cc[1], s.cc[2], s.cc[3], s.cd, s.cm[0] | (s.cm[1] << 1) | (s.cm[2] << 2) | (s.cm[3] << 3), [s.scis, s.sb[0], s.sb[1], s.sb[2], s.sb[3]]);
          this._dirty();
        }
        _draw(mode, first, count, idx) {
          const p = this._prog; if (!p || !p._ok) return this._e(0x502);
          const tg = this._target(); if (!tg) return this._e(0x506);
          const s = this._st, a = this._sa;
          a[0] = s.vp[0]; a[1] = s.vp[1]; a[2] = s.vp[2]; a[3] = s.vp[3]; a[4] = s.dtest && tg[3] ? 1 : 0; a[5] = s.dfunc; a[6] = s.dmask; a[7] = s.blend; a[8] = s.bf[0]; a[9] = s.bf[1]; a[10] = s.bf[2]; a[11] = s.bf[3]; a[12] = s.beq[0]; a[13] = s.beq[1];
          a[14] = s.cull; a[15] = s.cullface; a[16] = s.front; a[17] = s.scis; a[18] = s.sb[0]; a[19] = s.sb[1]; a[20] = s.sb[2]; a[21] = s.sb[3]; a[22] = s.cm[0]; a[23] = s.cm[1]; a[24] = s.cm[2]; a[25] = s.cm[3];
          a[26] = s.bc[0]; a[27] = s.bc[1]; a[28] = s.bc[2]; a[29] = s.bc[3]; a[30] = s.lw; a[31] = s.po[0]; a[32] = s.po[1]; a[33] = s.pofill; a[34] = s.dr[0]; a[35] = s.dr[1];
          const at = [];
          for (let l = 0; l < 16; l++) { const x = this._vao.attr[l]; at.push([x.on && x.buf ? 1 : 0, x.on && x.buf ? x.buf._d.buffer : null, x.size, x.type, x.norm, x.stride, x.off, x.k[0], x.k[1], x.k[2], x.k[3]]); }
          const tx = [];
          for (let i = 0; i < p._un.length; i++) { const sv = p._uv[i]; if (!sv) continue; const isCube = p._un[i].type === 0x8B60; for (let k = 0; k < sv.length; k++) { const unit = sv[k]; if (unit < 0 || unit >= 32 || tx[unit]) continue; const t = isCube ? null : this._tex[unit].t2; if (t && t._d && t._w) tx[unit] = [t._w, t._h, t._d, t._p[0x2801], t._p[0x2800], t._p[0x2802], t._p[0x2803]]; } }
          const r = N.glDraw(p._id, mode, first, count, idx, at, tx, a, tg);
          if (r === -2) return this._e(0x502);
          if (r === -3) return this._e(0x505);
          if (r === -4) { try { N.log('WebGL: a shader ran too long and was stopped', 3); } catch (e) {} }
          this._dirty();
        }
        drawArrays(m, f, c) { if (c < 0 || f < 0) return this._e(0x501); if (!c) return; this._draw(m, f, c, null); }
        drawElements(m, c, t, o) {
          const eb = this._vao.el; if (!eb) return this._e(0x502); if (!c) return;
          const es = t === 0x1401 ? 1 : t === 0x1403 ? 2 : t === 0x1405 ? 4 : 0; if (!es) return this._e(0x500); if (o % es) return this._e(0x502);
          if (o + c * es > eb._d.length) return this._e(0x502);
          const d = eb._d; const ia = es === 1 ? new Uint8Array(d.buffer, d.byteOffset + o, c) : es === 2 ? new Uint16Array(d.buffer.slice(d.byteOffset + o, d.byteOffset + o + c * 2)) : new Uint32Array(d.buffer.slice(d.byteOffset + o, d.byteOffset + o + c * 4));
          this._draw(m, 0, c, ia);
        }
        readPixels(x, y, w, h, fmt, type, out) {
          const tg = this._target(); if (!tg || !out) return this._e(0x502);
          const [W, H, col] = tg; const o = u8v(out); const ch = fmt === 0x1907 ? 3 : fmt === 0x1906 ? 1 : 4;
          if (type === 0x1406) { const f = out instanceof Float32Array ? out : null; if (!f) return this._e(0x502); for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) { const sx = x + xx, sy = y + yy; for (let k = 0; k < 4; k++) f[(yy * w + xx) * 4 + k] = sx >= 0 && sy >= 0 && sx < W && sy < H ? col[(sy * W + sx) * 4 + k] / 255 : 0; } return; }
          const rowb = Math.ceil(w * ch / this._st.palign) * this._st.palign;
          for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) { const sx = x + xx, sy = y + yy; const di = yy * rowb + xx * ch; for (let k = 0; k < ch; k++) o[di + k] = sx >= 0 && sy >= 0 && sx < W && sy < H ? col[(sy * W + sx) * 4 + (ch === 1 ? 3 : k)] : 0; }
        }
      }
      for (const k of Object.keys(GLC)) { WebGLRenderingContext[k] = GLC[k]; Object.defineProperty(WebGLRenderingContext.prototype, k, { value: GLC[k], enumerable: true }); }
      def0('WebGLRenderingContext', WebGLRenderingContext);
      const GL2C = { READ_BUFFER: 0x0C02, UNPACK_ROW_LENGTH: 0x0CF2, UNPACK_SKIP_ROWS: 0x0CF3, UNPACK_SKIP_PIXELS: 0x0CF4, PACK_ROW_LENGTH: 0x0D02, PACK_SKIP_ROWS: 0x0D03, PACK_SKIP_PIXELS: 0x0D04, COLOR: 0x1800, DEPTH: 0x1801, STENCIL: 0x1802, RED: 0x1903, RGB8: 0x8051, RGBA8: 0x8058, RGB10_A2: 0x8059, TEXTURE_BINDING_3D: 0x806A, UNPACK_SKIP_IMAGES: 0x806D, UNPACK_IMAGE_HEIGHT: 0x806E, TEXTURE_3D: 0x806F, TEXTURE_WRAP_R: 0x8072, MAX_3D_TEXTURE_SIZE: 0x8073, UNSIGNED_INT_2_10_10_10_REV: 0x8368, MAX_ELEMENTS_VERTICES: 0x80E8, MAX_ELEMENTS_INDICES: 0x80E9, TEXTURE_MIN_LOD: 0x813A, TEXTURE_MAX_LOD: 0x813B, TEXTURE_BASE_LEVEL: 0x813C, TEXTURE_MAX_LEVEL: 0x813D, MIN: 0x8007, MAX: 0x8008, DEPTH_COMPONENT24: 0x81A6, MAX_TEXTURE_LOD_BIAS: 0x84FD, TEXTURE_COMPARE_MODE: 0x884C, TEXTURE_COMPARE_FUNC: 0x884D, CURRENT_QUERY: 0x8865, QUERY_RESULT: 0x8866, QUERY_RESULT_AVAILABLE: 0x8867, STREAM_READ: 0x88E1, STREAM_COPY: 0x88E2, STATIC_READ: 0x88E5, STATIC_COPY: 0x88E6, DYNAMIC_READ: 0x88E9, DYNAMIC_COPY: 0x88EA, MAX_DRAW_BUFFERS: 0x8824, DRAW_BUFFER0: 0x8825, DRAW_BUFFER1: 0x8826, DRAW_BUFFER2: 0x8827, DRAW_BUFFER3: 0x8828, MAX_FRAGMENT_UNIFORM_COMPONENTS: 0x8B49, MAX_VERTEX_UNIFORM_COMPONENTS: 0x8B4A, SAMPLER_3D: 0x8B5F, SAMPLER_2D_SHADOW: 0x8B62, FRAGMENT_SHADER_DERIVATIVE_HINT: 0x8B8B, PIXEL_PACK_BUFFER: 0x88EB, PIXEL_UNPACK_BUFFER: 0x88EC, PIXEL_PACK_BUFFER_BINDING: 0x88ED, PIXEL_UNPACK_BUFFER_BINDING: 0x88EF, FLOAT_MAT2x3: 0x8B65, FLOAT_MAT2x4: 0x8B66, FLOAT_MAT3x2: 0x8B67, FLOAT_MAT3x4: 0x8B68, FLOAT_MAT4x2: 0x8B69, FLOAT_MAT4x3: 0x8B6A, SRGB: 0x8C40, SRGB8: 0x8C41, SRGB8_ALPHA8: 0x8C43, COMPARE_REF_TO_TEXTURE: 0x884E, RGBA32F: 0x8814, RGB32F: 0x8815, RGBA16F: 0x881A, RGB16F: 0x881B, VERTEX_ATTRIB_ARRAY_INTEGER: 0x88FD, MAX_ARRAY_TEXTURE_LAYERS: 0x88FF, MIN_PROGRAM_TEXEL_OFFSET: 0x8904, MAX_PROGRAM_TEXEL_OFFSET: 0x8905, MAX_VARYING_COMPONENTS: 0x8B4B, TEXTURE_2D_ARRAY: 0x8C1A, TEXTURE_BINDING_2D_ARRAY: 0x8C1D, R11F_G11F_B10F: 0x8C3A, UNSIGNED_INT_10F_11F_11F_REV: 0x8C3B, RGB9_E5: 0x8C3D, UNSIGNED_INT_5_9_9_9_REV: 0x8C3E, TRANSFORM_FEEDBACK_BUFFER_MODE: 0x8C7F, MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS: 0x8C80, TRANSFORM_FEEDBACK_VARYINGS: 0x8C83, TRANSFORM_FEEDBACK_BUFFER_START: 0x8C84, TRANSFORM_FEEDBACK_BUFFER_SIZE: 0x8C85, TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN: 0x8C88, RASTERIZER_DISCARD: 0x8C89, MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS: 0x8C8A, MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS: 0x8C8B, INTERLEAVED_ATTRIBS: 0x8C8C, SEPARATE_ATTRIBS: 0x8C8D, TRANSFORM_FEEDBACK_BUFFER: 0x8C8E, TRANSFORM_FEEDBACK_BUFFER_BINDING: 0x8C8F, RGBA32UI: 0x8D70, RGB32UI: 0x8D71, RGBA16UI: 0x8D76, RGB16UI: 0x8D77, RGBA8UI: 0x8D7C, RGB8UI: 0x8D7D, RGBA32I: 0x8D82, RGB32I: 0x8D83, RGBA16I: 0x8D88, RGB16I: 0x8D89, RGBA8I: 0x8D8E, RGB8I: 0x8D8F, RED_INTEGER: 0x8D94, RGB_INTEGER: 0x8D98, RGBA_INTEGER: 0x8D99, SAMPLER_2D_ARRAY: 0x8DC1, SAMPLER_2D_ARRAY_SHADOW: 0x8DC4, SAMPLER_CUBE_SHADOW: 0x8DC5, UNSIGNED_INT_VEC2: 0x8DC6, UNSIGNED_INT_VEC3: 0x8DC7, UNSIGNED_INT_VEC4: 0x8DC8, INT_SAMPLER_2D: 0x8DCA, INT_SAMPLER_3D: 0x8DCB, INT_SAMPLER_CUBE: 0x8DCC, INT_SAMPLER_2D_ARRAY: 0x8DCF, UNSIGNED_INT_SAMPLER_2D: 0x8DD2, UNSIGNED_INT_SAMPLER_3D: 0x8DD3, UNSIGNED_INT_SAMPLER_CUBE: 0x8DD4, UNSIGNED_INT_SAMPLER_2D_ARRAY: 0x8DD7, DEPTH_COMPONENT32F: 0x8CAC, DEPTH32F_STENCIL8: 0x8CAD, FLOAT_32_UNSIGNED_INT_24_8_REV: 0x8DAD, FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING: 0x8210, FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE: 0x8211, FRAMEBUFFER_ATTACHMENT_RED_SIZE: 0x8212, FRAMEBUFFER_ATTACHMENT_GREEN_SIZE: 0x8213, FRAMEBUFFER_ATTACHMENT_BLUE_SIZE: 0x8214, FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE: 0x8215, FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE: 0x8216, FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE: 0x8217, FRAMEBUFFER_DEFAULT: 0x8218, UNSIGNED_INT_24_8: 0x84FA, DEPTH24_STENCIL8: 0x88F0, UNSIGNED_NORMALIZED: 0x8C17, DRAW_FRAMEBUFFER_BINDING: 0x8CA6, READ_FRAMEBUFFER: 0x8CA8, DRAW_FRAMEBUFFER: 0x8CA9, READ_FRAMEBUFFER_BINDING: 0x8CAA, RENDERBUFFER_SAMPLES: 0x8CAB, FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER: 0x8CD4, MAX_COLOR_ATTACHMENTS: 0x8CDF, COLOR_ATTACHMENT1: 0x8CE1, COLOR_ATTACHMENT2: 0x8CE2, COLOR_ATTACHMENT3: 0x8CE3, FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: 0x8D56, MAX_SAMPLES: 0x8D57, HALF_FLOAT: 0x140B, RG: 0x8227, RG_INTEGER: 0x8228, R8: 0x8229, RG8: 0x822B, R16F: 0x822D, R32F: 0x822E, RG16F: 0x822F, RG32F: 0x8230, R8I: 0x8231, R8UI: 0x8232, R16I: 0x8233, R16UI: 0x8234, R32I: 0x8235, R32UI: 0x8236, RG8I: 0x8237, RG8UI: 0x8238, RG16I: 0x8239, RG16UI: 0x823A, RG32I: 0x823B, RG32UI: 0x823C, VERTEX_ARRAY_BINDING: 0x85B5, R8_SNORM: 0x8F94, RG8_SNORM: 0x8F95, RGB8_SNORM: 0x8F96, RGBA8_SNORM: 0x8F97, SIGNED_NORMALIZED: 0x8F9C, COPY_READ_BUFFER: 0x8F36, COPY_WRITE_BUFFER: 0x8F37, COPY_READ_BUFFER_BINDING: 0x8F36, COPY_WRITE_BUFFER_BINDING: 0x8F37, UNIFORM_BUFFER: 0x8A11, UNIFORM_BUFFER_BINDING: 0x8A28, UNIFORM_BUFFER_START: 0x8A29, UNIFORM_BUFFER_SIZE: 0x8A2A, MAX_VERTEX_UNIFORM_BLOCKS: 0x8A2B, MAX_FRAGMENT_UNIFORM_BLOCKS: 0x8A2D, MAX_COMBINED_UNIFORM_BLOCKS: 0x8A2E, MAX_UNIFORM_BUFFER_BINDINGS: 0x8A2F, MAX_UNIFORM_BLOCK_SIZE: 0x8A30, MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS: 0x8A31, MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS: 0x8A33, UNIFORM_BUFFER_OFFSET_ALIGNMENT: 0x8A34, ACTIVE_UNIFORM_BLOCKS: 0x8A36, UNIFORM_TYPE: 0x8A37, UNIFORM_SIZE: 0x8A38, UNIFORM_BLOCK_INDEX: 0x8A3A, UNIFORM_OFFSET: 0x8A3B, UNIFORM_ARRAY_STRIDE: 0x8A3C, UNIFORM_MATRIX_STRIDE: 0x8A3D, UNIFORM_IS_ROW_MAJOR: 0x8A3E, UNIFORM_BLOCK_BINDING: 0x8A3F, UNIFORM_BLOCK_DATA_SIZE: 0x8A40, UNIFORM_BLOCK_ACTIVE_UNIFORMS: 0x8A42, UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES: 0x8A43, UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER: 0x8A44, UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER: 0x8A46, INVALID_INDEX: 0xFFFFFFFF, MAX_VERTEX_OUTPUT_COMPONENTS: 0x9122, MAX_FRAGMENT_INPUT_COMPONENTS: 0x9125, MAX_SERVER_WAIT_TIMEOUT: 0x9111, OBJECT_TYPE: 0x9112, SYNC_CONDITION: 0x9113, SYNC_STATUS: 0x9114, SYNC_FLAGS: 0x9115, SYNC_FENCE: 0x9116, SYNC_GPU_COMMANDS_COMPLETE: 0x9117, UNSIGNALED: 0x9118, SIGNALED: 0x9119, ALREADY_SIGNALED: 0x911A, TIMEOUT_EXPIRED: 0x911B, CONDITION_SATISFIED: 0x911C, WAIT_FAILED: 0x911D, SYNC_FLUSH_COMMANDS_BIT: 0x00000001, VERTEX_ATTRIB_ARRAY_DIVISOR: 0x88FE, ANY_SAMPLES_PASSED: 0x8C2F, ANY_SAMPLES_PASSED_CONSERVATIVE: 0x8D6A, SAMPLER_BINDING: 0x8919, RGB10_A2UI: 0x906F, INT_2_10_10_10_REV: 0x8D9F, TRANSFORM_FEEDBACK: 0x8E22, TRANSFORM_FEEDBACK_PAUSED: 0x8E23, TRANSFORM_FEEDBACK_ACTIVE: 0x8E24, TRANSFORM_FEEDBACK_BINDING: 0x8E25, TEXTURE_IMMUTABLE_FORMAT: 0x912F, MAX_ELEMENT_INDEX: 0x8D6B, TEXTURE_IMMUTABLE_LEVELS: 0x82DF, TIMEOUT_IGNORED: -1, MAX_CLIENT_WAIT_TIMEOUT_WEBGL: 0x9247 };
      const glsl300 = (src, stage) => {
        const r = { src, locs: {}, blocks: [], is300: false };
        if (!/^\s*#\s*version\s+300\s+es/m.test(src)) return r;
        r.is300 = true;
        let s = src.replace(/^\s*#\s*version\s+300\s+es[^\n]*/m, '');
        s = s.replace(/(layout\s*\([^)]*\)\s*)?uniform\s+(\w+)\s*\{([^}]*)\}\s*(\w+)?\s*(\[[^\]]*\])?\s*;/g, (all, lay, name, body, inst) => {
          const mem = [];
          for (let d of body.split(';')) {
            d = d.replace(/layout\s*\([^)]*\)/g, '').replace(/\b(highp|mediump|lowp|row_major|column_major)\b/g, ' ').trim();
            if (!d) continue;
            const m = /^(\w+)\s+(.+)$/.exec(d);
            if (!m) continue;
            for (const v of m[2].split(',')) { const a = /^\s*(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*$/.exec(v); if (a) mem.push({ type: m[1], name: a[1], n: a[2] ? +a[2] : 0 }); }
          }
          r.blocks.push({ name, inst: inst || '', members: mem });
          const decl = mem.map((x) => x.type + ' ' + x.name + (x.n ? '[' + x.n + ']' : '') + ';').join(' ');
          if (inst) return 'struct ' + name + '_nvxB { ' + decl + ' }; uniform ' + name + '_nvxB ' + name + ';' + (inst !== name ? '\n#define ' + inst + ' ' + name + '\n' : '');
          return mem.map((x) => 'uniform ' + x.type + ' ' + x.name + (x.n ? '[' + x.n + ']' : '') + ';').join(' ');
        });
        let fo = 0;
        s = s.replace(/(^|[;{}\n])(\s*)(layout\s*\(([^)]*)\)\s*)?((?:(?:flat|smooth|centroid|noperspective|invariant|highp|mediump|lowp)\s+)*)(in|out)\s+((?:(?:highp|mediump|lowp|flat|smooth|centroid)\s+)*)(\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;/g, (all, pre, ws, lay, lin, q1, io, q2, ty, nm, arr) => {
          const lm = lin ? /location\s*=\s*(\d+)/.exec(lin) : null;
          const loc = lm ? +lm[1] : -1;
          if (stage === 0) { if (io === 'in') { if (loc >= 0) r.locs[nm] = loc; return pre + ws + 'attribute ' + ty + ' ' + nm + (arr || '') + ';'; } return pre + ws + 'varying ' + ty + ' ' + nm + (arr || '') + ';'; }
          if (io === 'in') return pre + ws + 'varying ' + ty + ' ' + nm + (arr || '') + ';';
          if ((loc <= 0) && !fo && !arr) { fo = 1; return pre + ws + '\n#define ' + nm + ' gl_FragColor\n'; }
          return pre + ws + ty + ' ' + nm + (arr || '') + ';';
        });
        r.src = s;
        return r;
      };
      const std140 = (mem) => {
        const SZ = { float: [4, 4, 1], int: [4, 4, 1], uint: [4, 4, 1], bool: [4, 4, 1], vec2: [8, 8, 2], ivec2: [8, 8, 2], uvec2: [8, 8, 2], bvec2: [8, 8, 2], vec3: [12, 16, 3], ivec3: [12, 16, 3], uvec3: [12, 16, 3], bvec3: [12, 16, 3], vec4: [16, 16, 4], ivec4: [16, 16, 4], uvec4: [16, 16, 4], bvec4: [16, 16, 4] };
        const MAT = { mat2: 2, mat3: 3, mat4: 4 };
        let off = 0;
        const out = [];
        for (const m of mem) {
          const isInt = /^(int|uint|bool|ivec|uvec|bvec)/.test(m.type);
          const cnt = Math.max(1, m.n);
          if (MAT[m.type]) { const c = MAT[m.type]; off = (off + 15) & ~15; out.push({ m, off, cols: c, rows: c, stride: 16 * c, cnt, isInt: false }); off += 16 * c * cnt; continue; }
          const z = SZ[m.type]; if (!z) { out.push({ m, off: -1 }); continue; }
          if (m.n) { off = (off + 15) & ~15; out.push({ m, off, comps: z[2], stride: 16, cnt, isInt }); off += 16 * cnt; }
          else { off = (off + z[1] - 1) & ~(z[1] - 1); out.push({ m, off, comps: z[2], stride: 0, cnt: 1, isInt }); off += z[0]; }
        }
        return { lay: out, size: (off + 15) & ~15 };
      };
      class WebGL2RenderingContext extends WebGLRenderingContext {
        constructor(cv, attrs) { super(cv, attrs); this._v2 = true; this._ubo = []; this._smp = []; this._rfb = null; }
        getParameter(p) {
          switch (p) {
          case 0x1F02: return 'WebGL 2.0 (OpenGL ES 3.0 NoovexOS software)'; case 0x8B8C: return 'WebGL GLSL ES 3.00 (OpenGL ES GLSL ES 3.0 NoovexOS)';
          case 0x8073: return 256; case 0x88FF: return 256; case 0x8824: return 4; case 0x8CDF: return 4; case 0x8D57: return 4; case 0x8A2F: return 24; case 0x8A30: return 65536; case 0x8A34: return 256; case 0x8A2B: return 12; case 0x8A2D: return 12; case 0x8A2E: return 24;
          case 0x8D6B: return 4294967295; case 0x9111: return 0; case 0x9247: return 0; case 0x84FD: return 16; case 0x9122: return 64; case 0x9125: return 60; case 0x8B4B: return 60; case 0x80E8: return 1048576; case 0x80E9: return 1048576; case 0x8905: return 7; case 0x8904: return -8;
          case 0x8B4A: return 4096; case 0x8B49: return 4096; case 0x8A31: return 4096 + 12 * 16384; case 0x8A33: return 4096 + 12 * 16384; case 0x8C8A: return 64; case 0x8C8B: return 4; case 0x8C80: return 4;
          case 0x85B5: return this._vaoObj || null; case 0x8CAA: return this._rfb; case 0x8A28: return this._buf[0x8A11] || null; case 0x8F36: return this._buf[0x8F36] || null; case 0x8F37: return this._buf[0x8F37] || null; case 0x88ED: return this._buf[0x88EB] || null; case 0x88EF: return this._buf[0x88EC] || null;
          case 0x0C02: return 0x8CE0; case 0x8825: return this._fb ? 0x8CE0 : 0x405; case 0x8919: return this._smp[this._unit] || null; case 0x806A: return this._tex[this._unit].t3 || null; case 0x8C1D: return this._tex[this._unit].ta || null; case 0x8C89: return false; case 0x8B8B: return 0x1100;
          }
          return super.getParameter(p);
        }
        getSupportedExtensions() { return ['EXT_color_buffer_float', 'OES_texture_float_linear', 'WEBGL_lose_context']; }
        _getExt(n) { if (n === 'EXT_color_buffer_float' || n === 'OES_texture_float_linear' || n === 'EXT_float_blend') return {}; if (n === 'WEBGL_lose_context') return { loseContext() {}, restoreContext() {} }; return null; }
        bindBuffer(t, b) { if (t === 0x8893) this._vao.el = b; else if (t === 0x8892 || t === 0x8A11 || t === 0x8F36 || t === 0x8F37 || t === 0x88EB || t === 0x88EC || t === 0x8C8E) this._buf[t] = b; else this._e(0x500); }
        _bb(t) { return t === 0x8893 ? this._vao.el : this._buf[t] || null; }
        bufferData(t, d, u, so, ln) { if (ArrayBuffer.isView(d) && so !== undefined) { const v = new Uint8Array(d.buffer, d.byteOffset, d.byteLength); const es = d.BYTES_PER_ELEMENT || 1; const st = so * es; const n = ln ? ln * es : v.length - st; super.bufferData(t, v.subarray(st, st + n), u); } else super.bufferData(t, d, u); const b = this._bb(t); if (b) b._ver = (b._ver || 0) + 1; }
        bufferSubData(t, o, d, so, ln) { if (ArrayBuffer.isView(d) && so !== undefined) { const es = d.BYTES_PER_ELEMENT || 1; const v = new Uint8Array(d.buffer, d.byteOffset, d.byteLength); const st = so * es; const n = ln ? ln * es : v.length - st; super.bufferSubData(t, o, v.subarray(st, st + n)); } else super.bufferSubData(t, o, d); const b = this._bb(t); if (b) b._ver = (b._ver || 0) + 1; }
        getBufferSubData(t, so, dst, dof, ln) { const b = this._bb(t); if (!b) return this._e(0x502); const es = dst.BYTES_PER_ELEMENT || 1; const o = new Uint8Array(dst.buffer, dst.byteOffset + (dof || 0) * es); const n = ln ? ln * es : Math.min(o.length, b._d.length - so); o.set(b._d.subarray(so, so + n)); }
        copyBufferSubData(rt, wt, ro, wo, n) { const r = this._bb(rt), w = this._bb(wt); if (!r || !w) return this._e(0x502); w._d.set(r._d.subarray(ro, ro + n), wo); w._ver = (w._ver || 0) + 1; }
        bindBufferBase(t, i, b) { this._buf[t] = b; if (t === 0x8A11) this._ubo[i] = { b, o: 0, n: 0 }; }
        bindBufferRange(t, i, b, o, n) { this._buf[t] = b; if (t === 0x8A11) this._ubo[i] = { b, o, n }; }
        createVertexArray() { const V = G.WebGLVertexArrayObject; const v = Object.create(V.prototype); v._s = { el: null, attr: [] }; for (let i = 0; i < 16; i++) v._s.attr.push({ on: 0, buf: null, size: 4, type: 0x1406, norm: 0, stride: 0, off: 0, k: this._vao0.attr[i].k }); return v; }
        bindVertexArray(v) { this._vao = v ? v._s : this._vao0; this._vaoObj = v || null; }
        deleteVertexArray(v) { if (v && this._vaoObj === v) this.bindVertexArray(null); if (v) v._dead = true; }
        isVertexArray(v) { return !!(v && v._s && !v._dead); }
        vertexAttribDivisor(i, d) { this._vao.attr[i].div = d; }
        drawArraysInstanced(m, f, c, n) { if (n <= 1 && !this._vao.attr.some((a) => a.on && a.div)) return this.drawArrays(m, f, c); this._inst(n, () => this.drawArrays(m, f, c)); }
        drawElementsInstanced(m, c, t, o, n) { if (n <= 1 && !this._vao.attr.some((a) => a.on && a.div)) return this.drawElements(m, c, t, o); this._inst(n, () => this.drawElements(m, c, t, o)); }
        drawRangeElements(m, s, e, c, t, o) { this.drawElements(m, c, t, o); }
        vertexAttribIPointer(i, size, type, stride, off) { this.vertexAttribPointer(i, size, type, false, stride, off); }
        vertexAttribI4i(i, x, y, z, w) { this.vertexAttrib4f(i, x, y, z, w); } vertexAttribI4ui(i, x, y, z, w) { this.vertexAttrib4f(i, x, y, z, w); } vertexAttribI4iv(i, v) { this.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); } vertexAttribI4uiv(i, v) { this.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); }
        uniform1ui(l, x) { this._u(l, [x]); } uniform2ui(l, x, y) { this._u(l, [x, y]); } uniform3ui(l, x, y, z) { this._u(l, [x, y, z]); } uniform4ui(l, x, y, z, w) { this._u(l, [x, y, z, w]); }
        _uv(l, v, so, ln) { let a = Float32Array.from(v); if (so) a = a.subarray(so, ln ? so + ln : undefined); else if (ln) a = a.subarray(0, ln); this._u(l, a); }
        uniform1fv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform2fv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform3fv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform4fv(l, v, so, ln) { this._uv(l, v, so, ln); }
        uniform1iv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform2iv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform3iv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform4iv(l, v, so, ln) { this._uv(l, v, so, ln); }
        uniform1uiv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform2uiv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform3uiv(l, v, so, ln) { this._uv(l, v, so, ln); } uniform4uiv(l, v, so, ln) { this._uv(l, v, so, ln); }
        uniformMatrix2fv(l, t, v, so, ln) { this._uv(l, v, so, ln); } uniformMatrix3fv(l, t, v, so, ln) { this._uv(l, v, so, ln); } uniformMatrix4fv(l, t, v, so, ln) { this._uv(l, v, so, ln); }
        uniformMatrix2x3fv() {} uniformMatrix3x2fv() {} uniformMatrix2x4fv() {} uniformMatrix4x2fv() {} uniformMatrix3x4fv() {} uniformMatrix4x3fv() {}
        compileShader(s) { if (!s) return; const tr = glsl300(s._src, s._t === 0x8B31 ? 0 : 1); s._locs = tr.locs; s._blocks = tr.blocks; const o = s._src; s._src = tr.src; super.compileShader(s); s._src = o; }
        linkProgram(p) {
          if (!p) return;
          const vs = p._sh.find((s) => s._t === 0x8B31), fs = p._sh.find((s) => s._t === 0x8B30);
          const user = p._bind; p._bind = Object.assign({}, (vs && vs._locs) || {}, user);
          super.linkProgram(p);
          p._bind = user;
          const bl = [];
          for (const s of [vs, fs]) if (s && s._blocks) for (const b of s._blocks) if (!bl.some((x) => x.name === b.name)) { const L = std140(b.members); bl.push({ name: b.name, inst: b.inst, members: b.members, lay: L.lay, size: L.size, binding: 0, vs: s === vs, fs: s === fs }); } else { const x = bl.find((y) => y.name === b.name); if (s === vs) x.vs = true; else x.fs = true; }
          p._blocks = bl;
          if (p._ok) for (const b of bl) for (const e of b.lay) { const nm = b.inst ? b.name + '.' + e.m.name : e.m.name; e.ui = p._un.findIndex((u) => u.name === nm || u.name === nm + '[0]'); }
        }
        getUniformBlockIndex(p, n) { if (!p || !p._blocks) return 0xFFFFFFFF; const i = p._blocks.findIndex((b) => b.name === n); return i < 0 ? 0xFFFFFFFF : i; }
        uniformBlockBinding(p, i, b) { if (p && p._blocks && p._blocks[i]) p._blocks[i].binding = b; }
        getActiveUniformBlockName(p, i) { return p && p._blocks && p._blocks[i] ? p._blocks[i].name : null; }
        getActiveUniformBlockParameter(p, i, q) {
          const b = p && p._blocks && p._blocks[i]; if (!b) return null;
          switch (q) { case 0x8A3F: return b.binding; case 0x8A40: return b.size; case 0x8A42: return b.lay.length; case 0x8A43: return new Uint32Array(b.lay.map((e) => e.ui >= 0 ? e.ui : 0)); case 0x8A44: return !!b.vs; case 0x8A46: return !!b.fs; }
          return null;
        }
        getUniformIndices(p, names) { if (!p || !p._ok) return null; return names.map((n) => { const i = p._un.findIndex((u) => u.name === n || u.name === n + '[0]' || u.name.replace(/\[0\]$/, '') === n.replace(/\[0\]$/, '')); return i < 0 ? 0xFFFFFFFF : i; }); }
        getActiveUniforms(p, idx, q) {
          const find = (ui) => { for (let bi = 0; bi < (p._blocks || []).length; bi++) for (const e of p._blocks[bi].lay) if (e.ui === ui) return [bi, e]; return null; };
          return idx.map((ui) => { const f = find(ui), u = p._un[ui]; switch (q) { case 0x8A37: return u ? u.type : 0; case 0x8A38: return u ? u.size : 0; case 0x8A3A: return f ? f[0] : -1; case 0x8A3B: return f ? f[1].off : -1; case 0x8A3C: return f && f[1].cnt > 1 ? f[1].stride : f ? 0 : -1; case 0x8A3D: return f && f[1].cols ? 16 : f ? 0 : -1; case 0x8A3E: return false; } return null; });
        }
        _ublocks() {
          const p = this._prog; if (!p || !p._blocks || !p._blocks.length) return;
          for (const b of p._blocks) {
            const ub = this._ubo[b.binding]; if (!ub || !ub.b || !ub.b._d) continue;
            const key = ub.b._ver + ':' + ub.o + ':' + b.binding;
            if (b._key === key && b._kb === ub.b) continue;
            b._key = key; b._kb = ub.b;
            const d = ub.b._d, dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
            for (const e of b.lay) {
              if (e.off < 0 || e.ui === undefined || e.ui < 0) continue;
              const base = ub.o + e.off, out = [];
              if (e.cols) { for (let k = 0; k < e.cnt; k++) for (let c = 0; c < e.cols; c++) for (let r = 0; r < e.rows; r++) { const o = base + k * e.stride + c * 16 + r * 4; out.push(o + 4 <= d.length ? dv.getFloat32(o, true) : 0); } }
              else for (let k = 0; k < e.cnt; k++) for (let c = 0; c < e.comps; c++) { const o = base + k * (e.stride || 0) + c * 4; out.push(o + 4 <= d.length ? (e.isInt ? dv.getInt32(o, true) : dv.getFloat32(o, true)) : 0); }
              N.glUniform(p._id, e.ui, new Float32Array(out), 0);
            }
          }
        }
        _draw(mode, first, count, idx) { this._ublocks(); this._smpApply(); super._draw(mode, first, count, idx); this._smpRestore(); }
        createSampler() { const s = Object.create(G.WebGLSampler.prototype); s._p = {}; return s; }
        deleteSampler(s) { if (s) s._dead = true; } isSampler(s) { return !!(s && s._p && !s._dead); }
        bindSampler(u, s) { this._smp[u] = s; }
        samplerParameteri(s, p, v) { if (s) s._p[p] = v; } samplerParameterf(s, p, v) { if (s) s._p[p] = v; } getSamplerParameter(s, p) { return s ? s._p[p] : null; }
        _smpApply() { this._smpSaved = []; for (let u = 0; u < 32; u++) { const s = this._smp[u], t = this._tex[u].t2; if (s && t) { this._smpSaved.push([t, Object.assign({}, t._p)]); Object.assign(t._p, s._p); } } }
        _smpRestore() { for (const [t, p] of this._smpSaved || []) t._p = p; this._smpSaved = null; }
        bindTexture(tg, t) { const u = this._tex[this._unit]; if (tg === 0x806F) u.t3 = t; else if (tg === 0x8C1A) u.ta = t; else super.bindTexture(tg, t); }
        _bt(tg) { const u = this._tex[this._unit]; return tg === 0x806F ? u.t3 : tg === 0x8C1A ? u.ta : super._bt(tg); }
        texStorage2D(tg, lv, fmt, w, h) { const t = this._bt(tg); if (!t) return this._e(0x502); if (tg === 0x8513) return; t._w = w; t._h = h; t._d = new Uint8Array(w * h * 4); t._fmt = fmt; t._imm = true; }
        texStorage3D() {} texImage3D() {} texSubImage3D() {} copyTexSubImage3D() {} compressedTexImage3D() {} compressedTexSubImage3D() {} framebufferTextureLayer() {}
        texImage2D(tg, lvl, ifmt, a, b, c, fmt, type, data, so) { if (arguments.length >= 9 && typeof data === 'number') data = null; if (arguments.length >= 10 && ArrayBuffer.isView(data) && so) { const es = data.BYTES_PER_ELEMENT || 1; data = new Uint8Array(data.buffer, data.byteOffset + so * es); } if (arguments.length >= 9) return super.texImage2D(tg, lvl, ifmt, a, b, c, fmt, type, data); if (arguments.length === 8 && (typeof c === 'number')) return super.texImage2D(tg, lvl, ifmt, a, b, c, fmt, type, null); return super.texImage2D(...arguments); }
        texSubImage2D(tg, lvl, x, y, a, b, c, d, e, so) { if (arguments.length >= 9 && typeof e === 'number') return; if (arguments.length >= 10 && ArrayBuffer.isView(e) && so) { const es = e.BYTES_PER_ELEMENT || 1; e = new Uint8Array(e.buffer, e.byteOffset + so * es); } if (arguments.length >= 9) return super.texSubImage2D(tg, lvl, x, y, a, b, c, d, e); return super.texSubImage2D(...arguments); }
        renderbufferStorage(t, f, w, h) { const map = { 0x88F0: 0x84F9, 0x81A6: 0x81A5, 0x8CAC: 0x81A5, 0x8CAD: 0x84F9, 0x8058: 0x8056, 0x8051: 0x8056, 0x8C43: 0x8056, 0x881A: 0x8056, 0x8814: 0x8056, 0x8229: 0x8056, 0x822B: 0x8056, 0x8059: 0x8056, 0x8C3A: 0x8056 }; super.renderbufferStorage(t, map[f] || f, w, h); if (this._rb) this._rb._f = f; }
        renderbufferStorageMultisample(t, s, f, w, h) { this.renderbufferStorage(t, f, w, h); }
        getInternalformatParameter(t, f, p) { return p === 0x80A9 ? new Int32Array([4]) : null; }
        bindFramebuffer(t, f) { if (t === 0x8CA8) { this._rfb = f; return; } if (t === 0x8D40) this._rfb = f; this._fb = f; }
        framebufferTexture2D(t, at, tg, tex, l) { const f = t === 0x8CA8 ? this._rfb : this._fb; if (!f) return this._e(0x502); if (at === 0x8CE0) f._c = tex; else if (at === 0x8D00 || at === 0x821A) f._dt = tex; else if (at > 0x8CE0 && at <= 0x8CEF) { if (!f._cx) f._cx = {}; f._cx[at] = tex; } }
        framebufferRenderbuffer(t, at, rt, rb) { const f = t === 0x8CA8 ? this._rfb : this._fb; if (!f) return this._e(0x502); if (at === 0x8D00 || at === 0x821A || at === 0x8D20) f._d = rb; else if (at === 0x8CE0) f._crb = rb; }
        checkFramebufferStatus(t) { const f = t === 0x8CA8 ? this._rfb : this._fb; if (!f) return 0x8CD5; return f._c || f._crb ? 0x8CD5 : 0x8CD7; }
        drawBuffers() {} readBuffer() {} invalidateFramebuffer() {} invalidateSubFramebuffer() {}
        _tgOf(f) { const sv = this._fb; this._fb = f; const r = this._target(); this._fb = sv; return r; }
        blitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask) {
          if (!(mask & 0x4000)) return;
          const S = this._tgOf(this._rfb), D = this._tgOf(this._fb); if (!S || !D) return this._e(0x506);
          const [sw, sh, sc] = S, [dw, dh, dc] = D;
          const W = dx1 - dx0, H = dy1 - dy0; if (!W || !H) return;
          for (let y = Math.min(dy0, dy1); y < Math.max(dy0, dy1); y++) { if (y < 0 || y >= dh) continue; const fy = Math.floor(sy0 + (y - dy0 + 0.5) * (sy1 - sy0) / H); if (fy < 0 || fy >= sh) continue;
            for (let x = Math.min(dx0, dx1); x < Math.max(dx0, dx1); x++) { if (x < 0 || x >= dw) continue; const fx = Math.floor(sx0 + (x - dx0 + 0.5) * (sx1 - sx0) / W); if (fx < 0 || fx >= sw) continue; const si = (fy * sw + fx) * 4, di = (y * dw + x) * 4; dc[di] = sc[si]; dc[di + 1] = sc[si + 1]; dc[di + 2] = sc[si + 2]; dc[di + 3] = sc[si + 3]; } }
          this._dirty();
        }
        clearBufferfv(buf, db, v, so) { so = so || 0; const s = this._st; if (buf === 0x1800) { const cc = s.cc; s.cc = [v[so], v[so + 1], v[so + 2], v[so + 3]]; this.clear(0x4000); s.cc = cc; } else if (buf === 0x1801) { const cd = s.cd; s.cd = v[so]; this.clear(0x100); s.cd = cd; } }
        clearBufferiv(buf, db, v, so) { this.clearBufferfv(buf, db, Array.from(v).map((x) => x / 255), so); } clearBufferuiv(buf, db, v, so) { this.clearBufferiv(buf, db, v, so); }
        clearBufferfi(buf, db, d) { const cd = this._st.cd; this._st.cd = d; this.clear(0x100); this._st.cd = cd; }
        createQuery() { return Object.create(G.WebGLQuery.prototype); } deleteQuery() {} isQuery(q) { return !!q; } beginQuery() {} endQuery() {} getQuery() { return null; } getQueryParameter(q, p) { return p === 0x8867 ? true : 0; }
        fenceSync() { return Object.create(G.WebGLSync.prototype); } isSync(s) { return !!s; } deleteSync() {} clientWaitSync() { return 0x911A; } waitSync() {} getSyncParameter(s, p) { return p === 0x9114 ? 0x9119 : p === 0x9112 ? 0x9116 : 0; }
        createTransformFeedback() { return Object.create(G.WebGLTransformFeedback.prototype); } deleteTransformFeedback() {} isTransformFeedback(t) { return !!t; } bindTransformFeedback() {} beginTransformFeedback() {} endTransformFeedback() {} pauseTransformFeedback() {} resumeTransformFeedback() {} transformFeedbackVaryings() {} getTransformFeedbackVarying() { return null; }
        getFragDataLocation() { return 0; } getIndexedParameter(t, i) { return t === 0x8A28 ? (this._ubo[i] ? this._ubo[i].b : null) : null; }
      }
      for (const k of Object.keys(GLC)) { WebGL2RenderingContext[k] = GLC[k]; }
      for (const k of Object.keys(GL2C)) { WebGL2RenderingContext[k] = GL2C[k]; Object.defineProperty(WebGL2RenderingContext.prototype, k, { value: GL2C[k], enumerable: true }); }
      def0('WebGL2RenderingContext', WebGL2RenderingContext);
      for (const n of ['WebGLVertexArrayObject', 'WebGLSampler', 'WebGLQuery', 'WebGLSync', 'WebGLTransformFeedback']) { const C = class { constructor() { throw new TypeError('Illegal constructor'); } }; Object.defineProperty(C, 'name', { value: n }); def0(n, C); }
      const ogc = Object.getOwnPropertyDescriptor(T.canvas, 'getContext');
      val(T.canvas, 'getContext', function (t, attrs) {
        t = String(t);
        if (t === 'webgl2') { if (this.__ctx && !(this.__ctx instanceof WebGL2RenderingContext)) return null; if (!this.__glc) { this.__glc = new WebGL2RenderingContext(this, attrs); this.__ctx = this.__glc; this.__gl = this.__glc; } return this.__glc; }
        if (t === 'webgl' || t === 'experimental-webgl') { if (this.__ctx && (!(this.__ctx instanceof WebGLRenderingContext) || this.__ctx instanceof WebGL2RenderingContext)) return null; if (!this.__glc) { this.__glc = new WebGLRenderingContext(this, attrs); this.__ctx = this.__glc; this.__gl = this.__glc; } return this.__glc; }
        if (t === '2d' && this.__glc) return null;
        return ogc.value.call(this, t, attrs);
      });
    }
    for (const n of ['WebGLRenderingContext', 'WebGL2RenderingContext', 'WebGLActiveInfo', 'WebGLBuffer', 'WebGLContextEvent', 'WebGLFramebuffer', 'WebGLProgram', 'WebGLQuery', 'WebGLRenderbuffer', 'WebGLSampler', 'WebGLShader', 'WebGLShaderPrecisionFormat', 'WebGLSync', 'WebGLTexture', 'WebGLTransformFeedback', 'WebGLUniformLocation', 'WebGLVertexArrayObject']) { if (G[n]) continue; const C = function () { throw new TypeError('Illegal constructor'); }; Object.defineProperty(C, 'name', { value: n }); def0(n, C); }
  }
}

{
  const defR = (n, v) => { try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true, enumerable: false }); } catch (e) { G[n] = v; } };
  const EP = P.Element, HP = P.HTMLElement;
  val(EP, 'setHTMLUnsafe', function (h) { this.innerHTML = String(h); });
  if (G.ShadowRoot && G.ShadowRoot.prototype) val(G.ShadowRoot.prototype, 'setHTMLUnsafe', function (h) { this.innerHTML = String(h); });
  if (G.Document) val(G.Document, 'parseHTMLUnsafe', function (h) { return new DOMParser().parseFromString(String(h), 'text/html'); });
  val(EP, 'moveBefore', function (n, r) { return this.insertBefore(n, r === undefined ? null : r); });
  if (P.Document) val(P.Document, 'moveBefore', function (n, r) { return this.insertBefore(n, r === undefined ? null : r); });
  for (const k of ['ariaLabel', 'ariaDescription', 'ariaChecked', 'ariaDisabled', 'ariaExpanded', 'ariaHidden', 'ariaSelected', 'ariaPressed', 'ariaValueNow', 'ariaValueMin', 'ariaValueMax', 'ariaValueText', 'ariaLive', 'ariaBusy', 'ariaHasPopup', 'ariaInvalid', 'ariaRequired', 'ariaReadOnly', 'ariaMultiLine', 'ariaMultiSelectable', 'ariaPlaceholder', 'ariaOrientation', 'ariaLevel', 'ariaModal', 'ariaAtomic', 'ariaAutoComplete', 'ariaCurrent', 'ariaPosInSet', 'ariaSetSize', 'ariaSort', 'ariaRoleDescription', 'ariaColCount', 'ariaColIndex', 'ariaColSpan', 'ariaRowCount', 'ariaRowIndex', 'ariaRowSpan', 'ariaKeyShortcuts', 'ariaRelevant', 'ariaBrailleLabel', 'ariaBrailleRoleDescription']) {
    if (Object.prototype.hasOwnProperty.call(EP, k)) continue;
    const a = 'aria-' + k.slice(4).toLowerCase();
    def(EP, k, function () { return this.getAttribute(a); }, function (v) { if (v === null || v === undefined) this.removeAttribute(a); else this.setAttribute(a, String(v)); });
  }
  if (!Object.prototype.hasOwnProperty.call(EP, 'role')) def(EP, 'role', function () { return this.getAttribute('role'); }, function (v) { if (v == null) this.removeAttribute('role'); else this.setAttribute('role', String(v)); });
  {
    let d = null; for (let o = HP; o && !d; o = Object.getPrototypeOf(o)) d = Object.getOwnPropertyDescriptor(o, 'hidden');
    if (d && d.get) def(HP, 'hidden', function () { const v = this.getAttribute('hidden'); return v !== null && v.toLowerCase() === 'until-found' ? 'until-found' : d.get.call(this); }, function (v) { if (typeof v === 'string' && v.toLowerCase() === 'until-found') this.setAttribute('hidden', 'until-found'); else if (d.set) d.set.call(this, v); else if (v) this.setAttribute('hidden', ''); else this.removeAttribute('hidden'); });
  }
  G.Image = function Image(w, h) { const i = document.createElement('img'); if (w !== undefined) i.setAttribute('width', String(w >>> 0)); if (h !== undefined) i.setAttribute('height', String(h >>> 0)); return i; };
  G.Image.prototype = G.HTMLImageElement ? G.HTMLImageElement.prototype : G.Image.prototype;
  G.Option = function Option(t, v, ds, s) { const o = document.createElement('option'); if (t !== undefined) o.textContent = String(t); if (v !== undefined) o.setAttribute('value', String(v)); if (ds) o.setAttribute('selected', ''); if (s) o.selected = true; return o; };
  if (G.HTMLOptionElement) G.Option.prototype = G.HTMLOptionElement.prototype;
  if (N.domGen) {
    const live = (orig) => function (a) {
      const self = this; let g = -1; const arr = [];
      const sync = () => { const ng = N.domGen(); if (ng === g) return; g = ng; const r = orig.call(self, a); arr.length = r.length; for (let i = 0; i < r.length; i++) arr[i] = r[i]; };
      const item = (i) => { sync(); return arr[i >>> 0] || null; };
      const named = (n) => { sync(); n = String(n); for (const e of arr) if (e.id === n || (e.getAttribute && e.getAttribute('name') === n)) return e; return null; };
      return new Proxy(arr, {
        get(t, k) { if (k === 'item') return item; if (k === 'namedItem') return named; sync(); if (typeof k === 'string' && !(k in t) && k !== 'then' && isNaN(+k)) { const e = named(k); if (e) return e; } return t[k]; },
        has(t, k) { sync(); return k in t; },
        ownKeys(t) { sync(); return Reflect.ownKeys(t); },
        getOwnPropertyDescriptor(t, k) { sync(); return Reflect.getOwnPropertyDescriptor(t, k); },
        set() { return true; }, deleteProperty() { return true; }
      });
    };
    for (const pr of [EP, P.Document]) { if (!pr) continue; for (const m of ['getElementsByClassName', 'getElementsByTagName']) { let o = null; for (let p = pr; p && !o; p = Object.getPrototypeOf(p)) { const d = Object.getOwnPropertyDescriptor(p, m); if (d && typeof d.value === 'function') o = d.value; } if (o && !o.__live) { const f = live(o); val(f, '__live', true); val(pr, m, f); } } }
  }
  {
    let d = null; for (let o = T.form; o && !d; o = Object.getPrototypeOf(o)) d = Object.getOwnPropertyDescriptor(o, 'elements');
    if (d && d.get) def(T.form, 'elements', function () { const r = d.get.call(this); const out = Array.from(r || []); for (const e of out) { for (const n of [e.id, e.getAttribute && e.getAttribute('name')]) { if (n && isNaN(+n) && !(n in out)) val(out, n, e); } } val(out, 'namedItem', (n) => out[n] || null); val(out, 'item', (i) => out[i] || null); return out; });
  }
  {
    const msgs = { valueMissing: 'Please fill out this field.', typeMismatch: 'Please enter a valid value.', patternMismatch: 'Please match the requested format.', rangeUnderflow: 'Value must be greater than or equal to the minimum.', rangeOverflow: 'Value must be less than or equal to the maximum.', tooLong: 'Please shorten this text.', tooShort: 'Please lengthen this text.', stepMismatch: 'Please enter a valid value.', badInput: 'Please enter a number.' };
    const barred = (el) => { if (el.disabled || (el.closest && el.closest('fieldset[disabled]'))) return true; const t = el.localName === 'input' ? (el.type || 'text').toLowerCase() : ''; if (t === 'hidden' || t === 'button' || t === 'reset' || t === 'submit' || t === 'image') return true; if (el.localName === 'button' || el.localName === 'fieldset' || el.localName === 'output') return true; if ((el.localName === 'input' || el.localName === 'textarea') && el.hasAttribute('readonly')) return true; return false; };
    const state = (el) => {
      const f = { valueMissing: false, typeMismatch: false, patternMismatch: false, tooLong: false, tooShort: false, rangeUnderflow: false, rangeOverflow: false, stepMismatch: false, badInput: false, customError: !!el.__cvm, valid: true };
      if (!barred(el)) {
        const tag = el.localName, t = tag === 'input' ? (el.type || 'text').toLowerCase() : tag;
        const v = tag === 'select' ? el.value : String(el.value == null ? '' : el.value);
        if (el.hasAttribute('required')) {
          if (t === 'checkbox') f.valueMissing = !el.checked;
          else if (t === 'radio') { const nm = el.name; const f0 = el.form || document; f.valueMissing = !(nm ? [...f0.querySelectorAll('input[type=radio]')].some((r) => r.name === nm && r.checked) : el.checked); }
          else if (t === 'file') f.valueMissing = !(el.files && el.files.length);
          else f.valueMissing = v === '';
        }
        if (v !== '' && tag === 'input') {
          if (t === 'email') f.typeMismatch = !(el.hasAttribute('multiple') ? v.split(',').map((s) => s.trim()) : [v]).every((s) => /^[a-zA-Z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$/.test(s));
          if (t === 'url') { try { new URL(v); } catch (e) { f.typeMismatch = true; } }
          const pat = el.getAttribute('pattern');
          if (pat && ['text', 'search', 'tel', 'url', 'email', 'password'].includes(t)) { try { f.patternMismatch = !new RegExp('^(?:' + pat + ')$', 'v').test(v); } catch (e) { try { f.patternMismatch = !new RegExp('^(?:' + pat + ')$', 'u').test(v); } catch (e2) {} } }
          if (t === 'number' || t === 'range') {
            const n = parseFloat(v); if (isNaN(n)) f.badInput = true; else {
              const mn = parseFloat(el.getAttribute('min')), mx = parseFloat(el.getAttribute('max'));
              if (!isNaN(mn) && n < mn) f.rangeUnderflow = true; if (!isNaN(mx) && n > mx) f.rangeOverflow = true;
              const st = el.getAttribute('step'); if (st !== 'any') { const s = parseFloat(st) || 1; const b = isNaN(mn) ? 0 : mn; const q = (n - b) / s; if (Math.abs(q - Math.round(q)) > 1e-7) f.stepMismatch = true; } }
          }
        }
      }
      f.valid = !Object.keys(f).some((k) => k !== 'valid' && f[k]);
      return f;
    };
    class ValidityState { }
    for (const p of [T.input, T.textarea, T.select, T.button, T.fieldset, T.output]) {
      if (!p) continue;
      def(p, 'validity', function () { const s = state(this); const o = Object.create(ValidityState.prototype); for (const k in s) Object.defineProperty(o, k, { value: s[k], enumerable: true }); return o; });
      def(p, 'willValidate', function () { return !barred(this); });
      def(p, 'validationMessage', function () { if (barred(this)) return ''; if (this.__cvm) return this.__cvm; const s = state(this); for (const k in msgs) if (s[k]) return msgs[k]; return ''; });
      val(p, 'setCustomValidity', function (m) { val(this, '__cvm', String(m == null ? '' : m)); });
      val(p, 'checkValidity', function () { if (barred(this)) return true; const ok = state(this).valid; if (!ok) this.dispatchEvent(new Event('invalid', { cancelable: true })); return ok; });
      val(p, 'reportValidity', function () { return this.checkValidity(); });
    }
    if (!G.ValidityState) defR('ValidityState', ValidityState);
    val(T.form, 'checkValidity', function () { let ok = true; for (const e of this.elements) if (e.checkValidity && !e.checkValidity()) ok = false; return ok; });
    val(T.form, 'reportValidity', function () { return this.checkValidity(); });
  }
  {
    const split = (txt) => { const out = []; let i = 0, n = txt.length; while (i < n) { while (i < n && /\s|;/.test(txt[i])) i++; if (i >= n) break; if (txt.startsWith('/*', i)) { const e = txt.indexOf('*/', i + 2); i = e < 0 ? n : e + 2; continue; } const st = i; let dep = 0, q = 0; for (; i < n; i++) { const c = txt[i]; if (q) { if (c === '\\') i++; else if (c === q) q = 0; continue; } if (c === '"' || c === "'") { q = c; continue; } if (c === '{') dep++; else if (c === '}') { dep--; if (dep <= 0) { i++; break; } } else if (c === ';' && dep === 0) { i++; break; } } out.push(txt.slice(st, i).trim()); } return out.filter(Boolean); };
    const mkRule = (t, sheet, parent) => {
      const b = t.indexOf('{'); const head = (b < 0 ? t : t.slice(0, b)).trim(); const body = b < 0 ? '' : t.slice(b + 1, t.lastIndexOf('}'));
      const r = { cssText: t, parentStyleSheet: sheet, parentRule: parent || null };
      if (head[0] === '@') { const nm = head.slice(1).split(/[\s({]/)[0].toLowerCase(); r.type = { media: 4, 'font-face': 5, page: 6, keyframes: 7, import: 3, charset: 2, namespace: 10, supports: 12, layer: 0, container: 0 }[nm] || 0; const cond = head.slice(nm.length + 1).trim(); if (nm === 'media') r.media = { mediaText: cond, length: cond ? 1 : 0 }, r.conditionText = cond; if (nm === 'supports' || nm === 'container') r.conditionText = cond; if (nm === 'keyframes') r.name = cond; if (nm === 'layer') r.name = cond; if (b >= 0 && nm !== 'font-face' && nm !== 'page') r.cssRules = split(body).map((x) => mkRule(x, sheet, r)); if (nm === 'font-face' || nm === 'page') r.style = decl(body); }
      else { r.type = 1; r.selectorText = head; r.style = decl(body); }
      return r;
    };
    const decl = (body) => { const o = {}; const names = []; for (const part of body.split(';')) { const c = part.indexOf(':'); if (c < 0) continue; const k = part.slice(0, c).trim(), v = part.slice(c + 1).trim(); if (!k) continue; if (!(k in o)) names.push(k); o[k] = v; } const s = { cssText: body.trim(), length: names.length, item: (i) => names[i] || '', getPropertyValue: (k) => o[k] !== undefined ? o[k].replace(/\s*!important$/, '') : '', getPropertyPriority: (k) => /!important$/.test(o[k] || '') ? 'important' : '', setProperty() {}, removeProperty() { return ''; } }; names.forEach((k, i) => { s[i] = k; const cc = k.replace(/-([a-z])/g, (m, x) => x.toUpperCase()); if (!(cc in s)) s[cc] = s.getPropertyValue(k); }); return s; };
    const list = (sheet, txt) => { if (sheet.__rk === txt && sheet.__rl) return sheet.__rl; const a = split(txt).map((x) => mkRule(x, sheet, null)); val(a, 'item', (i) => a[i] || null); val(sheet, '__rk', txt); val(sheet, '__rl', a); return a; };
    const ins = (rules, r, i) => { r = String(r).trim(); const a = split(r); if (a.length !== 1) throw new DOMException("Failed to execute 'insertRule' on 'CSSStyleSheet': Failed to parse the rule '" + r + "'.", 'SyntaxError'); i = i === undefined ? 0 : i >>> 0; if (i > rules.length) throw new DOMException("Failed to execute 'insertRule' on 'CSSStyleSheet': The index provided (" + i + ") is larger than the maximum index (" + rules.length + ').', 'IndexSizeError'); const t = rules.slice(); t.splice(i, 0, a[0]); return [t, i]; };
    const CS = G.CSSStyleSheet;
    if (CS && CS.prototype && CS.prototype.replaceSync) {
      const P0 = CS.prototype, rs0 = P0.replaceSync;
      def(P0, 'cssRules', function () { return list(this, this.__text || ''); }, function () {});
      def(P0, 'rules', function () { return this.cssRules; });
      val(P0, 'insertRule', function (r, i) { const [t, k] = ins(list(this, this.__text || '').map((x) => x.cssText), r, i); rs0.call(this, t.join('\n')); return k; });
      val(P0, 'deleteRule', function (i) { const t = list(this, this.__text || '').map((x) => x.cssText); if ((i >>> 0) >= t.length) throw new DOMException('Index out of range', 'IndexSizeError'); t.splice(i >>> 0, 1); rs0.call(this, t.join('\n')); });
      val(P0, 'addRule', function (s, b, i) { return this.insertRule(s + '{' + (b || '') + '}', i === undefined ? this.cssRules.length : i), -1; });
      val(P0, 'removeRule', function (i) { this.deleteRule(i || 0); });
    }
    def(T.style, 'sheet', function () {
      const el = this; if (!el.isConnected) return null;
      return cache(el, '__sh', () => { const sh = { ownerNode: el, type: 'text/css', href: null, title: el.getAttribute('title'), disabled: false, media: G.MediaList ? new G.MediaList(el) : { mediaText: el.getAttribute('media') || '' },
        get cssRules() { return list(sh, el.textContent || ''); }, get rules() { return this.cssRules; },
        insertRule(r, i) { const [t, k] = ins(this.cssRules.map((x) => x.cssText), r, i); el.textContent = t.join('\n'); return k; },
        deleteRule(i) { const t = this.cssRules.map((x) => x.cssText); if ((i >>> 0) >= t.length) throw new DOMException('Index out of range', 'IndexSizeError'); t.splice(i >>> 0, 1); el.textContent = t.join('\n'); },
        addRule(s, b, i) { this.insertRule(s + '{' + (b || '') + '}', i === undefined ? this.cssRules.length : i); return -1; }, removeRule(i) { this.deleteRule(i || 0); }, replaceSync(t) { el.textContent = String(t); }, replace(t) { el.textContent = String(t); return Promise.resolve(this); } };
        if (G.CSSStyleSheet) Object.setPrototypeOf(sh, Object.create(G.CSSStyleSheet.prototype, { cssRules: { get() { return list(sh, el.textContent || ''); } } }));
        return sh; });
    });
  }
  {
    let crc = null;
    const crc32 = (b, s, e) => { if (!crc) { crc = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; crc[n] = c >>> 0; } } let c = 0xFFFFFFFF; for (let i = s; i < e; i++) c = crc[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
    const png = (w, h, px) => {
      const raw = new Uint8Array((w * 4 + 1) * h); for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; raw.set(px.subarray(y * w * 4, (y + 1) * w * 4), y * (w * 4 + 1) + 1); }
      const nb = Math.max(1, Math.ceil(raw.length / 65535)); const z = new Uint8Array(2 + raw.length + nb * 5 + 4); let o = 0; z[o++] = 0x78; z[o++] = 0x01;
      for (let i = 0, k = 0; k < nb; k++) { const l = Math.min(65535, raw.length - i); z[o++] = k === nb - 1 ? 1 : 0; z[o++] = l & 255; z[o++] = l >> 8; z[o++] = ~l & 255; z[o++] = (~l >> 8) & 255; z.set(raw.subarray(i, i + l), o); o += l; i += l; }
      let a = 1, b2 = 0; for (let i = 0; i < raw.length; i++) { a = (a + raw[i]) % 65521; b2 = (b2 + a) % 65521; } const ad = ((b2 << 16) | a) >>> 0; z[o++] = ad >>> 24; z[o++] = (ad >>> 16) & 255; z[o++] = (ad >>> 8) & 255; z[o++] = ad & 255;
      const chunks = []; const chunk = (t, d) => { const c = new Uint8Array(12 + d.length); const dv = new DataView(c.buffer); dv.setUint32(0, d.length); for (let i = 0; i < 4; i++) c[4 + i] = t.charCodeAt(i); c.set(d, 8); dv.setUint32(8 + d.length, crc32(c, 4, 8 + d.length)); chunks.push(c); };
      const ih = new Uint8Array(13); const dv = new DataView(ih.buffer); dv.setUint32(0, w); dv.setUint32(4, h); ih[8] = 8; ih[9] = 6; chunk('IHDR', ih); chunk('IDAT', z.subarray(0, o)); chunk('IEND', new Uint8Array(0));
      const sig = [137, 80, 78, 71, 13, 10, 26, 10]; let len = 8; for (const c of chunks) len += c.length; const out = new Uint8Array(len); out.set(sig, 0); let p = 8; for (const c of chunks) { out.set(c, p); p += c.length; } return out;
    };
    const pix = (cv) => { const w = cv.width, h = cv.height; if (!w || !h) return null; let d = null; try { if (cv.__ctx && cv.__ctx.getImageData) d = cv.__ctx.getImageData(0, 0, w, h).data; } catch (e) {} if (!d) { try { d = new Uint8ClampedArray(N.cvGet(cv, 0, 0, w, h)); } catch (e) { d = new Uint8ClampedArray(w * h * 4); } } return { w, h, d: new Uint8Array(d.buffer, d.byteOffset, d.byteLength) }; };
    val(T.canvas, 'toDataURL', function () { const p = pix(this); if (!p || p.w * p.h > 4194304) return 'data:,'; const b = png(p.w, p.h, p.d); let s = ''; for (let i = 0; i < b.length; i += 32768) s += String.fromCharCode.apply(null, b.subarray(i, i + 32768)); return 'data:image/png;base64,' + btoa(s); });
    val(T.canvas, 'toBlob', function (cb) { const p = pix(this); const b = p && p.w * p.h <= 4194304 ? new Blob([png(p.w, p.h, p.d)], { type: 'image/png' }) : null; setTimeout(() => cb(b), 0); });
  }
  {
    class Highlight extends Set { constructor(...r) { super(r); this.priority = 0; this.type = 'highlight'; } }
    class HighlightRegistry extends Map { highlightsFromPoint() { return []; } }
    defR('Highlight', Highlight); defR('HighlightRegistry', HighlightRegistry);
    if (G.CSS && !G.CSS.highlights) G.CSS.highlights = new HighlightRegistry();
    class ReportingObserver { constructor(cb, o) { this._cb = cb; this._o = o || {}; } observe() {} disconnect() {} takeRecords() { return []; } }
    defR('ReportingObserver', ReportingObserver);
    if (!URL.parse) val(URL, 'parse', function (u, b) { try { return new URL(u, b); } catch (e) { return null; } });
    if (G.Atomics && !Atomics.waitAsync) {
      const waiters = [];
      const notify0 = Atomics.notify;
      val(Atomics, 'waitAsync', function (ta, i, v, t) { if (Atomics.load(ta, i) !== v) return { async: false, value: 'not-equal' }; t = t === undefined ? Infinity : +t; if (t <= 0) return { async: false, value: 'timed-out' }; let res; const p = new Promise((r) => { res = r; }); const w = { ta, i, res }; waiters.push(w); if (isFinite(t)) setTimeout(() => { const k = waiters.indexOf(w); if (k >= 0) { waiters.splice(k, 1); res('timed-out'); } }, t); return { async: true, value: p }; });
      val(Atomics, 'notify', function (ta, i, c) { let n = notify0 ? notify0.call(Atomics, ta, i, c) : 0; c = c === undefined ? Infinity : c; for (let k = 0; k < waiters.length && n < c;) { const w = waiters[k]; if (w.ta.buffer === ta.buffer && w.i === i) { waiters.splice(k, 1); w.res('ok'); n++; } else k++; } return n; });
    }
    if (!G.navigation) {
      let idc = 0; const key = () => Math.random().toString(36).slice(2, 10);
      class NavigationHistoryEntry extends EventTarget { constructor(u, st, ix) { super(); this.url = u; this.key = key(); this.id = 'e' + (++idc); this.index = ix; this.sameDocument = true; this._s = st; } getState() { return this._s === undefined ? undefined : structuredClone(this._s); } }
      class NavigationDestination { constructor(u) { this.url = u; this.key = ''; this.id = ''; this.index = -1; this.sameDocument = false; } getState() { return undefined; } }
      class NavigateEvent extends Event { constructor(t, o) { super(t, o); Object.assign(this, { navigationType: 'push', destination: null, canIntercept: false, userInitiated: false, hashChange: false, signal: new AbortController().signal, formData: null, downloadRequest: null, info: undefined }, o || {}); } intercept() {} scroll() {} }
      class Navigation extends EventTarget {
        constructor() { super(); this._e = [new NavigationHistoryEntry(location.href, undefined, 0)]; this._i = 0; this.transition = null; this.activation = null; this.onnavigate = null; this.oncurrententrychange = null; this.onnavigatesuccess = null; this.onnavigateerror = null; }
        get currentEntry() { if (this._e[this._i].url !== location.href) { this._e[this._i] = new NavigationHistoryEntry(location.href, undefined, this._i); } return this._e[this._i]; }
        get canGoBack() { return history.length > 1; } get canGoForward() { return false; }
        entries() { this.currentEntry; return this._e.slice(); }
        updateCurrentEntry(o) { this.currentEntry._s = o && o.state; }
        _go(fn, u) { const e = this.currentEntry; const ok = this.dispatchEvent(new NavigateEvent('navigate', { cancelable: true, destination: new NavigationDestination(u || location.href) })); if (ok) fn(); const r = Promise.resolve(this.currentEntry); return { committed: r, finished: r, ok }; }
        navigate(u, o) { const abs = new URL(String(u), location.href).href; return this._go(() => { if (o && o.history === 'replace') location.replace(abs); else location.href = abs; }, abs); }
        reload() { return this._go(() => location.reload()); }
        back() { return this._go(() => history.back()); }
        forward() { return this._go(() => history.forward()); }
        traverseTo() { return this._go(() => {}); }
      }
      defR('Navigation', Navigation); defR('NavigationHistoryEntry', NavigationHistoryEntry); defR('NavigateEvent', NavigateEvent); defR('NavigationDestination', NavigationDestination);
      let nav = null; Object.defineProperty(G, 'navigation', { get() { return nav || (nav = new Navigation()); }, configurable: true });
    }
  }
}

{
  const defG = (n, v) => { try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true }); } catch (e) {} };
  const nav = G.navigator;
  class MediaQueryListEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.media = i.media === undefined ? '' : String(i.media); this.matches = !!i.matches; } }
  const MQS = new Set();
  class MediaQueryList extends EventTarget {
    constructor(q) { super(); val(this, '_q', q); val(this, '_m', !!N.media(q)); val(this, '_oc', null); }
    get media() { return this._q; }
    get matches() { return !!N.media(this._q); }
    get onchange() { return this._oc; }
    set onchange(f) { this._oc = typeof f === 'function' ? f : null; if (this._oc) MQS.add(this); }
    addEventListener(t, f, o) { if (t === 'change') MQS.add(this); return super.addEventListener(t, f, o); }
    addListener(f) { if (f) this.addEventListener('change', f); }
    removeListener(f) { if (f) this.removeEventListener('change', f); }
  }
  const mqCheck = () => { for (const m of MQS) { const now = !!N.media(m._q); if (now !== m._m) { m._m = now; const ev = new MediaQueryListEvent('change', { media: m._q, matches: now }); if (m._oc) { try { m._oc.call(m, ev); } catch (e) { reportErr(e); } } m.dispatchEvent(ev); } } };
  defG('MediaQueryListEvent', MediaQueryListEvent); defG('MediaQueryList', MediaQueryList);
  G.matchMedia = (q) => new MediaQueryList(String(q === undefined ? '' : q).trim());
  class IdleDeadline { constructor(t0, to) { val(this, '_t0', t0); val(this, '_to', !!to); } get didTimeout() { return this._to; } timeRemaining() { return this._to ? 0 : Math.max(0, 50 - (performance.now() - this._t0)); } }
  defG('IdleDeadline', IdleDeadline);
  G.requestIdleCallback = (f, o) => { const tmo = o && o.timeout > 0 ? +o.timeout : 0; const t = performance.now(); return N.setTimeout(() => f(new IdleDeadline(performance.now(), tmo > 0 && performance.now() - t >= tmo)), 1); };
  class Lock { constructor(n, m) { val(this, '_n', n); val(this, '_m', m); } get name() { return this._n; } get mode() { return this._m; } }
  class LockManager {
    constructor() { val(this, '_h', new Map()); val(this, '_q', []); }
    _can(n, m) { const h = this._h.get(n); if (!h || !h.length) return true; return m === 'shared' && h.every((x) => x.mode === 'shared'); }
    _run() { for (let i = 0; i < this._q.length; i++) { const r = this._q[i]; if (!this._can(r.n, r.m)) continue; if (this._q.slice(0, i).some((p) => p.n === r.n)) continue; this._q.splice(i, 1); i--; this._grant(r); } }
    _grant(r) { const l = new Lock(r.n, r.m); const h = this._h.get(r.n) || []; h.push(l); this._h.set(r.n, h); let res; try { res = r.cb(l); } catch (e) { res = Promise.reject(e); } Promise.resolve(res).then((v) => { this._rel(r.n, l); r.ok(v); }, (e) => { this._rel(r.n, l); r.no(e); }); }
    _rel(n, l) { const h = this._h.get(n) || []; const i = h.indexOf(l); if (i >= 0) h.splice(i, 1); if (!h.length) this._h.delete(n); this._run(); }
    request(name, opts, cb) {
      if (typeof opts === 'function') { cb = opts; opts = {}; } opts = opts || {};
      if (typeof cb !== 'function') return Promise.reject(new TypeError("Failed to execute 'request' on 'LockManager': The callback provided as parameter 3 is not a function."));
      name = String(name); if (name.startsWith('-')) return Promise.reject(new DOMException("Failed to execute 'request' on 'LockManager': Names cannot start with '-'.", 'NotSupportedError'));
      const m = opts.mode === 'shared' ? 'shared' : 'exclusive';
      if (opts.signal && opts.signal.aborted) return Promise.reject(opts.signal.reason || new DOMException('The request was aborted.', 'AbortError'));
      return new Promise((ok, no) => {
        const r = { n: name, m, cb, ok, no };
        if (opts.steal) { this._h.delete(name); this._grant(r); return; }
        if (opts.ifAvailable && (!this._can(name, m) || this._q.some((p) => p.n === name))) { Promise.resolve().then(() => { let v; try { v = cb(null); } catch (e) { no(e); return; } Promise.resolve(v).then(ok, no); }); return; }
        this._q.push(r);
        if (opts.signal) opts.signal.addEventListener('abort', () => { const i = this._q.indexOf(r); if (i >= 0) { this._q.splice(i, 1); no(opts.signal.reason || new DOMException('The request was aborted.', 'AbortError')); } });
        this._run();
      });
    }
    query() { const held = [], pending = []; for (const [n, h] of this._h) for (const l of h) held.push({ name: n, mode: l.mode, clientId: '' }); for (const r of this._q) pending.push({ name: r.n, mode: r.m, clientId: '' }); return Promise.resolve({ held, pending }); }
  }
  defG('Lock', Lock); defG('LockManager', LockManager);
  const lm = new LockManager();
  try { Object.defineProperty(nav, 'locks', { get: () => lm, configurable: true }); } catch (e) {}
  class MediaSession {
    constructor() { val(this, '_h', new Map()); val(this, '_md', null); val(this, '_ps', 'none'); val(this, '_pos', null); }
    get metadata() { return this._md; } set metadata(v) { if (v !== null && G.MediaMetadata && !(v instanceof G.MediaMetadata)) throw new TypeError("Failed to set the 'metadata' property on 'MediaSession': Failed to convert value to 'MediaMetadata'."); this._md = v; }
    get playbackState() { return this._ps; } set playbackState(v) { if (['none', 'paused', 'playing'].indexOf(v) >= 0) this._ps = v; }
    setActionHandler(a, h) { const ok = ['play', 'pause', 'seekbackward', 'seekforward', 'previoustrack', 'nexttrack', 'skipad', 'stop', 'seekto', 'togglemicrophone', 'togglecamera', 'togglescreenshare', 'hangup', 'previousslide', 'nextslide', 'enterpictureinpicture', 'voiceactivity']; if (ok.indexOf(a) < 0) throw new TypeError("Failed to execute 'setActionHandler' on 'MediaSession': The provided value '" + a + "' is not a valid enum value of type MediaSessionAction."); if (h == null) this._h.delete(a); else this._h.set(a, h); }
    setPositionState(st) { if (st === undefined) { this._pos = null; return; } const d = st.duration, p = st.position === undefined ? 0 : st.position, r = st.playbackRate === undefined ? 1 : st.playbackRate; if (!(d >= 0) || p < 0 || p > d || r === 0) throw new TypeError("Failed to execute 'setPositionState' on 'MediaSession': The provided position state is invalid."); this._pos = { duration: d, position: p, playbackRate: r }; }
    setMicrophoneActive() { return Promise.resolve(); } setCameraActive() { return Promise.resolve(); }
  }
  defG('MediaSession', MediaSession);
  const ms = new MediaSession();
  try { Object.defineProperty(nav, 'mediaSession', { get: () => ms, configurable: true }); } catch (e) {}
  class StorageManager { estimate() { return Promise.resolve({ quota: 0, usage: 0 }); } persist() { return Promise.resolve(false); } persisted() { return Promise.resolve(false); } }
  defG('StorageManager', StorageManager);
  try { const so = nav.storage; if (so && typeof so === 'object') { for (const k of ['estimate', 'persist', 'persisted']) if (!Object.prototype.hasOwnProperty.call(so, k) && typeof so[k] === 'function') val(so, k, so[k]); Object.setPrototypeOf(so, StorageManager.prototype); } } catch (e) {}
  class ScreenOrientation extends EventTarget {
    constructor() { super(); val(this, '_oc', null); val(this, '_last', ''); }
    get type() { return N.vw() >= N.vh() ? 'landscape-primary' : 'portrait-primary'; }
    get angle() { return N.vw() >= N.vh() ? 0 : 90; }
    get onchange() { return this._oc; } set onchange(f) { this._oc = typeof f === 'function' ? f : null; }
    lock() { return Promise.reject(new DOMException('screen.orientation.lock() is not available on this device.', 'NotSupportedError')); }
    unlock() {}
  }
  defG('ScreenOrientation', ScreenOrientation);
  const so = new ScreenOrientation(); so._last = so.type;
  try { Object.defineProperty(G.screen, 'orientation', { get: () => so, configurable: true }); } catch (e) {}
  G.addEventListener('resize', () => { mqCheck(); const t = so.type; if (t !== so._last) { so._last = t; const ev = new Event('change'); if (so._oc) { try { so._oc.call(so, ev); } catch (e) { reportErr(e); } } so.dispatchEvent(ev); } });
  N.setInterval(mqCheck, 1000);
  class PerformanceTiming { toJSON() { const o = {}; for (const k of Object.keys(this)) o[k] = this[k]; return o; } }
  class PerformanceNavigation { toJSON() { return { type: this.type, redirectCount: this.redirectCount }; } }
  for (const [k, v] of Object.entries({ TYPE_NAVIGATE: 0, TYPE_RELOAD: 1, TYPE_BACK_FORWARD: 2, TYPE_RESERVED: 255 })) { Object.defineProperty(PerformanceNavigation, k, { value: v, enumerable: true }); Object.defineProperty(PerformanceNavigation.prototype, k, { value: v, enumerable: true }); }
  defG('PerformanceTiming', PerformanceTiming); defG('PerformanceNavigation', PerformanceNavigation);
  try { const pt = G.performance.timing; if (pt && typeof pt === 'object') Object.setPrototypeOf(pt, PerformanceTiming.prototype); const pn = G.performance.navigation; if (pn && typeof pn === 'object') { if (pn.redirectCount === undefined) pn.redirectCount = 0; Object.setPrototypeOf(pn, PerformanceNavigation.prototype); } } catch (e) {}
  class VideoPlaybackQuality { constructor(o) { o = o || {}; val(this, '_o', o); } get creationTime() { return this._o.creationTime || 0; } get totalVideoFrames() { return this._o.totalVideoFrames || 0; } get droppedVideoFrames() { return this._o.droppedVideoFrames || 0; } get corruptedVideoFrames() { return this._o.corruptedVideoFrames || 0; } }
  defG('VideoPlaybackQuality', VideoPlaybackQuality);
  if (T.video && typeof T.video.getVideoPlaybackQuality === 'function') { const og = T.video.getVideoPlaybackQuality; val(T.video, 'getVideoPlaybackQuality', function getVideoPlaybackQuality() { return new VideoPlaybackQuality(og.call(this)); }); }
  class MediaList {
    constructor(el) { val(this, '_el', el || null); val(this, '_t', ''); }
    get mediaText() { return this._el ? (this._el.getAttribute('media') || '') : this._t; }
    set mediaText(v) { v = String(v === null ? '' : v); if (this._el) { if (v) this._el.setAttribute('media', v); else this._el.removeAttribute('media'); } else this._t = v; }
    _l() { return this.mediaText.split(',').map((x) => x.trim()).filter((x) => x); }
    get length() { return this._l().length; }
    item(i) { const l = this._l(); return i >= 0 && i < l.length ? l[i] : null; }
    appendMedium(m) { const l = this._l(); m = String(m).trim(); if (m && l.indexOf(m) < 0) { l.push(m); this.mediaText = l.join(', '); } }
    deleteMedium(m) { const l = this._l(); const i = l.indexOf(String(m).trim()); if (i < 0) throw new DOMException("Failed to execute 'deleteMedium' on 'MediaList': Failed to delete '" + m + "'.", 'NotFoundError'); l.splice(i, 1); this.mediaText = l.join(', '); }
    toString() { return this.mediaText; }
    [Symbol.iterator]() { return this._l()[Symbol.iterator](); }
  }
  defG('MediaList', MediaList);
  class MediaCapabilities {
    _ct(c, kind) { const x = c && c[kind]; return x && x.contentType ? String(x.contentType) : ''; }
    decodingInfo(c) {
      if (!c || !c.type) return Promise.reject(new TypeError("Failed to execute 'decodingInfo' on 'MediaCapabilities': The provided value is not of type 'MediaDecodingConfiguration'."));
      if (!c.video && !c.audio) return Promise.reject(new TypeError("Failed to execute 'decodingInfo' on 'MediaCapabilities': The configuration dictionary has neither |video| nor |audio| specified."));
      const one = (kind) => { const ct = this._ct(c, kind); if (!ct) return true; if (c.type === 'media-source') return !!(G.MediaSource && G.MediaSource.isTypeSupported(ct)); const el = document.createElement(kind === 'video' ? 'video' : 'audio'); return el.canPlayType(ct) !== ''; };
      const sup = (!c.video || one('video')) && (!c.audio || one('audio'));
      const v = c.video || {}; const px = (v.width || 0) * (v.height || 0); const fps = v.framerate || 30;
      const smooth = sup && px <= 1920 * 1080 && fps <= 30;
      return Promise.resolve({ supported: sup, smooth, powerEfficient: false, keySystemAccess: null, configuration: c });
    }
    encodingInfo(c) { if (!c || !c.type) return Promise.reject(new TypeError("Failed to execute 'encodingInfo' on 'MediaCapabilities': The provided value is not of type 'MediaEncodingConfiguration'.")); return Promise.resolve({ supported: false, smooth: false, powerEfficient: false, configuration: c }); }
  }
  defG('MediaCapabilities', MediaCapabilities);
  const mc = new MediaCapabilities();
  try { Object.defineProperty(nav, 'mediaCapabilities', { get: () => mc, configurable: true }); } catch (e) {}
}

{
  const defP = (o, k, f) => { if (o && !(k in o)) try { Object.defineProperty(o, k, { value: f, writable: true, configurable: true }); } catch (e) {} };
  const EP = P.Element;
  const ser = (n, sr) => { let o = ''; for (const c of n.childNodes) { if (c.nodeType === 1) { if (sr && c.shadowRoot && c.shadowRoot.serializable !== false) { const h = c.outerHTML; const i = h.indexOf('>'); o += h.slice(0, i + 1) + '<template shadowrootmode="' + (c.shadowRoot.mode || 'open') + '">' + ser(c.shadowRoot, sr) + '</template>' + ser(c, sr) + h.slice(h.lastIndexOf('</')); } else o += c.outerHTML; } else if (c.nodeType === 3) o += String(c.data).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); else if (c.nodeType === 8) o += '<!--' + c.data + '-->'; } return o; };
  defP(EP, 'getHTML', function getHTML(o) { return o && o.serializableShadowRoots ? ser(this, true) : this.innerHTML; });
  if (G.ShadowRoot) defP(G.ShadowRoot.prototype, 'getHTML', function getHTML(o) { return o && o.serializableShadowRoots ? ser(this, true) : this.innerHTML; });
  defP(EP, 'webkitMatchesSelector', function webkitMatchesSelector(s) { return this.matches(s); });
  defP(EP, 'scrollIntoViewIfNeeded', function scrollIntoViewIfNeeded(center) { const r = this.getBoundingClientRect(); if (r.top >= 0 && r.bottom <= innerHeight) return; this.scrollIntoView(center === false ? { block: 'nearest' } : { block: 'center' }); });
  defP(EP, 'requestPointerLock', function requestPointerLock() { const e = new DOMException('Pointer lock is not available in this browser.', 'NotSupportedError'); setTimeout(() => { const ev = new Event('pointerlockerror'); run(document, ev); }, 0); return Promise.reject(e); });
  if (G.Document) defP(G.Document.prototype, 'createExpression', function createExpression(x, r) { const doc = this; return { evaluate(node, type, res) { return doc.evaluate(x, node, r || null, type || 0, res || null); } }; });
  if (G.Document) defP(G.Document.prototype, 'exitPictureInPicture', function exitPictureInPicture() { return Promise.reject(new DOMException('There is no Picture-in-Picture element in this document.', 'InvalidStateError')); });
  if (G.HTMLSelectElement) defP(G.HTMLSelectElement.prototype, 'showPicker', function showPicker() { if (this.disabled) throw new DOMException("Failed to execute 'showPicker' on 'HTMLSelectElement': Select cannot be disabled.", 'InvalidStateError'); this.focus(); this.click(); });
  defP(nav0(), 'requestMediaKeySystemAccess', function requestMediaKeySystemAccess() { return Promise.reject(new DOMException('Unsupported keySystem or supportedConfigurations.', 'NotSupportedError')); });
  function nav0() { return G.navigator; }
  defP(G, 'find', function find(str, cs, back) { str = String(str || ''); if (!str) return false; const t = document.body ? document.body.innerText || document.body.textContent || '' : ''; return cs ? t.indexOf(str) >= 0 : t.toLowerCase().indexOf(str.toLowerCase()) >= 0; });
  if (G.Request) defP(G.Request.prototype, 'formData', function formData() { const ct = (this.headers && this.headers.get('content-type')) || ''; return this.text().then((t) => { const fd = new FormData(); if (/multipart\/form-data/i.test(ct)) { const m = /boundary=("?)([^";]+)\1/i.exec(ct); if (!m) throw new TypeError('Failed to fetch'); for (const part of t.split('--' + m[2])) { const i = part.indexOf('\r\n\r\n'); if (i < 0) continue; const h = part.slice(0, i); const nm = /name="([^"]*)"/i.exec(h); if (!nm) continue; fd.append(nm[1], part.slice(i + 4).replace(/\r\n$/, '')); } } else for (const [k, v] of new URLSearchParams(t)) fd.append(k, v); return fd; }); });
  class CSSStyleValue { toString() { return ''; } }
  class CSSNumericValue extends CSSStyleValue { }
  class CSSUnitValue extends CSSNumericValue { constructor(v, u) { super(); val(this, '_v', +v); val(this, '_u', String(u)); } get value() { return this._v; } set value(x) { this._v = +x; } get unit() { return this._u; } toString() { return this._u === 'number' ? String(this._v) : this._u === 'percent' ? this._v + '%' : this._v + this._u; } }
  for (const [n, C] of [['CSSStyleValue', CSSStyleValue], ['CSSNumericValue', CSSNumericValue], ['CSSUnitValue', CSSUnitValue]]) if (!G[n]) defP(G, n, C);
  if (G.CSS) for (const u of ['number', 'percent', 'em', 'ex', 'ch', 'rem', 'vw', 'vh', 'vmin', 'vmax', 'cm', 'mm', 'in', 'pt', 'pc', 'px', 'deg', 'grad', 'rad', 'turn', 's', 'ms', 'Hz', 'kHz', 'dpi', 'dpcm', 'dppx', 'fr', 'svw', 'svh', 'lvw', 'lvh', 'dvw', 'dvh', 'cqw', 'cqh']) defP(G.CSS, u, ((uu) => function (v) { return new CSSUnitValue(v, uu); })(u));
  if (G.Intl && G.Intl.DateTimeFormat) defP(G.Intl.DateTimeFormat.prototype, 'formatRangeToParts', function formatRangeToParts(a, b) { const p1 = this.formatToParts(a).map((p) => Object.assign({}, p, { source: 'startRange' })); const p2 = this.formatToParts(b).map((p) => Object.assign({}, p, { source: 'endRange' })); return p1.concat([{ type: 'literal', value: ' – ', source: 'shared' }], p2); });
  if (G.Intl && !G.Intl.DurationFormat) {
    const UN = { years: ['yr', 'year'], months: ['mth', 'month'], weeks: ['wk', 'week'], days: ['day', 'day'], hours: ['hr', 'hour'], minutes: ['min', 'minute'], seconds: ['sec', 'second'], milliseconds: ['ms', 'millisecond'], microseconds: ['μs', 'microsecond'], nanoseconds: ['ns', 'nanosecond'] };
    class DurationFormat {
      constructor(l, o) { o = o || {}; val(this, '_l', Array.isArray(l) ? l[0] || 'en' : l || 'en'); val(this, '_s', o.style || 'short'); }
      format(d) { d = d || {}; if (this._s === 'digital') { const p2 = (x) => String(x | 0).padStart(2, '0'); return (d.hours | 0) + ':' + p2(d.minutes) + ':' + p2(d.seconds); } const out = []; for (const k of Object.keys(UN)) { const v = d[k]; if (!v) continue; const u = UN[k]; if (this._s === 'narrow') out.push(v + (k === 'months' ? 'mo' : u[0][0] === 'μ' ? u[0] : u[0].length > 3 ? u[0][0] : u[0])); else if (this._s === 'long') out.push(v + ' ' + u[1] + (v === 1 ? '' : 's')); else out.push(v + ' ' + u[0] + (v !== 1 && (k === 'years' || k === 'months' || k === 'weeks' || k === 'days') ? 's' : '')); } return this._s === 'narrow' ? out.join(' ') : out.join(', '); }
      formatToParts(d) { return [{ type: 'literal', value: this.format(d) }]; }
      resolvedOptions() { return { locale: this._l, style: this._s, numberingSystem: 'latn' }; }
      static supportedLocalesOf(l) { return Array.isArray(l) ? l.slice() : l ? [l] : []; }
    }
    G.Intl.DurationFormat = DurationFormat;
  }
}

{
  const seen = new Set();
  const pick = (q, ks) => { for (const k of ks) { const v = q.get(k); if (v) return v; } return ''; };
  G.__repSeen = (u, body) => {
    try {
      const url = String(u);
      if (!/jserror|\/csp[-_]?report|\/cspreport/i.test(url)) return;
      const U = new URL(url, document.baseURI);
      let q = U.searchParams;
      let bq = null;
      if (typeof body === 'string') { try { bq = new URLSearchParams(body); } catch (e) {} }
      else if (body && typeof URLSearchParams === 'function' && body instanceof URLSearchParams) bq = body;
      else if (body && G.FormData && body instanceof G.FormData) { bq = new URLSearchParams(); for (const [k, v] of body) if (typeof v === 'string') bq.append(k, v); }
      const g = (ks) => pick(q, ks) || (bq ? pick(bq, ks) : '');
      const msg = g(['error', 'message', 'msg', 'ex', 'jsel']) || (typeof body === 'string' ? body.slice(0, 300) : '');
      const where = g(['script', 'url', 'src', 'file', 'filename']);
      const line = g(['line', 'lineno', 'ln']);
      const tr = g(['trace', 'stack', 'stacktrace']);
      const key = msg + '|' + where + '|' + line;
      if (seen.has(key) || seen.size > 40) return; seen.add(key);
      N.log('Page reported its own error to ' + U.origin + U.pathname + ': ' + (msg || '(no message)') + (where ? ' at ' + where + (line ? ':' + line : '') : '') + (tr ? '\n' + tr.slice(0, 600) : ''), 4); try { if (tr && /:\d+:\d+\)/.test(tr)) N.errSnippet(tr); else if (where && line) N.errSnippet('(' + where + ':' + line + ':' + (g(['col', 'colno', 'column']) || '1') + ')'); } catch (e) {}
    } catch (e) {}
  };
  const X = G.XMLHttpRequest && G.XMLHttpRequest.prototype;
  if (X && X.open && X.send) {
    const oo = X.open, os = X.send;
    X.open = function open(m, u) { try { val(this, '__ru', String(u)); } catch (e) {} return oo.apply(this, arguments); };
    X.send = function send(b) { try { if (this.__ru) G.__repSeen(this.__ru, b); } catch (e) {} return os.apply(this, arguments); };
  }
  if (typeof G.fetch === 'function') {
    const of = G.fetch;
    G.fetch = function fetch(r, o) { try { const u = typeof r === 'string' ? r : r && r.url ? r.url : String(r); G.__repSeen(u, o && o.body); } catch (e) {} return of.apply(this, arguments); };
  }
  let IP = null; for (const C of [G.HTMLImageElement, G.HTMLElement, G.Element]) { const pp = C && C.prototype; if (pp && Object.getOwnPropertyDescriptor(pp, 'src')) { IP = pp; break; } }
  const sd = IP && Object.getOwnPropertyDescriptor(IP, 'src');
  if (sd && sd.set && sd.configurable) Object.defineProperty(IP, 'src', { get: sd.get, set: function (v) { try { if (this.localName === 'img') G.__repSeen(String(v)); } catch (e) {} return sd.set.call(this, v); }, configurable: true, enumerable: sd.enumerable });
}
if (!N.isFrame() && typeof N.pendingSet === 'function') {
  const PR = new Map(); let prid = 0;
  const PT = new Map();
  const shortU = (u) => { u = String(u); return u.length > 160 ? u.slice(0, 157) + '...' : u; };
  const of = G.fetch;
  if (of) G.fetch = function (input, init) { const id = ++prid; let u = ''; try { u = input instanceof Request ? input.url : String(input); } catch (e) {} PR.set(id, { k: 'fetch', m: (init && init.method) || (input && input.method) || 'GET', u, t: Date.now() }); let p; try { p = of.apply(this, arguments); } catch (e) { PR.delete(id); throw e; } Promise.resolve(p).then(() => PR.delete(id), () => PR.delete(id)); return p; };
  const XP = G.XMLHttpRequest && G.XMLHttpRequest.prototype;
  if (XP && XP.send) { const os = XP.send; XP.send = function (b) { const id = ++prid; PR.set(id, { k: 'xhr', m: this._m || 'GET', u: this._u || '', t: Date.now() }); const done = () => PR.delete(id); this.addEventListener('loadend', done); try { return os.call(this, b); } catch (e) { done(); throw e; } }; }
  const ost = G.setTimeout, osi = G.setInterval, oct = G.clearTimeout;
  const fnm = (f) => (typeof f === 'function' ? (f.name || 'anonymous') : 'code');
  G.setTimeout = function (f, ms, ...a) { let id; const g = typeof f === 'function' ? function () { PT.delete(id); return f.apply(this, a); } : f; id = ost(g, ms); if (ms >= 1000) PT.set(id, { k: 'timeout', ms: +ms || 0, n: fnm(f), t: Date.now() }); return id; };
  G.setInterval = function (f, ms, ...a) { const id = osi(f, ms, ...a); PT.set(id, { k: 'interval', ms: +ms || 0, n: fnm(f), t: Date.now() }); return id; };
  G.clearTimeout = G.clearInterval = function (id) { PT.delete(id | 0); return oct(id); };
  const TRN = new Map(), ANM = new Map();
  const elName = (e) => { if (!e || !e.tagName) return '?'; let s = e.tagName.toLowerCase(); if (e.id) s += '#' + e.id; const c = e.getAttribute && e.getAttribute('class'); if (c) s += '.' + c.trim().split(/\s+/).slice(0, 3).join('.'); return s.length > 90 ? s.slice(0, 87) + '...' : s; };
  const addEv = (m, k, e, p) => { let x = m.get(e); if (!x) { x = new Map(); m.set(e, x); } x.set(p, Date.now()); if (m.size > 400) m.delete(m.keys().next().value); };
  const delEv = (m, e, p) => { const x = m.get(e); if (x) { x.delete(p); if (!x.size) m.delete(e); } };
  document.addEventListener('transitionrun', nvxL((e) => addEv(TRN, 0, e.target, e.propertyName)), true);
  document.addEventListener('transitionend', nvxL((e) => delEv(TRN, e.target, e.propertyName)), true);
  document.addEventListener('transitioncancel', nvxL((e) => delEv(TRN, e.target, e.propertyName)), true);
  document.addEventListener('animationstart', nvxL((e) => addEv(ANM, 0, e.target, e.animationName)), true);
  document.addEventListener('animationend', nvxL((e) => delEv(ANM, e.target, e.animationName)), true);
  document.addEventListener('animationcancel', nvxL((e) => delEv(ANM, e.target, e.animationName)), true);
  const PP = new Map(); let ppid = 0; const PPS = new WeakSet();
  const regP = (p, l) => { if (!p || typeof p.then !== 'function' || PPS.has(p)) return p; PPS.add(p); const id = ++ppid; PP.set(id, { l, t: Date.now() }); if (PP.size > 300) PP.delete(PP.keys().next().value); try { p.then(() => PP.delete(id), () => PP.delete(id)); } catch (e) { PP.delete(id); } return p; };
  const patchM = (o, n, l) => { try { if (!o) return; const f = o[n]; if (typeof f !== 'function' || f.__pp) return; const w = function () { const a0 = arguments[0]; return regP(f.apply(this, arguments), typeof a0 === 'string' ? l.replace(/\(\)$/, '("' + a0.slice(0, 60) + '")') : l); }; val(w, '__pp', true); try { Object.defineProperty(w, 'name', { value: n }); } catch (e) {} Object.defineProperty(o, n, { value: w, writable: true, configurable: true }); } catch (e) {} };
  const patchG = (o, n, l) => { try { if (!o) return; let q = o, d; while (q && !(d = Object.getOwnPropertyDescriptor(q, n))) q = Object.getPrototypeOf(q); if (!d || !d.get || d.get.__pp) return; const g = d.get; const w = function () { return regP(g.call(this), l); }; val(w, '__pp', true); Object.defineProperty(q, n, { get: w, set: d.set, configurable: true, enumerable: d.enumerable }); } catch (e) {} };
  const nv = G.navigator || {};
  const tryo = (f) => { try { return f(); } catch (e) { return null; } };
  const cr = tryo(() => nv.credentials); for (const m of ['get', 'create', 'store', 'preventSilentAccess']) patchM(cr, m, 'navigator.credentials.' + m + '()');
  for (const C of ['PublicKeyCredential', 'Credential']) for (const m of ['isUserVerifyingPlatformAuthenticatorAvailable', 'isConditionalMediationAvailable', 'getClientCapabilities']) patchM(G[C], m, C + '.' + m + '()');
  patchM(tryo(() => nv.permissions), 'query', 'navigator.permissions.query()');
  const st = tryo(() => nv.storage); for (const m of ['estimate', 'persist', 'persisted', 'getDirectory']) patchM(st, m, 'navigator.storage.' + m + '()');
  const cb = tryo(() => nv.clipboard); for (const m of ['read', 'readText', 'write', 'writeText']) patchM(cb, m, 'navigator.clipboard.' + m + '()');
  patchM(tryo(() => nv.userAgentData), 'getHighEntropyValues', 'navigator.userAgentData.getHighEntropyValues()');
  const md = tryo(() => nv.mediaDevices); for (const m of ['enumerateDevices', 'getUserMedia', 'getDisplayMedia']) patchM(md, m, 'navigator.mediaDevices.' + m + '()');
  const sw = tryo(() => nv.serviceWorker); for (const m of ['register', 'getRegistration', 'getRegistrations']) patchM(sw, m, 'navigator.serviceWorker.' + m + '()'); patchG(sw, 'ready', 'navigator.serviceWorker.ready');
  patchM(tryo(() => nv.locks), 'request', 'navigator.locks.request()');
  for (const m of ['getBattery', 'requestMIDIAccess', 'requestMediaKeySystemAccess', 'share', 'getInstalledRelatedApps']) patchM(nv, m, 'navigator.' + m + '()');
  const fo = tryo(() => document.fonts); patchM(fo, 'load', 'document.fonts.load()'); patchG(fo, 'ready', 'document.fonts.ready');
  if (G.FontFace) { patchM(G.FontFace.prototype, 'load', 'FontFace.load()'); patchG(G.FontFace.prototype, 'loaded', 'FontFace.loaded'); }
  if (G.HTMLImageElement) patchM(G.HTMLImageElement.prototype, 'decode', 'img.decode()');
  for (const C of ['HTMLMediaElement', 'HTMLVideoElement', 'HTMLAudioElement']) if (G[C]) patchM(G[C].prototype, 'play', C === 'HTMLMediaElement' ? 'media.play()' : C.slice(4, -7).toLowerCase() + '.play()');
  for (const m of ['hasStorageAccess', 'requestStorageAccess', 'exitFullscreen', 'exitPointerLock']) patchM(document, m, 'document.' + m + '()');
  if (G.Element) patchM(G.Element.prototype, 'requestFullscreen', 'element.requestFullscreen()');
  const ca = tryo(() => G.caches); for (const m of ['open', 'match', 'has', 'keys', 'delete']) patchM(ca, m, 'caches.' + m + '()');
  patchM(tryo(() => G.indexedDB), 'databases', 'indexedDB.databases()');
  const sb = tryo(() => G.crypto && G.crypto.subtle); for (const m of ['digest', 'encrypt', 'decrypt', 'sign', 'verify', 'importKey', 'exportKey', 'generateKey', 'deriveKey', 'deriveBits', 'wrapKey', 'unwrapKey']) patchM(sb, m, 'crypto.subtle.' + m + '()');
  patchM(G, 'createImageBitmap', 'createImageBitmap()');
  if (G.Notification) patchM(G.Notification, 'requestPermission', 'Notification.requestPermission()');
  if (G.customElements) patchM(G.customElements, 'whenDefined', 'customElements.whenDefined()');
  if (G.scheduler) { patchM(G.scheduler, 'postTask', 'scheduler.postTask()'); patchM(G.scheduler, 'yield', 'scheduler.yield()'); }
  if (G.Animation) patchG(G.Animation.prototype, 'finished', 'Animation.finished');
  const OBS = [];
  for (const on of ['IntersectionObserver', 'ResizeObserver', 'MutationObserver', 'PerformanceObserver']) {
    const O = G[on]; if (typeof O !== 'function') continue;
    try {
      const W = { [on]: class extends O { constructor(cb, opt) { const rec = { n: on, calls: 0, obs: 0, t: Date.now(), f: fnm(cb) }; super(function () { rec.calls++; return typeof cb === 'function' ? cb.apply(this, arguments) : undefined; }, opt); val(this, '__rec', rec); OBS.push(rec); if (OBS.length > 300) OBS.shift(); } observe() { if (this.__rec) this.__rec.obs++; return super.observe.apply(this, arguments); } } }[on];
      for (const k of Object.getOwnPropertyNames(O)) if (!(k in W) && k !== 'prototype' && k !== 'length' && k !== 'name') try { W[k] = O[k]; } catch (e) {}
      Object.defineProperty(G, on, { value: W, writable: true, configurable: true });
    } catch (e) {}
  }
  if (typeof G.requestIdleCallback === 'function') { const ric = G.requestIdleCallback, cic = G.cancelIdleCallback; const IC = new Map(); G.requestIdleCallback = function (f, o) { let id; id = ric.call(G, function () { IC.delete(id); return f.apply(this, arguments); }, o); IC.set(id, Date.now()); return id; }; if (cic) G.cancelIdleCallback = function (id) { IC.delete(id); return cic.call(G, id); }; G.__ic = IC; }
  const EV = new Map(), EVF = new Set();
  const evLabel = (t) => t === G ? 'window' : t === document ? 'document' : t && t.nodeType === 1 ? elName(t) : t && t.nodeType === 9 ? 'document (other)' : null;
  const etp = G.EventTarget && G.EventTarget.prototype;
  if (etp && etp.addEventListener && !etp.addEventListener.__ev) {
    const oadd = etp.addEventListener;
    const w = function addEventListener(type, fn, opt) {
      try {
        const lb = evLabel(this);
        if (lb && typeof type === 'string') {
          const ty = type;
          if (!EV.has(ty)) { EV.set(ty, { n: 0, w: [], t: Date.now() }); oadd.call(G, ty, nvxL(() => { EVF.add(ty); }), true); }
          const e = EV.get(ty); e.n++; if (e.w.length < 3 && e.w.indexOf(lb) < 0) e.w.push(lb);
        }
      } catch (e) {}
      return oadd.call(this, type, fn, opt);
    };
    val(w, '__ev', true);
    try { Object.defineProperty(etp, 'addEventListener', { value: w, writable: true, configurable: true }); } catch (e) {}
  }
  const USER_EV = /^(click|dblclick|aux|context|mouse|pointer|touch|key|wheel|scroll|resize|input|beforeinput|change|submit|reset|select|drag|drop|copy|cut|paste|composition|focus|blur|beforeunload|unload|pagehide|popstate|hashchange|storage|online|offline|orientation|devicemotion|deviceorientation|gamepad|fullscreen|play|pause|ended|seek|volume|rate|timeupdate|invalid|toggle|beforetoggle|search|animation|transition|selectionchange|selectstart|languagechange|beforeprint|afterprint|appinstalled|visibilitychange|error|abort)/;
  const SD = new WeakSet();
  document.addEventListener('load', (e) => { if (e.target && e.target.tagName) SD.add(e.target); }, true);
  document.addEventListener('error', (e) => { if (e.target && e.target.tagName) SD.add(e.target); }, true);
  let loadAt = 0, dclAt = 0; const t00 = Date.now();
  const PBC = new WeakMap(), PBO = new WeakSet();
  let pbMo = null;
  const pbWatch = (el) => { if (PBO.has(el)) return; PBO.add(el); try { if (!pbMo) pbMo = new (G.MutationObserver)((ms) => { for (const m of ms) if (m.target && m.target.nodeType === 1) PBC.set(m.target, Date.now()); }); pbMo.observe(el, { attributes: true, attributeFilter: ['class', 'style', 'aria-hidden'] }); if (el.parentElement) pbMo.observe(el.parentElement, { attributes: true, attributeFilter: ['class', 'style', 'aria-hidden'] }); } catch (e) {} };
  G.addEventListener('load', () => { loadAt = Date.now(); });
  document.addEventListener('DOMContentLoaded', () => { dclAt = Date.now(); });
  const snap = () => { if (/^about:/.test(location.href)) return;
    const now = Date.now(), L = [], sec = (t) => ((now - t) / 1000).toFixed(1) + ' s';
    L.push('@ ' + shortU(location.href));
    L.push('= readyState ' + document.readyState + (dclAt ? ', DOMContentLoaded after ' + ((dclAt - t00) / 1000).toFixed(1) + ' s' : ', DOMContentLoaded not fired') + (loadAt ? ', load after ' + ((loadAt - t00) / 1000).toFixed(1) + ' s' : ', load not fired'));
    L.push('# Requests still running (' + PR.size + ')');
    for (const r of [...PR.values()].sort((a, b) => a.t - b.t).slice(0, 25)) L.push('- ' + r.k + ' ' + r.m + ' ' + shortU(r.u) + ' - waiting ' + sec(r.t));
    let ns = 0; const sl = [];
    for (const sc of document.querySelectorAll('script[src]')) { if (SD.has(sc) || !sc.isConnected) continue; const ty = (sc.getAttribute('type') || '').toLowerCase(); if (ty && !/javascript|module|ecmascript/.test(ty)) continue; ns++; if (sl.length < 15) sl.push('- script ' + shortU(sc.src)); }
    L.push('# Scripts without load or error event (' + ns + ')'); L.push(...sl);
    const fr = document.querySelectorAll('iframe');
    L.push('# Frames (' + fr.length + '): sent ' + FR.sent + ', delivered ' + FR.deliv + ', received ' + FR.recv + ', dropped ' + FR.dropped + ', engine switches ' + FR.switches);
    let fi = 0;
    for (const f of fr) { if (fi++ >= 15) break; const sv = f.getAttribute('srcdoc') !== null ? 'srcdoc (' + f.getAttribute('srcdoc').length + ' chars)' : shortU(f.getAttribute('src') || '(no src)'); const fsi = frSlotOf(f), FS = fsi >= 0 ? FR.S[fsi] : null; const st = FS ? ('RUNNING in engine ' + (fsi + 1) + ', ' + (FS.loaded ? (FS.failed ? 'load failed' : 'loaded ' + sec(FS.t0) + ' ago') : 'still loading ' + sec(FS.t0))) : frRunnable(f) ? 'not running (two frames run scripts at a time)' : 'empty'; const q = f.__fq ? f.__fq.length : 0; const cs = f.isConnected ? getComputedStyle(f) : null; const vis = cs && (cs.display === 'none' || cs.visibility === 'hidden' || f.offsetWidth < 2) ? 'hidden' : 'visible'; L.push('- ' + elName(f) + ' ' + sv + ' - ' + st + ', ' + vis + (q ? ', ' + q + ' messages waiting for it' : '')); }
    const tl = [...PT.values()];
    L.push('# Timers (' + tl.length + ' of 1 s or longer, plus all intervals)');
    for (const t of tl.sort((a, b) => a.t - b.t).slice(0, 15)) L.push('- ' + t.k + ' ' + t.ms + ' ms, function ' + t.n + ', set ' + sec(t.t) + ' ago');
    let nt = 0; const trl = [];
    for (const [e, m] of TRN) for (const [p, t] of m) { if (!e.isConnected) continue; nt++; if (trl.length < 15) trl.push('- ' + elName(e) + ' ' + p + ', started ' + sec(t) + ' ago'); }
    L.push('# Transitions without transitionend (' + nt + ')'); L.push(...trl);
    let na = 0; const anl = [];
    for (const [e, m] of ANM) for (const [p, t] of m) { if (!e.isConnected) continue; na++; if (anl.length < 15) anl.push('- ' + elName(e) + ' ' + p + ', started ' + sec(t) + ' ago'); }
    L.push('# Animations running (' + na + '; endless ones such as spinners are normal)'); L.push(...anl);
    const pl = [...PP.values()];
    L.push('# Browser promises not settled (' + pl.length + ')');
    for (const p of pl.sort((a, b) => a.t - b.t).slice(0, 20)) L.push('- ' + p.l + ', waiting ' + sec(p.t));
    const ol = OBS.filter((o) => (o.n === 'IntersectionObserver' || o.n === 'ResizeObserver') && o.obs > 0 && o.calls === 0 && now - o.t > 1000);
    L.push('# Intersection and resize observers that never called back (' + ol.length + ' of ' + OBS.length + ' observers)');
    for (const o of ol.slice(0, 15)) L.push('- ' + o.n + ' (callback ' + o.f + '), observing ' + o.obs + ' target' + (o.obs === 1 ? '' : 's') + ', created ' + sec(o.t) + ' ago');
    if (G.__ic && G.__ic.size) { L.push('# Idle callbacks not run (' + G.__ic.size + ')'); for (const t of [...G.__ic.values()].slice(0, 5)) L.push('- requestIdleCallback, waiting ' + sec(t)); }
    const evs = [...EV.entries()].filter(([t]) => !EVF.has(t));
    const evA = evs.filter(([t]) => !USER_EV.test(t)), evB = evs.filter(([t]) => USER_EV.test(t));
    L.push('# Events the page listens for that never came (' + evA.length + ' + ' + evB.length + ' user or rare events)');
    for (const [t, e] of evA.slice(0, 25)) L.push('- ' + t + ' on ' + e.w.join(', ') + (e.n > e.w.length ? ' (' + e.n + ' listeners)' : '') + ', listening for ' + sec(e.t));
    if (evB.length) L.push('- user or rare events not yet seen: ' + evB.map(([t]) => t).slice(0, 40).join(' '));
    L.push('# What is under the mouse (why clicks may be blocked)');
    try {
      const vw = innerWidth, vh = innerHeight, pts = [[vw / 2, vh / 2], [vw / 2, vh / 3], [vw / 3, vh / 2], [vw * 2 / 3, vh * 2 / 3]];
      const hits = new Set();
      for (const [x, y] of pts) {
        const el = document.elementFromPoint(x, y); if (!el) { L.push('- at ' + (x | 0) + ',' + (y | 0) + ': nothing'); continue; }
        for (let a = el; a && a.nodeType === 1 && a !== document.body && a !== document.documentElement; a = a.parentElement) hits.add(a);
        const cs = getComputedStyle(el); let why = [];
        for (let a = el; a && a.nodeType === 1; a = a.parentElement) { if (a.hasAttribute('inert')) why.push('inert on ' + elName(a)); if (a.getAttribute('aria-busy') === 'true') why.push('aria-busy on ' + elName(a)); if (a.getAttribute('aria-disabled') === 'true') why.push('aria-disabled on ' + elName(a)); const ac = getComputedStyle(a); if (ac.pointerEvents === 'none') why.push('pointer-events:none on ' + elName(a)); if (why.length > 4) break; }
        L.push('- at ' + (x | 0) + ',' + (y | 0) + ': ' + elName(el) + ' (position ' + cs.position + ', z-index ' + cs.zIndex + ', opacity ' + cs.opacity + ')' + (why.length ? ' - ' + why.slice(0, 4).join('; ') : ''));
      }
      let cov = 0; for (const el of hits) { if (cov >= 5) break; const cs = getComputedStyle(el); if (cs.position !== 'fixed' && cs.position !== 'absolute') continue; const r = el.getBoundingClientRect(); if (r.width * r.height < vw * vh * 0.5 || cs.display === 'none' || cs.visibility === 'hidden' || cs.pointerEvents === 'none') continue; cov++; L.push('- large overlay: ' + elName(el) + ' ' + (r.width | 0) + 'x' + (r.height | 0) + ', opacity ' + cs.opacity + ', z-index ' + cs.zIndex); }
      const ae = document.activeElement; L.push('- focused element: ' + (ae ? elName(ae) : 'none') + ', document.hasFocus() ' + document.hasFocus());
    } catch (e) { L.push('- could not check: ' + e.message); }
    const pbs = []; try { for (const el of document.querySelectorAll('[role=progressbar],progress,[class*="progress"]')) { if (pbs.length >= 6) break; if (el.parentElement && el.parentElement.closest && el.parentElement.closest('[role=progressbar]')) continue; pbs.push(el); } } catch (e) {}
    L.push('# Loading indicators (' + pbs.length + ')');
    for (const el of pbs) { pbWatch(el); const cs = getComputedStyle(el); const r = el.getBoundingClientRect(); let hid = ''; for (let a = el; a && a.nodeType === 1; a = a.parentElement) { const ac = getComputedStyle(a); if (ac.display === 'none' || ac.visibility === 'hidden' || ac.opacity === '0') { hid = ' (hidden by ' + elName(a) + ')'; break; } } L.push('- ' + elName(el) + ' role=' + (el.getAttribute('role') || '-') + ' aria-hidden=' + (el.getAttribute('aria-hidden') || '-') + ', display ' + cs.display + ', visibility ' + cs.visibility + ', opacity ' + cs.opacity + ', size ' + (r.width | 0) + 'x' + (r.height | 0) + hid + (PBC.has(el) ? ', classes last changed ' + sec(PBC.get(el)) + ' ago' : ', classes not changed since first seen') + ', classes "' + String(el.getAttribute('class') || '').slice(0, 120) + '"'); }
    let ni = 0; const il = [];
    for (const im of document.images) { if (im.complete) continue; ni++; if (il.length < 10) il.push('- img ' + shortU(im.currentSrc || im.src)); }
    L.push('# Images still loading (' + ni + ')'); L.push(...il);
    try { N.pendingSet(L.join('\n')); } catch (e) {}
  };
  osi(snap, 1000);
  G.addEventListener('pagehide', snap);
}
{
  const SBXF = typeof N.sandbox === 'function' ? N.sandbox() : 0;
  if (SBXF) {
    const blocked = (what, flag) => { try { N.log("Blocked " + what + " because the frame is sandboxed and the '" + flag + "' permission is not set.", 3); } catch (e) {} };
    if (!(SBXF & 32)) { G.alert = () => blocked('alert()', 'allow-modals'); G.confirm = () => { blocked('confirm()', 'allow-modals'); return false; }; G.prompt = () => { blocked('prompt()', 'allow-modals'); return null; }; G.print = () => blocked('print()', 'allow-modals'); }
    if (!(SBXF & 8)) G.open = () => { blocked('window.open()', 'allow-popups'); return null; };
    if (!(SBXF & 2)) {
      const se = (w) => () => { throw new DOMException("Failed to read the '" + w + "' property from 'Window': The document is sandboxed and lacks the 'allow-same-origin' flag.", 'SecurityError'); };
      try { Object.defineProperty(P.Document, 'cookie', { configurable: true, get() { throw new DOMException("Failed to read the 'cookie' property from 'Document': The document is sandboxed and lacks the 'allow-same-origin' flag.", 'SecurityError'); }, set() { throw new DOMException("Failed to set the 'cookie' property on 'Document': The document is sandboxed and lacks the 'allow-same-origin' flag.", 'SecurityError'); } }); } catch (e) {}
      for (const w of ['localStorage', 'sessionStorage', 'indexedDB']) { try { Object.defineProperty(G, w, { configurable: true, get: se(w) }); } catch (e) {} }
    }
    if (!(SBXF & 4)) { const FP = G.HTMLFormElement && G.HTMLFormElement.prototype; if (FP) { FP.submit = function () { blocked('form submission', 'allow-forms'); }; FP.requestSubmit = function () { blocked('form submission', 'allow-forms'); }; } }
  }
}
{
  const defA = (n, v) => { if (G[n] !== undefined) return; try { Object.defineProperty(G, n, { value: v, writable: true, configurable: true, enumerable: false }); } catch (e) { G[n] = v; } };
  const evc = (n, f) => { const C = class extends Event { constructor(t, i) { super(t, i); i = i || {}; for (const k of f) this[k] = i[k] !== undefined ? i[k] : null; } }; Object.defineProperty(C, 'name', { value: n }); defA(n, C); };
  evc('DeviceMotionEvent', ['acceleration', 'accelerationIncludingGravity', 'rotationRate', 'interval']);
  evc('DeviceOrientationEvent', ['alpha', 'beta', 'gamma', 'absolute']);
  evc('GamepadEvent', ['gamepad']);
  evc('MediaEncryptedEvent', ['initDataType', 'initData']);
  evc('TaskPriorityChangeEvent', ['previousPriority']);
  defA('Gamepad', class Gamepad { constructor() { throw new TypeError('Illegal constructor'); } });
  defA('Report', class Report { constructor() { throw new TypeError('Illegal constructor'); } toJSON() { return { type: this.type, url: this.url, body: this.body }; } });
  if (G.Animation) { defA('CSSAnimation', class CSSAnimation extends G.Animation { get animationName() { return this._name || ''; } }); defA('CSSTransition', class CSSTransition extends G.Animation { get transitionProperty() { return this._prop || ''; } }); }
  const ro = (n) => { const C = class { constructor() { throw new TypeError('Illegal constructor'); } }; Object.defineProperty(C, 'name', { value: n }); defA(n, C); return C; };
  const PA = ro('PluginArray'), MA = ro('MimeTypeArray'); ro('Plugin'); ro('MimeType');
  PA.prototype.item = function () { return null; }; PA.prototype.namedItem = function () { return null; }; PA.prototype.refresh = function () {}; Object.defineProperty(PA.prototype, 'length', { get() { return 0; } }); PA.prototype[Symbol.iterator] = function* () {};
  MA.prototype.item = function () { return null; }; MA.prototype.namedItem = function () { return null; }; Object.defineProperty(MA.prototype, 'length', { get() { return 0; } }); MA.prototype[Symbol.iterator] = function* () {};
  try { const pl = Object.create(PA.prototype), mt = Object.create(MA.prototype); Object.defineProperty(navigator, 'plugins', { get: () => pl, configurable: true }); Object.defineProperty(navigator, 'mimeTypes', { get: () => mt, configurable: true }); } catch (e) {}
  const FFS = ro('FontFaceSet');
  try { const f = document.fonts; if (f && Object.getPrototypeOf(f) === Object.prototype) { for (const k of ['add', 'delete', 'clear', 'has', 'check', 'load', 'forEach', 'entries', 'keys', 'values']) if (typeof f[k] === 'function') FFS.prototype[k] = function (...a) { return f[k].apply(f, a); }; Object.setPrototypeOf(f, FFS.prototype); } } catch (e) {}
  const VT = class ViewTransition { constructor() { throw new TypeError('Illegal constructor'); } skipTransition() { this._skip = true; } get types() { return this._types || new Set(); } };
  defA('ViewTransition', VT);
  try { val(P.Document, 'startViewTransition', function (arg) { const cb = typeof arg === 'function' ? arg : arg && typeof arg.update === 'function' ? arg.update : null; const t = Object.create(VT.prototype); t._types = new Set(arg && arg.types ? arg.types : []); let r; try { r = cb ? cb() : undefined; } catch (e) { r = Promise.reject(e); } const done = Promise.resolve(r); t.updateCallbackDone = done; t.ready = done.then(() => undefined); t.finished = done.then(() => undefined, () => undefined); t.ready.catch(() => {}); return t; }); } catch (e) {}
  class ImageBitmapRenderingContext { constructor(cv) { this.canvas = cv; } transferFromImageBitmap(b) { const c = this.canvas; if (!b) { const x = c.__bctx; if (x) x.clearRect(0, 0, c.width, c.height); return; } if (!c.__bctx) c.__bctx = new CanvasRenderingContext2D(c); N.cvInit(c); c.__bctx.clearRect(0, 0, c.width, c.height); c.__bctx.drawImage(b, 0, 0, c.width, c.height); } }
  defA('ImageBitmapRenderingContext', ImageBitmapRenderingContext);
  try { const ogc2 = Object.getOwnPropertyDescriptor(T.canvas, 'getContext'); val(T.canvas, 'getContext', function (t, a) { if (String(t) === 'bitmaprenderer') { if (this.__ctx && !(this.__ctx instanceof ImageBitmapRenderingContext)) return null; if (!this.__ctx) this.__ctx = new ImageBitmapRenderingContext(this); return this.__ctx; } return ogc2.value.call(this, t, a); }); } catch (e) {}
}
})();
