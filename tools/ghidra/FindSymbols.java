// Lists symbols whose qualified name contains each needle (case-insensitive),
// e.g. RTTI-recovered class namespaces and vftables. For a vftable it also
// lists the first slots as function RVAs.
//
// Args (positional): needle [needle ...]     e.g. ATTACK vftable
// Run through scripts/ghidra.ps1 -Symbols.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class FindSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        for (String needle : getScriptArgs()) {
            String lower = needle.toLowerCase();
            println("=== SYMBOLS containing \"" + needle + "\"");
            int hits = 0;
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Symbol s = it.next();
                String name = s.getName(true);
                if (!name.toLowerCase().contains(lower)) continue;
                if (++hits > 60) {
                    println("  ... more than 60 matches, refine the needle");
                    break;
                }
                long rva = s.getAddress().getOffset() - base;
                println(String.format("  0x%X %s (%s)", rva, name, s.getSymbolType()));
                if (s.getName().equals("vftable")) {
                    for (int i = 0; i < 16; i++) {
                        Address slot = s.getAddress().add(i * 8L);
                        long target = getLong(slot);
                        Function f = getFunctionAt(toAddr(target));
                        if (f == null) break;
                        println(String.format("    [%2d] +0x%02X -> %s@0x%X", i, i * 8,
                            f.getName(), target - base));
                    }
                }
            }
            if (hits == 0) println("  no matches");
        }
    }
}
