#![no_std]
extern crate alloc;

use alloc::vec::Vec;
use core::alloc::{GlobalAlloc, Layout as AllocLayout};
use core::ffi::c_void;
use taffy::prelude::*;
use taffy::{
    compute_cached_layout, compute_grid_layout, compute_leaf_layout, compute_root_layout, Cache, CacheTree, GridPlacement, LayoutInput, LayoutOutput,
};

extern "C" {
    fn nvx_rs_alloc(size: usize, align: usize) -> *mut u8;
    fn nvx_rs_free(p: *mut u8);
    fn nvx_rs_abort() -> !;
    fn nvx_grid_measure(ctx: *mut c_void, idx: i32, kw: f32, kh: f32, aw: f32, ah: f32, out: *mut f32);
}

struct NvAlloc;
unsafe impl GlobalAlloc for NvAlloc {
    unsafe fn alloc(&self, l: AllocLayout) -> *mut u8 {
        nvx_rs_alloc(l.size(), l.align())
    }
    unsafe fn dealloc(&self, p: *mut u8, _l: AllocLayout) {
        nvx_rs_free(p)
    }
}
#[global_allocator]
static GA: NvAlloc = NvAlloc;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    unsafe { nvx_rs_abort() }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct NvTrack {
    pub k: u8,
    pub mink: u8,
    pub p0: u8,
    pub p1: u8,
    pub v: f32,
    pub minv: f32,
}

#[repr(C)]
pub struct NvGrid {
    pub ncols: i32,
    pub nrows: i32,
    pub cols: *const NvTrack,
    pub rows: *const NvTrack,
    pub autorow: NvTrack,
    pub colgap: f32,
    pub rowgap: f32,
    pub w: f32,
    pub h: f32,
    pub jc: i32,
    pub ac: i32,
    pub ji: i32,
    pub ai: i32,
    pub flow: i32,
    pub minh: f32,
    pub maxh: f32,
}

#[repr(C)]
pub struct NvItem {
    pub cs: i32,
    pub ce: i32,
    pub rs: i32,
    pub re: i32,
    pub w: f32,
    pub h: f32,
    pub m: [f32; 4],
    pub mauto: i32,
    pub js: i32,
    pub as_: i32,
    pub replaced: i32,
    pub ox: f32,
    pub oy: f32,
    pub ow: f32,
    pub oh: f32,
}

const GT_PX: u8 = 0;
const GT_PCT: u8 = 1;
const GT_FR: u8 = 2;
const GT_MINC: u8 = 4;
const GT_MAXC: u8 = 5;

fn min_fn(k: u8, v: f32) -> MinTrackSizingFunction {
    match k {
        GT_PX => length(v),
        GT_PCT => percent(v / 100.0),
        GT_MINC => min_content(),
        GT_MAXC => max_content(),
        _ => auto(),
    }
}
fn max_fn(k: u8, v: f32) -> MaxTrackSizingFunction {
    match k {
        GT_PX => length(v),
        GT_PCT => percent(v / 100.0),
        GT_FR => fr(v),
        GT_MINC => min_content(),
        GT_MAXC => max_content(),
        _ => auto(),
    }
}
fn track(t: &NvTrack) -> TrackSizingFunction {
    let mk = if t.mink == GT_FR { 3 } else { t.mink };
    let (mink, minv) = if (t.k == GT_MINC || t.k == GT_MAXC) && mk == 3 { (t.k, 0.0) } else { (mk, t.minv) };
    minmax(min_fn(mink, minv), max_fn(t.k, t.v))
}

const AL_START: i32 = 1;
const AL_END: i32 = 2;
const AL_CENTER: i32 = 3;
const AL_STRETCH: i32 = 4;
const AL_BASELINE: i32 = 5;
const AL_BETWEEN: i32 = 6;
const AL_AROUND: i32 = 7;
const AL_EVENLY: i32 = 8;

fn al_items(a: i32) -> AlignItems {
    match a {
        AL_START => AlignItems::START,
        AL_END => AlignItems::END,
        AL_CENTER => AlignItems::CENTER,
        AL_STRETCH => AlignItems::STRETCH,
        AL_BASELINE => AlignItems::BASELINE,
        _ => AlignItems::NORMAL,
    }
}
fn al_self(a: i32) -> Option<AlignSelf> {
    if a == 0 {
        None
    } else {
        Some(al_items(a))
    }
}
fn al_content(a: i32) -> AlignContent {
    match a {
        AL_START => AlignContent::START,
        AL_END => AlignContent::END,
        AL_CENTER => AlignContent::CENTER,
        AL_STRETCH => AlignContent::STRETCH,
        AL_BETWEEN => AlignContent::SPACE_BETWEEN,
        AL_AROUND => AlignContent::SPACE_AROUND,
        AL_EVENLY => AlignContent::SPACE_EVENLY,
        _ => AlignContent::NORMAL,
    }
}
fn place(v: i32) -> GridPlacement {
    if v <= -1000 {
        let s = (-1000 - v).clamp(1, 1000) as u16;
        GridPlacement::Span(s)
    } else if v == 0 {
        GridPlacement::Auto
    } else {
        GridPlacement::Line((v.clamp(-1000, 1000) as i16).into())
    }
}
fn lpa(v: f32, auto_: bool) -> LengthPercentageAuto {
    if auto_ {
        LengthPercentageAuto::auto()
    } else {
        LengthPercentageAuto::length(v)
    }
}

