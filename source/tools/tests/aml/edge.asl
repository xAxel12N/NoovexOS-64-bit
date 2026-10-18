DefinitionBlock ("", "DSDT", 2, "NVX", "EDGE", 1)
{
    Name (GLHD, Zero)
    Name (MHB0, Zero)
    Scope (\_SB)
    {
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_ADR, Zero)
            Name (_BBN, Zero)
            OperationRegion (HBUS, PCI_Config, Zero, 0x0100)
            Field (HBUS, DWordAcc, NoLock, Preserve) { Offset (0x48), MHBR, 32 }
            Method (GMHB, 0, NotSerialized) { Return (And (MHBR, 0xFFFF8000)) }
            Method (_INI, 0, NotSerialized) { MHB0 = GMHB () }
            Device (LPCB)
            {
                Name (_ADR, 0x001F0000)
                Device (EC0)
                {
                    Name (_HID, EisaId ("PNP0C09"))
                    Name (_CRS, ResourceTemplate () { IO (Decode16, 0x62, 0x62, 0, 1) IO (Decode16, 0x66, 0x66, 0, 1) })
                    Method (_GLK, 0, NotSerialized) { Return (One) }
                    OperationRegion (ERAM, EmbeddedControl, Zero, 0xFF)
                    Field (ERAM, ByteAcc, Lock, Preserve) { Offset (0x10), ECV0, 8, ECV1, 8 }
                    Method (RDEC, 0, NotSerialized) { Return (Add (ECV0, ECV1)) }
                    Method (_Q20, 0, NotSerialized) { ECV1 = 0x33 }
                }
            }
        }
    }
    Method (HANG, 0, NotSerialized) { While (One) { Stall (100) } }
    Method (SLPS, 0, NotSerialized) { While (One) { Sleep (10) } }
    Method (TMRL, 0, NotSerialized)
    {
        Local0 = Timer
        Local1 = Zero
        While (LLess (Subtract (Timer, Local0), 200000)) { Stall (50) Local1++ }
        Return (Local1)
    }
    Method (RETI, 0, NotSerialized) { Local0 = Package () { 1, 2, 3 } Return (Index (Local0, 1)) }
    Method (GLCK, 0, NotSerialized)
    {
        Acquire (\_GL, 0xFFFF)
        GLHD = \_SB.PCI0.LPCB.EC0.ECV0
        Release (\_GL)
        Return (GLHD)
    }
}
