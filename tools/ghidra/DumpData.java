// Dumps qwords at an RVA, e.g. lookup tables.
//
// Args (positional): rva count [rva count ...]     e.g. 0x5C3420 32
// Run through scripts/ghidra.ps1 -Dump.
import ghidra.app.script.GhidraScript;

public class DumpData extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        String[] args = getScriptArgs();
        for (int i = 0; i + 1 < args.length; i += 2) {
            long rva = Long.decode(args[i]);
            int count = Integer.decode(args[i + 1]);
            println(String.format("=== DUMP 0x%X x %d qwords", rva, count));
            for (int q = 0; q < count; q++) {
                long v = getLong(toAddr(base + rva + q * 8L));
                println(String.format("  +0x%03X  %016X", q * 8, v));
            }
        }
    }
}
