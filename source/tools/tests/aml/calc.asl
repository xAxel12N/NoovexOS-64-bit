DefinitionBlock ("", "DSDT", 2, "NVX", "CALC", 1)
{
    Name (GINT, 0x1234)
    Name (GSTR, "Noovex")
    Name (GBUF, Buffer (8) { 1, 2, 3, 4, 5, 6, 7, 8 })
    Name (GPKG, Package () { 1, "two", Buffer () { 3 }, Package () { 4, 5 } })
    Name (BLVL, Package () { 80, 50, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 })
    Name (CNT0, 0)
    Mutex (MUT0, 0)
    Method (ADD3, 3) { Return (Add (Add (Arg0, Arg1), Arg2)) }
    Method (FACT, 1) { If (LLessEqual (Arg0, 1)) { Return (1) } Return (Multiply (Arg0, FACT (Subtract (Arg0, 1)))) }
    Method (FIB, 1) { If (LLess (Arg0, 2)) { Return (Arg0) } Return (Add (FIB (Subtract (Arg0, 1)), FIB (Subtract (Arg0, 2)))) }
    Method (SETR, 1) { Store (0x55, Arg0) }
    Method (INCR, 1) { Arg0 = DerefOf (Arg0) + 1 }
    Method (T001) { Return (Add (2, 3)) }
    Method (T002) { Local0 = 0xFFFFFFFFFFFFFFFF; Local0++; Return (Local0) }
    Method (T003) { Return (Subtract (3, 5)) }
    Method (T004) { Return (ShiftLeft (1, 40)) }
    Method (T005) { Divide (100, 7, Local1, Local0); Return (Add (Multiply (Local0, 1000), Local1)) }
    Method (T006) { Return (Mod (1000, 33)) }
    Method (T007) { Return (Or (And (0xF0F0, 0xFF00), Xor (0x0F, 0x3C))) }
    Method (T008) { Return (Not (0)) }
    Method (T009) { Return (NAnd (0xFF, 0x0F)) }
    Method (T010) { Return (NOr (0xF0, 0x0F)) }
    Method (T011) { Return (FindSetLeftBit (0x80010)) }
    Method (T012) { Return (FindSetRightBit (0x80010)) }
    Method (T013) { Return (ToBCD (1234)) }
    Method (T014) { Return (FromBCD (0x9876)) }
    Method (T015) { Return (ADD3 (10, 20, 30)) }
    Method (T016) { Return (FACT (10)) }
    Method (T017) { Return (FIB (15)) }
    Method (T018) { Local0 = 0; Local1 = 0; While (LLess (Local0, 100)) { Local0++; If (LEqual (Mod (Local0, 3), 0)) { Continue } Local1 += Local0; If (LGreater (Local0, 50)) { Break } } Return (Local1) }
    Method (T019) { Local0 = 7; Switch (Local0) { Case (1) { Return (10) } Case (Package () { 5, 6, 7 }) { Return (20) } Default { Return (30) } } }
    Method (T020) { Local0 = "abc"; Switch (Local0) { Case ("xyz") { Return (1) } Case ("abc") { Return (2) } } Return (3) }
    Method (T021) { Return (Concatenate ("Hello, ", "World")) }
    Method (T022) { Return (Concatenate ("v", 0x1F)) }
    Method (T023) { Return (Concatenate (Buffer () { 1, 2 }, Buffer () { 3 })) }
    Method (T024) { Return (Concatenate (0x0102, 0x0304)) }
    Method (T025) { Return (ToHexString (0xBEEF)) }
    Method (T026) { Return (ToDecimalString (987654)) }
    Method (T027) { Return (ToInteger ("0x1A2B")) }
    Method (T028) { Return (ToInteger ("4096")) }
    Method (T029) { Return (ToBuffer ("AB")) }
    Method (T030) { Return (ToString (Buffer () { 0x41, 0x42, 0x00, 0x43 }, Ones)) }
    Method (T031) { Return (Mid ("NoovexOS", 2, 4)) }
    Method (T032) { Return (Mid (Buffer () { 9, 8, 7, 6, 5 }, 1, 3)) }
    Method (T033) { Return (SizeOf (GPKG)) }
    Method (T034) { Return (SizeOf (GSTR)) }
    Method (T035) { Return (SizeOf (GBUF)) }
    Method (T036) { Return (DerefOf (Index (GPKG, 1))) }
    Method (T037) { Return (DerefOf (Index (DerefOf (Index (GPKG, 3)), 1))) }
    Method (T038) { Store (0x99, Index (GBUF, 2)); Return (GBUF) }
    Method (T039) { Return (DerefOf (Index (GBUF, 5))) }
    Method (T040) { Return (Match (BLVL, MEQ, 60, MTR, 0, 2)) }
    Method (T041) { Return (Match (BLVL, MGT, 65, MLT, 95, 2)) }
    Method (T042) { Return (Match (BLVL, MEQ, 33, MTR, 0, 0)) }
    Method (T043) { Name (BUF0, Buffer (16) {}) CreateDWordField (BUF0, 4, DW01) CreateWordField (BUF0, 0, WD00) CreateByteField (BUF0, 10, BY10) CreateBitField (BUF0, 127, BT7F)
        DW01 = 0x11223344; WD00 = 0xAABB; BY10 = 0x5A; BT7F = 1; Return (BUF0) }
    Method (T044) { Name (BUF1, Buffer (8) { 0xFF, 0x00, 0xF0, 0x0F, 0, 0, 0, 0 }) CreateField (BUF1, 4, 12, FL12) Return (FL12) }
    Method (T045) { Name (BUF2, Buffer (4) {}) CreateField (BUF2, 3, 9, FL09) FL09 = 0x1FF; Return (BUF2) }
    Method (T046) { Local0 = Package (3) { 1 }; Store (5, Index (Local0, 2)); Return (Add (DerefOf (Index (Local0, 0)), DerefOf (Index (Local0, 2)))) }
    Method (T047) { Local0 = 1; SETR (RefOf (Local0)); Return (Local0) }
    Method (T048) { CNT0 = 10; INCR (RefOf (CNT0)); INCR (RefOf (CNT0)); Return (CNT0) }
    Method (T049) { Local0 = RefOf (GINT); Return (DerefOf (Local0)) }
    Method (T050) { If (CondRefOf (\NOPE)) { Return (1) } If (CondRefOf (GINT, Local0)) { Return (Add (DerefOf (Local0), 1)) } Return (0) }
    Method (T051) { Return (Add (Add (ObjectType (GINT), Multiply (ObjectType (GSTR), 16)), Add (Multiply (ObjectType (GBUF), 256), Multiply (ObjectType (GPKG), 4096)))) }
    Method (T052) { Return (ObjectType (MUT0)) }
    Method (T053) { Acquire (MUT0, 0xFFFF); Local0 = 42; Release (MUT0); Return (Local0) }
    Method (T054) { Return (LAnd (LEqual ("abc", "abc"), LLess ("abc", "abd"))) }
    Method (T055) { Return (LOr (LGreater ("b", "abc"), 0)) }
    Method (T056) { Return (LNot (LEqual (Buffer () { 1, 2 }, Buffer () { 1, 2, 3 }))) }
    Method (T057) { Return (LNotEqual (5, 5)) }
    Method (T058) { Return (LGreaterEqual (5, 5)) }
    Method (T059) { Local0 = Buffer (4) { 1, 2, 3, 4 }; Local1 = 0; Local2 = SizeOf (Local0); While (Local2) { Local2--; Local1 = Add (ShiftLeft (Local1, 8), DerefOf (Index (Local0, Local2))) } Return (Local1) }
    Method (T060) { Local0 = 0; For (Local1 = 0, Local1 < 10, Local1++) { Local0 += Local1 * Local1 } Return (Local0) }
    Method (T061) { Return (_OSI ("Windows 2015")) }
    Method (T062) { Return (_OSI ("Linux")) }
    Method (T063) { Local0 = Package () { 0xA, "s", Package () { 1 } }; Return (Add (Multiply (ObjectType (Local0), 100), ObjectType (DerefOf (Index (Local0, 2))))) }
    Method (T064) { Local0 = GSTR; Local0 = Concatenate (Local0, "OS"); Return (Local0) }
    Method (T065) { Name (TINT, 5) TINT = "0x20"; Return (TINT) }
    Method (T066) { Name (TBUF, Buffer (3) { 0, 0, 0 }) TBUF = 0x0A0B0C0D; Return (TBUF) }
    Method (T067) { Name (TSTR, "x") TSTR = 0x41; Return (TSTR) }
    Method (T068) { Local0 = 0x123456789ABCDEF0; Return (ShiftRight (Local0, 36)) }
    Method (T069) { Return (Multiply (0x100000000, 0x100000000)) }
    Method (T070) { Local0 = 0; Return (Divide (10, Local0)) }
    Method (T071) { Local0 = Package () { "a", "b", "c" }; Local1 = ""; Local2 = 0; While (LLess (Local2, SizeOf (Local0))) { Local1 = Concatenate (Local1, DerefOf (Index (Local0, Local2))); Local2++ } Return (Local1) }
    Method (T072) { Return (Concatenate ("x", Buffer () { 1, 0xAB })) }
    Method (T073) { Return (ToHexString (Buffer () { 0x01, 0xAB, 0x10 })) }
    Method (T074) { Return (ToDecimalString (Buffer () { 1, 22, 255 })) }
    Method (T075) { Return (ToBuffer (0x0102030405060708)) }
    Method (T076) { Return (ToInteger (Buffer () { 0x78, 0x56, 0x34, 0x12 })) }
    Method (T077) { Local0 = 3; Local0 <<= 4; Local0 |= 1; Local0 ^= 0xFF; Return (Local0) }
    Method (T078) { Local0 = "ABCDEF"; Store (0x5A, Index (Local0, 1)); Return (Local0) }
    Method (T079) { Return (DerefOf (Index ("ABCDEF", 3))) }
    Method (T080) { Local0 = Package () { Package () { 1, 2 }, Package () { 3, 4 } }; Store (9, Index (DerefOf (Index (Local0, 1)), 0)); Return (DerefOf (Index (DerefOf (Index (Local0, 1)), 0))) }
    Method (T081) { Return (Timer () > 0) }
    Method (T082) { Local0 = 0; If (Local0) { Return (1) } ElseIf (LEqual (Local0, 0)) { Return (2) } Else { Return (3) } }
    Method (T083) { Return (Concatenate (Concatenate (ToHexString (0), ToHexString (0xF)), ToDecimalString (0))) }
    Method (T084) { Local0 = Buffer (2) { 0x34, 0x12 }; Return (Local0 + 1) }
    Method (T085) { Return ("5" + 3) }
    Method (T086) { Return (FIB (FACT (3))) }
    Method (T087) { Local0 = 0; Local1 = 1; Local2 = 2; Local3 = 3; Local4 = 4; Local5 = 5; Local6 = 6; Local7 = 7; Return (Local0 + Local1 + Local2 + Local3 + Local4 + Local5 + Local6 + Local7) }
    Method (T088, 7) { Return (Arg0 + Arg1 + Arg2 + Arg3 + Arg4 + Arg5 + Arg6) }
    Method (T089) { Return (T088 (1, 2, 4, 8, 16, 32, 64)) }
    Method (T090) { Local0 = Concatenate (GBUF, GBUF); Return (SizeOf (Local0)) }
}
