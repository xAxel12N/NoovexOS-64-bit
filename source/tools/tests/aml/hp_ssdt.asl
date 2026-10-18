DefinitionBlock ("", "SSDT", 2, "HPQOEM", "BKLT", 1)
{
    External (\_SB.PCI0.GFX0, DeviceObj)
    External (\_SB.PCI0.LPCB.EC0.BRIT, FieldUnitObj)
    Scope (\_SB.PCI0.GFX0)
    {
        Device (DD1F)
        {
            Name (_ADR, 0x0110)
            Name (BCLV, Package (0x0D) { 100, 40, 1, 8, 16, 24, 32, 40, 48, 56, 64, 72, 100 })
            Method (_BCL, 0, NotSerialized) { Return (BCLV) }
            Method (_BCM, 1, NotSerialized)
            {
                Local0 = Match (BCLV, MEQ, Arg0, MTR, Zero, 2)
                If (LNotEqual (Local0, Ones)) { \_SB.PCI0.LPCB.EC0.BRIT = Multiply (Subtract (Local0, 2), 23) }
            }
            Method (_BQC, 0, NotSerialized)
            {
                Local0 = Divide (\_SB.PCI0.LPCB.EC0.BRIT, 23)
                Return (DerefOf (Index (BCLV, Add (Local0, 2))))
            }
        }
    }
}
