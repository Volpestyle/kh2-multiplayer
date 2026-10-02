// Finds defined strings containing each needle (case-insensitive) and lists
// the code that references them, with the containing function.
//
// Args (positional): needle [needle ...]     e.g. "player attack" ATTACK@YS
// Run through scripts/ghidra.ps1 -Strings.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class FindStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        for (String needle : getScriptArgs()) {
            String lower = needle.toLowerCase();
            println("=== STRINGS containing \"" + needle + "\"");
            int hits = 0;
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object v = d.getValue();
                if (v == null || !v.toString().toLowerCase().contains(lower)) continue;
                if (++hits > 40) {
                    println("  ... more than 40 matches, refine the needle");
                    break;
                }
                println(String.format("  0x%X \"%s\"", d.getAddress().getOffset() - base,
                    v.toString().replace("\n", "\\n")));
                for (Reference ref : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(ref.getFromAddress());
                    println(String.format("    <- 0x%X in %s", ref.getFromAddress().getOffset() - base,
                        f == null ? "<none>" : String.format("%s@0x%X", f.getName(),
                            f.getEntryPoint().getOffset() - base)));
                }
            }
            if (hits == 0) println("  no matches");
        }
    }
}
