use std::collections::BTreeMap;
use std::io::Write;
use std::sync::mpsc;
use std::thread;
use std::time::Instant;

fn primes(lo: u64, hi: u64) -> u64 {
    (lo..hi).filter(|&n| n > 1 && (2..).take_while(|d| d * d <= n).all(|d| n % d != 0)).count() as u64
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    println!("Hello from Rust on NoovexOS");
    println!("rustc     {}", env!("NVX_RUSTC_VERSION"));
    if let Ok(v) = std::fs::read_to_string("/proc/version") {
        println!("kernel    {}", v.trim());
    }
    let cpus = thread::available_parallelism().map(|n| n.get()).unwrap_or(1);
    println!("cpus      {}", cpus);
    let n: u64 = args.get(1).and_then(|s| s.parse().ok()).unwrap_or(200_000);
    let t0 = Instant::now();
    let (tx, rx) = mpsc::channel();
    let k = 4u64;
    for i in 0..k {
        let tx = tx.clone();
        thread::spawn(move || {
            let c = primes(i * n / k, (i + 1) * n / k);
            tx.send((i, c)).unwrap();
        });
    }
    drop(tx);
    let mut parts = BTreeMap::new();
    for (i, c) in rx {
        parts.insert(i, c);
    }
    let total: u64 = parts.values().sum();
    println!("threads   {} found {} primes below {} in {} ms", k, total, n, t0.elapsed().as_millis());
    let path = std::env::temp_dir().join("rustinfo.txt");
    let ok = std::fs::File::create(&path).and_then(|mut f| writeln!(f, "{}", total)).is_ok() && std::fs::read_to_string(&path).map(|s| s.trim() == total.to_string()).unwrap_or(false);
    let _ = std::fs::remove_file(&path);
    println!("files     {}", if ok { "write and read back ok" } else { "failed" });
}
