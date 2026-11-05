#![no_std]
extern crate alloc;
use alloc::vec::Vec;
use core::marker::PhantomData;
use core::ops::{Deref, DerefMut};

pub unsafe trait Array {
    type Item;
}
unsafe impl<T, const N: usize> Array for [T; N] {
    type Item = T;
}

pub struct SmallVec<A: Array> {
    v: Vec<A::Item>,
    _p: PhantomData<A>,
}

impl<A: Array> SmallVec<A> {
    pub const fn new() -> Self {
        SmallVec { v: Vec::new(), _p: PhantomData }
    }
    pub fn with_capacity(n: usize) -> Self {
        SmallVec { v: Vec::with_capacity(n), _p: PhantomData }
    }
    pub fn from_vec(v: Vec<A::Item>) -> Self {
        SmallVec { v, _p: PhantomData }
    }
    pub fn push(&mut self, x: A::Item) {
        self.v.push(x)
    }
    pub fn pop(&mut self) -> Option<A::Item> {
        self.v.pop()
    }
    pub fn insert(&mut self, i: usize, x: A::Item) {
        self.v.insert(i, x)
    }
    pub fn remove(&mut self, i: usize) -> A::Item {
        self.v.remove(i)
    }
    pub fn clear(&mut self) {
        self.v.clear()
    }
    pub fn truncate(&mut self, n: usize) {
        self.v.truncate(n)
    }
    pub fn reserve(&mut self, n: usize) {
        self.v.reserve(n)
    }
    pub fn spilled(&self) -> bool {
        false
    }
    pub fn as_slice(&self) -> &[A::Item] {
        &self.v
    }
    pub fn as_mut_slice(&mut self) -> &mut [A::Item] {
        &mut self.v
    }
    pub fn into_vec(self) -> Vec<A::Item> {
        self.v
    }
    pub fn extend_from_slice(&mut self, s: &[A::Item])
    where
        A::Item: Clone,
    {
        self.v.extend_from_slice(s)
    }
    pub fn resize(&mut self, n: usize, x: A::Item)
    where
        A::Item: Clone,
    {
        self.v.resize(n, x)
    }
    pub fn dedup(&mut self)
    where
        A::Item: PartialEq,
    {
        self.v.dedup()
    }
    pub fn retain<F: FnMut(&mut A::Item) -> bool>(&mut self, f: F) {
        self.v.retain_mut(f)
    }
}

impl<A: Array> Default for SmallVec<A> {
    fn default() -> Self {
        Self::new()
    }
}
impl<A: Array> Clone for SmallVec<A>
where
    A::Item: Clone,
{
    fn clone(&self) -> Self {
        SmallVec { v: self.v.clone(), _p: PhantomData }
    }
}
impl<A: Array> core::fmt::Debug for SmallVec<A>
where
    A::Item: core::fmt::Debug,
{
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        self.v.fmt(f)
    }
}
impl<A: Array> PartialEq for SmallVec<A>
where
    A::Item: PartialEq,
{
    fn eq(&self, o: &Self) -> bool {
        self.v == o.v
    }
}
impl<A: Array> Eq for SmallVec<A> where A::Item: Eq {}
impl<A: Array> Deref for SmallVec<A> {
    type Target = [A::Item];
    fn deref(&self) -> &[A::Item] {
        &self.v
    }
}
impl<A: Array> DerefMut for SmallVec<A> {
    fn deref_mut(&mut self) -> &mut [A::Item] {
        &mut self.v
    }
}
impl<A: Array> Extend<A::Item> for SmallVec<A> {
    fn extend<I: IntoIterator<Item = A::Item>>(&mut self, i: I) {
        self.v.extend(i)
    }
}
impl<A: Array> FromIterator<A::Item> for SmallVec<A> {
    fn from_iter<I: IntoIterator<Item = A::Item>>(i: I) -> Self {
        SmallVec { v: i.into_iter().collect(), _p: PhantomData }
    }
}
impl<A: Array> IntoIterator for SmallVec<A> {
    type Item = A::Item;
    type IntoIter = alloc::vec::IntoIter<A::Item>;
    fn into_iter(self) -> Self::IntoIter {
        self.v.into_iter()
    }
}
impl<'a, A: Array> IntoIterator for &'a SmallVec<A> {
    type Item = &'a A::Item;
    type IntoIter = core::slice::Iter<'a, A::Item>;
    fn into_iter(self) -> Self::IntoIter {
        self.v.iter()
    }
}
impl<'a, A: Array> IntoIterator for &'a mut SmallVec<A> {
    type Item = &'a mut A::Item;
    type IntoIter = core::slice::IterMut<'a, A::Item>;
    fn into_iter(self) -> Self::IntoIter {
        self.v.iter_mut()
    }
}
impl<A: Array> From<Vec<A::Item>> for SmallVec<A> {
    fn from(v: Vec<A::Item>) -> Self {
        Self::from_vec(v)
    }
}

#[doc(hidden)]
pub use alloc::vec as __vec;

#[macro_export]
macro_rules! smallvec {
    ($($x:expr),* $(,)?) => {
        $crate::SmallVec::from_vec($crate::__vec![$($x),*])
    };
}
