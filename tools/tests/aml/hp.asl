DefinitionBlock ("", "DSDT", 2, "HPQOEM", "8AB8", 1)
{
    External (\_SB.PCI0.GFX0.DD1F, DeviceObj)
    Name (BNKR, Zero)
    Name (IXRS, Zero)
    Name (ECRG, Zero)
    OperationRegion (CMS1, SystemIO, 0x72, 0x02)
    Field (CMS1, ByteAcc, NoLock, Preserve) { CMSI, 8, CMSD, 8 }
    IndexField (CMSI, CMSD, ByteAcc, NoLock, Preserve) { Offset (0x40), HPBD, 8, HPOS, 16 }
    OperationRegion (BKIO, SystemIO, 0x0500, 0x04)
    Field (BKIO, ByteAcc, NoLock, Preserve) { BSEL, 8, , 8, BDAT, 16 }
    BankField (BKIO, BSEL, 0x02, ByteAcc, NoLock, Preserve) { Offset (0x02), BK2V, 16 }
    Scope (\_SB)
    {
        Method (_INI, 0, NotSerialized)
        {
            HPOS = 0xBEEF
            IXRS = Add (HPBD, 1)
            BK2V = 0x1234
            BNKR = BSEL
        }
        Device (PCI0)
        {
            Name (_HID, EisaId ("PNP0A03"))
            Device (GFX0)
            {
                Name (_ADR, 0x00080001)
                Method (_DOS, 1, NotSerialized) { }
            }
            Device (LPCB)
            {
                Name (_ADR, 0x00140003)
                Device (EC0)
                {
                    Name (_HID, EisaId ("PNP0C09"))
                    Name (_GPE, 0x03)
                    Method (_REG, 2, NotSerialized) { If (LEqual (Arg0, 3)) { ECRG = Arg1 } }
                    OperationRegion (ERAM, EmbeddedControl, Zero, 0xFF)
                    Field (ERAM, ByteAcc, NoLock, Preserve)
                    {
                        SMPR, 8, SMST, 8, SMAD, 8, SMCM, 8, SMD0, 256, BCNT, 8,
                        Offset (0x2A), BRIT, 8,
                        Offset (0x40), ACIN, 1, BT1P, 1, BT2P, 1, Offset (0x41), B1ST, 8, B2ST, 8
                    }
                    Field (ERAM, ByteAcc, NoLock, Preserve) { Offset (0x04), SMW0, 16 }
                    Mutex (SMBX, 0)
                    Method (SMRD, 3, Serialized)
                    {
                        If (LNot (ECRG)) { Return (0xFF) }
                        Acquire (SMBX, 0xFFFF)
                        Local0 = 4
                        While (LGreater (Local0, One))
                        {
                            SMST = Zero
                            SMAD = ShiftLeft (Arg1, 1)
                            SMCM = Arg2
                            SMPR = Arg0
                            Local3 = 0x10
                            While (LAnd (LNot (And (SMST, 0x80)), Local3)) { Sleep (2); Local3-- }
                            If (LEqual (And (SMST, 0xBF), 0x80)) { Local0 = Zero }
                            Else { Local0-- }
                        }
                        Local1 = SMW0
                        Release (SMBX)
                        If (Local0) { Return (Ones) }
                        Return (Local1)
                    }
                    Method (_Q0A, 0, NotSerialized) { Notify (\_SB.PCI0.GFX0.DD1F, 0x86) }
                    Method (_Q0B, 0, NotSerialized) { Notify (\_SB.PCI0.GFX0.DD1F, 0x87) }
                    Method (_Q03, 0, NotSerialized) { Notify (ACAD, 0x80); Notify (BAT1, 0x80); Notify (BAT2, 0x80) }
                }
            }
        }
        Method (BIFX, 1, Serialized)
        {
            Name (BPKG, Package (0x0D) { One, 0, 0, One, 0, 0, 0, 0x40, 0x40, "", "", "LIon", "Hewlett-Packard" })
            Local0 = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x18)
            BPKG [One] = Local0
            BPKG [0x02] = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x10)
            BPKG [0x04] = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x19)
            BPKG [0x05] = Divide (Local0, 10)
            BPKG [0x06] = Divide (Local0, 25)
            BPKG [0x09] = Concatenate ("Primary", ToDecimalString (Arg0))
            Return (BPKG)
        }
        Method (BSTX, 2, Serialized)
        {
            Name (SPKG, Package (0x04) { 0, 0, 0, 0 })
            SPKG [Zero] = Arg1
            SPKG [One] = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x0B)
            SPKG [0x02] = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x0F)
            SPKG [0x03] = \_SB.PCI0.LPCB.EC0.SMRD (0x09, Arg0, 0x09)
            Return (SPKG)
        }
        Device (BAT1)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, One)
            Method (_STA, 0, NotSerialized) { If (\_SB.PCI0.LPCB.EC0.BT1P) { Return (0x1F) } Return (0x0F) }
            Method (_BIF, 0, NotSerialized) { Return (BIFX (0x0A)) }
            Method (_BST, 0, NotSerialized) { Return (BSTX (0x0A, \_SB.PCI0.LPCB.EC0.B1ST)) }
        }
        Device (BAT2)
        {
            Name (_HID, EisaId ("PNP0C0A"))
            Name (_UID, 2)
            Method (_STA, 0, NotSerialized) { If (\_SB.PCI0.LPCB.EC0.BT2P) { Return (0x1F) } Return (0x0F) }
            Method (_BIF, 0, NotSerialized) { Return (BIFX (0x0B)) }
            Method (_BST, 0, NotSerialized) { Return (BSTX (0x0B, \_SB.PCI0.LPCB.EC0.B2ST)) }
        }
        Device (ACAD)
        {
            Name (_HID, "ACPI0003")
            Method (_PSR, 0, NotSerialized) { Return (\_SB.PCI0.LPCB.EC0.ACIN) }
        }
    }
}
