use std::collections::BTreeMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Instant;

const MB: usize = 1024 * 1024;

fn meminfo() -> BTreeMap<String, u64> {
    let mut m = BTreeMap::new();
    if let Ok(s) = std::fs::read_to_string("/proc/meminfo") {
        for line in s.lines() {
            let mut it = line.split_whitespace();
            if let (Some(k), Some(v)) = (it.next(), it.next()) {
                if let Ok(n) = v.parse::<u64>() {
                    m.insert(k.trim_end_matches(':').to_string(), n);
                }
            }
        }
    }
    m
}

fn bar(used: u64, total: u64, width: usize) -> String {
    let f = if total == 0 { 0 } else { ((used as u128 * width as u128) / total as u128) as usize };
    let f = f.min(width);
    format!("[{}{}]", "#".repeat(f), ".".repeat(width - f))
}

fn human(kb: u64) -> String {
    if kb >= 1024 * 1024 {
        format!("{:.1} GB", kb as f64 / 1048576.0)
    } else if kb >= 1024 {
        format!("{} MB", kb / 1024)
    } else {
        format!("{} KB", kb)
    }
}

fn info() {
    let m = meminfo();
    let total = *m.get("MemTotal").unwrap_or(&0);
    let free = *m.get("MemAvailable").or(m.get("MemFree")).unwrap_or(&0);
    let used = total.saturating_sub(free);
    if total == 0 {
        println!("nvx-mem: /proc/meminfo is not readable");
        return;
    }
    println!("memory    {} of {} in use {}", human(used), human(total), bar(used, total, 30));
    println!("free      {}", human(free));
    if let Some(h) = m.get("KernelHeap") {
        println!("kernel    {} heap", human(*h));
    }
    if let Ok(s) = std::fs::read_to_string("/proc/self/status") {
        for l in s.lines() {
            if l.starts_with("VmRSS") || l.starts_with("VmSize") {
                println!("this app  {}", l.replace('\t', " "));
            }
        }
    }
}

fn pattern(i: usize, pass: u32, seed: u64) -> u64 {
    let x = (i as u64).wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ seed;
    match pass {
        0 => 0,
        1 => !0,
        2 => 0xAAAA_AAAA_AAAA_AAAA,
        3 => 0x5555_5555_5555_5555,
        4 => 1u64.rotate_left((i % 64) as u32),
        5 => x,
        _ => !x,
    }
}

fn test(mb: usize, threads: usize) -> bool {
    println!("testing   {} MB with {} threads, 7 patterns", mb, threads);
    let errors = Arc::new(AtomicU64::new(0));
    let bytes = Arc::new(AtomicU64::new(0));
    let t0 = Instant::now();
    let per = (mb / threads).max(1);
    let mut hs = Vec::new();
    for t in 0..threads {
        let errors = errors.clone();
        let bytes = bytes.clone();
        hs.push(thread::spawn(move || {
            let n = per * MB / 8;
            let mut v: Vec<u64> = Vec::new();
            if v.try_reserve_exact(n).is_err() {
                return false;
            }
            v.resize(n, 0);
            let seed = 0x4E56_584D_0000_0000u64 | t as u64;
            for pass in 0..7u32 {
                for (i, w) in v.iter_mut().enumerate() {
                    *w = pattern(i, pass, seed);
                }
                let mut bad = 0u64;
                for (i, w) in v.iter().enumerate() {
                    if *w != pattern(i, pass, seed) {
                        bad += 1;
                    }
                }
                errors.fetch_add(bad, Ordering::Relaxed);
                bytes.fetch_add((n * 16) as u64, Ordering::Relaxed);
            }
            true
        }));
    }
    let mut ok = true;
    for h in hs {
        if !h.join().unwrap_or(false) {
            ok = false;
        }
    }
    let ms = t0.elapsed().as_millis().max(1) as u64;
    let e = errors.load(Ordering::Relaxed);
    let mbs = bytes.load(Ordering::Relaxed) / MB as u64 * 1000 / ms;
    if !ok {
        println!("result    not enough free memory for {} MB - try a smaller size", mb);
        return false;
    }
    println!("speed     {} MB/s written and checked in {} ms", mbs, ms);
    if e == 0 {
        println!("result    ok, no errors");
    } else {
        println!("result    {} wrong words found - the memory may be faulty", e);
    }
    e == 0
}

fn bench() {
    let n = 64 * MB;
    let a = vec![0x5Au8; n];
    let mut b = vec![0u8; n];
    let t0 = Instant::now();
    let rounds = 8;
    for _ in 0..rounds {
        b.copy_from_slice(&a);
        std::hint::black_box(&b);
    }
    let ms = t0.elapsed().as_millis().max(1) as usize;
    println!("copy      {} MB/s ({} x 64 MB)", rounds * 64 * 1000 / ms, rounds);
    let t1 = Instant::now();
    let mut s = 0u64;
    for _ in 0..rounds {
        s = s.wrapping_add(a.iter().map(|&x| x as u64).sum::<u64>());
    }
    std::hint::black_box(s);
    let ms = t1.elapsed().as_millis().max(1) as usize;
    println!("read      {} MB/s", rounds * 64 * 1000 / ms);
    let t2 = Instant::now();
    let mut boxes = Vec::with_capacity(100_000);
    for i in 0..100_000usize {
        boxes.push(vec![i as u8; 32 + (i % 200)]);
    }
    drop(boxes);
    println!("allocate  100000 small blocks in {} ms", t2.elapsed().as_millis());
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let cmd = args.get(1).map(|s| s.as_str()).unwrap_or("info");
    let cpus = thread::available_parallelism().map(|n| n.get()).unwrap_or(1).min(8);
    match cmd {
        "info" => info(),
        "test" => {
            let mb: usize = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(64).clamp(1, 65536);
            info();
            if !test(mb, cpus.min(mb)) {
                std::process::exit(1);
            }
        }
        "bench" => bench(),
        _ => {
            println!("usage: nvx-mem [info | test [MB] | bench]");
            std::process::exit(2);
        }
    }
}
