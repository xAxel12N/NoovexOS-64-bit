DefinitionBlock ("", "DSDT", 2, "LENOVO", "TP-R1X", 0x1300)
{
    OperationRegion (GNVS, SystemMemory, 0x20000000, 0x100)
    Field (GNVS, AnyAcc, Lock, Preserve)
    {
        OSYS, 16, BRTL, 8, DOSV, 8, INIC, 8, REGC, 8, IGDV, 16, RPDV, 16, DISI, 8, NBRT, 8, Q26C, 8, PCIR, 8
    }
    Name (ECON, Zero)
    Name (PWRS, One)
    Scope (\_SB)
    {
        Method (_INI, 0, NotSerialized)
        {
            OSYS = 0x07D0
            If (CondRefOf (\_OSI))
            {
                If (\_OSI ("Windows 2009")) { OSYS = 0x07D9 }
                If (\_OSI ("Windows 2015")) { OSYS = 0x07DF }
                If (\_OSI ("Linux")) { OSYS = 0x03E8 }
            }
            INIC++
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A08"))
            Name (_CID, EisaId ("PNP0A03"))
            Name (_UID, Zero)
            Name (_BBN, Zero)
            Device (GFX0)
            {
                Name (_ADR, 0x00020000)
                OperationRegion (IGDP, PCI_Config, Zero, 0x0100)
                Field (IGDP, AnyAcc, NoLock, Preserve) { VID0, 16, DID0, 16 }
                Method (_INI, 0, NotSerialized) { IGDV = VID0 }
                Method (_DOS, 1, NotSerialized) { DOSV = Arg0 }
                Device (LCD0)
                {
                    Name (_ADR, 0x0400)
                    Method (_BCL, 0, NotSerialized) { Return (Package () { 80, 50, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }) }
                    Method (_BCM, 1, NotSerialized) { BRTL = Arg0; NBRT++ }
                    Method (_BQC, 0, NotSerialized) { Return (BRTL) }
                }
            }
            Device (RP01)
            {
                Name (_ADR, 0x001C0000)
                Device (PXSX)
                {
                    Name (_ADR, Zero)
                    OperationRegion (PXCS, PCI_Config, Zero, 0x0100)
                    Field (PXCS, AnyAcc, NoLock, Preserve) { PVID, 16, PDID, 16 }
                    Method (_INI, 0, NotSerialized) { RPDV = PVID; PCIR++ }
                }
            }
            Device (DIS0)
            {
                Name (_HID, "NVX0001")
                Method (_STA, 0, NotSerialized) { Return (Zero) }
                Method (_INI, 0, NotSerialized) { DISI++ }
            }
            Device (LPCB)
            {
                Name (_ADR, 0x001F0000)
                Device (EC)
                {
                    Name (_HID, EisaId ("PNP0C09"))
                    Name (_UID, Zero)
                    Name (_GPE, 0x16)
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0062, 0x0062, 0x00, 0x01)
                        IO (Decode16, 0x0066, 0x0066, 0x00, 0x01)
                    })
                    Method (_REG, 2, NotSerialized)
                    {
                        If (LEqual (Arg0, 0x03)) { ECON = Arg1; REGC++ }
                    }
                    OperationRegion (ECOR, EmbeddedControl, Zero, 0x0100)
                    Field (ECOR, ByteAcc, Lock, Preserve)
                    {
                        Offset (0x30), HPAC, 1, , 3, HB0A, 1, HB1A, 1, Offset (0x31), HB0S, 8,
                        Offset (0x38), HB0C, 8,
                        Offset (0xA0), SBRC, 16, SBFC, 16, SBAE, 16, SBRS, 16, SBAC, 16, SBVO, 16, SBAF, 16, SBBS, 16,
                        Offset (0xC0), SBDC, 16, SBDV, 16, SBCC, 16
                    }
                    Mutex (BATM, 0x07)
                    Method (_Q11, 0, NotSerialized) { Notify (\_SB.PCI0.GFX0.LCD0, 0x86) }
                    Method (_Q12, 0, NotSerialized) { Notify (\_SB.PCI0.GFX0.LCD0, 0x87) }
                    Method (_Q26, 0, NotSerialized)
                    {
                        Q26C++
                        PWRS = HPAC
                        Notify (AC, 0x80)
                        Notify (BAT0, 0x80)
                    }
                    Method (_Q4C, 0, NotSerialized) { Notify (BAT0, 0x81) }
                    Device (BAT0)
                    {
                        Name (_HID, EisaId ("PNP0C0A"))
                        Name (_UID, Zero)
                        Name (BT0I, Package ()
                        {
                            Zero, Zero, 0xFFFFFFFF, 0xFFFFFFFF, One, 0x2D50, 0, 0, 0, 0, 0, 0, 0, 0, 0x20, 0x20,
                            "", "", "LION", "SMP"
                        })
                        Name (BT0P, Package () { Zero, Zero, Zero, Zero })
                        Method (_STA, 0, NotSerialized)
                        {
                            If (ECON) { If (HB0A) { Return (0x1F) } }
                            Return (0x0F)
                        }
                        Method (_BIX, 0, NotSerialized)
                        {
                            Acquire (BATM, 0xFFFF)
                            BT0I [0x02] = Multiply (SBDC, 10)
                            BT0I [0x03] = Multiply (SBFC, 10)
                            BT0I [0x05] = SBDV
                            BT0I [0x06] = Divide (Multiply (SBFC, 10), 20)
                            BT0I [0x07] = Divide (Multiply (SBFC, 10), 50)
                            BT0I [0x08] = SBCC
                            BT0I [0x10] = Concatenate ("L19M", ToDecimalString (3))
                            BT0I [0x11] = ToHexString (0x2A51)
                            Release (BATM)
                            Return (BT0I)
                        }
                        Method (_BST, 0, NotSerialized)
                        {
                            Acquire (BATM, 0xFFFF)
                            Local0 = HB0S
                            Local1 = SBAC
                            If (Local1 & 0x8000) { Local1 = And (Add (Not (Local1), One), 0xFFFF) }
                            Local1 = Divide (Multiply (Local1, SBVO), 1000)
                            BT0P [Zero] = Local0
                            BT0P [One] = Local1
                            BT0P [0x02] = Multiply (SBRC, 10)
                            BT0P [0x03] = SBVO
                            Release (BATM)
                            Return (BT0P)
                        }
                    }
                    Device (BAT1)
                    {
                        Name (_HID, EisaId ("PNP0C0A"))
                        Name (_UID, One)
                        Method (_STA, 0, NotSerialized)
                        {
                            If (ECON) { If (HB1A) { Return (0x1F) } }
                            Return (0x0F)
                        }
                        Method (_BIF, 0, NotSerialized) { Return (Package () { 0, 1, 1, 1, 1, 1, 1, 1, 1, "", "", "", "" }) }
                        Method (_BST, 0, NotSerialized) { Return (Package () { 0, 0, 0, 0 }) }
                    }
                    Device (AC)
                    {
                        Name (_HID, "ACPI0003")
                        Name (_UID, Zero)
                        Method (_PSR, 0, NotSerialized) { If (ECON) { Return (HPAC) } Return (PWRS) }
                    }
                }
            }
        }
    }
}