struct Node {
    style: Style,
    cache: Cache,
    layout: taffy::Layout,
    children: Vec<usize>,
}

struct Tree {
    nodes: Vec<Node>,
    ctx: *mut c_void,
}

struct ChildIter<'a>(core::slice::Iter<'a, usize>);
impl Iterator for ChildIter<'_> {
    type Item = NodeId;
    fn next(&mut self) -> Option<NodeId> {
        self.0.next().copied().map(NodeId::from)
    }
}

impl taffy::TraversePartialTree for Tree {
    type ChildIter<'a> = ChildIter<'a>;
    fn child_ids(&self, n: NodeId) -> Self::ChildIter<'_> {
        ChildIter(self.nodes[usize::from(n)].children.iter())
    }
    fn child_count(&self, n: NodeId) -> usize {
        self.nodes[usize::from(n)].children.len()
    }
    fn get_child_id(&self, n: NodeId, i: usize) -> NodeId {
        NodeId::from(self.nodes[usize::from(n)].children[i])
    }
}
impl taffy::TraverseTree for Tree {}

fn space(a: AvailableSpace) -> f32 {
    match a {
        AvailableSpace::Definite(v) => v,
        AvailableSpace::MinContent => -2.0,
        AvailableSpace::MaxContent => -3.0,
    }
}

impl taffy::LayoutPartialTree for Tree {
    type CustomIdent = alloc::string::String;
    type CoreContainerStyle<'a>
        = &'a Style
    where
        Self: 'a;
    fn get_core_container_style(&self, n: NodeId) -> Self::CoreContainerStyle<'_> {
        &self.nodes[usize::from(n)].style
    }
    fn set_unrounded_layout(&mut self, n: NodeId, l: &taffy::Layout) {
        self.nodes[usize::from(n)].layout = *l;
    }
    fn resolve_calc_value(&self, _v: *const (), _b: f32) -> f32 {
        0.0
    }
    fn compute_child_layout(&mut self, n: NodeId, inputs: LayoutInput) -> LayoutOutput {
        compute_cached_layout(self, n, inputs, |tree, n, inputs| {
            let i = usize::from(n);
            if i == 0 {
                return compute_grid_layout(tree, n, inputs);
            }
            let ctx = tree.ctx;
            let node = &tree.nodes[i];
            compute_leaf_layout(inputs, &node.style, |_v, _b| 0.0, |known, avail| {
                let mut out = [0f32; 2];
                unsafe {
                    nvx_grid_measure(ctx, (i - 1) as i32, known.width.unwrap_or(-1.0), known.height.unwrap_or(-1.0), space(avail.width), space(avail.height), out.as_mut_ptr());
                }
                Size { width: out[0].max(0.0), height: out[1].max(0.0) }
            })
        })
    }
}

impl taffy::LayoutContainingBlock for Tree {
    type OofItemStyle<'a>
        = &'a Style
    where
        Self: 'a;
    fn get_oof_item_style(&self, n: NodeId) -> Self::OofItemStyle<'_> {
        &self.nodes[usize::from(n)].style
    }
    fn clear_hoisted_children(&mut self, _n: NodeId) {}
    fn add_hoisted_children(&mut self, _n: NodeId, _h: &[NodeId]) {}
}

impl CacheTree for Tree {
    fn cache_get(&mut self, n: NodeId, inputs: &LayoutInput) -> Option<LayoutOutput> {
        self.nodes[usize::from(n)].cache.get(inputs)
    }
    fn cache_store(&mut self, n: NodeId, inputs: &LayoutInput, o: LayoutOutput) {
        self.nodes[usize::from(n)].cache.store(inputs, o)
    }
    fn cache_clear(&mut self, n: NodeId) {
        self.nodes[usize::from(n)].cache.clear();
    }
}

