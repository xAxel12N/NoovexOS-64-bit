#include "posix.h"
int main(void){
    printf("== math ==\n");
    printf("hypot3(1,2,2)=%.1f sigmoid(0)=%.2f smoothstep(0,1,.5)=%.3f\n",hypot3(1,2,2),sigmoid(0),smoothstep(0,1,0.5));
    printf("binomial(10,3)=%llu factorial(6)=%llu ipow(2,10)=%lld\n",binomial(10,3),factorial(6),ipow(2,10));
    printf("wrap180(270)=%.0f roundto(3.14159,2)=%.2f\n",wrap180(270),roundto(3.14159,2));
    printf("== crypto ==\n");
    char hex[80]; sha256_hex("hello",5,hex); printf("sha256(hello)=%s\n",hex);
    md5_hex("hello",5,hex); printf("md5(hello)=%s\n",hex);
    printf("crc32c(hi)=%08x murmur3(hi)=%08x siphash key0=%016llx\n",crc32c(0,"hi",2),murmur3_32("hi",2,0),(unsigned long long)siphash24("hi",2,(const u8*)"0123456789abcdef"));
    printf("is_prime(1000003)=%d next_prime(100)=%llu\n",is_prime_u32(1000003),(unsigned long long)next_prime(100));
    char rot[]="Hello"; rot13(rot); printf("rot13(Hello)=%s\n",rot);
    printf("== files ==\n");
    write_str("/tmp/nx.txt","one\ntwo\nthree\n"); printf("wrote /tmp/nx.txt, lines=%ld words=%d size=%ld\n",count_lines("/tmp/nx.txt"),wc_words("/tmp/nx.txt"),file_size("/tmp/nx.txt"));
    char ln[64]; read_last_line("/tmp/nx.txt",ln,64); printf("last line=%s crc32=%08x\n",ln,file_crc32("/tmp/nx.txt"));
    printf("basename(/a/b/c.txt)=%s ext=%s\n",path_base("/a/b/c.txt"),path_ext("/a/b/c.txt"));
    printf("== utils ==\n");
    printf("next_pow2(1000)=%u popcount(255)=%d clz(1)=%d\n",next_pow2(1000),popcount32(255),clz32(1));
    char rom[16]; int_to_roman(2024,rom); printf("2024 roman=%s back=%d\n",rom,roman_to_int(rom));
    char com[32]; fmt_commas(com,1234567); char by[24]; fmt_bytes(by,1536000); printf("commas=%s bytes(1536000)=%s\n",com,by);
    printf("day_of_week(2026,9,29)=%s leap(2024)=%d levenshtein(kitten,sitting)=%d\n",weekday_name(day_of_week(2026,9,29)),is_leap_year(2024),str_levenshtein("kitten","sitting"));
    printf("valid_ipv4(192.168.1.1)=%d valid_email(a@b.co)=%d luhn(4532015112830366)=%d\n",valid_ipv4("192.168.1.1"),valid_email("a@b.co"),luhn_check("4532015112830366"));
    printf("hsv->rgb(120,255,255)=%06x lerp_color=%06x\n",hsv_to_rgb(120,255,255),rgb_lerp(0xFF0000,0x0000FF,128));
    remove_file("/tmp/nx.txt");
    return 0;
}