impl taffy::LayoutGridContainer for Tree {
    type GridContainerStyle<'a>
        = &'a Style
    where
        Self: 'a;
    type GridItemStyle<'a>
        = &'a Style
    where
        Self: 'a;
    fn get_grid_container_style(&self, n: NodeId) -> Self::GridContainerStyle<'_> {
        &self.nodes[usize::from(n)].style
    }
    fn get_grid_child_style(&self, n: NodeId) -> Self::GridItemStyle<'_> {
        &self.nodes[usize::from(n)].style
    }
}

fn node(style: Style) -> Node {
    Node { style, cache: Cache::new(), layout: taffy::Layout::with_order(0), children: Vec::new() }
}

#[no_mangle]
pub unsafe extern "C" fn nvx_taffy_grid(g: *const NvGrid, items: *mut NvItem, n: i32, ctx: *mut c_void, out_h: *mut f32) -> i32 {
    if g.is_null() || items.is_null() || n <= 0 || n > 4096 {
        return 0;
    }
    let g = &*g;
    let items = core::slice::from_raw_parts_mut(items, n as usize);
    let mut cs = Style::DEFAULT;
    cs.display = Display::Grid;
    let mut cols = Vec::new();
    for i in 0..g.ncols.max(0) as usize {
        cols.push(GridTemplateComponent::Single(track(&*g.cols.add(i))));
    }
    let mut rows = Vec::new();
    for i in 0..g.nrows.max(0) as usize {
        rows.push(GridTemplateComponent::Single(track(&*g.rows.add(i))));
    }
    cs.grid_template_columns = cols;
    cs.grid_template_rows = rows;
    cs.grid_auto_rows = alloc::vec![track(&g.autorow)];
    cs.grid_auto_flow = match g.flow & 3 {
        1 => GridAutoFlow::Column,
        2 => GridAutoFlow::RowDense,
        3 => GridAutoFlow::ColumnDense,
        _ => GridAutoFlow::Row,
    };
    cs.gap = Size { width: LengthPercentage::length(g.colgap.max(0.0)), height: LengthPercentage::length(g.rowgap.max(0.0)) };
    cs.size = Size { width: Dimension::length(g.w.max(0.0)), height: if g.h >= 0.0 { Dimension::length(g.h) } else { Dimension::auto() } };
    if g.minh >= 0.0 {
        cs.min_size = Size { width: LengthPercentageAuto::auto(), height: LengthPercentageAuto::length(g.minh) };
    }
    if g.maxh >= 0.0 {
        cs.max_size = Size { width: LengthPercentageAuto::auto(), height: LengthPercentageAuto::length(g.maxh) };
    }
    cs.justify_content = al_content(g.jc);
    cs.align_content = al_content(g.ac);
    cs.justify_items = al_items(g.ji);
    cs.align_items = al_items(g.ai);
    let mut t = Tree { nodes: Vec::with_capacity(n as usize + 1), ctx };
    t.nodes.push(node(cs));
    for (i, it) in items.iter().enumerate() {
        let mut s = Style::DEFAULT;
        s.display = Display::Block;
        s.item_is_replaced = it.replaced != 0;
        s.size = Size {
            width: if it.w >= 0.0 { Dimension::length(it.w) } else { Dimension::auto() },
            height: if it.h >= 0.0 { Dimension::length(it.h) } else { Dimension::auto() },
        };
        s.margin = Rect { top: lpa(it.m[0], it.mauto & 1 != 0), right: lpa(it.m[1], it.mauto & 2 != 0), bottom: lpa(it.m[2], it.mauto & 4 != 0), left: lpa(it.m[3], it.mauto & 8 != 0) };
        s.justify_self = al_self(it.js);
        s.align_self = al_self(it.as_);
        s.grid_column = Line { start: place(it.cs), end: place(it.ce) };
        s.grid_row = Line { start: place(it.rs), end: place(it.re) };
        t.nodes.push(node(s));
        t.nodes[0].children.push(i + 1);
    }
    let avail = Size { width: AvailableSpace::Definite(g.w.max(0.0)), height: if g.h >= 0.0 { AvailableSpace::Definite(g.h) } else { AvailableSpace::MaxContent } };
    compute_root_layout(&mut t, NodeId::from(0usize), avail);
    for (i, it) in items.iter_mut().enumerate() {
        let l = &t.nodes[i + 1].layout;
        it.ox = l.location.x;
        it.oy = l.location.y;
        it.ow = l.size.width;
        it.oh = l.size.height;
    }
    let h = t.nodes[0].layout.size.height;
    if !out_h.is_null() {
        *out_h = if h.is_finite() { h.max(0.0) } else { 0.0 };
    }
    1
}

#[no_mangle]
pub extern "C" fn nvx_rust_version() -> u32 {
    0x000f00
}
